import { useState } from "react";
import { Sym } from "./Sym";
import { Platform, Rendition, RENDITIONS } from "./doc";
import { Frame } from "./core";
import { Stage } from "./Stage";

// Centro da janela: a barra de ferramentas do alvo (inventario §2), o canvas
// (§4) e a barra de rendicoes no rodape (§5).

export type EffectsMode = "disabled" | "gen26" | "gen27";
export type Background = { kind: "solid"; tone: "light" | "dark" } | { kind: "image"; file: string };

export const BACKGROUNDS = [
  "1 - sine-purple-orange.jpeg",
  "2 - sine-gasflame.jpeg",
  "3 - sine-magenta.jpeg",
  "4 - sine-green-yellow.jpeg",
  "5 - sine-purple-orange-black.jpeg",
  "6 - sine-gray.jpeg",
];

type Props = {
  title: string;
  frame: Frame | null;
  tile: Frame | null;
  onView: (v: { x: number; y: number; w: number; h: number }) => void;
  busy: boolean;
  error: string;
  thumbs: Record<string, string>;
  rendition: Rendition;
  onRendition: (r: Rendition) => void;
  platforms: Platform[];
  platform: Platform;
  onPlatform: (p: Platform) => void;
  effects: EffectsMode;
  onEffects: (m: EffectsMode) => void;
  background: Background;
  onBackground: (b: Background) => void;
  grid: boolean;
  onGrid: (g: boolean) => void;
  zoom: number;
  onZoom: (z: number) => void;
  onOpen: () => void;
  sidebarHidden: boolean;
  onToggleSidebar: () => void;
};

export function Canvas(p: Props) {
  const [bgMenu, setBgMenu] = useState(false);
  const [hoverRendition, setHoverRendition] = useState<Rendition | null>(null);
  const caption = RENDITIONS.find((r) => r.id === (hoverRendition ?? p.rendition))!.label;
  return (
    <section className="center">
      <div className="toolbar" data-tauri-drag-region>
        {p.sidebarHidden && (
          <button className="round-btn" onClick={p.onToggleSidebar} title="Show Sidebar">
            <Sym name="sidebar.left" size={17} />
          </button>
        )}
        <button className="doc-title" onClick={p.onOpen} title="Abrir .icon">
          {p.title}
        </button>
        <div className="toolbar-spacer" data-tauri-drag-region />

        <div className="capsule" title="Choose design generation or disable Liquid Glass effects">
          {(
            [
              ["disabled", "slash.circle", "Liquid Glass Effects Disabled"],
              ["gen26", "26.circle", "Design Generation 26"],
              ["gen27", "27.circle", "Design Generation 27"],
            ] as const
          ).map(([id, sym, tip]) => (
            <button
              key={id}
              className={`cap-btn${p.effects === id ? " on" : ""}`}
              title={tip}
              onClick={() => p.onEffects(id)}
            >
              <Sym name={sym} size={17} />
            </button>
          ))}
        </div>

        <div className="capsule bg-chooser" title="Choose background">
          <button
            className={`swatch dark${p.background.kind === "solid" ? " on" : ""}`}
            title="Solid Color Background"
            onClick={() => p.onBackground({ kind: "solid", tone: "dark" })}
          />
          <button
            className={`swatch image${p.background.kind === "image" ? " on" : ""}`}
            title="Image Background"
            style={
              p.background.kind === "image"
                ? { backgroundImage: `url("/apple/backgrounds/${p.background.file}")` }
                : undefined
            }
            onClick={() => setBgMenu((v) => !v)}
          />
          {bgMenu && (
            <div className="popover" onMouseLeave={() => setBgMenu(false)}>
              {BACKGROUNDS.map((f) => (
                <button
                  key={f}
                  className="bg-tile"
                  style={{ backgroundImage: `url("/apple/backgrounds/${f}")` }}
                  title={f.replace(/^\d - /, "").replace(/\.jpeg$/, "")}
                  onClick={() => {
                    p.onBackground({ kind: "image", file: f });
                    setBgMenu(false);
                  }}
                />
              ))}
            </div>
          )}
        </div>

        <div className="capsule">
          <button
            className={`cap-btn${p.grid ? " on" : ""}`}
            title="Show or hide grid"
            onClick={() => p.onGrid(!p.grid)}
          >
            <Sym name={p.grid ? "toolbar-grid-on" : "toolbar-grid-off"} custom size={17} />
          </button>
          <button className="cap-btn narrow" title="Grid Style" disabled>
            <Sym name="chevron.down" size={10} />
          </button>
        </div>

        <button className="capsule text-cap" title="Select preview size" disabled>
          Full size <Sym name="chevron.down" size={9} />
        </button>

        <div className="capsule text-cap zoom" title="Change zoom level">
          <span>{Math.round(p.zoom * 100)}%</span>
          <span className="stepper">
            <button onClick={() => p.onZoom(p.zoom * 1.25)} aria-label="Zoom In">
              <Sym name="chevron.down" size={8} className="up" />
            </button>
            <button onClick={() => p.onZoom(p.zoom / 1.25)} aria-label="Zoom Out">
              <Sym name="chevron.down" size={8} />
            </button>
          </span>
        </div>
      </div>

      <div className="stage">
        <Stage
          docKey={p.title}
          frame={p.frame}
          tile={p.tile}
          grid={p.grid}
          platform={p.platform}
          zoom={p.zoom}
          onZoom={p.onZoom}
          onView={p.onView}
        />
        {p.error ? (
          <pre className="error stage-note">{p.error}</pre>
        ) : !p.frame ? (
          <button className="empty stage-note" onClick={p.onOpen}>
            Abrir um .icon
          </button>
        ) : null}

        {p.frame && (
          <div className="rendition-bar">
            <div className="rgroup">
              <span className="rcaption">{p.platforms.map((x) => (x === "iOS" ? "iOS, macOS" : x)).join(" · ")}</span>
              <div className="rthumbs">
                {p.platforms.map((pl) => (
                  <button
                    key={pl}
                    className={`rthumb${pl === p.platform ? " on" : ""}`}
                    title={`Previews icon for ${pl === "iOS" ? "iOS, macOS" : pl}`}
                    onClick={() => p.onPlatform(pl)}
                  >
                    {p.thumbs[`p:${pl}`] && <img src={p.thumbs[`p:${pl}`]} alt="" />}
                  </button>
                ))}
              </div>
            </div>
            <div className="rgroup">
              <span className="rcaption">{caption}</span>
              <div className="rthumbs">
                {RENDITIONS.map((r) => (
                  <button
                    key={r.id}
                    className={`rthumb${r.id === p.rendition ? " on" : ""}`}
                    title={`Previews icon for ${r.label}`}
                    onMouseEnter={() => setHoverRendition(r.id)}
                    onMouseLeave={() => setHoverRendition(null)}
                    onClick={() => p.onRendition(r.id)}
                  >
                    {p.thumbs[`r:${r.id}`] && <img src={p.thumbs[`r:${r.id}`]} alt="" />}
                  </button>
                ))}
              </div>
            </div>
          </div>
        )}
        <div className="stage-status">
          {p.frame ? `núcleo · ${p.frame.ms.toFixed(1)} ms${p.busy ? " · …" : ""}` : p.busy ? "renderizando…" : ""}
        </div>
      </div>
    </section>
  );
}
