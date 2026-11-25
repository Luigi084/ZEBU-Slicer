// ZEBU Slicer: curved (non-planar) top surfaces, analysis stage.
//
// Finds the top surfaces of a mesh that could be printed following their shape instead of as stair
// steps, and checks each against the toolhead (HeadUnderside): the last `curved_layers` layers of such a
// surface are printed once the planar layers have reached its top; while the nozzle follows the surface,
// nothing already printed may come closer than `margin` to the head's underside.
// This stage only analyses: it does not change the slicing or the G-code.
#ifndef slic3r_Zebu_CurvedTops_hpp_
#define slic3r_Zebu_CurvedTops_hpp_

#include <array>
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
    bool             printable { false };      // The whole top curved layer passes; lower layers may need trimming at the edges.
};

std::vector<CurvedTopPatch> analyze_curved_tops(const SimpleMesh &mesh, const HeadUnderside &head, const CurvedTopsParams &params);

}} // namespace Slic3r::Zebu

#endif
