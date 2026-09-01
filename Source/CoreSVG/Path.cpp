#include "Source/CoreSVG/Path.h"

#include <charconv>
#include <cmath>

namespace icf::svg {
namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
bool isCommand(char c) {
    switch (c) {
        case 'M': case 'm': case 'L': case 'l': case 'H': case 'h':
        case 'V': case 'v': case 'C': case 'c': case 'S': case 's':
        case 'Q': case 'q': case 'T': case 't': case 'A': case 'a':
        case 'Z': case 'z':
            return true;
        default:
            return false;
    }
}

class Reader {
public:
    explicit Reader(std::string_view d) : d_(d) {}

    void skipSeparators() {
        while (i_ < d_.size() && (isSpace(d_[i_]) || d_[i_] == ',')) ++i_;
    }

    bool atEnd() {
        skipSeparators();
        return i_ >= d_.size();
    }

    char peek() const { return i_ < d_.size() ? d_[i_] : '\0'; }
    void take() { ++i_; }
    size_t offset() const { return i_; }

    // A number that starts here, or nothing. The exponent is not decoration:
    // 46 of the corpus's numbers carry one (doc 04 §2), and a scanner that stops
    // at the `e` reads `1e2` as 1 and then trips over a stray command letter.
    bool number(double& out) {
        skipSeparators();
        const size_t start = i_;
        if (i_ < d_.size() && (d_[i_] == '+' || d_[i_] == '-')) ++i_;
        const size_t digitsStart = i_;
        while (i_ < d_.size() && isDigit(d_[i_])) ++i_;
        if (i_ < d_.size() && d_[i_] == '.') {
            ++i_;
            while (i_ < d_.size() && isDigit(d_[i_])) ++i_;
        }
        if (i_ == digitsStart || (i_ == digitsStart + 1 && d_[digitsStart] == '.')) {
            i_ = start;
            return false;  // no digits at all
        }
        if (i_ < d_.size() && (d_[i_] == 'e' || d_[i_] == 'E')) {
            const size_t e = i_++;
            if (i_ < d_.size() && (d_[i_] == '+' || d_[i_] == '-')) ++i_;
            const size_t expDigits = i_;
            while (i_ < d_.size() && isDigit(d_[i_])) ++i_;
            if (i_ == expDigits) i_ = e;  // a bare `e` is not part of the number
        }
        const std::string_view text = d_.substr(start, i_ - start);
        auto r = std::from_chars(text.data(), text.data() + text.size(), out);
        if (r.ec != std::errc() || r.ptr != text.data() + text.size()) {
            i_ = start;
            return false;
        }
        return true;
    }

    // Is a number the next thing? That is what says a bare coordinate pair
    // continues the previous command instead of starting a new one.
    bool numberAhead() {
        skipSeparators();
        const char c = peek();
        return isDigit(c) || c == '+' || c == '-' || c == '.';
    }

private:
    std::string_view d_;
    size_t i_ = 0;
};

}  // namespace

PathResult parsePath(std::string_view d) {
    Reader r(d);
    Path path;

    Point current{};       // where the pen is
    Point subpathStart{};  // where `Z` returns to
    Point lastControl{};   // the second control of the previous cubic, for `S`
    bool lastWasCubic = false;
    char command = 0;

    if (r.atEnd()) return PathResult(path);

    for (;;) {
        if (r.atEnd()) break;

        const char c = r.peek();
        if (isCommand(c)) {
            command = c;
            r.take();
        } else if (command == 0) {
            // Data before any command, or a path that does not begin with a
            // moveto -- SVG requires one, and without it there is no current
            // point for anything else to be relative to.
            return PathResult(PathError{0, r.offset()});
        } else if (!r.numberAhead()) {
            return PathResult(PathError{0, r.offset()});
        } else if (command == 'M') {
            command = 'L';  // a repeated moveto is a lineto
        } else if (command == 'm') {
            command = 'l';
        }

        if (command != 'M' && command != 'm' && path.segments.empty()) {
            return PathResult(PathError{0, r.offset()});
        }

        const bool relative = command >= 'a';
        const Point base = relative ? current : Point{0, 0};
        Segment seg;

        switch (command) {
            case 'M': case 'm': {
                double x, y;
                if (!r.number(x) || !r.number(y)) return PathResult(PathError{0, r.offset()});
                current = {base.x + x, base.y + y};
                subpathStart = current;
                seg.kind = SegmentKind::Move;
                seg.p[0] = current;
                lastWasCubic = false;
                break;
            }
            case 'L': case 'l': {
                double x, y;
                if (!r.number(x) || !r.number(y)) return PathResult(PathError{0, r.offset()});
                current = {base.x + x, base.y + y};
                seg.kind = SegmentKind::Line;
                seg.p[0] = current;
                lastWasCubic = false;
                break;
            }
            case 'H': case 'h': {
                double x;
                if (!r.number(x)) return PathResult(PathError{0, r.offset()});
                current = {base.x + x, current.y};
                seg.kind = SegmentKind::Line;
                seg.p[0] = current;
                lastWasCubic = false;
                break;
            }
            case 'V': case 'v': {
                double y;
                if (!r.number(y)) return PathResult(PathError{0, r.offset()});
                current = {current.x, base.y + y};
                seg.kind = SegmentKind::Line;
                seg.p[0] = current;
                lastWasCubic = false;
                break;
            }
            case 'C': case 'c': {
                double x1, y1, x2, y2, x, y;
                if (!r.number(x1) || !r.number(y1) || !r.number(x2) || !r.number(y2) ||
                    !r.number(x) || !r.number(y)) {
                    return PathResult(PathError{0, r.offset()});
                }
                seg.kind = SegmentKind::Cubic;
                seg.p[0] = {base.x + x1, base.y + y1};
                seg.p[1] = {base.x + x2, base.y + y2};
                seg.p[2] = {base.x + x, base.y + y};
                lastControl = seg.p[1];
                current = seg.p[2];
                lastWasCubic = true;
                break;
            }
            case 'S': case 's': {
                double x2, y2, x, y;
                if (!r.number(x2) || !r.number(y2) || !r.number(x) || !r.number(y)) {
                    return PathResult(PathError{0, r.offset()});
                }
                // The first control is the previous cubic's second control
                // reflected about the current point -- and the CURRENT POINT
                // when the previous segment was not a cubic. Reflecting a stale
                // control after a line draws a curve nobody wrote.
                seg.kind = SegmentKind::Cubic;
                seg.p[0] = lastWasCubic
                               ? Point{2 * current.x - lastControl.x, 2 * current.y - lastControl.y}
                               : current;
                seg.p[1] = {base.x + x2, base.y + y2};
                seg.p[2] = {base.x + x, base.y + y};
                lastControl = seg.p[1];
                current = seg.p[2];
                lastWasCubic = true;
                break;
            }
            case 'Z': case 'z': {
                seg.kind = SegmentKind::Close;
                current = subpathStart;
                lastWasCubic = false;
                break;
            }
            default:
                // Q, T, A and their relatives. Named, not approximated: the
                // coverage report is what decides whether they are worth having.
                return PathResult(PathError{command, r.offset()});
        }

        path.segments.push_back(seg);
    }

    return PathResult(path);
}

}  // namespace icf::svg
