// ZEBU Slicer: "Import from OrcaSlicer" dialog and the first-run welcome dialog.
#ifndef slic3r_GUI_ZebuImportDialog_hpp_
#define slic3r_GUI_ZebuImportDialog_hpp_

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <wx/dialog.h>

#include "slic3r/Utils/ZebuOrcaImport.hpp"

class wxCheckBox;
class wxCheckListBox;
class wxChoice;
class wxDirPickerCtrl;
class wxScrolledWindow;
class wxStaticText;
class wxFlexGridSizer;
class wxComboBox;

namespace Slic3r { namespace GUI {

class ZebuImportDialog : public wxDialog
{
public:
    explicit ZebuImportDialog(wxWindow *parent);

    // Name of the first machine imported by the last successful import (to select it afterwards).
    const std::string &first_imported_machine() const { return m_first_machine; }

private:
    struct Row
    {
        Zebu::PresetKind kind;
        std::string      name;
        wxCheckBox      *check  {nullptr};
        wxChoice        *choice {nullptr};
        std::vector<std::string> suggested; // the Orca binding when it names several machines
    };

    void scan();
    void rebuild_lists();
    void add_rows(Zebu::PresetKind kind, wxScrolledWindow *panel, wxFlexGridSizer *grid);
    void do_import();
    std::vector<std::string> checked_machines() const;
    std::vector<std::filesystem::path> extra_profile_dirs() const;

    wxDirPickerCtrl  *m_dir {nullptr};
    wxStaticText     *m_status {nullptr};
    wxCheckListBox   *m_machines {nullptr};
    wxScrolledWindow *m_filament_panel {nullptr};
    wxFlexGridSizer  *m_filament_grid {nullptr};
    wxScrolledWindow *m_process_panel {nullptr};
    wxFlexGridSizer  *m_process_grid {nullptr};
    wxCheckBox       *m_duplicate {nullptr};
    wxCheckBox       *m_overwrite {nullptr};

    std::unique_ptr<Zebu::OrcaScan> m_scan;
    std::vector<std::string>        m_importable_machines;
    std::vector<Row>                m_rows;
    std::string                     m_first_machine;
};

// Shown at the first start (no user printer yet): language, then import from Orca / create a printer.
class ZebuWelcomeDialog : public wxDialog
{
public:
    enum Choice { ChoiceLater = 0, ChoiceImport, ChoiceCreatePrinter };

    explicit ZebuWelcomeDialog(wxWindow *parent);

    Choice      choice() const { return m_choice; }
    // Canonical wx language name chosen (e.g. "fr_FR"), empty when unchanged.
    std::string language() const { return m_language; }

private:
    Choice                   m_choice {ChoiceLater};
    std::string              m_language;
    wxComboBox              *m_lang {nullptr};
    std::vector<std::string> m_lang_codes;
};

// Runs the welcome flow when ZEBU has no user printer yet. Returns true when a dialog was shown.
bool zebu_first_run(wxWindow *parent);
// File > Import > Import from OrcaSlicer...
void zebu_show_orca_import(wxWindow *parent);

}} // namespace Slic3r::GUI

#endif
