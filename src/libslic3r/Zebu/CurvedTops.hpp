// ZEBU Slicer: curved (non-planar) top surfaces, analysis stage.
//
// Finds the top surfaces of a mesh that could be printed following their shape instead of as stair
// steps, and checks each against the toolhead (HeadUnderside): the last `curved_layers` layers of such a
// surface are printed once the planar layers have reached its top; while the nozzle follows the surface,
// nothing already printed may come closer than `margin` to the head's underside.
// CurvedTopsSlicing builds the curved layers of the surfaces found printable here.
#ifndef slic3r_Zebu_CurvedTops_hpp_
#define slic3r_Zebu_CurvedTops_hpp_

#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "HeadUnderside.hpp"

namespace Slic3r { namespace Zebu {

// A triangle mesh in printer coordinates (mm), already placed on the bed.
struct SimpleMesh
{
    std::vector<std::array<float, 3>> vertices;
    std::vector<std::array<int, 3>>   faces;
};

struct CurvedTopsParams
{
    double max_angle_deg { 15. }; // Steepest slope printed curved (the nozzle's cone allows ~18° on the G3D head).
    double min_angle_deg { 0.5 }; // A patch flatter than this everywhere is already printed flat: not worth it.
    double margin { 0.4 };        // Gap kept between the head and the print (mm).
    double layer_height { 0.2 };
    int    curved_layers { 4 };   // Layers following the surface (usually the top shell layers).
    double grid { 0.5 };          // Resolution of the collision check (mm).
    double near_radius { 4.0 };   // Nozzle zone: there the patch's own surface is covered by the slope limit, and other
                                  // material only has to stay below the head (no margin: the nozzle touches the print).
    // Tops of the object's layers, rising: the curved layers are printed with the layer holding the patch's top (the
    // last one sliced below it, so reaching it within half a layer). Empty: layers of layer_height from z = 0.
    std::vector<double> layer_tops;
};

// Other objects on the plate, which the head must clear too.
struct Obstacles
{
    SimpleMesh mesh;
    // Printed one object after another: the others may be complete already. Otherwise they are printed layer by
    // layer with the object, so no higher than the layer the curved surfaces are printed with.
    bool       full_height { false };
};

struct CurvedTopPatch
{
    std::vector<int> facets;           // Mesh faces making the patch.
    double           area { 0. };      // mm², projected on the bed.
    double           z_min { 0. }, z_max { 0. };
    double           max_slope_deg { 0. };
    double           clearance { 0. };   // Top curved layer: smallest gap left beyond the margin (mm); < 0: the head would hit.
    double           ok_fraction { 0. }; // Share of the patch where the top curved layer passes the check.
    double           ok_fraction_lower { 0. }; // Same for the lowest curved layer (the others lie between).
    double           print_z { 0. };           // Top of the layer the curved layers are printed with (>= z_max - layer_height / 2).
    bool             printable { false };      // Every curved layer passes everywhere.
};

std::vector<CurvedTopPatch> analyze_curved_tops(const SimpleMesh &mesh, const HeadUnderside &head, const CurvedTopsParams &params,
                                                const Obstacles *obstacles = nullptr);

// Height of a surface on a square grid (mm): cell (i, j) covers [x0 + i step, x0 + (i + 1) step) x [y0 + j step, ...).
struct HeightField
{
    double             x0 { 0. }, y0 { 0. }, step { 0.1 };
    int                nx { 0 }, ny { 0 };
    std::vector<float> z; // NaN: no surface over the cell

    bool  empty() const { return z.empty(); }
    bool  valid(int i, int j) const { return i >= 0 && j >= 0 && i < nx && j < ny && !std::isnan(z[size_t(j) * size_t(nx) + size_t(i)]); }
    float at(int i, int j) const { return z[size_t(j) * size_t(nx) + size_t(i)]; }
    int   cell_x(double x) const { return int(std::floor((x - x0) / step)); }
    int   cell_y(double y) const { return int(std::floor((y - y0) / step)); }
    // Height of the cell holding (x, y), NaN if none.
    double cell_height(double x, double y) const;
    // Bilinear between the cell centres around (x, y), over the valid ones; NaN if none of the four is valid.
    double sample(double x, double y) const;
};

// The highest of the given faces over each cell centre; then cells next to the surface, up to `extend` cells away,
// get the height of their nearest neighbour (so that points on the surface's outline find a height).
HeightField rasterize_facets(const SimpleMesh &mesh, const std::vector<int> &facets, double step, int extend);

// How many of `wanted` curved layers fit under a surface whose lowest point is at `z_min`: the planar layers end at
// least half a layer below them, and the first layer stays whole. 0: none.
int curved_layers_fitting(double z_min, double first_layer_top, double layer_height, int wanted);

// Path of a head file from the printer's "zebu_head_model" value: an id of resources/zebu/heads, or a full path.
std::string head_model_path(const std::string &value, const std::string &resources_dir);

}} // namespace Slic3r::Zebu

#endif
