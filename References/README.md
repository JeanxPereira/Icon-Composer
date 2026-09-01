# References — o corpo do delito

Aqui mora o material de engenharia reversa do Icon Composer: os DMGs da Apple, as
imagens de disco decodificadas, o bundle extraído e os dumps. **Nada disto entra no
git** (ver `.gitignore` desta pasta) — sobrevivem este README e os
`provenance.json`, que são metadado nosso e o lastro de cada selo `[BIN]`.

As ferramentas **não moram aqui**. `udif.py` e `hfs.py` vivem em
`AquaKit/References/scripts/`, a caixa compartilhada dos três projetos de RE da
Apple (AquaKit, SF-Symbols, Icon-Composer). A disciplina de selos é a do AquaKit:
`docs/metodo-re.md` de lá.

## Layout

```
References/
  <versão>/                       ← uma pasta por build, sempre
    provenance.json               ← url + sha256 + tamanho + data. VERSIONADO.
    disk.img                      ← a imagem plana que sai do udif.py
    extracted/                    ← o bundle, como o hfs.py o escreve
    out/                          ← dumps: .ll do metallib, strings, tabelas
    ghidra/                       ← projetos .gpr/.rep
```

A versão que nomeia a pasta é `CFBundleShortVersionString-CFBundleVersion` lido do
`Contents/version.plist` do próprio app — `2.0-125`, não o nome do arquivo do DMG.
Nome de arquivo é o que a Apple resolveu chamar naquele dia; `CFBundleVersion`
incrementa monotonicamente e é o que distingue duas betas.

**Um build por pasta, sempre.** Duas betas na mesma pasta é como se perde a
proveniência — e proveniência perdida é valor que vira `[OBS]`.

## Alvo atual

**Icon Composer 2.0 (125)** — beta 6, arquivo `Icon_Composer_2_beta_6.dmg`.

| campo | valor |
|---|---|
| `CFBundleIdentifier` | `com.apple.IconComposer` |
| `ProjectName` (interno) | **`IconStudio`** |
| `BuildVersion` | 212 |
| `DTSDKName` | `macosx27.0.internal` |
| `DTXcodeBuild` | `27A5252e` |
| `LSMinimumSystemVersion` | 26.4 |
| sha256 do DMG | `484786f72fbf988d0242b2460b00f5adaaf830c7a1d06d95bffbc9d0e96abb2a` |

O nome interno é `IconStudio`, não `IconComposer` — vale para procurar símbolo.

## O pipeline, do DMG ao bundle

```powershell
$AQ = "D:\CodingProjects\AquaKit\References\scripts"

# 1. o container: decodifica e CONFERE os checksums declarados
python $AQ\udif.py Icon_Composer_2_beta_6.dmg --verify --out References\2.0-125\disk.img

# 2. o selo de proveniência
python $AQ\udif.py Icon_Composer_2_beta_6.dmg --provenance References\2.0-125\provenance.json --url "<origem>"

# 3. o filesystem: o que tem lá dentro
python $AQ\hfs.py References\2.0-125\disk.img --tree
python $AQ\hfs.py References\2.0-125\disk.img --compression   # censo decmpfs

# 4. o bundle
python $AQ\hfs.py References\2.0-125\disk.img --extract / --out References\2.0-125\extracted
```

Nenhum passo depende de macOS, de WSL ou de FUSE. Os dois scripts são stdlib pura.

## A extração é PROVADA, não presumida

Um extractor que erra em silêncio produz um bundle plausível e envenena tudo que vem
depois. Quatro instrumentos independentes dizem que este não erra:

1. **Os checksums do próprio DMG.** `udif.py --verify` recalcula o CRC-32 de cada
   blkx e compara com o declarado no `mish`. 6/6 batem neste DMG, e 5/5 no
   `SF-Symbols-8.dmg` — que usa Apple Partition Map em vez de GPT, e 422 chunks
   `raw` contra 3.
2. **Os contadores do volume.** O header do HFS+ declara 64 arquivos e 45 pastas; a
   caminhada pelo catálogo acha exatamente 64 e 45.
3. **A assinatura da Apple.** Os oito `_CodeSignature/CodeResources` do bundle são o
   manifesto da própria Apple, com o SHA-256 de cada recurso. **27 de 27 conferem,
   zero divergência, zero ausente** — inclusive `ictool` e `icrtool`. É um oráculo
   adversarial de graça: foi escrito pela Apple, sem saber que nós existimos.
4. **Decodificadores independentes.** Os dois `Assets.car` abrem no `car_dump.py` do
   AquaKit (BOM válido, CoreUI-1008, Xcode `27A5252e` batendo com o `Info.plist`); o
   `default.metallib` do RenderBox rende **109 módulos de bitcode LLVM** no
   `metallib_extract.py`. Nenhum dos dois foi escrito para este projeto.

**O segundo corpus.** Os quatro acima falam de um volume só. O `SF-Symbols-8.dmg`
é o contraste útil, porque é diferente em tudo que importa: **Apple Partition Map**
em vez de GPT (foi assim que se descobriu que o auto-detect só conhecia GPT — o
`hfs.py` reportou honestamente "não achei" em vez de ler lixo, e a APM entrou),
422 chunks `raw` contra 3, e um único arquivo de **443 MB** contra os 6,8 MB do
maior daqui. Sai `443.291.940` bytes exatos, com magic `xar!` — leitura de fork em
escala, provada.

O que nada disso prova: que o `hfs.py` lê corretamente um volume que nenhum dos
dois DMGs contém. Ver "o que não foi exercitado", abaixo.

## O que este volume ensinou

- **decmpfs é obrigatório.** 48 dos 64 arquivos são comprimidos: 31 por método 4
  (zlib no resource fork) e 17 por método 9 (raw inline). Um leitor que ignore o
  `com.apple.decmpfs` extrai 48 arquivos VAZIOS e não reclama.
- **O método 9 carrega um `0xCC` na frente.** Medido em 17 de 17: o payload tem
  `tamanho + 1` bytes e o primeiro é `0xCC`. O `hfs.py` decide pelo COMPRIMENTO, não
  pelo byte — um arquivo que comece com `0xCC` de verdade enganaria o teste mágico.
- **O CRC-32 do blkx não cobre os chunks `ignore`.** Medido: com o `ignore` dentro
  dá `23ca5da6`, fora dá `09d27beb`, que é o declarado. Confirmado no DMG do
  SF-Symbols, onde as entradas `Apple_Free` — só `ignore` — declaram `crc32=0`.
- **Não há um único `.nib` no bundle.** O app é SwiftUI, e o caminho que o
  SF-Symbols usou (ler a menu bar exata do `MainMenu.nib`) não existe aqui.
- **O binário principal tem 161 KB.** O código está no `IconComposerKit` (6,8 MB) e
  no `IconComposerFoundation` (3,7 MB). Os dez Mach-O são FAT com duas arquiteturas.
- **Há dois `default.metallib`**: o do `RenderBox` (2,0 MB, 109 módulos) e o do
  `IconRendering` (94 KB).
- **Symlinks saem como arquivo de texto** com o alvo dentro (`Versions/Current` →
  `A`). É o que o data fork carrega; o Windows não tem para onde apontá-los.

## O que NÃO foi exercitado

Dito aqui para não virar confiança injustificada mais tarde:

- **decmpfs LZVN (7/8), LZFSE (11/12) e LZBITMAP (13/14)**: `hfs.py` os reconhece,
  os NOMEIA e falha alto. Nenhum aparece neste volume. `--compression` é o
  instrumento que diz se um DMG novo precisa deles.
- **decmpfs método 3** (zlib inline) e o marcador `0xFF`: não ocorre aqui. O código
  existe e está marcado como não exercitado.
- **Extents overflow**: os DOIS volumes têm 0 registros na árvore de extents —
  inclusive o que carrega um arquivo de 443 MB, que coube em extents inline. O
  caminho existe no `hfs.py` e nunca rodou.
- **Chunks ADC, bzip2, LZFSE e LZMA** do UDIF: `udif.py` os rejeita alto em vez de
  pular. Um chunk pulado em silêncio é um buraco na imagem que só aparece como
  arquivo corrompido, muito depois.
- **HFSX case-sensitive**: o `hfs.py` aceita a assinatura `HX`, mas casa nome sem
  diferenciar maiúscula na busca. Em volume case-sensitive isso pode achar o arquivo
  errado.

## Um aviso

Este material é da Apple. Fica local, não é distribuído, não sai do disco. O que sai
daqui para o repo é código reimplementado limpo com um selo apontando para a origem.
