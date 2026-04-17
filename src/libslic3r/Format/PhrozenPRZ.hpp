#pragma once

#include <string>

namespace Slic3r {

class SLAPrint;
class DynamicPrintConfig;

// Generate a complete PRZ format binary string from a finished SLAPrint.
// m_layer_images must already be populated (call after slapsRasterize).
std::string generate_prz(const SLAPrint &print);

// Calculate estimated print time in seconds using the full physics model
// (exposure + lift/retract motion + rest times). Can be called before
// rasterization if total_layers is known (e.g. printer_input.size()).
int calculate_prz_print_time(int total_layers, const DynamicPrintConfig &cfg);

} // namespace Slic3r
