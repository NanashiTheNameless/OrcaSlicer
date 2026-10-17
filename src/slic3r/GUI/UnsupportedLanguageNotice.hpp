#pragma once

#include <string>

class wxWindow;

namespace Slic3r {

class AppConfig;

namespace GUI {

// This fork supports English only. Before another language is used, the user acknowledges that once, in a
// notice shown in that language and in English. The notice's text lives in UnsupportedLanguageNotice.cpp
// instead of the gettext catalogs, so the fork's own strings never touch the upstream .po files.

// True for any English variant ("en", "en_US", "en_GB", ...).
bool is_english(const std::string& language);

// True when the notice has a translation for `language`, a canonical name such as "de_DE" or "zh_TW".
bool has_unsupported_language_notice_translation(const std::string& language);

// True when `language` may be used: it is English, the notice was acknowledged before, or the user
// acknowledges it now, which is remembered in `config`. The notice is shown in English, the system language,
// `previous_language` (the one in use, empty if none) and `language`, each once.
bool confirm_unsupported_language(wxWindow* parent, AppConfig& config, const std::string& language, const std::string& previous_language = {});

} // namespace GUI
} // namespace Slic3r
