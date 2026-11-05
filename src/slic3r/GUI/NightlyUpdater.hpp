#pragma once

#include "Downloader.hpp"
#include "libslic3r/NightlyManifest.hpp"

#include <wx/string.h>
#include <wx/timer.h>

#include <atomic>
#include <memory>
#include <optional>
#include <string>

namespace Slic3r { namespace GUI {

// Orca fork: keeps a nightly build up to date from the manifest on the nightly release. It checks at launch and
// every few hours while running, downloads and verifies the build for this platform, and installs it when the
// user agrees, at once or when OrcaSlicer closes. The user's settings decide each step; see
// docs/HLSD/nightly_updates.md.
class NightlyUpdater
{
public:
    enum class Trigger { Launch, Timer, User };

    NightlyUpdater();
    ~NightlyUpdater();
    NightlyUpdater(const NightlyUpdater &)            = delete;
    NightlyUpdater &operator=(const NightlyUpdater &) = delete;

    // Starts a check unless the user's settings rule it out. A check the user asked for always runs and
    // always reports its result; the others only speak up when there is an update.
    void check(Trigger trigger);

    // Called from the main window's close handler once the close can no longer be cancelled: installs an
    // update the user chose to install on close.
    void on_app_closing();

    // Arms the timer for the next timed check, from the interval in the settings. Preferences calls it when the
    // interval changes.
    void reschedule();

private:
    struct Ready
    {
        NightlyManifest manifest;
        NightlyAsset    asset;
        std::string     path; // the verified download
    };

    // A download in progress, or paused.
    struct Download
    {
        Trigger         trigger;
        NightlyManifest manifest;
        NightlyAsset    asset;
        std::string     path; // where the verified file goes; it is written to path + ".part" first
    };

    void on_manifest(Trigger trigger, const std::string &manifest_body, const std::string &signature);
    void download(Trigger trigger, const NightlyManifest &manifest, const NightlyAsset &asset);
    void start_transfer();
    // The buttons of the download notification: pause, resume, cancel, open the folder.
    bool on_download_action(DownloaderUserAction action);
    // Ends a download that will not finish: deletes the partial file and reports error, or a cancel when empty.
    void end_download(const wxString &error);
    void on_downloaded(Trigger trigger, Ready ready);
    void ask_to_install(const Ready &ready);
    void install_now();
    // Starts the helper that installs the download once OrcaSlicer has exited, and relaunches it if asked.
    bool start_installer(const Ready &ready, bool relaunch);
    void show_notice(const NightlyManifest &manifest, const NightlyAsset *asset, bool user_asked, bool signed_manifest);
    void report(Trigger trigger, const wxString &message);
    void finish();

    wxTimer               m_timer;
    std::atomic<bool>     m_busy{ false };
    std::optional<Download> m_download;
    std::atomic<bool>     m_cancel{ false };
    std::atomic<bool>     m_pause{ false };
    std::atomic<int>      m_percent{ -1 };
    // Why the transfer in flight was stopped, written by its progress callback.
    enum StopReason { StopNone, StopPause, StopCancel };
    std::atomic<int>      m_stop_reason{ StopNone };
    bool                  m_transferring = false; // a transfer is running or winding down; UI thread only
    std::optional<Ready>  m_ready;
    bool                  m_install_on_close = false;
};

}} // namespace Slic3r::GUI
