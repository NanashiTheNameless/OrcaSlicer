# Nightly updates — High Level Design

## Purpose and scope

The fork publishes one rolling release, `Nightly-Rolling`, and every build
reports the same version, so builds cannot be ordered by version. Instead CI
publishes a manifest, `latest.json`, next to the release's files, and a nightly
build compares the commit it was built from with the commit the manifest names.
The app then announces the update, or downloads, verifies and installs it, as
the user's settings allow. Nothing is installed without the user's consent: the
default asks first, and the one setting that installs without asking does so
only at startup, before any work is open.

The upstream update check, which asks upstream's server about upstream
releases, is kept in `GUI_App::check_new_version_sf()` but never reached, so
upstream changes to it still merge.

## The manifest

`scripts/nightly_manifest.py` writes `latest.json` in the last job of a green
`Build all` run of `main`, after every build has uploaded its file. It reads the
release from the GitHub API, which already reports each file's size and SHA-256,
so nothing is downloaded.

| Field | Meaning |
|---|---|
| `schema` | Format version. It changes only when an older build would misread the file; builds reject a newer one. |
| `commit`, `commit_time`, `commit_date` | The commit the run built. `commit_time` (committer time, seconds since the epoch) is what builds compare. |
| `release_url`, `notes`, `version` | Shown to the user. |
| `assets[]` | `name`, `platform`, `arch`, `kind`, `distro`, `sha256`, `size`, `current`. |

A file is `current` when it was uploaded after the first attempt of the run
started. Runs of `main` take turns (same concurrency group, nothing cancelled),
so those files are the run's own; a file from a runner the run skipped is still
listed, but not current, and is never offered. A re-run that finishes after a
newer run leaves the newer manifest in place.

CI signs the exact bytes of `latest.json` with an Ed25519 key, the
`NIGHTLY_MANIFEST_KEY` secret, into `latest.json.sig` (base64), and uploads the
signature before the manifest. `scripts/nightly_signing_key.sh` creates the key
and prints the public half, which is built into the app as
`NIGHTLY_MANIFEST_PUBLIC_KEY` in `src/slic3r/GUI/NightlyUpdater.cpp`. The SHA-256
in the manifest only proves a download is intact; the signature is what proves
the manifest, and through it the file, came from CI.

## The build

`src/slic3r/GitCommitHash.cmake` writes the build's commit, its committer time,
and whether it is a nightly into `git_commit_hash.h`, read through
`BuildCommit.hpp`. CI marks a build as a nightly (`orca_nightly_build=1`) only
where it uploads it: pushes to `main` of the fork. A build with uncommitted
changes is never a nightly. Pull request and local builds therefore never check
for updates; asked to, they say so and link the release page.

## Deciding

`libslic3r/NightlyManifest` holds the decisions, so they are unit tested
without a GUI (`tests/libslic3r/test_nightly_manifest.cpp`):

- a build is offered an update when the manifest's commit is newer than its own,
  and the release has a current file for its platform, architecture, package
  kind and, for an AppImage, Ubuntu base;
- "Skip this build" records the manifest's commit, so the next nightly is
  offered again;
- signatures and file hashes are checked with OpenSSL.

## Checking, downloading and installing

`GUI::NightlyUpdater` runs the flow. It checks at startup and then every few
hours (Preferences: *Check for updates when OrcaSlicer starts*, *Check for
updates while OrcaSlicer is running*, *Time between update checks*, 1 to 168
hours, 6 by default; stealth mode turns both checks off), and whenever the user
picks *Check for Updates*. *When an update is found* decides what follows:

- **Download first, then ask** (default): download in the background, check size
  and SHA-256, then offer *Install now*, *Install when I close OrcaSlicer*,
  *Later* or *Skip this build*.
- **Only tell me**: the update dialog with a link to the release page.
- **Install at startup without asking**: as the default, but an update found at
  startup with no unsaved work is installed and OrcaSlicer restarts at once.

The signature guards downloading and installing only. Without a valid one the
app works as before and still announces the update with a link to the release
page, so the user can update by hand; it just never downloads or installs
anything itself. An update is likewise only announced when this copy cannot
replace itself: a
portable Windows copy, a Microsoft Store copy, an AppImage or app bundle in a
folder the user cannot write to. An unsigned manifest also does not choose the
link the user is sent to.

The download streams straight to disk (`Http::body_to_file()`), so its size
does not matter, and shows in the notification area with a progress bar and
buttons to pause, resume, cancel and open the download folder. A resumed
download asks only for the rest, with an HTTP range request; the SHA-256 check
at the end catches a server that sends the whole file again. Only one build is
kept in the download folder.

*Install now* with unsaved work offers *Save and update*, or *Update and keep a
backup*: the project is written to the same backup the timed backups use and
detached from the process, so the next start offers to restore it as after a
crash. Unsaved preset edits are not part of that backup.

Installing starts a helper that outlives OrcaSlicer, then closes it:

| Package | Helper |
|---|---|
| AppImage | waits for the process to exit, copies the new AppImage over the running one, relaunches it |
| Flatpak bundle | `flatpak-spawn --host flatpak install --bundle` into the installation the app runs from, then relaunches once no instance is left; needs `--talk-name=org.freedesktop.Flatpak` |
| Windows installer | PowerShell waits for the process, runs the NSIS installer with `/S` (elevated), relaunches |
| macOS | waits for the process, unpacks `OrcaSlicer_Mac_universal_nightly.app.tar.gz` next to the app, swaps the bundles (restoring the old one if that fails), relaunches |

*Install when I close OrcaSlicer* starts the same helper from the main window's
close handler, once the close can no longer be cancelled, without relaunching.
