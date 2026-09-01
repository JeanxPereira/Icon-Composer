// The target's path fragment stages, transcribed from the IR.
//
// `[BIN]` `shader_path.metal` modules 62, 63 and 64. Doc 03 §12 carries the
// measurement. All three write to ONE render target -- `coverage`, a `half2` at
// location 1 -- and all three leave `.x` at zero and put their answer in `.y`.
//
// They are small: forty-three, one hundred and fifty-nine, and seventy-five
// lines of IR. Together they are the whole coverage rule.

// `[BIN]` mod62, and it is four instructions. The winding number, accumulated by
// the fan the interior pass draws from `origin`: a front-facing triangle adds
// one, a back-facing one subtracts it.
vec2 rbInteriorFragment(bool frontFacing) {
    return vec2(0.0, frontFacing ? 1.0 : -1.0);
}

// `[BIN]` mod63, `exterior_shape`. The EXACT area of the pixel lying to the
// right of the edge, times the winding sign.
//
// The pixel is found by TRUNCATING the fragment's position to an integer -- the
// pixel's lower-left corner -- and everything after that is in pixel-local
// coordinates. Nothing here is sampled or approximated: the trapezoid the edge
// cuts out of the pixel is integrated in closed form.
float rbExteriorShape(vec4 position, vec2 pathY, float slope, float intercept, float value) {
    // `[BIN]` int16 truncation, not floor: the target converts through `short`.
    // For a fragment position (always positive, always x.5) the two agree, and
    // the conversion is what the target does.
    vec2 pixel = vec2(ivec2(position.xy));

    // The pixel's y span, clipped to the edge's own.
    precise float y0 = max(pixel.y, pathY.x);
    precise float y1 = min(pixel.y + 1.0, pathY.y);

    // Where the edge sits at those two heights, relative to the pixel's left.
    precise vec2 xs = vec2(y0, y1) * slope + vec2(intercept - pixel.x);
    // Ordered low-to-high: a negative slope reverses which end is which.
    precise vec2 ordered = (slope < 0.0) ? xs.yx : xs;
    precise vec2 held = clamp(ordered, 0.0, 1.0);

    precise float dx = ordered.y - ordered.x;
    precise float cover = clamp(y1 - y0, 0.0, 1.0);

    // The rectangle to the right of the edge's far end.
    precise float area = cover * (1.0 - held.y);

    // And the triangle the slope cuts off it, when the edge crosses the pixel
    // rather than passing beside it. `dx == 0` is a vertical edge, where the
    // rectangle IS the answer -- and dividing by it would not be.
    if (dx != 0.0) {
        precise float mid = held.x * 0.5 + held.y * 0.5 - ordered.x;
        precise float k = (cover * mid) / dx;
        area = area - held.x * k + held.y * k;
    }
    return value * area;
}

// `[BIN]` mod64. The distance variant: the perpendicular from the point to the
// segment, saturated, floored at a half constant, and inverted.
//
// The floor is `0xH1626` = 0.0015010833740234375 -- exactly one half's worth
// above zero at that magnitude, so a point ON the segment does not come back as
// full coverage.
float rbDistanceShape(vec2 p, vec2 d, float scale) {
    precise float t = clamp(dot(p, d) * scale, 0.0, 1.0);
    precise vec2 v = d * (-t) + p;
    precise float dist = clamp(sqrt(dot(v, v)), 0.0, 1.0);
    // `[BIN]` The compare happens in HALF, not float: the target narrows before
    // the max. Round-tripping through packHalf2x16 reproduces that narrowing.
    float narrowed = unpackHalf2x16(packHalf2x16(vec2(dist, 0.0))).x;
    // `[BIN]` And the subtraction is `fsub half` too -- the target has narrowed
    // by this point and stays narrow. Leaving it in float puts the two sides on
    // different grids for a reason that is not a defect.
    float r = 1.0 - max(narrowed, 0.0015010833740234375);
    return unpackHalf2x16(packHalf2x16(vec2(r, 0.0))).x;
}
