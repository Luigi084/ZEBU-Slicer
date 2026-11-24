// ZEBU Slicer: application updates from the project's GitHub Releases.
//   Windows: downloads the installer, checks its SHA256, runs it silently in the current install folder
//            (/S /UPDATE) once ZEBU has closed; the installer restarts ZEBU.
//   Linux AppImage: replaces the running AppImage file in place and restarts it.
//   Elsewhere: opens the release page.
#ifndef slic3r_GUI_ZebuUpdater_hpp_
#define slic3r_GUI_ZebuUpdater_hpp_

namespace Slic3r { namespace GUI {

// by_user: from the Help menu - also reports "up to date" and errors, and ignores "skip this version".
void zebu_check_for_update(bool by_user);

// Called from GUI_App::OnExit(): starts the downloaded installer / restarts the updated AppImage, if any.
void zebu_run_pending_update();

}} // namespace Slic3r::GUI

#endif
