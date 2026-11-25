// ZEBU Slicer: the underside of a printer's toolhead, as a height map around the nozzle tip.
//
// height(dx, dy) is how high the lowest part of the toolhead is above the nozzle tip at the horizontal
// offset (dx, dy) from the nozzle (mm). Printed material at that offset must stay below it, or the head
// hits it. Files: resources/zebu/heads/<id>.json, schema "zebu.head_underside.v1" (see the files).
#ifndef slic3r_Zebu_HeadUnderside_hpp_
#define slic3r_Zebu_HeadUnderside_hpp_

#include <cstdint>
#include <string>
#include <vector>

namespace Slic3r { namespace Zebu {

class HeadUnderside
{
public:
    // Loads a head file. Returns false and fills *error (if given) when the file is missing or malformed.
    bool load(const std::string &path, std::string *error = nullptr);
    // From the file's text, for tests.
    bool parse(const std::string &json_text, std::string *error = nullptr);

    bool               empty() const { return m_heights.empty(); }
    const std::string &id() const { return m_id; }
    const std::string &name() const { return m_name; }
    double             cell() const { return m_cell; }
    // Everything not in the raster is at least this high above the tip.
    double             cap() const { return m_cap; }

    // Height of the head's underside above the nozzle tip at offset (dx, dy) from the nozzle, in mm.
    double height(double dx, double dy) const;

    // Lowest point of the head's underside over the square [cx - half, cx + half] x [cy - half, cy + half].
    double lowest_in_square(double cx, double cy, double half) const;

    // For a check on a square grid of step `grid`: every grid offset (di, dj) from the nozzle cell at which
    // the head is lower than below_height, with the lowest point over the square of half-size `grid` around
    // the offset (a nozzle anywhere in its cell and material anywhere in its cell can be that far apart), so
    // never less safe than the file. Offsets closer than min_radius (the nozzle's own cone, which the slope
    // limit covers) are left out.
    struct Offset
    {
        int    di, dj;
        double height;
    };
    std::vector<Offset> offsets_below(double below_height, double grid, double min_radius) const;

private:
    std::string           m_id;
    std::string           m_name;
    double                m_cell { 0. };
    double                m_origin_x { 0. }, m_origin_y { 0. };
    int                   m_nx { 0 }, m_ny { 0 };
    double                m_cap { 0. };
    std::vector<uint16_t> m_heights; // 0.01 mm, 65535 = at least m_cap
};

}} // namespace Slic3r::Zebu

#endif
