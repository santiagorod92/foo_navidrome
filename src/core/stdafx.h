#ifdef __cplusplus
// pfc/string-interface.h uses std::string and std::string_view without
// including <string> / <string_view> explicitly. On older toolchains
// (e.g. Xcode 15.4 / MacOSX14.5.sdk used by GitHub Actions macos-14
// runners) these aren't pulled in transitively, so the PCH compile fails
// with "no type named 'string_view' in namespace 'std'". Include them
// here BEFORE the SDK to guarantee availability across toolchains.
#include <string>
#include <string_view>
#include <helpers/foobar2000+atl.h>
#endif

#ifdef __OBJC__
#include <Cocoa/Cocoa.h>
#endif
