import { useEffect, useLayoutEffect, useRef, useState } from "react";
import { Sym } from "./Sym";
import { Platform, Rendition, RENDITIONS } from "./doc";
import { Frame } from "./core";

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
  const iconPx = Math.round(512 * p.zoom);
  const scroller = useRef<HTMLDivElement>(null);
  const wrap = useRef<HTMLDivElement>(null);
  const center = useRef<{ x: number; y: number } | null>(null);

  // O que se ve do quadrado do icone, em px CSS -- e o ladrilho que o App pede.
  const reportView = () => {
    const s = scroller.current, w = wrap.current;
    if (!s || !w) return;
    const sr = s.getBoundingClientRect(), wr = w.getBoundingClientRect();
    const x = Math.max(0, sr.left - wr.left), y = Math.max(0, sr.top - wr.top);
    const r = Math.min(wr.width, sr.right - wr.left), b = Math.min(wr.height, sr.bottom - wr.top);
    const s0 = scroller.current!;
    center.current = {
      x: (s0.scrollLeft + s0.clientWidth / 2) / Math.max(1, s0.scrollWidth),
      y: (s0.scrollTop + s0.clientHeight / 2) / Math.max(1, s0.scrollHeight),
    };
    if (r > x && b > y) p.onView({ x, y, w: r - x, h: b - y });
  };

  // O zoom segura o CENTRO da vista: depois de o quadrado crescer, a rolagem
  // volta a por no meio o mesmo ponto que estava no meio antes.
  useLayoutEffect(() => {
    const s = scroller.current;
    if (s && center.current) {
      s.scrollLeft = center.current.x * s.scrollWidth - s.clientWidth / 2;
      s.scrollTop = center.current.y * s.scrollHeight - s.clientHeight / 2;
    }
    reportView();
  }, [iconPx, p.frame !== null]);

  useEffect(() => {
    const onResize = () => reportView();
    window.addEventListener("resize", onResize);
    return () => window.removeEventListener("resize", onResize);
  }, []);

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
        <div className="stage-scroll" ref={scroller} onScroll={reportView}>
          {p.error ? (
            <pre className="error">{p.error}</pre>
          ) : p.frame ? (
            <div className="icon-wrap" ref={wrap} style={{ width: iconPx, height: iconPx }}>
              <FrameView frame={p.frame} cssSide={iconPx} />
              {p.tile && <FrameView frame={p.tile} cssSide={iconPx} />}
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

// O quadro do nucleo, pintado como veio: RGBA8, sem PNG no caminho. Ele se
// posiciona pela origem e pelo `size` do canvas para o qual foi pedido, entao
// um quadro de um zoom anterior fica no lugar certo (esticado) ate o novo chegar.
function FrameView({ frame, cssSide }: { frame: Frame; cssSide: number }) {
  const ref = useRef<HTMLCanvasElement>(null);
  useEffect(() => {
    const c = ref.current;
    if (!c) return;
    if (c.width !== frame.width || c.height !== frame.height) {
      c.width = frame.width;
      c.height = frame.height;
    }
    c.getContext("2d")!.putImageData(frame.image, 0, 0);
  }, [frame]);
  const k = cssSide / frame.size;
  return (
    <canvas
      ref={ref}
      className="frame"
      style={{ left: frame.originX * k, top: frame.originY * k, width: frame.width * k, height: frame.height * k }}
    />
  );
}
