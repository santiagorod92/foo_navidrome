#include "stdafx.h"
#include "NavidromeSettings.h"
#include "NavidromeGuids.h"

namespace navidrome {

cfg_string cfg_server_url(guids::cfgServerUrl, "http://localhost:4533/");
cfg_string cfg_username(guids::cfgUsername, "");
cfg_string cfg_password(guids::cfgPassword, "");
cfg_string cfg_salt(guids::cfgSalt, "fb2k_navidrome");
cfg_string cfg_custom_headers(guids::cfgCustomHeaders, "");
cfg_var_modern::cfg_bool cfg_scrobble(guids::cfgScrobble, true);
cfg_string cfg_stream_format(guids::cfgStreamFormat, "");
cfg_var_modern::cfg_int cfg_max_bitrate(guids::cfgMaxBitrate, 0);
cfg_var_modern::cfg_bool cfg_library_filter(guids::cfgLibraryFilter, false);
cfg_string cfg_library_ids(guids::cfgLibraryIds, "");
cfg_string cfg_ui_language(guids::cfgUiLanguage, "");
cfg_string cfg_browser_hidden_categories(guids::cfgBrowserHiddenCategories, "");

}
