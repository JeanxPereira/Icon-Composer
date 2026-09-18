#version 450
// The exterior fragment, as a real fragment shader. The arithmetic is the same
// file the coverage probe runs -- PathFragment.glsl -- so what draws and what is
// gated cannot drift apart.
#include "PathFragment.glsl"

// O MESMO bloco do estagio de vertice, palavra por palavra: um push constant
// e um so, e uma declaracao divergente e invalida.
layout(push_constant) uniform Globals {
    vec2 m0;
    vec2 m1;
    vec2 m2;
    vec2 twoOverSize;
    vec2 origin;
    float depth;
    float urx;
    float arg;
    int gridOriginX;
    int gridOriginY;
} g;

layout(location = 0) in vec2 vPathY;
layout(location = 1) in float vSlope;
layout(location = 2) in float vIntercept;
layout(location = 3) in float vValue;

// `[BIN]` The target writes `coverage`, a half2 at location 1, and leaves .x at
// zero. Here it is location 0 because it is the only attachment; the CHANNELS
// are the target's.
layout(location = 0) out vec2 coverage;

void main() {
    // A COORDENADA VOLTA A SER A DO CANVAS, e este e o ponto inteiro do
    // desenho de viewport (spec 2026-09-16, "O invariante que governa o
    // desenho").
    //
    // O estagio de vertice projeta no CANVAS e o deslocamento entra como um
    // inteiro no viewport do Vulkan, de modo que `gl_FragCoord` vale
    // `mundo - origem`. As varyings da aresta (`vPathY`, `vIntercept`) seguem
    // em coordenada de canvas, entao o que o fragmento compara tem que estar
    // na mesma grade -- e somar um INTEIRO a um `x.5` e exato em float, ao
    // contrario de transladar a matriz, que arredonda noutro expoente.
    vec4 p = gl_FragCoord;
    p.xy += vec2(float(g.gridOriginX), float(g.gridOriginY));
    coverage = vec2(0.0, rbExteriorShape(p, vPathY, vSlope, vIntercept, vValue));
}
