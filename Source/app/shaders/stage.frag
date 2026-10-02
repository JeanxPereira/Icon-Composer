#version 450
// O MONO, COMPOSTO NA GPU DA TELA, POR QUADRO (02/10).
//
// O que `rb::finishMono` faz na CPU -- `simulatedGlass` (SimulatedGlass.cpp) e
// depois `applyClear` ou `applyOverGlass` (IconRenderer.cpp) --, por pixel e na
// posicao em que o icone esta NESTE quadro. As entradas que nao mudam com a
// posicao chegam como textura: o icone de antes do vidro, o fundo do palco e a
// lente da pastilha (`rb::simulatedGlassLens`). As constantes vem de
// `rb::monoColourMatrices`, as mesmas que a CPU le.
//
// A refracao e a transcricao `Displacement.glsl` da RenderBox, a mesma do
// `icon_refract.comp`. `[INF]` O desfoque do fundo e a unica coisa que nao e a
// conta da CPU: la e uma gaussiana separavel sobre a grade de 512; aqui sao 25
// amostras a um sigma de passo, cada uma lendo o nivel de mip cuja pegada e o
// passo. Sobre uma cor chapada as duas dao a propria cor.
#extension GL_GOOGLE_include_directive : require

#include "Displacement.glsl"

layout(set = 0, binding = 0) uniform sampler2D uIcon;
layout(set = 0, binding = 1) uniform sampler2D uBackdrop;
layout(set = 0, binding = 2) uniform sampler2D uLens;   // rg: disp; b: cobertura; a: mascara

layout(push_constant) uniform Push {
    vec4 quad;
    vec4 quadUV;
    vec4 square;
    vec4 stage;
    vec4 cover;
    vec4 solid;
    vec4 lens;
    vec2 fb;
    vec2 pad;
} pc;

// As matrizes (`rb::monoColourMatrices`), como constantes de especializacao
// nao: sao poucas e fixas, e o app confere na partida que batem com a CPU
// (StageCompositor.cpp, `kShaderMatrices`).
const vec3 kGlassLight = vec3(0.12, 1.0, 1.2);
const vec3 kGlassDark = vec3(0.05, 0.3, 0.8);
const vec3 kClearLighten = vec3(0.9, 2.5, 2.0);
const vec3 kClearHighlight = vec3(0.2, 1.35, 1.4);
const float kClearDarkening = 0.3;
const float kVcmMin = -0.75;
const float kVcmMax = 1.0864043;   // 1.2^(1/2.2)

layout(location = 0) out vec4 outColour;

// `applyGlyphVCM`: BT.709 RGB->YCbCr, niveis no Y, ganho de croma em torno de
// 0.5, e de volta. `m` e [piso, teto, saturacao].
vec3 vcm(vec3 rgb, vec3 m) {
    float y = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    float cb = dot(rgb, vec3(-0.1146, -0.3854, 0.5)) + 0.5;
    float cr = dot(rgb, vec3(0.5, -0.4542, -0.0458)) + 0.5;
    y = (m.y - m.x) * y + m.x;
    float k = 0.5 - 0.5 * m.z;
    cb = m.z * cb + k;
    cr = m.z * cr + k;
    return vec3(y + 1.5748 * cr - 0.7874,
                y - 0.1873 * cb - 0.4681 * cr + 0.3277,
                y + 1.8556 * cb - 0.9278);
}

// O fundo do palco num ponto da tela: a cor, ou a imagem em `cover`, presa na
// borda do palco como `sampleBackdrop` prende.
vec3 backdropAt(vec2 pos, float lod) {
    if (pc.solid.a < 0.5) return pc.solid.rgb;
    vec2 t = clamp((pos - pc.stage.xy) / pc.stage.zw, vec2(0.0), vec2(1.0));
    return textureLod(uBackdrop, mix(pc.cover.xy, pc.cover.zw, t), lod).rgb;
}

vec3 backdropBlurred(vec2 pos) {
    if (pc.solid.a < 0.5) return pc.solid.rgb;
    float sigma = pc.lens.y;
    if (sigma < 0.3) return backdropAt(pos, 0.0);
    float lod = max(0.0, log2(sigma * pc.lens.z));
    vec3 acc = vec3(0.0);
    float total = 0.0;
    for (int j = -2; j <= 2; ++j) {
        for (int i = -2; i <= 2; ++i) {
            float w = exp(-0.5 * float(i * i + j * j));
            acc += w * backdropAt(pos + vec2(float(i), float(j)) * sigma, lod);
            total += w;
        }
    }
    return acc / total;
}

// As duas camadas do `displacementMap_v1`. A fonte e o fundo desfocado, lido
// pela UV do CANVAS (a lente prende a UV em 0..1 antes de amostrar, entao a
// refracao nunca le fora do quadrado -- como na grade da CPU).
vec4 rbDispSampleSource(vec2 uv) {
    return vec4(backdropBlurred(pc.square.xy + uv * pc.square.z), 1.0);
}
vec4 rbDispSampleMap(vec2 uv) {
    vec4 l = texture(uLens, uv);
    // `.z` e o peso por amostra, 1 em todo o mapa (GlassLayer.cpp).
    return vec4(l.rg, 1.0, l.a);
}

void main() {
    vec2 uv = (gl_FragCoord.xy - pc.square.xy) / pc.square.z;
    int mode = int(pc.lens.w + 0.5);
    bool clearMask = (mode & 1) != 0;
    bool dark = (mode & 2) != 0;

    vec3 raw = backdropAt(gl_FragCoord.xy, 0.0);

    // ---- o vidro simulado ---------------------------------------------------
    vec4 lensHere = texture(uLens, uv);
    float inside = (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0) ? 1.0 : 0.0;
    float coverage = clamp(lensHere.b, 0.0, 1.0) * inside;
    vec3 glass = backdropBlurred(gl_FragCoord.xy);
    if (lensHere.a > 0.0 && inside > 0.0) {
        float n = pc.square.w;
        RBDispLayer L;
        L.m0 = vec2(1.0 / n, 0.0);
        L.m1 = vec2(0.0, 1.0 / n);
        L.m2 = vec2(0.0);
        L.m3 = vec2(0.0);
        L.m4 = vec2(1.0);
        vec4 refracted = rbDisplacementMap(pc.lens.x, L, L, 2u, uv * n, vec2(1.0, 0.0), vec2(0.0, 1.0));
        glass += (refracted.rgb - glass) * lensHere.a;
    }
    glass = clamp(vcm(glass, dark ? kGlassDark : kGlassLight), 0.0, 1.0);
    // `backdropWithGlass`: o fundo com o vidro por cima, pela cobertura.
    vec3 o = raw + (glass - raw) * coverage;

    // ---- o icone ------------------------------------------------------------
    vec2 t = (uv - pc.quadUV.xy) / (pc.quadUV.zw - pc.quadUV.xy);
    vec4 icon = texture(uIcon, t);
    float A = clamp(icon.a, 0.0, 1.0);
    if (clearMask) {
        // `applyClear`: a matriz total na cor reta, e as tres passadas.
        if (A > 0.0) {
            float mr = clamp(icon.r, 0.0, 1.0);
            float mg = clamp(kClearDarkening * (1.0 - icon.g), 0.0, 1.0);
            float mb = clamp(icon.b, 0.0, 1.0);
            if (mr * A > 0.0) o += (clamp(vcm(o, kClearLighten), kVcmMin, kVcmMax) - o) * (mr * A);     // L
            o = max(vec3(0.0), o - mg * A);                                                               // D
            if (mb * A > 0.0) o += (clamp(vcm(o, kClearHighlight), kVcmMin, kVcmMax) - o) * (mb * A);   // H
        }
    } else {
        // `applyOverGlass`: o icone sobre o vidro.
        o = icon.rgb * A + o * (1.0 - A);
    }

    // Cor reta sobre o fundo CRU, como a CPU devolve: compor da `o`.
    float alpha = max(A, coverage);
    if (alpha <= 0.0) discard;
    vec3 straight = clamp((clamp(o, 0.0, 1.0) - raw * (1.0 - alpha)) / alpha, 0.0, 1.0);
    outColour = vec4(straight, alpha);
}
