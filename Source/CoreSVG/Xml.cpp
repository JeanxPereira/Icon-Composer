#include "Source/CoreSVG/Xml.h"

namespace icf::svg {
namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

bool isNameChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '_' || c == '-' || c == ':' || c == '.';
}

// The five XML predefined entities, and nothing else. A numeric reference or a
// DTD-declared one is left as it stands rather than guessed at -- and no corpus
// file uses either.
std::string resolveEntities(std::string_view raw) {
    if (raw.find('&') == std::string_view::npos) return std::string(raw);
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size();) {
        if (raw[i] != '&') {
            out += raw[i++];
            continue;
        }
        const size_t semi = raw.find(';', i);
        if (semi == std::string_view::npos) {
            out += raw[i++];
            continue;
        }
        const std::string_view name = raw.substr(i + 1, semi - i - 1);
        if (name == "amp") out += '&';
        else if (name == "lt") out += '<';
        else if (name == "gt") out += '>';
        else if (name == "quot") out += '"';
        else if (name == "apos") out += '\'';
        else {
            out += raw.substr(i, semi - i + 1);
        }
        i = semi + 1;
    }
    return out;
}

class Reader {
public:
    explicit Reader(std::string_view t) : t_(t) {}

    // Comments, the XML declaration and the doctype: skipped wherever they may
    // legally appear. `<!DOCTYPE …>` can carry an internal subset in brackets,
    // so the scan counts them rather than stopping at the first `>`.
    void skipTrivia() {
        for (;;) {
            while (i_ < t_.size() && isSpace(t_[i_])) ++i_;
            if (starts("<!--")) {
                const size_t end = t_.find("-->", i_ + 4);
                if (end == std::string_view::npos) { i_ = t_.size(); return; }
                i_ = end + 3;
            } else if (starts("<?")) {
                const size_t end = t_.find("?>", i_ + 2);
                if (end == std::string_view::npos) { i_ = t_.size(); return; }
                i_ = end + 2;
            } else if (starts("<!DOCTYPE") || starts("<!doctype")) {
                int depth = 0;
                size_t j = i_;
                for (; j < t_.size(); ++j) {
                    if (t_[j] == '[') ++depth;
                    else if (t_[j] == ']') --depth;
                    else if (t_[j] == '>' && depth <= 0) { ++j; break; }
                }
                i_ = j;
            } else {
                return;
            }
        }
    }

    bool element(Element& out) {
        skipTrivia();
        if (!starts("<") || starts("</")) return false;
        ++i_;  // '<'
        out.name = name();
        if (out.name.empty()) return false;

        for (;;) {
            while (i_ < t_.size() && isSpace(t_[i_])) ++i_;
            if (starts("/>")) {
                i_ += 2;
                return true;
            }
            if (starts(">")) {
                ++i_;
                break;
            }
            const std::string key = name();
            if (key.empty()) return false;
            while (i_ < t_.size() && isSpace(t_[i_])) ++i_;
            if (i_ >= t_.size() || t_[i_] != '=') return false;
            ++i_;
            while (i_ < t_.size() && isSpace(t_[i_])) ++i_;
            // An unquoted value is not XML. Accepting one would be inventing a
            // rule the file did not follow.
            if (i_ >= t_.size() || (t_[i_] != '"' && t_[i_] != '\'')) return false;
            const char quote = t_[i_++];
            const size_t start = i_;
            while (i_ < t_.size() && t_[i_] != quote) ++i_;
            if (i_ >= t_.size()) return false;
            out.attributes.emplace_back(key, resolveEntities(t_.substr(start, i_ - start)));
            ++i_;
        }

        // Content.
        for (;;) {
            const size_t textStart = i_;
            while (i_ < t_.size() && t_[i_] != '<') ++i_;
            if (i_ > textStart) {
                const std::string_view raw = t_.substr(textStart, i_ - textStart);
                // Whitespace between elements is layout, not content. Text that
                // has anything else in it is kept whole -- `<style>` needs it.
                bool blank = true;
                for (char c : raw) {
                    if (!isSpace(c)) { blank = false; break; }
                }
                if (!blank) out.text += resolveEntities(trim(raw));
            }
            if (i_ >= t_.size()) return false;  // never closed
            if (starts("</")) {
                i_ += 2;
                const std::string closing = name();
                while (i_ < t_.size() && isSpace(t_[i_])) ++i_;
                if (i_ >= t_.size() || t_[i_] != '>') return false;
                ++i_;
                return closing == out.name;
            }
            if (starts("<!--") || starts("<?") || starts("<![CDATA[")) {
                if (starts("<![CDATA[")) {
                    const size_t end = t_.find("]]>", i_ + 9);
                    if (end == std::string_view::npos) return false;
                    out.text += std::string(t_.substr(i_ + 9, end - i_ - 9));
                    i_ = end + 3;
                    continue;
                }
                skipTrivia();
                continue;
            }
            Element child;
            if (!element(child)) return false;
            out.children.push_back(std::move(child));
        }
    }

    bool atEnd() {
        skipTrivia();
        return i_ >= t_.size();
    }

private:
    bool starts(std::string_view s) const { return t_.compare(i_, s.size(), s) == 0; }

    static std::string_view trim(std::string_view s) {
        size_t a = 0, b = s.size();
        while (a < b && isSpace(s[a])) ++a;
        while (b > a && isSpace(s[b - 1])) --b;
        return s.substr(a, b - a);
    }

    std::string name() {
        const size_t start = i_;
        while (i_ < t_.size() && isNameChar(t_[i_])) ++i_;
        return std::string(t_.substr(start, i_ - start));
    }

    std::string_view t_;
    size_t i_ = 0;
};

}  // namespace

std::optional<Document> parseXml(std::string_view text) {
    Reader r(text);
    Document doc;
    if (!r.element(doc.root)) return std::nullopt;
    return doc;
}

}  // namespace icf::svg
