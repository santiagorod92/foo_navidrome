#include "TestHarness.h"
#include "../src/core/I18n.h"

#include <set>
#include <string>

using namespace navidrome::i18n;

namespace {

std::multiset<std::string> placeholders(const std::string& s) {
    std::multiset<std::string> out;
    for (std::size_t i = 0; i + 2 < s.size(); ++i)
        if (s[i] == '{' && s[i + 2] == '}' && s[i + 1] >= '0' && s[i + 1] <= '9') out.insert(s.substr(i, 3));
    return out;
}

struct LanguageScope {
    explicit LanguageScope(Language l) { setLanguage(l); }
    ~LanguageScope() { setLanguage(Language::English); }
};

}

TEST_CASE(testI18nLanguageCodes) {
    check(parseLanguageCode("es") == Language::Spanish, "es");
    check(parseLanguageCode("es-AR") == Language::Spanish, "es-AR");
    check(parseLanguageCode("es_ES.UTF-8") == Language::Spanish, "POSIX locale");
    check(parseLanguageCode("ES") == Language::Spanish, "case-insensitive");
    check(parseLanguageCode("pt-BR") == Language::Portuguese, "pt-BR");
    check(parseLanguageCode("pt_PT") == Language::Portuguese, "pt_PT");
    check(parseLanguageCode("zh-Hans-CN") == Language::Chinese, "zh-Hans-CN");
    check(parseLanguageCode("ja-JP") == Language::Japanese, "ja-JP");
    check(parseLanguageCode("fr-CA") == Language::French, "fr-CA");
    check(parseLanguageCode("de_DE@euro") == Language::German, "de_DE@euro");
    check(parseLanguageCode("ru") == Language::Russian, "ru");
    check(parseLanguageCode("en-US") == Language::English, "en-US");
    check(parseLanguageCode("est") == Language::English, "est is not Spanish");
    check(parseLanguageCode("it-IT") == Language::English, "an unsupported language falls back to English");
    check(parseLanguageCode("") == Language::English, "empty defaults to English");
    check(resolveLanguage("", "es-MX") == Language::Spanish, "automatic follows the system");
    check(resolveLanguage("", "ja-JP") == Language::Japanese, "automatic follows a Japanese system");
    check(resolveLanguage("en", "es-MX") == Language::English, "explicit English wins");
    check(resolveLanguage("es", "en-GB") == Language::Spanish, "explicit Spanish wins");
    check(resolveLanguage("de", "fr-FR") == Language::German, "explicit German wins");
    check(resolveLanguage("xx", "pt-BR") == Language::Portuguese, "an unknown setting follows the system");
    for (const auto& info : kLanguages) {
        check(languageCode(info.lang) == std::string(info.code), (std::string("code ") + info.code).c_str());
        check(parseLanguageCode(info.code) == info.lang, (std::string("round trip ") + info.code).c_str());
    }
}

TEST_CASE(testI18nLanguageOptions) {
    const auto& opts = languageOptions();
    check(opts.size() == kLanguages.size() + 1, "Automatic plus one option per language");
    check(std::string(opts[0].value).empty(), "Automatic first");
    for (std::size_t i = 0; i < kLanguages.size(); ++i) {
        check(std::string(opts[i + 1].value) == kLanguages[i].code, "option value is the language code");
        check(languageOptionIndex(kLanguages[i].code) == static_cast<int>(i + 1), "option index by code");
    }
    check(languageOptionIndex("xx") == 0, "unknown value selects Automatic");
}

TEST_CASE(testI18nLookup) {
    check(std::string(tr("Refresh")) == "Refresh", "English is the identity");
    {
        LanguageScope es(Language::Spanish);
        check(std::string(tr("Refresh")) == "Actualizar", "Spanish lookup");
        check(std::string(tr("No such key, kept as is")) == "No such key, kept as is", "missing key falls back");
        check(trf("Saved {0} tracks to the Navidrome play queue", { "7" }).find("7 pistas") != std::string::npos,
              "trf substitutes into the translation");
    }
    {
        LanguageScope ja(Language::Japanese);
        check(std::string(tr("Refresh")) == "更新", "Japanese lookup");
    }
    check(std::string(translate("Refresh", Language::Portuguese)) == "Atualizar", "Portuguese column");
    check(std::string(translate("Refresh", Language::Chinese)) == "刷新", "Chinese column");
    check(std::string(translate("Refresh", Language::French)) == "Actualiser", "French column");
    check(std::string(translate("Refresh", Language::German)) == "Aktualisieren", "German column");
    check(std::string(translate("Refresh", Language::Russian)) == "Обновить", "Russian column");
    check(std::string(tr("Refresh")) == "Refresh", "back to English");
    check(substitute("{1} of {0}, {2}", { "a", "b" }) == "b of a, {2}", "out-of-range placeholders stay");
}

TEST_CASE(testI18nTranslationTable) {
    std::set<std::string> seen;
    for (const auto& row : translationTable()) {
        const std::string en = row.english;
        check(seen.insert(en).second, ("duplicate key: " + en).c_str());
        for (std::size_t i = 0; i < row.text.size(); ++i) {
            const std::string where = std::string(kLanguages[i + 1].code) + ": " + en;
            check(row.text[i] && *row.text[i], ("empty translation " + where).c_str());
            if (row.text[i]) check(placeholders(en) == placeholders(row.text[i]), ("placeholders differ " + where).c_str());
        }
    }
}
