// A conversa com o nucleo C++ (`icserver`, via core.rs). Todo pixel do canvas
// e das miniaturas sai dele; aqui so se agenda pedidos e se decodifica o quadro.

import { invoke } from "@tauri-apps/api/core";

export type Frame = { width: number; height: number; originX: number; originY: number; ms: number; image: ImageData };

export type RenderParams = {
  size: number;
  appearance: string;
  idiom: string;
  tile?: [number, number, number, number];
};

async function renderNow(p: RenderParams): Promise<Frame> {
  const buf = await invoke<ArrayBuffer>("core_render", {
    size: p.size,
    appearance: p.appearance,
    idiom: p.idiom,
    tile: p.tile ?? [0, 0, 0, 0],
  });
  const v = new DataView(buf);
  const width = v.getUint32(0, true);
  const height = v.getUint32(4, true);
  const originX = v.getInt32(8, true);
  const originY = v.getInt32(12, true);
  const ms = v.getFloat64(16, true);
  const image = new ImageData(new Uint8ClampedArray(buf, 24, width * height * 4), width, height);
  return { width, height, originX, originY, ms, image };
}

// Um processo, um quadro por vez. Cada "raia" guarda so o pedido MAIS NOVO
// enquanto outro esta em voo, e a raia do canvas passa na frente das
// miniaturas -- a pessoa esta olhando para o canvas.
type Job = { params: RenderParams; done: (f: Frame) => void; fail: (e: string) => void };
const lanes: Map<string, Job> = new Map();
let busy = false;

function pump() {
  if (busy) return;
  const key = lanes.has("canvas") ? "canvas" : lanes.keys().next().value;
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
