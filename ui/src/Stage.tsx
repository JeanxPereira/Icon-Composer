import { useCallback, useEffect, useRef, useState } from "react";
import { CANVAS_POINTS, Frame, LayerRect, layerAt } from "./core";
import { Selection } from "./doc";
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
  gridStyle: "light" | "dark";
  platform: string;
  zoom: number; // o ALVO, dono e o App (a barra de ferramentas tambem o muda)
  onZoom: (z: number) => void;
  onView: (v: { x: number; y: number; w: number; h: number }) => void;
  rects: LayerRect[];
  selection: Selection;
  onSelect: (s: Selection) => void;
  onMove: (s: Selection, dx: number, dy: number, first: boolean) => void;
  snap: boolean;
};

// Ate onde um arraste ainda e um clique, em px CSS.
const CLICK_SLOP = 3;
// A distancia, em px de TELA, em que uma guia atrai (constante no zoom).
const SNAP_PX = 6;

type Guides = { x: number[]; y: number[] };

// "Snap to Guides" (`EditableIconCompositionCanvas._snapToGuides`,
// `LayoutGuide`): a caixa que anda -- as bordas e o centro dela -- e atraida
// pelas bordas e pelo centro do canvas e de cada camada visivel que nao anda.
// Devolve o quanto somar ao deslocamento em cada eixo e as guias que prenderam.
// `[INF]` quais guias o alvo oferece nao foi lido do binario; estas sao as de
// um editor de layout comum.
function snapBox(
  box: { x0: number; y0: number; x1: number; y1: number },
  others: LayerRect[],
  thr: number,
  lockX: boolean,
  lockY: boolean,
): { ax: number; ay: number; guides: Guides } {
  const axis = (a0: number, a1: number, targets: number[]) => {
    const own = [a0, (a0 + a1) / 2, a1];
    let best = { d: Infinity, t: 0 };
    for (const t of targets)
      for (const o of own) {
        const d = t - o;
        if (Math.abs(d) < Math.abs(best.d) && Math.abs(d) <= thr) best = { d, t };
      }
    if (!Number.isFinite(best.d)) return { adjust: 0, lines: [] as number[] };
    // Todas as guias que coincidem depois do ajuste, nao so a que ganhou.
    const moved = own.map((o) => o + best.d);
    return { adjust: best.d, lines: [...new Set(targets.filter((t) => moved.some((m) => Math.abs(m - t) < 0.01)))] };
  };
  const tx = [0, CANVAS_POINTS / 2, CANVAS_POINTS, ...others.flatMap((r) => [r.x0, (r.x0 + r.x1) / 2, r.x1])];
  const ty = [0, CANVAS_POINTS / 2, CANVAS_POINTS, ...others.flatMap((r) => [r.y0, (r.y0 + r.y1) / 2, r.y1])];
  const x = lockX ? { adjust: 0, lines: [] } : axis(box.x0, box.x1, tx);
  const y = lockY ? { adjust: 0, lines: [] } : axis(box.y0, box.y1, ty);
  return { ax: x.adjust, ay: y.adjust, guides: { x: x.lines, y: y.lines } };
}

function unionOf(rs: LayerRect[]) {
  return {
    x0: Math.min(...rs.map((r) => r.x0)),
    y0: Math.min(...rs.map((r) => r.y0)),
    x1: Math.max(...rs.map((r) => r.x1)),
    y1: Math.max(...rs.map((r) => r.y1)),
  };
}

export function Stage(p: Props) {
  const el = useRef<HTMLDivElement>(null);
  const [avail, setAvail] = useState({ w: 800, h: 600 });
  const target = useRef({ zoom: p.zoom, pan: { x: 0, y: 0 } as Vec });
  const [shown, setShown] = useState({ zoom: p.zoom, pan: { x: 0, y: 0 } as Vec });
  const shownRef = useRef(shown);
  const raf = useRef(0);
  // `move`: o que o toque pegou (`DragState.Operation {selection, move}`); sem
  // ele o arraste move a vista.
  const drag = useRef<{
    x: number;
    y: number;
    x0: number;
    y0: number;
    moved: boolean;
    move: Selection | null;
    base: LayerRect[];
    sent: boolean;
  } | null>(null);
  const [hover, setHover] = useState<LayerRect | null>(null);
  // Os retangulos com a camada (ou o grupo) ja no lugar novo: o contorno anda
  // com o ponteiro, sem esperar o nucleo. Depois de soltar, valem ate os
  // retangulos novos chegarem.
  const [live, setLive] = useState<{ rects: LayerRect[]; settling: boolean } | null>(null);
  const pendingMove = useRef<{ dx: number; dy: number } | null>(null);
  const [guides, setGuides] = useState<Guides | null>(null);
  const moveRaf = useRef(0);
  useEffect(() => {
    if (live?.settling) setLive(null);
  }, [p.rects]);
  const rects = live?.rects ?? p.rects;

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

  // Um ponto da tela em pontos do canvas, pelo quadrado MOSTRADO.
  const toPoints = (e: React.PointerEvent) => {
    const r = el.current!.getBoundingClientRect();
    const s = shownRef.current;
    const k = CANVAS_POINTS / (SIDE * s.zoom);
    return { x: (e.clientX - r.left - s.pan.x) * k, y: (e.clientY - r.top - s.pan.y) * k };
  };

  const onPointerDown = (e: React.PointerEvent) => {
    if (e.button !== 0 && e.button !== 1) return;
    // O toque decide o alvo: dentro de uma camada do grupo selecionado, o
    // grupo; noutra camada, ela (e ja fica selecionada); no vazio, a vista.
    // O botao do meio sempre move a vista.
    let move: Selection | null = null;
    if (e.button === 0) {
      const pt = toPoints(e);
      const hit = layerAt(p.rects, pt.x, pt.y);
      if (hit && p.selection.kind === "group" && hit.g === p.selection.g) move = p.selection;
      else if (hit) {
        move = { kind: "layer", g: hit.g, l: hit.l };
        if (p.selection.kind !== "layer" || p.selection.g !== hit.g || p.selection.l !== hit.l) p.onSelect(move);
      }
    }
    drag.current = { x: e.clientX, y: e.clientY, x0: e.clientX, y0: e.clientY, moved: false, move, base: p.rects, sent: false };
    (e.target as Element).setPointerCapture(e.pointerId);
  };

  // O passo ao nucleo, no maximo um por quadro de tela.
  const flushMove = () => {
    moveRaf.current = 0;
    const d = drag.current;
    const m = pendingMove.current;
    if (!d?.move || !m) return;
    pendingMove.current = null;
    p.onMove(d.move, m.dx, m.dy, !d.sent);
    d.sent = true;
  };
  const onPointerMove = (e: React.PointerEvent) => {
    const d = drag.current;
    if (!d) {
      // Sem botao: o destaque de passagem (`HighlightStyle.hovered`).
      const pt = toPoints(e);
      const h = layerAt(rects, pt.x, pt.y);
      if (h?.g !== hover?.g || h?.l !== hover?.l) setHover(h);
      return;
    }
    if (!d.moved && Math.hypot(e.clientX - d.x0, e.clientY - d.y0) <= CLICK_SLOP) return;
    d.moved = true;
    if (d.move) {
      // Em pontos do canvas; Shift trava no eixo que mais andou
      // (`ConstrainedDragGesture`).
      const k = CANVAS_POINTS / (SIDE * shownRef.current.zoom);
      let dx = (e.clientX - d.x0) * k;
      let dy = (e.clientY - d.y0) * k;
      let lockX = false;
      let lockY = false;
      if (e.shiftKey) {
        if (Math.abs(dx) > Math.abs(dy)) {
          dy = 0;
          lockY = true;
        } else {
          dx = 0;
          lockX = true;
        }
      }
      const sel = d.move;
      const moves = (r: LayerRect) =>
        sel.kind !== "icon" && r.g === sel.g && (sel.kind === "group" || r.l === sel.l);
      // As guias: Ctrl segura solta o ima, como nos editores de layout.
      const moving = d.base.filter((r) => moves(r) && !r.hidden);
      if (p.snap && !e.ctrlKey && moving.length) {
        const u = unionOf(moving);
        const s = snapBox(
          { x0: u.x0 + dx, y0: u.y0 + dy, x1: u.x1 + dx, y1: u.y1 + dy },
          d.base.filter((r) => !moves(r) && !r.hidden),
          SNAP_PX * k,
          lockX,
          lockY,
        );
        dx += s.ax;
        dy += s.ay;
        setGuides(s.guides.x.length || s.guides.y.length ? s.guides : null);
      } else setGuides(null);
      setLive({
        rects: d.base.map((r) => (moves(r) ? { ...r, x0: r.x0 + dx, x1: r.x1 + dx, y0: r.y0 + dy, y1: r.y1 + dy } : r)),
        settling: false,
      });
      pendingMove.current = { dx, dy };
      if (!moveRaf.current) moveRaf.current = requestAnimationFrame(flushMove);
      return;
    }
    const t = target.current;
    setTarget(t.zoom, { x: t.pan.x + e.clientX - d.x, y: t.pan.y + e.clientY - d.y });
    d.x = e.clientX;
    d.y = e.clientY;
  };
  // Soltar sem ter arrastado e um clique: escolhe a camada de cima sob o
  // ponteiro, ou o icone no vazio (`ick::canvasLayerAt`).
  const onPointerUp = (e: React.PointerEvent) => {
    const d = drag.current;
    if (d?.move && d.moved) {
      if (moveRaf.current) cancelAnimationFrame(moveRaf.current);
      flushMove();
      setLive((l) => (l ? { ...l, settling: true } : null));
    }
    setGuides(null);
    drag.current = null;
    // Um clique no vazio volta ao icone; numa camada, o toque ja escolheu.
    if (d && !d.moved && !d.move && e.button === 0) p.onSelect({ kind: "icon" });
  };

  const side = SIDE * shown.zoom;
  return (
    <div
      ref={el}
      className={`stage-view${drag.current?.moved ? (drag.current.move ? " moving" : " dragging") : ""}`}
      onWheel={onWheel}
      onPointerDown={onPointerDown}
      onPointerMove={onPointerMove}
      onPointerUp={onPointerUp}
      onPointerCancel={() => {
        drag.current = null;
        setLive(null);
        setGuides(null);
      }}
      onPointerLeave={() => setHover(null)}
    >
      {p.frame && (
        <div className="icon-wrap" style={{ left: shown.pan.x, top: shown.pan.y, width: side, height: side }}>
          <FrameView frame={p.frame} cssSide={side} />
          {p.tile && <FrameView frame={p.tile} cssSide={side} />}
          {p.grid && (
            <img
              className={`grid-overlay ${p.gridStyle}`}
              src={`/apple/custom/${p.platform === "watchOS" ? "appicongrid.watchos" : "appicongrid.ios"}.svg`}
              alt=""
            />
          )}
          <SelectionOverlay rects={rects} selection={p.selection} hover={drag.current?.moved ? null : hover} side={side} />
          {guides && <GuideLines guides={guides} side={side} />}
        </div>
      )}
    </div>
  );
}

// O destaque (`EditableIconCompositionCanvas.selectedMemberColor`,
// `HighlightStyle {selected, layerInSelectedGroup, hovered}`): geometria, nunca
// um render -- aparece antes do quadro chegar e acompanha o zoom mostrado.
function SelectionOverlay({
  rects,
  selection,
  hover,
  side,
}: {
  rects: LayerRect[];
  selection: Selection;
  hover: LayerRect | null;
  side: number;
}) {
  const k = side / CANVAS_POINTS;
  const box = (r: { x0: number; y0: number; x1: number; y1: number }, cls: string, key: string) => (
    <div
      key={key}
      className={`sel-box ${cls}`}
      style={{ left: r.x0 * k, top: r.y0 * k, width: (r.x1 - r.x0) * k, height: (r.y1 - r.y0) * k }}
    />
  );
  const out: React.ReactNode[] = [];
  if (selection.kind === "group") {
    const members = rects.filter((r) => r.g === selection.g && !r.hidden);
    members.forEach((r) => out.push(box(r, "in-group", `m${r.l}`)));
    if (members.length) {
      const u = {
        x0: Math.min(...members.map((r) => r.x0)),
        y0: Math.min(...members.map((r) => r.y0)),
        x1: Math.max(...members.map((r) => r.x1)),
        y1: Math.max(...members.map((r) => r.y1)),
      };
      out.push(box(u, "selected", "group"));
    }
  } else if (selection.kind === "layer") {
    const r = rects.find((x) => x.g === selection.g && x.l === selection.l);
    if (r) out.push(box(r, "selected", "layer"));
  }
  const hoverIsSelected =
    hover && selection.kind === "layer" && selection.g === hover.g && selection.l === hover.l;
  if (hover && !hoverIsSelected) out.push(box(hover, "hovered", "hover"));
  return <div className="sel-overlay">{out}</div>;
}

// As guias que prenderam, de ponta a ponta do canvas.
function GuideLines({ guides, side }: { guides: Guides; side: number }) {
  const k = side / CANVAS_POINTS;
  return (
    <div className="sel-overlay">
      {guides.x.map((x) => (
        <div key={`x${x}`} className="guide v" style={{ left: x * k }} />
      ))}
      {guides.y.map((y) => (
        <div key={`y${y}`} className="guide h" style={{ top: y * k }} />
      ))}
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
