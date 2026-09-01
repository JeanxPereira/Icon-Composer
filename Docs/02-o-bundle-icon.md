# 02 — O bundle `.icon`

O doc 01 cobre o `icon.json`. Este cobre a pasta em volta dele.

## As fontes

**As listagens.** 45 bundles públicos, lidos pela API do GitHub sem baixar um
byte de asset — a forma da pasta, sem custo.

**Os bundles locais.** 55 bundles completos em `References/corpus/<slug>/`,
baixados por `scripts/fetch-icon-corpus.py --with-assets`. É contra esses que o
`corpus_bundle_gate` roda.

## 1. A forma

`[ART]` Um `.icon` é uma **pasta**, e em 45 de 45 ela tem exatamente duas coisas:

```
<nome>.icon/
  icon.json        o documento (doc 01)
  Assets/          os arquivos que as camadas nomeiam
```

`Assets/` é **plano** — nenhum dos 45 tem subdiretório dentro dele. Fora
`icon.json` e `Assets/`, nada: um único bundle carrega um `.gdignore`, que é
arquivo do repositório dele e não do formato.

## 2. O que mora em `Assets/`

`[ART]` Medido nos 55 bundles locais: **209 arquivos**, e duas extensões só.

| extensão | n (na amostra de 35) |
|---|---|
| `.svg` | 76 |
| `.png` | 36 |

Tamanhos de 164 bytes a 759 KB, mediana 3,1 KB. Uma camada aponta para um asset
pelo `image-name`, e o nome é **o nome do arquivo**, sem caminho e sem extensão
implícita: `"image-name": "8.svg"` é `Assets/8.svg`.

## 3. A referência entre o documento e a pasta

`[ART]` **Todo arquivo em `Assets/` é nomeado por alguma camada, em algum
contexto.** 55 de 55 bundles, zero arquivos mortos. Esta metade se sustenta.

`[ART]` **Nem todo nome resolve.** Dois dos 55 bundles pedem um arquivo que não
existe — e não existe no repositório de origem também, então não é perda da
coleta:

| bundle | pede | tem |
|---|---|---|
| `chromium/chromium` | `2 – Layer.svg` | `2.svg` |
| `mysk-research/loupe` | `loupe-icon-light 3.png` | `loupe-icon-light.png` |

Os dois vêm de uma **especialização**, não do `image-name` simples, e os dois têm
a cara de nome velho que sobreviveu a um rename dentro do app. Um leitor que
assuma que toda referência resolve quebra em 3,6% dos bundles reais.

> **Correção.** A primeira medição, sobre 35 bundles, disse que a referência era
> uma bijeção, e este documento chegou a afirmar isso. Com 55 a afirmação caiu.
> O que ficou de pé é a metade acima, e o gate assere só ela.

## 4. Como o corpus é escolhido

`[ART]` A ordem em que os bundles são baixados **importa para a cobertura**, e
isso também foi aprendido errando: a primeira coleta pegou os 40 primeiros em
ordem alfabética e caiu em 40 bundles onde **nenhuma** camada especializa o seu
`image-name` — de forma que o gate não distinguia um coletor que percorre os 20
contextos de um que só olha o base. Onze dos 145 documentos especializam uma
imagem; o `fetch-icon-corpus.py` agora ordena por isso, e a mutação que reduz o
coletor ao contexto base passou de **1 asserção avermelhada para 3**.

## 5. O que NÃO está resolvido

1. **Se `Assets/` pode aninhar.** Nenhum dos 45 aninha. Um bundle que aninhasse
   viria com a pasta ausente da listagem do leitor, não com um caminho errado.
2. **Que outros tipos de arquivo o formato aceita.** Só SVG e PNG foram vistos.
   O `CoreSVG.framework` e o `ImageIO` que o `IconRendering` linka sugerem mais,
   mas sugestão não é medida.
3. **O que decide entre SVG e PNG.** Os dois convivem no mesmo bundle; nada no
   `icon.json` distingue, o nome do arquivo é a única pista.
4. **Se o app repara uma referência pendurada** ao abrir, ou se ela sobrevive a
   um salvamento. Os dois casos do §3 estão commitados nos repositórios de
   origem, o que sugere que sobrevive — mas isso é leitura, não teste.
