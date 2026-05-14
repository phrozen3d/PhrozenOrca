#include "SlaPrepareLayers.hpp"

#include <algorithm>

namespace Slic3r::GUI {

std::vector<double> build_sla_estimated_layer_top_zs(double z_bottom, double z_top, double initial_layer_height, double layer_height)
{
    std::vector<double> zs;
    if (z_top <= z_bottom)
        z_top = z_bottom + 50.0;
    if (initial_layer_height <= 0.0)
        initial_layer_height = 0.05;
    if (layer_height <= 0.0)
        layer_height = 0.05;

    const double top_z = z_top;
    double       z     = z_bottom + initial_layer_height;
    if (z >= top_z - 1e-9) {
        zs.push_back(top_z);
        return zs;
    }

    zs.push_back(z);
    const int kMaxLayers = 100000;
    int       guard      = 0;
    while (z < top_z - 1e-9 && guard < kMaxLayers) {
        const double next_z = z + layer_height;
        if (next_z >= top_z - 1e-9) {
            if (zs.back() < top_z - 1e-9)
                zs.push_back(top_z);
            break;
        }
        z = next_z;
        zs.push_back(z);
        ++guard;
    }
    if (!zs.empty() && zs.back() > top_z + 1e-6)
        zs.back() = top_z;
    return zs;
}

} // namespace Slic3r::GUI
