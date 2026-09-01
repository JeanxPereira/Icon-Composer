// The fill rule: accumulated coverage becomes alpha.
//
// `[BIN]` `RB::Shader::accumulator_shape`, module 4 of `shader_accumulator.metal`.
// It is the piece that was missing between "the coverage target holds a signed
// winding-weighted area" and "a pixel has an opacity".
//
// AND IT NAMES FOUR FIELDS OF THE STATE WORD that doc 03 section 11 had to leave
// as layout without meaning:
//
//     bits 6-7   the shape mode                  (state >> 6) & 3
//     bits 8-9   a sub-mode of mode 2            (state >> 8) & 3
//     bit  10    EVEN-ODD when set, NON-ZERO when clear
//     bit  11    enables the quadratic curve carried in `shape`
//
// The fill rule being one bit is the finding: everything else about the two
// rules -- the absolute value, the fract, the parity test -- is shared, and the
// bit only chooses which of two endings runs.

const float kShapeEpsilon = 0.0010004043579101562;  // 0xH1419

float rbAccumulatorShape(uint state, float coverage, vec4 shape) {
    uint mode = (state >> 6) & 3u;
    bool evenOdd = (state & 1024u) != 0u;
    float result = coverage;

    if (mode == 1u) {
        float a = abs(coverage);
        if (evenOdd) {
            float f = fract(a);
            float whole = a - f;
            bool even = (uint(whole) & 1u) == 0u;
            result = even ? f : (1.0 - f);
        } else {
            result = clamp(a, 0.0, 1.0);
        }
    } else if (mode == 2u) {
        float whole = floor(coverage);
        float f = coverage - whole;
        // `[BIN]` The two rules differ ONLY here: parity of the winding number
        // against "is it nonzero at all".
        float inside = evenOdd ? float(uint(abs(whole)) & 1u) : float(whole != 0.0);
        uint sub = (state >> 8) & 3u;
        if (sub == 0u)      result = (inside != 0.0) ? (1.0 - f) : 0.0;
        else if (sub == 1u) result = (inside != 0.0) ? 1.0 : f;
        else if (sub == 2u) result = (inside != 0.0) ? (1.0 - f * 0.5) : (f * 0.5);
        else                result = inside;
    }

    // `[BIN]` The curve, and the threshold that skips it. `shape.x` is both the
    // upper bound past which nothing happens AND the quadratic's leading
    // coefficient -- one value doing two jobs, which is the target's, not a
    // simplification of mine.
    if ((state & 2048u) != 0u && result >= kShapeEpsilon && result <= shape.x) {
        // `precise`, like every other comparison in this tower: without it the
        // driver may fuse the multiply and the add, and the host cannot. Measured
        // as one float ULP -- 0.192968756 against 0.192968741 -- which is a
        // difference with no defect behind it.
        precise float inner = shape.x * result + shape.y;
        precise float outer = inner * result + shape.z;
        result = clamp(outer, 0.0, 1.0);
    }
    return result;
}
