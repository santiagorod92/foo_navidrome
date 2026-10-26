#pragma once

#include <array>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <vector>

namespace navidrome::i18n {

enum class Language { English, Spanish, Portuguese, Chinese, Japanese, French, German, Russian };

struct LanguageInfo { Language lang; const char* code; const char* nativeName; };

inline constexpr std::array<LanguageInfo, 8> kLanguages = { {
    { Language::English,    "en", "English" },
    { Language::Spanish,    "es", "Español" },
    { Language::Portuguese, "pt", "Português" },
    { Language::Chinese,    "zh", "中文（简体）" },
    { Language::Japanese,   "ja", "日本語" },
    { Language::French,     "fr", "Français" },
    { Language::German,     "de", "Deutsch" },
    { Language::Russian,    "ru", "Русский" },
} };

inline constexpr std::size_t kTranslatedLanguages = kLanguages.size() - 1;

struct Translation {
    const char* english;
    std::array<const char*, kTranslatedLanguages> text;
};

using Resolver = Language (*)();

Language    parseLanguageCode(const std::string& code);
const char* languageCode(Language lang);
Language    resolveLanguage(const std::string& setting, const std::string& systemCode);

void     setResolver(Resolver resolver);
void     setLanguage(Language lang);
Language language();

const char* translate(const char* english, Language lang);
const char* tr(const char* english);
std::string tr(const std::string& english);
std::string trf(const char* english, std::initializer_list<std::string> args);
std::string substitute(const std::string& pattern, std::initializer_list<std::string> args);

const std::vector<Translation>& translationTable();

struct LanguageOption { const char* label; const char* value; };

const std::vector<LanguageOption>& languageOptions();
int languageOptionIndex(const std::string& value);

}

namespace navidrome {
constexpr const char* N_(const char* english) { return english; }
using i18n::tr;
using i18n::trf;
}
