// O documento como a UI o le. So leitura por enquanto: a escrita vai passar
// pelo nucleo C++ (o `icserver` do passo 2), que ja faz a escrita por escopo
// com round-trip byte-exato. Aqui se resolve o que o inspetor MOSTRA.

export type Json = null | boolean | number | string | Json[] | { [k: string]: Json };
export type Node = { [k: string]: Json };

export type Rendition = "default" | "dark" | "mono";
export type Platform = "iOS" | "watchOS";

export const RENDITIONS: { id: Rendition; label: string; appearance: string }[] = [
  { id: "default", label: "Default", appearance: "" },
  { id: "dark", label: "Dark", appearance: "dark" },
  // Mono le a fatia `tinted` do documento (laudo de 19/09 e inventario §0).
  { id: "mono", label: "Mono", appearance: "tinted" },
];

export type Selection =
  | { kind: "icon" }
  | { kind: "group"; g: number }
  | { kind: "layer"; g: number; l: number };

export function groups(doc: Node): Node[] {
  return Array.isArray(doc.groups) ? (doc.groups as Node[]) : [];
}

export function layers(group: Node): Node[] {
  return Array.isArray(group.layers) ? (group.layers as Node[]) : [];
}

export function nodeAt(doc: Node, sel: Selection): Node {
  if (sel.kind === "icon") return doc;
  const g = groups(doc)[sel.g] ?? {};
  return sel.kind === "group" ? g : layers(g)[sel.l] ?? {};
}

// A regra do nucleo (`icf::resolve`): se existe a lista, ela ganha da chave
// simples; dentro dela, a entrada cujo predicado casa com mais campos ganha, e
// a entrada sem predicado e o default.
export function resolve(
  node: Node,
  prop: string,
  appearance: string,
  idiom: string,
): Json | undefined {
  const list = node[`${prop}-specializations`];
  if (Array.isArray(list)) {
    let best: Json | undefined;
    let bestScore = -1;
    for (const e of list as Node[]) {
      const a = e.appearance as string | undefined;
      const i = e.idiom as string | undefined;
      if (a && a !== appearance) continue;
      if (i && i !== idiom && !(i === "square" && idiom !== "watchOS")) continue;
      const score = (a ? 2 : 0) + (i ? 1 : 0);
      if (score > bestScore) {
        bestScore = score;
        best = e.value;
      }
    }
    return best;
  }
  return node[prop];
}

// Uma entrada PROPRIA deste escopo existe? E o que o inspetor marca como
// "variacao" em vez de herdado.
export function hasOwnVariation(node: Node, prop: string, appearance: string): boolean {
  const list = node[`${prop}-specializations`];
  if (!Array.isArray(list) || !appearance) return false;
  return (list as Node[]).some((e) => e.appearance === appearance);
}

export function displayName(node: Node, fallback: string): string {
  const n = node.name;
  if (typeof n === "string" && n) return n;
  const img = node["image-name"];
  if (typeof img === "string" && img) return img.replace(/\.(svg|png)$/i, "");
  return fallback;
}

// A cor em numeros, como o nucleo a usa: componentes tomados como sRGB (o
// nucleo tambem nao converte P3 -- ele anota a falta).
export function parseColor(spec: string): [number, number, number, number] {
  const [space, rest] = spec.split(":");
  const v = (rest ?? "").split(",").map(Number);
  if (space === "extended-gray" || space === "gray") return [v[0], v[0], v[0], v[1] ?? 1];
  return [v[0], v[1], v[2], v[3] ?? 1];
}

// "srgb:0.1,0.2,0.3,1" / "display-p3:..." / "extended-gray:w,a" -> CSS
export function cssColor(spec: string): string {
  const [space, rest] = spec.split(":");
  const v = (rest ?? "").split(",").map(Number);
  const c = (x: number) => Math.round(Math.max(0, Math.min(1, x)) * 255);
  if (space === "extended-gray" || space === "gray") {
    return `rgba(${c(v[0])},${c(v[0])},${c(v[0])},${v[1] ?? 1})`;
  }
  if (space === "display-p3") return `color(display-p3 ${v[0]} ${v[1]} ${v[2]} / ${v[3] ?? 1})`;
  return `rgba(${c(v[0])},${c(v[1])},${c(v[2])},${v[3] ?? 1})`;
}

export type FillView =
  | { kind: "Automatic" }
  | { kind: "None" }
  | { kind: "System Light" }
  | { kind: "System Dark" }
  | { kind: "Solid"; color: string; alpha: number }
  | { kind: "Gradient"; colors: string[] };

export function fillView(v: Json | undefined): FillView {
  if (v === undefined || v === null || v === "automatic") return { kind: "Automatic" };
  if (v === "none") return { kind: "None" };
  if (v === "system-light") return { kind: "System Light" };
  if (v === "system-dark") return { kind: "System Dark" };
  if (typeof v === "object" && !Array.isArray(v)) {
    if (typeof v.solid === "string") {
      const parts = v.solid.split(",");
      return { kind: "Solid", color: cssColor(v.solid), alpha: Number(parts[parts.length - 1]) };
    }
    const g = (v["linear-gradient"] ?? v["automatic-gradient"]) as Json;
    if (Array.isArray(g)) return { kind: "Gradient", colors: (g as string[]).map(cssColor) };
    if (typeof g === "string") return { kind: "Gradient", colors: [cssColor(g)] };
  }
  return { kind: "Automatic" };
}

export const BLEND_LABELS: Record<string, string> = {
  normal: "Normal",
  "plus-lighter": "Plus Lighter",
  "plus-darker": "Plus Darker",
  overlay: "Overlay",
  multiply: "Multiply",
  "soft-light": "Soft Light",
  "hard-light": "Hard Light",
  darken: "Darken",
  lighten: "Lighten",
  screen: "Screen",
};

export function supportedPlatforms(doc: Node): Platform[] {
  const sp = doc["supported-platforms"] as Node | undefined;
  const out: Platform[] = [];
  if (!sp || sp.squares !== undefined) out.push("iOS");
  if (sp && sp.circles !== undefined) out.push("watchOS");
  return out;
}
