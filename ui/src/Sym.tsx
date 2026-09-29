// Um SF Symbol (ou asset do .car) como mascara CSS: a forma vem do SVG, a cor
// vem de `currentColor`, entao o mesmo arquivo serve claro, escuro e selecionado.
// Os arquivos moram em public/apple/ (fora do git; ver ui/.gitignore).

type Props = { name: string; size?: number; custom?: boolean; className?: string };

export function Sym({ name, size = 15, custom = false, className = "" }: Props) {
  const url = `/apple/${custom ? "custom" : "symbols"}/${name}.svg`;
  return (
    <span
      aria-hidden
      className={`sym ${className}`}
      style={{
        width: size,
        height: size,
        WebkitMaskImage: `url("${url}")`,
        maskImage: `url("${url}")`,
      }}
    />
  );
}
