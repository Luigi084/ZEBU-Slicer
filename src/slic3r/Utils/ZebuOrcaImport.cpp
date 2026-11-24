// ZEBU Slicer: import of OrcaSlicer user presets. See ZebuOrcaImport.hpp.
#include "ZebuOrcaImport.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <system_error>
#include <unordered_map>

namespace fs = std::filesystem;
using nlohmann::json;

namespace Slic3r { namespace Zebu {

const char *kind_folder(PresetKind kind)
{
    switch (kind) {
    case PresetKind::Machine:  return "machine";
    case PresetKind::Filament: return "filament";
    case PresetKind::Process:  return "process";
    }
    return "machine";
}

std::vector<OrcaUserPreset> &OrcaScan::list(PresetKind kind)
{
    return kind == PresetKind::Machine ? machines : kind == PresetKind::Filament ? filaments : processes;
}

const std::vector<OrcaUserPreset> &OrcaScan::list(PresetKind kind) const
{
    return kind == PresetKind::Machine ? machines : kind == PresetKind::Filament ? filaments : processes;
}

namespace {

// Keys describing a preset file rather than a setting: never copied from a parent to a child.
const std::set<std::string> &meta_keys()
{
    static const std::set<std::string> keys = {
        "inherits", "include", "instantiation", "from", "setting_id", "name", "type", "is_custom_defined",
        "version", "description", "renamed_from", "base_id", "user_id", "updated_time", "bundle_id",
    };
    return keys;
}

bool read_json(const fs::path &file, json &out, std::string &error)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "cannot open " + file.u8string();
        return false;
    }
    std::stringstream buf;
    buf << in.rdbuf();
    std::string text = buf.str();
    if (text.size() >= 3 && (unsigned char) text[0] == 0xEF && (unsigned char) text[1] == 0xBB && (unsigned char) text[2] == 0xBF)
        text.erase(0, 3); // UTF-8 BOM
    try {
        out = json::parse(text);
    } catch (const std::exception &e) {
        error = "invalid JSON in " + file.u8string() + ": " + e.what();
        return false;
    }
    if (!out.is_object()) {
        error = "not a JSON object: " + file.u8string();
        return false;
    }
    return true;
}

std::string get_str(const json &j, const char *key)
{
    auto it = j.find(key);
    if (it == j.end())
        return {};
    if (it->is_string())
        return it->get<std::string>();
    if (it->is_array() && !it->empty() && it->front().is_string())
        return it->front().get<std::string>();
    return {};
}

std::vector<std::string> get_str_list(const json &j, const char *key)
{
    std::vector<std::string> out;
    auto it = j.find(key);
    if (it == j.end())
        return out;
    if (it->is_array()) {
        for (const auto &v : *it)
            if (v.is_string() && !v.get<std::string>().empty())
                out.push_back(v.get<std::string>());
    } else if (it->is_string() && !it->get<std::string>().empty()) {
        out.push_back(it->get<std::string>());
    }
    return out;
}

bool kind_from_string(const std::string &s, PresetKind &kind)
{
    if (s == "machine" || s == "printer")      { kind = PresetKind::Machine;  return true; }
    if (s == "filament")                       { kind = PresetKind::Filament; return true; }
    if (s == "process" || s == "print")        { kind = PresetKind::Process;  return true; }
    return false;
}

// One system (or other parent) preset, as found on disk.
struct Entry
{
    PresetKind kind;
    std::string vendor; // first folder under the profile root; "" for user presets
    json raw;
};

// Name -> entries, per kind. Names of base presets ("fdm_filament_common", ...) repeat across vendors
// with different content, hence the vendor-aware lookup.
class Index
{
public:
    void add(PresetKind kind, const std::string &name, Entry entry)
    {
        m_map[key(kind, name)].push_back(std::move(entry));
    }

    // Prefers the given vendor, then the Orca filament library, then the first one found.
    const Entry *find(PresetKind kind, const std::string &name, const std::string &vendor) const
    {
        auto it = m_map.find(key(kind, name));
        if (it == m_map.end() || it->second.empty())
            return nullptr;
        for (const Entry &e : it->second)
            if (!vendor.empty() && e.vendor == vendor)
                return &e;
        for (const Entry &e : it->second)
            if (e.vendor == "OrcaFilamentLibrary")
                return &e;
        return &it->second.front();
    }

    size_t size() const
    {
        size_t n = 0;
        for (const auto &kv : m_map)
            n += kv.second.size();
        return n;
    }

private:
    static std::string key(PresetKind kind, const std::string &name) { return std::string(kind_folder(kind)) + "/" + name; }
    std::unordered_map<std::string, std::vector<Entry>> m_map;
};

void index_profile_root(const fs::path &root, Index &index, std::vector<std::string> &warnings)
{
    std::error_code ec;
    if (!fs::is_directory(root, ec))
        return;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
        if (ec)
            break;
        if (!it->is_regular_file(ec) || it->path().extension() != ".json")
            continue;
        const fs::path rel = fs::relative(it->path(), root, ec);
        if (ec || std::distance(rel.begin(), rel.end()) < 3)
            continue; // vendor index files (<Vendor>.json) sit at depth 1
        json j;
        std::string err;
        if (!read_json(it->path(), j, err)) {
            warnings.push_back(err);
            continue;
        }
        PresetKind kind;
        const std::string name = get_str(j, "name");
        if (name.empty() || !kind_from_string(get_str(j, "type"), kind)) {
            // Fall back to the folder name (<Vendor>/<machine|filament|process>/...).
            auto p = rel.begin();
            ++p;
            if (name.empty() || p == rel.end() || !kind_from_string(p->u8string(), kind))
                continue;
        }
        index.add(kind, name, Entry{kind, rel.begin()->u8string(), std::move(j)});
    }
}

void merge_settings(json &dst, const json &src)
{
    for (auto it = src.begin(); it != src.end(); ++it)
        if (meta_keys().count(it.key()) == 0)
            dst[it.key()] = it.value();
}

class Resolver
{
public:
    explicit Resolver(const Index &index) : m_index(index) {}

    // Complete settings of a parent preset (its own chain resolved), or false when something is missing.
    bool resolve(PresetKind kind, const std::string &name, const std::string &vendor, json &out, std::string &error, int depth = 0) const
    {
        if (depth > 32) {
            error = "inheritance loop at \"" + name + "\"";
            return false;
        }
        const Entry *e = m_index.find(kind, name, vendor);
        if (e == nullptr) {
            error = "parent preset \"" + name + "\" not found";
            return false;
        }
        out = json::object();
        const std::string parent = get_str(e->raw, "inherits");
        if (!parent.empty() && !resolve(kind, parent, e->vendor, out, error, depth + 1))
            return false;
        for (const std::string &inc : get_str_list(e->raw, "include")) {
            json inc_cfg;
            if (!resolve(kind, inc, e->vendor, inc_cfg, error, depth + 1))
                return false;
            merge_settings(out, inc_cfg);
        }
        merge_settings(out, e->raw);
        if (out.find("filament_id") == out.end() && e->raw.contains("filament_id"))
            out["filament_id"] = e->raw["filament_id"];
        return true;
    }

private:
    const Index &m_index;
};

std::string strip_at_suffix(const std::string &name)
{
    const size_t at = name.rfind(" @");
    return at == std::string::npos ? name : name.substr(0, at);
}

const char *settings_id_key(PresetKind kind)
{
    return kind == PresetKind::Machine ? "printer_settings_id" : kind == PresetKind::Filament ? "filament_settings_id" : "print_settings_id";
}

void finalize_flat(json &flat, PresetKind kind, const std::string &name, const std::string &version)
{
    flat["type"]              = kind_folder(kind);
    flat["name"]              = name;
    flat["from"]              = "User";
    flat["inherits"]          = "";
    flat["instantiation"]     = "true";
    flat["is_custom_defined"] = "0";
    flat["version"]           = version.empty() ? std::string("2.5.0.0") : version;
    if (kind == PresetKind::Filament)
        flat[settings_id_key(kind)] = json::array({name});
    else
        flat[settings_id_key(kind)] = name;
}

} // namespace

std::vector<fs::path> default_orca_data_dirs()
{
    std::vector<fs::path> dirs;
    auto env = [](const char *k) -> std::string {
        const char *v = std::getenv(k);
        return v ? std::string(v) : std::string();
    };
#ifdef _WIN32
    if (const std::string appdata = env("APPDATA"); !appdata.empty())
        dirs.push_back(fs::path(appdata) / "OrcaSlicer");
#elif defined(__APPLE__)
    if (const std::string home = env("HOME"); !home.empty())
        dirs.push_back(fs::path(home) / "Library" / "Application Support" / "OrcaSlicer");
#else
    const std::string home = env("HOME");
    std::string xdg = env("XDG_CONFIG_HOME");
    if (xdg.empty() && !home.empty())
        xdg = home + "/.config";
    if (!xdg.empty())
        dirs.push_back(fs::path(xdg) / "OrcaSlicer");
    if (!home.empty()) {
        dirs.push_back(fs::path(home) / ".var" / "app" / "com.orcaslicer.OrcaSlicer" / "config" / "OrcaSlicer");
        dirs.push_back(fs::path(home) / ".var" / "app" / "io.github.softfever.OrcaSlicer" / "config" / "OrcaSlicer");
    }
#endif
    std::stable_partition(dirs.begin(), dirs.end(), [](const fs::path &p) {
        std::error_code ec;
        return fs::is_directory(p / "user", ec);
    });
    return dirs;
}

std::string preset_file_name(const std::string &preset_name)
{
    std::string out = preset_name;
    for (char &c : out)
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            c = '_';
    return out + ".json";
}

OrcaScan scan_orca(const fs::path &orca_data_dir, const std::vector<fs::path> &extra_profile_dirs)
{
    OrcaScan scan;
    scan.data_dir = orca_data_dir;
    std::error_code ec;

    // 1) User presets: <data>/user/<user id or "default">/<kind>/*.json
    struct Raw { PresetKind kind; fs::path file; json j; };
    std::vector<Raw> raws;
    const fs::path user_root = orca_data_dir / "user";
    if (!fs::is_directory(user_root, ec)) {
        scan.warnings.push_back("no user folder in " + orca_data_dir.u8string());
        return scan;
    }
    std::set<std::string> seen; // the same preset may exist in several user folders (cloud + default)
    for (const auto &user_dir : fs::directory_iterator(user_root, fs::directory_options::skip_permission_denied, ec)) {
        if (!user_dir.is_directory(ec))
            continue;
        for (PresetKind kind : {PresetKind::Machine, PresetKind::Filament, PresetKind::Process}) {
            const fs::path dir = user_dir.path() / kind_folder(kind);
            if (!fs::is_directory(dir, ec))
                continue;
            for (const auto &f : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
                if (!f.is_regular_file(ec) || f.path().extension() != ".json")
                    continue;
                json j;
                std::string err;
                if (!read_json(f.path(), j, err)) {
                    scan.warnings.push_back(err);
                    continue;
                }
                std::string name = get_str(j, "name");
                if (name.empty())
                    name = f.path().stem().u8string();
                if (!seen.insert(std::string(kind_folder(kind)) + "/" + name).second)
                    continue;
                raws.push_back({kind, f.path(), std::move(j)});
            }
        }
    }

    // 2) Parents: user presets first (a user preset may inherit another one), then Orca's system copy,
    //    then the extra folders (Orca install resources/profiles, ZEBU's own resources/profiles).
    Index index;
    for (const Raw &r : raws)
        index.add(r.kind, get_str(r.j, "name").empty() ? r.file.stem().u8string() : get_str(r.j, "name"), Entry{r.kind, "", r.j});
    index_profile_root(orca_data_dir / "system", index, scan.warnings);
    for (const fs::path &extra : extra_profile_dirs)
        index_profile_root(extra, index, scan.warnings);
    scan.system_presets_indexed = index.size() - raws.size();
    Resolver resolver(index);

    // 3) Flatten.
    for (const Raw &r : raws) {
        OrcaUserPreset p;
        p.kind = r.kind;
        p.file = r.file;
        p.name = get_str(r.j, "name").empty() ? r.file.stem().u8string() : get_str(r.j, "name");
        json flat = json::object();
        const std::string parent = get_str(r.j, "inherits");
        std::string err;
        if (!parent.empty() && !resolver.resolve(r.kind, parent, "", flat, err)) {
            p.error = err;
        } else {
            merge_settings(flat, r.j);
            finalize_flat(flat, r.kind, p.name, get_str(r.j, "version"));
            p.flat = std::move(flat);
        }
        if (r.kind != PresetKind::Machine)
            p.orca_printers = get_str_list(r.j, "compatible_printers");
        scan.list(r.kind).push_back(std::move(p));
    }

    // 4) Proposed bindings, among the user's own printers only.
    std::set<std::string> machine_names;
    for (const auto &m : scan.machines)
        if (m.error.empty())
            machine_names.insert(m.name);
    for (PresetKind kind : {PresetKind::Filament, PresetKind::Process})
        for (auto &p : scan.list(kind)) {
            for (const std::string &printer : p.orca_printers)
                if (machine_names.count(printer))
                    p.suggested_printers.push_back(printer);
            if (p.suggested_printers.empty()) {
                const size_t at = p.name.rfind(" @");
                if (at != std::string::npos && machine_names.count(p.name.substr(at + 2)))
                    p.suggested_printers.push_back(p.name.substr(at + 2));
            }
        }

    auto by_name = [](const OrcaUserPreset &a, const OrcaUserPreset &b) { return a.name < b.name; };
    std::sort(scan.machines.begin(), scan.machines.end(), by_name);
    std::sort(scan.filaments.begin(), scan.filaments.end(), by_name);
    std::sort(scan.processes.begin(), scan.processes.end(), by_name);
    return scan;
}

ImportResult write_import(const OrcaScan &scan, const ImportPlan &plan, const fs::path &zebu_user_dir)
{
    ImportResult result;
    std::error_code ec;

    auto write_one = [&](PresetKind kind, const std::string &name, const json &content) {
        const fs::path dir = zebu_user_dir / kind_folder(kind);
        fs::create_directories(dir, ec);
        // u8path: the name is UTF-8. A plain std::string would be read in the Windows ANSI code page and
        // "Précision" would become "PrÃ©cision" in the file name (the preset name shown by the app).
        const fs::path file = dir / fs::u8path(preset_file_name(name));
        const std::string label = std::string(kind_folder(kind)) + "/" + name;
        if (fs::exists(file, ec) && !plan.overwrite) {
            result.skipped.push_back(label);
            return;
        }
        // Written next to the target first, then renamed, so a failure never leaves a truncated preset.
        fs::path tmp = file;
        tmp += ".zebu-tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out) {
                result.errors.push_back("cannot write " + file.u8string());
                return;
            }
            out << content.dump(4) << '\n';
            if (!out) {
                result.errors.push_back("cannot write " + file.u8string());
                return;
            }
        }
        fs::rename(tmp, file, ec);
        if (ec) {
            fs::remove(tmp, ec);
            result.errors.push_back("cannot write " + file.u8string() + ": " + ec.message());
            return;
        }
        // A stale .info (cloud sync state) from an earlier preset of the same name must not apply.
        fs::path info = file;
        info.replace_extension(".info");
        fs::remove(info, ec);
        result.written.push_back(label);
    };

    auto find = [&](PresetKind kind, const std::string &name) -> const OrcaUserPreset * {
        for (const auto &p : scan.list(kind))
            if (p.name == name)
                return &p;
        return nullptr;
    };

    const std::set<std::string> machines(plan.machines.begin(), plan.machines.end());
    for (const std::string &name : plan.machines) {
        const OrcaUserPreset *p = find(PresetKind::Machine, name);
        if (p == nullptr || !p->error.empty()) {
            result.errors.push_back("machine/" + name + (p ? ": " + p->error : ": not found"));
            continue;
        }
        write_one(PresetKind::Machine, name, p->flat);
    }

    auto write_bound = [&](PresetKind kind, const std::map<std::string, std::vector<std::string>> &bindings) {
        for (const auto &[name, targets] : bindings) {
            std::vector<std::string> bound;
            for (const std::string &m : targets)
                if (machines.count(m) && std::find(bound.begin(), bound.end(), m) == bound.end())
                    bound.push_back(m);
            if (bound.empty())
                continue;
            const OrcaUserPreset *p = find(kind, name);
            if (p == nullptr || !p->error.empty()) {
                result.errors.push_back(std::string(kind_folder(kind)) + "/" + name + (p ? ": " + p->error : ": not found"));
                continue;
            }
            auto emit = [&](const std::string &out_name, const std::vector<std::string> &printers) {
                json j = p->flat;
                finalize_flat(j, kind, out_name, get_str(j, "version"));
                j["compatible_printers"]           = printers;
                j["compatible_printers_condition"] = "";
                if (kind == PresetKind::Process)
                    j.erase("compatible_prints");
                write_one(kind, out_name, j);
            };
            if (plan.duplicate_per_machine && bound.size() > 1) {
                for (const std::string &m : bound)
                    emit(strip_at_suffix(name) + " @" + m, {m});
            } else {
                emit(name, bound);
            }
        }
    };
    write_bound(PresetKind::Filament, plan.filament_bindings);
    write_bound(PresetKind::Process, plan.process_bindings);
    return result;
}

}} // namespace Slic3r::Zebu
