// ZEBU Slicer: curved (non-planar) top surfaces, slicing stage (see CurvedTopsSlicing.hpp).
#include "CurvedTopsSlicing.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

#include "../ClipperUtils.hpp"
#include "../ExtrusionEntity.hpp"
#include "../ExtrusionEntityCollection.hpp"
#include "../Fill/FillBase.hpp"
#include "../Flow.hpp"
#include "../I18N.hpp"
#include "../Layer.hpp"
#include "../Print.hpp"
#include "../Surface.hpp"
#include "../Utils.hpp"

#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

namespace {

using Zebu::CurvedTopPlan;
using Zebu::HeightField;
using Zebu::SimpleMesh;

// The model parts of `object`, in the frame its layers were sliced in, shifted by `shift` (mm), with z as print_z.
void append_object_mesh(SimpleMesh &out, const PrintObject &object, const Vec2d &shift)
{
    const Transform3d trafo_object = object.trafo_centered();
    const double      dz           = object.slicing_parameters().object_print_z_min;
    for (const ModelVolume *volume : object.model_object()->volumes) {
        if (!volume->is_model_part())
            continue;
        const Transform3d           trafo  = trafo_object * volume->get_matrix();
        const indexed_triangle_set &its    = volume->mesh().its;
        const int                   offset = int(out.vertices.size());
        for (const auto &v : its.vertices) {
            const Vec3d p = trafo * v.cast<double>();
            out.vertices.push_back({float(p.x() + shift.x()), float(p.y() + shift.y()), float(p.z() + dz)});
        }
        // A mirroring transformation turns the faces inside out: keep them facing out.
        const bool flip = trafo.linear().determinant() < 0.;
        for (const auto &f : its.indices)
            out.faces.push_back(flip ? std::array<int, 3>{offset + f[0], offset + f[2], offset + f[1]} :
                                       std::array<int, 3>{offset + f[0], offset + f[1], offset + f[2]});
    }
}

// The cells of `field` (and only those) where pred(height) holds, as polygons (scaled): a rectangle per run of cells
// in a row, merged.
template<class Pred> ExPolygons field_mask(const HeightField &field, Pred pred)
{
    Polygons rects;
    for (int j = 0; j < field.ny; ++j) {
        int run = -1;
        for (int i = 0; i <= field.nx; ++i) {
            const bool in = i < field.nx && field.valid(i, j) && pred(double(field.at(i, j)));
            if (in && run < 0)
                run = i;
            else if (!in && run >= 0) {
                const coord_t x0 = scaled<coord_t>(field.x0 + run * field.step), x1 = scaled<coord_t>(field.x0 + i * field.step);
                const coord_t y0 = scaled<coord_t>(field.y0 + j * field.step), y1 = scaled<coord_t>(field.y0 + (j + 1) * field.step);
                rects.emplace_back(Points{{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}});
                run = -1;
            }
        }
    }
    return union_ex(rects);
}

// One filament for the whole object (1 based), or 0. The curved layers are printed with it.
unsigned int single_filament(const PrintObject &object)
{
    unsigned int filament = 0;
    for (size_t r = 0; r < object.num_printing_regions(); ++r) {
        const PrintRegionConfig &c = object.printing_region(r).config();
        for (const int id : {c.outer_wall_filament_id.value, c.inner_wall_filament_id.value, c.sparse_infill_filament_id.value,
                             c.internal_solid_filament_id.value, c.top_surface_filament_id.value, c.bottom_surface_filament_id.value}) {
            if (id <= 0 || (filament != 0 && unsigned(id) != filament))
                return 0;
            filament = unsigned(id);
        }
    }
    return filament;
}

// A 2D polyline lifted onto the curved layer `offset` (mm, <= 0) below the surface, as offsets from `host_z`, cut into
// pieces of at most `step` mm.
Polyline3 lift_onto(const Points &points, const HeightField &surface, double offset, double host_z, double step)
{
    Polyline3 out;
    auto push = [&](const Point &p) {
        double s = surface.sample(unscale<double>(p.x()), unscale<double>(p.y()));
        if (std::isnan(s))
            s = host_z; // Cannot happen inside the footprint: its outline is covered by extending the field.
        const Point3 p3(p.x(), p.y(), scaled<coord_t>(s + offset - host_z));
        if (out.points.empty() || out.points.back() != p3)
            out.points.emplace_back(p3);
    };
    for (size_t k = 0; k + 1 < points.size(); ++k) {
        const Vec2d  a = points[k].cast<double>(), b = points[k + 1].cast<double>();
        const double len = (b - a).norm();
        const int    n   = std::max(1, int(std::ceil(len / scaled<double>(step))));
        for (int i = 0; i < n; ++i)
            push((a + (b - a) * (double(i) / n)).cast<coord_t>());
    }
    if (!points.empty())
        push(points.back());
    return out;
}

} // namespace

double PrintObject::zebu_curved_top_base(const Point &pt) const
{
    const double x = unscale<double>(pt.x()), y = unscale<double>(pt.y());
    for (const CurvedTopPlan &plan : m_zebu_curved_tops) {
        const double s = plan.surface.cell_height(x, y);
        if (std::isnan(s))
            continue;
        // The highest planar layer kept under the point (the same rule as zebu_plan_curved_tops()).
        double base = std::numeric_limits<double>::quiet_NaN();
        for (const Layer *layer : m_layers) {
            if (!plan.planar_keeps(s, layer->print_z))
                break;
            base = layer->print_z;
        }
        return base;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

void PrintObject::zebu_plan_curved_tops()
{
    m_zebu_curved_tops.clear();
    if (!m_config.zebu_curved_tops.value || m_layers.size() < 2)
        return;
    const PrintConfig &print_config = m_print->config();
    auto               warn         = [this](const std::string &message) {
        BOOST_LOG_TRIVIAL(warning) << "ZEBU curved tops: " << message;
        this->active_step_add_warning(PrintStateBase::WarningLevel::NON_CRITICAL, message);
    };
    if (print_config.zebu_head_model.value.empty()) {
        warn(L("Curved top surfaces (ZEBU): this printer has no toolhead model, the top surfaces are printed flat. "
               "Choose one in the printer settings."));
        return;
    }
    bool zaa = false;
    for (size_t r = 0; r < this->num_printing_regions(); ++r)
        zaa |= this->printing_region(r).config().zaa_enabled.value;
    if (print_config.spiral_mode.value || print_config.belt_printer.value || zaa || m_model_object->is_mm_painted() ||
        single_filament(*this) == 0) {
        warn(L("Curved top surfaces (ZEBU) are not available with spiral vase, belt printers, Z contouring or an object "
               "printed with several filaments: the top surfaces are printed flat."));
        return;
    }
    Zebu::HeadUnderside head;
    std::string         error;
    if (!head.load(Zebu::head_model_path(print_config.zebu_head_model.value, resources_dir()), &error)) {
        warn((boost::format(L("Curved top surfaces (ZEBU): the toolhead model cannot be read (%1%), the top surfaces are "
                              "printed flat.")) % error).str());
        return;
    }

    const double lh = m_config.layer_height.value;
    const int    N  = std::max(1, m_config.zebu_curved_top_layers.value);
    Zebu::CurvedTopsParams params;
    params.max_angle_deg = std::min(m_config.zebu_curved_top_angle.value, print_config.zebu_curved_top_max_angle.value);
    params.margin        = print_config.zebu_curved_top_margin.value;
    params.layer_height  = lh;
    params.curved_layers = N;
    for (const Layer *layer : m_layers)
        params.layer_tops.push_back(layer->print_z);

    // Every place this object is printed at (its instances, and those of the objects printing its layers), each
    // against everything else on the plate.
    std::vector<std::pair<const PrintObject *, size_t>> placements, subjects;
    for (const PrintObject *object : m_print->objects())
        for (size_t i = 0; i < object->instances().size(); ++i) {
            placements.emplace_back(object, i);
            if (object == this || object->get_shared_object() == this)
                subjects.emplace_back(object, i);
        }
    SimpleMesh  mesh;
    append_object_mesh(mesh, *this, Vec2d::Zero());
    std::vector<Zebu::CurvedTopPatch> patches;
    std::vector<char>                 printable;
    for (size_t s = 0; s < subjects.size(); ++s) {
        // In the frame of this object's layers at that place.
        const Vec2d     place = unscale(subjects[s].first->instances()[subjects[s].second].shift);
        Zebu::Obstacles obstacles;
        obstacles.full_height = print_config.print_sequence.value == PrintSequence::ByObject;
        for (const auto &[object, i] : placements)
            if (object != subjects[s].first || i != subjects[s].second)
                append_object_mesh(obstacles.mesh, *object, unscale(object->instances()[i].shift) - place);
        std::vector<Zebu::CurvedTopPatch> here = Zebu::analyze_curved_tops(mesh, head, params, &obstacles);
        if (s == 0) {
            patches = std::move(here);
            printable.assign(patches.size(), 1);
            for (size_t p = 0; p < patches.size(); ++p)
                printable[p] = patches[p].printable;
        } else
            // Same mesh, so the same patches in the same order.
            for (size_t p = 0; p < patches.size() && p < here.size(); ++p)
                printable[p] &= here[p].printable;
    }

    int skipped_head = 0, skipped_layers = 0, skipped_thin = 0;
    for (size_t p = 0; p < patches.size(); ++p) {
        const Zebu::CurvedTopPatch &patch = patches[p];
        if (!printable[p]) {
            ++skipped_head;
            continue;
        }
        CurvedTopPlan plan;
        plan.layers       = N;
        plan.layer_height = lh;
        plan.z_min        = patch.z_min;
        plan.z_max        = patch.z_max;
        plan.surface      = Zebu::rasterize_facets(mesh, patch.facets, 0.1, 3);
        {
            Polygons triangles;
            for (int f : patch.facets) {
                Polygon t;
                for (int k = 0; k < 3; ++k) {
                    const auto &v = mesh.vertices[mesh.faces[f][k]];
                    t.points.emplace_back(scaled<coord_t>(v[0]), scaled<coord_t>(v[1]));
                }
                if (t.area() < 0)
                    t.reverse();
                if (t.area() > 0)
                    triangles.emplace_back(std::move(t));
            }
            plan.footprint = union_ex(triangles);
        }
        // The layer printing the curved layers: the one holding the top of the surface (the analysis chose it).
        auto host = std::lower_bound(params.layer_tops.begin(), params.layer_tops.end(), patch.print_z - 1e-6);
        if (host == params.layer_tops.end() || plan.footprint.empty() || plan.surface.empty()) {
            ++skipped_layers;
            continue;
        }
        plan.host_layer = size_t(host - params.layer_tops.begin());
        // Like the planar slicing, nothing above that layer's top (at most half a layer is lost): so the travels at
        // the height of the layers clear the curved layers.
        for (float &z : plan.surface.z)
            if (!std::isnan(z))
                z = std::min(z, float(*host));
        plan.z_max = std::min(plan.z_max, *host);

        // The planar layers it touches: from one layer below the lowest curved layer up to the host layer, none of
        // them the first layer. Their heights may vary ("precise Z height", variable layer height): the lowest curved
        // layer fills the step left by the last one, so they stay below 1.5 layer heights (it is then at most twice
        // as thick as the others).
        const double bottom = patch.z_min - (N + 1) * lh;
        bool         ok     = bottom > m_layers.front()->print_z + 1e-6;
        for (size_t l = 0; ok && l <= plan.host_layer; ++l)
            if (m_layers[l]->print_z > bottom && m_layers[l]->height > 1.5 * lh + 1e-6)
                ok = false;
        if (!ok) {
            ++skipped_layers;
            continue;
        }
        // The object must be solid under the surface down there: where a layer lies wholly below the surface, its
        // slices cover the footprint (the curved layers have the planar ones to stand on).
        for (size_t l = 0; ok && l <= plan.host_layer; ++l) {
            const Layer &layer = *m_layers[l];
            if (layer.print_z <= bottom)
                continue;
            const double     top = layer.print_z;
            const ExPolygons below_surface =
                intersection_ex(plan.footprint, field_mask(plan.surface, [top](double s) { return s > top; }));
            if (below_surface.empty())
                continue;
            const ExPolygons missing = diff_ex(offset_ex(below_surface, -float(scale_(0.3))), layer.lslices);
            double           area    = 0.;
            for (const ExPolygon &e : missing)
                area += e.area();
            if (area > scale_(1.) * scale_(1.) * 0.5)
                ok = false;
        }
        if (!ok) {
            ++skipped_thin;
            continue;
        }

        // Cut the curved layers' room out of the planar layers: under the surface, above the planar layers' end.
        for (size_t l = 0; l <= plan.host_layer; ++l) {
            Layer       &layer = *m_layers[l];
            const double top   = layer.print_z;
            if (layer.print_z <= bottom)
                continue;
            const ExPolygons cut = intersection_ex(plan.footprint, field_mask(plan.surface, [&plan, top](double s) {
                                                       return !plan.planar_keeps(s, top) && top <= s + plan.layer_height + 1e-6;
                                                   }));
            if (cut.empty())
                continue;
            for (LayerRegion *layerm : layer.regions())
                if (!layerm->slices.empty())
                    layerm->slices.set(diff_ex(layerm->slices.surfaces, cut), stInternal);
            layer.make_slices();
        }
        m_zebu_curved_tops.emplace_back(std::move(plan));
    }
    const int skipped = skipped_head + skipped_layers + skipped_thin;
    if (skipped > 0) {
        // Why, surface by surface.
        std::string why;
        auto        reason = [&why](int count, const std::string &text) {
            if (count > 0)
                why += (why.empty() ? "" : ", ") + (boost::format(text) % count).str();
        };
        reason(skipped_head, L("%1% where the toolhead could touch the print"));
        reason(skipped_layers, L("%1% too close to the bed or over layers thicker than 1.5 layer heights"));
        reason(skipped_thin, L("%1% where the object is too thin under the surface"));
        warn((boost::format(L("Curved top surfaces (ZEBU): %1% surface(s) printed curved, %2% printed flat (%3%).")) %
              m_zebu_curved_tops.size() % skipped % why).str());
    }
    BOOST_LOG_TRIVIAL(info) << "ZEBU curved tops: " << m_zebu_curved_tops.size() << " surface(s) printed curved, flat: "
                            << skipped_head << " toolhead, " << skipped_layers << " layers, " << skipped_thin << " thin, object "
                            << m_model_object->name;
}

void PrintObject::zebu_make_curved_tops()
{
    for (Layer *layer : m_layers) {
        layer->zebu_curved_tops.clear();
        layer->zebu_curved_region   = nullptr;
        layer->zebu_curved_filament = 0;
    }
    if (m_zebu_curved_tops.empty())
        return;
    const unsigned int filament = single_filament(*this);
    for (const CurvedTopPlan &plan : m_zebu_curved_tops) {
        Layer &host = *m_layers[plan.host_layer];
        // The region printing most of the planar layer under the curved ones.
        const double base_z = plan.z_min - plan.layers * plan.layer_height;
        const Layer *below  = nullptr;
        for (const Layer *layer : m_layers)
            if (layer->print_z <= base_z + 1e-6)
                below = layer;
        const LayerRegion *layerm = nullptr;
        double             best   = -1.;
        for (size_t r = 0; r < host.regions().size(); ++r) {
            double area = 0.;
            if (below != nullptr && r < below->regions().size())
                for (const ExPolygon &e : intersection_ex(plan.footprint, to_expolygons(below->regions()[r]->slices.surfaces)))
                    area += e.area();
            if (area > best) {
                best   = area;
                layerm = host.regions()[r];
            }
        }
        if (layerm == nullptr)
            continue;
        const PrintRegionConfig &config = layerm->region().config();
        const double             lh     = plan.layer_height;
        const Flow               ext    = layerm->flow(frExternalPerimeter, lh);
        const Flow               per    = layerm->flow(frPerimeter, lh);
        const Flow               solid  = layerm->flow(frSolidInfill, lh);
        const Flow               top    = layerm->flow(frTopSolidInfill, lh);
        const int                walls  = std::max(1, config.wall_loops.value);
        // Offsets of the walls' centre lines from the outline, then of the infill's edge.
        std::vector<double> wall_offsets{0.5 * ext.width()};
        for (int w = 1; w < walls; ++w)
            wall_offsets.push_back(wall_offsets.back() + (w == 1 ? 0.5 * (ext.spacing() + per.spacing()) : per.spacing()));
        const double infill_edge = wall_offsets.back() + 0.5 * (walls == 1 ? ext.spacing() : per.spacing()) -
                                   0.01 * config.top_bottom_infill_wall_overlap.value * solid.spacing();
        const double seam_gap    = 0.15 * ext.width();

        Point last = plan.footprint.front().contour.points.front();
        for (int k = 0; k < plan.layers; ++k) {
            const double offset = -(plan.layers - 1 - k) * lh; // below the surface
            const bool   is_top = k + 1 == plan.layers;
            auto        *block  = new ExtrusionEntityCollection();
            block->no_sort      = true;
            auto add_path = [&](const Points &points, ExtrusionRole role, const Flow &flow) {
                if (points.size() < 2)
                    return;
                ExtrusionPath path(role, flow.mm3_per_mm(), float(flow.width()), float(flow.height()));
                path.polyline    = lift_onto(points, plan.surface, offset, host.print_z, 0.4);
                path.z_contoured = true;
                path.zebu_curved = k == 0 ? 2 : 1;
                if (path.polyline.points.size() >= 2) {
                    last = path.polyline.points.back().to_point();
                    block->entities.push_back(new ExtrusionPath(std::move(path)));
                }
            };
            // Walls, inner ones first, each loop starting near the end of the previous move.
            for (int w = walls - 1; w >= 0; --w)
                for (const ExPolygon &e : offset_ex(plan.footprint, -float(scale_(wall_offsets[w])))) {
                    std::vector<const Polygon *> loops{&e.contour};
                    for (const Polygon &hole : e.holes)
                        loops.push_back(&hole);
                    for (const Polygon *loop : loops) {
                        if (loop->points.size() < 3)
                            continue;
                        size_t start = 0;
                        double dist  = std::numeric_limits<double>::max();
                        for (size_t i = 0; i < loop->points.size(); ++i)
                            if (const double d = (loop->points[i] - last).cast<double>().squaredNorm(); d < dist) {
                                dist  = d;
                                start = i;
                            }
                        Polyline closed;
                        for (size_t i = 0; i <= loop->points.size(); ++i)
                            closed.points.push_back(loop->points[(start + i) % loop->points.size()]);
                        closed.clip_end(scale_(seam_gap));
                        add_path(closed.points, w == 0 ? erExternalPerimeter : erPerimeter, w == 0 ? ext : per);
                    }
                }
            // Solid infill, the top one monotonic, crossing the one below.
            const Flow                   &fill_flow = is_top ? top : solid;
            const ExPolygons              area      = offset_ex(plan.footprint, -float(scale_(infill_edge)));
            std::unique_ptr<Fill>         fill(Fill::new_from_type(ipRectilinear));
            fill->set_bounding_box(this->bounding_box());
            fill->layer_id    = host.id() + size_t(k);
            fill->z           = host.print_z;
            fill->spacing     = fill_flow.spacing();
            fill->overlap     = 0.;
            fill->angle       = float(Geometry::deg2rad(k % 2 == 0 ? 45. : 135.));
            fill->fixed_angle = true;
            fill->link_max_length = coord_t(scale_(3. * fill->spacing));
            FillParams params;
            params.density   = 1.f;
            params.monotonic = is_top;
            params.flow      = fill_flow;
            params.extrusion_role = is_top ? erTopSolidInfill : erSolidInfill;
            for (const ExPolygon &e : area) {
                Surface   surface(is_top ? stTop : stInternalSolid, e);
                Polylines lines;
                try {
                    lines = fill->fill_surface(&surface, params);
                } catch (InfillFailedException &) {
                }
                for (const Polyline &line : lines)
                    add_path(line.points, is_top ? erTopSolidInfill : erSolidInfill, fill_flow);
            }
            if (block->entities.empty())
                delete block;
            else
                host.zebu_curved_tops.entities.push_back(block);
        }
        host.zebu_curved_region   = &layerm->region();
        host.zebu_curved_filament = filament;
    }
}

} // namespace Slic3r
