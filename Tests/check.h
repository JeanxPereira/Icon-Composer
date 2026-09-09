#pragma once
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace ictest {
// `file` is carried so the runner can put the CORPUS-READING cases LAST.
//
// Measured 2026-09-09: 20 of the 565 cases are 35 of the suite's 43 seconds --
// they walk 145 documents, 149 SVGs and 60 PNGs. Running them first made the
// mutation sweep's early exit worthless: the unit test that notices a mutation
// sat behind half a minute of corpus work the mutation never touched. Order is
// not correctness here, and it was four fifths of the clock.
struct Case { const char* name; void (*fn)(); const char* file; };
inline std::vector<Case>& cases() { static std::vector<Case> c; return c; }
inline int& failures() { static int f = 0; return f; }
struct Reg { Reg(const char* n, void (*f)(), const char* fl) { cases().push_back({n, f, fl}); } };
template <class T> std::string show(const T& v) { std::ostringstream o; o << v; return o.str(); }
inline std::string show(const std::string& v) { return "\"" + v + "\""; }
inline std::string show(bool v) { return v ? "true" : "false"; }
}  // namespace ictest

#define TEST_CASE(name) \
    static void name(); static ictest::Reg reg_##name(#name, name, __FILE__); static void name()
#define CHECK(expr) do { if (!(expr)) { \
    std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); ++ictest::failures(); } } while (0)
#define REQUIRE(expr) do { if (!(expr)) { \
    std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); ++ictest::failures(); return; } } while (0)
#define CHECK_EQ(a, b) do { auto va_ = (a); auto vb_ = (b); if (!(va_ == vb_)) { \
    std::printf("  FAIL %s:%d: %s == %s\n    left:  %s\n    right: %s\n", __FILE__, __LINE__, #a, #b, \
    ictest::show(va_).c_str(), ictest::show(vb_).c_str()); ++ictest::failures(); } } while (0)
