#pragma once
// SVG path data, normalised.
//
// Everything becomes absolute Move / Line / Cubic / Close. A consumer should not
// have to know that `h` or `s` existed, and a rasteriser should not carry
// sixteen cases -- the corpus uses thirteen distinct command letters and they
// all reduce to these four.
//
// Arcs are the exception and they are REFUSED, not approximated: `A` occurs
// twice in the 149 SVGs measured (doc 04 §2), and a curve invented to stand in
// for one would be a difference from the target that nothing would report.
#include <optional>
#include <string_view>
#include <vector>

namespace icf::svg {

struct Point {
    double x = 0;
    double y = 0;
};

enum class SegmentKind { Move, Line, Cubic, Close };

struct Segment {
    SegmentKind kind = SegmentKind::Move;
    // Move/Line use p[0]; Cubic uses p[0] and p[1] as controls and p[2] as the
    // end point; Close uses none.
    Point p[3];
};

struct Path {
    std::vector<Segment> segments;
};

struct PathError {
    // The command letter this reader does not implement, or 0 when the data was
    // malformed rather than merely beyond it.
    char unsupportedCommand = 0;
    size_t offset = 0;
};

// A path, or what stopped it. `Result`-shaped rather than exception-shaped: this
// tower is in the layer the tests hammer.
class PathResult {
public:
    PathResult(Path p) : path_(std::move(p)), ok_(true) {}
    PathResult(PathError e) : error_(e), ok_(false) {}

    bool has_value() const { return ok_; }
    explicit operator bool() const { return ok_; }
    const Path& operator*() const { return path_; }
    const Path* operator->() const { return &path_; }
    const PathError& error() const { return error_; }

private:
    Path path_;
    PathError error_;
    bool ok_;
};

PathResult parsePath(std::string_view d);

}  // namespace icf::svg
