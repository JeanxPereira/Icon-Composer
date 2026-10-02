#version 450
// O quadro do palco: um retangulo em pixels do framebuffer, sem buffer de
// vertices -- os quatro cantos saem do indice.

layout(push_constant) uniform Push {
    vec4 quad;      // x0, y0, x1, y1, em pixels do framebuffer
    vec4 quadUV;    // o que o retangulo cobre do canvas (0..1)
    vec4 square;    // x, y, lado do quadrado do icone; lado da grade da lente
    vec4 stage;     // x, y, largura, altura do palco
    vec4 cover;     // u0, v0, u1, v1 da imagem de fundo (`cover`)
    vec4 solid;     // a cor chapada; a > 0.5: o fundo e a imagem
    vec4 lens;      // forca (px da grade), sigma (px da tela), texels do fundo por px, modo
    vec2 fb;        // o tamanho do framebuffer
    vec2 pad;
} pc;

void main() {
    vec2 corner = vec2(float(gl_VertexIndex & 1), float(gl_VertexIndex >> 1));
    vec2 pos = mix(pc.quad.xy, pc.quad.zw, corner);
    gl_Position = vec4(pos / pc.fb * 2.0 - 1.0, 0.0, 1.0);
}
