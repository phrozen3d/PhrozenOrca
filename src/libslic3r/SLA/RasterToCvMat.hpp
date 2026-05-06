#pragma once

#include <vector>
#include <opencv2/core.hpp>
#include "libslic3r/SLA/RasterBase.hpp"
#include "libslic3r/ExPolygon.hpp"

namespace Slic3r {
namespace sla {

// Convert a single layer's ExPolygons to a CV_8UC1 grayscale cv::Mat.
//
// Parameters:
//   polys  - merged ExPolygons for one layer (model + support combined)
//   res    - printer resolution in pixels
//   pxdim  - physical pixel size in mm (display_width_mm / display_pixels_x, etc.)
//   trafo  - orientation/mirror transform (same Trafo used by the archive writer)
//   gamma  - AA gamma; 0.0 disables AA (threshold at 0.5); 1.0 = linear
//
// Returns a CV_8UC1 Mat (height_px rows x width_px cols),
// black background (0), white foreground (255), top-left origin.
cv::Mat expolygons_to_cvmat(
    const ExPolygons        &polys,
    const Resolution        &res,
    const PixelDim          &pxdim,
    const RasterBase::Trafo &trafo      = {},
    double                   gamma      = 1.0,
    int                      aa_steps   = 0,
    uint8_t                  gray_lo    = 0,
    uint8_t                  gray_hi    = 255,
    int                      blur_pixel = 0);

// Convert all layers to cv::Mat images in parallel.
// layer_polys[i] is the merged ExPolygons for layer i.
// Returns one CV_8UC1 cv::Mat per layer, in the same index order.
std::vector<cv::Mat> expolygons_layers_to_cvmat(
    const std::vector<ExPolygons> &layer_polys,
    const Resolution              &res,
    const PixelDim                &pxdim,
    const RasterBase::Trafo       &trafo      = {},
    double                         gamma      = 1.0,
    int                            aa_steps   = 0,
    uint8_t                        gray_lo    = 0,
    uint8_t                        gray_hi    = 255,
    int                            blur_pixel = 0);

// Apply picture_grayscale proportional scaling to a CV_8UC1 Mat in-place.
// Each pixel p is mapped to round(p * level / 255). level==255 is a no-op.
void apply_picture_grayscale_lut(cv::Mat &mat, uint8_t level);

} // namespace sla
} // namespace Slic3r
