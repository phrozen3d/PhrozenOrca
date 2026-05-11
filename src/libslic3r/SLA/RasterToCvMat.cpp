#include "RasterToCvMat.hpp"
#include "libslic3r/SLA/RasterBase.hpp"

#include <opencv2/imgproc.hpp>

#include <agg/agg_blur.h>
#include <agg/agg_pixfmt_gray.h>
#include <agg/agg_rendering_buffer.h>

#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>

#include <cmath>
#include <algorithm>

namespace Slic3r {
namespace sla {

cv::Mat expolygons_to_cvmat(
    const ExPolygons        &polys,
    const Resolution        &res,
    const PixelDim          &pxdim,
    const RasterBase::Trafo &trafo,
    double                   gamma,
    int                      aa_steps,
    uint8_t                  gray_lo,
    uint8_t                  gray_hi,
    int                      blur_pixel)
{
    auto raster = create_raster_grayscale_aa(res, pxdim, gamma, trafo);

    for (const ExPolygon &poly : polys)
        raster->draw(poly);

    // Extract the raw pixel buffer without PNG compression using a passthrough encoder.
    auto raw_enc = [](const void *ptr, size_t w, size_t h, size_t /*num_components*/) {
        const auto *buf = static_cast<const uint8_t *>(ptr);
        return EncodedRaster(std::vector<uint8_t>(buf, buf + w * h), "raw");
    };

    EncodedRaster enc = raster->encode(raw_enc);

    // Wrap buffer in a cv::Mat and clone to take ownership before enc goes out of scope.
    cv::Mat mat(int(res.height_px), int(res.width_px), CV_8UC1,
                const_cast<void *>(enc.data()));
    mat = mat.clone();

    // Gray scale level post-processing: only applied when aa_steps > 0
    // (i.e. anti_aliasing == spGrayScaleLevel).
    //
    // Stage 1 — AA quantization: snap the continuous AGG grayscale to aa_steps
    //           discrete levels to remove unwanted intermediate shades.
    // Stage 2 — Range mapping: linearly remap the quantized value from [0,255]
    //           to [gray_lo, gray_hi].  When {0,255} this is a no-op.
    //
    // Pure black (0) and pure white (255) pixels are left untouched.
    if (aa_steps > 0) {
        const double gray_interval = 255.0 / double(aa_steps);
        const double range         = double(gray_hi) - double(gray_lo);
        const bool   need_remap    = (gray_lo != 0 || gray_hi != 255);
        uint8_t     *data          = mat.data;
        const int    total         = mat.rows * mat.cols;

        for (int i = 0; i < total; ++i) {
            uint8_t &c = data[i];
            if (c == 0 || c == 255) continue;

            // Stage 1: quantize to aa_steps discrete levels
            c = (uint8_t)std::round(
                std::round(double(c) / gray_interval) / double(aa_steps) * 255.0);

            // Stage 2: remap to [gray_lo, gray_hi]
            if (need_remap)
                c = (uint8_t)std::round(double(gray_lo) + range * (double(c) / 255.0));
        }
    }

    // Stack blur: O(radius) separable algorithm, replaces cv::GaussianBlur.
    // blur_pixel is the radius (2–8); applied after AA/gray-scale post-processing.
    if (blur_pixel >= 2) {
        agg::rendering_buffer rbuf(mat.data,
                                   static_cast<unsigned>(mat.cols),
                                   static_cast<unsigned>(mat.rows),
                                   mat.cols);
        agg::pixfmt_gray8 pixf(rbuf);
        agg::stack_blur_gray8(pixf,
                              static_cast<unsigned>(blur_pixel),
                              static_cast<unsigned>(blur_pixel));
    }

    return mat;
}

std::vector<cv::Mat> expolygons_layers_to_cvmat(
    const std::vector<ExPolygons> &layer_polys,
    const Resolution              &res,
    const PixelDim                &pxdim,
    const RasterBase::Trafo       &trafo,
    double                         gamma,
    int                            aa_steps,
    uint8_t                        gray_lo,
    uint8_t                        gray_hi,
    int                            blur_pixel)
{
    std::vector<cv::Mat> result(layer_polys.size());

    tbb::parallel_for(tbb::blocked_range<size_t>(0, layer_polys.size()),
        [&](const tbb::blocked_range<size_t> &r) {
            for (size_t i = r.begin(); i < r.end(); ++i)
                result[i] = expolygons_to_cvmat(layer_polys[i], res, pxdim, trafo,
                                                gamma, aa_steps, gray_lo, gray_hi,
                                                blur_pixel);
        });

    return result;
}

void apply_picture_grayscale_lut(cv::Mat &mat, uint8_t level)
{
    if (level == 255) return;
    uint8_t lut_data[256];
    for (int i = 0; i < 256; ++i)
        lut_data[i] = static_cast<uint8_t>((static_cast<unsigned>(i) * level + 127u) / 255u);
    cv::Mat lut_mat(1, 256, CV_8UC1, lut_data);
    cv::LUT(mat, lut_mat, mat);
}

} // namespace sla
} // namespace Slic3r
