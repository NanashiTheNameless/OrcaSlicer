#include "NightlyManifest.hpp"

#include "nlohmann/json.hpp"

#include <openssl/evp.h>

#include <boost/nowide/fstream.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r {

std::optional<NightlyManifest> parse_nightly_manifest(const std::string &json)
{
    try {
        const nlohmann::json j = nlohmann::json::parse(json);
        if (!j.is_object())
            return std::nullopt;

        NightlyManifest m;
        m.schema = j.value("schema", 0);
        if (m.schema < 1 || m.schema > NIGHTLY_MANIFEST_SCHEMA)
            return std::nullopt;
        m.commit      = j.value("commit", std::string());
        m.commit_time = j.value("commit_time", 0LL);
        if (m.commit.empty() || m.commit_time <= 0)
            return std::nullopt;
        m.channel     = j.value("channel", std::string());
        m.version     = j.value("version", std::string());
        m.commit_date = j.value("commit_date", std::string());
        m.release_url = j.value("release_url", std::string());
        m.notes       = j.value("notes", std::string());

        if (const auto it = j.find("assets"); it != j.end() && it->is_array())
            for (const nlohmann::json &a : *it) {
                if (!a.is_object())
                    continue;
                NightlyAsset asset;
                asset.name     = a.value("name", std::string());
                asset.platform = a.value("platform", std::string());
                asset.arch     = a.value("arch", std::string());
                asset.kind     = a.value("kind", std::string());
                asset.distro   = a.value("distro", std::string());
                asset.sha256   = a.value("sha256", std::string());
                asset.size     = a.value("size", std::uint64_t(0));
                asset.current  = a.value("current", false);
                if (!asset.name.empty())
                    m.assets.push_back(std::move(asset));
            }
        return m;
    } catch (const std::exception &) {
        // Wrong types throw as well as broken JSON; either way the file is unusable.
        return std::nullopt;
    }
}

const NightlyAsset *find_nightly_asset(const NightlyManifest &manifest, const NightlyTarget &target)
{
    for (const std::string &kind : target.kinds) {
        const auto it = std::find_if(manifest.assets.begin(), manifest.assets.end(), [&](const NightlyAsset &a) {
            return a.current && a.kind == kind && a.platform == target.platform &&
                   (a.arch == target.arch || a.arch == "universal") &&
                   (target.distro.empty() || a.distro.empty() || a.distro == target.distro);
        });
        if (it != manifest.assets.end())
            return &*it;
    }
    return nullptr;
}

namespace {

bool is_commit_hash(const std::string &s)
{
    return s.size() >= 7 && s.size() <= 40 &&
           std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

// Whether two hashes, either of them possibly abbreviated, name the same commit.
bool same_commit(const std::string &a, const std::string &b)
{
    const size_t n = std::min(a.size(), b.size());
    return is_commit_hash(a) && is_commit_hash(b) && a.compare(0, n, b, 0, n) == 0;
}

} // namespace

const NightlyAsset *nightly_update_for(const NightlyManifest &manifest, const std::string &build_commit, long long build_commit_time,
                                       const NightlyTarget &target)
{
    // An all zero hash is what a build that could not find its commit records.
    if (!is_commit_hash(build_commit) || build_commit.find_first_not_of('0') == std::string::npos || build_commit_time <= 0)
        return nullptr;
    // The build's own commit has the same time, so it is never offered either.
    if (manifest.commit_time <= build_commit_time)
        return nullptr;
    return find_nightly_asset(manifest, target);
}

namespace {

// Decodes standard base64, ignoring surrounding white space. Returns nothing for anything that is not base64.
std::optional<std::vector<unsigned char>> decode_base64(const std::string &text)
{
    std::string s;
    for (char c : text)
        if (!std::isspace(static_cast<unsigned char>(c)))
            s.push_back(c);
    if (s.empty() || s.size() % 4 != 0)
        return std::nullopt;
    std::vector<unsigned char> out(s.size() / 4 * 3);
    const int n = EVP_DecodeBlock(out.data(), reinterpret_cast<const unsigned char *>(s.data()), int(s.size()));
    if (n < 0)
        return std::nullopt;
    // EVP_DecodeBlock counts the padding as data.
    const size_t padding = s.size() >= 2 && s[s.size() - 2] == '=' ? 2 : s.back() == '=' ? 1 : 0;
    out.resize(size_t(n) - padding);
    return out;
}

} // namespace

bool verify_nightly_signature(const std::string &data, const std::string &signature_base64, const std::string &public_key_base64)
{
    const auto signature = decode_base64(signature_base64);
    const auto key       = decode_base64(public_key_base64);
    if (!signature || !key || signature->size() != 64 || key->size() != 32)
        return false;

    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey(EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, key->data(), key->size()),
                                                             &EVP_PKEY_free);
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    if (!pkey || !ctx || EVP_DigestVerifyInit(ctx.get(), nullptr, nullptr, nullptr, pkey.get()) != 1)
        return false;
    // Ed25519 signs the message itself, in one call.
    return EVP_DigestVerify(ctx.get(), signature->data(), signature->size(), reinterpret_cast<const unsigned char *>(data.data()),
                            data.size()) == 1;
}

std::string sha256_file_hex(const std::string &path)
{
    boost::nowide::ifstream file(path, std::ios::binary);
    if (!file)
        return {};
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    if (!ctx || EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1)
        return {};
    std::vector<char> buffer(1 << 20);
    while (file) {
        file.read(buffer.data(), std::streamsize(buffer.size()));
        if (file.gcount() > 0 && EVP_DigestUpdate(ctx.get(), buffer.data(), size_t(file.gcount())) != 1)
            return {};
    }
    if (file.bad())
        return {};
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int  length = 0;
    if (EVP_DigestFinal_ex(ctx.get(), digest, &length) != 1)
        return {};
    std::string hex;
    char        byte[3];
    for (unsigned int i = 0; i < length; ++i) {
        std::snprintf(byte, sizeof(byte), "%02x", digest[i]);
        hex += byte;
    }
    return hex;
}

bool nightly_commit_skipped(const std::string &offered_commit, const std::string &skipped_commit)
{
    return same_commit(offered_commit, skipped_commit);
}

} // namespace Slic3r
