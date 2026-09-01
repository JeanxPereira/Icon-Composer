# Spec — o gradiente, inteiro

**Data:** 2026-09-01 · **Estado:** aprovado, em execução

Fechar o gradiente de ponta a ponta, não em fatias. Este documento diz o que
existe, o que será construído, o que **não** será, e qual é a prova de cada
parte.

---

## 1. Por que agora, e o que ele vale

`scripts/slice-reach.py`, depois de o compositor passar a desenhar raster:

| bloqueio | camadas | |
|---|---|---|
| camada de vidro | 87 | 44,8% |
| **gradiente** (todas as formas) | **~100** | ~25% do corpus, somando os caminhos |
| traço pintado | 31 | 16,0% |

O vidro é maior e é o **muro** — 56 modos de mescla com 6 decodados, o
`RenderState` sem semântica. O gradiente é o maior bloco **cujas peças já estão
no lugar**: o `CoreSVG` já lê `linearGradient`/`radialGradient` com paradas, o
`Gradient` já está modelado, e os seis parâmetros do gradiente automático já
foram lidos do binário (doc 03 §19.4).

---

## 2. São QUATRO produtores, e não são a mesma coisa

`[ART]` Medido no corpus. Confundir os quatro é o erro que este spec existe para
evitar.

| produtor | onde | ocorrências |
|---|---|---|
| **A.** `url(#id)` no SVG | atributo `fill` | 165 referências, 161 gradientes |
| **B.** `linear-gradient` no documento | `fill` de camada / de fundo | 48 + 49 |
| **C.** `automatic-gradient` | `fill` de camada / de fundo | 46 + 57 |
| **D.** `automatic` | `fill` de camada / de fundo | 55 + 28 |

**A** e **B** carregam a rampa. **C** e **D** carregam **uma cor** e derivam a
rampa de uma regra que vive no render.

---

## 3. As formas medidas — o escopo, com números atrás

### 3.1. O lado do SVG

`[ART]` Em 45 dos 149 arquivos:

| | |
|---|---|
| `linearGradient` | **155** |
| `radialGradient` | **6** |
| paradas por gradiente | 2 (19), 3 (67), 4 (14), 5 (60) |
| `gradientUnits` | `userSpaceOnUse` em **159**, ausente em 2 |
| `gradientTransform` | **13** |
| `spreadMethod` | **ZERO ocorrências** |
| `xlink:href` (herda paradas) | **1** |
| dentro de `<stop>` | `stop-color` 581, `offset` 463, `style` 34, `stop-opacity` 18 |

> **Reconciliação.** A primeira contagem deste spec deu `href` = 0 contra o 1 que
> o doc 04 mediu. A regex exigia tag de fechamento, e um gradiente que herda
> paradas vem **auto-fechado** — que é exatamente a forma dele. Corrigida, as
> duas medições concordam em 159/13/1.

`[INF]` **`spreadMethod` não é implementado**, e isso é decisão de escopo com
número: zero ocorrências em 161 gradientes. O comportamento é sempre `pad`.

### 3.2. O lado do documento

`[ART]` **`linear-gradient` tem SEMPRE exatamente duas paradas** — 48 de 48. Não
é uma tabela de paradas arbitrária: é uma rampa de duas cores mais uma
`orientation` (`start` e `stop`, pontos no quadrado unitário; 15 formas
distintas).

`[ART]` As cores de `automatic-gradient` chegam em `extended-srgb` e
`display-p3`.

---

## 4. O que será construído

### 4.1. `Gradient` — a amostragem, na torre `RenderBox`

Uma rampa amostrada em `t ∈ [0,1]`, com as paradas ordenadas, `pad` nas pontas,
e interpolação entre as duas paradas que cercam `t`.

**A prova:** diferencial GPU × CPU pelo mesmo caminho que os cinco estágios
anteriores — o shader e o oráculo lêem o mesmo arquivo, e a varredura de mutação
prova que cada guarda morde.

### 4.2. A geometria — linear e radial

- **Linear:** `t` é a projeção do ponto no eixo `(start → stop)`, normalizada.
- **Radial:** `t` é a distância ao centro sobre o raio.
- **`userSpaceOnUse` × `objectBoundingBox`:** o primeiro em 159 de 161; o segundo
  é o *default* do SVG e ocorre em 2, então **os dois** entram.
- **`gradientTransform`** (13 ocorrências) entra, componível com a transformação
  da forma.

### 4.3. O `linear-gradient` do documento

Duas cores e uma `orientation` no quadrado unitário, mapeada para a caixa da
camada.

### 4.4. `automatic-gradient` e `automatic` — e aqui há um RISCO NOMEADO

Os **seis parâmetros** estão medidos (doc 03 §19.4):

```
basePosition 0.0   saturationBoost 0.2
dimLightening 0.04   midDimLightening 0.08
midBrightLightening 0.15   brightLightening -0.05
```

`[OBS]` **A REGRA que os aplica não foi lida.** Saber que existem quatro faixas
de clareamento não diz onde ficam as fronteiras entre elas, nem em que espaço a
luminância é medida, nem como `basePosition` e `saturationBoost` entram.

**Isto é um passo de engenharia reversa que pode não fechar**, e o spec o trata
como tal:

- Se o corpo da função for lido → `automatic-gradient` é **transcrito** e gatado
  como o resto.
- Se **não** for → a arma fica **NÃO IMPLEMENTADA e nomeada**, do jeito que o
  `slice-reach` já nomeia. **Não será aproximada.** Uma rampa plausível inventada
  a partir de seis números cujo uso não foi lido produziria pixel diferente do
  alvo com aparência de acerto, que é o pior resultado possível num projeto de
  reprodução.

`[OBS]` `automatic` (55 + 28) provavelmente deriva do sistema e não da cor da
camada. **Não foi medido**, e é a primeira pergunta a responder nesta arma.

---

## 5. O que este spec NÃO cobre

| | por quê |
|---|---|
| `spreadMethod` (reflect/repeat) | zero ocorrências em 161 gradientes |
| Os 50 modos de mescla restantes | outra frente; o gradiente usa o caminho chapado |
| Vidro | o muro, e explicitamente fora |
| Conversão display-p3 → sRGB | a matriz não foi medida do alvo; segue reportada |

---

## 6. As tarefas

| # | tarefa | prova |
|---|---|---|
| 1 | medir os corpos de `Gradient::color`, `sample_stops_uniform`, `sample_stops_binary`, `fold_value` no IR | o que está decodado sai em doc 03 |
| 2 | `GradientOracle` na CPU + shader, amostragem de rampa | diferencial GPU × CPU |
| 3 | geometria linear e radial, `userSpaceOnUse` e `objectBoundingBox` | oráculo de forma fechada |
| 4 | `gradientTransform` | 13 arquivos reais do corpus |
| 5 | ligar a pintura `url(#id)` do SVG no `renderSvgPlaced` | o alcance sobe, medido |
| 6 | `linear-gradient` do documento com `orientation` | bundle sintético + corpus |
| 7 | ler a regra do `automatic-gradient` no binário | **pode não fechar** — ver §4.4 |
| 8 | mutações para tudo acima | cada guarda morde |
| 9 | re-medir o alcance e atualizar a régua | número novo em doc 03 |

---

## 7. Como saber que acabou

- O gate passa com a varredura completa.
- `slice-reach` mostra o alcance **novo**, e o gradiente sai da lista de
  bloqueios pelas formas implementadas.
- Cada arma não implementada está **nomeada** no relatório do `icrender`, com o
  motivo — nunca desenhada errada em silêncio.
