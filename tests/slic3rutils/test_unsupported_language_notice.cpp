#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <string>

#include "libslic3r/AppConfig.hpp"
#include "slic3r/GUI/UnsupportedLanguageNotice.hpp"

using namespace Slic3r;
using namespace Slic3r::GUI;

TEST_CASE("English variants never need the unsupported language notice", "[UnsupportedLanguageNotice]")
{
    const std::string language = GENERATE(as<std::string>{}, "en", "en_US", "en_GB", "en-AU");
    CHECK(is_english(language));

    // A config that never acknowledged the notice still lets English through without asking.
    AppConfig config;
    CHECK(confirm_unsupported_language(nullptr, config, language));
}

TEST_CASE("Other languages are not English", "[UnsupportedLanguageNotice]")
{
    const std::string language = GENERATE(as<std::string>{}, "de_DE", "eu", "zh_CN", "pt_BR");
    CHECK_FALSE(is_english(language));
}

TEST_CASE("Once acknowledged, any language is let through", "[UnsupportedLanguageNotice]")
{
    AppConfig config;
    config.set_bool("unsupported_language_acknowledged", true);
    CHECK(confirm_unsupported_language(nullptr, config, "ja_JP"));
}

TEST_CASE("The notice is translated for every shipped catalog", "[UnsupportedLanguageNotice]")
{
    // The catalogs in localization/i18n, as canonical names the language picker can save.
    const std::string language = GENERATE(as<std::string>{}, "ca_ES", "cs_CZ", "de_DE", "es_ES", "eu_ES", "fr_FR", "hu_HU", "it_IT",
                                          "ja_JP", "ko_KR", "lt_LT", "nl_NL", "pl_PL", "pt_BR", "ru_RU", "sv_SE", "th_TH", "tr_TR",
                                          "uk_UA", "vi_VN", "zh_CN", "zh_TW", "ro_RO");
    CHECK(has_unsupported_language_notice_translation(language));
}
