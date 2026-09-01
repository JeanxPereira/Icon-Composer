# 00 — Levantamento

Medido em 2026-08-31, antes de escrever uma linha. O que está aqui foi **conferido
nesta máquina**; o que é leitura minha e não medida está marcado como tal.

Alvo: `Icon_Composer_2_beta_6.dmg`, 19.812.864 bytes — o único arquivo do repo hoje.
Não há git, não há código, não há Docs anteriores neste diretório.

---

## 1. O DMG, medido

| campo | valor |
|---|---|
| formato | UDIF v4, trailer `koly` nos últimos 512 bytes |
| data fork | 19.804.247 bytes; XML `blkx` em `0x012E3057`, 7.622 bytes |
| tabela de partição | GPT — MBR, Primary/Backup GPT, **uma** partição `Apple_HFS` |
| partição HFS+ | offset 17.408, volume header `H+\x00\x04` em 18.432 |
| imagem plana decodificada | 21.233.664 bytes |
| tipos de chunk | só `raw` (0x1) e `zlib` (0x80000005) |

**Consequência prática:** o `udif_extract.py` que sobrou do SF-Symbols (48 linhas,
stdlib pura) decodifica este DMG **sem alteração nenhuma** — rodei, escreveu os 21,2 MB.
O DMG do SF-Symbols era Apple_HFS também, mesma família de chunks.

## 2. O que tem dentro — FECHADO

> Inventário completo em `References/README.md`, e reproduzível com
> `hfs.py <disk.img> --tree`. São 64 arquivos e 45 pastas, o mesmo que o volume
> header declara, com 27 de 27 SHA-256 conferidos contra os manifestos de
> assinatura da própria Apple.

```
Icon Composer.app/
  Contents/
    MacOS/Icon Composer
    Info.plist, version.plist, PkgInfo, _CodeSignature/CodeResources
    Frameworks/
      IconComposerKit.framework
      IconComposerFoundation.framework
      IconRendering.framework
      RenderBox.framework
      CoreSVG.framework
    Resources/
      Assets.car, AppIcon.icns, default.metallib, License.rtf, en.lproj/
      Backgrounds/  (6 jpegs "sine-*")
    PlugIns/
      Icon Composer QuickLook Preview.appex
      Icon Composer Thumbnail.appex
    Executables/
      ictool, icrtool
```

Três achados que mudam o plano:

1. **Não tem `.pkg`.** O SF-Symbols-8.dmg carregava um `SF Symbols.pkg` de 443 MB e
   um cpio `odc` dentro dele. Aqui o `.app` está solto no volume HFS+ — um passo a
   menos na cadeia.
2. **Os frameworks privados vêm DENTRO do app, e três deles são cópias literais
   do sistema.** `RenderBox`, `IconRendering` e `CoreSVG` carregam
   `LC_ID_DYLIB = /System/Library/PrivateFrameworks/…` — o caminho absoluto do
   sistema, não um `@rpath`. São **frameworks privados da Apple carregados junto
   com o app**, não forks dele. Já `IconComposerKit` e `IconComposerFoundation`
   são `@rpath`, e esses sim pertencem ao app.

   O que isso significa para o projeto: **um carve do dyld shared cache, de
   graça**. Não precisa de IPSW de 21 GB, nem de `extract-dsc.sh`, nem de
   `dsc_reader.py` para alcançar `RenderBox` e `CoreSVG` — eles estão em disco,
   inteiros, com os load commands intactos (é o que permitiu o `swift_meta.py`
   ler os endereços do próprio arquivo). É a diferença mais barata entre este
   projeto e o AquaKit.

   Fica aberto **qual cópia ganha em tempo de execução**: o link é por caminho
   absoluto, o que faz o dyld carregar a do sistema, e o app declara
   `LSMinimumSystemVersion 26.4` compilado contra o SDK 27.0 — o que explica por
   que ele carregaria uma cópia mais nova junto. O instrumento que responde é o
   `LC_BUILD_VERSION` da cópia empacotada contra a do sistema.
3. **`com.apple.decmpfs` aparece no volume.** Os arquivos estão comprimidos por
   AFSC: um leitor HFS+ ingênuo devolve data fork de tamanho zero e o conteúdo real
   mora no xattr / resource fork, em zlib (tipos 3/4), LZVN (7/8) ou LZFSE (11/12).
   Quem não tratar isso extrai um app vazio e acha que o parser está certo.

E dois brindes: `ictool` e `icrtool` são **CLI**. Ferramenta de linha de comando
costuma carregar `--help`, nomes de flag e mensagens de erro em texto claro — é a
fonte mais barata de schema que existe, e o SF-Symbols não tinha equivalente.

## 3. O buraco de ferramental — FECHADO em 2026-08-31

O levantamento achou o buraco: **não existia leitor de HFS+ versionado em lugar
nenhum**, e o `udif_extract.py` que fez o SF-Symbols sobrevivia por acaso num
scratchpad temporário. Ferramenta que abre o corpo de delito não mora em pasta
temporária.

Agora existem dois, em `AquaKit/References/scripts/`, stdlib pura, sem macOS, sem
WSL, sem FUSE:

- **`udif.py`** — decodifica o container `.dmg`, lê a GPT, extrai partição, grava
  proveniência, e **confere os CRC-32 que o próprio DMG declara**.
- **`hfs.py`** — lê o volume HFS+/HFSX: árvore de catálogo, extents overflow,
  árvore de atributos, decmpfs, e um censo (`--compression`) que diz quais métodos
  de compressão um volume novo realmente usa.

O caminho do `hfsfuse` não foi necessário e fica registrado como opção de oráculo
diferencial — montado como caixa-preta, nunca lido: é GPL, e a fronteira de
clean-room do CMI-Recomp vale aqui igual. O que foi lido para escrever os dois é
**spec**: a TN1150 da Apple e a documentação de formato do libyal.

Descartado pelo caminho: `mount -t hfsplus` no WSL é impossível — o kernel
6.6.87.2-microsoft não traz o módulo `hfsplus`.

## 4. A infra de update — a boa notícia e a ruim

**A boa.** O canal de release é público e polável:

```
https://devimages-cdn.apple.com/design/resources/download/Icon-Composer.dmg
HTTP 200 · 15.857.896 bytes · Last-Modified: Wed, 24 Sep 2025 17:06:09 GMT
ETag: "26869a5057e3249e80b1ad1ecd7b0481"
```

Um `HEAD` por dia comparando ETag/Content-Length/Last-Modified detecta build nova
sem baixar nada. É o mesmo endpoint que serve o `SF-Symbols-8.dmg` (confirmado,
443.397.894 bytes).

**A ruim.** O que o Jean tem é `Icon_Composer_2_beta_6.dmg`, 19.812.864 bytes — o
**Icon Composer 2, beta 6** — e **não é** o arquivo daquela URL (nome com underscore,
4 MB maior, e a URL pública serve a linha 1.x de release). O caminho
`Icon_Composer.dmg` no mesmo CDN dá **403**. Beta vem do
`developer.apple.com/download/all`, atrás de sessão autenticada da Apple Developer.
Então: **release automatiza, beta é entrega manual**. O que dá para automatizar dos
dois lados é a metade que importa — o **selo de proveniência**: sha256 + tamanho +
data + origem gravados num `<build>.provenance.json`, exatamente como o AquaKit faz.

**Orçamento de disco:** 20 MB por build. O AquaKit gasta 55 GB por build e não cabem
dois; aqui cabem **mil**. Isso muda uma coisa de forma estrutural: o *diferencial
build-a-build* deixa de ser um luxo e vira instrumento de primeira classe — é
justamente o que o SF-Symbols escreveu que gostaria de ter e não teve (doc 06 §6).
D: tem 225 GB livres.

## 5. O que já existe de Icon Composer — e por que não é base

`JeanxPereira/Icon-Composer` no GitHub, **privado**, 613 KB, TypeScript, último push
2026-08-23 (salvo na auditoria do dell04, junto com os outros 668 commits sem remote).
52 commits, 4 branches:

- `main`, `slice-1-webgpu-engine` (Next/WebGPU, abandonado)
- `native-shell-imgui-vulkan` (abandonado na task 5/13)
- `tauri-wgpu-shell` — **o que ficou de pé**: Rust em três camadas
  (`document` puro / `render` wgpu / `ipc` fino), modelo `.icon` com round-trip
  provado contra um `ImHex.icon` real, compositor offscreen fp16 com 17 blend modes
  em WGSL, 86 testes.

E `docs/re/icon-composer/` com `renderbox-engine.md`, `editor-ui.md`,
`ui-controls-map.md`, `appearance-recipes.md`, `kernels/icon-glass.md`,
`default-ramps.md`.

**O aviso, que é meu e está no topo daqueles arquivos:** essas 1.414 linhas vieram de
conversa com o Floyd, não do binário. Foram migradas marcadas como **material A
CONFRONTAR, não base**. Não têm selo, não têm endereço, não têm proveniência. Sob a
disciplina do AquaKit isso é `[OBS]` na melhor das hipóteses — e a regra é que `[BIN]`
ganha de `[OBS]` sempre. O valor real daquele repo é: (a) o `.icon` com round-trip
provado, (b) os 17 blend modes com gate GPU-vs-CPU, (c) uma lista de perguntas boas.
O valor que ele **não** tem é resposta selada.

## 6. O esquema do SF-Symbols, para replicar

O que faz aquele repo funcionar, em ordem de importância:

1. **`Docs/NN-*.md` numerados**, um por camada do alvo, com um `README.md` que é
   tabela de conteúdo + *estado* por peça (decodado / inventariado / aberto), e um
   `06-open-questions.md` onde o que não fechou vira **pergunta**, não chute plausível.
2. **Selos por linha.** `[BIN]` (lido do binário), `[NIB]` (lido do nib), `[INF]`
   (inferido de comentário de localização / nome de tipo). Onde o doc diz que um
   controle existe, é medido; onde diz como está arranjado, é leitura — e a linha diz
   qual dos dois é.
3. **`Docs/Specs/` e `Docs/Plans/`** datados, um por milestone.
4. **`Source/core` C++23 sem exceções** (`Result<T>`), `Source/cli`, `Source/viewer`
   (ImGui), `Source/tools/*.py` (os oráculos Python).
5. **`scripts/gate-*.ps1`** — e é aqui que mora o que separa este método de "escrevi
   um parser e passou":
   - M1: diferencial contra um oráculo independente, 36.975 documentos, char a char.
   - M2a: oráculos **analíticos** (área fechada) porque não havia segunda
     implementação, + goldens fixados, + **varredura de mutação obrigatória** — seis
     defeitos injetados um a um, cada um TEM que avermelhar a suíte, aplicados em
     árvore limpa e restaurados por backup verificado em SHA-256.
   - A lição que justifica o item anterior: o gate M1 ficou **verde com o
     `kIntegerWidths` corrompido**. 99/99 testes passando, 36.975/36.975 idênticos.
     Só a mutação obrigatória pegou.
6. **O corpus nunca entra no git.** Aponta-se por variável de ambiente
   (`SF_CATALOG_DIR`), e teste que precisa do corpus e não acha a variável **falha**,
   não pula.

E o esquema de UI (doc 08) — o modelo direto do que queremos aqui: *"nada aqui é
adivinhado de screenshot"*. Menu bar exata do `MainMenu.nib`, colunas exatas do
`GlyphListViewController_v2.nib`, nomes de classe do binário, e o painel SwiftUI —
que não tem nib para ler — marcado `[INF]` e derivado de comentário de localização.

**RESPONDIDO em 2026-08-31: não há um único `.nib` no bundle.** A caminhada
completa pelo catálogo (64 arquivos, todos listados) não acha nenhum. O app é
SwiftUI, e o caminho do doc 08 — menu bar exata lida do `MainMenu.nib` — **não
existe aqui**. A UI terá de sair de `strings` + metadata Swift (`swift_meta.py`) +
os dois `.appex` + o `ictool`, e boa parte dela nascerá com selo mais fraco que o
`[NIB]` do SF-Symbols. Isso é argumento a favor da milestone escolhida (o formato
`.icon`) antes da UI.

## 7. A infra do AquaKit, para replicar

- `References/` dentro do repo, com `.gitignore` deixando passar **só** `README.md` e
  `scripts/` — o corpo de delito nunca entra no git, o que o reproduz sempre entra.
- **Uma pasta por build**, nunca duas builds na mesma pasta: proveniência perdida é
  valor que vira `[OBS]`.
- `<build>.provenance.json` com url + sha256 + tamanho + data. Não é burocracia: é o
  que permite, meses depois, afirmar que a constante `[BIN]` saiu *daquele* binário
  *daquela* build.
- Script de fetch com resume e verificação de hash publicado.
- `docs/metodo-re.md` — a disciplina: o repo carrega o resultado reimplementado limpo
  e o selo apontando para a origem, **nunca** o material da origem.

## 8. O vínculo com o AquaKit — onde os dois projetos se tocam de verdade

| peça | AquaKit tem | Icon Composer precisa |
|---|---|---|
| `default.metallib` → bitcode LLVM → `.ll` | `metallib_extract.py`, provado ponta a ponta com llvmlite 0.49 / LLVM 22.1.0 contra `air64_v29` | **sim** — é o shader do vidro do ícone |
| `Assets.car` sem macOS | `car_dump.py` (+ o `core/car` em C++ do SF-Symbols) | sim — `Assets.car` do app |
| tipos/campos Swift de Mach-O | `swift_meta.py` | sim, e provavelmente é a fonte da UI |
| Ghidra headless + `decompile.ps1` | sim | sim |
| resolver `bl` opaco de ObjC | `objc_stub_resolve.py` | sim, se houver AppKit no meio |
| dyld shared cache | `dsc_reader.py`, `extract-dsc.sh` | **não** — os frameworks vêm no app |
| UDIF / HFS+ | ❌ **não existe** | **sim, e é o bloqueio nº 1** |

E o vínculo conceitual: `RenderBox` + `IconRendering` são o motor que desenha o
Liquid Glass **do ícone**; o AquaKit decodifica o Liquid Glass **do controle**
(CASDF, receitas de vidro, `CABackdropLayer`). São o mesmo material visto por duas
janelas — e o AquaKit já tem 13 camadas inventariadas e a cascata de 3 níveis
(default de efeito → receita → instância) que provavelmente vale aqui também.

## 9. As três decisões que travavam o resto — DECIDIDAS 2026-08-31

> **(a)** O **AquaKit vira a caixa de ferramentas** dos três projetos. `udif.py` e
> `hfs.py` nascem em `AquaKit/References/scripts/`, alcançados daqui por variável de
> ambiente, no mesmo padrão do `SF_AQUAKIT_SCRIPTS` que o SF-Symbols já usa.
>
> **(b)** Do repo antigo entra **só `docs/re/`**, como `Docs/_confrontar/`, com aviso
> de origem no topo de cada arquivo. `ImHex.icon` e os 17 blend modes entram como
> **fixture e oráculo**. O shell Tauri fica de fora desta rodada.
>
> **(c)** Milestone 1 é o **formato `.icon`**. M0 continua sendo a extração.



**(a) Onde mora a ferramenta compartilhada.** O SF-Symbols já depende do AquaKit por
variável de ambiente (`SF_AQUAKIT_SCRIPTS` aponta para `bvg_dump`/`car_dump`
originais). Três opções: copiar de novo em cada repo (foi o que aconteceu, e já
custou o extractor de UDIF); um repo `apple-re-kit` comum; ou consagrar o AquaKit
como a caixa de ferramentas dos três. **Minha recomendação: o terceiro** — é o
precedente que já existe, custa zero repo novo, e o `udif.py`/`hfs.py` nascem já no
lugar em que o SF-Symbols também os alcança.

**(b) O que fazer com os 52 commits antigos.** Recomendo trazer só `docs/re/` como
**pasta de perguntas** (`Docs/_confrontar/`), com o aviso de origem no topo de cada
arquivo, e o `.icon` + os 17 blend modes como *fixtures e oráculo*, nunca como
verdade. O shell Tauri fica de fora desta rodada: aqui o produto é a **decodificação
selada**, e o viewer vem depois — como no SF-Symbols, onde o viewer é M2b, não M0.

**(c) O alvo da primeira milestone.** Três candidatos, e eles não competem tanto
quanto parece: o formato `.icon` (o mais barato, tem oráculo pronto, e o `ictool`
provavelmente descreve o schema sozinho); a UI do app (o pedido explícito, mas
depende de descobrir se há nib); o motor de render (`RenderBox` + `default.metallib`,
o mais caro e o de maior vínculo com o AquaKit).

## 10. O caminho crítico

1. ✅ `udif.py` + `hfs.py` versionados em `AquaKit/References/scripts/`. Ver §3.
2. ✅ `.app` extraído em `References/2.0-125/extracted/`, `provenance.json` gravado
   (sha256 `484786f7…`), extração provada por quatro instrumentos independentes —
   `References/README.md` lista os quatro.
3. ✅ Inventário fechado, e a pergunta do §6 respondida: **não tem nib**.
4. ✅ O schema do `.icon`, em `01-o-formato-icon.md`. Veio mais barato do que o
   previsto e por outro caminho: o `ictool` é casca fina, e quem carrega o schema
   é o `IconComposerFoundation` — os `CodingKeys` do Swift, em claro no
   `__swift5_fieldmd`, **são** as chaves do JSON. 31 de 31 chaves de um `.icon`
   real explicadas pelo binário.
5. Escrever o spec da M1 — que agora é *a gramática dos valores*, não as chaves
   (doc 01 §9) — e trazer o `Docs/_confrontar/`.
