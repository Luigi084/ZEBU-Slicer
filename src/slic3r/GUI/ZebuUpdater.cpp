// ZEBU Slicer: application updates from GitHub Releases. See ZebuUpdater.hpp.
#include "ZebuUpdater.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/progdlg.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/filesystem/fstream.hpp>
#include <openssl/evp.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <sys/stat.h>
#endif

#include "libslic3r/libslic3r.h"

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "slic3r/Utils/Http.hpp"
#include "slic3r/Utils/ZebuUpdate.hpp"

namespace fs = boost::filesystem;

namespace Slic3r { namespace GUI {

namespace {

enum class Pending { None, RunInstaller, RestartAppImage };
Pending     g_pending = Pending::None;
fs::path    g_pending_path;

Zebu::UpdatePlatform platform()
{
#ifdef _WIN32
    return Zebu::UpdatePlatform::Windows;
#else
    const char *appimage = std::getenv("APPIMAGE");
    return appimage && *appimage ? Zebu::UpdatePlatform::LinuxAppImage : Zebu::UpdatePlatform::Other;
#endif
}

bool repo_configured()
{
    const std::string repo = ZEBU_UPDATE_REPO;
    return !repo.empty() && repo.find("CHANGE_ME") == std::string::npos && repo.find('/') != std::string::npos;
}

std::string sha256_hex(const std::string &data)
{
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int  len = 0;
    EVP_MD_CTX   *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, data.data(), data.size());
    EVP_DigestFinal_ex(ctx, md, &len);
    EVP_MD_CTX_free(ctx);
    std::string hex;
    char buf[3];
    for (unsigned int i = 0; i < len; ++i) {
        std::snprintf(buf, sizeof(buf), "%02x", md[i]);
        hex += buf;
    }
    return hex;
}

// Blocking download on the GUI thread with a cancellable progress dialog.
bool download(const std::string &url, std::string &out, const wxString &title, wxString &error)
{
    wxProgressDialog dlg(title, _L("Downloading..."), 1000, wxGetApp().mainframe, wxPD_APP_MODAL | wxPD_CAN_ABORT | wxPD_AUTO_HIDE | wxPD_ELAPSED_TIME);
    bool ok = false;
    Http::get(url)
        .timeout_connect(15)
        .on_progress([&dlg](Http::Progress p, bool &cancel) {
            const int v = p.dltotal > 0 ? int(1000.0 * double(p.dlnow) / double(p.dltotal)) : 0;
            const wxString msg = wxString::Format(_L("Downloading... %.1f / %.1f MB"), p.dlnow / 1048576.0, p.dltotal / 1048576.0);
            if (!dlg.Update(std::min(v, 999), msg))
                cancel = true;
        })
        .on_complete([&](std::string body, unsigned status) {
            ok  = status == 200 && !body.empty();
            out = std::move(body);
            if (!ok)
                error = wxString::Format("HTTP %u", status);
        })
        .on_error([&](std::string, std::string err, unsigned status) {
            error = wxString::FromUTF8(err.c_str()) + (status ? wxString::Format(" (HTTP %u)", status) : wxString());
        })
        .perform_sync();
    if (!ok && error.empty())
        error = _L("Download cancelled.");
    return ok;
}

bool write_file(const fs::path &path, const std::string &data)
{
    // boost::filesystem streams take the native (wide on Windows) path, so non-ASCII user folders work.
    fs::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f)
        return false;
    f.write(data.data(), std::streamsize(data.size()));
    return bool(f);
}

void install(const Zebu::ReleaseInfo &rel)
{
    wxWindow *parent = wxGetApp().mainframe;
    if (rel.asset_url.empty()) {
        wxLaunchDefaultBrowser(wxString::FromUTF8(rel.page_url.c_str()));
        return;
    }
    std::string data;
    wxString    error;
    if (!download(rel.asset_url, data, _L("ZEBU Slicer update"), error)) {
        wxMessageBox(_L("The update could not be downloaded.") + "\n" + error, _L("ZEBU Slicer update"), wxOK | wxICON_WARNING, parent);
        return;
    }
    if (!rel.sha256_url.empty()) {
        std::string sha_file;
        wxString    sha_error;
        const std::string expected = download(rel.sha256_url, sha_file, _L("ZEBU Slicer update"), sha_error) ? Zebu::parse_sha256_file(sha_file) : std::string();
        if (expected.empty() || expected != sha256_hex(data)) {
            wxMessageBox(_L("The downloaded file does not match its published checksum. The update was not installed."),
                         _L("ZEBU Slicer update"), wxOK | wxICON_ERROR, parent);
            return;
        }
    } else {
        BOOST_LOG_TRIVIAL(warning) << "ZEBU update: no .sha256 published for " << rel.asset_name << ", checksum not verified";
    }

#ifdef _WIN32
    const fs::path path = fs::path(wxStandardPaths::Get().GetTempDir().ToStdWstring()) / rel.asset_name;
    if (!write_file(path, data)) {
        wxMessageBox(_L("The update could not be saved to the temporary folder."), _L("ZEBU Slicer update"), wxOK | wxICON_WARNING, parent);
        return;
    }
    g_pending      = Pending::RunInstaller;
    g_pending_path = path;
    wxMessageBox(_L("ZEBU Slicer will now close to install the update, then start again."), _L("ZEBU Slicer update"), wxOK | wxICON_INFORMATION, parent);
    // The installer starts from OnExit(), so it only runs if ZEBU really closes (unsaved project prompt...).
    if (wxGetApp().mainframe)
        wxGetApp().mainframe->Close(false);
#else
    const char *appimage = std::getenv("APPIMAGE");
    if (appimage == nullptr || *appimage == 0) {
        wxLaunchDefaultBrowser(wxString::FromUTF8(rel.page_url.c_str()));
        return;
    }
    // Replace the AppImage in place: the running instance keeps its (now unlinked) file open.
    const fs::path target = fs::path(appimage);
    const fs::path tmp    = target.string() + ".zebu-new";
    boost::system::error_code ec;
    if (!write_file(tmp, data) || ::chmod(tmp.string().c_str(), 0755) != 0) {
        fs::remove(tmp, ec);
        wxMessageBox(_L("The update could not be written next to the AppImage (is its folder writable?)."), _L("ZEBU Slicer update"),
                     wxOK | wxICON_WARNING, parent);
        return;
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(tmp, ec);
        wxMessageBox(_L("The AppImage could not be replaced."), _L("ZEBU Slicer update"), wxOK | wxICON_WARNING, parent);
        return;
    }
    if (wxMessageBox(_L("The update is installed. Restart ZEBU Slicer now?"), _L("ZEBU Slicer update"), wxYES_NO | wxICON_QUESTION, parent) == wxYES) {
        g_pending      = Pending::RestartAppImage;
        g_pending_path = target;
        if (wxGetApp().mainframe)
            wxGetApp().mainframe->Close(false);
    }
#endif
}

void offer(const Zebu::ReleaseInfo &rel, bool by_user)
{
    AppConfig *cfg = wxGetApp().app_config;
    if (!by_user && cfg->get("zebu_skip_version") == rel.version)
        return;
    wxString notes = wxString::FromUTF8(rel.notes.c_str());
    if (notes.length() > 1200)
        notes = notes.Left(1200) + "...";
    wxString msg = wxString::Format(_L("ZEBU Slicer %s is available (you have %s)."), wxString::FromUTF8(rel.version.c_str()),
                                    wxString::FromUTF8(ZEBU_VERSION));
    if (!notes.empty())
        msg += "\n\n" + notes;
    wxMessageDialog dlg(wxGetApp().mainframe, msg, _L("ZEBU Slicer update"), wxYES_NO | wxCANCEL | wxICON_INFORMATION);
    dlg.SetYesNoCancelLabels(rel.asset_url.empty() ? _L("Open the release page") : _L("Update"), _L("Later"), _L("Skip this version"));
    const int res = dlg.ShowModal();
    if (res == wxID_YES)
        install(rel);
    else if (res == wxID_CANCEL)
        cfg->set("zebu_skip_version", rel.version);
}

} // namespace

void zebu_check_for_update(bool by_user)
{
    AppConfig *cfg = wxGetApp().app_config;
    if (!by_user && !cfg->get_bool("zebu_check_updates"))
        return;
    if (!repo_configured()) {
        if (by_user)
            wxMessageBox(_L("This build has no update repository configured (ZEBU_UPDATE_REPO)."), _L("ZEBU Slicer update"),
                         wxOK | wxICON_INFORMATION, wxGetApp().mainframe);
        return;
    }
    // Beta: pre-releases are offered unless switched off.
    const bool        prerelease = cfg->get_bool("zebu_update_prerelease");
    const std::string url        = std::string("https://api.github.com/repos/") + ZEBU_UPDATE_REPO + "/releases?per_page=20";
    Http::get(url)
        .header("Accept", "application/vnd.github+json")
        .timeout_connect(10)
        .timeout_max(30)
        .on_complete([by_user, prerelease](std::string body, unsigned status) {
            wxGetApp().CallAfter([by_user, prerelease, body = std::move(body), status]() {
                if (status != 200) {
                    if (by_user)
                        wxMessageBox(wxString::Format(_L("Could not check for updates (HTTP %u)."), status), _L("ZEBU Slicer update"),
                                     wxOK | wxICON_WARNING, wxGetApp().mainframe);
                    return;
                }
                const Zebu::ReleaseInfo rel = Zebu::pick_update(body, ZEBU_VERSION, platform(), prerelease);
                if (rel.found)
                    offer(rel, by_user);
                else if (by_user)
                    wxMessageBox(wxString::Format(_L("ZEBU Slicer %s is up to date."), wxString::FromUTF8(ZEBU_VERSION)),
                                 _L("ZEBU Slicer update"), wxOK | wxICON_INFORMATION, wxGetApp().mainframe);
            });
        })
        .on_error([by_user](std::string, std::string error, unsigned) {
            BOOST_LOG_TRIVIAL(warning) << "ZEBU update check failed: " << error;
            if (by_user)
                wxGetApp().CallAfter([error]() {
                    wxMessageBox(_L("Could not check for updates.") + "\n" + wxString::FromUTF8(error.c_str()), _L("ZEBU Slicer update"),
                                 wxOK | wxICON_WARNING, wxGetApp().mainframe);
                });
        })
        .perform();
}

void zebu_run_pending_update()
{
    if (g_pending == Pending::None)
        return;
#ifdef _WIN32
    if (g_pending == Pending::RunInstaller) {
        // ShellExecute honours the installer's "requireAdministrator" manifest (UAC prompt); CreateProcess would not.
        ShellExecuteW(nullptr, L"open", g_pending_path.wstring().c_str(), L"/S /UPDATE", nullptr, SW_SHOWNORMAL);
    }
#else
    if (g_pending == Pending::RestartAppImage)
        wxExecute(wxString::FromUTF8(("\"" + g_pending_path.string() + "\"").c_str()), wxEXEC_ASYNC | wxEXEC_MAKE_GROUP_LEADER);
#endif
    g_pending = Pending::None;
}

}} // namespace Slic3r::GUI
