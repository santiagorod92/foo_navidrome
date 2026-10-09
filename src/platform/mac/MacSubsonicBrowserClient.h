#pragma once

#include "../../core/NavidromeBrowserModel.h"
#include <memory>

namespace navidrome {

std::unique_ptr<IBrowserClient> makeMacBrowserClient();
}
