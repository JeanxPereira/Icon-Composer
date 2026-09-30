# Laudo — Tinted, as opções do Export, a localização e as cores do seletor (30/09/2026)

*Três investigações só de leitura, em paralelo, sobre as fatias de
`References/27.0-129/out/slices/`. Cada varredura foi conferida em cobertura
(capstone com `skipdata`: IconRendering 570.776/570.776 bytes de `__text`, Kit
385.595/385.595). Os achados que viraram código foram reconferidos à mão antes
(anotado em cada um).*

Selos: `[BIN]` lido no binário (endereço) · `[INF]` inferido · `[OBS]` aberto.

---

## 1. Tinted

### 1.1. Onde o modo mora

- `ICRIconStyle` = `{platform, appearance, renderingMode{contents},
  layoutDirection, designGeneration}` `[BIN]` (fieldmd 0xA4110/0xA4184), copiado
  para `ctx+0x4E0` (0x428C8–0x428E4). `ctx+0x4E1` é a aparência (light 0 /
  dark 1), `ctx+0x4E8..0x500` a cor do tint (4 × Double), `ctx+0x508` a
  `saturation`, `ctx+0x510` a etiqueta: 0 = tinted; 1 com carga zero = `.color`;
  1 com carga = `.clear` (0x42C14–0x42C40).
- **Isto nomeia os "cinco Doubles + byte de portão" que o doc 03 §36.3 e
  `GlassSpecular.h` deixaram sem nome:** o estado de recoloração identidade é
  `renderingMode == .color`.
- `RenderingMode.tinted(with:)` (0x5A6F4) e `ICRIconLayer.setTintColor`
  (0x57B10) guardam `.tinted(Color(r,g,b, alpha: 1), saturation: c.alpha)`.
- A cor, no app `[BIN]` (Kit 0x128B88–0x128C20):
  `tintLogicalSpectrum.color(at: tintSpectrumPosition).opacity(tintAlpha)`, com
  `systemRed` de reserva. O espectro é um `Gradient` de sete cores sRGB
  (Foundation 0x3EDC8): FF0E00, FF9B00, FFD400, 00D721, 0007FF, A100F2, FF0E00.
  Padrões: posição 0,75, alfa 0,625 (0x19F4/0x19FC); `tintStrength =
  (tintAlpha − 0,25)/0,75`.

### 1.2. Tinted Dark — a receita, dentro do IconRendering

Só com aparência escura e etiqueta ≠ 1 (0x43190–0x4334C; 0x48254–0x48414):

1. `addSaturationFilterWithAmount:(saturation)` — RenderBox `set_saturate`
   (0x11C000), matriz Rec.709. **Reconferido:** os pesos são imediatos
   `0x3E59B3D0` = 0,2126 e `0x3F371759` = 0,7152.
2. Duotom (0x7E408, 0x7E5A0–0x7E5E0): `out.rgb = lo + in.rgb·(hi − lo)`, alfa
   intocado, com `lo = mix(preto, mix(preto, tint, f), A.a)` e `hi = mix(branco,
   tint, tint.a)`; `f = darkTintDuotoneShadowBlendFactor` (padrão 0). Com os
   padrões: `out = saturate(in, s) × tint`.
3. Tudo é desenhado dentro da camada filtrada; os realces do chiclet também,
   enquanto `darkTintHighlightsBlendWithContent` (padrão true).

Comum aos modos tingidos: fundo `automatic` transparente (já em
`FillResolve.h`), sombra forçada `neutral` (0x49F40), passe de contorno só em
`.color` (0x47B88–0x47BB8). A camada compõe com `kCAFilterScreenBlendMode`
(0x3FC60–0x3FD04).

**Implementado** (commit 791efbf): `IconRenderOptions::tint`,
`shadowEffectiveStyle` passa a ser chamado, `applyTintedDark` na imagem
pronta. `[OBS]` espaço linear ou codificado do RenderBox; interpolação do
`Gradient.color(at:)` do SwiftUI (usada: linear em sRGB `[INF]`).

### 1.3. Tinted Light não é implementável destas fatias

A cor do tint não é lida no caminho principal do Tinted Light: ele recebe o
`ClearMode` (0x42D84–0x42DC8, portão num bit de `self+0x25A` sem nome) e vai
para a cadeia de CAFilter do Clear quando `applyToLightTintToo` (padrão true,
0x3FD38). A cor final sai do QuartzCore, a mesma fronteira do Clear.

### 1.4. Correções ao laudo de 20/09 (`2026-09-20-clear-mode.md`)

- §3.3 chamou 0x3FAE4 de caminho *mitigado*. É o contrário: a fábrica só
  trabalha com `drawMitigatedVersion` **falso** (0x3FB18, `tbz`).
- §3.4 ("o IconRendering nunca desenha com o ClearMode"): falso. 0x4AF20 e
  0x47D2C montam matrizes de cor do RenderBox com os Doubles do ClearMode por
  `ldr d` (R = lightening, G = darkening, B = highlights). A varredura daquele
  laudo procurava só leituras de byte.

## 2. As opções do Export

Caminho: Kit 0x3AE88 → 0x1284A4 (monta `FinalizedIcon` + `GlobalConfiguration`)
→ `FinalizedIcon.rendered(with:)` 0x3B010. Nenhum pós-processo da imagem no Kit.

| opção | o que muda | veredito |
|---|---|---|
| **Nome do arquivo** | Kit 0x3B100: `[nome, plataforma (tabela 0x1853F8), rendição (tabela 0x185438: Default, Dark, TintedLight, TintedDark, ClearLight, ClearDark), localização (só se não for Base), "%g@%ldx"]`, nils fora, juntado com "-" `[BIN]` | **feito** |
| **Hero** | `ConstrainedPlatform.heroPreviewSize` (Foundation 0x23B80): 1024 pt iOS/macOS, 1088 pt watchOS, escala trocada por `overrideScale` `[BIN]` | **feito** |
| **macOS pre-Tahoe** ("Use legacy metrics" é o subtítulo do mesmo item, 0x400A4 — uma opção, não duas) | `relativeIconInset = nil` (inerte: só lido em `useLegacyInsetting`, que o Kit nunca liga) e `chicletDropShadow = true`, que só pesa com `drawMitigatedVersion` `[BIN]` | parcial: muda pixel só com "Glass Chiclet" excluído num modo tingido/clear, pelo passe de sombra 0x433D0, não transcrito |
| **Glass Chiclet** Excluded (`mitigateGlassExports`) | `drawMitigated = mitigateGlassExports && rendition > 1` (0x47CD0–0x47CE8): só nas quatro tingidas/clear. Pinta um preenchimento chapado (0x469E8/0x46FC4, cor de 0x5E254), troca o conjunto de realces (0x47770) e as constantes de 0x478B4 `[BIN]` | parcial: portão exato, cores e realces do caminho mitigado não lidos |
| `maskToChiclet` | copiado para a config (+0x1B) e **nenhum leitor achado** `[OBS]` | nada a fazer |
| `bareRender` | deixa `GlobalConfiguration()` nos padrões (0x1287B4) `[BIN]`; o Export não o liga | nada a fazer |

## 3. Localização no `.icon`

- **`"languages"`** na raiz: array de strings de locale ID (`LanguageID
  {localeID}`), omitido quando vazio (0xC93D0) — por isso o corpus não tem
  nenhum. `[BIN]` `Snapshot.JSONContent.CodingKeys` caso 4 (0x418E0).
  O conjunto aceito é `NSLocale.availableLocaleIdentifiers` canonizado
  (0x39904): `[INF]` grafia ICU com sublinhado (`pt_BR`, `zh_Hans`). Fora dele,
  o diagnóstico `unknown-locale-id` (0x12aec0).
- **A imagem localizada é um ARQUIVO, não uma chave:**
  `Assets/<localeID>.lproj/<mesmo nome>` (leitor 0x8E4D0, `.lproj` em 0x8E718,
  `lprojName` 0x38970). Exigências: a variante base existe e o tamanho é o
  mesmo da base (`localized-asset-size-mis-match`).
- `localization`/`languageDirection` existem nos CodingKeys de
  `Specialization` mas são **mortos** em 27.0-129: nem lidos nem escritos
  (0xD0C44/0xD0F5C).
- No render: `Localization {language(id), base, leftToRight, rightToLeft}`;
  espelha a base quando a camada é espelhável e a direção é RTL (0x9B754).
  No nome do arquivo: `Base` / `Left-to-Right` / `Right-to-Left` (0x38F24).

**Não implementado.** Pede leitura das pastas `.lproj` no `IconBundle`, a
seleção por localização no contexto de render e a UI de idiomas.

## 4. As cores do seletor

- **Standard: 15** (Kit, init único 0x5D838; cabeçalho do array em 0x1864B0 =
  15, **reconferido**): systemRed, Orange, Yellow, Green, Mint, Blue, Cyan,
  Teal, Indigo, Purple, e `white.mix(with: black, by: t)` para t = 0, 0,25,
  0,5, 0,75, 1. Os valores **não** estão no binário: o alvo resolve
  `Color.systemX` em aparência clara em tempo de execução. Usados os da tabela
  publicada da Apple para o macOS claro `[INF]`.
- **Recent: 10** (`RecentColorsManager.fixedSize`, `mov w9,#0xa` em 0x5F1F0,
  **reconferido**), guardadas em UserDefaults `"recentIconColors"` (0x1DBC40).

**Implementado** (commit 92f752f).
