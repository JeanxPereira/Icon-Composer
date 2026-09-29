import { useCallback, useEffect, useState } from "react";
import { invoke } from "@tauri-apps/api/core";
import { open } from "@tauri-apps/plugin-dialog";
import { Sidebar } from "./Sidebar";
import { Background, Canvas, EffectsMode } from "./Canvas";
import { Inspector, Pane } from "./Inspector";
import { Node, Platform, Rendition, RENDITIONS, Selection, supportedPlatforms } from "./doc";
import "./App.css";

type Opened = { name: string; json: string; assets: string[] };
type Rendered = { png: string; report: string };

// O canvas pede 1024 e mostra em 512 x zoom: nitido ate 200 %.
const CANVAS_RENDER = 1024;
const THUMB = 128;

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
  const [background, setBackground] = useState<Background>({ kind: "image", file: "6 - sine-gray.jpeg" });
  const [grid, setGrid] = useState(false);
  const [zoom, setZoom] = useState(1);

  const [image, setImage] = useState<string | null>(null);
  const [thumbs, setThumbs] = useState<Record<string, string>>({});
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);
  const [renderMs, setRenderMs] = useState<number | null>(null);

  const load = useCallback(async (p: string) => {
    try {
      const d = await invoke<Opened>("open_document", { path: p });
      const parsed = JSON.parse(d.json) as Node;
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

  // O render do canvas.
  useEffect(() => {
    if (!path) return;
    let live = true;
    setBusy(true);
    const t0 = performance.now();
    invoke<Rendered>("render_bundle", {
      path,
      size: CANVAS_RENDER,
      idiom: platform,
      appearance: appearanceOf(rendition),
    })
      .then((r) => {
        if (!live) return;
        setImage(r.png);
        setRenderMs(performance.now() - t0);
        setError("");
      })
      .catch((e) => live && setError(String(e)))
      .finally(() => live && setBusy(false));
    return () => {
      live = false;
    };
  }, [path, platform, rendition]);

  // As miniaturas da barra de rendicoes: as tres aparencias na plataforma
  // corrente, e as plataformas na aparencia corrente.
  useEffect(() => {
    if (!path || !doc) return;
    let live = true;
    setThumbs({});
    const jobs: [string, string, string][] = [
      ...RENDITIONS.map((r) => [`r:${r.id}`, platform, r.appearance] as [string, string, string]),
      ...supportedPlatforms(doc).map(
        (pl) => [`p:${pl}`, pl, appearanceOf(rendition)] as [string, string, string],
      ),
    ];
    for (const [key, idiom, appearance] of jobs) {
      invoke<Rendered>("render_bundle", { path, size: THUMB, idiom, appearance })
        .then((r) => live && setThumbs((t) => ({ ...t, [key]: r.png })))
        .catch(() => {});
    }
    return () => {
      live = false;
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

  return (
    <div className={`window${sidebarHidden ? " no-sidebar" : ""}`}>
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
        image={image}
        busy={busy}
        error={error}
        renderMs={renderMs}
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
      <Inspector
        doc={doc}
        selection={selection}
        rendition={rendition}
        platform={platform}
        pane={pane}
        onPane={setPane}
      />
    </div>
  );
}
