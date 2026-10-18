#pragma once

#import <Foundation/Foundation.h>
#include "../../core/I18n.h"
#include <initializer_list>
#include <string>

static inline NSString *NDTr(const char *english) {
    return [NSString stringWithUTF8String:navidrome::tr(english)] ?: @"";
}

static inline NSString *NDTrf(const char *english, std::initializer_list<std::string> args) {
    return [NSString stringWithUTF8String:navidrome::trf(english, args).c_str()] ?: @"";
}

static inline std::string NDStd(NSString *s) {
    return s ? std::string(s.UTF8String ?: "") : std::string();
}
