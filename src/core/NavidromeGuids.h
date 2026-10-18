#pragma once

#include <cstdint>

namespace navidrome::guids {

constexpr GUID make(uint8_t group, uint8_t item) {
    return { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, group, item } };
}

inline constexpr GUID cfgServerUrl              = make(0x01, 0x01);
inline constexpr GUID cfgUsername               = make(0x01, 0x02);
inline constexpr GUID cfgPassword               = make(0x01, 0x03);
inline constexpr GUID cfgSalt                   = make(0x01, 0x04);
inline constexpr GUID prefsPage                 = make(0x01, 0x05);
inline constexpr GUID mainmenuOpenBrowser       = make(0x01, 0x07);
inline constexpr GUID macLibraryViewer          = make(0x01, 0x08);
inline constexpr GUID libraryPrefsPage          = make(0x01, 0x09);
inline constexpr GUID cfgCustomHeaders          = make(0x01, 0x0a);
inline constexpr GUID cfgScrobble               = make(0x01, 0x0b);
inline constexpr GUID cfgStreamFormat           = make(0x01, 0x0c);
inline constexpr GUID cfgMaxBitrate             = make(0x01, 0x0d);
inline constexpr GUID mainmenuBookmark          = make(0x01, 0x0e);
inline constexpr GUID macUiElementBrowser       = make(0x01, 0x0e);
inline constexpr GUID radioPrefsPage            = make(0x01, 0x0f);
inline constexpr GUID cfgLibraryFilter          = make(0x01, 0x10);
inline constexpr GUID cfgLibraryIds             = make(0x01, 0x11);
inline constexpr GUID librariesPrefsPage        = make(0x01, 0x12);
inline constexpr GUID macUiElementLyrics        = make(0x01, 0x13);
inline constexpr GUID winUiElementBrowser       = make(0x01, 0x14);
inline constexpr GUID winColumnsUiPanel         = make(0x01, 0x15);
inline constexpr GUID cfgBrowserHiddenCategories = make(0x01, 0x16);
inline constexpr GUID sectionsPrefsPage         = make(0x01, 0x17);
inline constexpr GUID advRefreshRatingsOnStart  = make(0x01, 0x18);
inline constexpr GUID winUiElementLyrics        = make(0x01, 0x19);
inline constexpr GUID winColumnsUiLyrics        = make(0x01, 0x1a);
inline constexpr GUID inputHandler              = make(0x02, 0x01);
inline constexpr GUID contextMenuGroup          = make(0x03, 0x00);
inline constexpr GUID contextMenuItemsBase      = make(0x03, 0x01);
inline constexpr GUID contextMenuMixItemsBase   = make(0x03, 0x10);
inline constexpr GUID cfgAudioMuseUrl           = make(0x04, 0x01);
inline constexpr GUID cfgAudioMuseToken         = make(0x04, 0x02);
inline constexpr GUID cfgAudioMuseServer        = make(0x04, 0x03);
inline constexpr GUID cfgAudioMuseCount         = make(0x04, 0x04);
inline constexpr GUID audioMusePrefsPage        = make(0x04, 0x05);
inline constexpr GUID mainmenuAudioMuseGroup    = make(0x04, 0x10);
inline constexpr GUID mainmenuAudioMuseSearch   = make(0x04, 0x11);
inline constexpr GUID mainmenuAudioMusePlaylist = make(0x04, 0x12);
inline constexpr GUID advVerboseLog             = make(0x05, 0x01);
inline constexpr GUID mainmenuPlayQueueSave     = make(0x06, 0x01);
inline constexpr GUID mainmenuPlayQueueRestore  = make(0x06, 0x02);
inline constexpr GUID cfgUiLanguage             = make(0x08, 0x01);

}
