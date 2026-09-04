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
#   3. A mandatory mutation sweep. Ninety defects go in one at a time, and every
#      one of them MUST redden the suite WITH AN ASSERTION -- a mutation that
#      merely crashes the process is caught by accident and is reported as a
#      failure of the test, because a suite that dies hides every case after it.
#      Each is applied to a pristine tree and
#      restored from a byte-exact backup verified by SHA-256 -- never by a git
#      command, because this repository has no git history to lean on, and a
#      mutation that silently fails to apply reports a green suite and turns the
#      gate into a lie.
[CmdletBinding()]
param(
    # The sweep can be run in SLICES. It grew past an hour, and on this machine
    # a run that long does not reliably survive to the end -- three were cut off
    # mid-sweep, and twice that left a mutated file on disk for the recovery to
    # find. Slicing lets each invocation finish.
    #
    # A SLICE IS NOT A PASS, and the script will not let one be mistaken for
    # one: with either bound given, the verdict says PARTIAL and names the range,
    # never "gate-m1 passed". Only a run over every mutation can print that.
    [int]$From = 0,
    [int]$To = 0,
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
    svgpath  = Join-Path $root "Source/CoreSVG/Path.cpp"
    svgxml   = Join-Path $root "Source/CoreSVG/Xml.cpp"
    svgdoc   = Join-Path $root "Source/CoreSVG/Document.cpp"
    svgpaint = Join-Path $root "Source/CoreSVG/Paint.cpp"
    rbdevice = Join-Path $root "Source/RenderBox/Device.cpp"
    rbbuffer = Join-Path $root "Source/RenderBox/Buffer.cpp"
    rbpath   = Join-Path $root "Source/RenderBox/PathBuffer.cpp"
    rbglsl   = Join-Path $root "Source/RenderBox/shaders/PathVertex.glsl"
    rbprobe  = Join-Path $root "Source/RenderBox/shaders/path_probe.comp"
    rboracle = Join-Path $root "Source/RenderBox/PathVertexOracle.cpp"
    rbfrag   = Join-Path $root "Source/RenderBox/shaders/PathFragment.glsl"
    rbcov    = Join-Path $root "Source/RenderBox/PathCoverageOracle.cpp"
    rbvert   = Join-Path $root "Source/RenderBox/shaders/path_exterior.vert"
    rbpass   = Join-Path $root "Source/RenderBox/CoveragePass.cpp"
    rbres    = Join-Path $root "Source/RenderBox/shaders/PathResolve.glsl"
    rbcomp   = Join-Path $root "Source/RenderBox/shaders/PathComposite.glsl"
    rbsvg    = Join-Path $root "Source/RenderBox/SvgRenderer.cpp"
    png      = Join-Path $root "Source/IconComposerFoundation/Png.cpp"
    inflate  = Join-Path $root "Source/IconComposerFoundation/Inflate.cpp"
    icon     = Join-Path $root "Source/RenderBox/IconRenderer.cpp"
    grad     = Join-Path $root "Source/RenderBox/GradientOracle.cpp"
    gradglsl = Join-Path $root "Source/RenderBox/shaders/Gradient.glsl"
    autograd = Join-Path $root "Source/RenderBox/AutomaticGradient.cpp"
    rbsvg2   = Join-Path $root "Source/RenderBox/SvgRenderer.cpp"
    glassor  = Join-Path $root "Source/RenderBox/GlassOracle.cpp"
    glassgl  = Join-Path $root "Source/RenderBox/shaders/Glass.glsl"
    dispor   = Join-Path $root "Source/RenderBox/DisplacementOracle.cpp"
    dispgl   = Join-Path $root "Source/RenderBox/shaders/Displacement.glsl"
    field    = Join-Path $root "Source/RenderBox/DistanceField.cpp"
    fillres  = Join-Path $root "Source/RenderBox/FillResolve.cpp"
    sysfill  = Join-Path $root "Source/RenderBox/SystemFill.cpp"
    blend    = Join-Path $root "Source/RenderBox/BlendMode.cpp"
    blendfx  = Join-Path $root "Source/RenderBox/BlendFormula.cpp"
    strokegeo= Join-Path $root "Source/RenderBox/StrokeGeometry.cpp"
    fieldgl  = Join-Path $root "Source/RenderBox/shaders/DistanceField.glsl"
    mip      = Join-Path $root "Source/RenderBox/MipPyramid.cpp"
    mipcomp  = Join-Path $root "Source/RenderBox/shaders/mip_reduce.comp"
    glassbg  = Join-Path $root "Source/RenderBox/GlassBackground.cpp"
    bggl     = Join-Path $root "Source/RenderBox/shaders/GlassBackground.glsl"
    glassfg  = Join-Path $root "Source/RenderBox/GlassForeground.cpp"
    fggl     = Join-Path $root "Source/RenderBox/shaders/GlassForeground.glsl"
}
$original = @{}
$hashes = @{}

# THE BACKUP LIVES ON DISK, NOT ONLY IN MEMORY.
#
# This script edits real source files and puts them back. A `try/finally` covers
# an exception and a Ctrl+C; it does NOT cover the process being killed, and it
# does not cover the machine going down. On 2026-09-01 exactly that happened --
# a mutation made the suite ask for a hundred gigabytes, the machine rebooted
# mid-sweep, and `PathBuffer.cpp` was left MUTATED in the working tree with the
# pristine text gone with the process that held it.
#
# So the pristine text is written to disk before anything is touched, together
# with a marker. A run that finds the marker knows a previous run died and puts
# every file back before doing anything else.
$Backup = Join-Path $BuildDir "gate-backup"
$Marker = Join-Path $Backup "SWEEP-IN-PROGRESS"

function Save-Pristine {
    New-Item -ItemType Directory -Force -Path $Backup | Out-Null
    foreach ($k in $sources.Keys) {
        [IO.File]::WriteAllText((Join-Path $Backup "$k.bak"), $original[$k])
    }
    [IO.File]::WriteAllText($Marker, (Get-Date -Format o))
}

function Recover-FromCrashedRun {
    if (-not (Test-Path $Marker)) { return }
    Write-Host "A previous sweep did not finish -- restoring the tree from $Backup"
    foreach ($k in $sources.Keys) {
        $bak = Join-Path $Backup "$k.bak"
        if (-not (Test-Path $bak)) { continue }
        $want = [IO.File]::ReadAllText($bak)
        if ([IO.File]::ReadAllText($sources[$k]) -ne $want) {
            Write-Host "  restored: $($sources[$k])"
            [IO.File]::WriteAllText($sources[$k], $want)
            (Get-Item $sources[$k]).LastWriteTime = Get-Date
        }
    }
    Remove-Item $Marker -Force
}

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
       to   = 'o << (false ? "  MISSING  " : "  ok       ")' },
    # ---- the SVG reader ----
    @{ file = "svgpath"; name = "a smooth cubic always reflects, even after a line"
       from = '? Point{2 * current.x - lastControl.x, 2 * current.y - lastControl.y}'
       to   = '? Point{lastControl.x, lastControl.y}' },
    @{ file = "svgpath"; name = "a repeated moveto stays a moveto instead of a line"
       from = "command = 'L';  // a repeated moveto is a lineto"
       to   = "command = 'M';" },
    @{ file = "svgpath"; name = "a horizontal line forgets the current y"
       from = 'current = {base.x + x, current.y};'
       to   = 'current = {base.x + x, base.y};' },
    @{ file = "svgxml"; name = "a closing tag need not match what it closes"
       from = 'return closing == out.name;'
       to   = 'return true;' },
    @{ file = "svgxml"; name = "character data dropped, so style and title come back empty"
       from = 'if (i_ > textStart) {'
       to   = 'if (false) {' },
    @{ file = "svgdoc"; name = "transform list composed in the wrong order"
       from = 'result = t.then(result);'
       to   = 'result = result.then(t);' },
    @{ file = "svgdoc"; name = "what defs defines is never reported"
       from = 'unsupported.insert("defs:" + c.name);'
       to   = 'void(0);' },
    @{ file = "svgdoc"; name = "paint silently dropped instead of named"
       from = 'if (a.first == p) unsupported.insert("paint:" + a.first);'
       to   = 'if (false) unsupported.insert("paint:" + a.first);' },
    # ---- SVG paint ----
    @{ file = "svgpaint"; name = "three-digit hex scaled by 16 instead of 17"
       from = '(nibble(0) * 17)'
       to   = '(nibble(0) * 16)' },
    @{ file = "svgpaint"; name = "a display-p3 colour reported as sRGB"
       from = 'return colorPaint({n[0], n[1], n[2], n.size() == 4 ? n[3] : 1.0, true});'
       to   = 'return colorPaint({n[0], n[1], n[2], n.size() == 4 ? n[3] : 1.0, false});' },
    @{ file = "svgpaint"; name = "a url reference keeps its hash"
       from = 'p.reference = std::string(inner.substr(1));'
       to   = 'p.reference = std::string(inner);' },
    @{ file = "svgdoc"; name = "the presentation attribute outranks style"
       from = 'if (it != style.end()) return it->second;'
       to   = 'if (false) return it->second;' },
    @{ file = "svgdoc"; name = "paint stops inheriting down the tree"
       from = 'inherited = resolve(e, style, classDeclarations(e), inherited);'
       to   = 'inherited = resolve(e, style, classDeclarations(e), Inherited{});' },
    @{ file = "svgdoc"; name = "a rounded rect loses its corner radius"
       from = 'rx = std::min(rx, *w / 2);'
       to   = 'rx = 0;' },
    # ---- the stylesheet ----
    @{ file = "svgpaint"; name = "a class selector keeps its dot, so nothing matches"
       from = 'auto& slot = out[std::string(one.substr(1))];'
       to   = 'auto& slot = out[std::string(one)];' },
    @{ file = "svgdoc"; name = "the stylesheets are never collected"
       from = 'b.collectStyles(xml->root);'
       to   = 'void(0);' },
    @{ file = "svgdoc"; name = "a class rule never reaches the element"
       from = 'if (cls != fromClass.end()) return cls->second;'
       to   = 'if (false) return cls->second;' },
    # ---- RenderBox P0: the headless device ----
    #
    # These four are what makes "128 cases, 0 failures" mean the GPU was actually
    # driven. Without them a Device that never submits, a copy that never runs and
    # an allocation that ignores what was asked for all pass, because the suite
    # only ever looked at bytes it wrote itself.
    @{ file = "rbdevice"; name = "the memory type ignores the properties asked for"
       from = 'if ((mem.memoryTypes[i].propertyFlags & properties) == properties) return i;'
       to   = 'return i;' },
    @{ file = "rbdevice"; name = "the command buffer is never submitted to the queue"
       from = 'VkResult submitted = vkQueueSubmit(queue_, 1, &si, fence);'
       to   = 'VkResult submitted = VK_SUCCESS; (void)si;' },
    @{ file = "rbbuffer"; name = "device-local memory is reported as mappable"
       from = 'if ((properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0) {'
       to   = 'if (true) {' },
    @{ file = "rbbuffer"; name = "a zero-sized buffer is allocated instead of refused"
       from = 'if (size == 0) return std::unexpected(std::string("a buffer size of zero is not allocatable"));'
       to   = 'if (size == 0) size = 4;' },
    # ---- RenderBox P1: the path buffer, one per measured convention ----
    #
    # Each of the four is a convention read out of `path_edges_vertex`, and each
    # of these four breaks exactly one of them. A buffer that violates any is
    # still a well-formed array of 32-byte structs -- which is why nothing but a
    # test that knows the convention can tell.
    @{ file = "rbpath"; name = "convention 2: the prefix sum is inclusive, not exclusive"
       from = 's.count = running;                                          // convention 2
        running += options.subdivisions;'
       to   = 'running += options.subdivisions;
        s.count = running;                                          // convention 2' },
    @{ file = "rbpath"; name = "convention 3: the header does not carry the start point"
       from = 'setPoint(header.p3, path.segments.front().p[0]);'
       to   = 'void(0);' },
    @{ file = "rbpath"; name = "convention 4: a move is not marked as a subpath break"
       from = 'markSubpathBreak(s);'
       to   = 's.p1[0] = 0.0f;' },
    @{ file = "rbpath"; name = "a subpath break advances the prefix sum"
       from = 's.count = running;   // a break contributes no vertices, so the'
       to   = 's.count = ++running; // a break contributes no vertices, so the' },
    @{ file = "rbpath"; name = "convention 1: the header vertex total is written as a float"
       from = 'std::memcpy(&header.recip_n, &running, sizeof running);'
       to   = 'header.recip_n = static_cast<float>(running);' },
    # ---- RenderBox P2: the vertex stage, GPU against a CPU oracle ----
    #
    # These cut BOTH ways on purpose. Mutating the GLSL means the CPU oracle has
    # to notice; mutating the oracle means the GPU has to. A differential that
    # only ever catches one side is a differential with one working half.
    @{ file = "rbglsl"; name = "the bezier is evaluated by the textbook formula, not the target's"
       from = 'precise vec2 inner = vec2(u) * p1 + p2 * t;
    precise vec2 acc = vec2(u3) * p0 + vec2(k) * inner;'
       to   = 'precise vec2 inner = vec2(3.0 * u2 * t) * p1 + vec2(3.0 * u * t * t) * p2;
    precise vec2 acc = vec2(u3) * p0 + inner;' },
    @{ file = "rbglsl"; name = "the Y axis is not flipped on the way to clip space"
       from = 'precise float y = world.y * -twoOverSize.y + 1.0;'
       to   = 'precise float y = world.y * twoOverSize.y - 1.0;' },
    @{ file = "rbglsl"; name = "a vertex that must not be drawn stays inside the clip volume"
       from = 'const vec4 kClippedAway = vec4(-2.0, -2.0, 0.0, 1.0);'
       to   = 'const vec4 kClippedAway = vec4(0.0, 0.0, 0.0, 1.0);' },
    @{ file = "rbprobe"; name = "the binary search keeps the wrong half"
       from = 'bool goLeft = segments[mid].count > vertexIndex;'
       to   = 'bool goLeft = segments[mid].count <= vertexIndex;' },
    @{ file = "rbprobe"; name = "both ends of an edge get the same t"
       from = 'vec2 p = rbCubicAt(corner == 0u ? t0 : t1, p0, seg.p1, seg.p2, seg.p3);'
       to   = 'vec2 p = rbCubicAt(t0, p0, seg.p1, seg.p2, seg.p3);' },
    @{ file = "rboracle"; name = "the oracle reads p0 from the segment itself, not the one before"
       from = 'const Vec2 p0 = point(buffer.entries[static_cast<std::size_t>(base - 1)].p3);'
       to   = 'const Vec2 p0 = point(seg.p3);' },
    @{ file = "rboracle"; name = "the oracle packs the edges pass 32 to an instance, not 64"
       from = 'case PathPass::Edges: return 64;     // 32 edges, two ends each'
       to   = 'case PathPass::Edges: return 32;     // 32 edges, two ends each' },
    # ---- RenderBox P3: the interior and exterior passes ----
    @{ file = "rbprobe"; name = "the interior fan starts at a curve point, not at origin"
       from = 'out_.position = rbToClip(g.origin, g.m0, g.m1, g.m2, g.twoOverSize, g.depth);'
       to   = 'out_.position = rbToClip(vec2(0.0), g.m0, g.m1, g.m2, g.twoOverSize, g.depth);' },
    @{ file = "rbprobe"; name = "the interior third and fourth vertices are not the same point"
       from = 'vec2 p = (corner == 1u) ? pa : pb;'
       to   = 'vec2 p = (corner == 3u) ? pa : pb;' },
    @{ file = "rbprobe"; name = "a near-horizontal edge is kept instead of dropped"
       from = 'if (!(abs(d.y) > 1.0e-4)) {'
       to   = 'if (false) {' },
    @{ file = "rbprobe"; name = "the slope is dy/dx instead of dx/dy"
       from = 'precise float slope = d.x / d.y;              // dx/dy, not dy/dx'
       to   = 'precise float slope = d.y / d.x;              // dx/dy, not dy/dx' },
    @{ file = "rbprobe"; name = "path_y is not ordered by the edge direction"
       from = 'vec2 pathY = upward ? vec2(a.y, b.y) : vec2(b.y, a.y);'
       to   = 'vec2 pathY = vec2(a.y, b.y);' },
    @{ file = "rbprobe"; name = "the exterior quad stops at the edge instead of reaching urx"
       from = '                          : (g.urx + 0.5);'
       to   = '                          : (max(a.x, b.x) + 0.5);' },
    @{ file = "rbprobe"; name = "path_value loses its sign, so winding cannot cancel"
       from = 'out_.extra = vec4(upward ? 1.0 : -1.0, 0.0, 0.0, 0.0);'
       to   = 'out_.extra = vec4(1.0, 0.0, 0.0, 0.0);' },
    @{ file = "rboracle"; name = "the oracle takes the fan apex from the wrong corner"
       from = 'if (pass == PathPass::Interior && corner == 0u) {'
       to   = 'if (pass == PathPass::Interior && corner == 1u) {' },
    @{ file = "rboracle"; name = "the ULP comparison accepts any two floats"
       from = 'return ulpsApart(a.line[2], b.line[2]) <= maxUlps &&
           ulpsApart(a.line[3], b.line[3]) <= maxUlps;'
       to   = 'return true;' },
    # ---- the coverage fragments ----
    @{ file = "rbfrag"; name = "the interior fragment loses the facing sign"
       from = 'return vec2(0.0, frontFacing ? 1.0 : -1.0);'
       to   = 'return vec2(0.0, 1.0);' },
    @{ file = "rbfrag"; name = "the exterior area skips the trapezoid correction"
       from = '    if (dx != 0.0) {'
       to   = '    if (false) {' },
    @{ file = "rbfrag"; name = "the pixel is not truncated to its corner"
       from = 'vec2 pixel = vec2(ivec2(position.xy));'
       to   = 'vec2 pixel = position.xy;' },
    @{ file = "rbfrag"; name = "the edge y span does not clip the pixel's"
       from = 'precise float y0 = max(pixel.y, pathY.x);'
       to   = 'precise float y0 = pixel.y;' },
    @{ file = "rbfrag"; name = "a negative slope is not put back in order"
       from = 'precise vec2 ordered = (slope < 0.0) ? xs.yx : xs;'
       to   = 'precise vec2 ordered = xs;' },
    @{ file = "rbfrag"; name = "the winding sign no longer multiplies the area"
       from = 'return value * area;'
       to   = 'return area;' },
    @{ file = "rbfrag"; name = "the distance loses its half floor"
       from = 'float r = 1.0 - max(narrowed, 0.0015010833740234375);'
       to   = 'float r = 1.0 - narrowed;' },
    @{ file = "rbcov"; name = "the coverage tolerance accepts any two floats"
       from = 'return (gap < 0 ? -gap : gap) <= static_cast<float>(maxHalfUlps) * 0.00048828125f;'
       to   = 'return true;' },
    # ---- P5: the render ----
    @{ file = "rbvert"; name = "the clip Y keeps Metal's flip on Vulkan"
       from = 'gl_Position = vec4(x * g.twoOverSize.x + -1.0, y * g.twoOverSize.y - 1.0,'
       to   = 'gl_Position = vec4(x * g.twoOverSize.x + -1.0, y * -g.twoOverSize.y + 1.0,' },
    @{ file = "rbvert"; name = "the quad is split on the strip diagonal, not the ring's"
       from = 'const int kCorner[6] = int[6](0, 1, 2, 0, 2, 3);'
       to   = 'const int kCorner[6] = int[6](0, 1, 2, 1, 2, 3);' },
    @{ file = "rbvert"; name = "the quad stops at the edge instead of reaching urx"
       from = 'precise float x = (corner == 0u || corner == 3u) ? (min(a.x, b.x) - 0.5) : (g.urx + 0.5);'
       to   = 'precise float x = (corner == 0u || corner == 3u) ? (min(a.x, b.x) - 0.5) : (max(a.x, b.x) + 0.5);' },
    @{ file = "rbpass"; name = "the blend replaces instead of accumulating"
       from = 'blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;'
       to   = 'blend.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;' },
    @{ file = "rbpass"; name = "the draw trusts a header it did not check"
       from = 'if (!headerAgreesWithSegments(path)) {'
       to   = 'if (false) {' },
    # ---- the fill rule ----
    @{ file = "rbres"; name = "the even-odd bit is ignored, so every fill is non-zero"
       from = 'bool evenOdd = (state & 1024u) != 0u;'
       to   = 'bool evenOdd = false;' },
    @{ file = "rbres"; name = "non-zero keeps the sign instead of taking the magnitude"
       from = '        float a = abs(coverage);'
       to   = '        float a = coverage;' },
    @{ file = "rbres"; name = "even-odd tests the wrong parity"
       from = 'bool even = (uint(whole) & 1u) == 0u;'
       to   = 'bool even = (uint(whole) & 1u) != 0u;' },
    @{ file = "rbres"; name = "the shape curve ignores its own upper bound"
       from = 'if ((state & 2048u) != 0u && result >= kShapeEpsilon && result <= shape.x) {'
       to   = 'if ((state & 2048u) != 0u && result >= kShapeEpsilon) {' },
    @{ file = "rbres"; name = "the shape curve runs whether its bit is set or not"
       from = 'if ((state & 2048u) != 0u && result >= kShapeEpsilon && result <= shape.x) {'
       to   = 'if (result >= kShapeEpsilon && result <= shape.x) {' },
    @{ file = "rbres"; name = "mode 2's sub-mode selector is dropped"
       from = 'uint sub = (state >> 8) & 3u;'
       to   = 'uint sub = 1u;' },
    # ---- the flat composite ----
    @{ file = "rbcomp"; name = "the colour is not premultiplied by the shape"
       from = 'precise vec4 premultiplied = vec4(shape) * colour;'
       to   = 'precise vec4 premultiplied = colour;' },
    @{ file = "rbcomp"; name = "the invert bit is ignored"
       from = 'float reported = ((word3 & 2u) != 0u) ? (1.0 - alpha) : alpha;'
       to   = 'float reported = alpha;' },
    @{ file = "rbcomp"; name = "the alpha broadcast never happens"
       from = 'o.colour = ((word2 & 524288u) != 0u) ? vec4(premultiplied.a) : premultiplied;'
       to   = 'o.colour = premultiplied;' },
    @{ file = "rbcomp"; name = "the depth nudge tests the raw alpha, not the reported one"
       from = 'float d = (reported < kCompositeEpsilon) ? (depth + 1.0) : depth;'
       to   = 'float d = (alpha < kCompositeEpsilon) ? (depth + 1.0) : depth;' },
    @{ file = "rbcomp"; name = "a fragment that paints nothing is not pushed back"
       from = 'float d = (reported < kCompositeEpsilon) ? (depth + 1.0) : depth;'
       to   = 'float d = depth;' },
    # ---- the connector: an SVG drawn into pixels ----
    #
    # Every stage below already had its own mutations. These are for the JOINTS,
    # which is where a stage gate cannot see: the viewBox fit, the fill rule
    # reaching the render, the premultiply, and the reporting of what was not
    # drawn.
    @{ file = "rbsvg"; name = "the viewBox is stretched instead of fit uniformly"
       from = 'const double s = std::min(static_cast<double>(width) / w,'
       to   = 'const double s = std::max(static_cast<double>(width) / w,' },
    @{ file = "rbsvg"; name = "the viewBox fit forgets to centre"
       from = 'g.m2[0] = static_cast<float>((width - s * w) * 0.5 - s * box.x);'
       to   = 'g.m2[0] = static_cast<float>(-s * box.x);' },
    @{ file = "rbsvg"; name = "the viewBox origin is ignored"
       from = 'g.m2[1] = static_cast<float>((height - s * h) * 0.5 - s * box.y);'
       to   = 'g.m2[1] = static_cast<float>((height - s * h) * 0.5);' },
    @{ file = "rbsvg"; name = "an edge cannot reach the end of its row"
       from = 'g.urx = static_cast<float>(width);'
       to   = 'g.urx = 0.0f;' },
    @{ file = "rbsvg"; name = "the even-odd fill rule never reaches the render"
       from = 'return kPlainFill | (rule == icf::svg::FillRule::EvenOdd ? kEvenOdd : 0u);'
       to   = 'return kPlainFill;' },
    @{ file = "rbsvg"; name = "the colour is not premultiplied before compositing"
       from = 'for (int k = 0; k < 3; ++k) dst[k] = colour[k] * srcA + dst[k] * inv;'
       to   = 'for (int k = 0; k < 3; ++k) dst[k] = colour[k] + dst[k] * inv;' },
    @{ file = "rbsvg"; name = "the result is left premultiplied"
       from = 'out.rgba[t * 4 + k] = a > 0.0f ? acc[t * 4 + k] / a : 0.0f;'
       to   = 'out.rgba[t * 4 + k] = acc[t * 4 + k];' },
    @{ file = "rbsvg"; name = "a shape whose path buffer fails is dropped in silence"
       from = 'out.skipped.push_back({i, shape.element, buffer.error()});'
       to   = 'if (false) out.skipped.push_back({i, shape.element, buffer.error()});' },
    # ---- the PNG ----
    @{ file = "png"; name = "the stored block length is not complemented"
       from = 'z.push_back(static_cast<std::uint8_t>(~n));'
       to   = 'z.push_back(static_cast<std::uint8_t>(n));' },
    @{ file = "png"; name = "the zlib header is not a multiple of 31"
       from = 'z.push_back(0x01);  // FLG: no dictionary, fastest'
       to   = 'z.push_back(0x00);  // FLG: no dictionary, fastest' },
    @{ file = "png"; name = "the row filter byte is dropped"
       from = '        raw.push_back(0);
        for (std::uint32_t x = 0; x < width; ++x) {'
       to   = '        for (std::uint32_t x = 0; x < width; ++x) {' },
    @{ file = "png"; name = "a channel out of range wraps instead of clamping"
       from = 'if (v >= 1.0f) return 255;'
       to   = 'if (v >= 1.0f) return static_cast<std::uint8_t>(v * 255.0f);' },
    @{ file = "png"; name = "the colour type says RGB where the data is RGBA"
       from = 'ihdr.push_back(6);  // colour type: RGBA'
       to   = 'ihdr.push_back(2);  // colour type: RGBA' },
    # ---- inflate: the pixels of a PNG are behind it ----
    #
    # A wrong inflate rarely produces plausible output -- it produces the wrong
    # LENGTH, and IHDR fixes that length from a different chunk. Several of
    # these are caught by that arithmetic rather than by a pixel comparison.
    @{ file = "inflate"; name = "a back reference copies without overlapping"
       from = 'for (std::size_t k = 0; k < length; ++k) out.data.push_back(out.data[from + k]);'
       to   = 'out.data.insert(out.data.end(), out.data.begin() + static_cast<std::ptrdiff_t>(from), out.data.begin() + static_cast<std::ptrdiff_t>(from) + static_cast<std::ptrdiff_t>(length > distance ? distance : length));' },
    @{ file = "inflate"; name = "a stored block''s length complement is not checked"
       from = 'if (static_cast<std::uint16_t>(~len) != nlen) return false;'
       to   = 'if (false) return false;' },
    @{ file = "inflate"; name = "an over-subscribed Huffman table is accepted"
       from = 'if (left < 0) return false;'
       to   = 'if (false) return false;' },
    @{ file = "inflate"; name = "the Adler-32 is computed but never compared"
       from = 'if (((b << 16) | a) != want) {'
       to   = 'if (false) {' },
    @{ file = "inflate"; name = "the zlib header check value is not verified"
       from = 'if (((cmf << 8) | flg) % 31u != 0) {'
       to   = 'if (false) {' },
    @{ file = "inflate"; name = "the last length-code symbol is dropped from the table"
       from = 'for (int len = 1; len < 15; ++len) offsets[len + 1] = offsets[len] + counts_[len];'
       to   = 'for (int len = 1; len < 14; ++len) offsets[len + 1] = offsets[len] + counts_[len];' },
    @{ file = "inflate"; name = "a repeat of the previous code length starts at 2, not 3"
       from = 'repeat = 3 + e;'
       to   = 'repeat = 2 + e;' },
    # ---- the PNG decoder ----
    @{ file = "png"; name = "the average filter does not halve"
       from = 'case 3: v += (a + b) / 2; break;'
       to   = 'case 3: v += (a + b); break;' },
    @{ file = "png"; name = "Paeth breaks its tie the wrong way"
       from = 'return pb <= pc ? b : c;'
       to   = 'return pb < pc ? b : c;' },
    @{ file = "png"; name = "the Paeth predictor drops the diagonal neighbour"
       from = 'const int p = a + b - c;'
       to   = 'const int p = a + b;' },
    @{ file = "png"; name = "the up filter reads the current row instead of the one above"
       from = 'const int b = up ? up[x] : 0;'
       to   = 'const int b = x >= channels ? cur[x - channels] : 0;' },
    @{ file = "png"; name = "the decompressed length is not checked against IHDR"
       from = 'if (raw.data.size() != want) {'
       to   = 'if (false) {' },
    @{ file = "png"; name = "only the last IDAT chunk is kept"
       from = 'idat.insert(idat.end(), body, body + len);'
       to   = 'idat.assign(body, body + len);' },
    @{ file = "png"; name = "an interlaced PNG is decoded as though it were not"
       from = 'if (interlace != 0) {'
       to   = 'if (false) {' },
    @{ file = "png"; name = "a three-channel image comes back fully transparent"
       from = 'if (channels == 3) out.rgba[i * 4 + 3] = 1.0f;'
       to   = 'if (channels == 3) out.rgba[i * 4 + 3] = 0.0f;' },
    @{ file = "png"; name = "a truncated file is accepted because IEND is not required"
       from = 'if (!sawEnd) {'
       to   = 'if (false) {' },
    # ---- the compositor: a document walked, and its art placed ----
    #
    # The transform is where the document's numbers meet the canvas. Two of its
    # conventions are assumptions and one -- the sign of y -- is measured from
    # the corpus, so all three are pinned: an assumption that quietly changes is
    # worse than one that was never written down.
    @{ file = "icon"; name = "a group's scale does not reach the layer's translation"
       from = 'out.translateX = g.scale * l.translateX + g.translateX;'
       to   = 'out.translateX = l.translateX + g.translateX;' },
    @{ file = "icon"; name = "the group and layer scales are added, not composed"
       from = 'out.scale = g.scale * l.scale;'
       to   = 'out.scale = g.scale + l.scale - 1.0;' },
    @{ file = "icon"; name = "art is placed at the origin instead of centred"
       from = 'const double left = (kCanvasPoints - w * p.scale) * 0.5 + p.translateX;'
       to   = 'const double left = p.translateX;' },
    @{ file = "icon"; name = "the sign of the y translation is flipped"
       from = 'const double top = (kCanvasPoints - h * p.scale) * 0.5 + p.translateY;'
       to   = 'const double top = (kCanvasPoints - h * p.scale) * 0.5 - p.translateY;' },
    @{ file = "icon"; name = "the centring ignores the layer's scale"
       from = 'const double left = (kCanvasPoints - w * p.scale) * 0.5 + p.translateX;
    const double top = (kCanvasPoints - h * p.scale) * 0.5 + p.translateY;'
       to   = 'const double left = (kCanvasPoints - w) * 0.5 + p.translateX;
    const double top = (kCanvasPoints - h) * 0.5 + p.translateY;' },
    @{ file = "icon"; name = "the viewBox origin is not taken out of the placement"
       from = 'g.m2[0] = static_cast<float>(left * k - s * box.x);'
       to   = 'g.m2[0] = static_cast<float>(left * k);' },
    @{ file = "icon"; name = "the canvas is measured in pixels rather than points"
       from = 'const double k = static_cast<double>(size) / kCanvasPoints;
    const double s = p.scale * k;'
       to   = 'const double k = 1.0;
    const double s = p.scale * k;' },
    @{ file = "icon"; name = "a hidden layer is drawn anyway"
       from = 'if (boolOr(layer.resolve("hidden", options.context), false)) {'
       to   = 'if (false) {' },
    # This slot used to hold "a glass layer is drawn instead of being named",
    # anchored on the `skip(...)` that stood at IconRenderer.cpp:291 until the
    # glass was wired in. That branch is gone -- the layer is DRAWN now -- so the
    # mutation went stale and the pre-flight stopped the sweep before it started,
    # which is exactly the job it exists for.
    #
    # Its successor bites the same claim in the new shape: with `isGlass` forced
    # false a glass layer becomes an ordinary one, its group's material is never
    # applied and the backdrop is never refracted.
    @{ file = "icon"; name = "a glass layer is treated as an ordinary layer"
       from = 'const bool isGlass = boolOr(layer.resolve("glass", options.context), false);'
       to   = 'const bool isGlass = false;' },
    # And the raster gap must stay NAMED rather than silently drawn: with the
    # identity check dropped, a glass layer whose refraction is a no-op still
    # walks the whole displacement path.
    @{ file = "icon"; name = "the zero-refraction short circuit is removed"
       from = 'if (isGlass && !glassRefractionIsIdentity(refraction)) {'
       to   = 'if (isGlass) {' },
    @{ file = "icon"; name = "a blend this renderer does not have is drawn as normal"
       from = 'if (*s != "normal") {'
       to   = 'if (false) {' },
    @{ file = "icon"; name = "the layer opacity never reaches the composite"
       from = 'over(acc, drew->rgba, static_cast<float>(opacity));'
       to   = 'over(acc, drew->rgba, 1.0f);' },
    @{ file = "icon"; name = "the over operator does not hold back the destination"
       from = 'for (int k = 0; k < 3; ++k) acc[i + k] = src[i + k] * a + acc[i + k] * inv;'
       to   = 'for (int k = 0; k < 3; ++k) acc[i + k] = src[i + k] * a + acc[i + k];' },
    @{ file = "icon"; name = "a raster is sampled without premultiplying, so edges bleed"
       from = 'acc[c] += static_cast<float>(wgt) * img.rgba[s + c] * sa;'
       to   = 'acc[c] += static_cast<float>(wgt) * img.rgba[s + c];' },
    @{ file = "icon"; name = "a dangling asset is drawn as though it resolved"
       from = 'if (!std::filesystem::is_regular_file(art)) {'
       to   = 'if (false) {' },
    # ---- the gradient ----
    #
    # The 4-bit field is FOUR GEOMETRIES BY FOUR SPREADS, and that decomposition
    # is the measurement (doc 03 section 23.1). One wrong cell still draws a
    # gradient, so the cells are what these aim at.
    @{ file = "grad"; name = "a geometry cell moves: case 7 is not linear"
       from = 'case 0: case 1: case 2: case 7:   return Geometry::Linear;'
       to   = 'case 0: case 1: case 2:           return Geometry::Linear;' },
    @{ file = "grad"; name = "a spread cell moves: case 13 is not pad"
       from = 'case 0: case 3: case 9: case 13:  return Spread::Pad;'
       to   = 'case 0: case 3: case 9:           return Spread::Pad;' },
    @{ file = "grad"; name = "the selector reads more than four bits"
       from = 'return (state >> kGradientKindShift) & kGradientKindMask;'
       to   = 'return (state >> kGradientKindShift) & 31u;' },
    @{ file = "grad"; name = "reflect is a sawtooth, not a triangle"
       from = 'return doubled > 1.0f ? 2.0f - doubled : doubled;'
       to   = 'return doubled;' },
    @{ file = "grad"; name = "pad does not clamp"
       from = 'case Spread::Pad:
            return saturate(t);'
       to   = 'case Spread::Pad:
            return t;' },
    @{ file = "grad"; name = "the radial geometry drops its offset"
       from = 'return std::sqrt(px * px + py * py) * a + b;'
       to   = 'return std::sqrt(px * px + py * py) * a;' },
    @{ file = "grad"; name = "an untranscribed geometry claims to be covered"
       from = 'if (covered) *covered = false;
            return 0.0f;'
       to   = 'return 0.0f;' },
    @{ file = "grad"; name = "the two-colour gamma is applied unsaturated"
       from = 'if ((state & kStopGamma) != 0u) u = saturate(std::pow(saturate(t), gamma));'
       to   = 'if ((state & kStopGamma) != 0u) u = std::pow(t, gamma);' },
    @{ file = "grad"; name = "the segment parameter is not saturated, so the ends extrapolate"
       from = 'float f = span > 0.0f ? (t - lo.location) / span : 1.0f;
    f = saturate(f);'
       to   = 'float f = span > 0.0f ? (t - lo.location) / span : 1.0f;' },
    @{ file = "grad"; name = "the stop segment ignores where the stops are"
       from = 'float f = span > 0.0f ? (t - lo.location) / span : 1.0f;'
       to   = 'float f = t;' },
    @{ file = "gradglsl"; name = "the shader and the oracle disagree on a geometry cell"
       from = 'if (k == 3u || k == 4u || k == 5u || k == 8u) return 1;'
       to   = 'if (k == 3u || k == 4u || k == 5u) return 1;' },
    @{ file = "gradglsl"; name = "the shader mixes with the wrong factoring"
       from = 'precise vec4 span = hi - lo;
    precise vec4 result = lo + span * u;'
       to   = 'precise vec4 span = hi - lo;
    precise vec4 result = lo * (1.0 - u) + hi * u;' },
    # ---- the automatic gradient ----
    @{ file = "autograd"; name = "the luminance is a plain average, not Rec.709"
       from = 'return 0.2126 * r + 0.7152 * g + 0.0722 * b;'
       to   = 'return (r + g + b) / 3.0;' },
    @{ file = "autograd"; name = "a band boundary moves"
       from = 'if (L <= 0.25) {'
       to   = 'if (L <= 0.35) {' },
    @{ file = "autograd"; name = "the band comparison stops being inclusive"
       from = 'if (L <= 0.25) {
        lightening = p.dimLightening;
    } else if (L <= 0.50) {'
       to   = 'if (L < 0.25) {
        lightening = p.dimLightening;
    } else if (L <= 0.50) {' },
    @{ file = "autograd"; name = "the saturation boost pushes toward the luminance"
       from = 'double boosted[3] = {r - sb * (L - r), g - sb * (L - g), b - sb * (L - b)};'
       to   = 'double boosted[3] = {r + sb * (L - r), g + sb * (L - g), b + sb * (L - b)};' },
    @{ file = "autograd"; name = "the lightening always blends toward white"
       from = 'const double toward = positive ? (1.0 - boosted[i]) : boosted[i];'
       to   = 'const double toward = 1.0 - boosted[i];' },
    @{ file = "autograd"; name = "the derived colour is not clamped"
       from = 'shifted[i] = clamp01(boosted[i] + lightening * toward);'
       to   = 'shifted[i] = boosted[i] + lightening * toward;' },
    @{ file = "autograd"; name = "the two ends do not flip with the sign"
       from = 'a.location = positive ? 0.0 : 1.0;'
       to   = 'a.location = 0.0;' },
    @{ file = "autograd"; name = "basePosition moves the wrong stop"
       from = 'c.location = positive ? (1.0 - p.basePosition) : (0.0 + p.basePosition);'
       to   = 'c.location = positive ? 1.0 : 0.0;' },
    @{ file = "autograd"; name = "the alpha is lightened along with the colour"
       from = 'a.rgba[3] = alpha;'
       to   = 'a.rgba[3] = clamp01(alpha + lightening);' },
    # ---- the gradient wired into the renderer ----
    @{ file = "rbsvg2"; name = "objectBoundingBox is treated as user space"
       from = 'if (!g.userSpace) {'
       to   = 'if (false) {' },
    @{ file = "rbsvg2"; name = "the linear axis is not normalised by its own length"
       from = 'raw[0] = dx / len2;
        raw[1] = dy / len2;'
       to   = 'raw[0] = dx;
        raw[1] = dy;' },
    @{ file = "rbsvg2"; name = "the linear axis forgets where it starts"
       from = 'raw[2] = -(g.x1 * dx + g.y1 * dy) / len2;'
       to   = 'raw[2] = 0.0;' },
    @{ file = "rbsvg2"; name = "the radial gradient forgets its centre"
       from = 'raw[2] = -g.cx / g.radius;'
       to   = 'raw[2] = 0.0;' },
    @{ file = "rbsvg2"; name = "the gradient is sampled at the pixel corner, not its centre"
       from = 'const double px = static_cast<double>(t % options.width) + 0.5;
                const double py = static_cast<double>(t / options.width) + 0.5;
                const double ux = (px - globals.m2[0]) * sx;'
       to   = 'const double px = static_cast<double>(t % options.width);
                const double py = static_cast<double>(t / options.width);
                const double ux = (px - globals.m2[0]) * sx;' },
    @{ file = "rbsvg2"; name = "a reference that does not resolve is drawn anyway"
       from = 'if (!ramp.ok) {'
       to   = 'if (false) {' },
    # ---- the glass tower: the shared math ------------------------------------
    #
    # Eight stages landed with their own differentials and none of them had a
    # mutation here, which means the sweep was proving the OLD tower and calling
    # it the tower. Each of the forty-eight below aims at a CLAIM one of the eight
    # makes -- a constant read out of the IR, a guard, an ordering, a tap table
    # -- and not at arithmetic chosen for being easy to break. The plausible
    # wrong answer is used wherever there is one: AquaKit's epsilon rather than a
    # random number, float(1/3) rather than a random third, the other Rec. 709
    # rounding rather than a random triple. A transcriber makes THOSE mistakes.
    @{ file = "glassor"; name = "the dispersion weight is indexed instead of accumulated"
       from = 'for (int i = 0; i < index; ++i) w = w + kAberrationStepDown;'
       to   = 'w = 1.0f - static_cast<float>(index) / 3.0f;' },
    @{ file = "glassor"; name = "the second dispersion loop does not negate its offset"
       from = 't.offset = -w;                    // %665: uv - step * w'
       to   = 't.offset = w;                     // %665: uv - step * w' },
    @{ file = "glassor"; name = "the compression measures with the speculars rec709 triple"
       from = 'const float y = dot3(kLumaCompress, rgb);'
       to   = 'const float y = dot3(kLumaWeight, rgb);' },
    @{ file = "glassor"; name = "the band profile adds its offset instead of subtracting it"
       from = 'const float x = saturate((-d - offset) * invHeight);'
       to   = 'const float x = saturate((-d + offset) * invHeight);' },
    @{ file = "glassor"; name = "the ycc composite ignores the fills own alpha"
       from = 'const float k = 1.0f - fillPremultiplied[3];'
       to   = 'const float k = 1.0f;' },
    @{ file = "glassor"; name = "the face matrix mixes from its output, not from the input"
       from = 'for (int i = 0; i < 3; ++i) out[i] = rgb[i] + (f[i] - rgb[i]) * faceOpacity;'
       to   = 'for (int i = 0; i < 3; ++i) out[i] = f[i] + (rgb[i] - f[i]) * faceOpacity;' },
    @{ file = "glassgl"; name = "the shader carries AquaKits epsilon instead of the half 0xH1419"
       from = 'const float kRbGlassEpsilon = 0.00100040435791015625;  // 0xH1419'
       to   = 'const float kRbGlassEpsilon = 1.0e-4;  // 0xH1419' },
    @{ file = "glassgl"; name = "the green dispersion scale is float(1/3), not the half"
       from = 'const vec3 kRbAberrationRgbScale = vec3(0.5, 0.333251953125, 0.5);    // green is the HALF 1/3'
       to   = 'const vec3 kRbAberrationRgbScale = vec3(0.5, 1.0 / 3.0, 0.5);    // green is the HALF 1/3' },
    @{ file = "glassgl"; name = "the band profile does not clamp its input"
       from = 'precise float x = clamp(raw, 0.0, 1.0);'
       to   = 'precise float x = raw;' },
    # ---- the glass tower: the displacement ----------------------------------
    @{ file = "dispor"; name = "an eight-tap jitter offset is permuted"
       from = '{0.3125f, 0.0625f},   {-0.1875f, -0.3125f},'
       to   = '{0.0625f, 0.3125f},   {-0.1875f, -0.3125f},' },
    @{ file = "dispor"; name = "the four-tap variant carries the eight-taps final scale"
       from = 'case 2: return 0.25f;   // 0xH3400'
       to   = 'case 2: return 0.125f;  // 0xH3400' },
    @{ file = "dispor"; name = "the displacement moves the unjittered point"
       from = 'displacementLayerUV(params.source, offset[0] + q[0], offset[1] + q[1], uvSource);'
       to   = 'displacementLayerUV(params.source, offset[0] + px, offset[1] + py, uvSource);' },
    # A ONE-LINE ANCHOR EVEN WHERE THE CODE IS TWO, and it is not a preference:
    # five of the files below are stored with CRLF while this script is stored
    # with LF, so a multi-line anchor typed here never matches them. The
    # pre-flight catches that in milliseconds -- it caught exactly this, six
    # times, before a single build was paid for -- but the fix belongs in the
    # anchor rather than in the guard.
    @{ file = "dispor"; name = "the layer transform nests the x term inside the y one"
       from = 'const float t = fma1(x, layer.m[0][k], inner);'
       to   = 'const float t = fma1(y, layer.m[1][k], fma1(x, layer.m[0][k], layer.m[2][k])); (void)inner;' },
    @{ file = "dispgl"; name = "the per-tap z weight is hoisted to tap zeros"
       from = 'precise vec4 weighted = t == 0 ? vec4(disp.z) * colour : fma(colour, vec4(disp.z), acc);'
       to   = 'vec2 q0 = rbDispJitter(p, ddx, ddy, rbDispTapOffset(variant, 0).x, rbDispTapOffset(variant, 0).y);
        float w0 = rbDispSampleMap(rbDispLayerUV(map, q0)).z;
        precise vec4 weighted = t == 0 ? vec4(w0) * colour : fma(colour, vec4(w0), acc);' },
    @{ file = "dispgl"; name = "the decode bias is -2s, so one half is no longer neutral"
       from = 'precise vec2 result = fma(disp, vec2(s * 2.0), vec2(-s));'
       to   = 'precise vec2 result = fma(disp, vec2(s * 2.0), vec2(-s * 2.0));' },
    @{ file = "dispgl"; name = "variant zero reads the derivatives after all"
       from = 'vec2 ddx = taps == 1 ? vec2(0.0) : dpdx;'
       to   = 'vec2 ddx = dpdx;' },
    # ---- the glass tower: the distance field ---------------------------------
    @{ file = "fieldgl"; name = "the normalize runs on a flat neighbourhood too"
       from = 'if (any(notEqual(delta, vec2(0.0)))) {'
       to   = 'if (true) {' },
    @{ file = "fieldgl"; name = "a zero distance reports full coverage in .w"
       from = 'return vec4(0.0, u1, u1, 0.0);'
       to   = 'return vec4(0.0, u1, u1, 1.0);' },
    @{ file = "fieldgl"; name = "the x taps do not straddle p when dpdx is negative"
       from = 'vec2 hx = vec2(abs(dpdx), 0.0);'
       to   = 'vec2 hx = vec2(dpdx, 0.0);' },
    @{ file = "field"; name = "the oracles zero branch reports coverage instead of none"
       from = 'out[1] = u1;
        out[2] = u1;
        out[3] = 0.0f;'
       to   = 'out[1] = u1;
        out[2] = u1;
        out[3] = 1.0f;' },
    @{ file = "field"; name = "the central difference is taken in float, not in half"
       from = 'float g[2] = {fieldNarrowToHalf(right - left), fieldNarrowToHalf(up - down)};'
       to   = 'float g[2] = {right - left, up - down};' },
    @{ file = "field"; name = "the coverage band loses its half-pixel offset"
       from = 'const double cov = -out.distance / w + 0.5;'
       to   = 'const double cov = -out.distance / w;' },
    @{ file = "field"; name = "the generator fills every pixel with the even-odd rule"
       from = 'const bool inside = options.rule == FieldRule::NonZero ? winding != 0 : parity;'
       to   = 'const bool inside = parity;' },
    # ---- the glass tower: the mip pyramid ------------------------------------
    @{ file = "mip"; name = "the backdrop is un-premultiplied by an unfloored alpha"
       from = 'const float a = std::max(rgba[3], kBackdropRadiusFloor);'
       to   = 'const float a = rgba[3];' },
    @{ file = "mip"; name = "the lod cap is applied as a floor"
       from = 'return std::min(logged - bias, cap);'
       to   = 'return std::max(logged - bias, cap);' },
    @{ file = "mip"; name = "an odd extent is reduced by the two-tap box"
       from = 'if ((srcExtent & 1u) == 0u) {
        offset[0] = 2 * i;'
       to   = 'if (true) {
        offset[0] = 2 * i;' },
    @{ file = "mip"; name = "the three-tap weights lose their ramp across the row"
       from = 'weight[0] = (n - static_cast<float>(i)) / total;'
       to   = 'weight[0] = n / total;' },
    @{ file = "mip"; name = "the half rounding drops its tie to even"
       from = 'if (dropped > 0x1000u || (dropped == 0x1000u && (kept & 1u))) {'
       to   = 'if (dropped > 0x1000u) {' },
    @{ file = "mip"; name = "an odd extent rounds its next level up"
       from = 'std::uint32_t nextMipExtent(std::uint32_t extent) { return extent > 1 ? extent / 2 : 1; }'
       to   = 'std::uint32_t nextMipExtent(std::uint32_t extent) { return extent > 1 ? (extent + 1) / 2 : 1; }' },
    @{ file = "mipcomp"; name = "the radius floor is AquaKits epsilon, not the half 0xH1419"
       from = 'const float kBackdropRadiusFloor = 1.00040435791015625e-3;'
       to   = 'const float kBackdropRadiusFloor = 1.0e-4;' },
    @{ file = "mipcomp"; name = "the reduction sums the taps instead of their differences"
       from = 'acc += (wx[i] * wy[j]) * (s - ref);
        }
    }
    precise vec4 result = ref + acc;'
       to   = 'acc += (wx[i] * wy[j]) * s;
        }
    }
    precise vec4 result = acc;' },
    # ---- the glass tower: the background pass --------------------------------
    @{ file = "glassbg"; name = "the blur fill darkens with the max instead of the min"
       from = 'const float mn = std::fmin(face[i], backdrop[i]);   // %749'
       to   = 'const float mn = std::fmax(face[i], backdrop[i]);   // %749' },
    @{ file = "glassbg"; name = "the composite interpolates from the face, not from the shadow"
       from = 'out[i] = shadowPre[i] + (facePre[i] - shadowPre[i]) * maskValue;  // %1082'
       to   = 'out[i] = facePre[i] + (shadowPre[i] - facePre[i]) * maskValue;  // %1082' },
    @{ file = "glassbg"; name = "the specular weighs the face with the compressions triple"
       from = 'const float y = dot3(faceRgb, glass::kLumaWeight);'
       to   = 'const float y = dot3(faceRgb, glass::kLumaCompress);' },
    @{ file = "glassbg"; name = "the short circuit ignores the highlight and the ring shadow"
       from = 'highlight < glass::kEpsilon && ringShadow < glass::kEpsilon;'
       to   = 'true;' },
    @{ file = "glassbg"; name = "the aberration step is not swapped into its result"
       from = 'out[0] = pr1 * height;                                  // %604, %607'
       to   = 'out[0] = pr0 * height;                                  // %604, %607' },
    @{ file = "glassbg"; name = "the ring shadow re-samples with an x offset too"
       from = 'out[0] = p[0] - 0.0f;'
       to   = 'out[0] = p[0] - offsetY;' },
    @{ file = "glassbg"; name = "the ring shadow slot is handed the highlight"
       from = 'applyRingShadow(col, ringShadowValue, withRing);'
       to   = 'applyRingShadow(col, highlight, withRing);' },
    # `p.z + (p.x + p.y)` and `(p.x + p.y) + p.z` are the SAME float -- addition
    # commutes even where it does not associate -- so the mutation that bites is
    # the other association, not the other operand order.
    @{ file = "glassbg"; name = "the blur ramp associates its three bands the other way"
       from = 'const float sum = prod[2] + s01;'
       to   = 'const float sum = prod[0] + (prod[1] + prod[2]); (void)s01;' },
    @{ file = "bggl"; name = "the ring shadow reaches the colour as well as the alpha"
       from = 'precise vec4 r = col * vec4(1.0 - ringShadow) + vec4(0.0, 0.0, 0.0, ringShadow);'
       to   = 'precise vec4 r = col * vec4(1.0 - ringShadow) + vec4(ringShadow);' },
    @{ file = "bggl"; name = "the shadow colour is biased before it is scaled"
       from = 'precise vec3 rgb = scaled + bias;                          // %444'
       to   = 'precise vec3 rgb = vec3(vibrancy) * (vec3(f0, f1, f2) + bias);  // %444' },
    # ---- the glass tower: the foreground pass --------------------------------
    @{ file = "glassfg"; name = "the aberration lobe rotates where it should reflect"
       from = 'const float v[2] = {dot2(g, rowY), dot2(g, rowX)};'
       to   = 'const float v[2] = {dot2(g, rowX), dot2(g, rowY)};' },
    @{ file = "glassfg"; name = "the alpha sum adds the floored divisor"
       from = 'alphaSum += c[3];'
       to   = 'alphaSum += a;' },
    @{ file = "glassfg"; name = "the edge fade reaches the colour but not the alpha"
       from = 'for (int k = 0; k < 4; ++k) out[k] = fade * rgba[k];'
       to   = 'for (int k = 0; k < 3; ++k) out[k] = fade * rgba[k];
    out[3] = rgba[3];' },
    @{ file = "glassfg"; name = "the coverage never reaches the resolve"
       from = 'const float alpha = (edge * coverage) * sevenths;              // %259, %266'
       to   = 'const float alpha = edge * sevenths;                           // %259, %266' },
    @{ file = "glassfg"; name = "the gradient reads the fields xy instead of its yz"
       from = 'foregroundGradient(u, f[1], f[2], g);'
       to   = 'foregroundGradient(u, f[0], f[1], g);' },
    @{ file = "fggl"; name = "the tap divisor is floored with the fwidth epsilon"
       from = 'precise float a = max(c.w, kRbGlassEpsilon);'
       to   = 'precise float a = max(c.w, kRbFgFwidthFloor);' },
    @{ file = "fggl"; name = "the coverage cutoff compares against the fwidth floor"
       from = 'if (f.w < kRbGlassEpsilon) return vec4(0.0);'
       to   = 'if (f.w < kRbFgFwidthFloor) return vec4(0.0);' }

    # ---- the fill resolution: the seven tags and the TWO converters -------
    #
    # Every anchor below is a SINGLE line, checked to occur exactly once. Both
    # `FillResolve.cpp` and this script were verified for line endings first:
    # the source is CRLF and this script is LF, so a multi-line anchor could
    # never match -- the trap that stopped a sweep in milliseconds once already.
    @{ file = "fillres"; name = "the light rendition maps to the dark appearance"
       from = 'case Rendition::LightColor: return icf::Appearance::Light;'
       to   = 'case Rendition::LightColor: return icf::Appearance::Dark;' },
    @{ file = "fillres"; name = "the dark rendition maps to the light appearance"
       from = 'case Rendition::DarkColor:  return icf::Appearance::Dark;'
       to   = 'case Rendition::DarkColor:  return icf::Appearance::Light;' },
    @{ file = "fillres"; name = "a tint rendition stops collapsing onto tinted"
       from = 'case Rendition::LightTint:  return icf::Appearance::Tinted;'
       to   = 'case Rendition::LightTint:  return icf::Appearance::Light;' },
    @{ file = "fillres"; name = "a clear rendition stops collapsing onto tinted"
       from = 'case Rendition::DarkClear:  return icf::Appearance::Tinted;'
       to   = 'case Rendition::DarkClear:  return icf::Appearance::Dark;' },
    # The arm nobody predicted, and the one a light/dark-only test set misses.
    @{ file = "fillres"; name = "the tinted background is a ramp instead of clear"
       from = 'return solidOf(iconColorClear());'
       to   = 'return systemOf(SystemFill::Light);' },
    # The layer's `automatic` reads its own fill at the LIGHT slot, not at the
    # slot being rendered, and THIS is the line that chooses it. Point it at the
    # appearance being rendered and the inheritance operator becomes identity.
    @{ file = "fillres"; name = "the layer inherits from the slot being rendered"
       from = 'lightCtx.appearance = icf::Appearance::Light;'
       to   = 'lightCtx.appearance = ctx.appearance;' },
    # WHY THE OBVIOUS ANCHOR IS NOT THE ONE USED. The first version of this
    # mutation pointed at the RECURSIVE call instead --
    #   `layerFillFrom(*lightSlotFill, icf::Appearance::Light, nullptr)` -> `appearance`
    # -- and it SURVIVED, because the two forms are provably indistinguishable:
    # the third argument is `nullptr`, so for kind `Automatic` either the light
    # terminator fires or the `!lightSlotFill` guard does, and the other six
    # kinds never read the appearance at all. Every input gives the same answer.
    #
    # That is an EQUIVALENT MUTANT, not a hole in the tests, and it is recorded
    # here rather than left in the list. A sweep that reports a defect nobody
    # can fix teaches people to ignore the sweep. The `Light` in that recursive
    # call stays in the source because it is what the binary passes -- faithful
    # and unobservable are not in conflict.
    @{ file = "fillres"; name = "the light-slot terminator keys on dark"
       from = 'if (appearance == icf::Appearance::Light) return noFill();'
       to   = 'if (appearance == icf::Appearance::Dark) return noFill();' },
    @{ file = "fillres"; name = "a layer with no light slot refuses instead of drawing"
       from = 'if (!lightSlotFill) return noFill();'
       to   = 'if (!lightSlotFill) return refuse("no light slot");' },
    # `[BIN]` The ONE site of four that reads `orientation`, and the order the
    # constructor at 0x10C1DC takes its two points in.
    @{ file = "fillres"; name = "the layer ramp takes its start from the stop point"
       from = 'p.start = {fill.orientation->start.x, fill.orientation->start.y};'
       to   = 'p.start = {fill.orientation->stop.x, fill.orientation->stop.y};' },
    @{ file = "fillres"; name = "the layer ramp takes its end from the start point"
       from = 'p.end = {fill.orientation->stop.x, fill.orientation->stop.y};'
       to   = 'p.end = {fill.orientation->start.x, fill.orientation->start.y};' },

    # ---- the canned ramps, the opacity rewrite, the placement ------------
    #
    # THE SHARPEST ONE IS THE OPACITY. Both canned ramps carry alpha 1.0, where
    # replacing and multiplying give the same number, so NO corpus case can tell
    # the two rules apart -- only the dedicated test can. If this mutation
    # survives, that test is decoration.
    @{ file = "sysfill"; name = "the stop opacity is multiplied instead of replaced"
       from = 's.rgba[3] = opacity;'
       to   = 's.rgba[3] = in.rgba[3] * opacity;' },
    # A grey short one digit still rounds to the same sRGB byte. What it no
    # longer equals is the double that 245/255 produces.
    @{ file = "sysfill"; name = "the light ramp loses a digit off its second grey"
       from = 'return buildSystemRamp(1.0, 0.9607843137254902);'
       to   = 'return buildSystemRamp(1.0, 0.960784313725490);' },
    @{ file = "sysfill"; name = "the dark ramp moves one double off its second grey"
       from = 'return buildSystemRamp(0.12156862745098039, 0.058823529411764705);'
       to   = 'return buildSystemRamp(0.12156862745098039, 0.05882352941176471);' },
    @{ file = "sysfill"; name = "the ramp stops are laid out back to front"
       from = 'stops[0].location = 0.0;'
       to   = 'stops[0].location = 1.0;' },
    @{ file = "sysfill"; name = "the second ramp stop sits on the first"
       from = 'stops[1].location = 1.0;'
       to   = 'stops[1].location = 0.0;' },
    @{ file = "sysfill"; name = "a canned ramp stop is born transparent"
       from = 'stops[0].rgba[3] = 1.0;'
       to   = 'stops[0].rgba[3] = 0.0;' },
    # The axis this project spent a whole spec unable to read.
    @{ file = "sysfill"; name = "the default axis runs across instead of down"
       from = 'p.end = {0.0, 1.0};'
       to   = 'p.end = {1.0, 0.0};' },
    @{ file = "sysfill"; name = "the default axis does not start at the origin"
       from = 'p.start = {0.0, 0.0};'
       to   = 'p.start = {0.0, 1.0};' },
    @{ file = "sysfill"; name = "a placement the document named is overridden by the default"
       from = 'const GradientPlacement p = placement.has_value() ? *placement : defaultGradientPlacement();'
       to   = 'const GradientPlacement p = defaultGradientPlacement();' },
    @{ file = "sysfill"; name = "the resolved system fill carries a placement"
       from = 'out.placement = std::nullopt;'
       to   = 'out.placement = defaultGradientPlacement();' },
    # The refusal is the point: the chiclet-aligned rect was never read, so
    # there is no number to return. Falling back to the bounding box would draw
    # something plausible on a geometry nobody measured, and the header forbids
    # it -- this proves the ban is enforced and not merely written down.
    @{ file = "sysfill"; name = "the unread chiclet rect falls back to the bounding box"
       from = 'if (source == SystemFillRectSource::BoundingRect) return boundingRect;'
       to   = 'return boundingRect;' },

    # ---- the renderer wiring --------------------------------------------
    #
    # Being MORE correct than the target is the defect here: the background
    # converter never reads `orientation`, so honouring it is a different pixel.
    # The note is how the divergence stays visible, and dropping it makes the
    # renderer quietly claim a fidelity it does not have.
    @{ file = "icon"; name = "the discarded background orientation is not reported"
       from = 'note(out.notes, kDiscardedBackgroundOrientationNote);'
       to   = '(void)0;' },

    # ---- the GROUP's blend, which this renderer read off the layer only -----
    #
    # THE DEFECT THESE TWO GUARD WAS SILENT, and that is why both exist. Until
    # 2026-09-03 the group's `blend-mode` was never read: twelve corpus
    # documents were composited as `normal` and the report came back EMPTY.
    # Nothing went red, because nothing looked.
    #
    # The first mutation restores exactly that defect. The second is its mirror,
    # and it is the one a careless fix would survive: refusing EVERY group that
    # carries the key -- `normal` included -- also makes the first test pass,
    # and turns a value that means "do nothing" into a gap.
    @{ file = "icon"; name = "the group's blend mode is never read"
       from = 'if (*s != "normal") groupBlend = s;'
       to   = 'if (false) groupBlend = s;' },
    @{ file = "icon"; name = "a group blending as normal is refused along with the rest"
       from = 'if (*s != "normal") groupBlend = s;'
       to   = 'groupBlend = s;' },
    # And the reason has to NAME the mode. A gap reported without saying which
    # blend provoked it is a count, not a report.
    @{ file = "icon"; name = "the group blend gap does not name the mode"
       from = 'skip("mescla de grupo ''" + *groupBlend + "'' -- o grupo inteiro "'
       to   = 'skip("mescla de grupo -- o grupo inteiro "' },

    # ---- as duas tabelas da mescla ---------------------------------------
    #
    # A PRIMEIRA E A QUE IMPORTA. `plus_lighter` (43) e `plus_darker` (44) sao
    # vizinhos na `cg_table` e adjacentes no CGBlendMode (27 e 26), e foram os
    # DOIS que o casamento por formula deixou ambiguos. Troca-los e exatamente o
    # erro que uma leitura descuidada comete, e o unico teste que o pega e o que
    # confere contra a numeracao publica do CoreGraphics.
    @{ file = "blend"; name = "the two plus modes are swapped in the cg table"
       from = '    44,  // CG 26 plusDarker      -> plus_darker'
       to   = '    43,  // CG 26 plusDarker      -> plus_darker' },
    @{ file = "blend"; name = "plus-lighter takes the neighbouring CG constant"
       from = '    27,  // plusLighter -> CG plusLighter'
       to   = '    26,  // plusLighter -> CG plusLighter' },
    # O bug que um `static_cast` entre os dois enums produziria: no formato
    # `plusLighter` e a tag 1, e a tag 1 do motor e `darken`.
    @{ file = "blend"; name = "the format enum is bridged by tag, as a cast would"
       from = '        case icf::BlendMode::PlusLighter: return BlendMode::PlusLighter;'
       to   = '        case icf::BlendMode::PlusLighter: return BlendMode::Darken;' },
    @{ file = "blend"; name = "every mode answers with the same json spelling"
       from = '        if (r.mode == mode) return r.key;'
       to   = '        if (r.mode == mode) return "normal";' },
    @{ file = "blend"; name = "the first two shader case names are transposed"
       from = '    "copy", "clear", "source_over", "source_in", "source_out", "source_atop",'
       to   = '    "clear", "copy", "source_over", "source_in", "source_out", "source_atop",' },

    # ---- a aritmetica da mescla -------------------------------------------
    #
    # `overlay` e `hard_light` sao a MESMA funcao com os operandos trocados, e a
    # unica coisa que os separa e em qual lado o ramo testa. Sao os dois casos
    # em que o casamento por formula sozinho nao decide -- por isso as duas
    # mutacoes abaixo existem em par.
    @{ file = "blendfx"; name = "overlay branches on the source, becoming hard-light"
       from = '                b[k] = (d > 0.5 * ab) ? (2.0 * (s * ab + d * (as - s)) - as * ab)'
       to   = '                b[k] = (s > 0.5 * as) ? (2.0 * (s * ab + d * (as - s)) - as * ab)' },
    @{ file = "blendfx"; name = "hard-light branches on the backdrop, becoming overlay"
       from = '                b[k] = (s > 0.5 * as) ? (as * ab - 2.0 * (ab - d) * (as - s))'
       to   = '                b[k] = (d > 0.5 * ab) ? (as * ab - 2.0 * (ab - d) * (as - s))' },
    # O pareamento cruzado dos alphas. Invisivel sempre que os dois alphas sao
    # iguais -- que era o caso de TODOS os outros testes deste arquivo ate o
    # teste de alphas mistos ser escrito por causa desta mutacao.
    @{ file = "blendfx"; name = "darken pairs each side with its own alpha"
       from = '                b[k] = std::min(as * d, ab * s);'
       to   = '                b[k] = std::min(ab * d, as * s);' },
    # O piso do alpha do soft-light: sem ele, um fundo transparente divide por
    # zero e o resultado deixa de ser um numero.
    @{ file = "blendfx"; name = "soft-light divides by the raw backdrop alpha"
       from = '                const double floorA = std::max(ab, kSoftLightAlphaFloor);'
       to   = '                const double floorA = ab;' },
    # O `screen` nao tem cauda de composicao -- ele esta na banda barata. Usar o
    # alpha no lugar do canal transforma-o na cauda e muda o numero.
    @{ file = "blendfx"; name = "screen is folded into the composition tail"
       from = '                out.rgba[k] = src.rgba[k] + dst.rgba[k] * (1.0 - src.rgba[k]);'
       to   = '                out.rgba[k] = src.rgba[k] + dst.rgba[k] * (1.0 - as);' },
    # A folga negativa que separa plus-darker de plus-lighter, com o sinal
    # invertido: vira mais claro que o mais claro.
    @{ file = "blendfx"; name = "the plus-darker slack has its sign flipped"
       from = '            const double slack = (mode == BlendMode::PlusDarker) ? (a - sum) : 0.0;'
       to   = '            const double slack = (mode == BlendMode::PlusDarker) ? (sum - a) : 0.0;' },
    @{ file = "blendfx"; name = "the composition tail adds the alphas without the product"
       from = '    out.rgba[3] = as + ab - as * ab;'
       to   = '    out.rgba[3] = as + ab;' },
    # E a recusa: um modo nao transcrito TEM que ser perguntavel. Se ele se
    # declara pronto, o renderizador reporta fidelidade que nao tem -- o mesmo
    # defeito que o silencio da mescla de grupo era.
    @{ file = "blendfx"; name = "an untranscribed mode claims to be transcribed"
       from = '            return false;'
       to   = '            return true;' },

    # ---- a geometria do traco ---------------------------------------------
    #
    # A PRIMEIRA E O DEFEITO QUE A NOSSA PROPRIA DOCUMENTACAO CARREGOU. O doc 03
    # §10.2 publicava min(join[iid], join[iid+2]) ate 2026-09-04, e com esse
    # indice o ponto lido e o fantasma que carrega o -3: o primeiro segmento de
    # TODO subpath desaparece. A mutacao restaura o texto errado.
    @{ file = "strokegeo"; name = "the discard rule reads the ghost instead of the segment"
       from = '    return std::min(stream[iid + 1].join, stream[iid + 2].join) >= 0;'
       to   = '    return std::min(stream[iid].join, stream[iid + 2].join) >= 0;' },
    # O limite de miter compara contra o QUADRADO. Sem o quadrado a regra ainda
    # e monotona e ainda degenera -- so degenera no angulo errado.
    @{ file = "strokegeo"; name = "the miter limit is compared without being squared"
       from = '    if ((1.0 + c) * miterLimit * miterLimit < 2.0) return LineJoin::Bevel;'
       to   = '    if ((1.0 + c) * miterLimit < 2.0) return LineJoin::Bevel;' },
    @{ file = "strokegeo"; name = "the round-corner early out moves off its constant"
       from = 'constexpr double kRoundCorner = 0.99;'
       to   = 'constexpr double kRoundCorner = 0.9;' },
    # O encurtamento do vertice e a outra METADE da ponta butt: sem ele a conta
    # do cap roda da origem errada e o traco termina um pixel e meio comprido.
    @{ file = "strokegeo"; name = "the butt end is not shortened by the vertex stage"
       from = '    if (params.cap == LineCap::Butt && wholeLen > s) {'
       to   = '    if (false && params.cap == LineCap::Butt && wholeLen > s) {' },
    @{ file = "strokegeo"; name = "the butt cap forgets the pixel the ramp needs"
       from = '        case LineCap::Butt:   return std::max(r + ov - s, d);'
       to   = '        case LineCap::Butt:   return std::max(r + ov, d);' },
    # O fantasma e uma REFLEXAO do vizinho pelo extremo, nao uma copia do
    # extremo: uma copia daria um segmento de comprimento zero na janela.
    @{ file = "strokegeo"; name = "the ghost point is a copy of the end, not its mirror"
       from = '    return {2.0 * end.x - neighbour.x, 2.0 * end.y - neighbour.y};'
       to   = '    return {end.x, end.y};' },
    # O fio de cabelo mantem a tinta DESBOTANDO. Sem a escala ele fica opaco e
    # largo demais, que e o defeito classico de antialiasing de traco fino.
    @{ file = "strokegeo"; name = "a hairline keeps full alpha instead of fading"
       from = '    double alpha = params.hardCoverage ? 1.0 : (rEff == 0.0 ? 1.0 : r / rEff);'
       to   = '    double alpha = 1.0;' },
    @{ file = "strokegeo"; name = "the antialiased ramp is two pixels wide"
       from = '    return alpha * smoothstep01((half - sd) / s);'
       to   = '    return alpha * smoothstep01((half - sd) / (s * 2.0));' },
    @{ file = "strokegeo"; name = "the lines pass runs one instance too many"
       from = '    return pointCount < 3 ? 0 : pointCount - 3;'
       to   = '    return pointCount < 3 ? 0 : pointCount - 2;' },

    # ---- a mescla ligada no compositor ------------------------------------
    #
    # Um acumulador JA e premultiplicado. Passa-lo pelo caminho reto multiplica
    # o alpha duas vezes -- invisivel onde o alpha e 1, que e quase todo o
    # corpus, e por isso a mutacao existe.
    @{ file = "icon"; name = "a group accumulator is premultiplied a second time"
       from = '            sc.rgba[k] = src[i + k];'
       to   = '            sc.rgba[k] = src[i + k] * src[i + 3];' },
    @{ file = "icon"; name = "the group is composited straight onto the canvas"
       from = '        std::vector<float>& target = blendTheGroup ? groupAcc : acc;'
       to   = '        std::vector<float>& target = acc;' },
    @{ file = "icon"; name = "the group result is never mixed back in"
       from = '        if (blendTheGroup) blendPremulOver(acc, groupAcc, *groupMode);'
       to   = '        (void)0;' },
    # A RECUSA sobre vidro e a decisao mais cara deste trabalho -- ela custa 8
    # documentos. Se ela cair sem ninguem notar, o renderizador passa a desenhar
    # refracao sobre fundo vazio e a reportar sucesso.
    @{ file = "icon"; name = "a blended group over glass is drawn anyway"
       from = '        const bool blendTheGroup = groupBlend && groupMode && !groupHasGlass;'
       to   = '        const bool blendTheGroup = groupBlend && groupMode;' },
    # O pulo de alpha zero que `over` faz e que `blendOver` NAO pode fazer:
    # plus-darker tem termo que nao some com origem transparente.
    # NAO ha mutacao aqui para o pulo de alpha zero, e a ausencia e o
    # registro. A primeira versao desta lista tinha uma, e ela SOBREVIVEU --
    # corretamente: uma origem com alpha zero e a identidade nos nove modos
    # transcritos (a cauda vira `d`, todo termo B carrega um fator `as`, e a
    # folga do par plus e `saturate(ab) - ab = 0`). Pular e misturar escrevem
    # o mesmo numero, entao nenhum teste pode distinguir os dois. Mutante
    # equivalente, classificado por analise de caso e nao por chute.
    #
    # No lugar dela, uma que e observavel: a arte da camada chega RETA e o
    # blend a quer premultiplicada.
    @{ file = "icon"; name = "the layer art enters the blend without being premultiplied"
       from = '        for (int k = 0; k < 3; ++k) s.rgba[k] = src[i + k] * a;'
       to   = '        for (int k = 0; k < 3; ++k) s.rgba[k] = src[i + k];' }
)

Write-Host "gate-m1: $($mutations.Count) mutations, corpus at $CorpusDir`n"

# BEFORE anything is read as pristine: if a previous run died mid-sweep, the tree
# on disk is still mutated and reading it now would enshrine the mutation AS the
# pristine text -- the sweep would then "restore" to a defect and pass.
# The slice, taken BEFORE the anchor pre-flight so a slice checks only its own
# anchors -- otherwise a stale anchor outside the range would stop a run that
# was never going to touch it.
$sweepAll = $mutations
$sliced = $false
if ($From -gt 0 -or $To -gt 0) {
    $sliced = $true
    $lo = if ($From -gt 0) { $From } else { 1 }
    $hi = if ($To -gt 0) { $To } else { $mutations.Count }
    if ($lo -lt 1) { $lo = 1 }
    if ($hi -gt $mutations.Count) { $hi = $mutations.Count }
    if ($lo -gt $hi) {
        Write-Host "FAILED: an empty slice ($lo..$hi) is not a run"
        exit 1
    }
    $mutations = $mutations[($lo - 1)..($hi - 1)]
    Write-Host "SLICE $lo..$hi of $($sweepAll.Count) -- this is NOT a full gate run`n"
}

Recover-FromCrashedRun

foreach ($k in $sources.Keys) {
    $original[$k] = [IO.File]::ReadAllText($sources[$k])
}
Save-Pristine

# EVERY anchor is checked BEFORE the first build, not when the sweep reaches it.
#
# An anchor rots whenever the code it points at is edited, and this guard has
# fired six times for that reason -- each time twenty minutes into a run, after
# dozens of builds had already been paid for. The check costs milliseconds and
# it belongs at the front. It also reports ALL the stale anchors at once, so a
# refactor that moved three lines is one round of repair instead of three.
$stale = @()
foreach ($m in $mutations) {
    if (-not $sources.ContainsKey($m.file)) {
        $stale += "$($m.name): unknown file key '$($m.file)'"
        continue
    }
    $n = ([regex]::Matches($original[$m.file], [regex]::Escape($m.from))).Count
    if ($n -ne 1) { $stale += "$($m.name): anchor appears $n times in $($m.file), expected 1" }
}
if ($stale.Count -gt 0) {
    Write-Host "FAILED: $($stale.Count) stale anchor(s) -- the code moved and the sweep did not"
    foreach ($x in $stale) { Write-Host "  $x" }
    # NOTHING WAS MUTATED, so the in-progress marker must not survive. Leaving it
    # made every later run "recover" by restoring sources from this run's backup
    # -- harmless while the tree is unchanged, and a way to LOSE an edit made
    # between the two runs, which is the repair a stale anchor asks for.
    if (Test-Path $Marker) { Remove-Item $Marker -Force }
    exit 1
}
Write-Host "anchors: $($mutations.Count) of $($mutations.Count) resolve`n"

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
if ($pristine.text -notmatch '(\d+) SVGs: (\d+) read, (\d+) refused') {
    Write-Host "FAILED: the SVG document gate did not report -- did it run?"; exit 1
}
$svgs = [int]$Matches[1]; $svgread = [int]$Matches[2]; $svgbad = [int]$Matches[3]

if ($docs -lt 145)  { Write-Host "FAILED: only $docs documents, expected at least 145"; exit 1 }
if ($exact -lt 135) { Write-Host "FAILED: only $exact byte-exact, expected at least 135"; exit 1 }
if ($clean -ne $docs) { Write-Host "FAILED: $clean of $docs documents fully understood"; exit 1 }
if ($reach -ne $specs) { Write-Host "FAILED: $reach of $specs specializations reachable"; exit 1 }
if ($vbad -ne 0) { Write-Host "FAILED: $vbad values failed to decode"; exit 1 }
if ($vals -lt 60000) { Write-Host "FAILED: only $vals values decoded, expected 60000+"; exit 1 }
if ($tbad -ne 0) { Write-Host "FAILED: $tbad tree(s) fell back to raw JSON"; exit 1 }
if ($svgbad -ne 0) { Write-Host "FAILED: $svgbad SVG(s) refused"; exit 1 }
if ($svgs -lt 140) { Write-Host "FAILED: only $svgs SVGs, expected 140+"; exit 1 }
Write-Host "pristine: OK`n"

# ---- 3: the mutation sweep -----------------------------------------------


foreach ($k in $sources.Keys) { $hashes[$k] = (Get-FileHash $sources[$k] -Algorithm SHA256).Hash }
$caught = 0
$survivors = @()
$crashed = @()

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
                if ($n -eq 0) {
                    # A non-zero exit with no FAIL line is a CRASH, not a test
                    # noticing. It counts as a detection only by accident, and a
                    # suite that dies stops reporting everything after it -- so
                    # this is a failure of the TEST, and the gate says so.
                    Write-Host ("  {0,-56} CRASHED (no assertion fired)" -f $m.name)
                    $crashed += $m.name
                } else {
                    Write-Host ("  {0,-56} caught ({1} assertion(s))" -f $m.name, $n)
                    $caught++
                }
            }
        }
        Restore-Sources
    }
} finally {
    Restore-Sources
    # The sweep is over and the tree is proven byte-exact: the marker may go.
    if (Test-Path $Marker) { Remove-Item $Marker -Force }
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
Write-Host "svg:    $svgread of $svgs read into geometry, $svgbad refused"
Write-Host "sweep:  $caught of $($mutations.Count) mutations caught"
if ($crashed.Count -gt 0) {
    Write-Host "`nVERDICT: FAILED -- a mutation that crashes the suite is caught by accident"
    foreach ($c in $crashed) { Write-Host "  crashed: $c" }
    Write-Host "  A test that throws instead of asserting hides every case after it."
    exit 1
}
if ($caught -ne $mutations.Count) {
    Write-Host "`nVERDICT: FAILED -- a defect the suite does not see is a defect that ships"
    foreach ($s in $survivors) { Write-Host "  survived: $s" }
    exit 1
}
if ($sliced) {
    Write-Host "`nVERDICT: PARTIAL -- slice of $($mutations.Count) of $($sweepAll.Count) mutations, all caught."
    Write-Host "         A slice is not a pass. Run with no -From/-To for the gate."
    exit 0
}
Write-Host "`nVERDICT: gate-m1 passed"
exit 0
