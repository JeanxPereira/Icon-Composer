// A navegacao do canvas, portada de Source/IconComposerKit/PanelCanvas.cpp
// (que a trouxe do ImageViewer do Onyx): zoom ancorado no ponteiro, pan com
// limite e um valor mostrado que persegue o alvo com uma suavizacao
// exponencial. Mesmos nomes e mesmos numeros do C++.

export const kCanvasZoomMin = 0.125;
export const kCanvasZoomMax = 16;
export const kCanvasPanMargin = 80;
export const kWheelStep = 1.15;

export type Vec = { x: number; y: number };

export function canvasClampZoom(zoom: number): number {
  if (!(zoom > 0)) return kCanvasZoomMin; // tambem pega NaN
  return Math.min(kCanvasZoomMax, Math.max(kCanvasZoomMin, zoom));
}

export function canvasFitZoom(availW: number, availH: number, sidePx: number): number {
  return canvasClampZoom(Math.min(availW / sidePx, availH / sidePx));
}

export function canvasCentrePan(availW: number, availH: number, sidePx: number, zoom: number): Vec {
  const side = sidePx * zoom;
  return { x: (availW - side) * 0.5, y: (availH - side) * 0.5 };
}

// O ponto sob o ponteiro fica parado: o canto se move pela diferenca.
export function canvasZoomAnchored(pan: Vec, fromZoom: number, toZoom: number, anchor: Vec): Vec {
  if (!(fromZoom > 0)) return pan;
  const scale = canvasClampZoom(toZoom) / fromZoom;
  return { x: anchor.x - (anchor.x - pan.x) * scale, y: anchor.y - (anchor.y - pan.y) * scale };
}

// Menor que a area: fica centrado, com a margem de folga. Maior: a borda de
// la nao entra alem da margem, entao o icone nao sai arrastado da tela.
export function canvasClampPan(
  pan: Vec,
  availW: number,
  availH: number,
  sidePx: number,
  zoom: number,
  margin = kCanvasPanMargin,
): Vec {
  const side = sidePx * zoom;
  const axis = (v: number, avail: number) => {
    if (side <= avail) {
      const centre = (avail - side) * 0.5;
      return Math.min(centre + margin, Math.max(centre - margin, v));
    }
    return Math.min(margin, Math.max(avail - side - margin, v));
  };
  return { x: axis(pan.x, availW), y: axis(pan.y, availH) };
}

// `canvasEase`: a fracao do caminho que o valor mostrado anda em `dt` segundos.
export function canvasEase(dt: number): number {
  return 1 - Math.exp(-18 * dt);
}
