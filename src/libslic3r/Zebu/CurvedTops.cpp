// ZEBU Slicer: curved (non-planar) top surfaces, analysis stage.
#include "CurvedTops.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <utility>

namespace Slic3r { namespace Zebu {

namespace {

constexpr double NONE = -std::numeric_limits<double>::infinity();

// A height field on a square grid covering the mesh: z of the highest surface above each cell centre.
struct Grid
{
    double              x0 { 0. }, y0 { 0. }, step { 1. };
    int                 nx { 0 }, ny { 0 };
    std::vector<double> z;

    double &at(int i, int j) { return z[size_t(j) * size_t(nx) + size_t(i)]; }
    double  at(int i, int j) const { return z[size_t(j) * size_t(nx) + size_t(i)]; }
    bool    inside(int i, int j) const { return i >= 0 && j >= 0 && i < nx && j < ny; }
};

using V3 = std::array<double, 3>;

V3 vertex(const SimpleMesh &mesh, int idx) { return {mesh.vertices[idx][0], mesh.vertices[idx][1], mesh.vertices[idx][2]}; }

// Raises the grid to the triangle's height at every cell centre inside the triangle, and at its corners.
void rasterize_max(Grid &grid, const V3 &a, const V3 &b, const V3 &c)
{
    const double d = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1]);
    for (const V3 *p : {&a, &b, &c}) {
        const int i = int(std::floor(((*p)[0] - grid.x0) / grid.step)), j = int(std::floor(((*p)[1] - grid.y0) / grid.step));
        if (grid.inside(i, j))
            grid.at(i, j) = std::max(grid.at(i, j), (*p)[2]);
    }
    if (std::abs(d) < 1e-12)
        return; // Vertical: its corners (and its neighbours) cover it.
    const double xmin = std::min({a[0], b[0], c[0]}), xmax = std::max({a[0], b[0], c[0]});
    const double ymin = std::min({a[1], b[1], c[1]}), ymax = std::max({a[1], b[1], c[1]});
    const int    i0 = std::max(0, int(std::ceil((xmin - grid.x0) / grid.step - 0.5)));
    const int    i1 = std::min(grid.nx - 1, int(std::floor((xmax - grid.x0) / grid.step - 0.5)));
    const int    j0 = std::max(0, int(std::ceil((ymin - grid.y0) / grid.step - 0.5)));
    const int    j1 = std::min(grid.ny - 1, int(std::floor((ymax - grid.y0) / grid.step - 0.5)));
    for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i) {
            const double x  = grid.x0 + (i + 0.5) * grid.step, y = grid.y0 + (j + 0.5) * grid.step;
            const double l1 = ((b[1] - c[1]) * (x - c[0]) + (c[0] - b[0]) * (y - c[1])) / d;
            const double l2 = ((c[1] - a[1]) * (x - c[0]) + (a[0] - c[0]) * (y - c[1])) / d;
            const double l3 = 1. - l1 - l2;
            if (l1 >= -1e-9 && l2 >= -1e-9 && l3 >= -1e-9)
                grid.at(i, j) = std::max(grid.at(i, j), l1 * a[2] + l2 * b[2] + l3 * c[2]);
        }
}

struct UnionFind
{
    std::vector<int> parent;
    explicit UnionFind(size_t n) : parent(n) { std::iota(parent.begin(), parent.end(), 0); }
    int  find(int x) { return parent[x] == x ? x : parent[x] = find(parent[x]); }
    void unite(int a, int b) { parent[find(a)] = find(b); }
};

} // namespace

double HeightField::cell_height(double x, double y) const
{
    const int i = cell_x(x), j = cell_y(y);
    return valid(i, j) ? double(at(i, j)) : std::numeric_limits<double>::quiet_NaN();
}

double HeightField::sample(double x, double y) const
{
    const double fx = (x - x0) / step - 0.5, fy = (y - y0) / step - 0.5;
    const int    i = int(std::floor(fx)), j = int(std::floor(fy));
    const double tx = fx - i, ty = fy - j;
    double sum = 0., weight = 0.;
    for (int dj = 0; dj < 2; ++dj)
        for (int di = 0; di < 2; ++di)
            if (valid(i + di, j + dj)) {
                const double w = (di ? tx : 1. - tx) * (dj ? ty : 1. - ty) + 1e-9;
                sum += w * at(i + di, j + dj);
                weight += w;
            }
    return weight > 0. ? sum / weight : std::numeric_limits<double>::quiet_NaN();
}

HeightField rasterize_facets(const SimpleMesh &mesh, const std::vector<int> &facets, double step, int extend)
{
    HeightField out;
    if (facets.empty())
        return out;
    double xmin = 1e30, ymin = 1e30, xmax = -1e30, ymax = -1e30;
    for (int f : facets)
        for (int k = 0; k < 3; ++k) {
            const auto &v = mesh.vertices[mesh.faces[f][k]];
            xmin = std::min(xmin, double(v[0])); xmax = std::max(xmax, double(v[0]));
            ymin = std::min(ymin, double(v[1])); ymax = std::max(ymax, double(v[1]));
        }
    Grid grid;
    grid.step = step;
    grid.x0   = xmin - (extend + 1) * step;
    grid.y0   = ymin - (extend + 1) * step;
    grid.nx   = int(std::ceil((xmax - grid.x0) / step)) + extend + 2;
    grid.ny   = int(std::ceil((ymax - grid.y0) / step)) + extend + 2;
    grid.z.assign(size_t(grid.nx) * size_t(grid.ny), NONE);
    for (int f : facets)
        rasterize_max(grid, vertex(mesh, mesh.faces[f][0]), vertex(mesh, mesh.faces[f][1]), vertex(mesh, mesh.faces[f][2]));
    // Grow the surface by its nearest heights, one ring of cells at a time.
    for (int ring = 0; ring < extend; ++ring) {
        std::vector<double> next = grid.z;
        for (int j = 0; j < grid.ny; ++j)
            for (int i = 0; i < grid.nx; ++i)
                if (grid.at(i, j) == NONE) {
                    double sum = 0.;
                    int    n   = 0;
                    for (int dj = -1; dj <= 1; ++dj)
                        for (int di = -1; di <= 1; ++di)
                            if ((di != 0 || dj != 0) && grid.inside(i + di, j + dj) && grid.at(i + di, j + dj) != NONE) {
                                sum += grid.at(i + di, j + dj);
                                ++n;
                            }
                    if (n > 0)
                        next[size_t(j) * size_t(grid.nx) + size_t(i)] = sum / n;
                }
        grid.z = std::move(next);
    }
    out.x0 = grid.x0;
    out.y0 = grid.y0;
    out.step = step;
    out.nx = grid.nx;
    out.ny = grid.ny;
    out.z.resize(grid.z.size());
    for (size_t k = 0; k < grid.z.size(); ++k)
        out.z[k] = grid.z[k] == NONE ? std::numeric_limits<float>::quiet_NaN() : float(grid.z[k]);
    return out;
}

int curved_layers_fitting(double z_min, double first_layer_top, double layer_height, int wanted)
{
    int n = std::max(0, wanted);
    // Same rule as CurvedTopPlan::planar_keeps(): the first layer keeps the lowest point.
    while (n > 0 && first_layer_top > z_min - n * layer_height + 0.5 * layer_height + 1e-6)
        --n;
    return n;
}

std::string head_model_path(const std::string &value, const std::string &resources_dir)
{
    if (value.find('/') != std::string::npos || value.find('\\') != std::string::npos)
        return value;
    return resources_dir + "/zebu/heads/" + value + ".json";
}

std::vector<CurvedTopPatch> analyze_curved_tops(const SimpleMesh &mesh, const HeadUnderside &head, const CurvedTopsParams &params,
                                                const Obstacles *obstacles)
{
    std::vector<CurvedTopPatch> patches;
    if (mesh.faces.empty() || head.empty())
        return patches;
    const double cos_max = std::cos(params.max_angle_deg * M_PI / 180.);

    // The whole mesh's top height field.
    Grid top;
    {
        double xmin = 1e30, ymin = 1e30, xmax = -1e30, ymax = -1e30;
        for (const auto &v : mesh.vertices) {
            xmin = std::min(xmin, double(v[0])); xmax = std::max(xmax, double(v[0]));
            ymin = std::min(ymin, double(v[1])); ymax = std::max(ymax, double(v[1]));
        }
        // With other objects around, the grid reaches as far as the head does, so that it sees them.
        double reach = 2. * params.grid;
        if (obstacles != nullptr && !obstacles->mesh.faces.empty())
            for (const auto &o : head.offsets_below(head.cap() + 1., params.grid, 0.))
                reach = std::max(reach, (std::max(std::abs(o.di), std::abs(o.dj)) + 1) * params.grid);
        top.step = params.grid;
        top.x0   = xmin - reach;
        top.y0   = ymin - reach;
        top.nx   = int(std::ceil((xmax + reach - top.x0) / top.step)) + 1;
        top.ny   = int(std::ceil((ymax + reach - top.y0) / top.step)) + 1;
        top.z.assign(size_t(top.nx) * size_t(top.ny), NONE);
        for (const auto &f : mesh.faces)
            rasterize_max(top, vertex(mesh, f[0]), vertex(mesh, f[1]), vertex(mesh, f[2]));
    }
    // The other objects, on the same grid (only what lies over it can come under the head).
    Grid others = top;
    std::fill(others.z.begin(), others.z.end(), NONE);
    bool has_others = false;
    if (obstacles != nullptr)
        for (const auto &f : obstacles->mesh.faces) {
            rasterize_max(others, vertex(obstacles->mesh, f[0]), vertex(obstacles->mesh, f[1]), vertex(obstacles->mesh, f[2]));
            has_others = true;
        }

    // Candidate faces: facing up, not steeper than the limit, and on top (nothing above them).
    const size_t      nf = mesh.faces.size();
    std::vector<char> candidate(nf, 0);
    std::vector<double> slope(nf, 0.), area(nf, 0.);
    for (size_t f = 0; f < nf; ++f) {
        const V3 a = vertex(mesh, mesh.faces[f][0]), b = vertex(mesh, mesh.faces[f][1]), c = vertex(mesh, mesh.faces[f][2]);
        const V3 u = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, w = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const V3 n = {u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]};
        const double len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len < 1e-12)
            continue;
        const double nz = n[2] / len;
        slope[f]        = std::acos(std::clamp(nz, -1., 1.)) * 180. / M_PI;
        area[f]         = 0.5 * std::abs(n[2]); // Projected area.
        if (nz < cos_max)
            continue;
        const double cx = (a[0] + b[0] + c[0]) / 3., cy = (a[1] + b[1] + c[1]) / 3., cz = (a[2] + b[2] + c[2]) / 3.;
        const int    i = int(std::floor((cx - top.x0) / top.step)), j = int(std::floor((cy - top.y0) / top.step));
        if (top.inside(i, j) && cz >= top.at(i, j) - params.layer_height)
            candidate[f] = 1;
    }

    // Patches: candidates sharing an edge.
    UnionFind uf(nf);
    {
        std::map<std::pair<int, int>, int> edge_owner;
        for (size_t f = 0; f < nf; ++f)
            if (candidate[f])
                for (int e = 0; e < 3; ++e) {
                    int v0 = mesh.faces[f][e], v1 = mesh.faces[f][(e + 1) % 3];
                    if (v0 > v1)
                        std::swap(v0, v1);
                    auto [it, inserted] = edge_owner.emplace(std::make_pair(v0, v1), int(f));
                    if (!inserted)
                        uf.unite(int(f), it->second);
                }
    }
    std::map<int, size_t> patch_of_root;
    for (size_t f = 0; f < nf; ++f)
        if (candidate[f]) {
            auto [it, inserted] = patch_of_root.emplace(uf.find(int(f)), patches.size());
            if (inserted)
                patches.emplace_back();
            patches[it->second].facets.push_back(int(f));
        }

    const int    N = std::max(1, params.curved_layers);
    const double lh = params.layer_height;
    std::vector<CurvedTopPatch> result;
    for (CurvedTopPatch &patch : patches) {
        for (int f : patch.facets) {
            patch.area += area[f];
            patch.max_slope_deg = std::max(patch.max_slope_deg, slope[f]);
        }
        if (patch.max_slope_deg < params.min_angle_deg)
            continue; // Flat: printed flat already.

        // The patch's own height field.
        Grid own = top;
        std::fill(own.z.begin(), own.z.end(), NONE);
        for (int f : patch.facets)
            rasterize_max(own, vertex(mesh, mesh.faces[f][0]), vertex(mesh, mesh.faces[f][1]), vertex(mesh, mesh.faces[f][2]));
        patch.z_min = 1e30;
        patch.z_max = -1e30;
        std::vector<std::pair<int, int>> cells;
        for (int j = 0; j < own.ny; ++j)
            for (int i = 0; i < own.nx; ++i)
                if (own.at(i, j) != NONE) {
                    cells.emplace_back(i, j);
                    patch.z_min = std::min(patch.z_min, own.at(i, j));
                    patch.z_max = std::max(patch.z_max, own.at(i, j));
                }
        if (cells.empty())
            continue;

        // When the curved layers are printed, the planar layers have reached z_c everywhere else: the top of the
        // layer holding the surface's top (the last one sliced below it).
        double z_c = std::ceil(patch.z_max / lh - 0.5 - 1e-6) * lh;
        if (!params.layer_tops.empty()) {
            auto it = std::lower_bound(params.layer_tops.begin(), params.layer_tops.end(), patch.z_max - 0.5 * lh - 1e-6);
            if (it == params.layer_tops.end())
                continue; // No layer reaches the top: nothing to print it with.
            z_c = *it;
        }
        patch.print_z = z_c;
        // Material around the nozzle: the patch itself (its curved layers rise like the surface), elsewhere
        // the object up to z_c. For the curved layers below the top one, the nozzle sits lower: gap_at() below.
        // Other objects: as high as they are when complete, or capped like the object.
        auto other_object = [&](int i, int j) {
            const double o = others.at(i, j);
            return o == NONE ? NONE : (obstacles->full_height ? o : std::min(o, z_c));
        };
        auto material = [&](int i, int j) -> std::pair<double, bool> {
            if (!top.inside(i, j))
                return {NONE, false};
            const double o = has_others ? other_object(i, j) : NONE;
            if (own.at(i, j) != NONE)
                return {std::max(own.at(i, j), o), o <= own.at(i, j)};
            const double t = top.at(i, j);
            return {std::max(t == NONE ? NONE : std::min(t, z_c), o), false};
        };
        double highest = NONE;
        for (const double t : top.z)
            if (t != NONE)
                highest = std::max(highest, std::min(t, z_c) + (N - 1) * lh);
        if (has_others)
            for (int j = 0; j < others.ny; ++j)
                for (int i = 0; i < others.nx; ++i)
                    highest = std::max(highest, other_object(i, j) + (N - 1) * lh);
        const double rise_max = std::max(highest, patch.z_max) - patch.z_min;
        const auto   offsets  = head.offsets_below(rise_max + params.margin, params.grid, 0.);

        // Gaps between the head and the print, per nozzle cell, for the top curved layer (k = 0); for the
        // layer k below it, material outside the patch is k layers higher relative to the nozzle.
        // - near the nozzle (its cone and land), material outside the patch must not be above the head;
        // - farther, everything must stay `margin` below the head; the patch's own surface is only checked
        //   there, its slope limit covering the nozzle's cone.
        double worst_gap = 1e30;
        size_t ok_top = 0, ok_all = 0;
        for (const auto &[pi, pj] : cells) {
            const double z = own.at(pi, pj);
            double near_other = 1e30, far_other = 1e30, far_self = 1e30;
            for (const auto &o : offsets) {
                const auto [m, in_patch] = material(pi + o.di, pj + o.dj);
                if (m == NONE)
                    continue;
                const double gap  = o.height - (m - z);
                const bool   near = std::hypot(o.di * params.grid, o.dj * params.grid) < params.near_radius;
                if (in_patch) {
                    if (!near)
                        far_self = std::min(far_self, gap);
                } else if (near)
                    near_other = std::min(near_other, gap);
                else
                    far_other = std::min(far_other, gap);
            }
            auto gap_at = [&](int k) {
                return std::min({near_other - k * lh, far_other - k * lh - params.margin, far_self - params.margin});
            };
            const double top_gap = gap_at(0);
            worst_gap = std::min(worst_gap, top_gap);
            ok_top += top_gap >= 0.;
            ok_all += gap_at(N - 1) >= 0.;
        }
        patch.clearance         = worst_gap == 1e30 ? head.cap() : worst_gap;
        patch.ok_fraction       = double(ok_top) / double(cells.size());
        patch.ok_fraction_lower = double(ok_all) / double(cells.size());
        patch.printable         = ok_top == cells.size() && ok_all == cells.size();
        result.push_back(std::move(patch));
    }
    std::sort(result.begin(), result.end(), [](const CurvedTopPatch &a, const CurvedTopPatch &b) { return a.area > b.area; });
    return result;
}

}} // namespace Slic3r::Zebu
