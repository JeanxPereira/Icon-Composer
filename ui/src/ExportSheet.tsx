import { useState } from "react";
import { invoke } from "@tauri-apps/api/core";
import { open, save } from "@tauri-apps/plugin-dialog";
import { blobToBase64 } from "./core";
import { Platform, Rendition, RENDITIONS } from "./doc";

// A folha "Export Icon as Image" do alvo (inventario §9). `ExportOptions`
// `[BIN]`: `platform` (one / all / preTahoe), `rendition`, `localization` e
// `previewSize` (cada um `AxisChoice {one, all}`), `overrideScale`,
// `mitigateGlassExports` ("Glass Chiclet") e `maskToChiclet`. Com mais de uma
// imagem o alvo cria uma pasta ("Create ... folder with N images", "<nome>
// Exports"). O nome do arquivo `[BIN]` (Kit 0x3B100): nome, plataforma (tabela
// 0x1853F8), rendicao (tabela 0x185438: Default, Dark, TintedLight, TintedDark,
// ClearLight, ClearDark), localizacao (so quando nao e Base) e `%g@%ldx`,
// juntados com "-" -- `AppIcon-iOS-TintedDark-1024@2x.png`. `ExportSize.hero`
// `[BIN]` (Foundation 0x23B80) e 1024 pt no iOS/macOS e 1088 pt no watchOS.
//
// Ficam de fora macOS pre-Tahoe, "Glass Chiclet" excluido e Localization: o
// nucleo nao tem esses renders.

type Axis<T> = T | "all";

export type ExportRender = (platform: Platform, rendition: Rendition, px: number) => Promise<Blob>;

const HERO: Record<Platform, number> = { iOS: 1024, watchOS: 1088 };

const SIZES = [1024, 512, 256, 128, 64, 32, 16];
const SCALES = [1, 2, 3];

const PLATFORM_NAME: Record<Platform, string> = { iOS: "iOS", watchOS: "watchOS" };
const PLATFORM_LABEL: Record<Platform, string> = { iOS: "iOS, macOS", watchOS: "watchOS" };

export function ExportSheet({
  iconName,
  platforms,
  initial,
  render,
  renditionName,
  onDone,
}: {
  iconName: string;
  platforms: Platform[];
  initial: { platform: Platform; rendition: Rendition };
  render: ExportRender;
  // O nome da rendicao no arquivo (o Mono com tint e o TintedDark do alvo).
  renditionName: (r: Rendition) => string;
  onDone: (error?: string) => void;
}) {
  const [platform, setPlatform] = useState<Axis<Platform>>(initial.platform);
  const [rendition, setRendition] = useState<Axis<Rendition>>(initial.rendition);
  const [size, setSize] = useState<Axis<number> | "hero">(1024);
  const [scale, setScale] = useState(1);
  const [progress, setProgress] = useState<string | null>(null);

  const ps = platform === "all" ? platforms : [platform];
  const rs = rendition === "all" ? RENDITIONS.map((r) => r.id) : [rendition];
  const jobs = ps.flatMap((p) =>
    rs.flatMap((r) => (size === "all" ? SIZES : [size === "hero" ? HERO[p] : size]).map((s) => ({ p, r, s }))),
  );
  const fileName = (j: { p: Platform; r: Rendition; s: number }) =>
    `${iconName}-${PLATFORM_NAME[j.p]}-${renditionName(j.r)}-${j.s}@${scale}x.png`;

  const run = async () => {
    try {
      let targets: string[];
      if (jobs.length === 1) {
        const t = await save({
          title: "Export Icon as Image",
          defaultPath: fileName(jobs[0]),
          filters: [{ name: "PNG", extensions: ["png"] }],
        });
        if (!t) return;
        targets = [t];
      } else {
        const where = await open({ directory: true, title: `Create “${iconName} Exports” folder in…` });
        if (typeof where !== "string") return;
        const dir = `${where.replace(/[\\/]$/, "")}\\${iconName} Exports`;
        await invoke("make_dir", { path: dir });
        targets = jobs.map((j) => `${dir}\\${fileName(j)}`);
      }
      for (let i = 0; i < jobs.length; i++) {
        setProgress(`${i + 1} / ${jobs.length}`);
        const j = jobs[i];
        const png = await render(j.p, j.r, j.s * scale);
        await invoke("write_file", { path: targets[i], base64: await blobToBase64(png) });
      }
      onDone();
    } catch (e) {
      onDone(String(e));
    } finally {
      setProgress(null);
    }
  };

  const busy = progress !== null;
  return (
    <div className="sheet-backdrop" onPointerDown={(e) => e.target === e.currentTarget && !busy && onDone()}>
      <div className="sheet" onKeyDown={(e) => e.key === "Escape" && !busy && onDone()}>
        <h2>Export Icon as Image</h2>
        <p className="sheet-note">Export static versions of your icon for use on websites, advertising, or for review.</p>
        <Row label="Platform">
          <select value={platform} onChange={(e) => setPlatform(e.target.value as Axis<Platform>)} disabled={busy}>
            {platforms.map((p) => (
              <option key={p} value={p}>
                {PLATFORM_LABEL[p]}
              </option>
            ))}
            {platforms.length > 1 && <option value="all">All</option>}
          </select>
        </Row>
        <Row label="Appearance">
          <select value={rendition} onChange={(e) => setRendition(e.target.value as Axis<Rendition>)} disabled={busy}>
            {RENDITIONS.map((r) => (
              <option key={r.id} value={r.id}>
                {r.label}
              </option>
            ))}
            <option value="all">All</option>
          </select>
        </Row>
        <Row label="Size">
          <select
            value={String(size)}
            onChange={(e) => {
              const v = e.target.value;
              setSize(v === "all" || v === "hero" ? v : Number(v));
            }}
            disabled={busy}
          >
            <option value="hero">Hero</option>
            {SIZES.map((s) => (
              <option key={s} value={s}>
                {s} pt
              </option>
            ))}
            <option value="all">All</option>
          </select>
          <select value={scale} onChange={(e) => setScale(Number(e.target.value))} disabled={busy} className="narrow">
            {SCALES.map((s) => (
              <option key={s} value={s}>
                @{s}x
              </option>
            ))}
          </select>
        </Row>
        <p className="sheet-note">
          {jobs.length === 1
            ? fileName(jobs[0])
            : `Create “${iconName} Exports” folder with ${jobs.length} images`}
        </p>
        <div className="sheet-buttons">
          {busy && <span className="sheet-progress">Exporting {progress}</span>}
          <button className="sheet-btn" onClick={() => onDone()} disabled={busy}>
            Cancel
          </button>
          <button className="sheet-btn primary" autoFocus onClick={run} disabled={busy}>
            Export…
          </button>
        </div>
      </div>
    </div>
  );
}

function Row({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div className="sheet-row">
      <span>{label}</span>
      <span className="sheet-controls">{children}</span>
    </div>
  );
}
