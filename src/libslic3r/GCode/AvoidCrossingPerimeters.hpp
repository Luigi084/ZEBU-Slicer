#ifndef slic3r_AvoidCrossingPerimeters_hpp_
#define slic3r_AvoidCrossingPerimeters_hpp_

#include "../ExPolygon.hpp"
#include "../EdgeGrid.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/BoundingBox.hpp"
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>
#include "libslic3r/MultiMaterialSegmentation.hpp"

namespace Slic3r {

// Forward declarations.
class GCode;
class Layer;
class Point;

class AvoidCrossingPerimeters
{
public:
    // Routing around the objects vs. inside a single object.
    void        use_external_mp(bool use = true) { m_use_external_mp = use; };
    bool        used_external_mp() { return m_use_external_mp; }
    void        use_external_mp_once()  { m_use_external_mp_once = true; }
    bool        used_external_mp_once() { return m_use_external_mp_once; }
    void        disable_once()          { m_disabled_once = true; }
    bool        disabled_once() const   { return m_disabled_once; }
    void        reset_once_modifiers()  { m_use_external_mp_once = false; m_disabled_once = false; }

    void        init_layer(const Layer &layer);

    Polyline    travel_to(const GCode& gcodegen, const Point& point)
    {
        bool could_be_wipe_disabled;
        return this->travel_to(gcodegen, point, &could_be_wipe_disabled);
    }

    Polyline    travel_to(const GCode& gcodegen, const Point& point, bool* could_be_wipe_disabled);

    // ZEBU: a layer's boundaries depend on that layer only, so prepare_layer() builds them ahead of the serial
    // G-code generation, in parallel (it is thread-safe). travel_to() then uses them instead of building them.
    // The G-code is identical: same polygons, and the same grid whenever the travel stays within their extents
    // (otherwise travel_to() builds the grid as before, from the prepared polygons).
    void        prepare_layer(const Layer &layer);
    void        clear_prepared();

    struct Boundary {
        // Collection of boundaries used for detection of crossing perimeters for travels
        Polygons                        boundaries;
        // Bounding box of boundaries
        BoundingBoxf                    bbox;
        // Precomputed distances of all points in boundaries
        std::vector<std::vector<float>> boundaries_params;
        // Used for detection of intersection between line and any polygon from boundaries
        EdgeGrid::Grid                  grid;

        void clear()
        {
            boundaries.clear();
            boundaries_params.clear();
        }
    };

private:
    bool           m_use_external_mp { false };
    // just for the next travel move
    bool           m_use_external_mp_once { false };
    // this flag disables reduce_crossing_wall just for the next travel move
    // we enable it by default for the first travel move in print
    bool           m_disabled_once { true };

    // Lslices offseted by half an external perimeter width. Used for detection if line or polyline is inside of any polygon.
    ExPolygons               m_lslices_offset;
    std::vector<BoundingBox> m_lslices_offset_bboxes;
    // Used for detection of line or polyline is inside of any polygon.
    EdgeGrid::Grid m_grid_lslice;
    // Store all needed data for travels inside object
    Boundary m_internal;
    // Store all needed data for travels outside object
    Boundary m_external;

    // ZEBU: what init_layer() builds (the slices index), prepared ahead.
    struct SlicesIndex {
        ExPolygons               lslices_offset;
        std::vector<BoundingBox> lslices_offset_bboxes;
        EdgeGrid::Grid           grid;
    };
    // ZEBU: boundaries prepared by prepare_layer(), per layer.
    struct Prepared {
        double                    print_z { 0. };
        Polygons                  internal_polygons;
        BoundingBox               internal_extents;
        std::unique_ptr<Boundary> internal; // Built without merge points; null if there are no polygons.
        Polygons                  external_polygons;
        BoundingBox               external_extents;
        std::unique_ptr<Boundary> external;
        std::unique_ptr<SlicesIndex> slices;
    };
    struct PreparedStore {
        std::mutex                                                     mutex;
        std::unordered_map<const Layer *, std::shared_ptr<const Prepared>> layers;
    };
    std::shared_ptr<PreparedStore>  m_prepared { std::make_shared<PreparedStore>() };
    std::shared_ptr<const Prepared> find_prepared(const Layer &layer) const;
    // The boundaries travel_to() works with: m_internal / m_external, or a prepared one kept alive by *_hold.
    const Boundary                 *m_internal_active { nullptr };
    const Boundary                 *m_external_active { nullptr };
    std::shared_ptr<const Prepared> m_internal_hold;
    std::shared_ptr<const Prepared> m_external_hold;
    // The slices index in use: the members above, or a prepared one kept alive by m_slices_hold.
    const ExPolygons               *m_lslices_offset_used { &m_lslices_offset };
    const std::vector<BoundingBox> *m_lslices_bboxes_used { &m_lslices_offset_bboxes };
    const EdgeGrid::Grid           *m_grid_lslice_used    { &m_grid_lslice };
    std::shared_ptr<const Prepared> m_slices_hold;
};

} // namespace Slic3r

#endif // slic3r_AvoidCrossingPerimeters_hpp_
