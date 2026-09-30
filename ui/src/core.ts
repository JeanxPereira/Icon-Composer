// A conversa com o nucleo C++ (`icserver`, via core.rs). Todo pixel do canvas
// e das miniaturas sai dele; aqui so se agenda pedidos e se decodifica o quadro.

import { invoke } from "@tauri-apps/api/core";

// `size` e o lado do canvas inteiro para o qual o quadro foi pedido: um
// ladrilho so se posiciona sabendo de que canvas ele e pedaco.
export type Frame = { size: number; width: number; height: number; originX: number; originY: number; ms: number; image: ImageData };

export type RenderParams = {
  size: number;
  appearance: string;
  idiom: string;
  tile?: [number, number, number, number];
  subdivisions?: number;
  effects?: boolean;
  // Tinted Dark: [r, g, b, saturation] (`tintColor` em doc.ts).
  tint?: [number, number, number, number];
  // O quadrado do canvas em pixels do backdrop (`coreBackdrop`) e se a
  // aparencia e escura: [x, y, lado, 0|1]. Sozinho e o Clear; com `tint`, o
  // Tinted Dark sobre o vidro simulado.
  clear?: [number, number, number, number];
};

async function renderNow(p: RenderParams): Promise<Frame> {
  const buf = await invoke<ArrayBuffer>("core_render", {
    size: p.size,
    appearance: p.appearance,
    idiom: p.idiom,
    tile: p.tile ?? [0, 0, 0, 0],
    subdivisions: p.subdivisions ?? 16,
    effects: p.effects ?? true,
    tint: p.tint ?? null,
    clear: p.clear ?? null,
  });
  const v = new DataView(buf);
  const width = v.getUint32(0, true);
  const height = v.getUint32(4, true);
  const originX = v.getInt32(8, true);
  const originY = v.getInt32(12, true);
  const ms = v.getFloat64(16, true);
  const image = new ImageData(new Uint8ClampedArray(buf, 24, width * height * 4), width, height);
  return { size: p.size, width, height, originX, originY, ms, image };
}

// Um processo, um quadro por vez. Cada "raia" guarda so o pedido MAIS NOVO
// enquanto outro esta em voo, e a raia do canvas passa na frente das
// miniaturas -- a pessoa esta olhando para o canvas.
type Job = { params: RenderParams; done: (f: Frame) => void; fail: (e: string) => void };
const lanes: Map<string, Job> = new Map();
let busy = false;

function pump() {
  if (busy) return;
  const key = lanes.has("canvas")
    ? "canvas"
    : lanes.has("canvas-tile")
      ? "canvas-tile"
      : lanes.keys().next().value;
  if (key === undefined) return;
  const job = lanes.get(key)!;
  lanes.delete(key);
  busy = true;
  renderNow(job.params)
    .then(job.done, (e) => job.fail(String(e)))
    .finally(() => {
      busy = false;
      pump();
    });
}

export function requestFrame(lane: string, params: RenderParams): Promise<Frame> {
  return new Promise((done, fail) => {
    const old = lanes.get(lane);
    if (old) old.fail("substituido");
    lanes.set(lane, { params, done, fail });
    pump();
  });
}

export async function coreOpen(path: string): Promise<void> {
  lanes.clear();
  await invoke("core_open", { path });
}

export function frameToDataUrl(f: Frame): string {
  const c = document.createElement("canvas");
  c.width = f.width;
  c.height = f.height;
  c.getContext("2d")!.putImageData(f.image, 0, 0);
  return c.toDataURL();
}

export function frameToPng(f: Frame): Promise<Blob> {
  const c = document.createElement("canvas");
  c.width = f.width;
  c.height = f.height;
  c.getContext("2d")!.putImageData(f.image, 0, 0);
  return new Promise((done, fail) => c.toBlob((b) => (b ? done(b) : fail("toBlob falhou")), "image/png"));
}

export async function blobToBase64(b: Blob): Promise<string> {
  return bytesToBase64(new Uint8Array(await b.arrayBuffer()));
}

export function bytesToBase64(bytes: Uint8Array): string {
  let s = "";
  for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
  return btoa(s);
}

// O retangulo de uma camada em pontos do canvas (0..1024), sob o contexto do
// canvas: a regua de `ick::canvasLayerRect`, calculada no nucleo.
export type LayerRect = { g: number; l: number; x0: number; y0: number; x1: number; y1: number; hidden: boolean };
export const CANVAS_POINTS = 1024;

export async function coreRects(appearance: string, idiom: string): Promise<LayerRect[]> {
  return JSON.parse(await invoke<string>("core_rects", { appearance, idiom })) as LayerRect[];
}

// A camada sob um ponto (em pontos do canvas): a MAIS ACIMA que o contem. O
// array corre da frente para tras, entao a primeira que acerta e a de cima; uma
// escondida nao e candidata (`ick::canvasLayerAt`).
export function layerAt(rects: LayerRect[], x: number, y: number): LayerRect | null {
  for (const r of rects) if (!r.hidden && x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1) return r;
  return null;
}

// A escrita: o nucleo aplica `setProperty` sob o escopo e devolve o icon.json.
export async function coreSet(
  group: number,
  layer: number,
  scope: { appearance: string; idiom: string },
  prop: string,
  value: unknown,
  coalesce = false,
): Promise<string> {
  return invoke<string>("core_set", {
    coalesce,
    group,
    layer,
    appearance: scope.appearance,
    idiom: scope.idiom,
    prop,
    value: JSON.stringify(value ?? null),
  });
}

export function coreHistory(step: "undo" | "redo" | "get"): Promise<string> {
  return invoke<string>("core_history", { step });
}

export function coreSave(): Promise<void> {
  return invoke("core_save");
}

export type NodeOp = "add-group" | "add-layer" | "remove" | "duplicate" | "move" | "rename";

// A estrutura, pelo nucleo (Edit.h): devolve o icon.json depois da operacao.
export function coreNode(op: NodeOp, group: number, layer: number, arg = ""): Promise<string> {
  return invoke<string>("core_node", { op, group, layer, arg });
}

// Copia um arquivo para Assets/ do documento aberto; devolve o nome la dentro.
export function coreImport(file: string): Promise<string> {
  return invoke<string>("core_import", { file });
}

// O fundo da janela como a tela o mostra, para o Clear: ele e ENTRADA do
// render (a composicao do alvo e sobre o fundo).
// `scale`: pixels da copia por ponto da tela.
export function coreBackdrop(width: number, height: number, scale: number, rgba: Uint8Array): Promise<void> {
  return invoke("core_backdrop", { width, height, scale, rgba: bytesToBase64(rgba) });
}
