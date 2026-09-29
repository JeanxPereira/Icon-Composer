import { useEffect, useRef, useState } from "react";
import { invoke } from "@tauri-apps/api/core";
import { groups, layers, Node } from "../doc";
import { Art, Arts, drawIcon } from "./draw";

type Props = {
  path: string;
  doc: Node;
  appearance: string;
  idiom: string;
  cssSize: number; // lado do icone na tela, em px CSS (512 x zoom)
  onDrawn: (ms: number) => void;
};

// Os nomes de arte que o documento usa, em qualquer especializacao.
function artNames(doc: Node): string[] {
  const out = new Set<string>();
  for (const g of groups(doc)) {
    for (const l of layers(g)) {
      if (typeof l["image-name"] === "string") out.add(l["image-name"]);
      const list = l["image-name-specializations"];
      if (Array.isArray(list)) for (const e of list as Node[]) if (typeof e.value === "string") out.add(e.value);
    }
  }
  return [...out];
}

// Um SVG sem width/height nao tem tamanho natural para o navegador. Os dois
// passam a ser o viewBox, que e a regua de pontos do nucleo (`placeOnCanvas`).
function svgArt(dataUrl: string): Promise<Art> {
  const text = new TextDecoder().decode(Uint8Array.from(atob(dataUrl.split(",")[1]), (c) => c.charCodeAt(0)));
  const m = text.match(/viewBox\s*=\s*["']\s*([-\d.eE]+)[\s,]+([-\d.eE]+)[\s,]+([-\d.eE]+)[\s,]+([-\d.eE]+)/);
  const [x, y, w, h] = m ? m.slice(1).map(Number) : [0, 0, 1024, 1024];
  const sized = text.replace(/<svg\b([^>]*)>/, (_all, attrs: string) => {
    const clean = attrs.replace(/\s(width|height)\s*=\s*("[^"]*"|'[^']*')/g, "");
    return `<svg${clean} width="${w}" height="${h}">`;
  });
  const url = URL.createObjectURL(new Blob([sized], { type: "image/svg+xml" }));
  return new Promise((resolve, reject) => {
    const img = new Image();
    img.onload = () => resolve({ img, x, y, w, h });
    img.onerror = () => reject(new Error("svg nao carregou"));
    img.src = url;
  });
}

function rasterArt(dataUrl: string): Promise<Art> {
  return new Promise((resolve, reject) => {
    const img = new Image();
    img.onload = () => resolve({ img, x: 0, y: 0, w: img.naturalWidth, h: img.naturalHeight });
    img.onerror = () => reject(new Error("imagem nao carregou"));
    img.src = dataUrl;
  });
}

export function LiveIcon({ path, doc, appearance, idiom, cssSize, onDrawn }: Props) {
  const canvas = useRef<HTMLCanvasElement>(null);
  const [arts, setArts] = useState<Arts | null>(null);

  // As artes: carregadas uma vez por documento.
  useEffect(() => {
    let live = true;
    setArts(null);
    Promise.all(
      artNames(doc).map(async (name) => {
        try {
          const data = await invoke<string>("read_asset", { path, name });
          const art = /\.svg$/i.test(name) ? await svgArt(data) : await rasterArt(data);
          return [name, art] as const;
        } catch {
          return null;
        }
      }),
    ).then((pairs) => {
      if (!live) return;
      setArts(new Map(pairs.filter((p): p is readonly [string, Art] => p !== null)));
    });
    return () => {
      live = false;
    };
  }, [path, doc]);

  // O desenho: a cada mudanca, no tamanho exato dos pixels da tela.
  useEffect(() => {
    const c = canvas.current;
    if (!c || !arts) return;
    const px = Math.max(1, Math.round(cssSize * window.devicePixelRatio));
    if (c.width !== px || c.height !== px) {
      c.width = px;
      c.height = px;
    }
    const raf = requestAnimationFrame(() => {
      const ms = drawIcon(c.getContext("2d")!, px, { doc, appearance, idiom, arts });
      onDrawn(ms);
    });
    return () => cancelAnimationFrame(raf);
  }, [arts, doc, appearance, idiom, cssSize, onDrawn]);

  return <canvas ref={canvas} className="icon" style={{ width: cssSize, height: cssSize }} />;
}
