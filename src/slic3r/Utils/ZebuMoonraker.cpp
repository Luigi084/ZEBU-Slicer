// ZEBU Slicer: Moonraker (Klipper) status decoding. See ZebuMoonraker.hpp.
#include "ZebuMoonraker.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace Slic3r { namespace Zebu {

namespace {

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

double num(const json &obj, const char *key)
{
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_number())
        return NAN;
    return it->get<double>();
}

std::string str(const json &obj, const char *key)
{
    auto it = obj.find(key);
    return it != obj.end() && it->is_string() ? it->get<std::string>() : std::string();
}

// Moonraker's query string separates object names from attributes with '=' and keeps the space of
// "heater_generic chamber" URL-encoded.
std::string encode_object(const std::string &object)
{
    std::string out;
    for (char c : object)
        out += c == ' ' ? std::string("%20") : std::string(1, c);
    return out;
}

} // namespace

std::string normalize_base_url(const std::string &host)
{
    std::string h = host;
    // trim
    h.erase(h.begin(), std::find_if(h.begin(), h.end(), [](unsigned char c) { return !std::isspace(c); }));
    h.erase(std::find_if(h.rbegin(), h.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), h.end());
    if (h.empty())
        return h;
    if (lower(h).rfind("http://", 0) != 0 && lower(h).rfind("https://", 0) != 0)
        h = "http://" + h;
    while (!h.empty() && h.back() == '/')
        h.pop_back();
    return h;
}

std::string resolve_url(const std::string &base, const std::string &path_or_url)
{
    const std::string l = lower(path_or_url);
    if (l.rfind("http://", 0) == 0 || l.rfind("https://", 0) == 0)
        return path_or_url;
    if (path_or_url.empty())
        return base;
    return base + (path_or_url.front() == '/' ? "" : "/") + path_or_url;
}

std::string find_chamber_object(const std::string &objects_list_body)
{
    try {
        const json j       = json::parse(objects_list_body);
        const json &result = j.contains("result") ? j["result"] : j;
        if (!result.contains("objects") || !result["objects"].is_array())
            return {};
        std::string heater, sensor;
        for (const auto &o : result["objects"]) {
            if (!o.is_string())
                continue;
            const std::string name = o.get<std::string>();
            const std::string l    = lower(name);
            if (l.find("chamber") == std::string::npos)
                continue;
            if (heater.empty() && l.rfind("heater_generic ", 0) == 0)
                heater = name;
            else if (sensor.empty() && l.rfind("temperature_sensor ", 0) == 0)
                sensor = name;
        }
        return heater.empty() ? sensor : heater;
    } catch (...) {
        return {};
    }
}

std::string status_query_path(const std::string &chamber_object)
{
    std::string q = "/printer/objects/query?extruder=temperature,target&heater_bed=temperature,target"
                    "&print_stats=state,filename,print_duration,message,info"
                    "&display_status=progress,message&virtual_sdcard=progress";
    if (!chamber_object.empty())
        q += "&" + encode_object(chamber_object) + "=temperature,target";
    return q;
}

LiveStatus parse_status(const std::string &body, const std::string &chamber_object)
{
    LiveStatus s;
    json j;
    try {
        j = json::parse(body);
    } catch (const std::exception &e) {
        s.error = std::string("invalid answer: ") + e.what();
        return s;
    }
    const json *status = nullptr;
    if (j.contains("result") && j["result"].contains("status"))
        status = &j["result"]["status"];
    else if (j.contains("status"))
        status = &j["status"];
    if (status == nullptr || !status->is_object()) {
        s.error = j.contains("error") && j["error"].is_object() ? str(j["error"], "message") : std::string("no status in answer");
        if (s.error.empty())
            s.error = "no status in answer";
        return s;
    }
    s.ok = true;
    if (status->contains("extruder")) {
        s.nozzle        = num((*status)["extruder"], "temperature");
        s.nozzle_target = num((*status)["extruder"], "target");
    }
    if (status->contains("heater_bed")) {
        s.bed        = num((*status)["heater_bed"], "temperature");
        s.bed_target = num((*status)["heater_bed"], "target");
    }
    if (!chamber_object.empty() && status->contains(chamber_object)) {
        s.chamber        = num((*status)[chamber_object], "temperature");
        s.chamber_target = num((*status)[chamber_object], "target");
    }
    double sd_progress = -1, display_progress = -1;
    if (status->contains("virtual_sdcard"))
        if (double p = num((*status)["virtual_sdcard"], "progress"); !std::isnan(p))
            sd_progress = p;
    if (status->contains("display_status")) {
        const json &d = (*status)["display_status"];
        if (double p = num(d, "progress"); !std::isnan(p))
            display_progress = p;
        s.message = str(d, "message");
    }
    if (status->contains("print_stats")) {
        const json &p    = (*status)["print_stats"];
        s.print_state    = str(p, "state");
        s.filename       = str(p, "filename");
        s.print_duration = std::isnan(num(p, "print_duration")) ? 0 : num(p, "print_duration");
        if (s.message.empty())
            s.message = str(p, "message");
        if (p.contains("info") && p["info"].is_object()) {
            const json &info = p["info"];
            if (info.contains("current_layer") && info["current_layer"].is_number())
                s.layer = info["current_layer"].get<int>();
            if (info.contains("total_layer") && info["total_layer"].is_number())
                s.total_layers = info["total_layer"].get<int>();
        }
    }
    // display_status.progress follows M73 from the slicer (more accurate), virtual_sdcard is file position.
    s.progress = display_progress > 0 ? display_progress : sd_progress;
    if ((s.print_state == "printing" || s.print_state == "paused") && s.progress > 0.01 && s.print_duration > 0)
        s.eta_seconds = s.print_duration / s.progress - s.print_duration;
    return s;
}

std::string snapshot_url(const std::string &webcams_body, const std::string &base)
{
    try {
        const json j       = json::parse(webcams_body);
        const json &result = j.contains("result") ? j["result"] : j;
        if (result.contains("webcams") && result["webcams"].is_array())
            for (const auto &cam : result["webcams"]) {
                if (cam.contains("enabled") && cam["enabled"].is_boolean() && !cam["enabled"].get<bool>())
                    continue;
                const std::string snap = str(cam, "snapshot_url");
                if (!snap.empty())
                    return resolve_url(base, snap);
            }
    } catch (...) {
    }
    return resolve_url(base, "/webcam/?action=snapshot");
}

std::string format_duration(double seconds)
{
    if (seconds < 0 || std::isnan(seconds))
        return "-";
    const long s = long(seconds + 0.5);
    char buf[32];
    if (s >= 3600)
        std::snprintf(buf, sizeof(buf), "%ldh%02ld", s / 3600, (s % 3600) / 60);
    else if (s >= 60)
        std::snprintf(buf, sizeof(buf), "%ld min", s / 60);
    else
        std::snprintf(buf, sizeof(buf), "%ld s", s);
    return buf;
}

}} // namespace Slic3r::Zebu
