# gate-m1.ps1 -- the acceptance test for the `.icon` reader, writer and model.
#
# The suite being green is not the gate. SF-Symbols' M1 differential stayed green
# with `kIntegerWidths` corrupted -- 99 of 99 unit tests passing, 36,975 of 36,975
# blocks identical -- and the only reason anyone found out is that the mutation
# step was mandatory there too. So this script is three things:
#
#   1. The suite, pristine, against 145 real `.icon` documents.
#   2. Two differentials. The WRITER: 135 documents must come back byte-identical
#      to what Apple's own `JSONEncoder` wrote; the other 10 had a formatter run
#      over them in their own repository, and idempotence proves their content
#      survives regardless. The MODEL: not one key in 145 documents may be
#      unknown to it, not one of the 1,740 specializations may be unreachable by
#      the resolver, and not one of the 61,537 values it resolves may fail to
#      decode into a type.
#   3. A mandatory mutation sweep. Twenty-four defects go in one at a time, and every
#      one of them MUST redden the suite. Each is applied to a pristine tree and
#      restored from a byte-exact backup verified by SHA-256 -- never by a git
#      command, because this repository has no git history to lean on, and a
#      mutation that silently fails to apply reports a green suite and turns the
#      gate into a lie.
[CmdletBinding()]
param(
    [string]$BuildDir = "build/mingw",
    [string]$CorpusDir = $(if ($env:IC_CORPUS_DIR) { $env:IC_CORPUS_DIR } else { "References/corpus" })
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$sources = @{
    json     = Join-Path $root "Source/IconComposerFoundation/Json.cpp"
    document = Join-Path $root "Source/IconComposerFoundation/IconDocument.cpp"
    coverage = Join-Path $root "Source/IconComposerFoundation/Coverage.cpp"
    values   = Join-Path $root "Source/IconComposerFoundation/Values.cpp"
    bundle   = Join-Path $root "Source/IconComposerFoundation/IconBundle.cpp"
    report   = Join-Path $root "Source/cli/Report.cpp"
}
$original = @{}
$hashes = @{}

# Put every file back and prove it, byte for byte.
#
# The timestamp is not a detail. `Copy-Item` PRESERVES the source file's
# LastWriteTime, so restoring from a backup taken before the sweep stamps the
# file OLDER than the object built from the mutated text -- ninja then reports
# "no work to do" and the final run exercises the mutated binary while the tree
# on disk is pristine. That is a gate that lies. Written through, and stamped
# now, on purpose.
function Restore-Sources {
    foreach ($k in $sources.Keys) {
        [IO.File]::WriteAllText($sources[$k], $original[$k])
        (Get-Item $sources[$k]).LastWriteTime = Get-Date
        $now = (Get-FileHash $sources[$k] -Algorithm SHA256).Hash
        if ($now -ne $hashes[$k]) {
            Write-Host "FAILED: $($sources[$k]) was not restored byte-exactly"
            exit 1
        }
    }
}

function Invoke-Build {
    $out = & cmake --build $BuildDir 2>&1
    return ($LASTEXITCODE -eq 0)
}

function Invoke-Suite {
    $env:IC_CORPUS_DIR = (Resolve-Path $CorpusDir).Path
    $out = & (Join-Path $BuildDir "Tests/ic_tests.exe") 2>&1
    return @{ ok = ($LASTEXITCODE -eq 0); text = ($out -join "`n") }
}

# Each mutation: the file it touches, a name, an anchor that MUST be present
# exactly once, and what replaces it. An anchor the script cannot find is a hard
# error -- a mutation that fails to apply would report a green suite.
$mutations = @(
    # ---- the JSON layer ----
    @{ file = "json"; name = "number decoded to a double instead of kept as a lexeme"
       from = 'return Value::number(std::string(t_.substr(start, i_ - start)));'
       to   = 'return Value::number(std::to_string(std::stod(std::string(t_.substr(start, i_ - start)))));' },
    @{ file = "json"; name = "the space around the colon dropped"
       from = 'out += "\" : ";'
       to   = 'out += "\":";' },
    @{ file = "json"; name = "keys no longer sorted"
       from = 'return a->first < b->first;'
       to   = 'return false;' },
    @{ file = "json"; name = "trailing garbage accepted"
       from = 'if (r.offset() != r.size()) return std::nullopt;'
       to   = 'if (false) return std::nullopt;' },
    @{ file = "json"; name = "array opened without its newline"
       from = 'out += "[\n";'
       to   = 'out += "[";' },
    @{ file = "json"; name = "null misspelled"
       from = 'out += "null";'
       to   = 'out += "nul";' },
    # ---- the model ----
    @{ file = "document"; name = "appearance vocabulary loses a case"
       from = 'if (s == "tinted") return Appearance::Tinted;'
       to   = 'if (false) return Appearance::Tinted;' },
    @{ file = "document"; name = "appearance predicate compared the wrong way"
       from = 'if (!want || *want != ctx.appearance) return false;'
       to   = 'if (!want || *want == ctx.appearance) return false;' },
    @{ file = "document"; name = "specificity ignored, first match wins"
       from = 'if (score > bestScore) {'
       to   = 'if (best == nullptr) {' },
    @{ file = "document"; name = "the plain property outranks its specializations"
       from = 'std::string listKey(property);'
       to   = 'if (owner.find(property)) return owner.find(property); std::string listKey(property);' },
    # ---- the coverage walk ----
    @{ file = "coverage"; name = "a nested shape accepts any key"
       from = 'if (!contains(keys, m.first)) out.push_back(join(path, m.first));'
       to   = 'if (false) out.push_back(join(path, m.first));' },
    @{ file = "coverage"; name = "layers checked against the document's key set"
       from = 'const Level child = (m.first == "groups") ? Level::Group : Level::Layer;'
       to   = 'const Level child = Level::Document;' },
    # ---- the typed values ----
    @{ file = "values"; name = "grey read as four components instead of two"
       from = '{"gray", ColorSpace::Gray, 2},'
       to   = '{"gray", ColorSpace::Gray, 4},' },
    @{ file = "values"; name = "a component accepted as a mere prefix"
       from = 'return r.ec == std::errc() && r.ptr == last;'
       to   = 'return r.ec == std::errc();' },
    @{ file = "values"; name = "blend mode loses a case"
       from = 'if (s == "screen") return BlendMode::Screen;'
       to   = 'if (false) return BlendMode::Screen;' },
    @{ file = "values"; name = "shadow kind spelled the Swift way, not the disk way"
       from = 'if (s == "layer-color") return ShadowKind::LayerColor;'
       to   = 'if (s == "layerColor") return ShadowKind::LayerColor;' },
    @{ file = "values"; name = "a linear gradient loses its orientation"
       from = 'f.orientation = Orientation{*start, *stop};'
       to   = '(void)start;' },
    # ---- the bundle ----
    @{ file = "bundle"; name = "images collected for the base context only"
       from = 'Appearance::Base, Appearance::Light, Appearance::Dark, Appearance::Tinted,'
       to   = 'Appearance::Base, Appearance::Base, Appearance::Base, Appearance::Base,' },
    @{ file = "bundle"; name = "an unreferenced asset never reported"
       from = 'if (std::find(referenced.begin(), referenced.end(), file) == referenced.end()) {'
       to   = 'if (false) {' },
    @{ file = "bundle"; name = "any folder with an icon.json taken for a bundle"
       from = 'if (!IconDocument::open(*parsed)) return std::nullopt;'
       to   = ';' },
    # ---- the report ----
    @{ file = "report"; name = "a shadow dumped as raw JSON"
       from = 'if (property == "shadow") {'
       to   = 'if (false) {' },
    @{ file = "report"; name = "a glass layer no longer marked"
       from = 'o << " [glass]";'
       to   = ';' },
    @{ file = "report"; name = "the tree ignores the context it was given"
       from = 'const json::Value* image = l.resolve("image-name", ctx);'
       to   = 'const json::Value* image = l.resolve("image-name", Context{});' },
    @{ file = "report"; name = "a dangling reference reported as fine"
       from = 'o << (absent ? "  MISSING  " : "  ok       ")'
       to   = 'o << (false ? "  MISSING  " : "  ok       ")' }
)

Write-Host "gate-m1: $($mutations.Count) mutations, corpus at $CorpusDir`n"

foreach ($k in $sources.Keys) {
    $original[$k] = [IO.File]::ReadAllText($sources[$k])
}

# ---- 1 and 2: the pristine run -------------------------------------------
# Ninja decides by timestamp, so the gate must not inherit one it did not set.
foreach ($k in $sources.Keys) { (Get-Item $sources[$k]).LastWriteTime = Get-Date }
if (-not (Invoke-Build)) { Write-Host "FAILED: the pristine tree does not build"; exit 1 }
$pristine = Invoke-Suite
Write-Host $pristine.text
if (-not $pristine.ok) { Write-Host "FAILED: the pristine suite is not green"; exit 1 }
if ($pristine.text -notmatch '(\d+) documents: (\d+) parsed, (\d+) lossless, (\d+) byte-exact') {
    Write-Host "FAILED: the writer gate did not report -- did it run?"; exit 1
}
$docs = [int]$Matches[1]; $exact = [int]$Matches[4]
if ($pristine.text -notmatch '(\d+) documents opened, (\d+) with no unknown key') {
    Write-Host "FAILED: the model gate did not report -- did it run?"; exit 1
}
$clean = [int]$Matches[2]
if ($pristine.text -notmatch '(\d+) specializations \((\d+) reachable\)') {
    Write-Host "FAILED: the reachability gate did not report -- did it run?"; exit 1
}
$specs = [int]$Matches[1]; $reach = [int]$Matches[2]
if ($pristine.text -notmatch '(\d+) values decoded, (\d+) failed') {
    Write-Host "FAILED: the value gate did not report -- did it run?"; exit 1
}
$vals = [int]$Matches[1]; $vbad = [int]$Matches[2]
if ($pristine.text -notmatch '(\d+) bundles, (\d+) trees rendered, (\d+) offender') {
    Write-Host "FAILED: the report gate did not report -- did it run?"; exit 1
}
$trees = [int]$Matches[2]; $tbad = [int]$Matches[3]

if ($docs -lt 145)  { Write-Host "FAILED: only $docs documents, expected at least 145"; exit 1 }
if ($exact -lt 135) { Write-Host "FAILED: only $exact byte-exact, expected at least 135"; exit 1 }
if ($clean -ne $docs) { Write-Host "FAILED: $clean of $docs documents fully understood"; exit 1 }
if ($reach -ne $specs) { Write-Host "FAILED: $reach of $specs specializations reachable"; exit 1 }
if ($vbad -ne 0) { Write-Host "FAILED: $vbad values failed to decode"; exit 1 }
if ($vals -lt 60000) { Write-Host "FAILED: only $vals values decoded, expected 60000+"; exit 1 }
if ($tbad -ne 0) { Write-Host "FAILED: $tbad tree(s) fell back to raw JSON"; exit 1 }
Write-Host "pristine: OK`n"

# ---- 3: the mutation sweep -----------------------------------------------
foreach ($k in $sources.Keys) { $hashes[$k] = (Get-FileHash $sources[$k] -Algorithm SHA256).Hash }
$caught = 0
$survivors = @()

try {
    foreach ($m in $mutations) {
        $path = $sources[$m.file]
        $text = $original[$m.file]
        $count = ([regex]::Matches($text, [regex]::Escape($m.from))).Count
        if ($count -ne 1) {
            Write-Host "FAILED: anchor for '$($m.name)' appears $count times in $($m.file), expected exactly 1"
            exit 1
        }
        [IO.File]::WriteAllText($path, $text.Replace($m.from, $m.to))
        (Get-Item $path).LastWriteTime = Get-Date

        if (-not (Invoke-Build)) {
            Write-Host ("  {0,-56} DOES NOT COMPILE" -f $m.name)
            $survivors += "$($m.name) [did not compile]"
        } else {
            $r = Invoke-Suite
            if ($r.ok) {
                Write-Host ("  {0,-56} SURVIVED" -f $m.name)
                $survivors += "$($m.name) [suite stayed green]"
            } else {
                $n = ([regex]::Matches($r.text, 'FAIL ')).Count
                Write-Host ("  {0,-56} caught ({1} assertion(s))" -f $m.name, $n)
                $caught++
            }
        }
        Restore-Sources
    }
} finally {
    Restore-Sources
}

# ---- the verdict ---------------------------------------------------------
if (-not (Invoke-Build)) { Write-Host "FAILED: the restored tree does not build"; exit 1 }
$final = Invoke-Suite
Write-Host ""
if (-not $final.ok) { Write-Host "FAILED: the suite is not green after the sweep"; exit 1 }

Write-Host "writer: $docs documents, $exact byte-exact against Apple's encoder"
Write-Host "model:  $clean of $docs fully understood, $reach of $specs specializations reachable"
Write-Host "values: $vals decoded, $vbad failed"
Write-Host "report: $trees trees rendered, $tbad fell back to raw JSON"
Write-Host "sweep:  $caught of $($mutations.Count) mutations caught"
if ($caught -ne $mutations.Count) {
    Write-Host "`nVERDICT: FAILED -- a defect the suite does not see is a defect that ships"
    foreach ($s in $survivors) { Write-Host "  survived: $s" }
    exit 1
}
Write-Host "`nVERDICT: gate-m1 passed"
exit 0
