#include "NightlyUpdater.hpp"

#include "BuildCommit.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "GUI_Utils.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "MsgDialog.hpp"
#include "NotificationManager.hpp"
#include "Plater.hpp"
#include "format.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/NightlyManifest.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r_version.h"
#include "slic3r/Utils/Http.hpp"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

#include <wx/stdpaths.h>
#include <wx/utils.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r { namespace GUI {

namespace {

const char *const RELEASE_PAGE  = "https://github.com/NanashiTheNameless/OrcaSlicer/releases/tag/Nightly-Rolling";
const char *const DOWNLOAD_BASE = "https://github.com/NanashiTheNameless/OrcaSlicer/releases/download/Nightly-Rolling/";

// The Ed25519 public key that signs latest.json, base64 encoded; scripts/nightly_signing_key.sh prints it, and its
// private half is the NIGHTLY_MANIFEST_KEY repository secret. A manifest it does not verify is only announced, never
// downloaded or installed. Changing the key means builds with the old one can no longer update by themselves.
const char *const NIGHTLY_MANIFEST_PUBLIC_KEY = "/jrTs3R0VTSPqxxmSErPJcaIg149rNMMOmMa8hu7Rk4=";

// Settings, with their defaults in AppConfig::set_defaults().
const char *const SETTING_CHECK_ON_LAUNCH = "nightly_check_on_launch";   // bool
const char *const SETTING_CHECK_PERIODIC  = "nightly_check_periodically"; // bool
const char *const SETTING_UPDATE_MODE     = "nightly_update_mode";        // see below
const char *const MODE_NOTIFY             = "notify";            // tell the user, link the release page
const char *const MODE_DOWNLOAD           = "download";          // download and verify, then ask to install
const char *const MODE_INSTALL_ON_LAUNCH  = "install_on_launch"; // as download, but install without asking at launch
// Shared with the upstream update dialog, which records the skipped build here.
const char *const SETTING_SKIP            = "skip_version";

const char *const SETTING_INTERVAL        = "nightly_check_interval_hours"; // int, in hours
// Must fit a wxTimer's int milliseconds: 168 hours is a week.
constexpr int MIN_INTERVAL_HOURS = 1, MAX_INTERVAL_HOURS = 168, DEFAULT_INTERVAL_HOURS = 6;

// A download notification id clear of the ones Downloader hands out, which count up from 0.
constexpr size_t DOWNLOAD_NOTIFICATION_ID = size_t(-2);

int check_interval_hours(const AppConfig &config)
{
    int hours = DEFAULT_INTERVAL_HOURS;
    try {
        hours = std::stoi(config.get(SETTING_INTERVAL));
    } catch (...) {}
    return std::clamp(hours, MIN_INTERVAL_HOURS, MAX_INTERVAL_HOURS);
}

bool is_flatpak() { return boost::filesystem::exists("/.flatpak-info"); }

NightlyTarget running_target()
{
    NightlyTarget target;
#if defined(_WIN32)
    target.platform = "windows";
    target.kinds    = { "installer", "portable" };
#elif defined(__APPLE__)
    target.platform = "macos";
    target.kinds    = { "app" };
#else
    target.platform = "linux";
    if (is_flatpak()) {
        target.kinds = { "flatpak" };
    } else {
        target.kinds = { "appimage" };
        // The AppImages are built on two Ubuntu bases, and the running one names its base.
        if (const char *appimage = std::getenv("APPIMAGE"); appimage != nullptr) {
            const std::string name = boost::filesystem::path(appimage).filename().string();
            if (name.find("2204") != std::string::npos)
                target.distro = "ubuntu2204";
            else if (name.find("2404") != std::string::npos)
                target.distro = "ubuntu2404";
        }
    }
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
    target.arch = "arm64";
#else
    target.arch = "x86_64";
#endif
    return target;
}

boost::filesystem::path executable_path() { return boost::filesystem::path(into_u8(wxStandardPaths::Get().GetExecutablePath())); }

#ifdef __APPLE__
// The .app bundle the running executable is in, or an empty path.
boost::filesystem::path app_bundle()
{
    for (boost::filesystem::path p = executable_path(); !p.empty(); p = p.parent_path())
        if (p.extension() == ".app")
            return p;
    return {};
}
#endif

bool is_writable_dir(const boost::filesystem::path &dir)
{
    boost::system::error_code ec;
    const boost::filesystem::path probe = dir / boost::filesystem::unique_path(".orca-update-%%%%%%%%");
    {
        boost::nowide::ofstream f(probe.string());
        if (!f)
            return false;
    }
    boost::filesystem::remove(probe, ec);
    return true;
}

// Whether this copy of OrcaSlicer can install the file itself. Otherwise the update is only announced.
bool can_install(const NightlyAsset &asset)
{
    if (is_running_in_msix())
        return false; // the Microsoft Store updates these
    if (asset.kind == "appimage") {
        const char *appimage = std::getenv("APPIMAGE");
        return appimage != nullptr && is_writable_dir(boost::filesystem::path(appimage).parent_path());
    }
    if (asset.kind == "flatpak")
        return is_flatpak();
    if (asset.kind == "installer")
        // The installer replaces an installed copy; a portable copy has no uninstaller next to it.
        return boost::filesystem::exists(executable_path().parent_path() / "Uninstall.exe");
#ifdef __APPLE__
    if (asset.kind == "app") {
        const boost::filesystem::path bundle = app_bundle();
        return !bundle.empty() && is_writable_dir(bundle.parent_path());
    }
#endif
    return false;
}

std::string update_dir() { return (boost::filesystem::path(data_dir()) / "nightly_update").string(); }

wxString describe(const NightlyManifest &manifest)
{
    // TRN: %1% is the commit the nightly build was made from, %2% the date of that commit.
    return format_wxstr(_L("Nightly %1% (%2%)"), manifest.commit.substr(0, 10), manifest.commit_date.substr(0, 10));
}

std::string size_mb(std::uint64_t size)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f", double(size) / (1024. * 1024.));
    return text;
}

} // namespace

NightlyUpdater::NightlyUpdater()
{
    m_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent &) {
        check(Trigger::Timer);
        reschedule();
    });
    reschedule();
}

void NightlyUpdater::reschedule()
{
    // A one shot timer, armed again after every check, so a changed interval applies from the next one on.
    m_timer.StartOnce(check_interval_hours(*wxGetApp().app_config) * 60 * 60 * 1000);
}

NightlyUpdater::~NightlyUpdater() { m_timer.Stop(); }

void NightlyUpdater::check(Trigger trigger)
{
    AppConfig *config = wxGetApp().app_config;
    const bool user   = trigger == Trigger::User;
    if (!user && (config->get_stealth_mode() ||
                  !config->get_bool(trigger == Trigger::Launch ? SETTING_CHECK_ON_LAUNCH : SETTING_CHECK_PERIODIC)))
        return;
    if (!build_is_nightly) {
        report(trigger, _L("This build was not made by the nightly build pipeline, so it cannot check for updates."));
        return;
    }
    if (m_busy.exchange(true)) {
        report(trigger, _L("OrcaSlicer is already checking for or downloading an update."));
        return;
    }

    const std::string url = config->version_check_url();
    Http::get(url)
        .size_limit(1024 * 1024)
        .timeout_connect(10)
        .timeout_max(30)
        .on_error([this, trigger](std::string, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << "Nightly update check failed: HTTP " << status << ", " << error;
            wxGetApp().CallAfter([this, trigger] {
                report(trigger, _L("Could not check for updates."));
                finish();
            });
        })
        .on_complete([this, trigger, url](std::string body, unsigned status) {
            if (status != 200) {
                wxGetApp().CallAfter([this, trigger] {
                    report(trigger, _L("Could not check for updates."));
                    finish();
                });
                return;
            }
            // The signature sits next to the manifest. Without one the manifest is only announced.
            Http::get(url + ".sig")
                .size_limit(4096)
                .timeout_connect(10)
                .timeout_max(30)
                .on_error([this, trigger, body](std::string, std::string, unsigned) {
                    wxGetApp().CallAfter([this, trigger, body] { on_manifest(trigger, body, std::string()); });
                })
                .on_complete([this, trigger, body](std::string signature, unsigned sig_status) {
                    if (sig_status != 200)
                        signature.clear();
                    wxGetApp().CallAfter([this, trigger, body, signature] { on_manifest(trigger, body, signature); });
                })
                .perform();
        })
        .perform();
}

void NightlyUpdater::on_manifest(Trigger trigger, const std::string &manifest_body, const std::string &signature)
{
    const bool signed_manifest = verify_nightly_signature(manifest_body, signature, NIGHTLY_MANIFEST_PUBLIC_KEY);
    const std::optional<NightlyManifest> manifest = parse_nightly_manifest(manifest_body);
    if (!manifest) {
        BOOST_LOG_TRIVIAL(error) << "Nightly update check: the manifest is unusable";
        report(trigger, _L("Could not check for updates."));
        finish();
        return;
    }
    const NightlyAsset *asset = nightly_update_for(*manifest, build_commit_hash, build_commit_time, running_target());
    BOOST_LOG_TRIVIAL(info) << "Nightly update check: build " << build_commit_hash << " (" << build_commit_time << "), manifest "
                            << manifest->commit << " (" << manifest->commit_time << "), signed " << signed_manifest << ", file "
                            << (asset ? asset->name : std::string("none"));
    if (asset == nullptr) {
        if (trigger == Trigger::User)
            wxGetApp().no_new_version();
        finish();
        return;
    }
    if (trigger != Trigger::User && nightly_commit_skipped(manifest->commit, wxGetApp().app_config->get(SETTING_SKIP))) {
        finish();
        return;
    }

    // Only a signed manifest is trusted to choose a file to download and run, and only a file this copy can install.
    // The signature guards nothing else: without one the update is still announced, with the official release
    // page to update by hand, and the app works as before.
    const std::string mode = wxGetApp().app_config->get(SETTING_UPDATE_MODE);
    if (mode == MODE_NOTIFY || !signed_manifest || !can_install(*asset)) {
        show_notice(*manifest, asset, trigger == Trigger::User, signed_manifest);
        finish();
        return;
    }
    download(trigger, *manifest, *asset);
}

void NightlyUpdater::download(Trigger trigger, const NightlyManifest &manifest, const NightlyAsset &asset)
{
    const std::string path = (boost::filesystem::path(update_dir()) / asset.name).string();
    // A download kept from an earlier check is used again when it is still this build.
    if (boost::filesystem::exists(path) && sha256_file_hex(path) == asset.sha256) {
        on_downloaded(trigger, { manifest, asset, path });
        return;
    }
    // Only one build is ever kept: anything else in the folder, a partial download included, is from another.
    boost::system::error_code ec;
    boost::filesystem::create_directories(update_dir(), ec);
    for (boost::filesystem::directory_iterator it(update_dir(), ec), end; !ec && it != end; it.increment(ec))
        boost::filesystem::remove(it->path(), ec);

    m_download = Download{ trigger, manifest, asset, path };
    m_cancel   = false;
    m_pause    = false;
    if (Plater *plater = wxGetApp().plater())
        plater->get_notification_manager()->push_download_URL_progress_notification(
            DOWNLOAD_NOTIFICATION_ID, asset.name,
            [this](DownloaderUserAction action, int) { return on_download_action(action); });
    BOOST_LOG_TRIVIAL(info) << "Nightly update: downloading " << asset.name << " (" << asset.size << " bytes)";
    start_transfer();
}

void NightlyUpdater::start_transfer()
{
    const Download    d    = *m_download;
    const std::string part = d.path + ".part";
    // Resuming after a pause asks only for the rest, and appends it.
    boost::system::error_code ec;
    const std::uintmax_t have = boost::filesystem::exists(part, ec) ? boost::filesystem::file_size(part, ec) : 0;

    m_transferring = true;
    m_stop_reason  = StopNone;
    Http http = Http::get(DOWNLOAD_BASE + Http::url_encode(d.asset.name));
    if (have > 0)
        http.set_range(std::to_string(have) + "-");
    // Streamed to disk, so the download needs no memory however large the file is.
    http.body_to_file(part, have > 0)
        .size_limit(size_t(d.asset.size) + 1024)
        .timeout_connect(10)
        .on_progress([this, have, total = d.asset.size](Http::Progress progress, bool &cancel) {
            if (m_cancel || m_pause) {
                m_stop_reason = m_cancel ? StopCancel : StopPause;
                cancel        = true;
                return;
            }
            const float done    = total == 0 ? 0.f : float(double(have + progress.dlnow) / double(total));
            const int   percent = int(done * 100.f);
            // Only whole percent steps reach the UI thread.
            if (percent != m_percent.exchange(percent))
                wxGetApp().CallAfter([done] {
                    if (Plater *plater = wxGetApp().plater())
                        plater->get_notification_manager()->set_download_URL_progress(DOWNLOAD_NOTIFICATION_ID, std::min(done, 0.99f));
                });
        })
        .on_error([this](std::string, std::string error, unsigned status) {
            // A pause or cancel stops the transfer from the progress callback, which ends here too.
            const int reason = m_stop_reason;
            if (reason == StopNone)
                BOOST_LOG_TRIVIAL(error) << "Nightly update download failed: HTTP " << status << ", " << error;
            wxGetApp().CallAfter([this, reason] {
                m_transferring = false;
                if (reason == StopPause) {
                    if (!m_pause)
                        start_transfer(); // resumed before this transfer had wound down
                    else if (Plater *plater = wxGetApp().plater())
                        plater->get_notification_manager()->set_download_URL_paused(DOWNLOAD_NOTIFICATION_ID);
                    return; // the partial file stays for resuming
                }
                end_download(reason == StopCancel ? wxString() : _L("Could not download the update."));
            });
        })
        .on_complete([this, d, part](std::string, unsigned) {
            // Checked on this worker thread; only the result goes to the UI thread.
            boost::system::error_code ec;
            bool ok = boost::filesystem::file_size(part, ec) == d.asset.size && !ec && sha256_file_hex(part) == d.asset.sha256;
            if (ok)
                boost::filesystem::rename(part, d.path, ec);
            ok = ok && !ec;
            if (!ok)
                boost::filesystem::remove(part, ec);
            wxGetApp().CallAfter([this, d, ok] {
                m_transferring = false;
                if (!ok) {
                    BOOST_LOG_TRIVIAL(error) << "Nightly update: the download of " << d.asset.name << " does not match the manifest";
                    end_download(_L("The downloaded update is damaged and was deleted."));
                    return;
                }
                m_download.reset();
                if (Plater *plater = wxGetApp().plater())
                    plater->get_notification_manager()->set_download_URL_progress(DOWNLOAD_NOTIFICATION_ID, 1.f);
                on_downloaded(d.trigger, { d.manifest, d.asset, d.path });
            });
        })
        .perform();
}

bool NightlyUpdater::on_download_action(DownloaderUserAction action)
{
    if (!m_download)
        return false;
    switch (action) {
    case DownloadUserCanceled:
        m_cancel = true;
        // A paused download has no transfer left to notice.
        if (!m_transferring)
            end_download(wxString());
        break;
    case DownloadUserPaused: m_pause = true; break;
    case DownloadUserContinued:
        if (m_pause) {
            m_pause   = false;
            m_percent = -1;
            // Otherwise the transfer that is still stopping restarts itself once it has.
            if (!m_transferring)
                start_transfer();
        }
        break;
    case DownloadUserOpenedFolder: wxLaunchDefaultApplication(wxString::FromUTF8(update_dir())); break;
    }
    return true;
}

void NightlyUpdater::end_download(const wxString &error)
{
    boost::system::error_code ec;
    boost::filesystem::remove(m_download->path + ".part", ec);
    const Trigger trigger = m_download->trigger;
    m_download.reset();
    if (Plater *plater = wxGetApp().plater()) {
        if (error.empty())
            plater->get_notification_manager()->set_download_URL_canceled(DOWNLOAD_NOTIFICATION_ID);
        else
            plater->get_notification_manager()->set_download_URL_error(DOWNLOAD_NOTIFICATION_ID, into_u8(error));
    }
    if (!error.empty())
        report(trigger, error);
    finish();
}

void NightlyUpdater::on_downloaded(Trigger trigger, Ready ready)
{
    BOOST_LOG_TRIVIAL(info) << "Nightly update: " << ready.asset.name << " is ready";
    // A later check finds the build already waiting, or a newer one that takes its place. Either way the user who
    // chose to install on close is not asked again; the newest download is what installs.
    m_ready = std::move(ready);
    finish();
    if (m_install_on_close && trigger != Trigger::User)
        return;
    const bool unsaved = wxGetApp().plater() != nullptr &&
                         (wxGetApp().plater()->is_project_dirty() || wxGetApp().plater()->is_presets_dirty());
    // Installing without asking is for launch only, and never with work open (such as a restored project).
    if (trigger == Trigger::Launch && wxGetApp().app_config->get(SETTING_UPDATE_MODE) == MODE_INSTALL_ON_LAUNCH && !unsaved) {
        if (start_installer(*m_ready, true))
            wxGetApp().mainframe->Close(true);
        return;
    }
    ask_to_install(*m_ready);
}

void NightlyUpdater::ask_to_install(const Ready &ready)
{
    const wxString message = format_wxstr(_L("%1% is downloaded and verified.\nInstalling it closes and restarts OrcaSlicer."),
                                          describe(ready.manifest)) +
                             (ready.manifest.notes.empty() ? wxString() : "\n\n" + wxString::FromUTF8(ready.manifest.notes));
    MessageDialog dialog(wxGetApp().mainframe, message, wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Update available"),
                         wxICON_INFORMATION);
    dialog.AddButton(wxID_YES, _L("Install now"), true);
    dialog.AddButton(wxID_APPLY, _L("Install when I close OrcaSlicer"));
    dialog.AddButton(wxID_NO, _L("Later"));
    dialog.AddButton(wxID_IGNORE, _L("Skip this build"));
    dialog.Fit();
    switch (dialog.ShowModal()) {
    case wxID_YES: install_now(); break;
    case wxID_APPLY: m_install_on_close = true; break;
    case wxID_IGNORE:
        wxGetApp().app_config->set(SETTING_SKIP, ready.manifest.commit);
        m_ready.reset();
        break;
    default: break; // offered again at the next check
    }
}

void NightlyUpdater::install_now()
{
    Plater *plater = wxGetApp().plater();
    if (plater != nullptr && (plater->is_project_dirty() || plater->is_presets_dirty())) {
        MessageDialog dialog(wxGetApp().mainframe,
                             _L("The current project has unsaved changes.\n"
                                "Save it before updating, or keep a backup that OrcaSlicer offers to restore when it restarts. "
                                "Unsaved preset changes are not kept in the backup."),
                             wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Update available"), wxICON_WARNING);
        dialog.AddButton(wxID_SAVE, _L("Save and update"), true);
        dialog.AddButton(wxID_APPLY, _L("Update and keep a backup"));
        dialog.AddButton(wxID_CANCEL, _L("Cancel"));
        dialog.Fit();
        const int answer = dialog.ShowModal();
        if (answer == wxID_SAVE) {
            if (plater->save_project() != wxID_YES)
                return;
        } else if (answer == wxID_APPLY) {
            // The same backup the timed backups write. Detached, it outlives this process, and the next start
            // finds it through the last backup directory and offers to restore it.
            Model &model = plater->model();
            plater->export_3mf(model.get_backup_path() + "/.3mf", SaveStrategy::Backup);
            wxGetApp().app_config->update_last_backup_dir(model.get_backup_path());
            wxGetApp().app_config->save();
            model.set_backup_path("detach");
        } else {
            return;
        }
    }
    if (start_installer(*m_ready, true))
        // Forced, so the close does not ask to save again.
        wxGetApp().mainframe->Close(true);
}

void NightlyUpdater::on_app_closing()
{
    m_timer.Stop();
    if (m_install_on_close && m_ready)
        start_installer(*m_ready, false);
}

bool NightlyUpdater::start_installer(const Ready &ready, bool relaunch)
{
    const std::string pid = std::to_string(get_current_pid());
    long              started = 0;
#if defined(_WIN32)
    // PowerShell waits for this process, runs the installer silently with the elevation it asks for, then
    // relaunches. Paths go in single quoted strings, where a quote is doubled.
    const auto quote = [](std::string s) {
        for (size_t i = s.find('\''); i != std::string::npos; i = s.find('\'', i + 2))
            s.insert(i, 1, '\'');
        return "'" + s + "'";
    };
    std::string script = "Wait-Process -Id " + pid + " -ErrorAction SilentlyContinue; " +
                         "Start-Process -FilePath " + quote(ready.path) + " -ArgumentList '/S' -Verb RunAs -Wait";
    if (relaunch)
        script += "; Start-Process -FilePath " + quote(executable_path().string());
    started = wxExecute(wxString::FromUTF8("powershell.exe -NoProfile -NonInteractive -WindowStyle Hidden -Command \"" + script + "\""),
                        wxEXEC_ASYNC | wxEXEC_HIDE_CONSOLE);
#else
    // A shell outlives this process: it waits for it to exit, installs, and relaunches. The paths are passed as
    // arguments, so nothing in them is ever read as shell syntax.
    std::vector<std::string> args;
    const char *appimage = std::getenv("APPIMAGE");
    if (ready.asset.kind == "appimage" && appimage != nullptr) {
        args = { "/bin/sh", "-c",
                 "while kill -0 \"$1\" 2>/dev/null; do sleep 1; done; "
                 "cp -f \"$2\" \"$3.new\" && chmod 755 \"$3.new\" && mv -f \"$3.new\" \"$3\" && "
                 "if [ \"$4\" = 1 ]; then exec \"$3\"; fi",
                 "orca-update", pid, ready.path, appimage, relaunch ? "1" : "0" };
    } else if (ready.asset.kind == "flatpak") {
        // The process id means nothing outside the sandbox; Flatpak installs over a running app, and the
        // relaunch waits until no instance is left.
        const std::string app_id = "dev.namelessnanashi.OrcaSlicer";
        std::string       scope  = "--system";
        {
            std::string info;
            load_string_file("/.flatpak-info", info);
            if (info.find("app-path=/var/lib/flatpak") == std::string::npos)
                scope = "--user";
        }
        args = { "flatpak-spawn", "--host", "/bin/sh", "-c",
                 "flatpak install \"$3\" -y --noninteractive --bundle \"$1\" && "
                 "if [ \"$4\" = 1 ]; then while flatpak ps --columns=application | grep -qx \"$2\"; do sleep 1; done; "
                 "exec flatpak run \"$2\"; fi",
                 "orca-update", ready.path, app_id, scope, relaunch ? "1" : "0" };
#ifdef __APPLE__
    } else if (ready.asset.kind == "app") {
        // The new bundle is unpacked next to the old one, then swapped in; the old one comes back if that fails.
        args = { "/bin/sh", "-c",
                 "while kill -0 \"$1\" 2>/dev/null; do sleep 1; done; "
                 "dir=$(mktemp -d \"$3.update.XXXXXX\") && tar -xzf \"$2\" -C \"$dir\" && [ -d \"$dir/OrcaSlicer.app\" ] && "
                 "rm -rf \"$3.old\" && mv \"$3\" \"$3.old\" && "
                 "if mv \"$dir/OrcaSlicer.app\" \"$3\"; then rm -rf \"$3.old\"; else mv \"$3.old\" \"$3\"; fi; "
                 "rm -rf \"$dir\"; if [ \"$4\" = 1 ]; then open \"$3\"; fi",
                 "orca-update", pid, ready.path, app_bundle().string(), relaunch ? "1" : "0" };
#endif
    }
    if (!args.empty()) {
        std::vector<const char *> argv;
        for (const std::string &a : args)
            argv.push_back(a.c_str());
        argv.push_back(nullptr);
        started = wxExecute(argv.data(), wxEXEC_ASYNC);
    }
#endif
    if (started <= 0) {
        BOOST_LOG_TRIVIAL(error) << "Nightly update: could not start the installer for " << ready.asset.name;
        MessageDialog(wxGetApp().mainframe, _L("Could not start the update. Install it from the nightly release page instead."),
                      wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Update available"), wxOK | wxICON_ERROR)
            .ShowModal();
        return false;
    }
    BOOST_LOG_TRIVIAL(info) << "Nightly update: installing " << ready.asset.name << (relaunch ? " and relaunching" : "");
    return true;
}

void NightlyUpdater::show_notice(const NightlyManifest &manifest, const NightlyAsset *asset, bool user_asked, bool signed_manifest)
{
    // TRN: %1% is a file name, %2% its size in megabytes.
    std::string description = format(_u8L("File: %1% (%2% MB)"), asset->name, size_mb(asset->size));
    if (!asset->sha256.empty())
        description += "\nSHA-256: " + asset->sha256;
    if (!manifest.notes.empty())
        description += "\n\n" + manifest.notes;

    VersionInfo info;
    info.version_str   = manifest.commit; // what a skip records
    info.version_name  = into_u8(describe(manifest));
    // An unsigned manifest does not get to choose where the user is sent.
    info.url           = signed_manifest && !manifest.release_url.empty() ? manifest.release_url : RELEASE_PAGE;
    info.description   = description;
    info.force_upgrade = false;
    wxGetApp().show_new_version(info, user_asked);
}

void NightlyUpdater::report(Trigger trigger, const wxString &message)
{
    if (trigger != Trigger::User)
        return;
    MessageDialog(wxGetApp().mainframe, message, wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Check for Updates"),
                  wxOK | wxICON_INFORMATION, "", _L("Check the nightly release page on GitHub to update manually."),
                  [](const wxString &) { wxLaunchDefaultBrowser(RELEASE_PAGE, wxBROWSER_NEW_WINDOW); })
        .ShowModal();
}

void NightlyUpdater::finish() { m_busy = false; }

}} // namespace Slic3r::GUI
