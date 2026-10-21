#pragma once

#include <string>

#include <wx/timer.h>

namespace Slic3r {

class AppConfig;

// Anonymous usage census reported to NamelessTelemetry (https://github.com/NanashiTheNameless/NamelessTelemetry).
// A ping carries only the project name and the SHA-256 of a random per-install UUID; the server counts each
// install once per UTC day and keeps nothing else.
namespace Telemetry {

// SHA-256 of the instance UUID as lowercase hex, the id format the census server counts.
std::string hash_instance_id(const std::string& uuid);

// True when DO_NOT_TRACK, TELEMETRY_OPTOUT or ORCA_TELEMETRY_OPTOUT is truthy, or TELEMETRY is falsy.
bool env_opted_out();

// Where an opt-out made outside OrcaSlicer's own settings comes from, so the GUI can say why telemetry is off.
enum class SystemOptOut { None, Registry, Environment };

// Registry: on Windows, a nonzero TelemetryOptOut DWORD under HKLM or HKCU\SOFTWARE\Policies\OrcaSlicer.
// Environment: env_opted_out().
SystemOptOut system_opt_out();
inline bool system_opted_out() { return system_opt_out() != SystemOptOut::None; }

// True when the user has telemetry on and nothing overrides it: stealth mode, which also stays on until the
// setup wizard is finished, or a system opt-out.
bool is_enabled(AppConfig& config);

// Sends one census ping in the background if enabled. Failures are ignored.
void send_census(AppConfig& config);

// Turn telemetry off in the stored config and save -- a normal choice the user can later flip in
// Preferences, unlike the sticky Policies opt-out. Shared by the two --turn-telemetry-off entry points.
void turn_off(AppConfig& config);

// Applies the --turn-telemetry-off command-line flag, which CLI::setup() relays via the
// ORCA_TURN_TELEMETRY_OFF env var, by calling turn_off(). Works on every platform and launch; the Windows
// installer's finish-page checkbox launches the app with the flag. No-op when the variable is unset, so the
// default behavior is unchanged. A second launch forwarded to an already-running single instance is handled
// separately, in OtherInstanceMessageHandler::handle_message().
void consume_installer_optout(AppConfig& config);

// Repeats send_census() while the app runs, so sessions spanning several UTC days are counted on each.
class CensusTimer : public wxTimer
{
public:
    explicit CensusTimer(AppConfig& config) : m_config(config) {}
    void Notify() override { send_census(m_config); }

private:
    AppConfig& m_config;
};

} // namespace Telemetry
} // namespace Slic3r
