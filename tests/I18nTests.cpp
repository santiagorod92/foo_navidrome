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
    check(parseLanguageCode("en-US") == Language::English, "en-US");
    check(parseLanguageCode("est") == Language::English, "est is not Spanish");
    check(parseLanguageCode("") == Language::English, "empty defaults to English");
    check(resolveLanguage("", "es-MX") == Language::Spanish, "automatic follows the system");
    check(resolveLanguage("en", "es-MX") == Language::English, "explicit English wins");
    check(resolveLanguage("es", "en-GB") == Language::Spanish, "explicit Spanish wins");
    check(std::string(languageCode(Language::Spanish)) == "es", "code es");
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
    check(std::string(tr("Refresh")) == "Refresh", "back to English");
    check(substitute("{1} of {0}, {2}", { "a", "b" }) == "b of a, {2}", "out-of-range placeholders stay");
}

TEST_CASE(testI18nSpanishTable) {
    std::set<std::string> seen;
    for (const auto& [en, es] : spanishTable()) {
        check(seen.insert(en).second, (std::string("duplicate key: ") + en).c_str());
        check(es && *es, (std::string("empty translation: ") + en).c_str());
        check(placeholders(en) == placeholders(es), (std::string("placeholders differ: ") + en).c_str());
    }
}
