import { useEffect, useRef, useState } from "react";
import { Sym } from "./Sym";
import {
  BLEND_LABELS,
  fillView,
  hasOwnVariation,
  hexToRgb,
  Json,
  Node,
  nodeAt,
  parseColor,
  Platform,
  Rendition,
  RENDITIONS,
  resolve,
  rgbToHex,
  Scope,
  Selection,
  srgbSpec,
  writeScope,
} from "./doc";

// O inspetor do alvo: `IconCompositionInspector` com as secoes Color (escopo:
// a rendicao), Liquid Glass e Composition (escopo: All) -- inventario §6 e §7.
// Toda escrita vai ao nucleo (`icf::setProperty` no icserver), que devolve o
// documento; nada e escrito no JSON deste lado.

export type Pane = "content" | "document";
// `coalesce`: o passo continua o anterior (um arraste) e nao abre entrada nova
// no desfazer.
export type Edit = (sel: Selection, scope: Scope, prop: string, value: unknown, coalesce?: boolean) => void;

type Props = {
  doc: Node | null;
  assets: string[];
  selection: Selection;
  rendition: Rendition;
  platform: Platform;
  pane: Pane;
  onPane: (p: Pane) => void;
  onEdit: Edit;
  onRendition: (r: Rendition) => void;
  onReplaceImage: () => void;
};

export function Inspector(p: Props) {
  const r = RENDITIONS.find((x) => x.id === p.rendition)!;
  const node = p.doc ? nodeAt(p.doc, p.selection) : null;
  const idiom = p.platform === "watchOS" ? "watchOS" : "iOS";
  const get = (prop: string, scoped: boolean) => (node ? resolve(node, prop, scoped ? r.appearance : "", idiom) : undefined);
  // `color`: a secao Color, que numa rendicao Dark/Mono escreve a variacao.
  const set = (prop: string, value: unknown, color: boolean, coalesce = false) => {
    if (!node) return;
    p.onEdit(p.selection, writeScope(node, prop, color ? r.appearance : "", idiom, color), prop, value, coalesce);
  };
  // O seletor do cabeçalho de Color: troca a rendicao (Default, Dark, Mono).
  const renditionPicker = (
    <ScopeMenu
      value={r.label}
      options={RENDITIONS.map((x) => x.label)}
      onChange={(label) => p.onRendition(RENDITIONS.find((x) => x.label === label)!.id)}
    />
  );

  return (
    <aside className="inspector">
      <div className="inspector-top" data-tauri-drag-region>
        <div className="capsule">
          <button className={`cap-btn${p.pane === "content" ? " on" : ""}`} title="Show or hide style options" onClick={() => p.onPane("content")}>
            <Sym name="paintbrush" size={16} />
          </button>
          <button className={`cap-btn${p.pane === "document" ? " on" : ""}`} title="Show or hide document options" onClick={() => p.onPane("document")}>
            <Sym name="document" size={16} />
          </button>
        </div>
      </div>

      <div className="inspector-body">
        {!p.doc || !node ? (
          <p className="muted center-note">No Selection</p>
        ) : p.pane === "document" ? (
          <DocumentPane doc={p.doc} onSet={(prop, v) => p.onEdit({ kind: "icon" }, { appearance: "", idiom: "" }, prop, v)} />
        ) : p.selection.kind === "icon" ? (
          <BackgroundPane get={get} set={set} picker={renditionPicker} varied={(prop) => hasOwnVariation(node, prop, r.appearance)} />
        ) : (
          <MemberPane
            node={node}
            isLayer={p.selection.kind === "layer"}
            get={get}
            set={set}
            assets={p.assets}
            picker={renditionPicker}
            varied={(prop) => hasOwnVariation(node, prop, r.appearance)}
            onReplaceImage={p.onReplaceImage}
          />
        )}
      </div>
    </aside>
  );
}

type Getter = (prop: string, scoped: boolean) => Json | undefined;
type Setter = (prop: string, value: unknown, color: boolean, coalesce?: boolean) => void;

// O item do alvo ("Replace...") no fim da lista de imagens do documento.
const REPLACE = "Replace…";

const LAYER_FILLS = ["Automatic", "None", "Solid", "Gradient"] as const;
const BACKGROUND_FILLS = ["Automatic", "Solid", "Gradient", "System Light", "System Dark"] as const;

// A cor corrente de um fill, para semear Solid/Gradient quando o tipo muda.
function seedColor(fill: Json | undefined): [number, number, number, number] {
  const f = fill as Record<string, unknown> | undefined;
  if (f && typeof f === "object") {
    if (typeof f.solid === "string") return parseColor(f.solid);
    const g = f["automatic-gradient"] ?? f["linear-gradient"];
    if (typeof g === "string") return parseColor(g);
    if (Array.isArray(g) && typeof g[0] === "string") return parseColor(g[0]);
  }
  return [0.2, 0.47, 0.96, 1];
}

function fillValue(kind: string, seed: [number, number, number, number]): unknown {
  switch (kind) {
    case "Automatic":
      return "automatic";
    case "None":
      return "none";
    case "System Light":
      return "system-light";
    case "System Dark":
      return "system-dark";
    case "Solid":
      return { solid: srgbSpec(...seed) };
    default:
      return { "automatic-gradient": srgbSpec(...seed) };
  }
}

function FillRows({
  get,
  set,
  kinds,
  varied,
}: {
  get: Getter;
  set: Setter;
  kinds: readonly string[];
  varied?: (prop: string) => boolean;
}) {
  const raw = get("fill", true);
  const fill = fillView(raw);
  const seed = seedColor(raw);
  const kind = fill.kind === "Gradient" ? "Gradient" : fill.kind;
  return (
    <>
      <Line
        icon={<Sym name="fill" custom size={16} />}
        label="Fill"
        varied={varied?.("fill")}
        onRemoveVariation={() => set("fill", null, true)}
      >
        <Select value={kind} options={kinds} onChange={(k) => set("fill", fillValue(k, seed), true)} />
      </Line>
      {fill.kind === "Solid" && (
        <div className="subline">
          <ColorWell
            color={seed}
            onChange={(c) => set("fill", { solid: srgbSpec(c[0], c[1], c[2], seed[3]) }, true)}
          />
          <NumberBox
            value={Math.round(seed[3] * 100)}
            unit="%"
            min={0}
            max={100}
            onChange={(v, c) => set("fill", { solid: srgbSpec(seed[0], seed[1], seed[2], v / 100) }, true, c)}
          />
        </div>
      )}
      {fill.kind === "Gradient" && <GradientRow raw={raw as Record<string, Json>} set={set} />}
    </>
  );
}

// O degrade do alvo: a cor de cima e a de baixo. `automatic-gradient` guarda
// so a de cima e a de baixo e automatica ("Use Automatic Color");
// `linear-gradient` guarda as duas. "Flip Gradient Colors" troca as duas. A
// `orientation` que o documento trouxer e mantida. Uma cor que nao foi mexida
// fica na grafia original (display-p3 continua display-p3).
function GradientRow({ raw, set }: { raw: Record<string, Json>; set: Setter }) {
  const lin = raw["linear-gradient"];
  const top = (Array.isArray(lin) ? lin[0] : raw["automatic-gradient"]) as string;
  const bottom = Array.isArray(lin) ? (lin[1] as string) : null;
  const orientation = raw.orientation !== undefined ? { orientation: raw.orientation } : {};
  const spec = (c: [number, number, number], alpha: number) => srgbSpec(c[0], c[1], c[2], alpha);
  const write = (t: string, b: string | null) =>
    set("fill", b === null ? { "automatic-gradient": t, ...orientation } : { "linear-gradient": [t, b], ...orientation }, true);
  const tc = parseColor(top);
  const bc = bottom ? parseColor(bottom) : null;
  return (
    <div className="subline gradient-row">
      <ColorWell color={tc} onChange={(c) => write(spec(c, tc[3]), bottom)} />
      {bc ? (
        <ColorWell color={bc} onChange={(c) => write(top, spec(c, bc[3]))} />
      ) : (
        <label className="well auto" title="Automatic color: click to choose one">
          A
          <input type="color" value={rgbToHex(tc[0], tc[1], tc[2])} onChange={(e) => write(top, spec(hexToRgb(e.target.value), 1))} />
        </label>
      )}
      {bottom && (
        <>
          <button className="mini-btn" title="Flip Gradient Colors" onClick={() => write(bottom, top)}>
            ⇅
          </button>
          <button className="mini-btn" title="Use Automatic Color" onClick={() => write(top, null)}>
            Auto
          </button>
        </>
      )}
    </div>
  );
}

function MemberPane({
  node,
  isLayer,
  get,
  set,
  assets,
  picker,
  varied,
  onReplaceImage,
}: {
  node: Node;
  isLayer: boolean;
  get: Getter;
  set: Setter;
  assets: string[];
  picker: React.ReactNode;
  varied: (prop: string) => boolean;
  onReplaceImage: () => void;
}) {
  const opacity = get("opacity", true);
  const blend = (get("blend-mode", true) as string) ?? "normal";
  const glass = get("glass", false);
  const hidden = get("hidden", false);
  const pos = get("position", false) as { scale?: number; "translation-in-points"?: number[] } | undefined;
  const scale = pos?.scale ?? 1;
  const tx = pos?.["translation-in-points"]?.[0] ?? 0;
  const ty = pos?.["translation-in-points"]?.[1] ?? 0;
  const setPos = (s: number, x: number, y: number, c?: boolean) =>
    set("position", { scale: s, "translation-in-points": [x, y] }, false, c);
  const image = get("image-name", false);
  const blendKeys = Object.keys(BLEND_LABELS);

  return (
    <>
      <Section title="Color" scope={picker}>
        <Line
          icon={<Sym name="Opacity" custom image size={16} />}
          label="Opacity"
          varied={varied("opacity")}
          onRemoveVariation={() => set("opacity", null, true)}
        >
          <NumberBox
            value={Math.round((typeof opacity === "number" ? opacity : 1) * 100)}
            unit="%"
            min={0}
            max={100}
            onChange={(v, c) => set("opacity", v / 100, true, c)}
          />
        </Line>
        <Line
          icon={<Sym name="Blendmode" custom image size={18} />}
          label="Blend Mode"
          varied={varied("blend-mode")}
          onRemoveVariation={() => set("blend-mode", null, true)}
        >
          <Select
            value={BLEND_LABELS[blend] ?? blend}
            options={blendKeys.map((k) => BLEND_LABELS[k])}
            onChange={(label) => set("blend-mode", blendKeys.find((k) => BLEND_LABELS[k] === label), true)}
          />
        </Line>
        {isLayer && <FillRows get={get} set={set} kinds={LAYER_FILLS} varied={varied} />}
      </Section>

      {isLayer ? (
        <Section title="Liquid Glass" scope="All">
          <Line icon={<Sym name="custom.fx.circle" custom size={16} />} label="Effects">
            <Toggle on={glass !== false} onChange={(on) => set("glass", on, false)} />
          </Line>
        </Section>
      ) : (
        <GroupGlass get={get} set={set} />
      )}

      <Section title="Composition" scope="All">
        <Line icon={<Sym name="eye" size={16} />} label="Visible">
          <Toggle on={hidden !== true} onChange={(on) => set("hidden", !on, false)} />
        </Line>
        {isLayer && (
          <Line icon={<Sym name="photo" size={16} />} label="Image">
            <Select
              value={typeof image === "string" ? image : "—"}
              options={[...(assets.length ? assets : [typeof image === "string" ? image : "—"]), REPLACE]}
              chevron="down"
              onChange={(name) => (name === REPLACE ? onReplaceImage() : set("image-name", name, false))}
            />
          </Line>
        )}
        <Line icon={<Sym name="arrow.up.left.and.down.right.and.arrow.up.right.and.down.left" size={15} />} label="Layout">
          <NumberBox prefix="x" value={tx} unit="pt" onChange={(v, c) => setPos(scale, v, ty, c)} />
          <NumberBox prefix="y" value={ty} unit="pt" onChange={(v, c) => setPos(scale, tx, v, c)} />
        </Line>
        <div className="subline">
          <Sym name="arrow.up.left.and.arrow.down.right" size={13} className="muted-sym" />
          <NumberBox value={Math.round(scale * 100)} unit="%" min={1} onChange={(v, c) => setPos(v / 100, tx, ty, c)} />
        </div>
      </Section>
      {!isLayer && node.name === undefined && null}
    </>
  );
}

// As linhas de vidro do GRUPO (`ShadowInspector`, `TranslucencyInspector`,
// `GroupSpecularInspector`, `BlurMaterialInspector`), na grafia do corpus.
function GroupGlass({ get, set }: { get: Getter; set: Setter }) {
  const shadow = (get("shadow", false) as { kind?: string; opacity?: number } | undefined) ?? {};
  const kind = shadow.kind ?? "neutral";
  const tr = (get("translucency", false) as { enabled?: boolean; value?: number } | undefined) ?? {};
  const specular = get("specular", false);
  const blur = get("blur-material", false);
  const KINDS: Record<string, string> = { neutral: "Neutral", "layer-color": "Chromatic", none: "None" };
  return (
    <Section title="Liquid Glass" scope="All">
      <Line icon={<Sym name="specular" custom size={16} />} label="Specular">
        <Toggle on={specular !== false && specular !== undefined} onChange={(on) => set("specular", on, false)} />
      </Line>
      <Line icon={<Sym name="blur" custom size={16} />} label="Blur">
        <NumberBox
          value={Math.round((typeof blur === "number" ? blur : 0) * 100)}
          unit="%"
          min={0}
          max={100}
          onChange={(v, c) => set("blur-material", v > 0 ? v / 100 : null, false, c)}
        />
      </Line>
      <Line icon={<Sym name="translucency" custom size={16} />} label="Translucency">
        <Toggle on={tr.enabled !== false} onChange={(on) => set("translucency", { enabled: on, value: tr.value ?? 0.5 }, false)} />
        <NumberBox
          value={Math.round((tr.value ?? 0.5) * 100)}
          unit="%"
          min={0}
          max={100}
          onChange={(v, c) => set("translucency", { enabled: tr.enabled !== false, value: v / 100 }, false, c)}
        />
      </Line>
      <Line icon={<Sym name="shadow" custom size={16} />} label="Shadow">
        <Select
          value={KINDS[kind] ?? kind}
          options={Object.values(KINDS)}
          onChange={(label) => {
            const k = Object.keys(KINDS).find((x) => KINDS[x] === label)!;
            set("shadow", { kind: k, opacity: shadow.opacity ?? 0.5 }, false);
          }}
        />
      </Line>
      {kind !== "none" && (
        <div className="subline">
          <NumberBox
            value={Math.round((shadow.opacity ?? 0.5) * 100)}
            unit="%"
            min={0}
            onChange={(v, c) => set("shadow", { kind, opacity: v / 100 }, false, c)}
          />
        </div>
      )}
    </Section>
  );
}

function BackgroundPane({
  get,
  set,
  picker,
  varied,
}: {
  get: Getter;
  set: Setter;
  picker: React.ReactNode;
  varied: (prop: string) => boolean;
}) {
  return (
    <Section title="Background" scope={picker}>
      <FillRows get={get} set={set} kinds={BACKGROUND_FILLS} varied={varied} />
    </Section>
  );
}

function DocumentPane({ doc, onSet }: { doc: Node; onSet: (prop: string, v: unknown) => void }) {
  const sp = (doc["supported-platforms"] ?? {}) as Node;
  const p3 = doc["color-space-for-untagged-svg-colors"] === "display-p3";
  return (
    <>
      <Section title="Platforms" scope="">
        <Line icon={<Sym name="ipad.and.iphone" custom size={16} />} label="iOS, macOS">
          <Select
            value={sp.squares === "shared" ? "Shared" : sp.squares ? "Unique" : "Off"}
            options={["Shared", "Off"]}
            onChange={(v) => {
              const next = { ...sp } as Node;
              if (v === "Shared") next.squares = "shared";
              else delete next.squares;
              onSet("supported-platforms", next);
            }}
          />
        </Line>
        <Line icon={<Sym name="circle" size={15} />} label="watchOS">
          <Toggle
            on={sp.circles !== undefined}
            onChange={(on) => {
              const next = { ...sp } as Node;
              if (on) next.circles = ["watchOS"];
              else delete next.circles;
              onSet("supported-platforms", next);
            }}
          />
        </Line>
      </Section>
      <Section title="SVG Colors" scope="">
        <Line icon={<Sym name="paintbrush" size={15} />} label="Use Display P3 if untagged">
          <Toggle on={p3} onChange={(on) => onSet("color-space-for-untagged-svg-colors", on ? "display-p3" : null)} />
        </Line>
      </Section>
    </>
  );
}

function Section({ title, scope, children }: { title: string; scope: React.ReactNode; children: React.ReactNode }) {
  return (
    <section className="isection">
      <header className="isection-head">
        <span>{title}</span>
        {typeof scope === "string" ? (
          scope && <span className="scope scope-fixed">{scope}</span>
        ) : (
          scope
        )}
      </header>
      <div className="isection-box">{children}</div>
    </section>
  );
}

// `varied`: a propriedade tem uma variacao PROPRIA na aparencia mostrada. A
// bolinha e o "Remove Variation" do alvo: volta a herdar do Default.
function Line({
  icon,
  label,
  varied,
  onRemoveVariation,
  children,
}: {
  icon: React.ReactNode;
  label: string;
  varied?: boolean;
  onRemoveVariation?: () => void;
  children: React.ReactNode;
}) {
  return (
    <div className="iline">
      <span className="iline-icon">{icon}</span>
      <span className="iline-label">
        {label}
        {varied && (
          <button className="varied" title="Remove Variation" onClick={onRemoveVariation} disabled={!onRemoveVariation} />
        )}
      </span>
      <span className="iline-value">{children}</span>
    </div>
  );
}

// O campo numerico do alvo (`Scrubbable` + `MultiNumericTextField`): arrastar na
// horizontal muda o valor, clicar sem arrastar abre para digitar.
function NumberBox({
  value,
  unit,
  prefix,
  min = -Infinity,
  max = Infinity,
  onChange,
}: {
  value: number;
  unit?: string;
  prefix?: string;
  min?: number;
  max?: number;
  onChange: (v: number, continuing?: boolean) => void;
}) {
  const [editing, setEditing] = useState(false);
  const [text, setText] = useState("");
  const drag = useRef<{ x: number; start: number; moved: boolean; sent: boolean } | null>(null);
  const clamp = (v: number) => Math.min(max, Math.max(min, v));
  const shown = Number.isInteger(value) ? String(value) : value.toFixed(2);

  const commit = () => {
    setEditing(false);
    const v = parseFloat(text.replace(",", "."));
    if (Number.isFinite(v) && v !== value) onChange(clamp(v));
  };

  if (editing) {
    return (
      <span className="numbox editing">
        {prefix && <span className="numbox-prefix">{prefix}</span>}
        <input
          autoFocus
          className="numbox-input"
          value={text}
          onChange={(e) => setText(e.target.value)}
          onBlur={commit}
          onKeyDown={(e) => {
            if (e.key === "Enter") commit();
            if (e.key === "Escape") setEditing(false);
          }}
        />
        {unit && <span className="numbox-unit">{unit}</span>}
      </span>
    );
  }
  return (
    <span
      className="numbox scrub"
      onPointerDown={(e) => {
        drag.current = { x: e.clientX, start: value, moved: false, sent: false };
        (e.currentTarget as Element).setPointerCapture(e.pointerId);
      }}
      onPointerMove={(e) => {
        const d = drag.current;
        if (!d) return;
        const dx = e.clientX - d.x;
        if (Math.abs(dx) > 2) d.moved = true;
        if (d.moved) {
          const next = clamp(Math.round(d.start + dx * (e.shiftKey ? 0.1 : 1)));
          if (next !== value) {
            // O primeiro passo do arraste abre a entrada no desfazer; os outros juntam.
            onChange(next, d.sent);
            d.sent = true;
          }
        }
      }}
      onPointerUp={() => {
        const d = drag.current;
        drag.current = null;
        if (d && !d.moved) {
          setText(shown);
          setEditing(true);
        }
      }}
    >
      {prefix && <span className="numbox-prefix">{prefix}</span>}
      <span className="numbox-value">{shown}</span>
      {unit && <span className="numbox-unit">{unit}</span>}
    </span>
  );
}

// O pop-up do alvo sobre um <select> nativo transparente: o menu e o do sistema.
function Select({
  value,
  options,
  onChange,
  chevron = "updown",
}: {
  value: string;
  options: readonly string[];
  onChange: (v: string) => void;
  chevron?: "updown" | "down";
}) {
  return (
    <span className="popup">
      <span>{value}</span>
      <span className={`popup-chev ${chevron}`}>
        {chevron === "down" ? <Sym name="chevron.down" size={9} /> : <Sym name="chevron.up.chevron.down" size={11} />}
      </span>
      <select className="popup-select" value={value} onChange={(e) => onChange(e.target.value)}>
        {!options.includes(value) && <option value={value}>{value}</option>}
        {options.map((o) => (
          <option key={o} value={o}>
            {o}
          </option>
        ))}
      </select>
    </span>
  );
}

function Toggle({ on, onChange }: { on: boolean; onChange: (on: boolean) => void }) {
  return (
    <button className={`toggle${on ? " on" : ""}`} onClick={() => onChange(!on)} role="switch" aria-checked={on}>
      <span className="knob" />
    </button>
  );
}

// As cores prontas do `IconColorPicker` ("Standard"): 15, nesta ordem `[BIN]`
// (Kit, init unico 0x5D838, cabecalho do array em 0x1864B0 = 15): as dez cores
// de sistema e cinco passos de `white.mix(with: black, by: t)`, t = 0...1. Os
// VALORES nao estao no binario -- o alvo resolve `Color.systemX` no macOS em
// aparencia clara, em tempo de execucao --, entao sao os da tabela publicada da
// Apple para o macOS claro `[INF]`.
const STANDARD_COLORS: string[] = [
  "#ff3b30", // systemRed
  "#ff9500", // systemOrange
  "#ffcc00", // systemYellow
  "#28cd41", // systemGreen
  "#00c7be", // systemMint
  "#007aff", // systemBlue
  "#55bef0", // systemCyan
  "#59adc4", // systemTeal
  "#5856d6", // systemIndigo
  "#af52de", // systemPurple
  "#ffffff",
  "#bfbfbf",
  "#808080",
  "#404040",
  "#000000",
];

// "Recent" (`RecentColorsManager`): as ultimas cores escolhidas, por pessoa, no
// armazenamento do navegador -- conveniencia, nao documento.
const RECENT_KEY = "recentIconColors";
// `RecentColorsManager.fixedSize = 10` `[BIN]` (Kit 0x5F1F0); o alvo guarda em
// UserDefaults "recentIconColors".
const RECENT_MAX = 10;
function readRecent(): string[] {
  try {
    const v = JSON.parse(localStorage.getItem(RECENT_KEY) ?? "[]");
    return Array.isArray(v) ? v.filter((x) => typeof x === "string") : [];
  } catch {
    return [];
  }
}
function pushRecent(hex: string) {
  try {
    const next = [hex, ...readRecent().filter((x) => x !== hex)].slice(0, RECENT_MAX);
    localStorage.setItem(RECENT_KEY, JSON.stringify(next));
  } catch {
    // sem armazenamento: as recentes so nao persistem
  }
}

// A amostra do alvo: clicar abre o seletor do sistema ("Opens system color
// picker"); a seta ao lado abre as cores prontas e as recentes ("Opens color
// preset menu"). A cor entra nas recentes quando o seletor do sistema fecha.
function ColorWell({ color, onChange }: { color: [number, number, number, number]; onChange: (c: [number, number, number]) => void }) {
  const [hex, setHex] = useState(rgbToHex(color[0], color[1], color[2]));
  const [menu, setMenu] = useState(false);
  const input = useRef<HTMLInputElement>(null);
  useEffect(() => setHex(rgbToHex(color[0], color[1], color[2])), [color[0], color[1], color[2]]);
  useEffect(() => {
    const el = input.current;
    if (!el) return;
    const onCommit = () => pushRecent(el.value);
    el.addEventListener("change", onCommit);
    return () => el.removeEventListener("change", onCommit);
  }, []);
  const pick = (h: string) => {
    setHex(h);
    onChange(hexToRgb(h));
    pushRecent(h);
    setMenu(false);
  };
  const recent = menu ? readRecent() : [];
  return (
    <span className="well-wrap">
      <label className="well" style={{ background: hex }} title="Opens system color picker">
        <input
          ref={input}
          type="color"
          value={hex}
          onChange={(e) => {
            setHex(e.target.value);
            onChange(hexToRgb(e.target.value));
          }}
        />
      </label>
      <button className="well-menu-btn" title="Opens color preset menu" onClick={() => setMenu((v) => !v)}>
        <Sym name="chevron.down" size={8} />
      </button>
      {menu && (
        <div className="color-menu" onMouseLeave={() => setMenu(false)}>
          {STANDARD_COLORS.length > 0 && (
            <>
              <span className="color-menu-title">Standard</span>
              <div className="color-grid">
                {STANDARD_COLORS.map((c) => (
                  <button key={c} className="color-cell" style={{ background: c }} title={c} onClick={() => pick(c)} />
                ))}
              </div>
            </>
          )}
          <span className="color-menu-title">Recent</span>
          <div className="color-grid">
            {recent.length ? (
              recent.map((c) => <button key={c} className="color-cell" style={{ background: c }} title={c} onClick={() => pick(c)} />)
            ) : (
              <span className="muted">—</span>
            )}
          </div>
        </div>
      )}
    </span>
  );
}

// O seletor de escopo do cabecalho, sobre o menu do sistema.
function ScopeMenu({ value, options, onChange }: { value: string; options: string[]; onChange: (v: string) => void }) {
  return (
    <span className="scope" title="Opens menu to choose variation type">
      {value} <Sym name="chevron.down" size={7} />
      <select className="popup-select" value={value} onChange={(e) => onChange(e.target.value)}>
        {options.map((o) => (
          <option key={o} value={o}>
            {o}
          </option>
        ))}
      </select>
    </span>
  );
}
