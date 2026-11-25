// ZEBU Slicer: analysis of curved (non-planar) top surfaces for the selected object.
#ifndef slic3r_GUI_ZebuCurvedTopsDialog_hpp_
#define slic3r_GUI_ZebuCurvedTopsDialog_hpp_

class wxWindow;

namespace Slic3r { namespace GUI {

// Analyses the selected object against the current printer's toolhead model, colours the result in the
// 3D view (green: printable curved, orange: the toolhead would touch) and lists the surfaces.
void zebu_show_curved_tops_analysis(wxWindow *parent);

}} // namespace Slic3r::GUI

#endif
