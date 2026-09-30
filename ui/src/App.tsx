import { useCallback, useEffect, useRef, useState } from "react";
import { invoke } from "@tauri-apps/api/core";
import { open } from "@tauri-apps/plugin-dialog";
import { Sidebar } from "./Sidebar";
import { Background, Canvas, EffectsMode } from "./Canvas";
import { Edit, Inspector, Pane } from "./Inspector";
import { Node, Platform, Rendition, RENDITIONS, Selection, supportedPlatforms } from "./doc";
import { coreHistory, coreOpen, coreSave, coreSet, Frame, frameToDataUrl, requestFrame } from "./core";
import "./App.css";

type Opened = { name: string; json: string; assets: string[] };

const THUMB = 128;
// Ate aqui o canvas vem inteiro; acima, vem uma BASE inteira (esticada) e o
// LADRILHO visivel na resolucao da tela por cima.
const FULL_MAX_PX = 2048;
const BASE_PX = 1024;
const MAX_ZOOM = 16;

// Segmentos por cubica: 16 e o padrao do nucleo e basta a 100 %; com zoom, uma
// curva viraria poligono visivel.
const subdivisionsFor = (zoom: number) => Math.min(256, Math.max(16, Math.round(16 * zoom)));

export type ViewRect = { x: number; y: number; w: number; h: number }; // px CSS, no quadrado do icone

export default function App() {
  const [path, setPath] = useState<string | null>(null);
  const [docName, setDocName] = useState("");
  const [doc, setDoc] = useState<Node | null>(null);
  const [assets, setAssets] = useState<string[]>([]);
  // `rev` sobe a cada documento que o nucleo devolve: e o que pede um quadro novo.
  const [rev, setRev] = useState(0);
  const [dirty, setDirty] = useState(false);
  const edits = useRef<Promise<void>>(Promise.resolve());
  const [selection, setSelection] = useState<Selection>({ kind: "icon" });
  const [rendition, setRendition] = useState<Rendition>("default");
  const [platform, setPlatform] = useState<Platform>("iOS");
  const [pane, setPane] = useState<Pane>("content");
  const [sidebarHidden, setSidebarHidden] = useState(false);
  const [effects, setEffects] = useState<EffectsMode>("gen27");
  const [background, setBackground] = useState<Background>({ kind: "image", file: "1 - sine-purple-orange.jpeg" });
  const [grid, setGrid] = useState(false);
  const [zoom, setZoom] = useState(1);

  const [frame, setFrame] = useState<Frame | null>(null);
  const [tile, setTile] = useState<Frame | null>(null);
  const [view, setView] = useState<ViewRect | null>(null);
  const [thumbs, setThumbs] = useState<Record<string, string>>({});
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);

  const load = useCallback(async (p: string) => {
    try {
      const d = await invoke<Opened>("open_document", { path: p });
      const parsed = JSON.parse(d.json) as Node;
      await coreOpen(p);
      setFrame(null);
      setTile(null);
      setPath(p);
      setDocName(d.name);
      setDoc(parsed);
      setAssets(d.assets);
      setDirty(false);
      setRev((r) => r + 1);
      setSelection({ kind: "icon" });
      setError("");
      const plats = supportedPlatforms(parsed);
      setPlatform(plats[0] ?? "iOS");
    } catch (e) {
      setError(String(e));
    }
  }, []);

  const pick = async () => {
    // Um `.icon` e uma PASTA no Windows, entao o seletor e de diretorio.
    const chosen = await open({ directory: true, title: "Abrir .icon" });
    if (typeof chosen === "string") load(chosen);
  };

  const appearanceOf = (r: Rendition) => RENDITIONS.find((x) => x.id === r)!.appearance;

  // A escrita: em FILA, porque um arraste manda varias e elas tem de chegar ao
  // nucleo na ordem. Cada resposta e o documento inteiro, que vira o da UI.
  const adopt = (json: string) => {
    setDoc(JSON.parse(json) as Node);
    setRev((r) => r + 1);
  };
  const onEdit: Edit = (sel, scope, prop, value) => {
    const g = sel.kind === "icon" ? -1 : sel.g;
    const l = sel.kind === "layer" ? sel.l : -1;
    edits.current = edits.current
      .then(() => coreSet(g, l, scope, prop, value))
      .then((json) => {
        adopt(json);
        setDirty(true);
      })
      .catch((e) => setError(String(e)));
  };

  // Desfazer, refazer e salvar, pelo nucleo.
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (!e.ctrlKey || !path) return;
      const k = e.key.toLowerCase();
      if (k === "z" || k === "y") {
        const target = e.target as HTMLElement;
        if (target.tagName === "INPUT") return;
        e.preventDefault();
        const step = k === "y" || e.shiftKey ? "redo" : "undo";
        edits.current = edits.current
          .then(() => coreHistory(step))
          .then((json) => {
            adopt(json);
            setDirty(true);
          })
          .catch(() => {});
      } else if (k === "s") {
        e.preventDefault();
        edits.current = edits.current
          .then(() => coreSave())
          .then(() => setDirty(false))
          .catch((err) => setError(String(err)));
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [path]);

  const dpr = window.devicePixelRatio;
  const fullPx = Math.round(512 * zoom * dpr);
  const tiled = fullPx > FULL_MAX_PX;

  // O canvas inteiro: no tamanho da tela ate FULL_MAX_PX, ou a base de
  // BASE_PX quando o zoom passa disso (o ladrilho cobre o que se ve).
  useEffect(() => {
    if (!path) return;
    let alive = true;
    const size = tiled ? BASE_PX : fullPx;
    setBusy(true);
    requestFrame("canvas", { size, appearance: appearanceOf(rendition), idiom: platform, subdivisions: subdivisionsFor(size / (512 * dpr)) })
      .then((f) => {
        if (!alive) return;
        setFrame(f);
        setError("");
      })
      .catch((e) => alive && e !== "substituido" && setError(String(e)))
      .finally(() => alive && setBusy(false));
    return () => {
      alive = false;
    };
  }, [path, platform, rendition, fullPx, tiled, rev]);

  // O ladrilho: so a parte visivel, na resolucao da tela, pedida de novo a
  // cada rolagem ou zoom.
  useEffect(() => {
    if (!path || !tiled || !view) {
      setTile(null);
      return;
    }
    let alive = true;
    const x = Math.max(0, Math.floor(view.x * dpr));
    const y = Math.max(0, Math.floor(view.y * dpr));
    const w = Math.min(fullPx - x, Math.ceil(view.w * dpr));
    const h = Math.min(fullPx - y, Math.ceil(view.h * dpr));
    if (w <= 0 || h <= 0) return;
    const timer = window.setTimeout(() => {
      requestFrame("canvas-tile", {
        size: fullPx,
        appearance: appearanceOf(rendition),
        idiom: platform,
        tile: [x, y, w, h],
        subdivisions: subdivisionsFor(zoom),
      })
        .then((f) => alive && setTile(f))
        .catch((e) => alive && e !== "substituido" && setError(String(e)));
    }, 60);
    return () => {
      alive = false;
      window.clearTimeout(timer);
    };
  }, [path, platform, rendition, fullPx, tiled, view, zoom, rev]);

  // As miniaturas da barra de rendicoes, do mesmo nucleo, depois do canvas.
  useEffect(() => {
    if (!path || !doc) return;
    let alive = true;
    const jobs: [string, string, string][] = [
      ...RENDITIONS.map((r) => [`r:${r.id}`, platform, r.appearance] as [string, string, string]),
      ...supportedPlatforms(doc).map((pl) => [`p:${pl}`, pl, appearanceOf(rendition)] as [string, string, string]),
    ];
    // Espera a edicao assentar: um arraste nao refaz cinco miniaturas por passo.
    const timer = window.setTimeout(() => {
      for (const [key, idiom, appearance] of jobs) {
        requestFrame(`thumb:${key}`, { size: THUMB, idiom, appearance })
          .then((f) => alive && setThumbs((t) => ({ ...t, [key]: frameToDataUrl(f) })))
          .catch(() => {});
      }
    }, 250);
    return () => {
      alive = false;
      window.clearTimeout(timer);
    };
  }, [path, doc, platform, rendition]);

  // O fundo do viewport cobre a JANELA INTEIRA; a barra lateral e o inspetor
  // sao vidro fosco sobre ele, como no alvo.
  const backdrop =
    background.kind === "image"
      ? { backgroundImage: `url("/apple/backgrounds/${background.file}")` }
      : { background: background.tone === "dark" ? "#1e1e20" : "#f2f2f4" };

  return (
    <div className={`window${sidebarHidden ? " no-sidebar" : ""}`} style={backdrop}>
      {!sidebarHidden && (
        <Sidebar
          path={path}
          docName={docName}
          doc={doc}
          selection={selection}
          onSelect={setSelection}
          onToggleSidebar={() => setSidebarHidden(true)}
        />
      )}
      <Canvas
        title={docName ? `${docName}${dirty ? " — editado" : ""}` : "Icon Composer"}
        frame={frame}
        tile={tiled ? tile : null}
        onView={setView}
        busy={busy}
        error={error}
        thumbs={thumbs}
        rendition={rendition}
        onRendition={setRendition}
        platforms={doc ? supportedPlatforms(doc) : []}
        platform={platform}
        onPlatform={setPlatform}
        effects={effects}
        onEffects={setEffects}
        background={background}
        onBackground={setBackground}
        grid={grid}
        onGrid={setGrid}
        zoom={zoom}
        onZoom={(z) => setZoom(Math.min(MAX_ZOOM, Math.max(0.25, z)))}
        onOpen={pick}
        sidebarHidden={sidebarHidden}
        onToggleSidebar={() => setSidebarHidden(false)}
      />
      <Inspector
        doc={doc}
        assets={assets}
        selection={selection}
        rendition={rendition}
        platform={platform}
        pane={pane}
        onPane={setPane}
        onEdit={onEdit}
      />
    </div>
  );
}
