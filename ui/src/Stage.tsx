import { useCallback, useEffect, useRef, useState } from "react";
import { Frame } from "./core";
import {
  canvasCentrePan,
  canvasClampPan,
  canvasClampZoom,
  canvasEase,
  canvasFitZoom,
  canvasZoomAnchored,
  kWheelStep,
  Vec,
} from "./viewport";

// O palco do canvas: a navegacao do PanelCanvas antigo (viewport.ts) e os
// quadros do nucleo -- a base inteira e, com zoom, o ladrilho visivel por cima.
// O que se VE persegue o alvo com suavizacao; o que se PEDE ao nucleo e o alvo,
// entao a animacao nao dispara um render por quadro.

const SIDE = 512; // o lado do icone em px CSS a 100 %
const FIT_PADDING = { x: 80, y: 160 }; // folga para a barra de rendicoes

type Props = {
  docKey: string;
  frame: Frame | null;
  tile: Frame | null;
  grid: boolean;
  platform: string;
  zoom: number; // o ALVO, dono e o App (a barra de ferramentas tambem o muda)
  onZoom: (z: number) => void;
  onView: (v: { x: number; y: number; w: number; h: number }) => void;
};

export function Stage(p: Props) {
  const el = useRef<HTMLDivElement>(null);
  const [avail, setAvail] = useState({ w: 800, h: 600 });
  const target = useRef({ zoom: p.zoom, pan: { x: 0, y: 0 } as Vec });
  const [shown, setShown] = useState({ zoom: p.zoom, pan: { x: 0, y: 0 } as Vec });
  const shownRef = useRef(shown);
  const raf = useRef(0);
  const drag = useRef<{ x: number; y: number } | null>(null);

  // A animacao: um laco de rAF que so roda enquanto o mostrado nao chegou.
  const kick = useCallback(() => {
    if (raf.current) return;
    let last = performance.now();
    const step = (now: number) => {
      const k = canvasEase(Math.min(0.1, (now - last) / 1000));
      last = now;
      const s = shownRef.current;
      const t = target.current;
      let zoom = s.zoom + (t.zoom - s.zoom) * k;
      let x = s.pan.x + (t.pan.x - s.pan.x) * k;
      let y = s.pan.y + (t.pan.y - s.pan.y) * k;
      if (Math.abs(t.zoom - zoom) < 0.0005) zoom = t.zoom;
      if (Math.abs(t.pan.x - x) < 0.25) x = t.pan.x;
      if (Math.abs(t.pan.y - y) < 0.25) y = t.pan.y;
      const next = { zoom, pan: { x, y } };
      shownRef.current = next;
      setShown(next);
      raf.current = zoom === t.zoom && x === t.pan.x && y === t.pan.y ? 0 : requestAnimationFrame(step);
    };
    raf.current = requestAnimationFrame(step);
  }, []);

  // O que se ve do quadrado do icone NO ALVO, em px CSS: o ladrilho a pedir.
  const report = useCallback(() => {
    const t = target.current;
    const side = SIDE * t.zoom;
    const x = Math.max(0, -t.pan.x);
    const y = Math.max(0, -t.pan.y);
    const r = Math.min(side, avail.w - t.pan.x);
    const b = Math.min(side, avail.h - t.pan.y);
    if (r > x && b > y) p.onView({ x, y, w: r - x, h: b - y });
  }, [avail, p.onView]);

  const setTarget = useCallback(
    (zoom: number, pan: Vec) => {
      const z = canvasClampZoom(zoom);
      target.current = { zoom: z, pan: canvasClampPan(pan, avail.w, avail.h, SIDE, z) };
      if (z !== p.zoom) p.onZoom(z);
      report();
      kick();
    },
    [avail, p.zoom, p.onZoom, report, kick],
  );

  const zoomTo = useCallback(
    (wanted: number, anchor: Vec) => {
      const t = target.current;
      const z = canvasClampZoom(wanted);
      if (z === t.zoom) return;
      setTarget(z, canvasZoomAnchored(t.pan, t.zoom, z, anchor));
    },
    [setTarget],
  );

  // O tamanho da area.
  useEffect(() => {
    const e = el.current;
    if (!e) return;
    const ro = new ResizeObserver(() => setAvail({ w: e.clientWidth, h: e.clientHeight }));
    ro.observe(e);
    return () => ro.disconnect();
  }, []);

  // Documento novo (ou primeira medida da area): enquadrar.
  const fitted = useRef("");
  useEffect(() => {
    if (!p.frame || fitted.current === p.docKey || avail.w < 50) return;
    fitted.current = p.docKey;
    const z = Math.min(1, canvasFitZoom(avail.w - FIT_PADDING.x, avail.h - FIT_PADDING.y, SIDE));
    const pan = canvasCentrePan(avail.w, avail.h - (FIT_PADDING.y - FIT_PADDING.x) / 2, SIDE, z);
    target.current = { zoom: z, pan };
    shownRef.current = { zoom: z, pan };
    setShown({ zoom: z, pan });
    if (z !== p.zoom) p.onZoom(z);
    report();
  }, [p.frame, p.docKey, avail]);

  // Zoom vindo de FORA (as setas da barra): ancorado no centro da area.
  useEffect(() => {
    if (Math.abs(p.zoom - target.current.zoom) > 1e-6) zoomTo(p.zoom, { x: avail.w / 2, y: avail.h / 2 });
  }, [p.zoom]);

  // A area mudou de tamanho: o pan continua dentro do limite.
  useEffect(() => {
    setTarget(target.current.zoom, target.current.pan);
  }, [avail]);

  const onWheel = (e: React.WheelEvent) => {
    const r = el.current!.getBoundingClientRect();
    // Roda sem modificador = zoom, como no alvo; o trackpad manda deltas
    // pequenos, entao o passo e proporcional ao delta.
    const steps = -e.deltaY / 100;
    zoomTo(target.current.zoom * Math.pow(kWheelStep, steps), { x: e.clientX - r.left, y: e.clientY - r.top });
  };

  const onPointerDown = (e: React.PointerEvent) => {
    if (e.button !== 0 && e.button !== 1) return;
    drag.current = { x: e.clientX, y: e.clientY };
    (e.target as Element).setPointerCapture(e.pointerId);
  };
  const onPointerMove = (e: React.PointerEvent) => {
    const d = drag.current;
    if (!d) return;
    const t = target.current;
    setTarget(t.zoom, { x: t.pan.x + e.clientX - d.x, y: t.pan.y + e.clientY - d.y });
    drag.current = { x: e.clientX, y: e.clientY };
  };
  const onPointerUp = () => {
    drag.current = null;
  };

  const side = SIDE * shown.zoom;
  return (
    <div
      ref={el}
      className={`stage-view${drag.current ? " dragging" : ""}`}
      onWheel={onWheel}
      onPointerDown={onPointerDown}
      onPointerMove={onPointerMove}
      onPointerUp={onPointerUp}
      onPointerCancel={onPointerUp}
    >
      {p.frame && (
        <div className="icon-wrap" style={{ left: shown.pan.x, top: shown.pan.y, width: side, height: side }}>
          <FrameView frame={p.frame} cssSide={side} />
          {p.tile && <FrameView frame={p.tile} cssSide={side} />}
          {p.grid && (
            <img
              className="grid-overlay"
              src={`/apple/custom/${p.platform === "watchOS" ? "appicongrid.watchos" : "appicongrid.ios"}.svg`}
              alt=""
            />
          )}
        </div>
      )}
    </div>
  );
}

// O quadro do nucleo, pintado como veio: RGBA8, sem PNG no caminho. Ele se
// posiciona pela origem e pelo `size` do canvas para o qual foi pedido, entao
// um quadro de um zoom anterior fica no lugar certo (esticado) ate o novo chegar.
function FrameView({ frame, cssSide }: { frame: Frame; cssSide: number }) {
  const ref = useRef<HTMLCanvasElement>(null);
  useEffect(() => {
    const c = ref.current;
    if (!c) return;
    if (c.width !== frame.width || c.height !== frame.height) {
      c.width = frame.width;
      c.height = frame.height;
    }
    c.getContext("2d")!.putImageData(frame.image, 0, 0);
  }, [frame]);
  const k = cssSide / frame.size;
  return (
    <canvas
      ref={ref}
      className="frame"
      style={{ left: frame.originX * k, top: frame.originY * k, width: frame.width * k, height: frame.height * k }}
    />
  );
}
