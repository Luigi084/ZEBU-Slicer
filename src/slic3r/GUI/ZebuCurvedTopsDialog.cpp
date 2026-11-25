// ZEBU Slicer: analysis of curved (non-planar) top surfaces for the selected object.
#include "ZebuCurvedTopsDialog.hpp"

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/utils.h>

#include "libslic3r/Color.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Zebu/CurvedTops.hpp"

#include "GLCanvas3D.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "Plater.hpp"
#include "Selection.hpp"

namespace Slic3r { namespace GUI {

namespace {

// The model parts of one instance of an object, in printer coordinates.
Zebu::SimpleMesh instance_mesh(const ModelObject &object, int instance_idx)
{
    Zebu::SimpleMesh   mesh;
    const Transform3d &instance = object.instances[instance_idx]->get_matrix();
    for (const ModelVolume *volume : object.volumes) {
        if (!volume->is_model_part())
            continue;
        const Transform3d          trafo  = instance * volume->get_matrix();
        const indexed_triangle_set &its   = volume->mesh().its;
        const int                   offset = int(mesh.vertices.size());
        for (const auto &v : its.vertices) {
            const Vec3d p = trafo * v.cast<double>();
            mesh.vertices.push_back({float(p.x()), float(p.y()), float(p.z())});
        }
        // A mirroring transformation turns the faces inside out: keep them facing out.
        const bool flip = trafo.linear().determinant() < 0.;
        for (const auto &f : its.indices)
            mesh.faces.push_back(flip ? std::array<int, 3>{offset + f[0], offset + f[2], offset + f[1]} :
                                        std::array<int, 3>{offset + f[0], offset + f[1], offset + f[2]});
    }
    return mesh;
}

std::string head_model_path(const std::string &value)
{
    if (value.find('/') != std::string::npos || value.find('\\') != std::string::npos)
        return value;
    return (boost::filesystem::path(resources_dir()) / "zebu" / "heads" / (value + ".json")).string();
}

class CurvedTopsDialog : public wxDialog
{
public:
    explicit CurvedTopsDialog(wxWindow *parent)
        : wxDialog(parent, wxID_ANY, _L("Curved top surfaces analysis"), wxDefaultPosition, wxDefaultSize,
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    {
        auto *top = new wxBoxSizer(wxVERTICAL);
        m_header  = new wxStaticText(this, wxID_ANY, wxEmptyString);
        top->Add(m_header, 0, wxALL | wxEXPAND, FromDIP(10));
        m_list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(760), FromDIP(220)), wxLC_REPORT | wxLC_SINGLE_SEL);
        const wxString columns[] = {_L("Surface"), _L("Area"), _L("Height"), _L("Max slope"), _L("Spare gap"), _L("Passing"), _L("Result")};
        const int      widths[]  = {70, 90, 90, 90, 100, 90, 210};
        for (int i = 0; i < 7; ++i)
            m_list->AppendColumn(columns[i], wxLIST_FORMAT_LEFT, FromDIP(widths[i]));
        top->Add(m_list, 1, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(10));
        m_footer = new wxStaticText(this, wxID_ANY, wxEmptyString);
        top->Add(m_footer, 0, wxALL | wxEXPAND, FromDIP(10));
        auto *buttons = new wxBoxSizer(wxHORIZONTAL);
        auto *again   = new wxButton(this, wxID_REFRESH, _L("Analyse again"));
        auto *close   = new wxButton(this, wxID_CLOSE, _L("Close"));
        buttons->AddStretchSpacer();
        buttons->Add(again, 0, wxRIGHT, FromDIP(8));
        buttons->Add(close, 0);
        top->Add(buttons, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, FromDIP(10));
        SetSizerAndFit(top);

        again->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { this->analyse(); });
        close->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { this->Close(); });
        Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) {
            if (Plater *plater = wxGetApp().plater())
                if (GLCanvas3D *canvas = plater->get_view3D_canvas3D())
                    canvas->zebu_clear_overlay();
            s_instance = nullptr;
            this->Destroy();
        });
    }

    static CurvedTopsDialog *s_instance;

    void analyse()
    {
        m_list->DeleteAllItems();
        m_footer->SetLabel(wxEmptyString);
        Plater *plater = wxGetApp().plater();
        if (plater == nullptr)
            return;
        GLCanvas3D *canvas = plater->get_view3D_canvas3D();
        if (canvas)
            canvas->zebu_clear_overlay();

        const Selection &selection = plater->get_selection();
        const int        obj_idx   = selection.is_empty() ? -1 : selection.get_object_idx();
        const int        inst_idx  = selection.is_empty() ? -1 : selection.get_instance_idx();
        Model           &model     = plater->model();
        if (obj_idx < 0 || obj_idx >= int(model.objects.size()) || inst_idx < 0 || inst_idx >= int(model.objects[obj_idx]->instances.size())) {
            m_header->SetLabel(_L("Select one object (or one of its instances) on the plate, then click \"Analyse again\"."));
            this->Layout();
            return;
        }
        const ModelObject &object = *model.objects[obj_idx];

        PresetBundle        &bundle  = *wxGetApp().preset_bundle;
        const DynamicPrintConfig &printer = bundle.printers.get_edited_preset().config;
        const DynamicPrintConfig &print   = bundle.prints.get_edited_preset().config;
        const std::string head_value = printer.has("zebu_head_model") ? printer.opt_string("zebu_head_model") : std::string();
        if (head_value.empty()) {
            wxString message = _L("This printer has no toolhead model. Choose one in the printer settings, "
                                  "section \"Curved top surfaces (ZEBU)\".");
            // A hint from the printer's name; never chosen silently: a wrong model would be unsafe.
            const std::string name = boost::algorithm::to_lower_copy(bundle.printers.get_edited_preset().name);
            for (const auto &[word, id] : {std::make_pair("g3d", "g3d-pro-360-stealthburner-eddy"),
                                           std::make_pair("raise", "raise-k2plus-eva-sb"),
                                           std::make_pair("zortrax", "zortrax-m200k")})
                if (name.find(word) != std::string::npos) {
                    message += "\n" + wxString::Format(_L("For this printer, probably: %s"), id);
                    break;
                }
            m_header->SetLabel(message);
            this->Layout();
            return;
        }
        Zebu::HeadUnderside head;
        std::string         error;
        if (!head.load(head_model_path(head_value), &error)) {
            m_header->SetLabel(wxString::Format(_L("The toolhead model \"%s\" cannot be read: %s"), wxString::FromUTF8(head_value),
                                                wxString::FromUTF8(error)));
            this->Layout();
            return;
        }

        Zebu::CurvedTopsParams params;
        params.max_angle_deg = printer.opt_float("zebu_curved_top_max_angle");
        params.margin        = printer.opt_float("zebu_curved_top_margin");
        params.layer_height  = print.opt_float("layer_height");
        params.curved_layers = std::max(1, print.opt_int("top_shell_layers"));

        std::vector<Zebu::CurvedTopPatch> patches;
        {
            wxBusyCursor busy;
            const Zebu::SimpleMesh mesh = instance_mesh(object, inst_idx);
            patches                     = Zebu::analyze_curved_tops(mesh, head, params);

            // Colour the surfaces in the 3D view.
            std::vector<std::pair<indexed_triangle_set, ColorRGBA>> overlay;
            for (const Zebu::CurvedTopPatch &patch : patches) {
                indexed_triangle_set its;
                for (int f : patch.facets) {
                    const int base = int(its.vertices.size());
                    for (int k = 0; k < 3; ++k) {
                        const auto &v = mesh.vertices[mesh.faces[f][k]];
                        its.vertices.emplace_back(v[0], v[1], v[2]);
                    }
                    its.indices.emplace_back(base, base + 1, base + 2);
                }
                overlay.emplace_back(std::move(its), patch.printable ? ColorRGBA(0.15f, 0.80f, 0.30f, 0.80f) :
                                                                       ColorRGBA(1.00f, 0.45f, 0.05f, 0.85f));
            }
            if (canvas)
                canvas->zebu_set_overlay(overlay);
        }

        m_header->SetLabel(wxString::Format(_L("Object \"%s\" — toolhead: %s\nMaximum slope %.0f°, margin %.2f mm, %d curved layers of %.2f mm."),
                                            wxString::FromUTF8(object.name), wxString::FromUTF8(head.name()), params.max_angle_deg,
                                            params.margin, params.curved_layers, params.layer_height));
        int printable = 0;
        for (size_t i = 0; i < patches.size(); ++i) {
            const Zebu::CurvedTopPatch &p   = patches[i];
            const long                  row = m_list->InsertItem(long(i), wxString::Format("%d", int(i + 1)));
            m_list->SetItem(row, 1, wxString::Format("%.0f mm²", p.area));
            m_list->SetItem(row, 2, wxString::Format("%.2f mm", p.z_max - p.z_min));
            m_list->SetItem(row, 3, wxString::Format("%.1f°", p.max_slope_deg));
            m_list->SetItem(row, 4, p.clearance >= head.cap() - 1e-6 ? wxString(_L("plenty")) : wxString::Format("%+.2f mm", p.clearance));
            m_list->SetItem(row, 5, wxString::Format("%.0f %%", 100. * p.ok_fraction));
            m_list->SetItem(row, 6, p.printable ? _L("Printable curved") : _L("The toolhead would touch"));
            printable += p.printable;
        }
        m_footer->SetLabel(patches.empty() ?
            _L("No top surface of this object is gentle enough to be printed curved.") :
            wxString::Format(_L("%d of %d surfaces can be printed curved (green in the 3D view; orange: the toolhead "
                                "would touch). Nothing is printed curved yet: this is an analysis."), printable, int(patches.size())));
        this->Layout();
    }

private:
    wxStaticText *m_header { nullptr };
    wxStaticText *m_footer { nullptr };
    wxListCtrl   *m_list { nullptr };
};

CurvedTopsDialog *CurvedTopsDialog::s_instance = nullptr;

} // namespace

void zebu_show_curved_tops_analysis(wxWindow *parent)
{
    if (CurvedTopsDialog::s_instance == nullptr) {
        CurvedTopsDialog::s_instance = new CurvedTopsDialog(parent);
        CurvedTopsDialog::s_instance->Show();
    } else
        CurvedTopsDialog::s_instance->Raise();
    CurvedTopsDialog::s_instance->analyse();
}

}} // namespace Slic3r::GUI
