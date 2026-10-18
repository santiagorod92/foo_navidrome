#pragma once

#include <SDK/cfg_var.h>

namespace navidrome {

extern cfg_string cfg_server_url;
extern cfg_string cfg_username;
extern cfg_string cfg_password;
extern cfg_string cfg_salt;
extern cfg_string cfg_custom_headers;
extern cfg_var_modern::cfg_bool cfg_scrobble;
extern cfg_string cfg_stream_format;
extern cfg_var_modern::cfg_int cfg_max_bitrate;
extern cfg_var_modern::cfg_bool cfg_library_filter;
extern cfg_string cfg_library_ids;
extern cfg_string cfg_browser_hidden_categories;
extern cfg_string cfg_ui_language;

}
