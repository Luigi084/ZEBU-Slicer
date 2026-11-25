// ZEBU Slicer: the underside of a printer's toolhead, as a height map around the nozzle tip.
#include "HeadUnderside.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace Slic3r { namespace Zebu {

namespace {
bool fail(std::string *error, const std::string &message)
{
    if (error)
        *error = message;
    return false;
}

bool base64_decode(const std::string &in, std::vector<uint8_t> &out)
{
    static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int8_t table[256];
    std::fill(std::begin(table), std::end(table), int8_t(-1));
    for (size_t i = 0; i < chars.size(); ++i)
        table[uint8_t(chars[i])] = int8_t(i);
    out.clear();
    out.reserve(in.size() * 3 / 4);
    uint32_t buffer = 0;
    int      bits   = 0;
    for (char ch : in) {
        if (ch == '=')
            break;
        if (ch == '\n' || ch == '\r' || ch == ' ')
            continue;
        const int8_t value = table[uint8_t(ch)];
        if (value < 0)
            return false;
        buffer = (buffer << 6) | uint32_t(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(uint8_t((buffer >> bits) & 0xFF));
        }
    }
    return true;
}
} // namespace

bool HeadUnderside::load(const std::string &path, std::string *error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return fail(error, "cannot open " + path);
    std::stringstream text;
    text << file.rdbuf();
    return parse(text.str(), error);
}

bool HeadUnderside::parse(const std::string &json_text, std::string *error)
{
    *this = HeadUnderside();
    try {
        const nlohmann::json j = nlohmann::json::parse(json_text);
        if (j.value("schema", std::string()) != "zebu.head_underside.v1")
            return fail(error, "not a zebu.head_underside.v1 file");
        m_id       = j.at("id").get<std::string>();
        m_name     = j.value("name", m_id);
        m_cell     = j.at("cell").get<double>();
        m_origin_x = j.at("origin").at(0).get<double>();
        m_origin_y = j.at("origin").at(1).get<double>();
        m_nx       = j.at("size").at(0).get<int>();
        m_ny       = j.at("size").at(1).get<int>();
        m_cap      = j.at("cap").get<double>();
        std::vector<uint8_t> bytes;
        if (!base64_decode(j.at("heights_u16_b64").get<std::string>(), bytes))
            return fail(error, "bad base64 data");
        if (m_cell <= 0. || m_nx <= 0 || m_ny <= 0 || m_cap <= 0. || bytes.size() != size_t(m_nx) * size_t(m_ny) * 2)
            return fail(error, "inconsistent raster size");
        m_heights.resize(size_t(m_nx) * size_t(m_ny));
        for (size_t i = 0; i < m_heights.size(); ++i)
            m_heights[i] = uint16_t(bytes[2 * i] | (bytes[2 * i + 1] << 8)); // little endian
    } catch (const std::exception &ex) {
        *this = HeadUnderside();
        return fail(error, ex.what());
    }
    return true;
}

double HeadUnderside::height(double dx, double dy) const
{
    if (m_heights.empty())
        return 0.; // No model: assume the worst.
    const int i = int(std::floor((dx - m_origin_x) / m_cell));
    const int j = int(std::floor((dy - m_origin_y) / m_cell));
    if (i < 0 || j < 0 || i >= m_nx || j >= m_ny)
        return m_cap;
    const uint16_t h = m_heights[size_t(j) * size_t(m_nx) + size_t(i)];
    return h == 65535 ? m_cap : h * 0.01;
}

double HeadUnderside::lowest_in_square(double cx, double cy, double half) const
{
    if (m_heights.empty())
        return 0.;
    const int i0 = int(std::floor((cx - half - m_origin_x) / m_cell)), i1 = int(std::floor((cx + half - m_origin_x) / m_cell));
    const int j0 = int(std::floor((cy - half - m_origin_y) / m_cell)), j1 = int(std::floor((cy + half - m_origin_y) / m_cell));
    double lowest = m_cap;
    for (int j = std::max(0, j0); j <= std::min(m_ny - 1, j1); ++j)
        for (int i = std::max(0, i0); i <= std::min(m_nx - 1, i1); ++i) {
            const uint16_t h = m_heights[size_t(j) * size_t(m_nx) + size_t(i)];
            if (h != 65535)
                lowest = std::min(lowest, h * 0.01);
        }
    return lowest;
}

std::vector<HeadUnderside::Offset> HeadUnderside::offsets_below(double below_height, double grid, double min_radius) const
{
    std::vector<Offset> out;
    if (m_heights.empty() || grid <= 0.)
        return out;
    const int imin = int(std::floor(m_origin_x / grid)) - 1, imax = int(std::ceil((m_origin_x + m_nx * m_cell) / grid)) + 1;
    const int jmin = int(std::floor(m_origin_y / grid)) - 1, jmax = int(std::ceil((m_origin_y + m_ny * m_cell) / grid)) + 1;
    for (int dj = jmin; dj <= jmax; ++dj)
        for (int di = imin; di <= imax; ++di) {
            // Nearest distance between the nozzle's cell and the material's cell.
            const double near = std::hypot(std::max(0., std::abs(di) - 1.) * grid, std::max(0., std::abs(dj) - 1.) * grid);
            if (near < min_radius)
                continue;
            const double h = this->lowest_in_square(di * grid, dj * grid, grid);
            if (h < below_height)
                out.push_back({di, dj, h});
        }
    return out;
}

}} // namespace Slic3r::Zebu
