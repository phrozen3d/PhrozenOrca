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

// Composite a pure-binary support track onto dst using ONLY a local ROI
// (change: prz-support-roi-composite, "Opt-2").
//
// Replaces the full-frame "support cv::Mat + full-frame cv::max" path: supports
// are rasterized (contour→255, holes→0) into a thread-local ROI-sized buffer
// covering the support bounding box (plus a guard band, clamped to the image),
// then composited via a ROI-local cv::max(dst(roi), buf, dst(roi)).
//
// Byte-identical to the old full-frame composite: pixel coordinates are computed
// at FULL-FRAME precision (same to_cv_point math as the fast path) and only the
// destination index is shifted by the integer ROI origin — no coord_t truncation
// is introduced, so the ROI fill maps 1:1 onto the full-frame fill. Holes (0)
// preserve underlying model pixels; the guard-band border (0) leaves dst
// untouched. Empty support → no-op (dst untouched). Because the output matches
// the full-frame version pixel-for-pixel, CACHE_VERSION is intentionally NOT
// bumped.
void composite_support_binary(
    cv::Mat                 &dst,
    const ExPolygons        &support_polys,
    const Resolution        &res,
    const PixelDim          &pxdim,
    const RasterBase::Trafo &trafo);

// Dual-track layer rasterization (change: prz-support-binary-output).
// Renders the model and support geometry of one layer with DIFFERENT treatment
// and composites them, so supports come out pure-binary and solid:
//   * model_polys  → AA/gray-scale/blur + picture_grayscale LUT, UNLESS
//                    is_binary (bottom/plate-contact layers) which forces the
//                    model to pure binary too (no AA/gray/blur) for adhesion.
//   * support_polys → ALWAYS pure binary (gamma=0, no AA/gray/blur) and NEVER
//                    run through the picture_grayscale LUT (exempt from global
//                    dimming, always 255).
//   * composite     → support is rasterized + composited within a LOCAL ROI only
//                    (composite_support_binary): dst = max(model_after_LUT,
//                    support_255), byte-identical to the old full-frame cv::max.
// dst holds the final composite (caller supplies a thread-local Mat). No full-frame
// support scratch buffer is needed — composite_support_binary uses its own
// thread-local ROI buffer (change: prz-support-roi-composite).
// This is the single shared implementation called by BOTH the rasterize() main
// loop and the generate_prz() cache-miss path, guaranteeing byte-identical output.
void rasterize_layer_dual(
    cv::Mat                 &dst,
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
