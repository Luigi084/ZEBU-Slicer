// ZEBU Slicer: import of OrcaSlicer user presets (printers, filaments, processes).
//
// OrcaSlicer stores user presets as diffs against a parent ("inherits") that is usually a vendor/system
// preset. ZEBU no longer ships the vendor profiles, so an imported preset is "flattened": its parent chain
// is resolved from OrcaSlicer's own system folder (<orca data>/system) or any extra profile folder, and the
// preset is written as a complete, parentless user preset. Filaments and processes are then bound to the
// machines chosen by the user (compatible_printers).
//
// This module only depends on the standard library and nlohmann::json so it can be unit tested without
// wxWidgets / boost.
#ifndef slic3r_Utils_ZebuOrcaImport_hpp_
#define slic3r_Utils_ZebuOrcaImport_hpp_

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Slic3r { namespace Zebu {

enum class PresetKind { Machine, Filament, Process };

const char *kind_folder(PresetKind kind); // "machine" / "filament" / "process"

struct OrcaUserPreset
{
    PresetKind               kind {PresetKind::Machine};
    std::string              name;
    std::filesystem::path    file;
    nlohmann::json           flat;               // complete preset after resolving the parent chain
    std::vector<std::string> orca_printers;      // Orca's compatible_printers (filament / process)
    std::vector<std::string> suggested_printers; // proposed ZEBU binding, among the Orca user printers
    std::string              error;              // non-empty: cannot be imported (e.g. parent not found)
};

struct OrcaScan
{
    std::filesystem::path       data_dir;
    std::vector<OrcaUserPreset> machines;
    std::vector<OrcaUserPreset> filaments;
    std::vector<OrcaUserPreset> processes;
    std::vector<std::string>    warnings;
    size_t                      system_presets_indexed {0};

    std::vector<OrcaUserPreset>       &list(PresetKind kind);
    const std::vector<OrcaUserPreset> &list(PresetKind kind) const;
};

// Usual OrcaSlicer data folders on this platform, existing ones first.
std::vector<std::filesystem::path> default_orca_data_dirs();

// Reads every user preset under <orca_data_dir>/user/*/{machine,filament,process}.
// Parents are looked up in the user presets themselves, then <orca_data_dir>/system, then extra_profile_dirs
// (folders laid out like resources/profiles: <Vendor>/{machine,filament,process}/...json).
OrcaScan scan_orca(const std::filesystem::path &orca_data_dir, const std::vector<std::filesystem::path> &extra_profile_dirs = {});

struct ImportPlan
{
    std::vector<std::string>                        machines;          // machine presets to import
    std::map<std::string, std::vector<std::string>> filament_bindings; // filament name -> machines; absent/empty = skip
    std::map<std::string, std::vector<std::string>> process_bindings;  // process name  -> machines; absent/empty = skip
    bool                                            overwrite {false};
    // A filament / process bound to several machines is written once per machine ("Name @Machine"),
    // so each machine gets its own copy to tune. When false, one preset lists all its machines.
    bool                                            duplicate_per_machine {true};
};

struct ImportResult
{
    std::vector<std::string> written;  // "machine/Name" ...
    std::vector<std::string> skipped;  // already present, not overwritten
    std::vector<std::string> errors;
};

// Writes the selected presets as complete user presets into zebu_user_dir/{machine,filament,process}.
ImportResult write_import(const OrcaScan &scan, const ImportPlan &plan, const std::filesystem::path &zebu_user_dir);

// File name used for a preset name (characters invalid in file names replaced).
std::string preset_file_name(const std::string &preset_name);

}} // namespace Slic3r::Zebu

#endif
