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
    @{ file = "rbsvg"; name = "a shape that cannot be drawn is dropped in silence"
       from = 'out.skipped.push_back(
                {i, shape.element,'
       to   = 'if (false) out.skipped.push_back(
                {i, shape.element,' },
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
       to   = 'if (false) {' }
)

Write-Host "gate-m1: $($mutations.Count) mutations, corpus at $CorpusDir`n"

# BEFORE anything is read as pristine: if a previous run died mid-sweep, the tree
# on disk is still mutated and reading it now would enshrine the mutation AS the
# pristine text -- the sweep would then "restore" to a defect and pass.
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
Write-Host "`nVERDICT: gate-m1 passed"
exit 0
