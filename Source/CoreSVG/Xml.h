#pragma once
// Just enough XML to read an SVG.
//
// Not a conforming XML processor and not trying to be: no DTD resolution, no
// external entities, no namespace resolution. A prefix stays part of the name,
// because that is how SVG uses it -- `xlink:href` is looked up by that spelling
// and never by a URI.
//
// What it does refuse is anything not well-formed. A reader that recovers from
// a broken document invents structure the file does not have, and every question
// asked of it afterwards is answered about that invention.
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace icf::svg {

struct Element {
    std::string name;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::vector<Element> children;
    // The character data directly inside this element. Kept for `<style>` and
    // `<title>`; whitespace between child elements is not.
    std::string text;

    const std::string* attribute(std::string_view name) const {
        for (const auto& a : attributes) {
            if (a.first == name) return &a.second;
        }
        return nullptr;
    }
    const Element* child(std::string_view name) const {
        for (const auto& c : children) {
            if (c.name == name) return &c;
        }
        return nullptr;
    }
};

struct Document {
    Element root;
};

std::optional<Document> parseXml(std::string_view text);

}  // namespace icf::svg
