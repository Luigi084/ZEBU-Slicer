// ZEBU Slicer: "Import from OrcaSlicer" dialog and the first-run welcome dialog.
#include "ZebuImportDialog.hpp"

#include <algorithm>
#include <set>

#include <wx/bitmap.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/checklst.h>
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/filepicker.h>
#include <wx/intl.h>
#include <wx/msgdlg.h>
#include <wx/notebook.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/utils.h>

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "Plater.hpp"
#include "Tab.hpp"
#include "CreatePresetsDialog.hpp"

namespace fs = std::filesystem;

namespace Slic3r { namespace GUI {

namespace {

fs::path to_path(const wxString &s)
{
#ifdef _WIN32
    return fs::path(s.ToStdWstring());
#else
    return fs::path(s.ToUTF8().data());
#endif
}

wxString from_path(const fs::path &p)
{
#ifdef _WIN32
    return wxString(p.wstring());
#else
    return wxString::FromUTF8(p.string().c_str());
#endif
}

fs::path zebu_user_dir()
{
    return to_path(wxString::FromUTF8(data_dir().c_str())) / PRESET_USER_DIR / DEFAULT_USER_FOLDER_NAME;
}

// Reloads the user presets from disk and refreshes the preset lists.
void reload_user_presets(const std::string &select_printer)
{
    GUI_App &app = wxGetApp();
    app.preset_bundle->load_user_presets(DEFAULT_USER_FOLDER_NAME, ForwardCompatibilitySubstitutionRule::Enable);
    if (!select_printer.empty() && app.preset_bundle->printers.find_preset(select_printer, false) != nullptr) {
        if (Tab *tab = app.get_tab(Preset::TYPE_PRINTER))
            tab->select_preset(select_printer);
    }
    app.load_current_presets();
    if (app.mainframe)
        app.mainframe->update_side_preset_ui();
    if (app.plater())
        app.plater()->sidebar().update_all_preset_comboboxes();
}

} // namespace

// ------------------------------------------------------------------------------------------------
// ZebuImportDialog
// ------------------------------------------------------------------------------------------------

ZebuImportDialog::ZebuImportDialog(wxWindow *parent)
    : wxDialog(parent, wxID_ANY, _L("Import from OrcaSlicer"), wxDefaultPosition, wxSize(820, 680),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    auto *top = new wxBoxSizer(wxVERTICAL);

    auto *intro = new wxStaticText(this, wxID_ANY,
        _L("Pick the OrcaSlicer configuration folder, then the printers to import. Each filament and process is "
           "bound to the printer you choose for it and will only be listed for that printer."));
    intro->Wrap(780);
    top->Add(intro, 0, wxALL | wxEXPAND, 10);

    auto *dir_row = new wxBoxSizer(wxHORIZONTAL);
    dir_row->Add(new wxStaticText(this, wxID_ANY, _L("OrcaSlicer folder:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    const auto dirs = Zebu::default_orca_data_dirs();
    m_dir = new wxDirPickerCtrl(this, wxID_ANY, dirs.empty() ? wxString() : from_path(dirs.front()), _L("OrcaSlicer folder"),
                                wxDefaultPosition, wxDefaultSize, wxDIRP_USE_TEXTCTRL | wxDIRP_DIR_MUST_EXIST);
    dir_row->Add(m_dir, 1, wxEXPAND);
    auto *scan_btn = new wxButton(this, wxID_ANY, _L("Analyse"));
    dir_row->Add(scan_btn, 0, wxLEFT, 6);
    top->Add(dir_row, 0, wxLEFT | wxRIGHT | wxEXPAND, 10);

    m_status = new wxStaticText(this, wxID_ANY, wxEmptyString);
    top->Add(m_status, 0, wxALL | wxEXPAND, 10);

    auto *book = new wxNotebook(this, wxID_ANY);

    auto *machines_page = new wxPanel(book);
    auto *mp_sizer      = new wxBoxSizer(wxVERTICAL);
    m_machines          = new wxCheckListBox(machines_page, wxID_ANY);
    mp_sizer->Add(m_machines, 1, wxALL | wxEXPAND, 6);
    machines_page->SetSizer(mp_sizer);
    book->AddPage(machines_page, _L("Printers"));

    // Each list page: a tool row (tick all / none, give the ticked presets a printer) above the scrolled list.
    auto make_list_page = [this, book](const wxString &title, Zebu::PresetKind kind, wxScrolledWindow *&panel,
                                       wxFlexGridSizer *&grid, wxChoice *&bulk) {
        auto *page  = new wxPanel(book);
        auto *vs    = new wxBoxSizer(wxVERTICAL);
        auto *tools = new wxBoxSizer(wxHORIZONTAL);
        auto *all   = new wxButton(page, wxID_ANY, _L("Tick all"));
        auto *none  = new wxButton(page, wxID_ANY, _L("Untick all"));
        bulk        = new wxChoice(page, wxID_ANY, wxDefaultPosition, wxSize(220, -1));
        auto *apply = new wxButton(page, wxID_ANY, _L("Apply"));
        tools->Add(all, 0, wxRIGHT, 4);
        tools->Add(none, 0, wxRIGHT, 16);
        tools->Add(new wxStaticText(page, wxID_ANY, _L("Printer for the ticked presets:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        tools->Add(bulk, 0, wxRIGHT, 4);
        tools->Add(apply, 0);
        vs->Add(tools, 0, wxALL, 6);
        panel = new wxScrolledWindow(page, wxID_ANY);
        panel->SetScrollRate(0, 12);
        grid = new wxFlexGridSizer(2, 4, 12);
        grid->AddGrowableCol(0, 1);
        panel->SetSizer(grid);
        vs->Add(panel, 1, wxEXPAND);
        page->SetSizer(vs);
        book->AddPage(page, title);
        all->Bind(wxEVT_BUTTON, [this, kind](wxCommandEvent &) { check_all(kind, true); });
        none->Bind(wxEVT_BUTTON, [this, kind](wxCommandEvent &) { check_all(kind, false); });
        wxChoice *b = bulk;
        apply->Bind(wxEVT_BUTTON, [this, kind, b](wxCommandEvent &) { assign_ticked(kind, b->GetSelection()); });
    };
    make_list_page(_L("Filaments"), Zebu::PresetKind::Filament, m_filament_panel, m_filament_grid, m_bulk_filament);
    make_list_page(_L("Processes"), Zebu::PresetKind::Process, m_process_panel, m_process_grid, m_bulk_process);
    top->Add(book, 1, wxLEFT | wxRIGHT | wxEXPAND, 10);

    m_duplicate = new wxCheckBox(this, wxID_ANY, _L("One copy per printer when a preset goes to several printers (\"Name @Printer\")"));
    m_duplicate->SetValue(true);
    m_overwrite = new wxCheckBox(this, wxID_ANY, _L("Overwrite ZEBU presets that have the same name"));
    top->Add(m_duplicate, 0, wxLEFT | wxRIGHT | wxTOP, 10);
    top->Add(m_overwrite, 0, wxLEFT | wxRIGHT | wxTOP, 10);

    auto *buttons    = new wxStdDialogButtonSizer();
    auto *import_btn = new wxButton(this, wxID_OK, _L("Import"));
    buttons->AddButton(import_btn);
    buttons->AddButton(new wxButton(this, wxID_CANCEL, _L("Cancel")));
    buttons->Realize();
    top->Add(buttons, 0, wxALL | wxALIGN_RIGHT, 10);

    SetSizer(top);
    SetMinSize(wxSize(640, 520));

    scan_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { scan(); });
    m_dir->Bind(wxEVT_DIRPICKER_CHANGED, [this](wxFileDirPickerEvent &) { scan(); });
    import_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { do_import(); });

    if (!dirs.empty())
        scan();
    CentreOnParent();
}

std::vector<fs::path> ZebuImportDialog::extra_profile_dirs() const
{
    std::vector<fs::path> out;
#ifdef _WIN32
    // OrcaSlicer's own install, in case its data folder misses a system profile.
    for (const char *env : {"ProgramW6432", "ProgramFiles"}) {
        wxString pf;
        if (wxGetEnv(env, &pf))
            out.push_back(to_path(pf) / "OrcaSlicer" / "resources" / "profiles");
    }
#endif
    out.push_back(to_path(wxString::FromUTF8(resources_dir().c_str())) / "profiles");
    return out;
}

void ZebuImportDialog::scan()
{
    const fs::path dir = to_path(m_dir->GetPath());
    wxBusyCursor busy;
    m_scan = std::make_unique<Zebu::OrcaScan>(Zebu::scan_orca(dir, extra_profile_dirs()));

    size_t broken = 0;
    for (auto kind : {Zebu::PresetKind::Machine, Zebu::PresetKind::Filament, Zebu::PresetKind::Process})
        for (const auto &p : m_scan->list(kind))
            broken += p.error.empty() ? 0 : 1;
    wxString status = wxString::Format(_L("%d printers, %d filaments, %d processes found."), int(m_scan->machines.size()),
                                       int(m_scan->filaments.size()), int(m_scan->processes.size()));
    if (broken > 0)
        status += " " + wxString::Format(_L("%d cannot be imported (parent profile missing), they are not listed."), int(broken));
    if (m_scan->machines.empty() && m_scan->filaments.empty() && m_scan->processes.empty())
        status = _L("No OrcaSlicer user preset found in this folder.");
    m_status->SetLabel(status);
    m_status->Wrap(780);
    rebuild_lists();
}

void ZebuImportDialog::rebuild_lists()
{
    m_machines->Clear();
    m_importable_machines.clear();
    m_rows.clear();
    m_filament_panel->DestroyChildren();
    m_process_panel->DestroyChildren();
    m_filament_grid->Clear(false);
    m_process_grid->Clear(false);
    if (!m_scan)
        return;

    for (const auto &m : m_scan->machines) {
        if (!m.error.empty())
            continue;
        m_importable_machines.push_back(m.name);
        const int idx = m_machines->Append(wxString::FromUTF8(m.name.c_str()));
        m_machines->Check(idx, true);
    }
    for (wxChoice *bulk : {m_bulk_filament, m_bulk_process}) {
        bulk->Clear();
        for (const std::string &m : m_importable_machines)
            bulk->Append(wxString::FromUTF8(m.c_str()));
        bulk->Append(_L("All imported printers"));
        bulk->SetSelection(0);
    }
    add_rows(Zebu::PresetKind::Filament, m_filament_panel, m_filament_grid);
    add_rows(Zebu::PresetKind::Process, m_process_panel, m_process_grid);
    m_filament_panel->FitInside();
    m_process_panel->FitInside();
    Layout();
}

void ZebuImportDialog::add_rows(Zebu::PresetKind kind, wxScrolledWindow *panel, wxFlexGridSizer *grid)
{
    wxArrayString choices;
    for (const std::string &m : m_importable_machines)
        choices.Add(wxString::FromUTF8(m.c_str()));
    const int idx_all = int(choices.size());
    choices.Add(_L("All imported printers"));

    for (const auto &p : m_scan->list(kind)) {
        if (!p.error.empty())
            continue;
        Row row;
        row.kind   = kind;
        row.name   = p.name;
        row.check  = new wxCheckBox(panel, wxID_ANY, wxString::FromUTF8(p.name.c_str()));
        row.choice = new wxChoice(panel, wxID_ANY, wxDefaultPosition, wxSize(260, -1), choices);
        int sel = 0;
        if (p.suggested_printers.size() == 1) {
            auto it = std::find(m_importable_machines.begin(), m_importable_machines.end(), p.suggested_printers.front());
            sel     = it == m_importable_machines.end() ? 0 : int(it - m_importable_machines.begin());
        } else if (p.suggested_printers.size() > 1) {
            // Keep Orca's exact list as an extra entry.
            wxString label = _L("As in OrcaSlicer:");
            for (const std::string &m : p.suggested_printers)
                label += " " + wxString::FromUTF8(m.c_str()) + ";";
            label.RemoveLast();
            sel           = int(row.choice->Append(label));
            row.suggested = p.suggested_printers;
        }
        if (m_importable_machines.empty())
            sel = idx_all;
        row.choice->SetSelection(sel);
        // Presets that Orca tied to one of these printers are pre-selected; the others are left to the user.
        row.check->SetValue(!p.suggested_printers.empty());
        grid->Add(row.check, 1, wxALIGN_CENTER_VERTICAL | wxEXPAND | wxLEFT, 6);
        grid->Add(row.choice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        m_rows.push_back(row);
    }
}

void ZebuImportDialog::check_all(Zebu::PresetKind kind, bool value)
{
    for (Row &row : m_rows)
        if (row.kind == kind)
            row.check->SetValue(value);
}

void ZebuImportDialog::assign_ticked(Zebu::PresetKind kind, int selection)
{
    // The bulk list holds the machines then "All imported printers": the same first entries as each row's list.
    if (selection < 0)
        return;
    for (Row &row : m_rows)
        if (row.kind == kind && row.check->GetValue())
            row.choice->SetSelection(selection);
}

std::vector<std::string> ZebuImportDialog::checked_machines() const
{
    std::vector<std::string> out;
    for (unsigned i = 0; i < m_machines->GetCount() && i < m_importable_machines.size(); ++i)
        if (m_machines->IsChecked(i))
            out.push_back(m_importable_machines[i]);
    return out;
}

void ZebuImportDialog::do_import()
{
    if (!m_scan)
        return;
    Zebu::ImportPlan plan;
    plan.machines              = checked_machines();
    plan.overwrite             = m_overwrite->GetValue();
    plan.duplicate_per_machine = m_duplicate->GetValue();
    const int idx_all          = int(m_importable_machines.size());
    for (const Row &row : m_rows) {
        if (!row.check->GetValue())
            continue;
        const int sel = row.choice->GetSelection();
        std::vector<std::string> targets;
        if (sel >= 0 && sel < idx_all)
            targets = {m_importable_machines[sel]};
        else if (sel == idx_all)
            targets = m_importable_machines;
        else
            targets = row.suggested;
        auto &bindings = row.kind == Zebu::PresetKind::Filament ? plan.filament_bindings : plan.process_bindings;
        bindings[row.name] = targets;
    }
    if (plan.machines.empty()) {
        wxMessageBox(_L("Select at least one printer: filaments and processes are imported together with their printer."),
                     _L("Import from OrcaSlicer"), wxOK | wxICON_INFORMATION, this);
        return;
    }

    Zebu::ImportResult result;
    {
        wxBusyCursor busy;
        result = Zebu::write_import(*m_scan, plan, zebu_user_dir());
    }
    m_first_machine.clear();
    for (const std::string &w : result.written)
        if (w.rfind("machine/", 0) == 0) {
            m_first_machine = w.substr(8);
            break;
        }

    wxString msg = wxString::Format(_L("%d presets imported."), int(result.written.size()));
    if (!result.skipped.empty())
        msg += "\n" + wxString::Format(_L("%d already existed and were kept (tick \"Overwrite\" to replace them)."), int(result.skipped.size()));
    if (!result.errors.empty()) {
        msg += "\n\n" + _L("Errors:");
        for (const std::string &e : result.errors)
            msg += "\n" + wxString::FromUTF8(e.c_str());
    }
    wxMessageBox(msg, _L("Import from OrcaSlicer"), wxOK | (result.errors.empty() ? wxICON_INFORMATION : wxICON_WARNING), this);
    if (!result.written.empty())
        EndModal(wxID_OK);
}

// ------------------------------------------------------------------------------------------------
// ZebuWelcomeDialog
// ------------------------------------------------------------------------------------------------

ZebuWelcomeDialog::ZebuWelcomeDialog(wxWindow *parent)
    : wxDialog(parent, wxID_ANY, _L("Welcome to ZEBU Slicer"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
    auto *top = new wxBoxSizer(wxVERTICAL);

    const fs::path logo = to_path(wxString::FromUTF8(resources_dir().c_str())) / "images" / "OrcaSlicer_192px.png";
    wxBitmap bmp;
    if (bmp.LoadFile(from_path(logo), wxBITMAP_TYPE_PNG))
        top->Add(new wxStaticBitmap(this, wxID_ANY, bmp), 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 12);

    auto *title = new wxStaticText(this, wxID_ANY, "ZEBU Slicer " + wxString::FromUTF8(ZEBU_VERSION));
    wxFont f = title->GetFont();
    f.SetPointSize(f.GetPointSize() + 6);
    f.SetWeight(wxFONTWEIGHT_BOLD);
    title->SetFont(f);
    top->Add(title, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 10);

    // Language: on Windows the installer already chose it; this is mostly for Linux (AppImage, no installer).
    auto *lang_row = new wxBoxSizer(wxHORIZONTAL);
    lang_row->Add(new wxStaticText(this, wxID_ANY, _L("Language:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    wxArrayString names;
    const std::string current = wxGetApp().app_config->get("language");
    int sel = 0;
    // Same set as Preferences: English + every catalog shipped in resources/i18n.
    std::vector<const wxLanguageInfo *> infos { wxLocale::GetLanguageInfo(wxLANGUAGE_ENGLISH) };
    const wxArrayString translations = wxTranslations::Get()->GetAvailableTranslations(SLIC3R_L10N_CATALOG);
    for (const wxString &t : translations)
        if (const wxLanguageInfo *li = wxLocale::FindLanguageInfo(t); li != nullptr && li != infos.front())
            infos.push_back(li);
    std::sort(infos.begin() + 1, infos.end(), [](const wxLanguageInfo *a, const wxLanguageInfo *b) { return a->Description < b->Description; });
    for (const wxLanguageInfo *li : infos) {
        const std::string code = li->CanonicalName.ToUTF8().data();
        if (!current.empty() && (code == current || (current.size() >= 2 && code.compare(0, 2, current, 0, 2) == 0 && sel == 0)))
            sel = int(names.size());
        m_lang_codes.push_back(code);
        names.Add(li->DescriptionNative.empty() ? li->Description : li->DescriptionNative);
    }
    m_lang = new wxComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(260, -1), names, wxCB_READONLY);
    m_lang->SetSelection(sel);
    lang_row->Add(m_lang, 1);
    top->Add(lang_row, 0, wxLEFT | wxRIGHT | wxEXPAND, 20);

    auto *text = new wxStaticText(this, wxID_ANY,
        _L("ZEBU Slicer ships without vendor profiles: your printers, filaments and processes are your own, and each "
           "filament or process belongs to one printer. Start by importing them from OrcaSlicer, or create a printer."));
    text->Wrap(460);
    top->Add(text, 0, wxALL, 20);

    auto *btns       = new wxBoxSizer(wxHORIZONTAL);
    auto *import_btn = new wxButton(this, wxID_ANY, _L("Import from OrcaSlicer..."));
    auto *create_btn = new wxButton(this, wxID_ANY, _L("Create a printer..."));
    auto *later_btn  = new wxButton(this, wxID_CANCEL, _L("Later"));
    btns->Add(import_btn, 0, wxRIGHT, 8);
    btns->Add(create_btn, 0, wxRIGHT, 8);
    btns->Add(later_btn, 0);
    top->Add(btns, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 16);
    SetSizerAndFit(top);
    CentreOnParent();

    auto finish = [this](Choice c) {
        m_choice = c;
        const int i = m_lang->GetSelection();
        if (i >= 0 && size_t(i) < m_lang_codes.size() && m_lang_codes[i] != wxGetApp().app_config->get("language"))
            m_language = m_lang_codes[i];
        EndModal(c == ChoiceLater ? wxID_CANCEL : wxID_OK);
    };
    import_btn->Bind(wxEVT_BUTTON, [finish](wxCommandEvent &) { finish(ChoiceImport); });
    create_btn->Bind(wxEVT_BUTTON, [finish](wxCommandEvent &) { finish(ChoiceCreatePrinter); });
    later_btn->Bind(wxEVT_BUTTON, [finish](wxCommandEvent &) { finish(ChoiceLater); });
}

// ------------------------------------------------------------------------------------------------

void zebu_show_orca_import(wxWindow *parent)
{
    ZebuImportDialog dlg(parent);
    if (dlg.ShowModal() == wxID_OK)
        reload_user_presets(dlg.first_imported_machine());
}

static bool has_user_printer()
{
    for (const Preset &p : wxGetApp().preset_bundle->printers)
        if (p.is_user())
            return true;
    return false;
}

bool zebu_first_run(wxWindow *parent)
{
    AppConfig *cfg = wxGetApp().app_config;
    if (has_user_printer() || cfg->get("zebu_welcome_done") == "1")
        return false;

    ZebuWelcomeDialog dlg(parent);
    dlg.ShowModal();
    cfg->set("zebu_welcome_done", "1");
    cfg->set("firstguide", "finish", "true");

    switch (dlg.choice()) {
    case ZebuWelcomeDialog::ChoiceImport: zebu_show_orca_import(parent); break;
    case ZebuWelcomeDialog::ChoiceCreatePrinter:
        if (wxGetApp().plater())
            wxGetApp().plater()->sidebar().create_printer_preset();
        break;
    default: break;
    }
    if (!dlg.language().empty())
        wxGetApp().zebu_switch_language(dlg.language()); // rebuilds the GUI, so keep it last
    return true;
}

}} // namespace Slic3r::GUI
