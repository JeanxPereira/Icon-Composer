import { useState } from "react";
import { useCallback } from "react";
import { Sym } from "./Sym";
import { Node, Platform, Rendition, RENDITIONS } from "./doc";
import { LiveIcon } from "./live/LiveIcon";

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
  path: string | null;
  doc: Node | null;
  live: boolean;
  onLive: (v: boolean) => void;
  title: string;
  image: string | null;
  busy: boolean;
  error: string;
  renderMs: number | null;
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
  const [liveMs, setLiveMs] = useState<number | null>(null);
  const onDrawn = useCallback((ms: number) => setLiveMs(ms), []);
  const appearance = RENDITIONS.find((r) => r.id === p.rendition)!.appearance;
  const caption = RENDITIONS.find((r) => r.id === (hoverRendition ?? p.rendition))!.label;
  const iconPx = Math.round(512 * p.zoom);

  const bgStyle =
    p.background.kind === "image"
      ? { backgroundImage: `url("/apple/backgrounds/${p.background.file}")` }
      : { background: p.background.tone === "dark" ? "#1e1e20" : "#f2f2f4" };

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
            <button onClick={() => p.onZoom(Math.min(4, p.zoom + 0.25))} aria-label="Zoom In">
              <Sym name="chevron.down" size={8} className="up" />
            </button>
            <button onClick={() => p.onZoom(Math.max(0.25, p.zoom - 0.25))} aria-label="Zoom Out">
              <Sym name="chevron.down" size={8} />
            </button>
          </span>
        </div>
      </div>

      <div className="stage" style={bgStyle}>
        <div className="stage-scroll">
          {p.error ? (
            <pre className="error">{p.error}</pre>
          ) : p.live && p.path && p.doc ? (
            <div className="icon-wrap" style={{ width: iconPx, height: iconPx }}>
              <LiveIcon path={p.path} doc={p.doc} appearance={appearance} idiom={p.platform} cssSize={iconPx} onDrawn={onDrawn} />
              {p.grid && (
                <img
                  className="grid-overlay"
                  src={`/apple/custom/${p.platform === "watchOS" ? "appicongrid.watchos" : "appicongrid.ios"}.svg`}
                  alt=""
                />
              )}
            </div>
          ) : p.image ? (
            <div className="icon-wrap" style={{ width: iconPx, height: iconPx }}>
              <img className="icon" src={p.image} alt="" />
              {p.grid && (
                <img
                  className="grid-overlay"
                  src={`/apple/custom/${p.platform === "watchOS" ? "appicongrid.watchos" : "appicongrid.ios"}.svg`}
                  alt=""
                />
              )}
            </div>
          ) : (
            <button className="empty" onClick={p.onOpen}>
              Abrir um .icon
            </button>
          )}
        </div>

        {(p.image || (p.live && p.doc)) && (
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
        {p.doc && (
          <button
            className="stage-status"
            title="Alterna entre o render ao vivo (navegador, sem vidro ainda) e o render fiel do nucleo (CPU)"
            onClick={() => p.onLive(!p.live)}
          >
            {p.live
              ? `ao vivo · ${liveMs !== null ? liveMs.toFixed(1) : "–"} ms · sem vidro`
              : p.busy
                ? "fiel · renderizando…"
                : `fiel · ${p.renderMs !== null ? Math.round(p.renderMs) : "–"} ms`}
          </button>
        )}
      </div>
    </section>
  );
}
