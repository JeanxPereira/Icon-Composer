#include "Source/IconComposerFoundation/Json.h"
#include <algorithm>
#include <charconv>

namespace icf::json {
namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }

class Reader {
public:
    explicit Reader(std::string_view t) : t_(t) {}

    std::optional<Value> value() {
        skipSpace();
        if (i_ >= t_.size()) return std::nullopt;
        switch (t_[i_]) {
            case 'n': return literal("null", Value::null());
            case 't': return literal("true", Value::boolean(true));
            case 'f': return literal("false", Value::boolean(false));
            case '"': return string();
            case '[': return array();
            case '{': return object();
            default: return number();
        }
    }

    void skipSpace() {
        while (i_ < t_.size() &&
               (t_[i_] == ' ' || t_[i_] == '\t' || t_[i_] == '\n' || t_[i_] == '\r')) {
            ++i_;
        }
    }

    size_t offset() const { return i_; }
    size_t size() const { return t_.size(); }

private:
    std::optional<Value> literal(std::string_view word, Value v) {
        if (t_.substr(i_, word.size()) != word) return std::nullopt;
        i_ += word.size();
        return v;
    }

    std::optional<Value> string() {
        const size_t open = i_++;
        while (i_ < t_.size()) {
            if (t_[i_] == '\\') {
                i_ += 2;
                continue;
            }
            if (t_[i_] == '"') {
                std::string raw(t_.substr(open + 1, i_ - open - 1));
                ++i_;
                return Value::string(std::move(raw));
            }
            ++i_;
        }
        return std::nullopt;  // unterminated
    }

    // JSON's number grammar: -? int frac? exp?
    std::optional<Value> number() {
        const size_t start = i_;
        if (i_ < t_.size() && t_[i_] == '-') ++i_;
        const size_t intStart = i_;
        while (i_ < t_.size() && isDigit(t_[i_])) ++i_;
        if (i_ == intStart) return std::nullopt;
        if (i_ < t_.size() && t_[i_] == '.') {
            const size_t dot = i_++;
            while (i_ < t_.size() && isDigit(t_[i_])) ++i_;
            if (i_ == dot + 1) return std::nullopt;  // a dot with no digits after it
        }
        if (i_ < t_.size() && (t_[i_] == 'e' || t_[i_] == 'E')) {
            const size_t e = i_++;
            if (i_ < t_.size() && (t_[i_] == '+' || t_[i_] == '-')) ++i_;
            const size_t digits = i_;
            while (i_ < t_.size() && isDigit(t_[i_])) ++i_;
            if (i_ == digits) i_ = e;  // an `e` with no exponent stops the number
        }
        return Value::number(std::string(t_.substr(start, i_ - start)));
    }

    std::optional<Value> array() {
        ++i_;  // '['
        std::vector<Value> out;
        skipSpace();
        if (i_ < t_.size() && t_[i_] == ']') {
            ++i_;
            return Value::array(std::move(out));
        }
        for (;;) {
            auto v = value();
            if (!v) return std::nullopt;
            out.push_back(std::move(*v));
            skipSpace();
            if (i_ >= t_.size()) return std::nullopt;
            if (t_[i_] == ',') {
                ++i_;
                continue;
            }
            if (t_[i_] == ']') {
                ++i_;
                return Value::array(std::move(out));
            }
            return std::nullopt;
        }
    }

    std::optional<Value> object() {
        ++i_;  // '{'
        std::vector<Value::Member> out;
        skipSpace();
        if (i_ < t_.size() && t_[i_] == '}') {
            ++i_;
            return Value::object(std::move(out));
        }
        for (;;) {
            skipSpace();
            if (i_ >= t_.size() || t_[i_] != '"') return std::nullopt;
            auto key = string();
            if (!key) return std::nullopt;
            skipSpace();
            if (i_ >= t_.size() || t_[i_] != ':') return std::nullopt;
            ++i_;
            auto v = value();
            if (!v) return std::nullopt;
            out.emplace_back(key->rawString(), std::move(*v));
            skipSpace();
            if (i_ >= t_.size()) return std::nullopt;
            if (t_[i_] == ',') {
                ++i_;
                continue;
            }
            if (t_[i_] == '}') {
                ++i_;
                return Value::object(std::move(out));
            }
            return std::nullopt;
        }
    }

    std::string_view t_;
    size_t i_ = 0;
};

void writeInto(const Value& v, int depth, std::string& out) {
    const std::string pad(static_cast<size_t>(depth) * 2, ' ');
    const std::string padIn(static_cast<size_t>(depth + 1) * 2, ' ');
    switch (v.kind()) {
        case Value::Kind::Null:
            out += "null";
            return;
        case Value::Kind::Bool:
            out += v.boolean() ? "true" : "false";
            return;
        case Value::Kind::Number:
            out += v.number();
            return;
        case Value::Kind::String:
            out += '"';
            out += v.rawString();
            out += '"';
            return;
        case Value::Kind::Array: {
            out += "[\n";
            bool first = true;
            for (const auto& e : v.elements()) {
                if (!first) out += ",\n";
                first = false;
                out += padIn;
                writeInto(e, depth + 1, out);
            }
            out += '\n';
            out += pad;
            out += ']';
            return;
        }
        case Value::Kind::Object: {
            // `.sortedKeys`. Raw byte order is right for `.icon`, whose every
            // key is ASCII; it is NOT Swift's Unicode-aware String ordering, and
            // a non-ASCII key would be a case this writer has never seen.
            std::vector<const Value::Member*> sorted;
            sorted.reserve(v.members().size());
            for (const auto& m : v.members()) sorted.push_back(&m);
            std::sort(sorted.begin(), sorted.end(),
                      [](const Value::Member* a, const Value::Member* b) {
                          return a->first < b->first;
                      });
            out += "{\n";
            bool first = true;
            for (const auto* m : sorted) {
                if (!first) out += ",\n";
                first = false;
                out += padIn;
                out += '"';
                out += m->first;
                out += "\" : ";
                writeInto(m->second, depth + 1, out);
            }
            out += '\n';
            out += pad;
            out += '}';
            return;
        }
    }
}

}  // namespace

void Value::set(std::string key, Value v) {
    for (auto& m : members_) {
        if (m.first == key) {
            m.second = std::move(v);
            return;
        }
    }
    members_.emplace_back(std::move(key), std::move(v));
}

bool Value::erase(std::string_view key) {
    for (auto it = members_.begin(); it != members_.end(); ++it) {
        if (it->first == key) {
            members_.erase(it);
            return true;
        }
    }
    return false;
}

Value Value::number(double d) {
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof buf, d);
    return Value::number(std::string(buf, r.ptr));
}

std::optional<Value> parse(std::string_view text) {
    Reader r(text);
    auto v = r.value();
    if (!v) return std::nullopt;
    // The document is the WHOLE text. Stopping at the first complete value would
    // accept a truncated file with anything after it and call it a success.
    r.skipSpace();
    if (r.offset() != r.size()) return std::nullopt;
    return v;
}

std::string write(const Value& value) {
    std::string out;
    writeInto(value, 0, out);
    return out;
}

}  // namespace icf::json
