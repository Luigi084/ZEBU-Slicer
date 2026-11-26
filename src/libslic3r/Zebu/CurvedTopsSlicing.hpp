// ZEBU Slicer: curved (non-planar) top surfaces, slicing stage.
//
// For each top surface that the analysis (CurvedTops) finds printable with the printer's toolhead:
// - the planar layers stop `layers` layer heights below the surface: the part of them above is cut out of their
//   slices before the perimeters are made (PrintObject::zebu_plan_curved_tops(), in the slicing step);
// - `layers` curved layers follow the surface over its outline, the lowest one filling the steps left by the planar
//   layers (its flow follows its thickness); they are printed with the first layer reaching the top of the surface,
//   after everything else of the object on that layer (PrintObject::zebu_make_curved_tops(), Layer::zebu_curved_tops).
// Everything here only runs with the "zebu_curved_tops" option and a toolhead model: otherwise the slicing and the
// G-code are those of a print without curved tops.
#ifndef slic3r_Zebu_CurvedTopsSlicing_hpp_
#define slic3r_Zebu_CurvedTopsSlicing_hpp_

#include <cstddef>
#include <vector>

#include "../ExPolygon.hpp"
#include "CurvedTops.hpp"

namespace Slic3r { namespace Zebu {

// One top surface printed curved, in the frame of its PrintObject's layers (x, y as the layers' polygons, z as
// Layer::print_z).
struct CurvedTopPlan
{
    ExPolygons  footprint;         // The surface seen from above (scaled).
    HeightField surface;           // Its height (mm).
    int         layers { 4 };      // Curved layers.
    double      layer_height { 0.2 };
    double      z_min { 0. }, z_max { 0. };
    size_t      host_layer { 0 };  // Index in PrintObject::layers() of the layer they are printed with.

    // A planar layer of top `print_z` keeps the point under a surface of height s when it ends at least half a layer
    // below the curved layers, whose lowest one starts at s - layers * layer_height.
    bool planar_keeps(double s, double print_z) const { return print_z <= s - layers * layer_height + 0.5 * layer_height + 1e-6; }
};

}} // namespace Slic3r::Zebu

#endif
