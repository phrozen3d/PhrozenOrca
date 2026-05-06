#pragma once

#include <functional>
#include <iosfwd>
#include "libslic3r/GCode/ThumbnailData.hpp"

namespace Slic3r {

class SLAPrint;
class DynamicPrintConfig;

// Stream a complete PRZ file directly to out — no intermediate buffer.
// raster_params() must have a value (call after slapsRasterize completes).
// progress(pct) is called after each batch with pct in [0,100]; return false to abort.
void generate_prz(std::ostream &out, const SLAPrint &print, const ThumbnailData *thumb = nullptr,
                  std::function<bool(int)> progress = nullptr);

// Calculate estimated print time in seconds using the full physics model
// (exposure + lift/retract motion + rest times). Can be called before
// rasterization if total_layers is known (e.g. printer_input.size()).
int calculate_prz_print_time(int total_layers, const DynamicPrintConfig &cfg);

// Same base model as above, plus optional per-layer time compensation from print settings
// when "print_time_compensation" is enabled: result = base + layer_print_time_compensation * N.
int adjusted_prz_print_time_seconds(int total_layers, const DynamicPrintConfig &cfg);

} // namespace Slic3r