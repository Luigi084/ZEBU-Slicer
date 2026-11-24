// ZEBU Slicer: Moonraker (Klipper) status decoding for the ZEBU Live panel.
// Standard library + nlohmann::json only, so it can be tested without the GUI.
#ifndef slic3r_Utils_ZebuMoonraker_hpp_
#define slic3r_Utils_ZebuMoonraker_hpp_

#include <cmath>
#include <string>

namespace Slic3r { namespace Zebu {

struct LiveStatus
{
    bool        ok {false};
    std::string error;

    double nozzle {NAN}, nozzle_target {NAN};
    double bed {NAN}, bed_target {NAN};
    double chamber {NAN}, chamber_target {NAN}; // target stays NAN for a plain sensor

    std::string print_state;   // standby / printing / paused / complete / cancelled / error
    std::string filename;
    std::string message;       // display_status.message (M117) or print_stats.message
    double      progress {-1}; // 0..1, -1 = unknown
    double      print_duration {0};
    double      eta_seconds {-1};
    int         layer {-1}, total_layers {-1};
};

// "192.168.1.89", "g3d.local:7125/", "https://x" -> "http://192.168.1.89" etc. (no trailing slash)
std::string normalize_base_url(const std::string &host);
// Absolute URL from a base and a path or URL returned by Moonraker.
std::string resolve_url(const std::string &base, const std::string &path_or_url);

// From GET /printer/objects/list: the Klipper object holding the chamber temperature ("" when none).
// Prefers a heater (heater_generic) over a sensor, both matched on "chamber" in their name.
std::string find_chamber_object(const std::string &objects_list_body);

// Path + query for GET, e.g. "/printer/objects/query?extruder=temperature,target&...".
std::string status_query_path(const std::string &chamber_object);

// Decodes the answer of status_query_path().
LiveStatus parse_status(const std::string &body, const std::string &chamber_object);

// From GET /server/webcams/list: absolute snapshot URL of the first enabled webcam, or the crowsnest default.
std::string snapshot_url(const std::string &webcams_body, const std::string &base);

// "1h12" / "12 min" / "45 s"
std::string format_duration(double seconds);

}} // namespace Slic3r::Zebu

#endif
