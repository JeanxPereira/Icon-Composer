import { useEffect, useState } from "react";
import { invoke } from "@tauri-apps/api/core";
import { getCurrentWindow } from "@tauri-apps/api/window";
import { Sym } from "./Sym";
import { displayName, groups, layers, Node, Selection } from "./doc";

// A lista do alvo: `LayerSidebar` -> `LayerList` -> linhas `IconRow`,
// `GroupRow`, `LayerRow` (inventario §3).

type Props = {
  path: string | null;
  docName: string;
  doc: Node | null;
  selection: Selection;
  onSelect: (s: Selection) => void;
  onToggleSidebar: () => void;
};

export function Sidebar({ path, docName, doc, selection, onSelect, onToggleSidebar }: Props) {
  const [collapsed, setCollapsed] = useState<Record<number, boolean>>({});
  const win = getCurrentWindow();
  const iconName = docName.replace(/\.icon$/i, "");

  return (
    <aside className="sidebar">
      <div className="sidebar-top" data-tauri-drag-region>
        <div className="traffic">
          <button className="light close" onClick={() => win.close()} aria-label="Fechar" />
          <button className="light min" onClick={() => win.minimize()} aria-label="Minimizar" />
          <button className="light max" onClick={() => win.toggleMaximize()} aria-label="Maximizar" />
        </div>
        <button className="round-btn" onClick={onToggleSidebar} title="Show Sidebar">
          <Sym name="sidebar.left" size={17} />
        </button>
      </div>

      <div className="tree">
        {doc && (
          <>
            <Row
              depth={0}
              selected={selection.kind === "icon"}
              onClick={() => onSelect({ kind: "icon" })}
              icon={<Sym name="square" size={14} />}
              label={iconName}
            />
            {groups(doc).map((g, gi) => {
              const open = !collapsed[gi];
              return (
                <div key={gi}>
                  <Row
                    depth={0}
                    disclosure={open ? "open" : "closed"}
                    onDisclose={() => setCollapsed((c) => ({ ...c, [gi]: open }))}
                    selected={selection.kind === "group" && selection.g === gi}
                    onClick={() => onSelect({ kind: "group", g: gi })}
                    icon={<Sym name="folder" size={16} />}
                    label={displayName(g, "Group")}
                    hidden={g.hidden === true}
                  />
                  {open &&
                    layers(g).map((l, li) => (
                      <Row
                        key={li}
                        depth={1}
                        selected={
                          selection.kind === "layer" && selection.g === gi && selection.l === li
                        }
                        onClick={() => onSelect({ kind: "layer", g: gi, l: li })}
                        icon={<Thumb path={path} image={l["image-name"] as string | undefined} />}
                        label={displayName(l, "Layer")}
                        hidden={l.hidden === true}
                        tall
                      />
                    ))}
                </div>
              );
            })}
          </>
        )}
      </div>

      <div className="sidebar-bottom">
        <button className="plain-btn" title="Opens menu to add new group or image layer" disabled>
          <Sym name="plus" size={13} />
        </button>
        <button className="plain-btn" title="Removes selected layers from the icon" disabled>
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
  disclosure?: "open" | "closed";
  onDisclose?: () => void;
  tall?: boolean;
};

function Row({ depth, selected, onClick, icon, label, hidden, disclosure, onDisclose, tall }: RowProps) {
  return (
    <div
      className={`row${selected ? " selected" : ""}${tall ? " tall" : ""}`}
      style={{ paddingLeft: 10 + depth * 28 }}
      onClick={onClick}
    >
      <span
        className="disclosure"
        onClick={(e) => {
          e.stopPropagation();
          onDisclose?.();
        }}
      >
        {disclosure && <Sym name="chevron.down" size={9} className={disclosure === "closed" ? "rot" : ""} />}
      </span>
      <span className="row-icon">{icon}</span>
      <span className="row-label">{label}</span>
      {hidden && (
        <span className="row-hidden" title="Toggles layer visibility">
          <Sym name="eye.slash" size={15} />
        </span>
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
