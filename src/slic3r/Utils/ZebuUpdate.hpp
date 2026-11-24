// ZEBU Slicer: update check against GitHub Releases (logic only, no GUI; std + nlohmann::json).
#ifndef slic3r_Utils_ZebuUpdate_hpp_
#define slic3r_Utils_ZebuUpdate_hpp_

#include <string>

namespace Slic3r { namespace Zebu {

enum class UpdatePlatform { Windows, LinuxAppImage, Other };

struct ReleaseInfo
{
    bool        found {false};
    std::string version;     // "0.1.1-beta" (tag without the leading "v")
    std::string tag;         // "v0.1.1-beta"
    std::string name;
    std::string notes;       // release body (markdown)
    std::string page_url;    // html_url of the release
    std::string asset_name;  // installer / AppImage for this platform ("" if none)
    std::string asset_url;   // browser_download_url
    std::string sha256_url;  // "<asset>.sha256" next to it, if published
    bool        prerelease {false};
};

// Semantic version comparison: -1 / 0 / 1. "0.1.0-beta" < "0.1.0-beta.2" < "0.1.0-rc" < "0.1.0" < "0.1.1-beta".
// A leading "v" is ignored. Unparsable versions compare equal.
int compare_versions(const std::string &a, const std::string &b);

// From GET https://api.github.com/repos/<owner>/<repo>/releases : the newest non-draft release strictly newer
// than current_version (pre-releases included only when allow_prerelease), with this platform's asset.
ReleaseInfo pick_update(const std::string &releases_json, const std::string &current_version, UpdatePlatform platform,
                        bool allow_prerelease);

// Lower-case hex digest from a "<hex>  <file name>" .sha256 file ("" when malformed).
std::string parse_sha256_file(const std::string &content);

}} // namespace Slic3r::Zebu

#endif
