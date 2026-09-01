// ictool -- open a `.icon` and say what is in it.
//
// The grammar copies the target's shape (doc 01 §11): a command first, then the
// document as an unnamed argument, then named arguments. What it does is only
// what this repository can honestly answer -- there is no render here.
#include "Source/cli/Report.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

const char* kUsage =
    "ictool -- reads a .icon bundle\n"
    "\n"
    "  ictool <document.icon>                      what the bundle holds\n"
    "  ictool --tree <document.icon>               the composition, resolved\n"
    "  ictool --assets <document.icon>             the reference to Assets/\n"
    "  ictool --json <document.icon>               re-emit icon.json canonically\n"
    "  ictool --version\n"
    "  ictool --help\n"
    "\n"
    "  --appearance base|light|dark|tinted         context for --tree (default base)\n"
    "  --idiom base|square|iOS|macOS|watchOS       context for --tree (default base)\n";

int fail(const std::string& message) {
    std::fprintf(stderr, "ictool: %s\n", message.c_str());
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "--help" || args[0] == "-h") {
        std::fputs(kUsage, stdout);
        return args.empty() ? 2 : 0;
    }
    if (args[0] == "--version") {
        std::puts("ictool 0.1.0 (Icon-Composer, reimplemented)");
        return 0;
    }

    std::string command = "--summary";
    size_t i = 0;
    if (!args[0].empty() && args[0][0] == '-') {
        command = args[0];
        i = 1;
    }
    if (i >= args.size()) return fail("missing the document path");

    const std::string path = args[i++];
    icf::Context ctx;
    for (; i < args.size(); ++i) {
        if (i + 1 >= args.size()) return fail("missing a value for " + args[i]);
        const std::string& key = args[i];
        const std::string& value = args[++i];
        if (key == "--appearance") {
            auto a = icf::appearanceFromString(value);
            if (!a) return fail("unexpected value for --appearance: " + value);
            ctx.appearance = *a;
        } else if (key == "--idiom") {
            auto d = icf::idiomFromString(value);
            if (!d) return fail("unexpected value for --idiom: " + value);
            ctx.idiom = *d;
        } else {
            return fail("invalid argument name " + key);
        }
    }

    auto bundle = icf::IconBundle::open(path);
    if (!bundle) return fail("not a .icon bundle: " + path);

    if (command == "--summary") {
        std::fputs(icf::cli::summary(*bundle).c_str(), stdout);
    } else if (command == "--tree") {
        std::fputs(icf::cli::tree(*bundle, ctx).c_str(), stdout);
    } else if (command == "--assets") {
        std::fputs(icf::cli::assets(*bundle).c_str(), stdout);
    } else if (command == "--json") {
        std::fputs(icf::json::write(bundle->json()).c_str(), stdout);
        std::fputc('\n', stdout);
    } else {
        return fail("invalid command name " + command);
    }
    return 0;
}
