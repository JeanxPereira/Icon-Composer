import { useEffect, useState } from "react";
import { invoke } from "@tauri-apps/api/core";
import { Sym } from "./Sym";
import { TrafficLights } from "./TrafficLights";
import { displayName, groups, layers, Node, Selection } from "./doc";

// A lista do alvo: `LayerSidebar` -> `LayerList` -> linhas `IconRow`,
// `GroupRow`, `LayerRow` (inventario §3). Toda acao vai ao nucleo pelo App.

export type SidebarActions = {
  addGroup: () => void;
  addImage: () => void;
  remove: () => void;
  toggleHidden: (sel: Selection, hidden: boolean) => void;
  rename: (sel: Selection, name: string) => void;
  // Negativo vai para a frente (indice 0); |delta| > 1 anda ate a borda.
  move: (sel: Selection, delta: number) => void;
  duplicate: () => void;
};

type Props = {
  path: string | null;
  docName: string;
  doc: Node | null;
  selection: Selection;
  onSelect: (s: Selection) => void;
  onToggleSidebar: () => void;
  actions: SidebarActions;
  dirty: boolean;
};

export function Sidebar({ path, docName, doc, selection, onSelect, onToggleSidebar, actions, dirty }: Props) {
  const [collapsed, setCollapsed] = useState<Record<number, boolean>>({});
  const [addMenu, setAddMenu] = useState(false);
  const [editing, setEditing] = useState<string | null>(null);
  const iconName = docName.replace(/\.icon$/i, "");
  const key = (s: Selection) => (s.kind === "icon" ? "i" : s.kind === "group" ? `g${s.g}` : `l${s.g}.${s.l}`);
  const same = (a: Selection, b: Selection) => key(a) === key(b);

  // Teclado da lista: Delete remove, F2 / Enter renomeia, Alt+setas move,
  // Ctrl+D duplica -- como no inspetor antigo.
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const t = e.target as HTMLElement;
      if (t.tagName === "INPUT" || t.tagName === "SELECT" || editing || selection.kind === "icon") return;
      if (e.key === "Delete" || e.key === "Backspace") {
        e.preventDefault();
        actions.remove();
      } else if (e.key === "F2" || e.key === "Enter") {
        e.preventDefault();
        setEditing(key(selection));
      } else if (e.altKey && (e.key === "ArrowUp" || e.key === "ArrowDown")) {
        e.preventDefault();
        actions.move(selection, e.key === "ArrowUp" ? -1 : 1);
      } else if (e.ctrlKey && e.key.toLowerCase() === "d") {
        e.preventDefault();
        actions.duplicate();
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [selection, editing, actions]);

  const row = (sel: Selection, depth: number, icon: React.ReactNode, label: string, extra: Partial<RowProps> = {}) => (
    <Row
      key={key(sel)}
      depth={depth}
      selected={same(sel, selection)}
      onClick={() => onSelect(sel)}
      icon={icon}
      label={label}
      editing={editing === key(sel)}
      onStartEdit={sel.kind === "icon" ? undefined : () => setEditing(key(sel))}
      onRename={(name) => {
        setEditing(null);
        if (name && name !== label) actions.rename(sel, name);
      }}
      {...extra}
    />
  );

  return (
    <aside className="sidebar">
      <div className="sidebar-top" data-tauri-drag-region>
        <TrafficLights dirty={dirty} />
        <button className="round-btn" onClick={onToggleSidebar} title="Show Sidebar">
          <Sym name="sidebar.left" size={17} />
        </button>
      </div>

      <div className="tree">
        {doc && (
          <>
            {row({ kind: "icon" }, 0, <Sym name="square" size={14} />, iconName)}
            {groups(doc).map((g, gi) => {
              const open = !collapsed[gi];
              const gsel: Selection = { kind: "group", g: gi };
              return (
                <div key={gi}>
                  {row(gsel, 0, <Sym name="folder" size={16} />, displayName(g, "Group"), {
                    disclosure: open ? "open" : "closed",
                    onDisclose: () => setCollapsed((c) => ({ ...c, [gi]: open })),
                    hidden: g.hidden === true,
                    onToggleHidden: () => actions.toggleHidden(gsel, g.hidden !== true),
                  })}
                  {open &&
                    layers(g).map((l, li) => {
                      const lsel: Selection = { kind: "layer", g: gi, l: li };
                      return row(lsel, 1, <Thumb path={path} image={l["image-name"] as string | undefined} />, displayName(l, "Layer"), {
                        hidden: l.hidden === true,
                        onToggleHidden: () => actions.toggleHidden(lsel, l.hidden !== true),
                        tall: true,
                      });
                    })}
                </div>
              );
            })}
          </>
        )}
      </div>

      <div className="sidebar-bottom">
        <div className="add-wrap">
          <button className="plain-btn" title="Opens menu to add new group or image layer" disabled={!doc} onClick={() => setAddMenu((v) => !v)}>
            <Sym name="plus" size={13} />
          </button>
          {addMenu && (
            <div className="menu" onMouseLeave={() => setAddMenu(false)}>
              <button
                onClick={() => {
                  setAddMenu(false);
                  actions.addImage();
                }}
              >
                New Image…
              </button>
              <button
                onClick={() => {
                  setAddMenu(false);
                  actions.addGroup();
                }}
              >
                New Group
              </button>
            </div>
          )}
        </div>
        <button className="plain-btn" title="Removes selected layers from the icon" disabled={!doc || selection.kind === "icon"} onClick={actions.remove}>
          <Sym name="minus" size={13} />
        </button>
      </div>
    </aside>
  );
}

type RowProps = {
  depth: number;
  selected: boolean;
  onClick: () => void;
  icon: React.ReactNode;
  label: string;
  hidden?: boolean;
  onToggleHidden?: () => void;
  disclosure?: "open" | "closed";
  onDisclose?: () => void;
  tall?: boolean;
  editing?: boolean;
  onStartEdit?: () => void;
  onRename?: (name: string) => void;
};

function Row(p: RowProps) {
  const [text, setText] = useState(p.label);
  useEffect(() => setText(p.label), [p.label, p.editing]);
  return (
    <div
      className={`row${p.selected ? " selected" : ""}${p.tall ? " tall" : ""}${p.hidden ? " is-hidden" : ""}`}
      style={{ paddingLeft: 10 + p.depth * 28 }}
      onClick={p.onClick}
      onDoubleClick={p.onStartEdit}
    >
      <span
        className="disclosure"
        onClick={(e) => {
          e.stopPropagation();
          p.onDisclose?.();
        }}
      >
        {p.disclosure && <Sym name="chevron.down" size={9} className={p.disclosure === "closed" ? "rot" : ""} />}
      </span>
      <span className="row-icon">{p.icon}</span>
      {p.editing ? (
        <input
          className="row-edit"
          autoFocus
          value={text}
          onClick={(e) => e.stopPropagation()}
          onChange={(e) => setText(e.target.value)}
          onBlur={() => p.onRename?.(text.trim())}
          onKeyDown={(e) => {
            if (e.key === "Enter") p.onRename?.(text.trim());
            if (e.key === "Escape") p.onRename?.(p.label);
          }}
        />
      ) : (
        <span className="row-label">{p.label}</span>
      )}
      {p.onToggleHidden && (
        <button
          className={`row-hidden${p.hidden ? " on" : ""}`}
          title="Toggles layer visibility"
          onClick={(e) => {
            e.stopPropagation();
            p.onToggleHidden?.();
          }}
        >
          <Sym name={p.hidden ? "eye.slash" : "eye"} size={15} />
        </button>
      )}
    </div>
  );
}

// A miniatura da camada sobre o xadrez (`Checkerboard` no alvo).
function Thumb({ path, image }: { path: string | null; image?: string }) {
  const [src, setSrc] = useState<string | null>(null);
  useEffect(() => {
    let live = true;
    setSrc(null);
    if (path && image) {
      invoke<string>("read_asset", { path, name: image })
        .then((s) => live && setSrc(s))
        .catch(() => {});
    }
    return () => {
      live = false;
    };
  }, [path, image]);
  return <span className="thumb">{src && <img src={src} alt="" />}</span>;
}
