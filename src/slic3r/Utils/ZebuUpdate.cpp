// ZEBU Slicer: update check against GitHub Releases. See ZebuUpdate.hpp.
#include "ZebuUpdate.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace Slic3r { namespace Zebu {

namespace {

struct Ver
{
    bool                     ok {false};
    long                     major {0}, minor {0}, patch {0};
    std::vector<std::string> pre; // empty = release
};

std::vector<std::string> split(const std::string &s, char sep)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else
            cur += c;
    }
    out.push_back(cur);
    return out;
}

bool all_digits(const std::string &s)
{
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
}

Ver parse(std::string s)
{
    Ver v;
    if (!s.empty() && (s[0] == 'v' || s[0] == 'V'))
        s.erase(0, 1);
    const size_t plus = s.find('+'); // build metadata is ignored
    if (plus != std::string::npos)
        s.erase(plus);
    std::string core = s, pre;
    const size_t dash = s.find('-');
    if (dash != std::string::npos) {
        core = s.substr(0, dash);
        pre  = s.substr(dash + 1);
    }
    const auto parts = split(core, '.');
    if (parts.size() < 2 || parts.size() > 4)
        return v;
    for (const auto &p : parts)
        if (!all_digits(p))
            return v;
    v.major = std::atol(parts[0].c_str());
    v.minor = std::atol(parts[1].c_str());
    v.patch = parts.size() > 2 ? std::atol(parts[2].c_str()) : 0;
    if (!pre.empty())
        v.pre = split(pre, '.');
    v.ok = true;
    return v;
}

int cmp(long a, long b) { return a < b ? -1 : a > b ? 1 : 0; }

int cmp_pre(const std::vector<std::string> &a, const std::vector<std::string> &b)
{
    if (a.empty() || b.empty())
        return a.empty() == b.empty() ? 0 : (a.empty() ? 1 : -1); // a release is newer than its pre-releases
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
        const bool na = all_digits(a[i]), nb = all_digits(b[i]);
        int c;
        if (na && nb)
            c = cmp(std::atol(a[i].c_str()), std::atol(b[i].c_str()));
        else if (na != nb)
            c = na ? -1 : 1; // numeric identifiers sort before alphanumeric ones
        else
            c = a[i] < b[i] ? -1 : (a[i] > b[i] ? 1 : 0);
        if (c != 0)
            return c;
    }
    return cmp(long(a.size()), long(b.size()));
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

bool ends_with(const std::string &s, const std::string &suffix)
{
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool asset_matches(const std::string &name, UpdatePlatform platform)
{
    const std::string l = lower(name);
    switch (platform) {
    case UpdatePlatform::Windows:       return ends_with(l, ".exe") && l.find("windows") != std::string::npos && l.find("x64") != std::string::npos;
    case UpdatePlatform::LinuxAppImage: return ends_with(l, ".appimage");
    default:                            return false;
    }
}

std::string str(const json &j, const char *key)
{
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

} // namespace

int compare_versions(const std::string &a, const std::string &b)
{
    const Ver va = parse(a), vb = parse(b);
    if (!va.ok || !vb.ok)
        return 0;
    if (int c = cmp(va.major, vb.major)) return c;
    if (int c = cmp(va.minor, vb.minor)) return c;
    if (int c = cmp(va.patch, vb.patch)) return c;
    return cmp_pre(va.pre, vb.pre);
}

ReleaseInfo pick_update(const std::string &releases_json, const std::string &current_version, UpdatePlatform platform,
                        bool allow_prerelease)
{
    ReleaseInfo best;
    json j;
    try {
        j = json::parse(releases_json);
    } catch (...) {
        return best;
    }
    if (j.is_object())
        j = json::array({j}); // /releases/latest returns a single object
    if (!j.is_array())
        return best;
    for (const auto &rel : j) {
        if (!rel.is_object())
            continue;
        if (rel.value("draft", false))
            continue;
        const bool pre = rel.value("prerelease", false);
        if (pre && !allow_prerelease)
            continue;
        const std::string tag     = str(rel, "tag_name");
        const std::string version = (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) ? tag.substr(1) : tag;
        if (!parse(version).ok || compare_versions(version, current_version) <= 0)
            continue;
        if (best.found && compare_versions(version, best.version) <= 0)
            continue;
        ReleaseInfo r;
        r.version    = version;
        r.tag        = tag;
        r.name       = str(rel, "name");
        r.notes      = str(rel, "body");
        r.page_url   = str(rel, "html_url");
        r.prerelease = pre;
        if (rel.contains("assets") && rel["assets"].is_array()) {
            for (const auto &a : rel["assets"])
                if (asset_matches(str(a, "name"), platform)) {
                    r.asset_name = str(a, "name");
                    r.asset_url  = str(a, "browser_download_url");
                    break;
                }
            if (!r.asset_name.empty())
                for (const auto &a : rel["assets"])
                    if (str(a, "name") == r.asset_name + ".sha256")
                        r.sha256_url = str(a, "browser_download_url");
        }
        r.found = true;
        best    = r;
    }
    return best;
}

std::string parse_sha256_file(const std::string &content)
{
    std::string hex;
    for (char c : content) {
        if (std::isxdigit(static_cast<unsigned char>(c)))
            hex += char(std::tolower(static_cast<unsigned char>(c)));
        else
            break;
    }
    return hex.size() == 64 ? hex : std::string();
}

}} // namespace Slic3r::Zebu
