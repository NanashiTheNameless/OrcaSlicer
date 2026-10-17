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

// True when TELEMETRY_OPTOUT or ORCA_TELEMETRY_OPTOUT is truthy, or TELEMETRY is falsy.
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
