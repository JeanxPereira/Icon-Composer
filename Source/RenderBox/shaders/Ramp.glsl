// As duas rampas de `GradientOracle.cpp` -- `rampAtPositions` e
// `rampSmoothAtPositions` --, transcritas para o caminho residente.
//
// Quem inclui declara antes um buffer `rampStops` de `RampStop`. As paradas
// chegam como o `RampPoint` do host: localizacao e quatro componentes.
#ifndef RB_RAMP_GLSL
#define RB_RAMP_GLSL

vec4 rbRampAtPositions(float t, uint n) {
    if (n == 0u) return vec4(0.0);
    if (n == 1u) return rampStops[0].rgba;
    uint i = 0u;
    while (i + 2u < n && rampStops[i + 1u].location.x <= t) ++i;
    vec4 lo = rampStops[i].rgba;
    vec4 hi = rampStops[i + 1u].rgba;
    float span = rampStops[i + 1u].location.x - rampStops[i].location.x;
    float f = span > 0.0 ? (t - rampStops[i].location.x) / span : 1.0;
    f = clamp(f, 0.0, 1.0);
    return lo + (hi - lo) * f;
}

// `[BIN]` A cubica monotona (codigo de interpolacao 4) -- `smoothColorCoefficients`
// componente a componente, com o mesmo recuo para a rampa linear quando as
// paradas nao sao uniformes.
vec4 rbRampSmoothAtPositions(float t, uint n) {
    if (n == 0u) return vec4(0.0);
    if (n == 1u) return rampStops[0].rgba;
    float stepSize = 1.0 / float(n - 1u);
    for (uint i = 0u; i < n; ++i) {
        float want = float(i) * stepSize;
        if (abs(rampStops[i].location.x - want) > 1.0e-4) return rbRampAtPositions(t, n);
    }
    float scaled = clamp(t, 0.0, 1.0) * float(n - 1u);
    uint seg = uint(scaled);
    if (seg + 1u >= n) seg = n - 2u;
    float f = scaled - float(seg);

    vec4 p1 = rampStops[seg].rgba;
    vec4 p2 = rampStops[seg + 1u].rgba;
    vec4 p0 = seg == 0u ? p1 : rampStops[seg - 1u].rgba;
    vec4 p3 = seg + 2u < n ? rampStops[seg + 2u].rgba : p2;

    vec4 outc;
    for (int k = 0; k < 4; ++k) {
        float d0 = p1[k] - p0[k];
        float d1 = p2[k] - p1[k];
        float d2 = p3[k] - p2[k];

        float m1 = (d0 + d1) * 0.5;
        if ((d0 < 0.0) != (d1 < 0.0)) m1 = 0.0;
        if (abs(m1) > abs(3.0 * d0)) m1 = 3.0 * d0;
        if (abs(m1) > abs(3.0 * d1)) m1 = 3.0 * d1;

        float m2 = (d1 + d2) * 0.5;
        if ((d1 < 0.0) != (d2 < 0.0)) m2 = 0.0;
        if (abs(m2) > abs(3.0 * d1)) m2 = 3.0 * d1;
        if (abs(m2) > abs(3.0 * d2)) m2 = 3.0 * d2;

        float b0 = p1[k] + m1 * (1.0 / 3.0);
        float b1 = p2[k] - m2 * (1.0 / 3.0);
        float c0 = p1[k];
        float c1 = m1;
        float c2 = 3.0 * (p1[k] - 2.0 * b0 + b1);
        float c3 = d1 + 3.0 * (b0 - b1);
        outc[k] = c0 + f * (c1 + f * (c2 + f * c3));
    }
    return outc;
}

#endif
