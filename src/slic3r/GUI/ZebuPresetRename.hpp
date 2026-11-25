// ZEBU Slicer: filament / process presets follow a printer preset saved under a new name.
#ifndef slic3r_GUI_ZebuPresetRename_hpp_
#define slic3r_GUI_ZebuPresetRename_hpp_

#include <string>

class wxWindow;

namespace Slic3r { namespace GUI {

// Called by Tab::save_preset() once the printer preset `old_name` has been saved as `new_name`
// (the new one is selected). If user filament / process presets or physical printers are bound to
// `old_name`, asks whether the machine was renamed (they move to `new_name` and `old_name` is deleted),
// copied (both machines share them) or neither. Returns true if `old_name` was deleted.
bool zebu_after_printer_save_as(wxWindow *parent, const std::string &old_name, const std::string &new_name);

}} // namespace Slic3r::GUI

#endif
