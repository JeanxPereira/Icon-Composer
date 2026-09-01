# IconRendering default ramps — recovered constants

Static reverse-engineering of Apple's `IconRendering` framework (macOS 27 Icon
Composer, Swift x86_64 Mach-O) via Ghidra. Every value below cites the address
it was read from. Doubles are IEEE-754 little-endian unless noted.

The authoritative source of most defaults is the static getter
`ICRRenderingParameters.default` (`…ICRRenderingParametersV7defaultACvgZ` @
`0x6c160`). It runs a one-time builder `FUN_0006bbc0` (@ `0x6bbc0`) that writes
all literals into the singleton blob `DAT_000c61c0`, which is then `memcpy`'d
(0x2eb bytes) into every returned struct. Property getters just read fixed
offsets into that struct, so a getter's offset + the builder's store at that
offset gives the default.

---

## ICRRenderingParameters — Refraction ramp

Builder stores six consecutive doubles from `DAT_000a1e10`+ into struct offsets
`0x1a0`–`0x1c8` (decompiler: `puVar1[0x34..0x39]`; getters confirm the offsets).

| property | default | source |
|---|---|---|
| blurStrengthMax | **64.0** | getter `…V15blurStrengthMaxSdvg` @ `0x6ef00` reads off `0x1a0`; `DAT_000a1e10` = `00 00 00 00 00 00 50 40` (`0x4050000000000000`) |
| refractionHeightMin | **12.8** | getter @ `0x6ef40` reads `0x1a8`; `DAT_000a1e18` = `9a 99 99 99 99 99 29 40` (`0x402999999999999a`) |
| refractionHeightMax | **256.0** | getter @ `0x6ef80` reads `0x1b0`; `DAT_000a1e20` = `00 00 00 00 00 00 70 40` (`0x4070000000000000`) |
| refractionHeightPower | **1.0** | getter @ `0x6efc0` reads `0x1b8`; `DAT_000a1e28` = `00 00 00 00 00 00 f0 3f` (`0x3ff0000000000000`) |
| refractionStrengthMax | **640.0** | getter @ `0x6f000` reads `0x1c0`; `DAT_000a1e30` = `00 00 00 00 00 00 84 40` (`0x4084000000000000`) |
| refractionStrengthPower | **1.0** | getter @ `0x6f040` reads `0x1c8`; `DAT_000a1e38` = `00 00 00 00 00 00 f0 3f` (`0x3ff0000000000000`) |

---

## ICRRenderingParameters.ContourGradients — tint / clear / fakeGlass ramps

10-double struct (CodingKeys 0–9, all `decode` — no in-decoder defaults; decoder
@ `0x77b10`). Defaults come from the builder, which occupies parent struct
offsets `0x290`–`0x2d8`. The builder uses the `XORPS` + `MOVHPS` +
`MOVUPS` idiom (verified by disassembly @ `0x6c05c`–`0x6c0e8` and `0x6bbf0`):
each 16-byte store writes a zero low half and a constant high half, so every
"End" field defaults to 0.0 except fakeGlassEnd.

Field order (matches the property list exactly; keys 6–9 confirmed by named
getters `…ContourGradientsV24darkClearStartBrightness` @ `0x77690`,
`darkClearEnd` @ `0x776b0`, `fakeGlassStart` @ `0x776d0`, `fakeGlassEnd` @
`0x776f0`):

| property | default | source |
|---|---|---|
| lightTintStartBrightness | **0.15** | store `[RBX+0x290]`; `DAT_0009c5d0` = `33 33 33 33 33 33 c3 3f` (`0x3fc3333333333333`) |
| lightTintEndBrightness | **0.0** | store `[RBX+0x298]` low half = 0 |
| darkTintStartBrightness | **0.15** | store `[RBX+0x2a0]`; `DAT_0009c5d0` (same as above) |
| darkTintEndBrightness | **0.0** | store `[RBX+0x2a8]` low half = 0 |
| lightClearStartBrightness | **0.1** | store `[RBX+0x2b0]`; `DAT_000a0fb8` = `9a 99 99 99 99 99 b9 3f` (`0x3fb9999999999999`) |
| lightClearEndBrightness | **0.0** | store `[RBX+0x2b8]` low half = 0 |
| darkClearStartBrightness | **0.12** | store `[RBX+0x2c0]`; `DAT_000a1cf0` = `b8 1e 85 eb 51 b8 be 3f` (`0x3fbeb851eb851eb8`) |
| darkClearEndBrightness | **0.0** | store `[RBX+0x2c8]` low half = 0 |
| fakeGlassStartBrightness | **0.88** | store `[RBX+0x2d0]`; `DAT_000a1cf8` = `29 5c 8f c2 f5 28 ec 3f` (`0x3fec28f5c28f5c29`) |
| fakeGlassEndBrightness | **0.75** | immediate `MOV [RBX+0x2d8], 0x3fe8000000000000` @ `0x6c0c5` |

---

## ICRRenderingParameters — top-level tint / mitigation scalars

| property | default | source |
|---|---|---|
| darkTintDuotoneShadowBlendFactor | **0.0** | getter `…V32darkTintDuotoneShadowBlendFactor` @ `0x34f60` reads struct offset `0x0`; builder `MOV qword ptr [RAX], 0x0` @ `0x6bbe2` |
| mitigatedChicletLightTintFactor | **0.9** | getter @ `0x6cb50` reads `0x70`; builder `MOVAPS [0xa1d30]`→`[RBX+0x70]` @ `0x6bc5d`; `DAT_000a1d30` = `cd cc cc cc cc cc ec 3f` (`0x3feccccccccccccd`) |
| mitigatedChicletDarkTintFactor | **0.0** | getter @ `0x6cbb0` reads `0x98`; builder `MOVSD [0x9c5c8]`→`MOVUPS [RBX+0x90]` @ `0x6bc76` — high half (offset `0x98`) is the zeroed upper of the MOVSD load |

Note: `mitigatedChicletLightTint`, `…DarkTint`, `…LightClear`, `…DarkClear` are
`ICRColor` values (not covered here — non-scalar).

---

## GlobalConfiguration — light

Three initializers examined: `lightAngle:drawMitigatedVersion:` @ `0x35df0`,
`lightIntensity:lightDirection:` @ `0x35cb0`, and the 4-arg variant @ `0x35f30`.

| property | default | source |
|---|---|---|
| lightIntensity | **1.0** | field at struct offset `0x0`. In `lightIntensity:` init it is set from the arg; in the `lightAngle:` init (@ `0x35df0`) it is baked as `*param_1 = 0x3ff0000000000000` (1.0) since no intensity is supplied |
| lightAngle | *(no baked literal)* | `lightAngle:` is a required parameter; the init computes `sincos(lightAngle)` and stores the resulting direction. No default angle constant is present in these initializers |
| lightDirection (cartesian) | **(0.0, 1.0, 0.0)** | The init compares the derived direction against the once-initialized default `DAT_000c6128/0x6130/0x6138` (populated by `FUN_0008b5e0`, the confirmed `ICRUnitCartesianCoordinates.defaultLightDirection`). Confirmed elsewhere: constant 1.0 @ `0x9c5c8`, initializer @ `0x8b5e0` |

**ICRUnitSphericalCoordinates.defaultLightDirection** (getter
`…defaultLightDirectionABvgZ` @ `0x8b810`) — body is `return 0;`. It does **not**
forward to the cartesian default; it returns a zero/empty spherical value. The
real default light direction is defined only in cartesian form `(0,1,0)`.

---

## Round 2 (2026-07-07) — factory light angle RESOLVED + light-dir reconciled

Traced the actual no-arg `ICRGlobalConfiguration()` used by the UI to source the
factory light angle (this is the `sub_123134` the rig saw in IconComposerKit).

**`ICRGlobalConfiguration()` init** (`…ICRGlobalConfigurationC…ABycfc` @ `0x601e0`):
- struct off `0x0` ← `0x3ff0000000000000` = **lightIntensity 1.0**
- struct off `0x8/0x10/0x18` ← 0,0,0 (the stored direction, unused — see flag)
- struct off `0x20` (byte) ← **1** = "use-default-direction" flag = true
- off `0x28` ← `DAT_000c5e28` (once-init `FUN_00036480`), off `0x38` ← `0x201`, etc.

**`GlobalConfiguration.lightDirection` getter** (`…14lightDirection…avg` @ `0x34f80`):
`if flag(off 0x20)==1 → return the global default (0,1,0)` (via `FUN_0008b5e0`,
`DAT_000c6128..6138`); `else → return stored dir at off 0x8`. Since the init sets
flag=1, **the factory light direction really is (0,1,0)** — it is the *active*
default, not merely a type-level constant.

**`GlobalConfiguration.lightAngles` getter** (`…11lightAngles…avg` @ `0x35780`):
converts direction→spherical as `(asin(z), atan2(x, y))`. With the default
`(x,y,z)=(0,1,0)`: `asin(0)=0`, `atan2(0,1)=0` ⇒ **default lightAngles = (0.0, 0.0) rad**.

**⇒ factory `defaultLightAngle` = φ × 180/π = 0 × 57.29578 = `0.0°`** (light from
straight above). This closes Q2. The `sincos` the rig flagged is the *inverse*
path (angle→direction, in `GlobalConfiguration.init(lightAngle:)` @ `0x35df0`),
used only when a custom angle is set (which also flips flag `0x20`→0). So the two
findings are consistent: default = (0,1,0)/0°; custom angles go through sincos.

### Correction to the earlier "lightAngle: no baked literal" row
That row stands for the `init(lightAngle:)` *initializer* (angle is its argument).
But the **factory default** angle IS determinate = **0°**, sourced from
`ICRGlobalConfiguration()` above, not from that initializer.

---

## Q1 — ClearMode / totalHighlightsStrength: NOT a code literal here

`ICRRenderingParameters.ClearMode` is only ever **decoded** — the sole constructor
reached is `ClearMode.init(from:Decoder)` (`@ 0x6d9f0`), and its only non-external
caller `FUN_0006df20` is a bare thunk to that decoder (a protocol witness). There is
**no memberwise/default init with baked literals**, and `clearMode` is `Optional`
on `ICRRenderingParameters` (default `nil`). A full-app grep for
`totalHighlightsStrength|clearMode|renderingParameters` in any bundled `.json`/
`.plist` returns **nothing** — the keys exist only as Codable key-strings inside the
`IconRendering` and **`IconComposerKit`** Mach-Os.

Also note the real `ClearMode` field roster (from its getters) is **VCM-based**, not
the five "…Strength" doubles assumed earlier: `highlightsVCM`, `lighteningVCM`,
`darkeningVCM` (each a `HighlightVCM` struct), `contentDarkeningStrength` (Double
@ `0x3f240`), plus bools (`darkeningUsesVCM`, `applyToLightTintToo`,
`drawByReference`, `leaveChicletDarklightsToSystem`) and enums (`passOrder`,
`debugFill`). Any "totalHighlightsStrength" is a component of a `HighlightVCM`
(cf. the recovered `glyphHighlightVCM=(0.75,1.3,1.5,0.0,true)` tuple).

**Conclusion:** the default ClearMode (used by the clear / clear-dark appearances)
is authored in the **UI layer (IconComposerKit)** or supplied at runtime — resolve
it either by RE'ing `iconcomposer-rig-haul/metal/bin/IconComposerKit.macho`, or from
an H5 GPU capture. It is genuinely not present as a constant in `IconRendering`.

---

## IconRendering.GlassMaterial — per-layer init defaults

Init `…GlassMaterialV11hasSpecular11shadowStyle…specularPlacement…tcfC` @
`0x3bbb0`. The optional (`…Sg`) numeric parameters fall back to constants via
`BLENDVPD`; disassembly @ `0x3bbb0`–`0x3bc74` resolves them:

| parameter | default when omitted | source |
|---|---|---|
| shadowOpacity | **1.0** | `BLENDVPD XMM3, [0x9ef50]` @ `0x3bc06`; low double of `DAT_0009ef50` = `00 00 00 00 00 00 f0 3f` (1.0) |
| translucency | **0.5** | high double of `_DAT_0009ef50` = `00 00 00 00 00 00 e0 3f` (0.5) |
| blurStrength | **0.0** | `BLENDVPD XMM2, XMM1` @ `0x3bc44`; XMM1 low half zeroed (`XORPD` @ `0x3bc33`) |
| refractionHeight | **0.5** | XMM1 high half `MOVHPD [0x9c5b0]` @ `0x3bc37`; `DAT_0009c5b0` = `00 00 00 00 00 00 e0 3f` (0.5) |
| refractionStrength | **0.0** | `PXOR XMM0` then conditional `MOVQ [RBP+0x30]`; nil ⇒ 0.0 (@ `0x3bc53`) |

`hasSpecular` (Bool), `shadowStyle`, `specularPlacement` are enums/flags (no
numeric defaults).

---

## ICRRenderingParameters.HighlightSettings — decoder shape

Decoder @ `0x38dc0`. CodingKeys 0–9. Most fields use plain `decode` (required,
no default); `decodeIfPresent` (optional, default = nil / applied by the memberwise
default) is used for key 2 (`opacity`), key 6 (`minInsetPixels`), and key 9
(`blendModeOverride`). Field roster from getters: `bias` (Double, key 0),
`inset` (SizeBasedValue&lt;Double&gt;), `opacity` (optional SizeBasedValue),
`minDistancePixels`, `spread` (SizeBasedValue&lt;Angle&gt;), `minInsetPixels`
(optional). No literal scalar defaults are embedded in the decoder itself — the
concrete default HighlightSettings instances live inside the Highlights builder
(see below).

---

## ICRRenderingParameters.Highlights — defaults

Built by `FUN_0006fa70` (@ `0x6fa70`) into a stack buffer that is later `memcpy`'d
into the heap `Highlights` object. Buffer offset = property offset (getter reads
`R13+off`, `defaultChicletLight` at +0). Field names for getter-less slots were
confirmed against the Codable key-string cluster at `0xab3d0`–`0xab490`.
`SizeBasedValue<Double>` is 4 consecutive doubles (size-class ramp); components
listed in memory order.

| property | default | source |
|---|---|---|
| defaultChicletLight (LightAngles, `0x00`) | 0.0 | immediate `puVar1[0] = 0` |
| defaultGlyphLight (LightAngles, `0x08`) | 0.0 | immediate `puVar1[1] = 0` |
| glyphHighlightCurvature (`0x10`, SBV) | **(0.75, 0.7, 0.7, 0.7)** | `DAT_000a1f50` = `00 00 00 00 00 00 e8 3f` (0.75); `DAT_000a1f58` = `66 66 66 66 66 66 e6 3f` (0.7); `DAT_000a1f60` (+0,+8) = 0.7, 0.7 |
| glyphDarklightCurvature (`0x30`, SBV) | **(0.75, 0.7, 0.7, 0.7)** | same DATs (`a1f50`/`a1f58`/`a1f60`) reused |
| chicletHighlightCurvature (`0x50`, SBV) | **(0.7, 0.7, 0.7, 0.7)** | `DAT_000a1f60` 16-byte pattern `66…e6 3f` ×2, repeated |
| chicletDarklightCurvature (`0x70`, SBV) | **(0.7, 0.7, 0.7, 0.7)** | same `DAT_000a1f60` pattern |
| glyphHighlightsUseVCM (`0x90`, Bool) | true | immediate byte 1 at `puVar1+0x12` |
| maxDimChicletLuminance (`0x98`) | **0.2** | `DAT_000a1f70` = `9a 99 99 99 99 99 c9 3f` (`0x3fc999999999999a`) |
| minBrightChicletLuminance (`0xa0`) | **0.99** | getter `…HighlightsV25minBrightChicletLuminance` @ `0x71bc0` reads `0xa0`; `DAT_000a1f78` = `ae 47 e1 7a 14 ae ef 3f` (`0x3fefae147ae147ae`) |
| iconBrightnessOnlyUsesMax (`0xa8`, Bool) | false | immediate byte 0 at `puVar1+0x15` |
| glyphHighlightVCM (`0xb0`) | (0.75, 1.3, 1.5, 0.0, true) | `DAT_000a1f80` = 0.75; `DAT_000a1f88` = `cd cc cc cc cc cc f4 3f` (1.3); immediate `0x3ff8000000000000` (1.5); 0.0; Bool from `0x101` word |
| glyphDarklightVCM (`0xd8`) | (-0.5, 0.5, 1.5, 0.0, true) | `DAT_000a1da0` = `00 00 00 00 00 00 e0 bf` (-0.5); `DAT_000a1da8` = 0.5; immediate 1.5; 0.0; true |

**chicletHighlightCurvature** and **glyphHighlightCurvature** therefore both DO
exist — as `SizeBasedValue<Double>` inside `Highlights` (defaults 0.7 / 0.75-then-0.7).

---

## Still unresolved / not present in this binary

- **totalHighlightsStrength** — it is NOT a `Highlights` property; it is a stored
  property of `ICRRenderingParameters.ClearMode` at struct offset `0x10`
  (`ClearMode.encode` @ `0x6d520` encodes 5 doubles as keys 0–4; key-string
  cluster @ `0xab110` = totalLighteningStrength, totalDarkeningStrength,
  totalHighlightsStrength, contentLighteningStrength, contentDarkeningStrength;
  `contentDarkeningStrength` getter @ `0x3f240` reads `+0x20`, anchoring the
  order). No ClearMode default builder is reachable; `clearMode` is
  `Optional` (`ClearModeVSg`) on `ICRRenderingParameters`, so the default is most
  likely `clearMode = nil`. Its numeric default is **unresolved**.
- **chicletHighlightCurvature** and **glyphHighlightCurvature** DO exist — as
  `SizeBasedValue<Double>` inside `Highlights` (resolved above; defaults 0.7 and
  0.75-then-0.7). The earlier assumption that these were absent was wrong.
- **GlobalConfiguration default lightAngle** — no baked numeric literal; the
  angle is always caller-supplied (see above).
- **ICRUnitSphericalCoordinates.defaultLightDirection** — getter returns 0; no
  meaningful spherical default is stored.
