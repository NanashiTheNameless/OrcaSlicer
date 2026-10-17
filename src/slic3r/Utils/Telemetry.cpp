#include "Telemetry.hpp"

#include "Http.hpp"
#include "libslic3r/AppConfig.hpp"

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/log/trivial.hpp>
#include <nlohmann/json.hpp>
#include <openssl/sha.h>

#include <cstdlib>
#include <initializer_list>
#include <set>
#include <string>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace Slic3r {
namespace Telemetry {

namespace {

constexpr const char* ENDPOINT     = "https://telemetry.namelessnanashi.dev/census";
constexpr const char* PROJECT_NAME = "OrcaSlicer-Nanashi";

std::string env_value(const char* name)
{
    const char* value = std::getenv(name);
    std::string s     = value ? value : "";
    boost::algorithm::trim(s);
    boost::algorithm::to_lower(s);
    return s;
}

bool env_truthy(const char* name)
{
    static const std::set<std::string> truthy{"1", "true", "yes", "on", "y", "t"};
    return truthy.count(env_value(name)) > 0;
}

bool env_falsy(const char* name)
{
    static const std::set<std::string> falsy{"0", "false", "no", "off", "n", "f"};
    return falsy.count(env_value(name)) > 0;
}

#ifdef _WIN32
// A nonzero TelemetryOptOut DWORD under SOFTWARE\Policies\OrcaSlicer, in HKLM for the whole machine
// (Group Policy, admins) or HKCU for one user.
bool registry_opted_out()
{
    for (HKEY hive : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
        DWORD value = 0;
        DWORD size  = sizeof(value);
        if (::RegGetValueW(hive, L"SOFTWARE\\Policies\\OrcaSlicer", L"TelemetryOptOut", RRF_RT_REG_DWORD, nullptr, &value, &size) ==
                ERROR_SUCCESS &&
            value != 0)
            return true;
    }
    return false;
}
#endif

} // namespace

std::string hash_instance_id(const std::string& uuid)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(uuid.data()), uuid.size(), hash);

    static const char digits[] = "0123456789abcdef";
    std::string       hex;
    hex.reserve(2 * SHA256_DIGEST_LENGTH);
    for (unsigned char byte : hash) {
        hex.push_back(digits[byte >> 4]);
        hex.push_back(digits[byte & 0x0f]);
    }
    return hex;
}

bool env_opted_out()
{
    return env_truthy("TELEMETRY_OPTOUT") || env_truthy("ORCA_TELEMETRY_OPTOUT") || env_falsy("TELEMETRY");
}

SystemOptOut system_opt_out()
{
#ifdef _WIN32
    if (registry_opted_out())
        return SystemOptOut::Registry;
#endif
    return env_opted_out() ? SystemOptOut::Environment : SystemOptOut::None;
}

bool is_enabled(AppConfig& config)
{
    return config.get_bool("telemetry") && !config.get_stealth_mode() && !system_opted_out();
}

void send_census(AppConfig& config)
{
    if (!is_enabled(config))
        return;

    const std::string uuid = config.get("telemetry_uuid");
    if (uuid.empty())
        return;

    const std::string body = nlohmann::json{{"id", hash_instance_id(uuid)}, {"project", PROJECT_NAME}}.dump();

    auto http = Http::post(ENDPOINT);
    // Drop the process-wide headers meant for vendor clouds so the ping carries nothing but the body.
    http.headers_clear()
        .header("Content-Type", "application/json")
        .timeout_connect(2)
        .timeout_max(5)
        .set_post_body(body)
        .on_error([](std::string, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(debug) << "Telemetry: census ping failed, status " << status << ": " << error;
        })
        .perform();
}

} // namespace Telemetry
} // namespace Slic3r
