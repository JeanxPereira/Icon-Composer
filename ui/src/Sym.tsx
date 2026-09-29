// Um SF Symbol (ou asset do .car) como mascara CSS: a forma vem do SVG, a cor
// vem de `currentColor`, entao o mesmo arquivo serve claro, escuro e selecionado.
// Os arquivos moram em public/apple/ (fora do git; ver ui/.gitignore).

type Props = { name: string; size?: number; custom?: boolean; image?: boolean; className?: string };

// `image`: para os dois assets que vieram de PDF (Opacity, Blendmode). Eles
// dependem de degrade e de branco DENTRO da forma, e uma mascara so enxerga a
// transparencia -- como mascara viravam um disco chapado.
export function Sym({ name, size = 15, custom = false, image = false, className = "" }: Props) {
  const url = `/apple/${custom ? "custom" : "symbols"}/${name}.svg`;
  if (image) {
    return <img aria-hidden className={`sym-img ${className}`} src={url} style={{ width: size, height: size }} alt="" />;
  }
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
