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

// In-place variant: rasterizes directly into dst, reusing its existing memory
// if the Mat is already the correct size (rows==height_px, cols==width_px,
// CV_8UC1, continuous). Only allocates on the first call per thread when used
// with tbb::enumerable_thread_specific<cv::Mat>.
void expolygons_to_cvmat(
    cv::Mat                 &dst,
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

// Dual-track layer rasterization (change: prz-support-binary-output).
// Renders the model and support geometry of one layer with DIFFERENT treatment
// and composites them, so supports come out pure-binary and solid:
//   * model_polys  → AA/gray-scale/blur + picture_grayscale LUT, UNLESS
//                    is_binary (bottom/plate-contact layers) which forces the
//                    model to pure binary too (no AA/gray/blur) for adhesion.
//   * support_polys → ALWAYS pure binary (gamma=0, no AA/gray/blur) and NEVER
//                    run through the picture_grayscale LUT (exempt from global
//                    dimming, always 255).
//   * composite     → dst = max(model_after_LUT, support_255), so the support's
//                    255 always wins and sub-pixel support never darkens model.
// dst holds the final composite; support_tmp is scratch. Both are reused
// (caller supplies thread-local Mats); neither needs pre-clearing — the in-place
// expolygons_to_cvmat() fully clears its target frame on every call.
// This is the single shared implementation called by BOTH the rasterize() main
// loop and the generate_prz() cache-miss path, guaranteeing byte-identical output.
void rasterize_layer_dual(
    cv::Mat                 &dst,
    cv::Mat                 &support_tmp,
    const ExPolygons        &model_polys,
    const ExPolygons        &support_polys,
    const Resolution        &res,
    const PixelDim          &pxdim,
    const RasterBase::Trafo &trafo,
    double                   gamma,
    int                      aa_steps,
    uint8_t                  gray_lo,
    uint8_t                  gray_hi,
    int                      blur_pixel,
    uint8_t                  picture_grayscale,
    bool                     is_binary);

} // namespace sla
} // namespace Slic3r
