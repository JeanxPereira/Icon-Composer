import { useState } from "react";
import { Platform, Rendition, RENDITIONS } from "./doc";

// A folha "Export Icon as Image" do alvo (`ExportSheet`, `ExportOptions`,
// inventario §9): plataforma, aparencia e tamanho. O PNG sai do mesmo nucleo
// que desenha o canvas.

export type ExportChoice = { platform: Platform; rendition: Rendition; size: number };

const SIZES = [1024, 512, 256, 128, 64, 32, 16];

export function ExportSheet({
  platforms,
  initial,
  onExport,
  onCancel,
}: {
  platforms: Platform[];
  initial: ExportChoice;
  onExport: (c: ExportChoice) => void;
  onCancel: () => void;
}) {
  const [c, setC] = useState<ExportChoice>(initial);
  return (
    <div className="sheet-backdrop" onPointerDown={(e) => e.target === e.currentTarget && onCancel()}>
      <div className="sheet" onKeyDown={(e) => e.key === "Escape" && onCancel()}>
        <h2>Export Icon as Image</h2>
        <label className="sheet-row">
          <span>Platform</span>
          <select value={c.platform} onChange={(e) => setC({ ...c, platform: e.target.value as Platform })}>
            {platforms.map((p) => (
              <option key={p} value={p}>
                {p === "iOS" ? "iOS, macOS" : p}
              </option>
            ))}
          </select>
        </label>
        <label className="sheet-row">
          <span>Appearance</span>
          <select value={c.rendition} onChange={(e) => setC({ ...c, rendition: e.target.value as Rendition })}>
            {RENDITIONS.map((r) => (
              <option key={r.id} value={r.id}>
                {r.label}
              </option>
            ))}
          </select>
        </label>
        <label className="sheet-row">
          <span>Size</span>
          <select value={c.size} onChange={(e) => setC({ ...c, size: Number(e.target.value) })}>
            {SIZES.map((s) => (
              <option key={s} value={s}>
                {s} × {s} px
              </option>
            ))}
          </select>
        </label>
        <div className="sheet-buttons">
          <button className="sheet-btn" onClick={onCancel}>
            Cancel
          </button>
          <button className="sheet-btn primary" autoFocus onClick={() => onExport(c)}>
            Export…
          </button>
        </div>
      </div>
    </div>
  );
}
