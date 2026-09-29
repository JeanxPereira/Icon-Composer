import { useCallback, useEffect, useState } from "react";
import { invoke } from "@tauri-apps/api/core";
import { open } from "@tauri-apps/plugin-dialog";
import { Sidebar } from "./Sidebar";
import { Background, Canvas, EffectsMode } from "./Canvas";
import { Inspector, Pane } from "./Inspector";
import { Node, Platform, Rendition, RENDITIONS, Selection, supportedPlatforms } from "./doc";
import { coreOpen, Frame, frameToDataUrl, requestFrame } from "./core";
import "./App.css";

type Opened = { name: string; json: string; assets: string[] };

const THUMB = 128;
// Acima disto o canvas estica; o proximo passo e pedir so o ladrilho visivel.
const MAX_CANVAS_PX = 2048;

export default function App() {
  const [path, setPath] = useState<string | null>(null);
  const [docName, setDocName] = useState("");
  const [doc, setDoc] = useState<Node | null>(null);
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
  const [thumbs, setThumbs] = useState<Record<string, string>>({});
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);

  const load = useCallback(async (p: string) => {
    try {
      const d = await invoke<Opened>("open_document", { path: p });
      const parsed = JSON.parse(d.json) as Node;
      await coreOpen(p);
      setFrame(null);
      setPath(p);
      setDocName(d.name);
      setDoc(parsed);
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

  // O canvas: o nucleo renderiza no tamanho dos pixels da tela (512 x zoom x
  // densidade), entao o zoom e um render novo, nao uma textura esticada.
  useEffect(() => {
    if (!path) return;
    let alive = true;
    const size = Math.min(MAX_CANVAS_PX, Math.round(512 * zoom * window.devicePixelRatio));
    setBusy(true);
    requestFrame("canvas", { size, appearance: appearanceOf(rendition), idiom: platform })
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
  }, [path, platform, rendition, zoom]);

  // As miniaturas da barra de rendicoes, do mesmo nucleo, depois do canvas.
  useEffect(() => {
    if (!path || !doc) return;
    let alive = true;
    setThumbs({});
    const jobs: [string, string, string][] = [
      ...RENDITIONS.map((r) => [`r:${r.id}`, platform, r.appearance] as [string, string, string]),
      ...supportedPlatforms(doc).map((pl) => [`p:${pl}`, pl, appearanceOf(rendition)] as [string, string, string]),
    ];
    for (const [key, idiom, appearance] of jobs) {
      requestFrame(`thumb:${key}`, { size: THUMB, idiom, appearance })
        .then((f) => alive && setThumbs((t) => ({ ...t, [key]: frameToDataUrl(f) })))
        .catch(() => {});
    }
    return () => {
      alive = false;
    };
  }, [path, doc, platform, rendition]);

  // Ctrl + roda = zoom, como no alvo.
  useEffect(() => {
    const onWheel = (e: WheelEvent) => {
      if (!e.ctrlKey) return;
      e.preventDefault();
      setZoom((z) => Math.min(4, Math.max(0.25, z * (e.deltaY < 0 ? 1.1 : 1 / 1.1))));
    };
    window.addEventListener("wheel", onWheel, { passive: false });
    return () => window.removeEventListener("wheel", onWheel);
  }, []);

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
        title={docName || "Icon Composer"}
        frame={frame}
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
        onZoom={setZoom}
        onOpen={pick}
        sidebarHidden={sidebarHidden}
        onToggleSidebar={() => setSidebarHidden(false)}
      />
      <Inspector doc={doc} selection={selection} rendition={rendition} platform={platform} pane={pane} onPane={setPane} />
    </div>
  );
}
