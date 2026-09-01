#pragma once
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace ictest {
struct Case { const char* name; void (*fn)(); };
inline std::vector<Case>& cases() { static std::vector<Case> c; return c; }
inline int& failures() { static int f = 0; return f; }
struct Reg { Reg(const char* n, void (*f)()) { cases().push_back({n, f}); } };
template <class T> std::string show(const T& v) { std::ostringstream o; o << v; return o.str(); }
inline std::string show(const std::string& v) { return "\"" + v + "\""; }
inline std::string show(bool v) { return v ? "true" : "false"; }
}  // namespace ictest

#define TEST_CASE(name) \
    static void name(); static ictest::Reg reg_##name(#name, name); static void name()
#define CHECK(expr) do { if (!(expr)) { \
    std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); ++ictest::failures(); } } while (0)
#define REQUIRE(expr) do { if (!(expr)) { \
    std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); ++ictest::failures(); return; } } while (0)
#define CHECK_EQ(a, b) do { auto va_ = (a); auto vb_ = (b); if (!(va_ == vb_)) { \
    std::printf("  FAIL %s:%d: %s == %s\n    left:  %s\n    right: %s\n", __FILE__, __LINE__, #a, #b, \
    ictest::show(va_).c_str(), ictest::show(vb_).c_str()); ++ictest::failures(); } } while (0)
