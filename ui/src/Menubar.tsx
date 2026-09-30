import { useEffect, useRef, useState } from "react";

// A barra de menus do alvo (inventario §8): no macOS ela mora no topo da tela;
// aqui mora na barra de titulo, como num app do Windows. Os atalhos vivem no
// App (um so ouvinte de teclado); aqui eles so aparecem escritos.

export type MenuItem =
  | { label: string; shortcut?: string; action: () => void; disabled?: boolean; checked?: boolean }
  | "-";

export type Menu = { title: string; items: MenuItem[] };

export function Menubar({ menus }: { menus: Menu[] }) {
  const [open, setOpen] = useState<number | null>(null);
  const ref = useRef<HTMLDivElement>(null);

  // Clique fora ou Esc fecha.
  useEffect(() => {
    if (open === null) return;
    const onDown = (e: PointerEvent) => {
      if (!ref.current?.contains(e.target as Node)) setOpen(null);
    };
    const onKey = (e: KeyboardEvent) => e.key === "Escape" && setOpen(null);
    window.addEventListener("pointerdown", onDown);
    window.addEventListener("keydown", onKey);
    return () => {
      window.removeEventListener("pointerdown", onDown);
      window.removeEventListener("keydown", onKey);
    };
  }, [open]);

  return (
    <div className="menubar" ref={ref}>
      {menus.map((m, i) => (
        <div key={m.title} className="menubar-item">
          <button
            className={`menubar-title${open === i ? " on" : ""}`}
            onClick={() => setOpen(open === i ? null : i)}
            // Com um menu aberto, passar por cima troca de menu, como no sistema.
            onMouseEnter={() => open !== null && setOpen(i)}
          >
            {m.title}
          </button>
          {open === i && (
            <div className="menu drop">
              {m.items.map((it, j) =>
                it === "-" ? (
                  <div key={j} className="menu-sep" />
                ) : (
                  <button
                    key={j}
                    disabled={it.disabled}
                    className={it.checked ? "checked" : ""}
                    onClick={() => {
                      setOpen(null);
                      it.action();
                    }}
                  >
                    <span>{it.label}</span>
                    {it.shortcut && <span className="menu-key">{it.shortcut}</span>}
                  </button>
                ),
              )}
            </div>
          )}
        </div>
      ))}
    </div>
  );
}
