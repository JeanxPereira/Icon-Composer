// A forma e o fundo da pastilha, portados de Source/RenderBox/ChicletShape.cpp,
// SystemFill.cpp e AutomaticGradient.cpp. Os numeros sao os do nucleo, que os
// leu do binario; aqui so muda a linguagem.

// ---- o canto continuo (`continuousRoundedRect`) ----------------------------

const kSlack = 0.528664947;
const kBlendExtentBase = 1.0;
const kBlendExtentSlope = 0.528664947;
const kBlendControlBase = 0.959999979;
const kBlendControlSlope = 0.128490031;
const kBlendShoulderBase = 0.819999993;
const kBlendShoulderSlope = 0.0484070182;
const kNear = 0.074911400675773621;
const kMid1 = 0.16906000673770905;
const kMid2 = 0.37282401323318481;
const kFar = 0.63149398565292358;

type CornerParams = { extent: number; control: number; shoulder: number };

function cornerParams(edge: number, rNear: number, rFar: number): CornerParams {
  const sum = Math.fround(rNear) + Math.fround(rFar);
  const room = Math.abs(edge) - sum;
  const t = room / (sum * kSlack);
  if (t >= 1) return { extent: 1.5286649465560913, control: 1.0884900093078613, shoulder: 0.8684070110321045 };
  const u = Math.min(1, Math.max(0, t));
  return {
    extent: kBlendExtentBase + kBlendExtentSlope * u,
    control: kBlendControlBase + kBlendControlSlope * u,
    shoulder: kBlendShoulderBase + kBlendShoulderSlope * u,
  };
}

type V = { x: number; y: number };

export function continuousRoundedRect(x: number, y: number, w: number, h: number, rx: number, ry: number): Path2D {
  rx = Math.min(Math.max(rx, 0), Math.abs(w) * 0.5);
  ry = Math.min(Math.max(ry, 0), Math.abs(h) * 0.5);
  const x0 = Math.min(x, x + w), x1 = Math.max(x, x + w);
  const y0 = Math.min(y, y + h), y1 = Math.max(y, y + h);
  const vert = cornerParams(y1 - y0, ry, ry);
  const horz = cornerParams(x1 - x0, rx, rx);
  const up = { x: 0, y: -1 }, down = { x: 0, y: 1 }, left = { x: -1, y: 0 }, right = { x: 1, y: 0 };
  const p = new Path2D();
  const seam = 0.5 * (y0 + ry + (y1 - ry));
  p.moveTo(x1, seam);
  const at = (c: V, e: V, se: number, f: V, sf: number) => [c.x + e.x * se + f.x * sf, c.y + e.y * se + f.y * sf] as const;
  const corner = (c: V, e: V, re: number, pe: CornerParams, f: V, rf: number, pf: CornerParams) => {
    p.lineTo(...at(c, e, pe.extent * re, f, 0));
    p.bezierCurveTo(...at(c, e, pe.control * re, f, 0), ...at(c, e, pe.shoulder * re, f, 0), ...at(c, e, kFar * re, f, kNear * rf));
    p.bezierCurveTo(...at(c, e, kMid2 * re, f, kMid1 * rf), ...at(c, e, kMid1 * re, f, kMid2 * rf), ...at(c, e, kNear * re, f, kFar * rf));
    p.bezierCurveTo(...at(c, e, 0, f, pf.shoulder * rf), ...at(c, e, 0, f, pf.control * rf), ...at(c, e, 0, f, pf.extent * rf));
  };
  corner({ x: x1, y: y1 }, up, ry, vert, left, rx, horz); // baixo direita
  corner({ x: x0, y: y1 }, right, rx, horz, up, ry, vert); // baixo esquerda
  corner({ x: x0, y: y0 }, down, ry, vert, right, rx, horz); // cima esquerda
  corner({ x: x1, y: y0 }, left, rx, horz, down, ry, vert); // cima direita
  p.lineTo(x1, seam);
  p.closePath();
  return p;
}

// `ChicletGeometry::of`: a pastilha num quadrado de `size` pixels.
export function chicletPath(size: number, watch: boolean): Path2D {
  if (watch) {
    const p = new Path2D();
    p.arc(size / 2, size / 2, (size - 1) / 2, 0, Math.PI * 2);
    return p;
  }
  const r = (266.24 * size) / 1024;
  return continuousRoundedRect(0, 0, size, size, r, r);
}

// ---- os fundos ---------------------------------------------------------------

export type Stop = { rgba: [number, number, number, number]; location: number };

// `buildSystemRamp`: dois cinzas, de cima (0) para baixo (1).
export function systemRamp(dark: boolean): Stop[] {
  const [a, b] = dark ? [0.12156862745098039, 0.058823529411764705] : [1.0, 0.9607843137254902];
  return [
    { rgba: [a, a, a, 1], location: 0 },
    { rgba: [b, b, b, 1], location: 1 },
  ];
}

// `automaticGradient`, com os parametros padrao do nucleo.
export function automaticRamp(r: number, g: number, b: number, alpha: number): Stop[] {
  const L = 0.2126 * r + 0.7152 * g + 0.0722 * b;
  const lightening = L <= 0.25 ? 0.04 : L <= 0.5 ? 0.08 : L <= 0.75 ? 0.15 : -0.05;
  const sb = 0.2;
  const boosted = [r - sb * (L - r), g - sb * (L - g), b - sb * (L - b)];
  const positive = lightening > 0;
  const shifted = boosted.map((c) => Math.min(1, Math.max(0, c + lightening * (positive ? 1 - c : c))));
  const a: Stop = { rgba: [shifted[0], shifted[1], shifted[2], alpha], location: positive ? 0 : 1 };
  const c: Stop = { rgba: [r, g, b, alpha], location: positive ? 1 : 0 };
  return [a, c].sort((x, y) => x.location - y.location);
}
