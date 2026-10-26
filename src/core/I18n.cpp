#include "I18n.h"

#include <atomic>
#include <cctype>
#include <string_view>
#include <unordered_map>

namespace navidrome::i18n {

namespace {

constexpr int kUnresolved = -1;

std::atomic<int>      g_language{ kUnresolved };
std::atomic<Resolver> g_resolver{ nullptr };

const std::unordered_map<std::string_view, const Translation*>& translationMap() {
    static const auto map = [] {
        std::unordered_map<std::string_view, const Translation*> m;
        for (const auto& row : translationTable()) m.emplace(row.english, &row);
        return m;
    }();
    return map;
}

const LanguageInfo* findLanguage(std::string_view code) {
    for (const auto& info : kLanguages)
        if (code == info.code) return &info;
    return nullptr;
}

}

Language parseLanguageCode(const std::string& code) {
    std::string primary;
    for (char c : code) {
        if (c == '-' || c == '_' || c == '.' || c == '@') break;
        primary.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    const LanguageInfo* info = findLanguage(primary);
    return info ? info->lang : Language::English;
}

const char* languageCode(Language lang) {
    return kLanguages[static_cast<std::size_t>(lang)].code;
}

Language resolveLanguage(const std::string& setting, const std::string& systemCode) {
    if (const LanguageInfo* info = findLanguage(setting)) return info->lang;
    return parseLanguageCode(systemCode);
}

void setResolver(Resolver resolver) { g_resolver.store(resolver); }

void setLanguage(Language lang) { g_language.store(static_cast<int>(lang)); }

Language language() {
    int lang = g_language.load();
    if (lang == kUnresolved) {
        const Resolver r = g_resolver.load();
        lang = static_cast<int>(r ? r() : Language::English);
        g_language.store(lang);
    }
    return static_cast<Language>(lang);
}

const char* translate(const char* english, Language lang) {
    if (!english || lang == Language::English) return english;
    const auto& map = translationMap();
    const auto it = map.find(english);
    if (it == map.end()) return english;
    const char* text = it->second->text[static_cast<std::size_t>(lang) - 1];
    return text && *text ? text : english;
}

const char* tr(const char* english) { return translate(english, language()); }

std::string tr(const std::string& english) { return tr(english.c_str()); }

std::string substitute(const std::string& pattern, std::initializer_list<std::string> args) {
    std::string out;
    out.reserve(pattern.size());
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '{' && i + 2 < pattern.size() && pattern[i + 2] == '}'
            && std::isdigit(static_cast<unsigned char>(pattern[i + 1]))) {
            const std::size_t n = static_cast<std::size_t>(pattern[i + 1] - '0');
            if (n < args.size()) {
                out += *(args.begin() + n);
                i += 2;
                continue;
            }
        }
        out.push_back(pattern[i]);
    }
    return out;
}

const std::vector<LanguageOption>& languageOptions() {
    static const std::vector<LanguageOption> k = [] {
        std::vector<LanguageOption> v{ { N_("Automatic (system language)"), "" } };
        for (const auto& info : kLanguages) v.push_back({ info.nativeName, info.code });
        return v;
    }();
    return k;
}

int languageOptionIndex(const std::string& value) {
    const auto& opts = languageOptions();
    for (std::size_t i = 0; i < opts.size(); ++i)
        if (value == opts[i].value) return static_cast<int>(i);
    return 0;
}

std::string trf(const char* english, std::initializer_list<std::string> args) {
    return substitute(tr(english), args);
}

}
