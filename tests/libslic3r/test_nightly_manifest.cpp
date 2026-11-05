#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "libslic3r/NightlyManifest.hpp"
#include "test_utils.hpp"

#include <openssl/evp.h>

#include <boost/nowide/fstream.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace Slic3r;

namespace {

// A manifest for commit f73faf9e2a1c, committed at 1762185494, with an x86_64 AppImage for each Ubuntu base, a
// Windows installer and portable zip, and a macOS universal DMG. The arm64 Windows installer is listed but was
// not rebuilt from that commit.
const char *const manifest_json = R"({
  "schema": 1,
  "channel": "nightly",
  "version": "2.5.0-Nanashi",
  "commit": "f73faf9e2a1c4b6d8e0f1a2b3c4d5e6f7a8b9c0d",
  "commit_date": "2026-11-03T15:58:14Z",
  "commit_time": 1762185494,
  "release_url": "https://example.invalid/releases/tag/Nightly-Rolling",
  "assets": [
    { "name": "app-2204.AppImage", "platform": "linux", "arch": "x86_64", "kind": "appimage", "distro": "ubuntu2204",
      "sha256": "aa", "size": 10, "current": true },
    { "name": "app-2404.AppImage", "platform": "linux", "arch": "x86_64", "kind": "appimage", "distro": "ubuntu2404",
      "sha256": "bb", "size": 11, "current": true },
    { "name": "app.exe", "platform": "windows", "arch": "x86_64", "kind": "installer", "sha256": "cc", "size": 12, "current": true },
    { "name": "app.zip", "platform": "windows", "arch": "x86_64", "kind": "portable", "sha256": "dd", "size": 13, "current": true },
    { "name": "app_arm64.exe", "platform": "windows", "arch": "arm64", "kind": "installer", "sha256": "ee", "size": 14, "current": false },
    { "name": "app.dmg", "platform": "macos", "arch": "universal", "kind": "dmg", "sha256": "ff", "size": 15, "current": true }
  ]
})";

NightlyManifest manifest()
{
    std::optional<NightlyManifest> m = parse_nightly_manifest(manifest_json);
    REQUIRE(m.has_value());
    return *m;
}

std::string asset_name(const NightlyAsset *asset) { return asset == nullptr ? std::string("none") : asset->name; }

} // namespace

TEST_CASE("A nightly manifest is read with its commit and files", "[NightlyManifest]")
{
    const NightlyManifest m = manifest();
    CHECK(m.commit == "f73faf9e2a1c4b6d8e0f1a2b3c4d5e6f7a8b9c0d");
    CHECK(m.commit_time == 1762185494);
    CHECK(m.release_url == "https://example.invalid/releases/tag/Nightly-Rolling");
    REQUIRE(m.assets.size() == 6);
    CHECK(m.assets[1].distro == "ubuntu2404");
    CHECK(m.assets[1].sha256 == "bb");
    CHECK(m.assets[1].size == 11);
    CHECK(m.assets[1].current);
}

TEST_CASE("A nightly manifest that cannot be trusted is rejected", "[NightlyManifest]")
{
    const std::string json = GENERATE(
        std::string("not json"),
        std::string("[]"),
        std::string(R"({"schema": 1, "commit_time": 5})"),                      // no commit
        std::string(R"({"schema": 1, "commit": "abcdef1"})"),                   // no commit time
        std::string(R"({"schema": 0, "commit": "abcdef1", "commit_time": 5})"),  // no schema
        std::string(R"({"schema": 99, "commit": "abcdef1", "commit_time": 5})"), // a schema newer than this build reads
        std::string(R"({"schema": 1, "commit": "abcdef1", "commit_time": "5"})") // a commit time of the wrong type
    );
    CAPTURE(json);
    CHECK_FALSE(parse_nightly_manifest(json).has_value());
}

TEST_CASE("A build is offered its own file from a later commit only", "[NightlyManifest]")
{
    const NightlyManifest m = manifest();
    const NightlyTarget   linux_2204{ "linux", "x86_64", { "appimage" }, "ubuntu2204" };
    const long long       hour = 3600;

    CHECK(asset_name(nightly_update_for(m, "1234abc", m.commit_time - hour, linux_2204)) == "app-2204.AppImage");
    // The build is the manifest's commit, by its abbreviated or its full hash.
    CHECK(asset_name(nightly_update_for(m, "f73faf9", m.commit_time, linux_2204)) == "none");
    CHECK(asset_name(nightly_update_for(m, m.commit, m.commit_time, linux_2204)) == "none");
    // A build committed after the manifest's commit, such as one from a run that has not finished, is newer.
    CHECK(asset_name(nightly_update_for(m, "1234abc", m.commit_time + hour, linux_2204)) == "none");
    // A build that does not know its commit, or when it was made, has nothing to compare.
    CHECK(asset_name(nightly_update_for(m, "0000000", m.commit_time - hour, linux_2204)) == "none");
    CHECK(asset_name(nightly_update_for(m, "", m.commit_time - hour, linux_2204)) == "none");
    CHECK(asset_name(nightly_update_for(m, "1234abc", 0, linux_2204)) == "none");
}

TEST_CASE("The file offered matches the platform, architecture, package and Ubuntu base of the build", "[NightlyManifest]")
{
    const NightlyManifest m = manifest();

    CHECK(asset_name(find_nightly_asset(m, { "linux", "x86_64", { "appimage" }, "ubuntu2404" })) == "app-2404.AppImage");
    // An AppImage whose base is unknown takes the first one listed.
    CHECK(asset_name(find_nightly_asset(m, { "linux", "x86_64", { "appimage" }, "" })) == "app-2204.AppImage");
    // The run built no Flatpak and no arm64 AppImage.
    CHECK(asset_name(find_nightly_asset(m, { "linux", "x86_64", { "flatpak" }, "" })) == "none");
    CHECK(asset_name(find_nightly_asset(m, { "linux", "arm64", { "appimage" }, "" })) == "none");
    // Kinds are tried in order.
    CHECK(asset_name(find_nightly_asset(m, { "windows", "x86_64", { "installer", "portable" }, "" })) == "app.exe");
    CHECK(asset_name(find_nightly_asset(m, { "windows", "x86_64", { "portable", "installer" }, "" })) == "app.zip");
    // A universal file serves either Mac architecture.
    CHECK(asset_name(find_nightly_asset(m, { "macos", "arm64", { "dmg" }, "" })) == "app.dmg");
    CHECK(asset_name(find_nightly_asset(m, { "macos", "x86_64", { "dmg" }, "" })) == "app.dmg");
}

TEST_CASE("A file the run did not rebuild is not offered", "[NightlyManifest]")
{
    const NightlyManifest m = manifest();
    // The arm64 installer on the release is from an older commit, so an older arm64 build would be "updated" to a
    // file that may be older than itself.
    CHECK(asset_name(nightly_update_for(m, "1234abc", m.commit_time - 3600, { "windows", "arm64", { "installer", "portable" }, "" })) ==
          "none");
}

TEST_CASE("Skipping a nightly build skips that commit only", "[NightlyManifest]")
{
    const std::string commit = "f73faf9e2a1c4b6d8e0f1a2b3c4d5e6f7a8b9c0d";
    CHECK(nightly_commit_skipped(commit, commit));
    // A skip recorded from an abbreviated hash still matches.
    CHECK(nightly_commit_skipped(commit, "f73faf9"));
    // The next nightly is offered again.
    CHECK_FALSE(nightly_commit_skipped("1234abcdef", commit));
    // Nothing skipped, or a version string left over from an upstream build, skips nothing.
    CHECK_FALSE(nightly_commit_skipped(commit, ""));
    CHECK_FALSE(nightly_commit_skipped(commit, "2.3.0"));
    CHECK_FALSE(nightly_commit_skipped("", commit));
}

namespace {

std::string encode_base64(const unsigned char *data, size_t size)
{
    std::string out(4 * ((size + 2) / 3), '\0');
    out.resize(size_t(EVP_EncodeBlock(reinterpret_cast<unsigned char *>(out.data()), data, int(size))));
    return out;
}

// A fresh Ed25519 key, as CI would keep it, with its public half in the form built into the app.
struct SigningKey
{
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key{ nullptr, &EVP_PKEY_free };
    std::string                                         public_base64;

    SigningKey()
    {
        std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr), &EVP_PKEY_CTX_free);
        EVP_PKEY *raw = nullptr;
        REQUIRE(ctx);
        REQUIRE(EVP_PKEY_keygen_init(ctx.get()) == 1);
        REQUIRE(EVP_PKEY_keygen(ctx.get(), &raw) == 1);
        key.reset(raw);
        unsigned char pub[32];
        size_t        pub_size = sizeof(pub);
        REQUIRE(EVP_PKEY_get_raw_public_key(key.get(), pub, &pub_size) == 1);
        public_base64 = encode_base64(pub, pub_size);
    }

    std::string sign(const std::string &data) const
    {
        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
        REQUIRE(EVP_DigestSignInit(ctx.get(), nullptr, nullptr, nullptr, key.get()) == 1);
        unsigned char sig[64];
        size_t        sig_size = sizeof(sig);
        REQUIRE(EVP_DigestSign(ctx.get(), sig, &sig_size, reinterpret_cast<const unsigned char *>(data.data()), data.size()) == 1);
        return encode_base64(sig, sig_size);
    }
};

} // namespace

TEST_CASE("A nightly manifest signature verifies only for the signed bytes and key", "[NightlyManifest]")
{
    const SigningKey  key;
    const std::string data      = manifest_json;
    const std::string signature = key.sign(data);

    CHECK(verify_nightly_signature(data, signature, key.public_base64));
    // The signature file ends in a newline; white space around the base64 does not matter.
    CHECK(verify_nightly_signature(data, signature + "\n", " " + key.public_base64 + "\n"));

    // One changed byte, another key, or no signature at all fails.
    std::string tampered = data;
    tampered[tampered.find("1762185494")] = '2';
    CHECK_FALSE(verify_nightly_signature(tampered, signature, key.public_base64));
    CHECK_FALSE(verify_nightly_signature(data, signature, SigningKey().public_base64));
    CHECK_FALSE(verify_nightly_signature(data, "", key.public_base64));
    CHECK_FALSE(verify_nightly_signature(data, "not base64!", key.public_base64));
    // No public key built in, as before one is set up, verifies nothing.
    CHECK_FALSE(verify_nightly_signature(data, signature, ""));
}

TEST_CASE("A downloaded file is hashed with SHA-256", "[NightlyManifest]")
{
    ScopedTemporaryFile file;
    {
        boost::nowide::ofstream out(file.string(), std::ios::binary);
        out << "abc";
    }
    // The SHA-256 test vector for "abc".
    CHECK(sha256_file_hex(file.string()) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256_file_hex(file.string() + ".missing").empty());
}
