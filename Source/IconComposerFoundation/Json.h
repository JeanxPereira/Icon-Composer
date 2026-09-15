#pragma once
// A JSON reader and writer for `.icon` documents.
//
// Not a general-purpose JSON library, and deliberately so. The acceptance gate
// is byte-exactness against the 135 documents Apple's own `JSONEncoder` wrote
// (doc 01), and that rules out the usual design: a scalar decoded into a native
// type cannot be spelled back the way it arrived. `1` returns as `1.0`, and the
// corpus carries numbers out to twenty decimal places that no shortest-form
// printer reproduces.
//
// So scalars keep their SOURCE TEXT and are converted on demand. A number is its
// lexeme; a string is the raw bytes between its quotes, escapes and all.
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace icf::json {

class Value {
public:
    using Member = std::pair<std::string, Value>;

    enum class Kind { Null, Bool, Number, String, Array, Object };

    static Value null() { return Value(Kind::Null); }
    static Value boolean(bool b) {
        Value v(Kind::Bool);
        v.bool_ = b;
        return v;
    }
    static Value number(std::string lexeme) {
        Value v(Kind::Number);
        v.text_ = std::move(lexeme);
        return v;
    }
    static Value string(std::string raw) {
        Value v(Kind::String);
        v.text_ = std::move(raw);
        return v;
    }
    static Value array(std::vector<Value> elements) {
        Value v(Kind::Array);
        v.elements_ = std::move(elements);
        return v;
    }
    static Value object(std::vector<Member> members) {
        Value v(Kind::Object);
        v.members_ = std::move(members);
        return v;
    }

    Kind kind() const { return kind_; }
    bool boolean() const { return bool_; }
    // The number's lexeme, exactly as it was written.
    const std::string& number() const { return text_; }
    // The bytes between the quotes, exactly as they were written.
    const std::string& rawString() const { return text_; }
    const std::vector<Value>& elements() const { return elements_; }
    const std::vector<Member>& members() const { return members_; }

    // The member named `key`, or nullptr. Keys are compared raw, which is right
    // for `.icon`: every key the format uses is ASCII kebab-case.
    const Value* find(std::string_view key) const {
        for (const auto& m : members_) {
            if (m.first == key) return &m.second;
        }
        return nullptr;
    }

    // ---- mutation ----------------------------------------------------------
    // Since 2026-09-13 (spec 13/09 §4.1). The lexeme stays the truth: mutating one
    // member never re-spells a sibling, which is what keeps the 135 byte-exact
    // documents byte-exact after an edit somewhere else in the tree.
    std::vector<Value>& elements() { return elements_; }
    std::vector<Member>& members() { return members_; }
    Value* find(std::string_view key) {
        for (auto& m : members_) {
            if (m.first == key) return &m.second;
        }
        return nullptr;
    }
    // Replaces the member named `key`, or appends one. `write` sorts keys, so
    // where it lands does not reach the bytes.
    void set(std::string key, Value v);
    bool erase(std::string_view key);

    // `[ART]` The spelling Apple's encoder gives a Double: the shortest string
    // that round-trips, and no ".0" on an integral value -- 1,361 non-integer
    // and 1,152 integer tokens over 145 documents, every one (spec 13/09 §2.2).
    // `std::to_chars` without a format is exactly that.
    static Value number(double d);

private:
    explicit Value(Kind k) : kind_(k) {}

    Kind kind_;
    bool bool_ = false;
    std::string text_;
    std::vector<Value> elements_;
    std::vector<Member> members_;
};

std::optional<Value> parse(std::string_view text);

// Apple's canonical form: two-space indent, `" : "` between key and value, keys
// sorted, no trailing newline.
std::string write(const Value& value);

}  // namespace icf::json
