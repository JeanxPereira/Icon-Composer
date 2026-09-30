import { useCallback, useEffect, useRef, useState } from "react";
import { invoke } from "@tauri-apps/api/core";
import { open, save } from "@tauri-apps/plugin-dialog";
import { getCurrentWebview } from "@tauri-apps/api/webview";
import { Sidebar, SidebarActions } from "./Sidebar";
import { Background, BACKGROUNDS, Canvas, EffectsMode } from "./Canvas";
import { Edit, Inspector, Pane } from "./Inspector";
import { Menu, Menubar } from "./Menubar";
import { ExportChoice, ExportSheet } from "./ExportSheet";
import { groups, layers, Node, nodeAt, Platform, Rendition, RENDITIONS, resolve, Selection, supportedPlatforms, writeScope } from "./doc";
import {
  blobToBase64,
  coreHistory,
  coreImport,
  coreNode,
  coreOpen,
  coreRects,
  coreSave,
  coreSet,
  Frame,
  frameToDataUrl,
  frameToPng,
  LayerRect,
  NodeOp,
  requestFrame,
} from "./core";
import "./App.css";

type Opened = { name: string; json: string; assets: string[] };

const THUMB = 128;
// Ate aqui o canvas vem inteiro; acima, vem uma BASE inteira (esticada) e o
// LADRILHO visivel na resolucao da tela por cima.
const FULL_MAX_PX = 2048;
const BASE_PX = 1024;
const MAX_ZOOM = 16;

// Segmentos por cubica: 16 e o padrao do nucleo e basta a 100 %; com zoom, uma
// curva viraria poligono visivel.
const subdivisionsFor = (zoom: number) => Math.min(256, Math.max(16, Math.round(16 * zoom)));

export type ViewRect = { x: number; y: number; w: number; h: number }; // px CSS, no quadrado do icone

export default function App() {
  const [path, setPath] = useState<string | null>(null);
  const [docName, setDocName] = useState("");
  const [doc, setDoc] = useState<Node | null>(null);
  const [assets, setAssets] = useState<string[]>([]);
  // `rev` sobe a cada documento que o nucleo devolve: e o que pede um quadro novo.
  const [rev, setRev] = useState(0);
  const [dirty, setDirty] = useState(false);
  const edits = useRef<Promise<void>>(Promise.resolve());
  const [selection, setSelection] = useState<Selection>({ kind: "icon" });
  const [rendition, setRendition] = useState<Rendition>("default");
  const [platform, setPlatform] = useState<Platform>("iOS");
  const [pane, setPane] = useState<Pane>("content");
  const [sidebarHidden, setSidebarHidden] = useState(false);
  const [effects, setEffects] = useState<EffectsMode>("gen27");
  const [background, setBackground] = useState<Background>({ kind: "image", url: `/apple/backgrounds/${BACKGROUNDS[0]}` });
  // Os fundos que a pessoa acrescentou ("Add Background..."), como data URL.
  const [userBackgrounds, setUserBackgrounds] = useState<string[]>([]);
  const [exporting, setExporting] = useState(false);
  const [grid, setGrid] = useState(false);
  const [snap, setSnap] = useState(true);
  const [gridStyle, setGridStyle] = useState<"light" | "dark">("dark");
  const [zoom, setZoom] = useState(1);

  const [frame, setFrame] = useState<Frame | null>(null);
  // O ladrilho guarda o documento (`rev`) para o qual foi feito: um ladrilho de
  // antes de uma edicao mostraria a camada no lugar velho por cima da base.
  const [tile, setTile] = useState<(Frame & { rev: number }) | null>(null);
  const [view, setView] = useState<ViewRect | null>(null);
  const [thumbs, setThumbs] = useState<Record<string, string>>({});
  const [rects, setRects] = useState<LayerRect[]>([]);
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);

  const load = useCallback(async (p: string) => {
    try {
      const d = await invoke<Opened>("open_document", { path: p });
      const parsed = JSON.parse(d.json) as Node;
      await coreOpen(p);
      setFrame(null);
      setTile(null);
      setPath(p);
      setDocName(d.name);
      setDoc(parsed);
      setAssets(d.assets);
      setDirty(false);
      setRev((r) => r + 1);
      setSelection({ kind: "icon" });
      setError("");
      const plats = supportedPlatforms(parsed);
      setPlatform(plats[0] ?? "iOS");
    } catch (e) {
      setError(String(e));
    }
  }, []);

  // File > New: um `.icon` vazio gravado onde a pessoa escolher, ja aberto.
  const newDoc = async () => {
    const target = await save({
      title: "New Icon",
      defaultPath: "Untitled.icon",
      filters: [{ name: "Icon", extensions: ["icon"] }],
    });
    if (!target) return;
    try {
      load(await invoke<string>("new_document", { path: target }));
    } catch (e) {
      setError(String(e));
    }
  };

  const pick = async () => {
    // Um `.icon` e uma PASTA no Windows, entao o seletor e de diretorio.
    const chosen = await open({ directory: true, title: "Abrir .icon" });
    if (typeof chosen === "string") load(chosen);
  };

  const appearanceOf = (r: Rendition) => RENDITIONS.find((x) => x.id === r)!.appearance;

  // A escrita: em FILA, porque um arraste manda varias e elas tem de chegar ao
  // nucleo na ordem. Cada resposta e o documento inteiro, que vira o da UI.
  const adopt = (json: string) => {
    setDoc(JSON.parse(json) as Node);
    setRev((r) => r + 1);
  };
  const onEdit: Edit = (sel, scope, prop, value, coalesce) => {
    const g = sel.kind === "icon" ? -1 : sel.g;
    const l = sel.kind === "layer" ? sel.l : -1;
    edits.current = edits.current
      .then(() => coreSet(g, l, scope, prop, value, coalesce))
      .then((json) => {
        adopt(json);
        setDirty(true);
      })
      .catch((e) => setError(String(e)));
  };

  // A estrutura, pelo nucleo; `then` escolhe a selecao a partir do documento novo.
  const structural = (op: NodeOp, g: number, l: number, arg: string, then: (d: Node) => Selection) => {
    edits.current = edits.current
      .then(() => coreNode(op, g, l, arg))
      .then((json) => {
        const d = JSON.parse(json) as Node;
        setDoc(d);
        setRev((r) => r + 1);
        setDirty(true);
        setSelection(then(d));
      })
      .catch((e) => setError(String(e)));
  };
  // Arrastar no canvas (`DragState.Operation.move`): `dx`/`dy` sao o
  // deslocamento TOTAL desde o inicio, em pontos do canvas. A posicao lida e a
  // que vale no contexto mostrado, e a escrita cai na entrada que ganha nele.
  // Uma camada anda no espaco do grupo, entao o deslocamento se divide pela
  // escala dele. O primeiro passo abre a entrada no desfazer; os outros juntam.
  const moveStart = useRef<{ s: number; x: number; y: number; gs: number } | null>(null);
  const onMove = (sel: Selection, dx: number, dy: number, first: boolean) => {
    if (!doc || sel.kind === "icon") return;
    const appearance = appearanceOf(rendition);
    const node = nodeAt(doc, sel);
    type Pos = { scale?: number; "translation-in-points"?: number[] } | undefined;
    if (first || !moveStart.current) {
      const pos = resolve(node, "position", appearance, platform) as Pos;
      const gp = sel.kind === "layer" ? (resolve(groups(doc)[sel.g], "position", appearance, platform) as Pos) : undefined;
      moveStart.current = {
        s: pos?.scale ?? 1,
        x: pos?.["translation-in-points"]?.[0] ?? 0,
        y: pos?.["translation-in-points"]?.[1] ?? 0,
        gs: gp?.scale || 1,
      };
    }
    const st = moveStart.current;
    const value = {
      scale: st.s,
      "translation-in-points": [Math.round(st.x + dx / st.gs), Math.round(st.y + dy / st.gs)],
    };
    onEdit(sel, writeScope(node, "position", appearance, platform, false), "position", value, !first);
  };

  // Imagens para dentro do documento (New Image..., ou soltas na janela): cada
  // uma vira uma camada NA FRENTE do grupo selecionado (o indice 0 e a frente),
  // na ordem da lista -- a primeira fica em cima. Sem grupo, cria um.
  const importFiles = (files: string[]) => {
    const imgs = files.filter((f) => /\.(svg|png)$/i.test(f));
    if (!doc || !imgs.length) return;
    const hasGroup = groups(doc).length > 0;
    const g = hasGroup ? groupOf(selection) : 0;
    edits.current = edits.current
      .then(async () => {
        if (!hasGroup) await coreNode("add-group", -1, -1, "Group");
        let json = "";
        for (const f of [...imgs].reverse()) {
          const name = await coreImport(f);
          setAssets((a) => (a.includes(name) ? a : [...a, name].sort()));
          const d = JSON.parse(await coreNode("add-layer", g, -1, name)) as Node;
          const n = layers(groups(d)[g]).length;
          json = n > 1 ? await coreNode("move", g, n - 1, "-9999") : JSON.stringify(d);
        }
        return json;
      })
      .then((json) => {
        adopt(json);
        setDirty(true);
        setSelection({ kind: "layer", g, l: 0 });
      })
      .catch((e) => setError(String(e)));
  };

  // Arquivos soltos na janela: um `.icon` abre; imagens entram no documento.
  const dropRef = useRef<(paths: string[]) => void>(() => {});
  dropRef.current = (paths) => {
    const icon = paths.find((f) => /\.icon[\\/]?$/i.test(f));
    if (icon) load(icon.replace(/[\\/]$/, ""));
    else importFiles(paths);
  };
  const [dropping, setDropping] = useState(false);
  useEffect(() => {
    const un = getCurrentWebview().onDragDropEvent((e) => {
      if (e.payload.type === "over" || e.payload.type === "enter") setDropping(true);
      else if (e.payload.type === "leave") setDropping(false);
      else if (e.payload.type === "drop") {
        setDropping(false);
        dropRef.current(e.payload.paths);
      }
    });
    return () => {
      un.then((f) => f());
    };
  }, []);

  const at = (sel: Selection): [number, number] => [sel.kind === "icon" ? -1 : sel.g, sel.kind === "layer" ? sel.l : -1];
  const groupOf = (sel: Selection) => (sel.kind === "icon" ? 0 : sel.g);

  const sidebarActions: SidebarActions = {
    addGroup: () => structural("add-group", -1, -1, "Group", (d) => ({ kind: "group", g: groups(d).length - 1 })),
    addImage: async () => {
      const files = await open({
        title: "New Image",
        multiple: true,
        filters: [{ name: "Image", extensions: ["svg", "png"] }],
      });
      if (Array.isArray(files)) importFiles(files);
      else if (typeof files === "string") importFiles([files]);
    },
    remove: () => {
      if (selection.kind === "icon") return;
      const [g, l] = at(selection);
      structural("remove", g, l, "", () => (selection.kind === "layer" ? { kind: "group", g } : { kind: "icon" }));
    },
    duplicate: () => {
      if (selection.kind === "icon") return;
      const [g, l] = at(selection);
      structural("duplicate", g, l, "", () =>
        selection.kind === "layer" ? { kind: "layer", g, l: l + 1 } : { kind: "group", g: g + 1 },
      );
    },
    toggleHidden: (sel, hidden) => onEdit(sel, { appearance: "", idiom: "" }, "hidden", hidden),
    rename: (sel, name) => {
      const [g, l] = at(sel);
      structural("rename", g, l, name, () => sel);
    },
    move: (sel, delta) => {
      const [g, l] = at(sel);
      structural("move", g, l, String(delta), (d) => {
        if (sel.kind === "layer") {
          const n = layers(groups(d)[g]).length;
          return { kind: "layer", g, l: Math.min(n - 1, Math.max(0, l + delta)) };
        }
        return { kind: "group", g: Math.min(groups(d).length - 1, Math.max(0, g + delta)) };
      });
    },
  };

  // Desfazer, refazer e salvar, pelo nucleo.
  const history = (step: "undo" | "redo") => {
    edits.current = edits.current
      .then(() => coreHistory(step))
      .then((json) => {
        adopt(json);
        setDirty(true);
      })
      .catch(() => {});
  };
  const saveDoc = () => {
    edits.current = edits.current
      .then(() => coreSave())
      .then(() => setDirty(false))
      .catch((err) => setError(String(err)));
  };

  // Um PNG do documento como esta no nucleo (editado ou nao).
  const renderPng = async (c: ExportChoice) => {
    await edits.current;
    const f = await requestFrame("export", {
      size: c.size,
      appearance: appearanceOf(c.rendition),
      idiom: c.platform,
      subdivisions: subdivisionsFor(c.size / 512),
      effects: effects !== "disabled",
    });
    return frameToPng(f);
  };
  const exportIcon = async (c: ExportChoice) => {
    setExporting(false);
    const base = docName.replace(/\.icon$/i, "") || "Icon";
    const suffix = c.rendition === "default" ? "" : `-${c.rendition}`;
    const target = await save({
      title: "Export Icon as Image",
      defaultPath: `${base}${suffix}-${c.size}.png`,
      filters: [{ name: "PNG", extensions: ["png"] }],
    });
    if (!target) return;
    try {
      const png = await renderPng(c);
      await invoke("write_file", { path: target, base64: await blobToBase64(png) });
    } catch (e) {
      setError(String(e));
    }
  };
  const copyImage = async () => {
    try {
      const png = await renderPng({ platform, rendition, size: 1024 });
      await navigator.clipboard.write([new ClipboardItem({ "image/png": png })]);
    } catch (e) {
      setError(String(e));
    }
  };

  const selectParent = () => setSelection((s) => (s.kind === "layer" ? { kind: "group", g: s.g } : { kind: "icon" }));
  // O indice 0 e a FRENTE (o render compoe de tras para a frente invertendo).
  const arrange = (delta: number) => {
    if (selection.kind !== "icon") sidebarActions.move(selection, delta);
  };
  const zoomBy = (f: number) => setZoom((z) => Math.min(MAX_ZOOM, Math.max(0.25, z * f)));

  const addBackground = async () => {
    const file = await open({
      title: "Add Background",
      multiple: false,
      filters: [{ name: "Image", extensions: ["png", "jpg", "jpeg", "webp", "bmp"] }],
    });
    if (typeof file !== "string") return;
    try {
      const url = await invoke<string>("read_image", { path: file });
      setUserBackgrounds((b) => [...b, url]);
      setBackground({ kind: "image", url });
    } catch (e) {
      setError(String(e));
    }
  };

  // "Replace..." da linha Image: importa o arquivo e aponta a camada para ele.
  const replaceImage = async () => {
    if (selection.kind !== "layer") return;
    const file = await open({
      title: "Replace Image",
      multiple: false,
      filters: [{ name: "Image", extensions: ["svg", "png"] }],
    });
    if (typeof file !== "string") return;
    const sel = selection;
    edits.current = edits.current
      .then(async () => {
        const name = await coreImport(file);
        setAssets((a) => (a.includes(name) ? a : [...a, name].sort()));
        return coreSet(sel.g, sel.l, { appearance: "", idiom: "" }, "image-name", name);
      })
      .then((json) => {
        adopt(json);
        setDirty(true);
      })
      .catch((e) => setError(String(e)));
  };

  const hasDoc = !!path;
  const member = selection.kind !== "icon";
  const menus: Menu[] = [
    {
      title: "File",
      items: [
        { label: "New", shortcut: "Ctrl+N", action: newDoc },
        { label: "Open…", shortcut: "Ctrl+O", action: pick },
        { label: "Save", shortcut: "Ctrl+S", action: saveDoc, disabled: !hasDoc },
        "-",
        { label: "Export…", shortcut: "Ctrl+Shift+E", action: () => setExporting(true), disabled: !hasDoc },
        { label: "Copy Icon as Image", shortcut: "Ctrl+Shift+C", action: copyImage, disabled: !hasDoc },
      ],
    },
    {
      title: "Edit",
      items: [
        { label: "Undo", shortcut: "Ctrl+Z", action: () => history("undo"), disabled: !hasDoc },
        { label: "Redo", shortcut: "Ctrl+Y", action: () => history("redo"), disabled: !hasDoc },
        "-",
        { label: "Duplicate", shortcut: "Ctrl+D", action: sidebarActions.duplicate, disabled: !member },
        { label: "Delete", shortcut: "Del", action: sidebarActions.remove, disabled: !member },
        "-",
        { label: "Select Parent", shortcut: "Ctrl+↑", action: selectParent, disabled: !member },
        { label: "Deselect All", shortcut: "Esc", action: () => setSelection({ kind: "icon" }), disabled: !member },
      ],
    },
    {
      title: "View",
      items: [
        { label: sidebarHidden ? "Show Sidebar" : "Hide Sidebar", shortcut: "Ctrl+Alt+S", action: () => setSidebarHidden((h) => !h) },
        { label: "Style Inspector", shortcut: "Ctrl+1", action: () => setPane("content"), checked: pane === "content" },
        { label: "Document Settings Inspector", shortcut: "Ctrl+2", action: () => setPane("document"), checked: pane === "document" },
        "-",
        { label: "Zoom In", shortcut: "Ctrl+=", action: () => zoomBy(1.25) },
        { label: "Zoom Out", shortcut: "Ctrl+-", action: () => zoomBy(1 / 1.25) },
        { label: "Actual Size", shortcut: "Ctrl+0", action: () => setZoom(1) },
        "-",
        { label: "Snap to Guides", shortcut: "Ctrl+;", action: () => setSnap((v) => !v), checked: snap },
        { label: grid ? "Hide Grid" : "Show Grid", shortcut: "Ctrl+'", action: () => setGrid((g) => !g) },
      ],
    },
    {
      title: "Arrange",
      items: [
        { label: "Bring Forward", shortcut: "Ctrl+]", action: () => arrange(-1), disabled: !member },
        { label: "Bring to Front", shortcut: "Ctrl+Shift+]", action: () => arrange(-9999), disabled: !member },
        { label: "Send Backward", shortcut: "Ctrl+[", action: () => arrange(1), disabled: !member },
        { label: "Send to Back", shortcut: "Ctrl+Shift+[", action: () => arrange(9999), disabled: !member },
      ],
    },
  ];

  // Um ouvinte so para os atalhos dos menus; os da lista (Delete, F2,
  // Alt+setas, Ctrl+D) moram na Sidebar. A ref deixa o ouvinte ver o estado novo.
  const keys = useRef<(e: KeyboardEvent) => void>(() => {});
  keys.current = (e: KeyboardEvent) => {
    const t = e.target as HTMLElement;
    const typing = t.tagName === "INPUT" || t.tagName === "SELECT";
    if (exporting || typing) return;
    if (e.key === "Escape" && member) {
      setSelection({ kind: "icon" });
      return;
    }
    if (!e.ctrlKey) return;
    const run = (f: () => void) => {
      e.preventDefault();
      f();
    };
    const c = e.code;
    if (c === "KeyO") run(pick);
    else if (c === "KeyN") run(newDoc);
    else if (!hasDoc) return;
    else if (c === "KeyZ") run(() => history(e.shiftKey ? "redo" : "undo"));
    else if (c === "KeyY") run(() => history("redo"));
    else if (c === "KeyS" && e.altKey) run(() => setSidebarHidden((h) => !h));
    else if (c === "KeyS") run(saveDoc);
    else if (c === "KeyE" && e.shiftKey) run(() => setExporting(true));
    else if (c === "KeyC" && e.shiftKey) run(copyImage);
    else if (c === "Digit1") run(() => setPane("content"));
    else if (c === "Digit2") run(() => setPane("document"));
    else if (c === "Equal" || c === "NumpadAdd") run(() => zoomBy(1.25));
    else if (c === "Minus" || c === "NumpadSubtract") run(() => zoomBy(1 / 1.25));
    else if (c === "Digit0" || c === "Numpad0") run(() => setZoom(1));
    else if (c === "Quote" || c === "Backquote") run(() => setGrid((g) => !g));
    else if (c === "Semicolon" || c === "Slash") run(() => setSnap((v) => !v));
    else if (c === "ArrowUp" && !e.altKey && member) run(selectParent);
    else if (c === "BracketRight" && member) run(() => arrange(e.shiftKey ? -9999 : -1));
    else if (c === "BracketLeft" && member) run(() => arrange(e.shiftKey ? 9999 : 1));
  };
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => keys.current(e);
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, []);

  const dpr = window.devicePixelRatio;
  const fullPx = Math.round(512 * zoom * dpr);
  const tiled = fullPx > FULL_MAX_PX;

  // O canvas inteiro: no tamanho da tela ate FULL_MAX_PX, ou a base de
  // BASE_PX quando o zoom passa disso (o ladrilho cobre o que se ve).
  //
  // Um quadro que chega depois de o documento ter mudado NAO e descartado: num
  // arraste o documento muda a cada passo, e descartar o que estava em voo
  // deixava o canvas parado ate soltar. Vale o mais novo PEDIDO (a sequencia),
  // e so do documento aberto.
  const frameSeq = useRef(0);
  const shownSeq = useRef(0);
  const pathRef = useRef(path);
  pathRef.current = path;
  useEffect(() => {
    if (!path) return;
    const seq = ++frameSeq.current;
    const forPath = path;
    const size = tiled ? BASE_PX : fullPx;
    setBusy(true);
    requestFrame("canvas", { size, appearance: appearanceOf(rendition), idiom: platform, subdivisions: subdivisionsFor(size / (512 * dpr)), effects: effects !== "disabled" })
      .then((f) => {
        if (seq < shownSeq.current || pathRef.current !== forPath) return;
        shownSeq.current = seq;
        setFrame(f);
        setError("");
      })
      .catch((e) => e !== "substituido" && seq >= shownSeq.current && setError(String(e)))
      .finally(() => seq === frameSeq.current && setBusy(false));
  }, [path, platform, rendition, fullPx, tiled, rev, effects]);

  // O ladrilho: so a parte visivel, na resolucao da tela, pedida de novo a
  // cada rolagem ou zoom.
  useEffect(() => {
    if (!path || !tiled || !view) {
      setTile(null);
      return;
    }
    let alive = true;
    const x = Math.max(0, Math.floor(view.x * dpr));
    const y = Math.max(0, Math.floor(view.y * dpr));
    const w = Math.min(fullPx - x, Math.ceil(view.w * dpr));
    const h = Math.min(fullPx - y, Math.ceil(view.h * dpr));
    if (w <= 0 || h <= 0) return;
    const timer = window.setTimeout(() => {
      requestFrame("canvas-tile", {
        size: fullPx,
        appearance: appearanceOf(rendition),
        idiom: platform,
        tile: [x, y, w, h],
        subdivisions: subdivisionsFor(zoom),
        effects: effects !== "disabled",
      })
        .then((f) => alive && setTile({ ...f, rev }))
        .catch((e) => alive && e !== "substituido" && setError(String(e)));
    }, 60);
    return () => {
      alive = false;
      window.clearTimeout(timer);
    };
  }, [path, platform, rendition, fullPx, tiled, view, zoom, rev, effects]);

  // Os retangulos das camadas, para o destaque e o clique no canvas: a cada
  // documento novo do nucleo e a cada troca do contexto que o canvas mostra.
  useEffect(() => {
    if (!path) {
      setRects([]);
      return;
    }
    let alive = true;
    coreRects(appearanceOf(rendition), platform)
      .then((r) => alive && setRects(r))
      .catch(() => {});
    return () => {
      alive = false;
    };
  }, [path, rev, rendition, platform]);

  // As miniaturas da barra de rendicoes, do mesmo nucleo, depois do canvas.
  useEffect(() => {
    if (!path || !doc) return;
    let alive = true;
    const jobs: [string, string, string][] = [
      ...RENDITIONS.map((r) => [`r:${r.id}`, platform, r.appearance] as [string, string, string]),
      ...supportedPlatforms(doc).map((pl) => [`p:${pl}`, pl, appearanceOf(rendition)] as [string, string, string]),
    ];
    // Espera a edicao assentar: um arraste nao refaz cinco miniaturas por passo.
    const timer = window.setTimeout(() => {
      for (const [key, idiom, appearance] of jobs) {
        requestFrame(`thumb:${key}`, { size: THUMB, idiom, appearance, effects: effects !== "disabled" })
          .then((f) => alive && setThumbs((t) => ({ ...t, [key]: frameToDataUrl(f) })))
          .catch(() => {});
      }
    }, 250);
    return () => {
      alive = false;
      window.clearTimeout(timer);
    };
  }, [path, doc, platform, rendition, effects]);

  // O fundo do viewport cobre a JANELA INTEIRA; a barra lateral e o inspetor
  // sao vidro fosco sobre ele, como no alvo.
  const backdrop =
    background.kind === "image" ? { backgroundImage: `url("${background.url}")` } : { background: background.color };

  return (
    <div className={`window${sidebarHidden ? " no-sidebar" : ""}`} style={backdrop}>
      {!sidebarHidden && (
        <Sidebar
          path={path}
          docName={docName}
          doc={doc}
          selection={selection}
          onSelect={setSelection}
          onToggleSidebar={() => setSidebarHidden(true)}
          actions={sidebarActions}
        />
      )}
      <Canvas
        title={docName ? `${docName}${dirty ? " — editado" : ""}` : "Icon Composer"}
        frame={frame}
        tile={tiled && tile && tile.rev === rev && tile.size === fullPx ? tile : null}
        onView={setView}
        rects={rects}
        onMove={onMove}
        snap={snap}
        selection={selection}
        onSelect={setSelection}
        busy={busy}
        error={error}
        thumbs={thumbs}
        rendition={rendition}
        onRendition={setRendition}
        platforms={doc ? supportedPlatforms(doc) : []}
        platform={platform}
        onPlatform={setPlatform}
        effects={effects}
        onEffects={setEffects}
        background={background}
        onBackground={setBackground}
        userBackgrounds={userBackgrounds}
        onAddBackground={addBackground}
        menubar={<Menubar menus={menus} />}
        grid={grid}
        onGrid={setGrid}
        gridStyle={gridStyle}
        onGridStyle={setGridStyle}
        zoom={zoom}
        onZoom={(z) => setZoom(Math.min(MAX_ZOOM, Math.max(0.25, z)))}
        onOpen={pick}
        sidebarHidden={sidebarHidden}
        onToggleSidebar={() => setSidebarHidden(false)}
      />
      <Inspector
        doc={doc}
        assets={assets}
        selection={selection}
        rendition={rendition}
        platform={platform}
        pane={pane}
        onPane={setPane}
        onEdit={onEdit}
        onRendition={setRendition}
        onReplaceImage={replaceImage}
      />
      {dropping && (
        <div className="drop-hint">
          <span>{doc ? "Solte para adicionar as imagens (SVG, PNG)" : "Solte um .icon para abrir"}</span>
        </div>
      )}
      {exporting && (
        <ExportSheet
          platforms={doc ? supportedPlatforms(doc) : ["iOS"]}
          initial={{ platform, rendition, size: 1024 }}
          onExport={exportIcon}
          onCancel={() => setExporting(false)}
        />
      )}
    </div>
  );
}
