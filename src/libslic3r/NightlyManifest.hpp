#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r {

// The nightly release carries latest.json, written by CI after every build of a run has been uploaded.
// It names the commit the release was built from and the files of the release, and is what a nightly build
// compares itself against. See docs/HLSD/nightly_updates.md.

struct NightlyAsset
{
    std::string   name;     // file name on the release
    std::string   platform; // "linux", "windows" or "macos"
    std::string   arch;     // "x86_64", "arm64" or "universal"
    std::string   kind;     // "appimage", "flatpak", "installer", "portable" or "dmg"
    std::string   distro;   // the Ubuntu base of an AppImage, such as "ubuntu2404"; empty for other files
    std::string   sha256;
    std::uint64_t size = 0;
    // Built from the manifest's commit. A file the last run did not rebuild, such as one from a skipped
    // runner, is still listed, but it holds an older build.
    bool          current = false;
};

struct NightlyManifest
{
    int                       schema = 0;
    std::string               channel;
    std::string               version;
    std::string               commit;          // the full hash
    std::string               commit_date;     // for people; commit_time is what is compared
    long long                 commit_time = 0; // committer time, in seconds since the epoch
    std::string               release_url;
    std::string               notes;
    std::vector<NightlyAsset> assets;
};

// The newest manifest schema this build reads. Later additions to the format keep the schema number;
// it only changes when an older build would misread the file.
constexpr int NIGHTLY_MANIFEST_SCHEMA = 1;

// Returns nothing for malformed JSON, a missing commit or commit time, or a schema newer than this build reads.
std::optional<NightlyManifest> parse_nightly_manifest(const std::string &json);

// What the running build is, to pick its file from the release.
struct NightlyTarget
{
    std::string              platform;
    std::string              arch;
    std::vector<std::string> kinds;  // acceptable kinds, preferred first
    std::string              distro; // empty when unknown, which accepts any
};

// The current file of the release for target, or nullptr when the release has none built from its commit.
const NightlyAsset *find_nightly_asset(const NightlyManifest &manifest, const NightlyTarget &target);

// The file to offer a build of build_commit (a full or abbreviated hash) committed at build_commit_time: the
// target's file when the manifest names a commit committed later, and the release has that file.
// A build without a commit or a commit time is never offered one.
const NightlyAsset *nightly_update_for(const NightlyManifest &manifest, const std::string &build_commit, long long build_commit_time,
                                       const NightlyTarget &target);

// Whether signature_base64 (a raw Ed25519 signature, base64 encoded) signs data with public_key_base64 (the raw
// 32 byte Ed25519 public key, base64 encoded). CI signs latest.json this way into latest.json.sig.
bool verify_nightly_signature(const std::string &data, const std::string &signature_base64, const std::string &public_key_base64);

// The SHA-256 of a file as lowercase hex, or an empty string when it cannot be read.
std::string sha256_file_hex(const std::string &path);

// Whether the user skipped the nightly offered: the commit they skipped is the commit offered. A skip value that is
// not a commit hash, such as a version left over from an upstream build, skips nothing.
bool nightly_commit_skipped(const std::string &offered_commit, const std::string &skipped_commit);

} // namespace Slic3r
