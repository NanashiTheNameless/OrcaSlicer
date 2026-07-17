#ifndef slic3r_PluginAuditManager_hpp_
#define slic3r_PluginAuditManager_hpp_

#include <Python.h>
#include <mutex>
#include <string>
#include <vector>

#include <boost/filesystem.hpp>

namespace Slic3r {

struct AuditDecision {
    bool        allowed = true;
    std::string reason;
};

struct AuditViolation {
    std::string            plugin_key;
    std::string            event_name;
    boost::filesystem::path  path;
    std::string            reason;
};

// Returns true if candidate resolves to a path inside allowed_root.
// Uses weakly_canonical and component-wise comparison to reject traversal attacks.
bool is_inside_allowed_root(const boost::filesystem::path& candidate,
                            const boost::filesystem::path& allowed_root);

class PluginAuditManager
{
public:
    static PluginAuditManager& instance();

    // Call once after Py_Initialize to install the global audit hook.
    void install_hook();

    // --- current-plugin context (thread_local) ---
    void        set_current_plugin(const std::string& plugin_key);
    std::string current_plugin() const;
    void        clear_current_plugin();

    // --- current-capability context (thread_local) ---
    // The capability whose method is currently executing, within the current plugin. Empty
    // while a plugin-wide call runs, and during capture (get_name/get_type), where the
    // capability has no cached name yet.
    void        set_current_capability(const std::string& capability_name);
    std::string current_capability() const;
    void        clear_current_capability();

    // --- allowed-roots registry ---
    void add_global_allowed_root(const boost::filesystem::path& root);
    void add_scoped_allowed_root(const boost::filesystem::path& root);

    // --- enforcement mode ---
    enum class AuditMode {
        // Import/loading phase: allow reads anywhere, only block writes
        // outside allowed roots.  Python needs to read stdlib modules
        // during import and those are not inside plugin directories.
        Loading,

        // Execution phase: block both reads and writes outside allowed
        // roots, plus subprocess/socket/ctypes.
        Enforcing,
    };

    void set_audit_mode(AuditMode mode);
    AuditMode audit_mode() const;

    // --- policy checks ---
    AuditDecision check_open(const std::string& path, const std::string& mode);

    void report_violation(const AuditViolation& violation);
    void clear_last_violation();
    bool last_violation(AuditViolation& violation) const;

    bool verbose_events = true;

private:
    friend class ScopedPluginAuditContext;

    PluginAuditManager() = default;

    static int audit_hook(const char* event, PyObject* args, void* user_data);

    static thread_local std::string  m_current_plugin_key;
    static thread_local std::string  m_current_capability_name;
    static thread_local AuditMode    m_audit_mode;
    static thread_local std::vector<boost::filesystem::path> m_scoped_allowed_roots;
    static thread_local bool         m_has_last_violation;
    static thread_local AuditViolation m_last_violation;

    std::mutex m_mutex;
    std::vector<boost::filesystem::path> m_global_allowed_roots;
};

// RAII guard that sets the current plugin key and capability name, restoring the previous
// pair on scope exit. `capability_name` may be empty for calls that are not scoped to a
// single capability.
class ScopedPluginAuditContext
{
public:
    explicit ScopedPluginAuditContext(
        const std::string&                   plugin_key,
        const std::string&                   capability_name = {},
        PluginAuditManager::AuditMode        mode = PluginAuditManager::AuditMode::Loading);

    ~ScopedPluginAuditContext();

    ScopedPluginAuditContext(const ScopedPluginAuditContext&)            = delete;
    ScopedPluginAuditContext& operator=(const ScopedPluginAuditContext&) = delete;

private:
    std::string                   m_previous_id;
    std::string                   m_previous_capability;
    PluginAuditManager::AuditMode m_previous_mode;
    std::vector<boost::filesystem::path> m_previous_scoped_roots;
};

} // namespace Slic3r

#endif // slic3r_PluginAuditManager_hpp_
