#pragma once
#define _WIN32_WINNT 0x0601  // Windows 7+
#define WIN32_LEAN_AND_MEAN
// pfc/timers.h calls timeGetTime() (winmm). Under WIN32_LEAN_AND_MEAN, windows.h
// pulls in neither mmsystem.h nor timeapi.h, so the declaration must be provided
// explicitly here — before the foobar2000 SDK headers that transitively include
// pfc. Requires linking winmm.lib (see the .vcxproj AdditionalDependencies).
#include <windows.h>
#include <timeapi.h>
#include <helpers/foobar2000+atl.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <thread>
#include <sstream>
#include <iomanip>
