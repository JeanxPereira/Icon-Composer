import { Sym } from "./Sym";
import {
  BLEND_LABELS,
  fillView,
  hasOwnVariation,
  Json,
  Node,
  nodeAt,
  Platform,
  Rendition,
  RENDITIONS,
  resolve,
  Selection,
} from "./doc";

// O inspetor do alvo: `IconCompositionInspector` com as secoes Color (escopo:
// a rendicao), Liquid Glass e Composition (escopo: All) -- inventario §6 e §7.
// So leitura neste passo: a escrita vem com o processo do nucleo.

export type Pane = "content" | "document";

type Props = {
  doc: Node | null;
  selection: Selection;
  rendition: Rendition;
  platform: Platform;
  pane: Pane;
  onPane: (p: Pane) => void;
};

export function Inspector({ doc, selection, rendition, platform, pane, onPane }: Props) {
  const r = RENDITIONS.find((x) => x.id === rendition)!;
  const node = doc ? nodeAt(doc, selection) : null;
  const idiom = platform === "watchOS" ? "watchOS" : "iOS";
  const get = (prop: string, scoped: boolean) =>
    node ? resolve(node, prop, scoped ? r.appearance : "", idiom) : undefined;

  return (
    <aside className="inspector">
      <div className="inspector-top" data-tauri-drag-region>
        <div className="capsule">
          <button
            className={`cap-btn${pane === "content" ? " on" : ""}`}
            title="Show or hide style options"
            onClick={() => onPane("content")}
          >
            <Sym name="paintbrush" size={16} />
          </button>
          <button
            className={`cap-btn${pane === "document" ? " on" : ""}`}
            title="Show or hide document options"
            onClick={() => onPane("document")}
          >
            <Sym name="document" size={16} />
          </button>
        </div>
      </div>

      <div className="inspector-body">
        {!doc ? (
          <p className="muted center-note">No Selection</p>
        ) : pane === "document" ? (
          <DocumentPane doc={doc} rendition={r.label} />
        ) : selection.kind === "icon" ? (
          <IconPane doc={doc} get={get} rendition={r.label} />
        ) : (
          <MemberPane
            node={node!}
            isLayer={selection.kind === "layer"}
            get={get}
            renditionLabel={r.label}
            scopedVariation={(prop) => hasOwnVariation(node!, prop, r.appearance)}
          />
        )}
      </div>
    </aside>
  );
}

type Getter = (prop: string, scoped: boolean) => Json | undefined;

function MemberPane({
  node,
  isLayer,
  get,
  renditionLabel,
  scopedVariation,
}: {
  node: Node;
  isLayer: boolean;
  get: Getter;
  renditionLabel: string;
  scopedVariation: (prop: string) => boolean;
}) {
  const opacity = get("opacity", true);
  const blend = get("blend-mode", true);
  const fill = fillView(get("fill", true));
  const glass = get("glass", false);
  const hidden = get("hidden", false);
  const pos = get("position", false) as { scale?: number; "translation-in-points"?: number[] } | undefined;
  const image = get("image-name", false);

  return (
    <>
      <Section title="Color" scope={renditionLabel}>
        <Line icon={<Sym name="Opacity" custom size={16} />} label="Opacity" varied={scopedVariation("opacity")}>
          <NumberBox value={Math.round((typeof opacity === "number" ? opacity : 1) * 100)} unit="%" />
        </Line>
        <Line icon={<Sym name="Blendmode" custom size={16} />} label="Blend Mode" varied={scopedVariation("blend-mode")}>
          <PopUp value={BLEND_LABELS[(blend as string) ?? "normal"] ?? String(blend)} />
        </Line>
        <Line icon={<Sym name="fill" custom size={16} />} label="Fill" varied={scopedVariation("fill")}>
          <PopUp value={fill.kind} />
        </Line>
        {fill.kind === "Solid" && (
          <div className="subline">
            <span className="well" style={{ background: fill.color }} />
            <span className="mini-pop">
              <Sym name="chevron.down" size={9} />
            </span>
            <NumberBox value={Math.round(fill.alpha * 100)} unit="%" />
          </div>
        )}
        {fill.kind === "Gradient" && (
          <div className="subline">
            {fill.colors.map((c, i) => (
              <span key={i} className="well small" style={{ background: c }} />
            ))}
          </div>
        )}
      </Section>

      <Section title="Liquid Glass" scope="All">
        <Line icon={<Sym name="custom.fx.circle" custom size={16} />} label="Effects">
          <Toggle on={glass !== false} />
        </Line>
      </Section>

      <Section title="Composition" scope="All">
        <Line icon={<Sym name="eye" size={16} />} label="Visible">
          <Toggle on={hidden !== true} />
        </Line>
        {isLayer && (
          <>
            <Line icon={<Sym name="photo" size={16} />} label="Image">
              <PopUp value={typeof image === "string" ? image : "—"} chevron="down" />
            </Line>
            <Line
              icon={<Sym name="arrow.up.left.and.down.right.and.arrow.up.right.and.down.left" size={15} />}
              label="Layout"
            >
              <NumberBox prefix="x" value={pos?.["translation-in-points"]?.[0] ?? 0} unit="pt" />
              <NumberBox prefix="y" value={pos?.["translation-in-points"]?.[1] ?? 0} unit="pt" />
            </Line>
            <div className="subline">
              <Sym name="arrow.up.left.and.arrow.down.right" size={13} className="muted-sym" />
              <NumberBox value={Math.round((pos?.scale ?? 1) * 100)} unit="%" />
            </div>
          </>
        )}
      </Section>
      {!isLayer && (
        <p className="muted small-note">
          {node.name ? String(node.name) : "Group"}: sombra, translucidez, especular e refração
          entram aqui na próxima volta.
        </p>
      )}
    </>
  );
}

function IconPane({ doc, get, rendition }: { doc: Node; get: Getter; rendition: string }) {
  const fill = fillView(get("fill", true));
  return (
    <Section title="Background" scope={rendition}>
      <Line icon={<Sym name="fill" custom size={16} />} label="Fill">
        <PopUp value={fill.kind} />
      </Line>
      {fill.kind === "Gradient" && (
        <div className="subline">
          {fill.colors.map((c, i) => (
            <span key={i} className="well small" style={{ background: c }} />
          ))}
        </div>
      )}
      <p className="muted small-note">{(doc.groups as Json[] | undefined)?.length ?? 0} grupos</p>
    </Section>
  );
}

function DocumentPane({ doc, rendition }: { doc: Node; rendition: string }) {
  const sp = (doc["supported-platforms"] ?? {}) as Node;
  const squares = sp.squares;
  const circles = sp.circles;
  const p3 = doc["color-space-for-untagged-svg-colors"] === "display-p3";
  return (
    <>
      <Section title="Platforms" scope="">
        <Line icon={<Sym name="ipad.and.iphone" custom size={16} />} label="iOS, macOS">
          <PopUp value={squares === "shared" ? "Shared" : squares ? "Unique" : "Off"} />
        </Line>
        <Line icon={<Sym name="circle" size={15} />} label="watchOS">
          <Toggle on={circles !== undefined} />
        </Line>
      </Section>
      <Section title="SVG Colors" scope="">
        <Line icon={<Sym name="paintbrush" size={15} />} label="Use Display P3 if untagged">
          <Toggle on={p3} />
        </Line>
      </Section>
      <Section title="Languages" scope="">
        <p className="muted small-note">Localização: ainda não ({rendition}).</p>
      </Section>
    </>
  );
}

function Section({ title, scope, children }: { title: string; scope: string; children: React.ReactNode }) {
  return (
    <section className="isection">
      <header className="isection-head">
        <span>{title}</span>
        {scope && (
          <span className="scope" title="Opens menu to choose variation type">
            {scope} <Sym name="chevron.down" size={7} />
          </span>
        )}
      </header>
      <div className="isection-box">{children}</div>
    </section>
  );
}

function Line({
  icon,
  label,
  varied,
  children,
}: {
  icon: React.ReactNode;
  label: string;
  varied?: boolean;
  children: React.ReactNode;
}) {
  return (
    <div className="iline">
      <span className="iline-icon">{icon}</span>
      <span className="iline-label">
        {label}
        {varied && <span className="varied" title="Variation" />}
      </span>
      <span className="iline-value">{children}</span>
    </div>
  );
}

function NumberBox({ value, unit, prefix }: { value: number; unit?: string; prefix?: string }) {
  return (
    <span className="numbox">
      {prefix && <span className="numbox-prefix">{prefix}</span>}
      <span className="numbox-value">{Number.isInteger(value) ? value : value.toFixed(2)}</span>
      {unit && <span className="numbox-unit">{unit}</span>}
    </span>
  );
}

function PopUp({ value, chevron = "updown" }: { value: string; chevron?: "updown" | "down" }) {
  return (
    <span className="popup">
      <span>{value}</span>
      <span className={`popup-chev ${chevron}`}>
        {chevron === "down" ? (
          <Sym name="chevron.down" size={9} />
        ) : (
          <Sym name="chevron.up.chevron.down" size={11} />
        )}
      </span>
    </span>
  );
}

function Toggle({ on }: { on: boolean }) {
  return (
    <span className={`toggle${on ? " on" : ""}`}>
      <span className="knob" />
    </span>
  );
}
