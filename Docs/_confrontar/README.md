# _confrontar — material sem selo

**Nada nesta pasta é documentação deste projeto.** É o resultado de uma
investigação anterior, trazida aqui para ser *confrontada* contra o binário —
não para ser citada, não para virar base, e não para responder pergunta nenhuma
sozinha.

## De onde veio

Do repositório `JeanxPereira/Icon-Composer` como ele existia até 2026-08-31 —
52 commits, quatro branches, uma tentativa em Next/WebGPU, uma em ImGui/Vulkan e
uma em Tauri/wgpu. Estes arquivos são o `docs/re/icon-composer/` da branch
`tauri-wgpu-shell`, mais o `ui-spec` e o roadmap.

O repositório foi apagado. A história inteira está preservada em
`D:\BackupLibrary\git-bundles\Icon-Composer-legacy-webgpu-tauri-2026-08-31.bundle`
(sha256 `7f957e7d588e19cefc03378e6115b8951b83896aa4c6b67fff8f1662f6b62f04`,
`git bundle verify` diz *complete history*). Restaurar:

```powershell
git clone D:\BackupLibrary\git-bundles\Icon-Composer-legacy-webgpu-tauri-2026-08-31.bundle
```

## Por que não vale como fonte

Estas 1.622 linhas **não saíram do binário**. Saíram de conversa, e o próprio
arquivo de origem já dizia isso: quando foram escritas, foram marcadas como
*material A CONFRONTAR, não base*. Não têm endereço, não têm imagem, não têm
proveniência — sob a disciplina de `docs/metodo-re.md` do AquaKit isso é `[OBS]`
na melhor das hipóteses, e `[BIN]` ganha de `[OBS]` sempre.

O que já foi confrontado e **caiu**:

| o material dizia | o binário diz |
|---|---|
| as aparências são `dark` e `tinted` | são **quatro**: `base`, `light`, `dark`, `tinted` (doc 01 §6) |
| 17 modos de mescla | **dez** — o formato não sabe nomear os outros sete (doc 01 §6) |
| faltavam cinco das seis famílias de especialização | a lista fechada está em doc 01 §3 e §4 |
| o `automatic-gradient` e o espaço `gray` não apareciam | os dois existem, e `gray` tem **dois** componentes (doc 01 §7, §8) |

O `examples/ImHex.icon/icon.json` é a exceção útil: é um artefato **real**,
escrito pelo app, e foi o primeiro contra o qual o schema selado foi cruzado —
31 de 31 chaves explicadas (doc 01 §9). Hoje o corpus tem 145 documentos e ele
não é mais necessário; fica como o exemplo de onde a coisa começou.

## Como usar

Como lista de perguntas. Uma afirmação daqui vale uma investigação, nunca uma
citação. Quando uma delas for medida no binário, ela sai daqui e entra num doc
numerado com selo — e a linha correspondente aqui deve ser riscada.
