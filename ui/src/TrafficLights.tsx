import { useEffect, useState } from "react";
import { getCurrentWindow } from "@tauri-apps/api/window";

// As luzes da janela do macOS 27, pelo "braco imagem" do `MacWindowControlElement`
// (DesignLibrary; a logica e os valores vem de AquaKit/lab/export/traffic-lights,
// README §1-§4, cada um com seu endereco la). Uma janela titulada comum cai
// nele: `controlSize == .regular`, sem vidro.
//
// - Corpo: o bitmap `WindowControlBodies/<Close|Minimize|Expand>` (22 pt com a
//   sombra; o disco e os 14 pt do meio), ou `Inactive` quando a janela nao esta
//   em foco (`isDimmed`). Hover e pressed nao mudam o corpo (§2).
// - Glifo: o PDF `WindowControls/*` como mascara, na cor da tabela §3.3. Aparece
//   so com o grupo em rollover ou um botao pressionado; o hover e do GRUPO, as
//   tres juntas (§3.2). Excecao: o ponto de "nao salvo" (`CloseUnsaved`) fica
//   sempre visivel. Animacao `.spring(response: 0.1, dampingFraction: 1.0)`.
// - Verde: entra/sai de tela cheia (`FullScreenEnter`/`Exit`); com Option (aqui
//   Alt) e o zoom, o "+" (§2.1).
// - Geometria: botao 14 pt, espaco 9 pt (§4).
//
// Os PNG/SVG moram em public/apple/traffic/ (fora do git, como os outros).

type Light = "close" | "minimize" | "zoom";

export function TrafficLights({ dirty = false }: { dirty?: boolean }) {
  const win = getCurrentWindow();
  const [hover, setHover] = useState(false);
  const [pressed, setPressed] = useState<Light | null>(null);
  const [focused, setFocused] = useState(true);
  const [fullscreen, setFullscreen] = useState(false);
  const [alt, setAlt] = useState(false);

  useEffect(() => {
    const un = win.onFocusChanged(({ payload }) => setFocused(payload));
    const unResize = win.onResized(() => win.isFullscreen().then(setFullscreen).catch(() => {}));
    win.isFullscreen().then(setFullscreen).catch(() => {});
    return () => {
      un.then((f) => f());
      unResize.then((f) => f());
    };
  }, []);

  // Option (Alt) so e ouvido com o mouse sobre o grupo
  // (`startMonitoringFlagsChanged`, §3.2).
  useEffect(() => {
    if (!hover) return;
    const on = (e: KeyboardEvent) => setAlt(e.altKey);
    window.addEventListener("keydown", on);
    window.addEventListener("keyup", on);
    return () => {
      window.removeEventListener("keydown", on);
      window.removeEventListener("keyup", on);
    };
  }, [hover]);

  const zoomGlyph = alt && !fullscreen ? "Zoom" : fullscreen ? "FullScreenExit" : "FullScreenEnter";
  const lights: { id: Light; body: string; glyph: string; label: string; act: () => void }[] = [
    { id: "close", body: "Close", glyph: dirty ? "CloseUnsaved" : "Close", label: "Close", act: () => win.close() },
    { id: "minimize", body: "Minimize", glyph: "Minimize", label: "Minimize", act: () => win.minimize() },
    {
      id: "zoom",
      body: "Expand",
      glyph: zoomGlyph,
      label: zoomGlyph === "Zoom" ? "Zoom" : fullscreen ? "Exit Full Screen" : "Enter Full Screen",
      act: () =>
        zoomGlyph === "Zoom"
          ? win.toggleMaximize()
          : win.setFullscreen(!fullscreen).then(() => setFullscreen(!fullscreen)),
    },
  ];

  return (
    <div
      // `isDimmed = disabled || (idle && !windowAppearsActive)` (§3.2): com o
      // mouse no grupo o estado nao e idle, e as cores voltam.
      className={`traffic-lights${!focused && !hover && pressed === null ? " dimmed" : ""}`}
      onMouseEnter={() => setHover(true)}
      onMouseLeave={() => {
        setHover(false);
        setPressed(null);
        setAlt(false);
      }}
    >
      {lights.map((l) => {
        // Glifo visivel: rollover/pressed; o ponto de nao salvo, sempre.
        const shown = hover || pressed !== null || (l.id === "close" && dirty);
        return (
          <button
            key={l.id}
            className={`tl tl-${l.id}`}
            aria-label={l.label}
            title={l.label}
            onPointerDown={() => setPressed(l.id)}
            onPointerUp={() => setPressed(null)}
            onClick={(e) => {
              setAlt(e.altKey);
              l.act();
            }}
          >
            <span className="tl-body" />
            <span
              className="tl-glyph"
              style={{
                WebkitMaskImage: `url("/apple/traffic/${l.glyph}.svg")`,
                maskImage: `url("/apple/traffic/${l.glyph}.svg")`,
                opacity: shown ? 1 : 0,
              }}
            />
          </button>
        );
      })}
    </div>
  );
}
