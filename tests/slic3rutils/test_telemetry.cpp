#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <string>

#include "libslic3r/AppConfig.hpp"
#include "slic3r/Utils/Telemetry.hpp"

using namespace Slic3r;

namespace {

void finish_wizard(AppConfig& config)
{
    config.set("firstguide", "finish", true);
    config.set_bool("stealth_mode", false);
    config.set_bool("telemetry", true);
}

} // namespace

TEST_CASE("The instance id is the lowercase SHA-256 hex of the UUID", "[Telemetry]")
{
    // FIPS 180-2 test vector for "abc".
    CHECK(Telemetry::hash_instance_id("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("A new config has telemetry on, stealth mode off and its own install UUID", "[Telemetry]")
{
    AppConfig config;
    CHECK(config.get_bool("telemetry"));
    CHECK_FALSE(config.get_bool("stealth_mode"));
    CHECK_FALSE(config.get("telemetry_uuid").empty());
    CHECK(config.get("telemetry_uuid") != config.get("slicer_uuid"));
}

TEST_CASE("Telemetry stays off until the setup wizard is finished", "[Telemetry]")
{
    AppConfig config;
    finish_wizard(config);
    config.set("firstguide", "finish", false);
    CHECK_FALSE(Telemetry::is_enabled(config));
}

TEST_CASE("Stealth mode forces telemetry off", "[Telemetry]")
{
    AppConfig config;
    finish_wizard(config);
    config.set_bool("stealth_mode", true);
    CHECK_FALSE(Telemetry::is_enabled(config));
}

TEST_CASE("Telemetry follows the user's choice once the wizard is finished", "[Telemetry]")
{
    AppConfig config;
    finish_wizard(config);
    // An environment or registry opt-out overrides the choice, so compare against it rather than assume a clean machine.
    CHECK(Telemetry::is_enabled(config) == !Telemetry::system_opted_out());

    config.set_bool("telemetry", false);
    CHECK_FALSE(Telemetry::is_enabled(config));
}

TEST_CASE("DO_NOT_TRACK opts out of telemetry", "[Telemetry]")
{
#ifdef _WIN32
    _putenv_s("DO_NOT_TRACK", "1");
    CHECK(Telemetry::env_opted_out());
    _putenv_s("DO_NOT_TRACK", "");
#else
    setenv("DO_NOT_TRACK", "1", 1);
    CHECK(Telemetry::env_opted_out());
    unsetenv("DO_NOT_TRACK");
#endif
}
