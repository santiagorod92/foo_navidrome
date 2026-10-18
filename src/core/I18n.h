#pragma once

#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace navidrome::i18n {

enum class Language { English, Spanish };

using Resolver = Language (*)();

Language    parseLanguageCode(const std::string& code);
const char* languageCode(Language lang);
Language    resolveLanguage(const std::string& setting, const std::string& systemCode);

void     setResolver(Resolver resolver);
void     setLanguage(Language lang);
Language language();

const char* tr(const char* english);
std::string tr(const std::string& english);
std::string trf(const char* english, std::initializer_list<std::string> args);
std::string substitute(const std::string& pattern, std::initializer_list<std::string> args);

const std::vector<std::pair<const char*, const char*>>& spanishTable();

struct LanguageOption { const char* label; const char* value; };

const std::vector<LanguageOption>& languageOptions();
int languageOptionIndex(const std::string& value);

}

namespace navidrome {
constexpr const char* N_(const char* english) { return english; }
using i18n::tr;
using i18n::trf;
}
