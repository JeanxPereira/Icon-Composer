#include "Source/CoreSVG/Paint.h"

#include <charconv>
#include <vector>

namespace icf::svg {
namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

std::string_view trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && isSpace(s[a])) ++a;
    while (b > a && isSpace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// The numbers of a `color()` function: whitespace-separated, with an optional
// `/ alpha` at the end.
std::vector<double> componentList(std::string_view text) {
    std::vector<double> out;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && (isSpace(text[i]) || text[i] == '/')) ++i;
        if (i >= text.size()) break;
        const size_t start = i;
        if (text[i] == '+' || text[i] == '-') ++i;
        while (i < text.size() && ((text[i] >= '0' && text[i] <= '9') || text[i] == '.')) ++i;
        if (i == start) return {};
        double v = 0;
        auto r = std::from_chars(text.data() + start, text.data() + i, v);
        if (r.ec != std::errc() || r.ptr != text.data() + i) return {};
        out.push_back(v);
    }
    return out;
}

Paint colorPaint(SvgColor c) {
    Paint p;
    p.kind = PaintKind::Color;
    p.color = c;
    return p;
}

}  // namespace

Paint parsePaint(std::string_view value) {
    const std::string_view v = trim(value);
    if (v.empty()) return Paint{};

    if (v == "none") {
        Paint p;
        p.kind = PaintKind::None;
        return p;
    }
    // The whole colour-name table this corpus needs. Two entries, measured:
    // 145 other CSS names occur zero times, and a reader that guessed at one
    // would paint something nobody wrote.
    if (v == "white") return colorPaint({1, 1, 1, 1, false});
    if (v == "black") return colorPaint({0, 0, 0, 1, false});

    if (v[0] == '#') {
        const std::string_view digits = v.substr(1);
        auto nibble = [&](size_t i) { return hexDigit(digits[i]); };
        if (digits.size() == 6) {
            for (char c : digits) {
                if (hexDigit(c) < 0) return Paint{};
            }
            return colorPaint({(nibble(0) * 16 + nibble(1)) / 255.0,
                               (nibble(2) * 16 + nibble(3)) / 255.0,
                               (nibble(4) * 16 + nibble(5)) / 255.0, 1, false});
        }
        if (digits.size() == 3) {
            for (char c : digits) {
                if (hexDigit(c) < 0) return Paint{};
            }
            // `#f80` is `#ff8800`: each digit doubles, so the range still ends
            // at 255 rather than at 240.
            return colorPaint({(nibble(0) * 17) / 255.0, (nibble(1) * 17) / 255.0,
                               (nibble(2) * 17) / 255.0, 1, false});
        }
        return Paint{};
    }

    if (v.compare(0, 4, "url(") == 0 && v.back() == ')') {
        std::string_view inner = trim(v.substr(4, v.size() - 5));
        if (!inner.empty() && (inner.front() == '\'' || inner.front() == '"')) {
            if (inner.size() < 2 || inner.back() != inner.front()) return Paint{};
            inner = inner.substr(1, inner.size() - 2);
        }
        if (inner.empty() || inner.front() != '#') return Paint{};
        Paint p;
        p.kind = PaintKind::Reference;
        p.reference = std::string(inner.substr(1));
        return p;
    }

    // `rgb(r, g, b)`, components 0-255. Not in the first survey of the corpus --
    // the survey classified `fill` and `stroke` and these live mostly in
    // `stop-color`. The document gate found them by naming the exact values it
    // could not read, which is the whole reason the report prints values and not
    // just counts.
    if (v.compare(0, 4, "rgb(") == 0 && v.back() == ')') {
        std::string inner(v.substr(4, v.size() - 5));
        for (char& c : inner) {
            if (c == ',') c = ' ';
        }
        const auto n = componentList(inner);
        if (n.size() != 3) return Paint{};
        return colorPaint({n[0] / 255.0, n[1] / 255.0, n[2] / 255.0, 1, false});
    }

    // CSS Color 4. The same space the `.icon` format writes its colours in, and
    // the reason the space travels with the value instead of being assumed.
    if (v.compare(0, 6, "color(") == 0 && v.back() == ')') {
        const std::string_view inner = trim(v.substr(6, v.size() - 7));
        const size_t sp = inner.find(' ');
        if (sp == std::string_view::npos) return Paint{};
        if (trim(inner.substr(0, sp)) != "display-p3") return Paint{};
        const auto n = componentList(inner.substr(sp));
        if (n.size() != 3 && n.size() != 4) return Paint{};
        return colorPaint({n[0], n[1], n[2], n.size() == 4 ? n[3] : 1.0, true});
    }

    return Paint{};  // Unreadable, and the caller names it
}

std::map<std::string, std::string> parseStyle(std::string_view style) {
    std::map<std::string, std::string> out;
    size_t i = 0;
    while (i <= style.size()) {
        const size_t end = style.find(';', i);
        const std::string_view decl =
            trim(style.substr(i, end == std::string_view::npos ? style.size() - i : end - i));
        if (!decl.empty()) {
            const size_t colon = decl.find(':');
            // A declaration with no colon is not a declaration. Skipped, not
            // guessed at.
            if (colon != std::string_view::npos) {
                const std::string_view key = trim(decl.substr(0, colon));
                const std::string_view val = trim(decl.substr(colon + 1));
                if (!key.empty()) out[std::string(key)] = std::string(val);
            }
        }
        if (end == std::string_view::npos) break;
        i = end + 1;
    }
    return out;
}

}  // namespace icf::svg

namespace icf::svg {

std::map<std::string, std::map<std::string, std::string>> parseStylesheet(std::string_view css) {
    std::map<std::string, std::map<std::string, std::string>> out;
    std::string text(css);

    // Comments and a CDATA wrapper are noise around the rules, not rules.
    for (size_t at = text.find("/*"); at != std::string::npos; at = text.find("/*", at)) {
        const size_t end = text.find("*/", at + 2);
        if (end == std::string::npos) {
            text.erase(at);
            break;
        }
        text.erase(at, end - at + 2);
    }
    for (const char* marker : {"<![CDATA[", "]]>"}) {
        for (size_t at = text.find(marker); at != std::string::npos; at = text.find(marker)) {
            text.erase(at, std::string(marker).size());
        }
    }

    size_t i = 0;
    while (i < text.size()) {
        const size_t open = text.find('{', i);
        if (open == std::string::npos) break;
        const size_t close = text.find('}', open);
        if (close == std::string::npos) break;
        const std::string_view selectors(text.data() + i, open - i);
        const auto declarations = parseStyle(std::string_view(text.data() + open + 1,
                                                              close - open - 1));
        // A comma-separated list is several selectors sharing one body.
        size_t j = 0;
        while (j <= selectors.size()) {
            const size_t comma = selectors.find(',', j);
            const std::string_view one =
                trim(selectors.substr(j, comma == std::string_view::npos ? selectors.size() - j
                                                                         : comma - j));
            // Only a bare class selector. Anything else -- an id, an element, a
            // descendant, a pseudo-class -- is DROPPED: applying a rule this
            // reader cannot target would paint things the rule never named.
            if (one.size() > 1 && one[0] == '.' &&
                one.find_first_of(" \t>+~:[#.", 1) == std::string_view::npos) {
                auto& slot = out[std::string(one.substr(1))];
                for (const auto& d : declarations) slot[d.first] = d.second;
            }
            if (comma == std::string_view::npos) break;
            j = comma + 1;
        }
        i = close + 1;
    }
    return out;
}

}  // namespace icf::svg
