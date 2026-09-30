#include "check.h"
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <vector>

// THE MUTATION SWEEP ONLY NEEDS TO KNOW *THAT* THE SUITE WENT RED, AND THE ORDER
// OF THE CASES IS WHAT DECIDES HOW LONG THAT TAKES.
//
// Measured 2026-09-09: the whole suite is 43 seconds, and 20 of its 565 cases
// are 35 of them -- the ones that walk 145 documents, 149 SVGs and 60 PNGs. Those
// twenty registered FIRST, so a mutation noticed by a one-millisecond unit test
// still paid for the corpus before anyone heard about it. At 308 mutations that
// was five and a half hours, four fifths of it re-proving verdicts already
// reached.
//
// Two changes, and neither touches what any case asserts:
//
//   ORDER      the corpus-reading files run LAST. A file is corpus-reading
//              because it is named below, not because of a guess about its
//              contents -- an unlisted file simply runs early, which costs time
//              and never correctness.
//   STOP       `IC_STOP_ON_FIRST_FAILURE=1` returns as soon as a case leaves a
//              failure behind. Set by `gate-m1.ps1` for the MUTATED runs only;
//              the pristine run and any run a person reads still execute
//              everything, because there the list of what failed is the point.
//
// The count printed under the flag is a floor and says so, rather than letting
// "1 failure(s)" be mistaken for the tally.
namespace {

// THE FOUR FILES THAT ARE THE SUITE'S CLOCK, and they were found by MEASURING
// (`IC_TIME_CASES=1`) rather than by reasoning about what looks expensive.
//
//   test_automatic_fill   17.7s   42% of the whole suite, on its own
//   test_glass_layer       8.6s
//   test_icon_render       6.7s
//   test_png               5.9s
//                         -----
//                         38.9s of 41.8s -- ninety-three per cent
//
// The first version of this list guessed "the ones that read the corpus" and
// named `test_corpus` and `test_svg_corpus`. They are 547ms and 210ms. Both
// guesses were wrong and the list bought twenty per cent instead of ninety.
//
// An unlisted file simply runs early: that costs time and never correctness, so
// the list going stale is a slow sweep and not a wrong one.
bool isSlow(const char* file) {
    static const char* kSlow[] = {
        "test_automatic_fill", "test_glass_layer", "test_icon_render", "test_png",
        "test_values_tojson", "test_document_edit", "test_bundle_save",
        // Os dois caminhos de render sobre um recorte do corpus (frente GPU, G1).
        "test_gpu_fidelity",
        // The time budget is here for a DIFFERENT reason from the other six, and
        // the reason matters. The others are slow because they walk the corpus;
        // this one is slow ON PURPOSE -- it renders the corpus's heaviest
        // document at the preview size and asks how long that took, which is
        // 2.6s in Release and about 15s in Debug. Running it early would put
        // fifteen seconds in front of every mutation the sweep's early exit was
        // meant to answer in one. Last, it is paid only by the pristine run and
        // by mutations that survived everything else.
        "test_time_budget",
    };
    for (const char* s : kSlow) {
        if (std::strstr(file, s)) return true;
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    const char* stopEnv = std::getenv("IC_STOP_ON_FIRST_FAILURE");
    const bool stopEarly = stopEnv && stopEnv[0] == '1';

    // `IC_TIME_CASES=1` prints each case's own milliseconds and the file it came
    // from. It exists because the first attempt to speed the sweep up ordered the
    // cases by a GUESS: I had measured by name substring and then partitioned by
    // file, which are not the same partition, and the "fix" bought twenty per cent
    // where it should have bought four fifths. Measure the thing you reorder.
    const char* timeEnv = std::getenv("IC_TIME_CASES");
    const bool timeCases = timeEnv && timeEnv[0] == '1';

    std::vector<const ictest::Case*> order;
    order.reserve(ictest::cases().size());
    for (const auto& c : ictest::cases()) {
        if (!isSlow(c.file)) order.push_back(&c);
    }
    for (const auto& c : ictest::cases()) {
        if (isSlow(c.file)) order.push_back(&c);
    }

    int ran = 0;
    bool stopped = false;
    for (const ictest::Case* c : order) {
        if (filter && !std::strstr(c->name, filter)) continue;
        std::printf("%s\n", c->name);
        const auto t0 = std::chrono::steady_clock::now();
        c->fn();
        if (timeCases) {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - t0).count();
            std::printf("  [%lldms] %s\n", static_cast<long long>(ms), c->file);
        }
        ++ran;
        if (stopEarly && ictest::failures()) {
            stopped = true;
            break;
        }
    }
    if (stopped) {
        std::printf("\n%d case(s) run, %d failure(s) -- STOPPED AT THE FIRST, "
                    "so neither number is a total\n",
                    ran, ictest::failures());
    } else {
        std::printf("\n%d case(s), %d failure(s)\n", ran, ictest::failures());
    }
    return ictest::failures() ? 1 : 0;
}
