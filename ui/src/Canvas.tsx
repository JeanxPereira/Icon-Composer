import { useState } from "react";
import { Sym } from "./Sym";
import { Platform, Rendition, RENDITIONS, Selection } from "./doc";
import { Frame, LayerRect } from "./core";
import { Stage } from "./Stage";

// Centro da janela: a barra de ferramentas do alvo (inventario §2), o canvas
// (§4) e a barra de rendicoes no rodape (§5).

export type EffectsMode = "disabled" | "gen26" | "gen27";
export type Background = { kind: "solid"; color: string } | { kind: "image"; url: string };

// `BackgroundColorPopoverContent`: as cores prontas do fundo solido, mais o
// seletor do sistema.
const SOLID_COLORS = ["#ffffff", "#f2f2f4", "#c7c7cc", "#8e8e93", "#48484a", "#1e1e20", "#000000"];

export const PREVIEW_SIZES = [0, 1024, 256, 128, 64, 32] as const;

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
  rects: LayerRect[];
  selection: Selection;
  onSelect: (s: Selection) => void;
  onMove: (s: Selection, dx: number, dy: number, first: boolean) => void;
  snap: boolean;
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
  userBackgrounds: string[];
  onAddBackground: () => void;
  menubar: React.ReactNode;
  grid: boolean;
  onGrid: (g: boolean) => void;
  gridStyle: "light" | "dark";
  onGridStyle: (s: "light" | "dark") => void;
  zoom: number;
  onZoom: (z: number) => void;
  onOpen: () => void;
  sidebarHidden: boolean;
  onToggleSidebar: () => void;
};

export function Canvas(p: Props) {
  const [bgMenu, setBgMenu] = useState<"solid" | "image" | null>(null);
  // O ultimo de cada tipo, para o clique na amostra voltar a ele.
  const [lastSolid, setLastSolid] = useState("#1e1e20");
  const [lastImage, setLastImage] = useState(`/apple/backgrounds/${BACKGROUNDS[0]}`);
  const pickBackground = (b: Background) => {
    if (b.kind === "solid") setLastSolid(b.color);
    else setLastImage(b.url);
    p.onBackground(b);
  };
  const [menu, setMenu] = useState<"grid" | "size" | null>(null);
  const px = Math.round(512 * p.zoom);
  const previewLabel = (PREVIEW_SIZES as readonly number[]).includes(px) && px !== 512 ? `${px} pt` : "Full size";
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
        {p.menubar}
        <button className="doc-title" onClick={p.onOpen} title="Abrir .icon">
          {p.title}
        </button>
        <div className="toolbar-spacer" data-tauri-drag-region />

        <div className="capsule" title="Choose design generation or disable Liquid Glass effects">
          {(
            [
              ["disabled", "slash.circle", "Liquid Glass Effects Disabled"],
              ["gen26", "26.circle", "Design Generation 26 (este render ainda nao distingue 26 de 27)"],
              ["gen27", "27.circle", "Design Generation 27 (este render ainda nao distingue 26 de 27)"],
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

        {/* `BackgroundKindPickerButton`: o clique numa amostra ja selecionada
            abre o popover dela; numa nao selecionada, volta ao ultimo do tipo. */}
        <div className="capsule bg-chooser" title="Choose background">
          <button
            className={`swatch${p.background.kind === "solid" ? " on" : ""}`}
            title="Solid Color Background"
            style={{ background: lastSolid }}
            onClick={() =>
              p.background.kind === "solid"
                ? setBgMenu(bgMenu === "solid" ? null : "solid")
                : pickBackground({ kind: "solid", color: lastSolid })
            }
          />
          <button
            className={`swatch image${p.background.kind === "image" ? " on" : ""}`}
            title="Image Background"
            style={{ backgroundImage: `url("${lastImage}")` }}
            onClick={() =>
              p.background.kind === "image"
                ? setBgMenu(bgMenu === "image" ? null : "image")
                : pickBackground({ kind: "image", url: lastImage })
            }
          />
          {bgMenu === "solid" && (
            <div className="popover solid-pop" onMouseLeave={() => setBgMenu(null)}>
              {SOLID_COLORS.map((c) => (
                <button
                  key={c}
                  className={`color-dot${p.background.kind === "solid" && p.background.color === c ? " on" : ""}`}
                  style={{ background: c }}
                  onClick={() => pickBackground({ kind: "solid", color: c })}
                />
              ))}
              <label className="color-dot custom" title="Opens system color picker">
                <input
                  type="color"
                  value={p.background.kind === "solid" ? p.background.color : lastSolid}
                  onChange={(e) => pickBackground({ kind: "solid", color: e.target.value })}
                />
              </label>
            </div>
          )}
          {bgMenu === "image" && (
            <div className="popover" onMouseLeave={() => setBgMenu(null)}>
              {[...BACKGROUNDS.map((f) => `/apple/backgrounds/${f}`), ...p.userBackgrounds].map((url, i) => (
                <button
                  key={i}
                  className={`bg-tile${p.background.kind === "image" && p.background.url === url ? " on" : ""}`}
                  style={{ backgroundImage: `url("${url}")` }}
                  title={i < BACKGROUNDS.length ? BACKGROUNDS[i].replace(/^\d - /, "").replace(/\.jpeg$/, "") : "Custom"}
                  onClick={() => {
                    pickBackground({ kind: "image", url });
                    setBgMenu(null);
                  }}
                />
              ))}
              <button
                className="bg-tile add"
                title="Add Background…"
                onClick={() => {
                  setBgMenu(null);
                  p.onAddBackground();
                }}
              >
                <Sym name="plus" size={16} />
              </button>
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
          <button className="cap-btn narrow" title="Grid Style" onClick={() => setMenu(menu === "grid" ? null : "grid")}>
            <Sym name="chevron.down" size={10} />
          </button>
          {menu === "grid" && (
            <div className="menu down" onMouseLeave={() => setMenu(null)}>
              {(["light", "dark"] as const).map((st) => (
                <button
                  key={st}
                  className={p.gridStyle === st ? "checked" : ""}
                  onClick={() => {
                    p.onGridStyle(st);
                    p.onGrid(true);
                    setMenu(null);
                  }}
                >
                  {st === "light" ? "Light" : "Dark"}
                </button>
              ))}
            </div>
          )}
        </div>

        {/* `IconPreviewDimensionsSettings`: o icone no tamanho de uma prévia. "Full
            size" e o enquadramento; os outros poem o icone com N px na tela. */}
        <div className="capsule-wrap">
          <button className="capsule text-cap" title="Select preview size" onClick={() => setMenu(menu === "size" ? null : "size")}>
            {previewLabel} <Sym name="chevron.down" size={9} />
          </button>
          {menu === "size" && (
            <div className="menu down" onMouseLeave={() => setMenu(null)}>
              {PREVIEW_SIZES.map((n) => (
                <button
                  key={n}
                  className={previewLabel === (n ? `${n} pt` : "Full size") ? "checked" : ""}
                  onClick={() => {
                    p.onZoom(n ? n / 512 : 1);
                    setMenu(null);
                  }}
                >
                  {n ? `${n} pt` : "Full size"}
                </button>
              ))}
            </div>
          )}
        </div>

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
          gridStyle={p.gridStyle}
          platform={p.platform}
          zoom={p.zoom}
          onZoom={p.onZoom}
          onView={p.onView}
          rects={p.rects}
          selection={p.selection}
          onSelect={p.onSelect}
          onMove={p.onMove}
          snap={p.snap}
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
