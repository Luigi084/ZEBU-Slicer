// ZEBU Slicer: "ZEBU Live" panel. See ZebuLivePanel.hpp.
#include "ZebuLivePanel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>

#include <wx/dcbuffer.h>
#include <wx/gauge.h>
#include <wx/mstream.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "slic3r/Utils/Http.hpp"
#include "slic3r/Utils/ZebuMoonraker.hpp"

namespace Slic3r { namespace GUI {

namespace {

constexpr int STATUS_PERIOD_MS = 2000;

std::string opt_str(const DynamicPrintConfig &cfg, const char *key)
{
    const auto *opt = cfg.option<ConfigOptionString>(key);
    return opt ? opt->value : std::string();
}

wxString temp_text(double t, double target)
{
    if (std::isnan(t))
        return "-";
    if (std::isnan(target) || target <= 0)
        return wxString::Format(wxString::FromUTF8("%.0f \xC2\xB0" "C"), t);
    return wxString::Format(wxString::FromUTF8("%.0f / %.0f \xC2\xB0" "C"), t, target);
}

wxString state_text(const std::string &state)
{
    if (state == "printing")  return _L("Printing");
    if (state == "paused")    return _L("Paused");
    if (state == "complete")  return _L("Finished");
    if (state == "cancelled") return _L("Cancelled");
    if (state == "error")     return _L("Error");
    if (state == "standby")   return _L("Ready");
    return wxString::FromUTF8(state.c_str());
}

} // namespace

ZebuLivePanel::ZebuLivePanel(wxWindow *parent)
    : wxPanel(parent, wxID_ANY), m_alive(std::make_shared<bool>(true)), m_status_timer(this), m_camera_timer(this)
{
    SetBackgroundColour(*wxWHITE);
    auto *top = new wxBoxSizer(wxVERTICAL);

    // Header: click to fold / unfold.
    m_header = new wxPanel(this, wxID_ANY);
    m_header->SetBackgroundColour(wxColour(0xF1, 0xF1, 0xF1));
    auto *hs = new wxBoxSizer(wxHORIZONTAL);
    m_title  = new wxStaticText(m_header, wxID_ANY, "ZEBU Live");
    wxFont bold = m_title->GetFont();
    bold.SetWeight(wxFONTWEIGHT_BOLD);
    m_title->SetFont(bold);
    m_state = new wxStaticText(m_header, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END | wxALIGN_RIGHT);
    hs->Add(m_title, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(8));
    hs->Add(m_state, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    m_header->SetSizer(hs);
    top->Add(m_header, 0, wxEXPAND);

    m_body  = new wxPanel(this, wxID_ANY);
    m_body->SetBackgroundColour(*wxWHITE);
    auto *bs = new wxBoxSizer(wxVERTICAL);
    m_camera = new wxPanel(m_body, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(180)));
    m_camera->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_camera->Bind(wxEVT_PAINT, &ZebuLivePanel::paint_camera, this);
    m_camera->Bind(wxEVT_SIZE, [this](wxSizeEvent &e) {
        // 16:9 box following the sidebar width.
        const int w = e.GetSize().GetWidth();
        if (w > 0 && std::abs(m_camera->GetMinSize().GetHeight() - w * 9 / 16) > 2) {
            m_camera->SetMinSize(wxSize(-1, w * 9 / 16));
            CallAfter([this]() { GetParent()->Layout(); });
        }
        m_camera->Refresh();
        e.Skip();
    });
    m_temps = new wxStaticText(m_body, wxID_ANY, wxEmptyString);
    m_job   = new wxStaticText(m_body, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_MIDDLE);
    m_gauge = new wxGauge(m_body, wxID_ANY, 1000, wxDefaultPosition, wxSize(-1, FromDIP(6)));
    bs->Add(m_camera, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
    bs->Add(m_temps, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
    bs->Add(m_job, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(4));
    bs->Add(m_gauge, 0, wxEXPAND | wxALL, FromDIP(8));
    m_body->SetSizer(bs);
    top->Add(m_body, 0, wxEXPAND);
    SetSizer(top);

    auto toggle = [this](wxMouseEvent &) { set_collapsed(!m_collapsed); };
    m_header->Bind(wxEVT_LEFT_DOWN, toggle);
    m_title->Bind(wxEVT_LEFT_DOWN, toggle);
    m_state->Bind(wxEVT_LEFT_DOWN, toggle);

    Bind(wxEVT_TIMER, [this](wxTimerEvent &e) {
        if (&e.GetTimer() == &m_status_timer)
            on_status_timer();
        else
            on_camera_timer();
    });

    const std::string interval = wxGetApp().app_config->get("zebu_live_camera_interval_ms");
    int cam_ms = interval.empty() ? 1000 : std::atoi(interval.c_str());
    if (cam_ms < 200)
        cam_ms = 200;
    set_collapsed(wxGetApp().app_config->get("zebu_live_collapsed") == "1");
    show_message(_L("No printer connection"));
    m_status_timer.Start(STATUS_PERIOD_MS);
    m_camera_timer.Start(cam_ms);
}

ZebuLivePanel::~ZebuLivePanel()
{
    *m_alive = false;
    m_status_timer.Stop();
    m_camera_timer.Stop();
}

void ZebuLivePanel::set_collapsed(bool collapsed)
{
    m_collapsed = collapsed;
    m_body->Show(!collapsed);
    m_title->SetLabel(collapsed ? wxString::FromUTF8("\xE2\x96\xB8 ZEBU Live") : wxString::FromUTF8("\xE2\x96\xBE ZEBU Live"));
    wxGetApp().app_config->set("zebu_live_collapsed", collapsed ? "1" : "0");
    Layout();
    if (GetParent())
        GetParent()->Layout();
}

bool ZebuLivePanel::active() const
{
    return IsShownOnScreen() && !m_target.base.empty();
}

ZebuLivePanel::Target ZebuLivePanel::current_target() const
{
    Target t;
    PresetBundle *bundle = wxGetApp().preset_bundle;
    if (bundle == nullptr)
        return t;
    const DynamicPrintConfig *cfg = nullptr;
    if (bundle->physical_printers.has_selection())
        cfg = bundle->physical_printers.get_selected_printer_config();
    if (cfg == nullptr || opt_str(*cfg, "print_host").empty())
        cfg = &bundle->printers.get_edited_preset().config;
    t.label    = bundle->printers.get_edited_preset().name;
    t.base     = Zebu::normalize_base_url(opt_str(*cfg, "print_host"));
    t.apikey   = opt_str(*cfg, "printhost_apikey");
    t.user     = opt_str(*cfg, "printhost_user");
    t.password = opt_str(*cfg, "printhost_password");
    return t;
}

void ZebuLivePanel::get(const std::string &path_or_url, std::function<void(bool, const std::string &)> done, long timeout_s)
{
    const std::string url = Zebu::resolve_url(m_target.base, path_or_url);
    auto http             = Http::get(url);
    http.timeout_connect(3).timeout_max(timeout_s);
    if (!m_target.apikey.empty())
        http.header("X-Api-Key", m_target.apikey);
    if (!m_target.user.empty())
        http.auth_basic(m_target.user, m_target.password);
    std::weak_ptr<bool> alive = m_alive;
    auto finish = [alive, done](bool ok, std::string body) {
        wxTheApp->CallAfter([alive, done, ok, body = std::move(body)]() {
            if (auto a = alive.lock(); a && *a)
                done(ok, body);
        });
    };
    http.on_complete([finish](std::string body, unsigned status) { finish(status >= 200 && status < 300, std::move(body)); })
        .on_error([finish](std::string body, std::string error, unsigned) { finish(false, error.empty() ? body : error); })
        .perform();
}

void ZebuLivePanel::show_message(const wxString &msg)
{
    m_state->SetLabel(msg);
    m_header->Layout();
}

void ZebuLivePanel::on_status_timer()
{
    const Target t = current_target();
    if (!(t == m_target)) {
        // Printer (or its connection) changed: start over.
        m_target         = t;
        m_objects_known  = false;
        m_chamber_object.clear();
        m_snapshot_url.clear();
        m_camera_failures = 0;
        m_frame           = wxImage();
        m_temps->SetLabel(wxEmptyString);
        m_job->SetLabel(wxEmptyString);
        m_gauge->SetValue(0);
        m_camera->Refresh();
        show_message(m_target.base.empty() ? _L("No Moonraker host set for this printer") : _L("Connecting..."));
    }
    if (!active() || m_status_busy)
        return;
    m_status_busy = true;

    if (!m_objects_known) {
        // Once per printer: find the chamber object (heater or sensor) if there is one.
        get("/printer/objects/list", [this](bool ok, const std::string &body) {
            m_status_busy = false;
            if (!ok) {
                show_message(_L("Printer unreachable"));
                return;
            }
            m_chamber_object = Zebu::find_chamber_object(body);
            m_objects_known  = true;
            on_status_timer();
        });
        return;
    }

    get(Zebu::status_query_path(m_chamber_object), [this](bool ok, const std::string &body) {
        m_status_busy = false;
        if (!ok) {
            show_message(_L("Printer unreachable"));
            return;
        }
        const Zebu::LiveStatus s = Zebu::parse_status(body, m_chamber_object);
        if (!s.ok) {
            show_message(wxString::FromUTF8(s.error.c_str()));
            return;
        }
        wxString temps = _L("Nozzle") + " " + temp_text(s.nozzle, s.nozzle_target) + "    " + _L("Bed") + " " + temp_text(s.bed, s.bed_target);
        if (!std::isnan(s.chamber))
            temps += "    " + _L("Chamber") + " " + temp_text(s.chamber, s.chamber_target);
        m_temps->SetLabel(temps);

        wxString job;
        const bool running = s.print_state == "printing" || s.print_state == "paused";
        if (!s.filename.empty() && (running || s.print_state == "complete")) {
            job = wxString::FromUTF8(s.filename.c_str());
            if (s.progress >= 0)
                job += wxString::Format("  %d %%", int(std::lround(s.progress * 100)));
            if (running && s.eta_seconds >= 0)
                job += "  " + _L("time left:") + " " + wxString::FromUTF8(Zebu::format_duration(s.eta_seconds).c_str());
            if (s.layer >= 0 && s.total_layers > 0)
                job += wxString::Format("  %s %d/%d", _L("layer"), s.layer, s.total_layers);
        } else if (!s.message.empty()) {
            job = wxString::FromUTF8(s.message.c_str());
        }
        m_job->SetLabel(job);
        m_gauge->SetValue(s.progress > 0 ? std::min(1000, int(s.progress * 1000)) : 0);
        show_message(wxString::FromUTF8(m_target.label.c_str()) + " - " + state_text(s.print_state));
        m_body->Layout();
    });
}

void ZebuLivePanel::on_camera_timer()
{
    if (!active() || m_collapsed || m_camera_busy || !m_objects_known)
        return;
    // After repeated failures (no camera), retry only every ~30 ticks.
    if (m_camera_failures >= 3 && (++m_camera_failures % 30) != 0)
        return;
    m_camera_busy = true;
    if (m_snapshot_url.empty()) {
        get("/server/webcams/list", [this](bool ok, const std::string &body) {
            m_camera_busy  = false;
            m_snapshot_url = Zebu::snapshot_url(ok ? body : std::string(), m_target.base);
        });
        return;
    }
    get(m_snapshot_url, [this](bool ok, const std::string &body) {
        m_camera_busy = false;
        wxImage img;
        if (ok && !body.empty()) {
            wxMemoryInputStream in(body.data(), body.size());
            img.LoadFile(in, wxBITMAP_TYPE_ANY);
        }
        if (!img.IsOk()) {
            ++m_camera_failures;
            return;
        }
        m_camera_failures = 0;
        m_frame           = img;
        m_camera->Refresh(false);
    }, 4);
}

void ZebuLivePanel::paint_camera(wxPaintEvent &)
{
    wxAutoBufferedPaintDC dc(m_camera);
    const wxSize sz = m_camera->GetClientSize();
    dc.SetBackground(wxBrush(wxColour(0x20, 0x20, 0x20)));
    dc.Clear();
    if (!m_frame.IsOk() || sz.x <= 0 || sz.y <= 0) {
        dc.SetTextForeground(wxColour(0xB0, 0xB0, 0xB0));
        const wxString msg = m_target.base.empty() ? _L("No camera") : (m_camera_failures >= 3 ? _L("No camera found") : _L("Waiting for camera..."));
        const wxSize   ts  = dc.GetTextExtent(msg);
        dc.DrawText(msg, (sz.x - ts.x) / 2, (sz.y - ts.y) / 2);
        return;
    }
    const double scale = std::min(double(sz.x) / m_frame.GetWidth(), double(sz.y) / m_frame.GetHeight());
    const int    w     = std::max(1, int(m_frame.GetWidth() * scale));
    const int    h     = std::max(1, int(m_frame.GetHeight() * scale));
    const wxBitmap bmp(m_frame.Scale(w, h, wxIMAGE_QUALITY_BILINEAR));
    dc.DrawBitmap(bmp, (sz.x - w) / 2, (sz.y - h) / 2);
}

}} // namespace Slic3r::GUI
