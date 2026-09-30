import { Background, solidCss } from "./Canvas";

// A copia do fundo da janela, em px CSS, como a tela o mostra: a cor solida
// (resolvida pelo CSS) ou a imagem em "cover" centralizada -- o mesmo que o
// `.window` faz. E o que o Clear clareia.
export async function snapshotBackdrop(bg: Background, el: HTMLElement): Promise<{ w: number; h: number; rgba: Uint8Array }> {
  const w = Math.max(1, Math.round(el.clientWidth));
  const h = Math.max(1, Math.round(el.clientHeight));
  const c = document.createElement("canvas");
  c.width = w;
  c.height = h;
  const g = c.getContext("2d")!;
  if (bg.kind === "solid") {
    // `var(--canvas-solid)` so se resolve pelo estilo computado.
    const probe = document.createElement("div");
    probe.style.background = solidCss(bg.color);
    el.appendChild(probe);
    g.fillStyle = getComputedStyle(probe).backgroundColor;
    probe.remove();
    g.fillRect(0, 0, w, h);
  } else {
    const img = new Image();
    img.src = bg.url;
    await img.decode();
    const k = Math.max(w / img.naturalWidth, h / img.naturalHeight);
    const dw = img.naturalWidth * k;
    const dh = img.naturalHeight * k;
    g.drawImage(img, (w - dw) / 2, (h - dh) / 2, dw, dh);
  }
  return { w, h, rgba: new Uint8Array(g.getImageData(0, 0, w, h).data.buffer) };
}
