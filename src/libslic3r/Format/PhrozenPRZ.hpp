#pragma once

#include <string>

namespace Slic3r {

class SLAPrint;

// Generate a complete PRZ format binary string from a finished SLAPrint.
// m_layer_images must already be populated (call after slapsRasterize).
std::string generate_prz(const SLAPrint &print);

} // namespace Slic3r
