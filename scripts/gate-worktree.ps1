<#
Runs `gate-m1.ps1` in a DEDICATED WORKTREE, so the sweep stops holding the main
tree hostage.

WHY THIS EXISTS
---------------
The sweep mutates sources one at a time: it writes a defect into a real file,
rebuilds, runs the suite, and restores. For the hour and a half that takes, the
tree is not safe to touch -- an edit made alongside it is either mistaken for a
mutation or restored over. So the sweep and every other kind of work are
mutually exclusive, and on 2026-09-03 that cost most of a day.

A worktree removes the exclusion. The sweep gets its own checkout and its own
build directory; the main tree stays free.

It also contains the damage. When a run is killed mid-sweep -- which has now
happened five times on this machine -- the mutated file is left on disk, because
a kill cannot run the cleanup. In a worktree that file is a throwaway checkout,
not the tree being worked in.

WHAT THE WORKTREE DOES NOT HAVE, AND WHY THE CORPUS IS PASSED IN
----------------------------------------------------------------
`References/` is almost entirely gitignored -- 4 files are versioned out of
79 MB. A worktree therefore has `corpus.json` and NOT the 145 bundles, so the
corpus is pointed at the main tree, where it is read and never written. The IR
dumps under `References/2.0-125/out/` are absent for the same reason; the gate
does not read them, only the reverse-engineering scripts do.

`gate-m1.ps1` needed no change to work here: it resolves its root from its own
location (`Split-Path -Parent $PSScriptRoot`) and already takes `-BuildDir` and
`-CorpusDir`.

SETUP, ONCE
    git worktree add --detach D:/CodingProjects/Icon-Composer-gate main
    cd D:/CodingProjects/Icon-Composer-gate ; cmake --preset mingw -DIC_BUILD_UI=OFF

`-DIC_BUILD_UI=OFF` E DELIBERADO. Desde 15/09/2026 a opcao nasce `ON` e arrasta o
OnyxSDK por `FetchContent` -- rede, e um SDK inteiro para compilar. O gate nao
toca no Kit nem no app: ele constroi `ic_tests`, que linka Foundation, CoreSVG,
CliLib e RenderBox e mais nada. Configurar com a UI ligada custa o download e a
arvore do Onyx para nao usar um arquivo dela.

USAGE
    powershell -File scripts/gate-worktree.ps1              # the gate
    powershell -File scripts/gate-worktree.ps1 -From 1 -To 60
#>
param(
    [int]$From = 0,
    [int]$To = 0,
    # Forwarded to `gate-m1.ps1`; see its own `-Files` for what it means.
    [string]$Files = "",
    [string]$Worktree = "D:/CodingProjects/Icon-Composer-gate",
    # The commit the sweep should test. The default is main's tip, because a
    # sweep against a stale checkout reports on code nobody has.
    [string]$Ref = "main"
)

# NOT "Stop": `git checkout` writes "HEAD is now at ..." to STDERR even when it
# succeeds, and under Stop PowerShell turns any native stderr line into a
# terminating error. The first version of this script died on a checkout that
# had exited 0. Exit codes are checked explicitly instead, which is what they
# are for.
$ErrorActionPreference = "Continue"
$main = Split-Path -Parent $PSScriptRoot

if (-not (Test-Path (Join-Path $Worktree "scripts/gate-m1.ps1"))) {
    Write-Host "FAILED: no worktree at $Worktree -- see SETUP in this file's header"
    exit 1
}

# A sweep that was killed leaves its marker and a mutated file behind. Say so
# before syncing, because `checkout --force` would erase the evidence silently
# and the next run would report on a tree nobody inspected.
$marker = Join-Path $Worktree "build/mingw/gate-backup/SWEEP-IN-PROGRESS"
if (Test-Path $marker) {
    Write-Host "note: a previous sweep in this worktree did not finish."
    Write-Host "      gate-m1 recovers from its own backup at startup; the sync below"
    Write-Host "      would also discard it. Nothing in a worktree is authored, so"
    Write-Host "      either is safe -- this line exists so it is not silent."
}

Write-Host "syncing $Worktree to $Ref"
& git -C $Worktree checkout --detach --force $Ref *> $null
if ($LASTEXITCODE -ne 0) { Write-Host "FAILED: could not sync the worktree"; exit 1 }
$at = (& git -C $Worktree rev-parse --short HEAD).Trim()
Write-Host "worktree at $at`n"

# Same cap as the sweep's own rebuild, and for the same reason: this is the
# WIDE build -- tudo o que a suite precisa, de uma vez.
#
# `--target ic_tests` pelo mesmo motivo que em `gate-m1.ps1`: desde 15/09/2026 a
# suite e `EXCLUDE_FROM_ALL` e `all` nao a contem mais. Sem o alvo, este
# aquecimento construiria tudo MENOS o executavel que o gate vai rodar. De
# quebra, nomear o alvo corta da conta o app e o Kit, que o gate nunca exercita.
& cmake --build (Join-Path $Worktree "build/mingw") --target ic_tests -- -j 4 2>&1 | Select-Object -Last 1
if ($LASTEXITCODE -ne 0) { Write-Host "FAILED: the worktree does not build"; exit 1 }

$corpus = Join-Path $main "References/corpus"
$gateArgs = @("-File", (Join-Path $Worktree "scripts/gate-m1.ps1"), "-CorpusDir", $corpus)
if ($From -gt 0) { $gateArgs += @("-From", $From) }
if ($To   -gt 0) { $gateArgs += @("-To",   $To) }
if ($Files -ne "") { $gateArgs += @("-Files", $Files) }

& powershell @gateArgs
exit $LASTEXITCODE
