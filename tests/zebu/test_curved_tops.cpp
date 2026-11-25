// ZEBU: analysis of curved top surfaces against the real head models (resources/zebu/heads).
#include "libslic3r/Zebu/CurvedTops.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>

using namespace Slic3r::Zebu;

// A box from z = 0 to its top surface top(x, y), on a regular grid (top made of triangles, then walls and bottom).
static SimpleMesh height_box(double w, double d, double n_steps, const std::function<double(double, double)> &top_z)
{
    SimpleMesh m;
    const int n = int(n_steps);
    auto id = [&](int i, int j, int layer) { return layer * (n + 1) * (n + 1) + j * (n + 1) + i; };
    for (int layer = 0; layer < 2; ++layer)
        for (int j = 0; j <= n; ++j)
            for (int i = 0; i <= n; ++i) {
                const double x = -w / 2 + w * i / n, y = -d / 2 + d * j / n;
                m.vertices.push_back({float(x + 150), float(y + 150), float(layer == 0 ? 0. : top_z(x, y))});
            }
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            // top (counter-clockwise seen from above), bottom (clockwise)
            m.faces.push_back({id(i, j, 1), id(i + 1, j, 1), id(i + 1, j + 1, 1)});
            m.faces.push_back({id(i, j, 1), id(i + 1, j + 1, 1), id(i, j + 1, 1)});
            m.faces.push_back({id(i, j, 0), id(i + 1, j + 1, 0), id(i + 1, j, 0)});
            m.faces.push_back({id(i, j, 0), id(i, j + 1, 0), id(i + 1, j + 1, 0)});
        }
    auto wall = [&](int a0, int a1) { // a0 -> a1 along the rim, outward wall
        m.faces.push_back({a0 - (n + 1) * (n + 1), a1 - (n + 1) * (n + 1), a1});
        m.faces.push_back({a0 - (n + 1) * (n + 1), a1, a0});
    };
    for (int i = 0; i < n; ++i) {
        wall(id(i + 1, 0, 1), id(i, 0, 1));
        wall(id(i, n, 1), id(i + 1, n, 1));
        wall(id(0, i, 1), id(0, i + 1, 1));
        wall(id(n, i + 1, 1), id(n, i, 1));
    }
    return m;
}

static void report(const char *title, const std::vector<CurvedTopPatch> &patches)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("%s: %zu patch(es)\n", title, patches.size());
    for (const auto &p : patches)
        std::printf("   area %7.1f mm2  z %.2f..%.2f (%.2f mm)  slope <= %4.1f deg  clearance %+.2f mm  ok top %3.0f %%  lowest %3.0f %%  -> %s\n",
                    p.area, p.z_min, p.z_max, p.z_max - p.z_min, p.max_slope_deg, p.clearance, 100 * p.ok_fraction,
                    100 * p.ok_fraction_lower, p.printable ? "printable" : "NOT printable");
}

int main(int argc, char **argv)
{
    const std::string dir = argc > 1 ? argv[1] : "resources/zebu/heads";
    HeadUnderside     g3d;
    std::string       err;
    if (!g3d.load(dir + "/g3d-pro-360-stealthburner-eddy.json", &err)) {
        std::printf("load: %s\n", err.c_str());
        return 1;
    }
    // The file's known points (computed from the CAD): nozzle land, sock, Eddy Duo, far field.
    std::printf("head %s: h(0,0)=%.2f h(0,-6)=%.2f h(20,-10)=%.2f h(0,30)=%.2f cap=%.1f\n", g3d.name().c_str(), g3d.height(0, 0),
                g3d.height(0, -6), g3d.height(20, -10), g3d.height(0, 30), g3d.cap());
    assert(g3d.height(0, 0) < 0.05);
    assert(std::abs(g3d.height(20, -10) - 2.50) < 0.02);
    assert(g3d.height(200, 200) == g3d.cap());

    CurvedTopsParams params; // 15°, 0.4 mm margin, 4 x 0.2 mm layers

    // 1. Flat top: nothing to do.
    auto flat = analyze_curved_tops(height_box(40, 40, 40, [](double, double) { return 10.; }), g3d, params);
    report("flat 40x40 box", flat);
    assert(flat.empty());

    // 2. Gentle dome on a 30 mm square, 1.5 mm high (radius 150 mm): within the head's 2.5 mm minus the margin.
    auto small_dome = analyze_curved_tops(height_box(30, 30, 60, [](double x, double y) {
        return 10. + std::sqrt(150. * 150. - x * x - y * y) - std::sqrt(150. * 150. - 2 * 15. * 15.); }), g3d, params);
    report("dome 30 mm, R150", small_dome);
    assert(!small_dome.empty() && small_dome.front().printable);

    // 2b. On a 40 mm square it is 2.7 mm high: a few spots fail (the Eddy Duo side), so not printable as is.
    auto mid_dome = analyze_curved_tops(height_box(40, 40, 80, [](double x, double y) {
        return 10. + std::sqrt(150. * 150. - x * x - y * y) - std::sqrt(150. * 150. - 2 * 20. * 20.); }), g3d, params);
    report("dome 40 mm, R150", mid_dome);
    assert(!mid_dome.empty() && !mid_dome.front().printable && mid_dome.front().ok_fraction > 0.9);

    // 3. Same dome over 80 mm: 5.4 mm high, the Eddy Duo (2.5 mm above the nozzle) would touch it.
    auto big_dome = analyze_curved_tops(height_box(80, 80, 160, [](double x, double y) {
        return 10. + std::sqrt(150. * 150. - x * x - y * y) - std::sqrt(150. * 150. - 2 * 40. * 40.); }), g3d, params);
    report("dome 80 mm, R150", big_dome);
    assert(!big_dome.empty() && !big_dome.front().printable);

    // 4. A 25° ramp: too steep, no patch on the slope.
    auto steep = analyze_curved_tops(height_box(30, 30, 30, [](double x, double) { return 10. + std::tan(25. * M_PI / 180.) * (x + 15.); }), g3d, params);
    report("ramp 25 deg", steep);
    assert(steep.empty());

    // 5. A 10° ramp, 6 mm wide: 1.1 mm high, printable.
    auto ramp = analyze_curved_tops(height_box(6, 30, 24, [](double x, double) { return 10. + std::tan(10. * M_PI / 180.) * (x + 3.); }), g3d, params);
    report("ramp 10 deg, 6 mm", ramp);
    assert(!ramp.empty() && ramp.front().printable);

    // The other heads load, and the 30 mm dome against each.
    for (const char *id : {"raise-k2plus-eva-sb", "zortrax-m200k"}) {
        HeadUnderside other;
        if (!other.load(dir + "/" + id + ".json", &err)) {
            std::printf("load %s: %s\n", id, err.c_str());
            return 1;
        }
        assert(other.height(0, 0) < 0.05);
        report((std::string("dome 30 mm on ") + other.name()).c_str(), analyze_curved_tops(height_box(30, 30, 60, [](double x, double y) {
            return 10. + std::sqrt(150. * 150. - x * x - y * y) - std::sqrt(150. * 150. - 2 * 15. * 15.); }), other, params));
    }

    std::puts("curved tops: OK");
    return 0;
}
