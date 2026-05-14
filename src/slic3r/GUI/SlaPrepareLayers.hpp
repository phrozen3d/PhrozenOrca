#ifndef slic3r_GUI_SlaPrepareLayers_hpp_
#define slic3r_GUI_SlaPrepareLayers_hpp_

#include <vector>

namespace Slic3r::GUI {

// World-space cumulative layer top Z (mm) from z_bottom to z_top (inclusive top).
std::vector<double> build_sla_estimated_layer_top_zs(double z_bottom, double z_top, double initial_layer_height, double layer_height);

} // namespace Slic3r::GUI

#endif
