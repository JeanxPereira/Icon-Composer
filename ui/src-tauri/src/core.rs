// O nucleo C++ como processo persistente (`icserver`, protocolo no topo de
// Source/cli/server_main.cpp). Um processo por app: ele guarda o dispositivo
// Vulkan e o cache de render entre quadros. O Rust so passa comandos e bytes;
// nenhum pixel e calculado deste lado.

use std::io::{BufRead, BufReader, Read, Write};
use std::path::PathBuf;
use std::process::{Child, ChildStdin, ChildStdout, Command, Stdio};
use std::sync::{Arc, Mutex};

pub struct Core {
    child: Child,
    stdin: ChildStdin,
    stdout: BufReader<ChildStdout>,
}

#[derive(Default, Clone)]
pub struct CoreState(pub Arc<Mutex<Option<Core>>>);

fn read_line(r: &mut BufReader<ChildStdout>) -> Result<String, String> {
    let mut s = String::new();
    let n = r.read_line(&mut s).map_err(|e| e.to_string())?;
    if n == 0 {
        return Err("o icserver fechou".into());
    }
    Ok(s.trim_end().to_string())
}

impl Core {
    pub fn spawn(bin: PathBuf, mingw: PathBuf) -> Result<Core, String> {
        let exe = bin.join("icserver.exe");
        let mut cmd = Command::new(&exe);
        let path_var = std::env::var_os("PATH").unwrap_or_default();
        let mut paths = vec![mingw];
        paths.extend(std::env::split_paths(&path_var));
        cmd.env("PATH", std::env::join_paths(paths).map_err(|e| e.to_string())?);
        cmd.stdin(Stdio::piped()).stdout(Stdio::piped()).stderr(Stdio::null());
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            cmd.creation_flags(0x0800_0000); // CREATE_NO_WINDOW
        }
        let mut child = cmd
            .spawn()
            .map_err(|e| format!("nao consegui abrir {}: {e}", exe.display()))?;
        let stdin = child.stdin.take().ok_or("sem stdin")?;
        let mut stdout = BufReader::with_capacity(1 << 20, child.stdout.take().ok_or("sem stdout")?);
        let hello = read_line(&mut stdout)?;
        if hello != "ready" {
            return Err(format!("icserver: {hello}"));
        }
        Ok(Core { child, stdin, stdout })
    }

    // O processo ainda esta de pe. Um erro de pipe e o processo que morreu.
    pub fn alive(&mut self) -> bool {
        matches!(self.child.try_wait(), Ok(None))
    }

    fn send(&mut self, line: &str) -> Result<(), String> {
        self.stdin
            .write_all(format!("{line}\n").as_bytes())
            .and_then(|_| self.stdin.flush())
            .map_err(|e| e.to_string())
    }

    fn expect_ok(&mut self) -> Result<(), String> {
        let r = read_line(&mut self.stdout)?;
        if r == "ok" {
            Ok(())
        } else {
            Err(r.trim_start_matches("err ").to_string())
        }
    }

    pub fn open(&mut self, path: &str) -> Result<(), String> {
        self.send(&format!("open {path}"))?;
        self.expect_ok()
    }

    pub fn set_doc(&mut self, json: &str) -> Result<(), String> {
        self.stdin
            .write_all(format!("doc {}\n", json.len()).as_bytes())
            .and_then(|_| self.stdin.write_all(json.as_bytes()))
            .and_then(|_| self.stdin.flush())
            .map_err(|e| e.to_string())?;
        self.expect_ok()
    }

    fn read_json(&mut self) -> Result<String, String> {
        let head = read_line(&mut self.stdout)?;
        let n: usize = match head.strip_prefix("json ") {
            Some(n) => n.parse().map_err(|e: std::num::ParseIntError| e.to_string())?,
            None => return Err(head.trim_start_matches("err ").to_string()),
        };
        let mut buf = vec![0u8; n];
        self.stdout.read_exact(&mut buf).map_err(|e| e.to_string())?;
        String::from_utf8(buf).map_err(|e| e.to_string())
    }

    // Escreve `prop` sob o escopo (`icf::setProperty` no nucleo) e devolve o
    // icon.json inteiro depois da escrita.
    pub fn set(
        &mut self,
        group: i64,
        layer: i64,
        appearance: &str,
        idiom: &str,
        prop: &str,
        value: &str,
        coalesce: bool,
    ) -> Result<String, String> {
        let a = if appearance.is_empty() { "-" } else { appearance };
        let i = if idiom.is_empty() { "-" } else { idiom };
        self.stdin
            .write_all(format!("set {group} {layer} {a} {i} {prop} {}{}\n", value.len(), if coalesce { " c" } else { "" }).as_bytes())
            .and_then(|_| self.stdin.write_all(value.as_bytes()))
            .and_then(|_| self.stdin.flush())
            .map_err(|e| e.to_string())?;
        self.read_json()
    }

    // A estrutura (add-group, add-layer, remove, duplicate, move, rename).
    pub fn node(&mut self, op: &str, group: i64, layer: i64, arg: &str) -> Result<String, String> {
        self.stdin
            .write_all(format!("node {op} {group} {layer} {}\n", arg.len()).as_bytes())
            .and_then(|_| self.stdin.write_all(arg.as_bytes()))
            .and_then(|_| self.stdin.flush())
            .map_err(|e| e.to_string())?;
        self.read_json()
    }

    // Copia um arquivo para Assets/; devolve o nome que ele ganhou la.
    pub fn import(&mut self, file: &str) -> Result<String, String> {
        self.stdin
            .write_all(format!("import {}\n", file.len()).as_bytes())
            .and_then(|_| self.stdin.write_all(file.as_bytes()))
            .and_then(|_| self.stdin.flush())
            .map_err(|e| e.to_string())?;
        let r = read_line(&mut self.stdout)?;
        match r.strip_prefix("asset ") {
            Some(name) => Ok(name.to_string()),
            None => Err(r.trim_start_matches("err ").to_string()),
        }
    }

    pub fn history(&mut self, cmd: &str) -> Result<String, String> {
        self.send(cmd)?;
        self.read_json()
    }

    // O retangulo de cada camada em pontos do canvas (0..1024), como JSON.
    pub fn rects(&mut self, appearance: &str, idiom: &str) -> Result<String, String> {
        let a = if appearance.is_empty() { "-" } else { appearance };
        let i = if idiom.is_empty() { "-" } else { idiom };
        self.send(&format!("rects {a} {i}"))?;
        self.read_json()
    }

    pub fn save(&mut self) -> Result<(), String> {
        self.send("save")?;
        self.expect_ok()
    }

    // Devolve 24 bytes de cabecalho (w, h, originX, originY: u32/i32 LE; ms: f64
    // LE) seguidos do RGBA8. O front le com um DataView, sem base64 nem PNG.
    pub fn render(
        &mut self,
        size: u32,
        appearance: &str,
        idiom: &str,
        tile: [i64; 4],
        subdivisions: u32,
        effects: bool,
        tint: Option<[f64; 4]>,
    ) -> Result<Vec<u8>, String> {
        let a = if appearance.is_empty() { "-" } else { appearance };
        let i = if idiom.is_empty() { "-" } else { idiom };
        // Tinted Dark: `tint r g b saturation` no fim da linha.
        let t = tint
            .map(|t| format!(" tint {} {} {} {}", t[0], t[1], t[2], t[3]))
            .unwrap_or_default();
        self.send(&format!(
            "render {size} {a} {i} {} {} {} {} {subdivisions} {}{t}",
            tile[0], tile[1], tile[2], tile[3], if effects { 1 } else { 0 }
        ))?;
        let head = read_line(&mut self.stdout)?;
        let f: Vec<&str> = head.split(' ').collect();
        if f.first() != Some(&"frame") || f.len() < 7 {
            return Err(head.trim_start_matches("err ").to_string());
        }
        let parse = |s: &str| s.parse::<f64>().map_err(|e| e.to_string());
        let (w, h, ox, oy, ms, n) = (
            parse(f[1])? as u32,
            parse(f[2])? as u32,
            parse(f[3])? as i32,
            parse(f[4])? as i32,
            parse(f[5])?,
            parse(f[6])? as usize,
        );
        let mut out = Vec::with_capacity(24 + n);
        out.extend_from_slice(&w.to_le_bytes());
        out.extend_from_slice(&h.to_le_bytes());
        out.extend_from_slice(&ox.to_le_bytes());
        out.extend_from_slice(&oy.to_le_bytes());
        out.extend_from_slice(&ms.to_le_bytes());
        out.resize(24 + n, 0);
        self.stdout
            .read_exact(&mut out[24..])
            .map_err(|e| e.to_string())?;
        Ok(out)
    }
}

impl Drop for Core {
    fn drop(&mut self) {
        let _ = self.send("quit");
        let _ = self.child.wait();
    }
}
