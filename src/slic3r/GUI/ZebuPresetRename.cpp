// ZEBU Slicer: filament / process presets follow a printer preset saved under a new name.
#include "ZebuPresetRename.hpp"

#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/msgdlg.h>
#include <wx/radiobut.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <boost/log/trivial.hpp>

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "GUI_App.hpp"
#include "I18N.hpp"

namespace Slic3r { namespace GUI {

namespace {

enum class Choice { Rename, Share, Nothing };

class ZebuRenameDialog : public wxDialog
{
public:
    ZebuRenameDialog(wxWindow *parent, const wxString &old_name, const wxString &new_name, const PresetBundle::ZebuBoundCount &count)
        : wxDialog(parent, wxID_ANY, _L("ZEBU Slicer - printer saved under a new name"), wxDefaultPosition, wxDefaultSize,
                   wxDEFAULT_DIALOG_STYLE)
    {
        auto *top = new wxBoxSizer(wxVERTICAL);

        wxString what = wxString::Format(_L("Bound to \"%s\": %d filament preset(s), %d process preset(s), %d printer connection(s)."),
                                         old_name, count.filaments, count.prints, count.physical);
        auto *intro = new wxStaticText(this, wxID_ANY, what);
        intro->Wrap(FromDIP(520));
        top->Add(intro, 0, wxALL, FromDIP(14));

        auto *question = new wxStaticText(this, wxID_ANY, wxString::Format(_L("What should \"%s\" get?"), new_name));
        top->Add(question, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(14));

        m_rename = new wxRadioButton(this, wxID_ANY,
                                     wxString::Format(_L("Rename: everything moves to \"%s\" and \"%s\" is deleted"), new_name, old_name),
                                     wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
        m_share  = new wxRadioButton(this, wxID_ANY,
                                     wxString::Format(_L("Share: \"%s\" and \"%s\" both use these presets"), old_name, new_name));
        m_none   = new wxRadioButton(this, wxID_ANY,
                                     wxString::Format(_L("Nothing: \"%s\" starts without presets, \"%s\" keeps them"), new_name, old_name));
        m_rename->SetValue(true);
        for (wxRadioButton *btn : {m_rename, m_share, m_none})
            top->Add(btn, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8) + FromDIP(6));

        if (wxSizer *buttons = CreateStdDialogButtonSizer(wxOK))
            top->Add(buttons, 0, wxALL | wxEXPAND, FromDIP(12));

        // Escape or the close box mean "Nothing": the safe answer, nothing is changed or deleted.
        SetEscapeId(wxID_CANCEL);
        SetSizerAndFit(top);
        CentreOnParent();
    }

    Choice choice() const { return m_rename->GetValue() ? Choice::Rename : m_share->GetValue() ? Choice::Share : Choice::Nothing; }

private:
    wxRadioButton *m_rename { nullptr };
    wxRadioButton *m_share  { nullptr };
    wxRadioButton *m_none   { nullptr };
};

} // namespace

bool zebu_after_printer_save_as(wxWindow *parent, const std::string &old_name, const std::string &new_name)
{
    PresetBundle *bundle = wxGetApp().preset_bundle;
    if (bundle == nullptr || old_name.empty() || new_name.empty() || old_name == new_name)
        return false;
    const Preset *old_preset = bundle->printers.find_preset(old_name, false, true);
    if (old_preset == nullptr || old_preset->name != old_name || !old_preset->is_user())
        return false;

    const PresetBundle::ZebuBoundCount count = bundle->zebu_count_bound_to_printer(old_name);
    if (!count.any())
        return false;

    ZebuRenameDialog dlg(parent, wxString::FromUTF8(old_name), wxString::FromUTF8(new_name), count);
    const Choice choice = dlg.ShowModal() == wxID_OK ? dlg.choice() : Choice::Nothing;
    if (choice == Choice::Nothing)
        return false;

    const PresetBundle::ZebuBoundCount changed = bundle->zebu_rebind_printer(old_name, new_name, choice == Choice::Share);
    BOOST_LOG_TRIVIAL(info) << "ZEBU: printer \"" << old_name << "\" -> \"" << new_name << "\" (" << (choice == Choice::Share ? "share" : "rename")
                            << "): " << changed.filaments << " filament(s), " << changed.prints << " process(es), " << changed.physical
                            << " physical printer(s)";
    if (choice == Choice::Share)
        return false;

    // A printer preset another one inherits from cannot go: its child would no longer load.
    for (const Preset &preset : bundle->printers)
        if (preset.inherits() == old_name) {
            wxMessageBox(wxString::Format(_L("\"%s\" was kept because the printer preset \"%s\" is based on it."),
                                          wxString::FromUTF8(old_name), wxString::FromUTF8(preset.name)),
                         "ZEBU Slicer", wxOK | wxICON_INFORMATION, parent);
            return false;
        }

    if (!bundle->printers.delete_preset(old_name)) {
        BOOST_LOG_TRIVIAL(warning) << "ZEBU: could not delete printer preset \"" << old_name << "\"";
        return false;
    }
    // delete_preset() does not move the selection index, so the saved preset is selected again by name.
    bundle->printers.select_preset_by_name(new_name, true);
    wxGetApp().app_config->clear_printer_settings(old_name);
    return true;
}

}} // namespace Slic3r::GUI
