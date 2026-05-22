#pragma once

// GDI+ include shim. gdiplus.h's headers reference unqualified min/max, which
// our NOMINMAX build breaks. Pulling std::min/std::max into the Gdiplus
// namespace before the include is the documented fix.

#include <algorithm>
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>
