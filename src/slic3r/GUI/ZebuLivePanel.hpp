// ZEBU Slicer: "ZEBU Live" panel, shown in the Prepare sidebar under the printer selection.
// Camera snapshot + temperatures + job progress of the selected printer, read from its Moonraker host
// (the printer's "print_host" / physical printer connection). Polls only while visible.
#ifndef slic3r_GUI_ZebuLivePanel_hpp_
#define slic3r_GUI_ZebuLivePanel_hpp_

#include <memory>
#include <string>

#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/panel.h>
#include <wx/timer.h>

class wxStaticText;
class wxGauge;

namespace Slic3r { namespace GUI {

class ZebuLivePanel : public wxPanel
{
public:
    explicit ZebuLivePanel(wxWindow *parent);
    ~ZebuLivePanel() override;

private:
    struct Target
    {
        std::string base; // normalized base URL, empty = no host
        std::string apikey, user, password;
        std::string label; // printer name
        bool operator==(const Target &o) const { return base == o.base && apikey == o.apikey && user == o.user && password == o.password; }
    };

    Target current_target() const;
    void   on_status_timer();
    void   on_camera_timer();
    void   set_collapsed(bool collapsed);
    void   show_message(const wxString &msg);
    void   paint_camera(wxPaintEvent &evt);
    bool   active() const;

    // Background HTTP GET; done(ok, body) is called on the GUI thread, only while this panel lives.
    void get(const std::string &path_or_url, std::function<void(bool, const std::string &)> done, long timeout_s = 5);

    std::shared_ptr<bool> m_alive;

    wxPanel      *m_header {nullptr};
    wxStaticText *m_title {nullptr};
    wxStaticText *m_state {nullptr};
    wxPanel      *m_body {nullptr};
    wxPanel      *m_camera {nullptr};
    wxStaticText *m_temps {nullptr};
    wxStaticText *m_job {nullptr};
    wxGauge      *m_gauge {nullptr};

    wxTimer m_status_timer;
    wxTimer m_camera_timer;

    Target      m_target;
    bool        m_collapsed {false};
    bool        m_status_busy {false};
    bool        m_camera_busy {false};
    bool        m_objects_known {false};
    std::string m_chamber_object;
    std::string m_snapshot_url;
    int         m_camera_failures {0};
    wxImage     m_frame;
};

}} // namespace Slic3r::GUI

#endif
