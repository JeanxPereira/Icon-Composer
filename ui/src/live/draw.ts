// O render AO VIVO, Etapa A: a pastilha com o fundo e as camadas sem vidro
// (tinta, opacidade, mescla), redesenhados a cada mudanca no tamanho exato da
// tela -- o SVG e rasterizado pelo navegador no tamanho pedido, entao o zoom e
// vetorial. O vidro (campo, sombra, translucidez, especular) entra na Etapa B,
// em WebGPU, lendo estas camadas como textura.
//
// A ordem e a do nucleo (`renderIcon`): grupos e camadas de TRAS para frente,
// fundo recortado pela pastilha, camadas sem recorte.

import { groups, layers, Node, parseColor, resolve } from "../doc";
import { automaticRamp, chicletPath, Stop, systemRamp } from "./shape";

export type Art = { img: CanvasImageSource; x: number; y: number; w: number; h: number };
export type Arts = Map<string, Art>;

export type LiveInputs = {
  doc: Node;
  appearance: string;
  idiom: string; // "iOS" | "watchOS"
  arts: Arts;
};

const CANVAS_POINTS = 1024;

const BLEND: Record<string, GlobalCompositeOperation> = {
  normal: "source-over",
  multiply: "multiply",
  screen: "screen",
  overlay: "overlay",
  darken: "darken",
  lighten: "lighten",
  "soft-light": "soft-light",
  "hard-light": "hard-light",
  "plus-lighter": "lighter",
  // Sem equivalente no Canvas 2D; fica normal ate a Etapa B.
  "plus-darker": "source-over",
};

type Placement = { scale: number; tx: number; ty: number };

function placementOf(v: unknown): Placement {
  const p = v as { scale?: number; "translation-in-points"?: number[] } | undefined;
  return { scale: p?.scale ?? 1, tx: p?.["translation-in-points"]?.[0] ?? 0, ty: p?.["translation-in-points"]?.[1] ?? 0 };
}

// `compose` do nucleo: a transformacao do grupo se aplica a da camada.
function compose(g: Placement, l: Placement): Placement {
  return { scale: g.scale * l.scale, tx: g.scale * l.tx + g.tx, ty: g.scale * l.ty + g.ty };
}

function rgba(c: [number, number, number, number]): string {
  const b = (x: number) => Math.round(Math.min(1, Math.max(0, x)) * 255);
  return `rgba(${b(c[0])},${b(c[1])},${b(c[2])},${c[3]})`;
}

function ramp(ctx: CanvasRenderingContext2D, stops: Stop[], x: number, y0: number, y1: number): CanvasGradient {
  const g = ctx.createLinearGradient(x, y0, x, y1);
  for (const s of stops) g.addColorStop(Math.min(1, Math.max(0, s.location)), rgba(s.rgba));
  return g;
}

// O fill como pintura dentro de um retangulo vertical (placement padrao
// (0,0)->(0,1), `defaultGradientPlacement`). null = sem pintura propria.
function paintOf(
  ctx: CanvasRenderingContext2D,
  fill: unknown,
  y0: number,
  y1: number,
  background: boolean,
): string | CanvasGradient | null {
  if (fill === "system-dark") return ramp(ctx, systemRamp(true), 0, y0, y1);
  if (fill === "system-light") return ramp(ctx, systemRamp(false), 0, y0, y1);
  if (fill === undefined || fill === null || fill === "none") return null;
  if (fill === "automatic") return background ? ramp(ctx, systemRamp(false), 0, y0, y1) : null;
  if (typeof fill === "object" && !Array.isArray(fill)) {
    const f = fill as Record<string, unknown>;
    if (typeof f.solid === "string") return rgba(parseColor(f.solid));
    const lg = f["linear-gradient"];
    if (Array.isArray(lg) && lg.length >= 2) {
      const [a, b] = [parseColor(lg[0] as string), parseColor(lg[1] as string)];
      return ramp(ctx, [{ rgba: a, location: 0 }, { rgba: b, location: 1 }], 0, y0, y1);
    }
    const ag = f["automatic-gradient"];
    if (typeof ag === "string") {
      const c = parseColor(ag);
      return ramp(ctx, automaticRamp(c[0], c[1], c[2], c[3]), 0, y0, y1);
    }
  }
  return null;
}

let scratch: HTMLCanvasElement | null = null;
let groupScratch: HTMLCanvasElement | null = null;

function sized(c: HTMLCanvasElement | null, px: number): HTMLCanvasElement {
  const out = c ?? document.createElement("canvas");
  if (out.width !== px || out.height !== px) {
    out.width = px;
    out.height = px;
  }
  return out;
}

export function drawIcon(ctx: CanvasRenderingContext2D, px: number, inp: LiveInputs): number {
  const t0 = performance.now();
  const k = px / CANVAS_POINTS;
  const { doc, appearance, idiom, arts } = inp;
  const watch = idiom === "watchOS";
  ctx.setTransform(1, 0, 0, 1, 0, 0);
  ctx.globalCompositeOperation = "source-over";
  ctx.globalAlpha = 1;
  ctx.clearRect(0, 0, px, px);

  // ---- o fundo, recortado pela pastilha --------------------------------
  const bg = paintOf(ctx, resolve(doc, "fill", appearance, idiom), 0, px, true);
  if (bg) {
    ctx.fillStyle = bg;
    ctx.fill(chicletPath(px, watch));
  }

  // ---- as camadas, de tras para frente ----------------------------------
  const gs = groups(doc);
  scratch = sized(scratch, px);
  const sctx = scratch.getContext("2d")!;
  for (let gi = gs.length - 1; gi >= 0; gi--) {
    const g = gs[gi];
    if (resolve(g, "hidden", appearance, idiom) === true) continue;
    const gp = placementOf(resolve(g, "position", appearance, idiom));
    const gBlend = (resolve(g, "blend-mode", appearance, idiom) as string) ?? "normal";
    const gOpacity = (resolve(g, "opacity", appearance, idiom) as number) ?? 1;
    const isolated = gBlend !== "normal" || gOpacity < 1;
    let target = ctx;
    if (isolated) {
      groupScratch = sized(groupScratch, px);
      target = groupScratch.getContext("2d")!;
      target.globalCompositeOperation = "source-over";
      target.globalAlpha = 1;
      target.clearRect(0, 0, px, px);
    }
    const ls = layers(g);
    for (let li = ls.length - 1; li >= 0; li--) {
      const l = ls[li];
      if (resolve(l, "hidden", appearance, idiom) === true) continue;
      const opacity = (resolve(l, "opacity", appearance, idiom) as number) ?? 1;
      if (opacity <= 0) continue;
      const name = resolve(l, "image-name", appearance, idiom);
      const art = typeof name === "string" ? arts.get(name) : undefined;
      if (!art) continue;
      const lp = compose(gp, placementOf(resolve(l, "position", appearance, idiom)));
      const w = art.w * lp.scale;
      const h = art.h * lp.scale;
      const left = (CANVAS_POINTS - w) * 0.5 + lp.tx;
      const top = (CANVAS_POINTS - h) * 0.5 + lp.ty;
      const r = { x: left * k, y: top * k, w: w * k, h: h * k };

      // A arte no rascunho, depois a tinta por cima so onde ha arte.
      sctx.globalCompositeOperation = "source-over";
      sctx.globalAlpha = 1;
      sctx.clearRect(0, 0, px, px);
      sctx.drawImage(art.img, r.x, r.y, r.w, r.h);
      const paint = paintOf(sctx, resolve(l, "fill", appearance, idiom), r.y, r.y + r.h, false);
      if (paint) {
        sctx.globalCompositeOperation = "source-in";
        sctx.fillStyle = paint;
        sctx.fillRect(r.x, r.y, r.w, r.h);
      }

      target.globalCompositeOperation = BLEND[(resolve(l, "blend-mode", appearance, idiom) as string) ?? "normal"] ?? "source-over";
      target.globalAlpha = opacity;
      target.drawImage(scratch, 0, 0);
      target.globalCompositeOperation = "source-over";
      target.globalAlpha = 1;
    }
    if (isolated && groupScratch) {
      ctx.globalCompositeOperation = BLEND[gBlend] ?? "source-over";
      ctx.globalAlpha = gOpacity;
      ctx.drawImage(groupScratch, 0, 0);
      ctx.globalCompositeOperation = "source-over";
      ctx.globalAlpha = 1;
    }
  }
  return performance.now() - t0;
}
