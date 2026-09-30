// `BlendFormula.cpp`, transcrito para o caminho residente: os dez modos que
// `blendIsTranscribed` aceita, com as opcoes no padrao (`extendedColor` e
// `clampPlusLighter` desligados -- e o que `IconRenderer.cpp` passa). Os numeros
// dos modos sao os do `BlendMode` do host.
//
// A CPU faz esta conta em double sobre entradas float e arredonda no fim; aqui e
// float do comeco ao fim. E a diferenca que o teto de fidelidade do plano mede.
#ifndef RB_BLEND_FORMULA_GLSL
#define RB_BLEND_FORMULA_GLSL

const float kSoftLightAlphaFloor = 0.005;

vec4 rbBlend(uint mode, vec4 s, vec4 d) {
    float as = s.a, ab = d.a;
    if (mode == 0u) return s + d * (1.0 - as);                 // normal
    if (mode == 6u) return s + d * (vec4(1.0) - s);            // screen, os quatro canais
    if (mode == 8u || mode == 4u) {                            // plus-lighter / plus-darker
        float sum = as + ab;
        float a = clamp(sum, 0.0, 1.0);
        float slack = (mode == 4u) ? (a - sum) : 0.0;
        return vec4(s.rgb + d.rgb + vec3(slack), a);
    }
    vec3 b;
    for (int k = 0; k < 3; ++k) {
        float sk = s[k], dk = d[k];
        if (mode == 2u) {                                      // multiply
            b[k] = sk * dk;
        } else if (mode == 1u) {                               // darken
            b[k] = min(as * dk, ab * sk);
        } else if (mode == 5u) {                               // lighten
            b[k] = max(as * dk, ab * sk);
        } else if (mode == 9u) {                               // overlay
            b[k] = (dk > 0.5 * ab) ? (2.0 * (sk * ab + dk * (as - sk)) - as * ab)
                                   : (2.0 * sk * dk);
        } else if (mode == 11u) {                              // hard-light
            b[k] = (sk > 0.5 * as) ? (as * ab - 2.0 * (ab - dk) * (as - sk))
                                   : (2.0 * sk * dk);
        } else if (mode == 10u) {                              // soft-light
            float floorA = max(ab, kSoftLightAlphaFloor);
            b[k] = 2.0 * dk * sk - (2.0 * sk - as) * dk * dk / floorA;
        } else {
            return s + d * (1.0 - as);
        }
    }
    vec4 o;
    o.a = as + ab - as * ab;
    o.rgb = s.rgb * (1.0 - ab) + d.rgb * (1.0 - as) + b;
    return o;
}

#endif
