// A casca Tauri e so a ponte para o nucleo C++: nenhum pixel e calculado
// deste lado. O canvas fala com o `icserver` (core.rs), um processo persistente
// que guarda o dispositivo e o cache de render; `render_bundle` (o `icrender`
// por chamada) fica para o que ainda nao migrou.

mod core;

use base64::Engine;
use core::{Core, CoreState};
use std::path::PathBuf;
use std::process::Command;

// Onde mora o `icrender` e as DLLs do MinGW que ele carrega. Os padroes sao os
// desta maquina; as variaveis de ambiente trocam sem recompilar.
fn core_bin() -> PathBuf {
    std::env::var_os("IC_CORE_BIN")
        .map(PathBuf::from)
        .unwrap_or_else(|| {
            PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/release/Source/cli")
        })
}

fn mingw_bin() -> PathBuf {
    std::env::var_os("IC_MINGW_BIN")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from(r"C:\Strawberry\c\bin"))
}

#[derive(serde::Serialize)]
struct Rendered {
    png: String,   // data URL, pronto para um <img>
    report: String, // a linha "drawn in" e o que o nucleo disse no stderr
}

#[derive(serde::Serialize)]
struct OpenedDocument {
    name: String,
    json: String,        // o icon.json cru; o front le a arvore dele
    assets: Vec<String>, // os arquivos de Assets/
}

#[tauri::command]
fn open_document(path: String) -> Result<OpenedDocument, String> {
    let dir = PathBuf::from(&path);
    let json = std::fs::read_to_string(dir.join("icon.json"))
        .map_err(|e| format!("{}: sem icon.json legivel ({e})", dir.display()))?;
    let mut assets: Vec<String> = std::fs::read_dir(dir.join("Assets"))
        .map(|rd| {
            rd.filter_map(|e| e.ok())
                .filter(|e| e.path().is_file())
                .map(|e| e.file_name().to_string_lossy().into_owned())
                .collect()
        })
        .unwrap_or_default();
    assets.sort();
    let name = dir
        .file_name()
        .map(|n| n.to_string_lossy().into_owned())
        .unwrap_or_default();
    Ok(OpenedDocument { name, json, assets })
}

// A arte de uma camada, como data URL, para a miniatura da lista.
#[tauri::command]
fn read_asset(path: String, name: String) -> Result<String, String> {
    let file = PathBuf::from(&path).join("Assets").join(&name);
    let bytes = std::fs::read(&file).map_err(|e| format!("{}: {e}", file.display()))?;
    let mime = match file.extension().and_then(|e| e.to_str()).map(|e| e.to_ascii_lowercase()) {
        Some(ref e) if e == "svg" => "image/svg+xml",
        Some(ref e) if e == "png" => "image/png",
        Some(ref e) if e == "jpg" || e == "jpeg" => "image/jpeg",
        _ => "application/octet-stream",
    };
    Ok(format!(
        "data:{mime};base64,{}",
        base64::engine::general_purpose::STANDARD.encode(bytes)
    ))
}

#[tauri::command]
async fn render_bundle(
    path: String,
    size: u32,
    idiom: Option<String>,
    appearance: Option<String>,
) -> Result<Rendered, String> {
    // Um nome por chamada: a barra de rendicoes pede varias ao mesmo tempo.
    static SEQ: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
    let n = SEQ.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
    let out = std::env::temp_dir().join(format!("icon-composer-ui-{}-{n}.png", std::process::id()));
    let exe = core_bin().join("icrender.exe");
    let mut cmd = Command::new(&exe);
    cmd.arg(&path).arg("--out").arg(&out).arg("--size").arg(size.to_string());
    if let Some(i) = idiom.as_deref().filter(|s| !s.is_empty()) {
        cmd.arg("--idiom").arg(i);
    }
    if let Some(a) = appearance.as_deref().filter(|s| !s.is_empty()) {
        cmd.arg("--appearance").arg(a);
    }
    let path_var = std::env::var_os("PATH").unwrap_or_default();
    let mut paths = vec![mingw_bin()];
    paths.extend(std::env::split_paths(&path_var));
    cmd.env("PATH", std::env::join_paths(paths).map_err(|e| e.to_string())?);
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        cmd.creation_flags(0x0800_0000); // CREATE_NO_WINDOW: sem console piscando
    }
    let run = tauri::async_runtime::spawn_blocking(move || cmd.output())
        .await
        .map_err(|e| e.to_string())?
        .map_err(|e| format!("nao consegui abrir {}: {e}", exe.display()))?;
    let stdout = String::from_utf8_lossy(&run.stdout);
    let stderr = String::from_utf8_lossy(&run.stderr);
    if !run.status.success() {
        return Err(format!("icrender saiu com {}: {}{}", run.status, stdout, stderr));
    }
    let bytes = std::fs::read(&out).map_err(|e| format!("o PNG nao apareceu: {e}"))?;
    let _ = std::fs::remove_file(&out);
    let b64 = base64::engine::general_purpose::STANDARD.encode(bytes);
    Ok(Rendered {
        png: format!("data:image/png;base64,{b64}"),
        report: format!("{}{}", stdout.trim(), if stderr.trim().is_empty() { String::new() } else { format!("\n{}", stderr.trim()) }),
    })
}

fn with_core<T>(
    state: &CoreState,
    f: impl FnOnce(&mut Core) -> Result<T, String>,
) -> Result<T, String> {
    let mut guard = state.0.lock().map_err(|_| "o nucleo travou".to_string())?;
    if guard.is_none() {
        *guard = Some(Core::spawn(core_bin(), mingw_bin())?);
    }
    let result = f(guard.as_mut().unwrap());
    // Um processo que caiu nao volta sozinho: o proximo comando abre outro.
    if result.is_err() {
        *guard = None;
    }
    result
}

#[tauri::command]
async fn core_open(state: tauri::State<'_, CoreState>, path: String) -> Result<(), String> {
    let s = state.inner().clone();
    tauri::async_runtime::spawn_blocking(move || with_core(&s, |c| c.open(&path)))
        .await
        .map_err(|e| e.to_string())?
}

#[tauri::command]
async fn core_set_doc(state: tauri::State<'_, CoreState>, json: String) -> Result<(), String> {
    let s = state.inner().clone();
    tauri::async_runtime::spawn_blocking(move || with_core(&s, |c| c.set_doc(&json)))
        .await
        .map_err(|e| e.to_string())?
}

// O quadro volta como bytes crus (`ipc::Response`): o front recebe um
// ArrayBuffer, sem PNG e sem base64 no caminho.
#[tauri::command]
async fn core_render(
    state: tauri::State<'_, CoreState>,
    size: u32,
    appearance: String,
    idiom: String,
    tile: [i64; 4],
    subdivisions: u32,
) -> Result<tauri::ipc::Response, String> {
    let s = state.inner().clone();
    let bytes = tauri::async_runtime::spawn_blocking(move || {
        with_core(&s, |c| c.render(size, &appearance, &idiom, tile, subdivisions))
    })
    .await
    .map_err(|e| e.to_string())??;
    Ok(tauri::ipc::Response::new(bytes))
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_dialog::init())
        .manage(CoreState::default())
        .invoke_handler(tauri::generate_handler![
            open_document,
            read_asset,
            render_bundle,
            core_open,
            core_set_doc,
            core_render
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
