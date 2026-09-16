# Laudo — o device do Onyx, e por que o app continua criando o seu

*15/09/2026. Uma decisão parada entre uma spec e uma branch, resolvida pelo
fato técnico. A branch foi apagada; a decisão da spec fica, e o que a torna
segura passa a estar escrito no código em vez de suposto.*

---

## 1. A pergunta

Havia duas afirmações incompatíveis sobre o mesmo processo.

A **spec 13/09 §6** decidiu que `app/` cria o seu próprio `rb::Device`, e que
dois `VkInstance` no mesmo processo — o do Onyx e o do RenderBox — são
deliberados.

A branch **`alt/device-adotado-do-onyx`** (`014c0ec`) dizia que isso não pode
funcionar, e a frase dela era precisa:

> o volk guarda UMA tabela de dispatch, com escopo de processo, e o Onyx a
> sobrescreve com o device dele (`VkContext.cpp:362`, `volkLoadDevice`). Um
> segundo device chamando por essa tabela cai no device do Onyx, em silêncio.

A branch então trocava `rb::Device::create` por `rb::Device::adopt`, tomando
instance/physical/device/fila prontos do Onyx.

O que torna isso digno de um laudo, e não de uma preferência, é que a branch
**não está errada sobre o mecanismo**. Ela está errada sobre a conclusão. E o
modo de falha que ela descreve é real: não é crash, é render submetido no lugar
errado sem uma linha de aviso.

---

## 2. As duas não nasceram no mesmo lugar, e essa é metade da resposta

```
680e090  "o app esta escrito e compila, mas ainda nao entra no build:
          o volk do Onyx colide com o loader do RenderBox"
   |
   +---- 6ffa00a  "o RenderBox passa a despachar pela tabela do PROPRIO
   |               device, e o executavel enfim linka"      -> main (+83)
   |
   +---- 014c0ec  "o app empresta o device do Onyx"         -> alt (+1)
```

`git merge-base` dá `680e090` para as duas. São **duas respostas ao mesmo
problema de link**, escritas do mesmo commit, e a branch ficou parada enquanto a
outra seguiu por 83 commits. A branch não contradiz o `main` de hoje — ela
nunca viu o `main` de hoje. `main..alt` = 1 commit; `alt..main` = 83.

`[OBS]` Isto é o mesmo padrão que já mordeu este projeto uma vez (memória
15/09: *a main não é o projeto*). Aqui foi o inverso e igualmente caro: a
branch parada era a que estava desatualizada, e ninguém tinha olhado qual das
duas datas era qual.

---

## 3. O fato técnico

`[BIN]` **A tabela global existe e o Onyx a toma.** Medido no checkout do
OnyxSDK, `Source/Rendering/VkContext.cpp`:

| linha | chamada | efeito |
|---|---|---|
| 109 | `volkInitialize()` | sobe o loader |
| 234 | `volkLoadInstance(m_instance)` | globais de instance **e** os slots device-level com trampolins genéricos |
| 362 | `volkLoadDevice(m_device)` | **especializa os globais device-level no device do Onyx** |

E em `volk.c` (VOLK_HEADER_VERSION 296, o que o FetchContent traz):

```c
void volkLoadDevice(VkDevice device)
{
    loadedDevice = device;            // <- estado GLOBAL de processo
    volkGenLoadDevice(device, vkGetDeviceProcAddrStub);
}
```

Até aqui a branch está certa, palavra por palavra.

`[BIN]` **E existe a outra porta, dez linhas abaixo.** `volk.c:189`:

```c
void volkLoadDeviceTable(struct VolkDeviceTable* table, VkDevice device)
{
    volkGenLoadDeviceTable(table, device, vkGetDeviceProcAddrStub);
}
```

`volkGenLoadDeviceTable` (`volk.c:1303`) escreve **só** em `table->...`. Não há
uma única atribuição a símbolo global no corpo dela. O próprio cabeçalho diz
para que serve (`volk.h:129-133`):

> *Load function pointers using application-created VkDevice into a table.
> Application should use function pointers from that table instead of using
> global function pointers.*

Este é o ponto de virada que a missão pedia para procurar, e ele está do lado da
spec.

`[BIN]` **E o `main` já passa por essa porta.** `Source/RenderBox/VulkanApi.cpp`:

```cpp
void loadInstanceApi(VkInstance instance) { volkLoadInstanceOnly(instance); }
void loadDeviceApi(DeviceApi& table, VkDevice device) { volkLoadDeviceTable(&table, device); }
```

`volkLoadInstanceOnly` e não `volkLoadInstance` — a diferença importa e já
estava anotada no cabeçalho: o segundo também enche os slots device-level
globais, desfazendo a especialização do Onyx por trampolins genéricos. Mais
lento para o Onyx e inútil para nós.

`[ART]` **A auditoria de que não sobrou chamada global device-level.** Varrendo
`Source/RenderBox/*.{cpp,h}` por `vk[A-Z]...(` sem o prefixo `api().`, restam
**oito** sítios, e os oito são global- ou instance-level, que despacham pelo
handle que recebem e por isso são indiferentes a quem carregou:

`vkCreateInstance`, `vkDestroyInstance`, `vkCreateDevice`,
`vkEnumerateInstanceLayerProperties` (2), `vkEnumeratePhysicalDevices` (2),
`vkGetPhysicalDeviceProperties`, `vkGetPhysicalDeviceMemoryProperties`,
`vkGetPhysicalDeviceQueueFamilyProperties` (2).

Nenhuma chamada device-level fora da tabela. `vkQueueWaitIdle` aparece na
varredura de texto e é **um comentário** (`Device.cpp:258`), não uma chamada.

---

## 4. O veredito

**A spec estava certa. A branch estava certa sobre o mecanismo e errada sobre a
saída.** O terceiro caso da missão — *ambos certos sob condições diferentes* —
é o que de fato vale, e a condição não é uma flag de compilação: é uma
**disciplina de chamada**, e por isso foi escrita no código (§7).

O custo da alternativa também não era neutro, e ele está na própria mensagem de
`014c0ec`: o Onyx cria o device com `queueCount = 1`, então adotá-lo põe este
motor na mesma fila por onde o Onyx apresenta. `VkQueue` não é thread-safe e não
há como compartilhar um lock com o frame loop do Onyx de fora dele — a branch
teve de voltar o render para a thread principal (`SyncScheduler` no lugar do
`JobScheduler`), e um render lento passa a travar a UI enquanto dura. O `main`
não paga isso: o render continua numa lane do `JobQueue`, na fila do device
dele.

Havia ainda um terceiro preço na branch: `RenderBox_volk`, uma **segunda
variante** das mesmas fontes, para o `icrender` e o `ic_tests` continuarem
linkando a de sempre. O `main` resolve com um alvo só e uma
`$<TARGET_EXISTS:volk::volk>` (`Source/RenderBox/CMakeLists.txt:110`) —
expressão de gerador, não `if(TARGET ...)`, com o porquê escrito ali.

---

## 5. A medição, porque leitura não basta

Build `release`, `IC_BUILD_UI:BOOL=ON` conferido no `CMakeCache.txt`.

### 5.1. `[ART]` A janela real desenha

`iconcomposer.exe References/27.0-129/out/AppIcon-27.icon`, painel Diagnostics:

```
render    6 of 6 layer(s) drawn in 0.62 s
view      idiom Square, appearance Default; the document declares squares: shared
```

Seis camadas em três grupos, a pastilha e as três lajes de vidro na tela. Não é
"abriu sem erro": é a contagem do painel e a imagem.

### 5.2. `[ART]` O que as camadas de validação disseram: **nada**

`validation = true` posta temporariamente em `Window.cpp` só para esta medição,
e revertida em seguida — ela está desligada por custo por frame, não por estar
quebrada.

**O primeiro resultado foi zero byte em stdout e stderr, e zero byte não é
prova.** A armadilha 1 vale aqui inteira: um layer que não carregou é tão
silencioso quanto um layer que nada tem a dizer. Então o silêncio foi
instrumentado antes de ser lido:

1. `VkLayer_khronos_validation.json` confirmado em
   `HKLM:\SOFTWARE\Khronos\Vulkan\ExplicitLayers` (SDK 1.4.350.0).
2. `VK_LAYER_SETTINGS_PATH` apontada para um `vk_layer_settings.txt` com
   `debug_action = VK_DBG_LAYER_ACTION_LOG_MSG` e `log_filename` num arquivo.
3. O arquivo apareceu **com o banner de ativação dentro** — a prova positiva de
   que o layer estava vivo e escrevendo *naquele* arquivo:

```
Validation Information: [ WARNING-CreateInstance-status-message ]
vkCreateInstance(): Khronos Validation Layer Active:
    Current Enables: None.
    Current Disables: None.
Objects: 1
    [0] VkInstance 0x29392637a80
```

4. Com o layer assim provado, **sete renders** na janela (a abertura mais seis
   alternâncias de visibilidade de camada, cada uma um `RenderRequest` novo,
   cada uma criando e destruindo recursos no nosso device enquanto o Onyx
   apresentava o swapchain dele): **o log parou nas mesmas 6 linhas.**

Zero erro, zero aviso, zero mensagem de sincronização. `6 of 6 layer(s) drawn in
0.57 s` ao fim, e o app respondendo.

### 5.3. `[ART]` O mesmo pixel pelos dois caminhos — a evidência mais forte

O que a branch temia é corrupção **silenciosa**. Silêncio de validação é um
instrumento; pixel é outro. `AppIcon-27` a 412 px, `icrender`, Release:

| build | RenderBox despacha por | SHA-256 do PNG |
|---|---|---|
| `IC_BUILD_UI=ON` | `volkLoadDeviceTable` (tabela por device) | `9776C1F6…6BF89C9D` |
| `IC_BUILD_UI=OFF` | protótipos do loader, sem volk | `9776C1F6…6BF89C9D` |

**Byte a byte iguais.** Se a tabela por device estivesse levando alguma chamada
para o lugar errado, não sairia o mesmo arquivo.

### 5.4. `[ART]` E a tabela por device não custa tempo de render

| build | 3 execuções, 412 px |
|---|---|
| `IC_BUILD_UI=ON` (com volk) | 0,319 / 0,321 / 0,330 s |
| `IC_BUILD_UI=OFF` (sem volk) | 0,347 / 0,337 / 0,346 s |

A indireção a mais **não** aparece; o caminho com volk saiu até um pouco à
frente, o que só quer dizer que a diferença está no ruído da máquina. A
referência de `799411c` é **0,272 s** e as duas colunas estão ~20 % acima dela —
**a distância é da máquina desta noite, não do despacho**, e é exatamente isso
que a segunda linha da tabela mostra: ela é a *mesma configuração* da
referência e também subiu.

### 5.5. `[ART]` A suíte

| build | casos | falhas |
|---|---|---|
| `release`, `IC_BUILD_UI=ON` | **678** | **0** |
| `release`, `IC_BUILD_UI=OFF` | 652 | 0 |

678 é a linha de base de `799411c` e ela **não caiu**. Os 26 de diferença são os
casos do `IconComposerKit`, que não entram no build sem UI — anotado aqui porque
a contagem menor num build sem UI parece regressão e não é.

`IC_CORPUS_DIR=D:/CodingProjects/Icon-Composer/References/corpus` nas duas.

---

## 6. O que foi feito com a branch

`alt/device-adotado-do-onyx` foi **apagada**. O commit não foi perdido: está na
tag **`rejeitado/device-adotado-do-onyx`** (`014c0ec`), que o mantém alcançável
contra o gc. `git show rejeitado/device-adotado-do-onyx` lê o diff inteiro.

**Por que ela não entrou, em uma frase, para quem vier reabrir isto:** porque o
problema que ela resolve — o `volkLoadDevice` do Onyx mandando no despacho de
todo o processo — já estava resolvido em `6ffa00a` pela porta que o próprio volk
documenta para este caso, e a solução dela cobra por cima uma fila compartilhada
com o loop de apresentação do Onyx, o render de volta na thread da UI e uma
segunda variante do alvo RenderBox.

---

## 7. O que mudou no código

Um comentário, em um lugar, e é o ponto do exercício: a condição que torna a
decisão segura deixou de ser algo que só o `VulkanApi.h` sabia.
`Source/app/Window.cpp`, no sítio que cria o segundo device, passa a dizer que a
segurança **não está naquela linha**, onde ela de fato está, e qual escrita a
quebraria:

> Writing a bare `vkFoo(device, ...)` anywhere in RenderBox — rather than
> `api().vkFoo(...)` — compiles in both builds and re-opens the hole in the UI
> build alone, where nothing would report it.

Esse é o modo de falha que resta, e ele é a única coisa neste assunto que ainda
pode morder: o compilador não o pega, a suíte sem UI não o pega, e o build sem
volk não o pega. `[OBS]` Um teste que o pegasse seria uma varredura das fontes
do RenderBox por `vk[A-Z]\w*\(` não precedido de `api().`, com a lista de oito
exceções de §3 como gabarito — não foi escrito nesta rodada.

Zero linha de código de render tocada; os SHA-256 de §5.3 são o controle disso.

---

## 8. Os worktrees de longa vida

| worktree | estado encontrado | feito |
|---|---|---|
| `D:/CodingProjects/Icon-Composer-gate` | detached em `cc5aaa9`, **limpo**, e `cc5aaa9` contido no `main` | **removido** |
| `D:/CodingProjects/Icon-Composer-ui` | `feat/casca-e-modelo-editavel`, **0 commits à frente** do `main`, mas a árvore **suja** | **não removido** — ver abaixo |

O `-ui` tinha `M scripts/gate-m1.ps1` e um `gate-run-output.log` não rastreado.
A modificação foi lida antes de qualquer decisão, e ela **não é trabalho novo**:
comparada com o arquivo do `main`, as ~11 mutações que ela parecia acrescentar
(camada de edição, `values`, `bundle`) **já estão no `main`** — os dois arquivos
têm as mesmas 328 entradas. A única diferença real é ao contrário do que parece:

```
main tem, o worktree não:
    $out = & cmake --build $BuildDir --target ic_tests -- -j $BuildJobs
o worktree tem:
    $out = & cmake --build $BuildDir -- -j $BuildJobs
```

Ou seja, a cópia suja é um estado **anterior** ao `main`, e o que lhe falta é
justamente o `--target ic_tests` — a correção da armadilha 4, com as onze linhas
de comentário que explicam que sem ela toda mutação é reportada como não
apanhada depois de quatro horas de varredura. **Descartar aquela cópia não perde
nada e a manter não ganha nada**, mas a remoção exige `--force` sobre trabalho
não commitado e isso não é uma chamada desta frente: fica para a mão do dono.

Cópia de segurança dos dois arquivos em
`<scratchpad>/device/backup-worktree-ui/`. Quando for a hora:

```
git worktree remove --force D:/CodingProjects/Icon-Composer-ui
git branch -d feat/casca-e-modelo-editavel
```

`[OBS]` A branch `feat/casca-e-modelo-editavel` continua existindo porque o
worktree a ocupa; com 0 commits à frente do `main`, ela é `-d` (não `-D`) e sai
sem perda.

Os três `worktree-agent-*` sob `.claude/worktrees/` são das outras frentes em
curso e **não foram tocados**.
