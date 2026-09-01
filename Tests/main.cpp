#include "check.h"
#include <cstring>

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (auto& c : ictest::cases()) {
        if (filter && !std::strstr(c.name, filter)) continue;
        std::printf("%s\n", c.name);
        c.fn();
        ++ran;
    }
    std::printf("\n%d case(s), %d failure(s)\n", ran, ictest::failures());
    return ictest::failures() ? 1 : 0;
}
