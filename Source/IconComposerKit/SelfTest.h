#pragma once
// The headless frame script: what makes "the UI opens" an assertion.
//
// sfsymview's precedent, and spec 13/09 §8. Every panel is drawn every frame in
// a context with no backend; the script opens, selects, edits, undoes, saves and
// compares bytes; the report is numbers a test can hold and a person can read.
//
// The panels return MEASUREMENTS of the frame (Panels.h) precisely so that this
// script can assert on a frame without a window: how many rows the tree drew,
// how many sections the inspector typed, whether the canvas had a texture.
//
// Three of these numbers carry the weight:
//   * `bytesRoundTripped` -- open, edit, undo, save gives the original bytes
//     back on a byte-exact document (spec 13/09 §8, and the 135 of §2). A `no`
//     here is a defect in the undo or in `setProperty`, never in the script.
//   * `imguiErrors` -- any recoverable ImGui error in a headless frame is wrong
//     drawing (a duplicated ID, an unbalanced stack). It must be zero.
//   * the panel measurements -- zero layer rows on a document with groups is a
//     mute UI, and nobody would see that without a window.
#include "Source/IconComposerKit/Ports.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace ick {

struct SelfTestReport {
    int frames = 0;
    std::size_t groups = 0, layers = 0, sections = 0, diagnostics = 0;
    // A BARRA DE RENDITIONS (T4): quantos itens ela desenhou e quantos deles
    // chegaram a ter miniatura. Os dois números, e não um: a suíte exercita a
    // fila das miniaturas com um agendador FALSO, e este é o único lugar onde
    // ela passa por um dispositivo de verdade. `renditions` sem
    // `renditionThumbs` é a forma na tela e nenhum pixel atrás dela.
    std::size_t renditions = 0, renditionThumbs = 0;
    bool textured = false;
    bool bytesRoundTripped = false;
    std::uint64_t imguiErrors = 0;
    std::string failure;   // non-empty: the script stopped here
};

// Runs the script against `bundleDir` with the given scheduler and sink: the app
// passes a synchronous scheduler over a real device, a test passes fakes. The
// bundle is copied to a scratch directory first -- the script saves, and the
// original is somebody's file.
SelfTestReport runSelfTest(const std::filesystem::path& bundleDir, RenderScheduler& scheduler,
                           TextureSink& sink, int frames);

// The one line the executable prints.
std::string describe(const SelfTestReport& r);

}  // namespace ick
