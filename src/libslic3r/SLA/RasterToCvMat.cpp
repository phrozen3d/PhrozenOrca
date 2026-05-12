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
#include <cstring>

namespace Slic3r {
namespace sla {

// ---------------------------------------------------------------------------
// Fast-path helpers (shared by both expolygons_to_cvmat overloads)
// ---------------------------------------------------------------------------

// Mirrors the exact coordinate-to-pixel mapping performed by AGGRaster::to_path():
//
//   landscape:  px = p.x * sx + cx,  py = p.y * sy + cy
//   portrait:   px = p.y * sy + cx,  py = p.x * sx + cy
//   mirror_x:   px = W - px
//   mirror_y:   py = H - py
//
// where sx = SCALING_FACTOR / pxdim.w_mm, sy = SCALING_FACTOR / pxdim.h_mm,
//       cx = center_x * sx, cy = center_y * sy   (AGG translate_all_paths values)
// and W = res.width_px, H = res.height_px
// (AGG's flip_x(0, W) → x = W - x; flip_y(0, H) → y = H - y).
static inline cv::Point to_cv_point(
    const Point             &p,
    double                   sx, double sy,
    double                   cx, double cy,
    bool                     flipXY,
    bool                     mirror_x, bool mirror_y,
    int                      W, int H)
{
    double px, py;
    if (flipXY) {
        // _to_path_flpxy: first coord = getPy, second = getPx
        px = p(1) * sy + cx;
        py = p(0) * sx + cy;
    } else {
        px = p(0) * sx + cx;
        py = p(1) * sy + cy;
    }
    if (mirror_x) px = W - px;
    if (mirror_y) py = H - py;
    return cv::Point(static_cast<int>(std::lround(px)),
                     static_cast<int>(std::lround(py)));
}

// Fill one Polygon (outer or hole) into dst using the thread-local point buffer.
// Thread-local storage: allocated once per thread, reused every call → zero malloc.
static void fill_polygon_fast(
    cv::Mat                 &dst,
    const Polygon           &poly,
    double                   sx, double sy,
    double                   cx, double cy,
    bool                     flipXY,
    bool                     mirror_x, bool mirror_y,
    int                      W, int H,
    uchar                    color)
{
    thread_local std::vector<cv::Point> contour_buf;

    const auto &pts = poly.points;
    contour_buf.resize(pts.size());
    for (size_t k = 0; k < pts.size(); ++k)
        contour_buf[k] = to_cv_point(pts[k], sx, sy, cx, cy, flipXY,
                                     mirror_x, mirror_y, W, H);

    const cv::Point *ptr  = contour_buf.data();
    int              npts = static_cast<int>(contour_buf.size());
    cv::fillPoly(dst, &ptr, &npts, 1, cv::Scalar(color));
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

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
    const int rows = static_cast<int>(res.height_px);
    const int cols = static_cast<int>(res.width_px);

    // Fast path: gamma≈0 and no blur → pure binary rasterization.
    // Bypasses AGG sub-pixel coverage computation (otherwise ~68 M pixel iterations
    // of wasted coverage math when the gamma threshold collapses everything to 0/255).
    if (std::abs(gamma) < 1e-6 && blur_pixel == 0) {
        cv::Mat mat = cv::Mat::zeros(rows, cols, CV_8UC1);

        const double sx = SCALING_FACTOR / pxdim.w_mm;
        const double sy = SCALING_FACTOR / pxdim.h_mm;
        const double cx = trafo.center_x * sx;
        const double cy = trafo.center_y * sy;

        for (const ExPolygon &ep : polys) {
            fill_polygon_fast(mat, ep.contour, sx, sy, cx, cy,
                              trafo.flipXY, trafo.mirror_x, trafo.mirror_y,
                              cols, rows, 255);
            for (const Polygon &hole : ep.holes)
                fill_polygon_fast(mat, hole, sx, sy, cx, cy,
                                  trafo.flipXY, trafo.mirror_x, trafo.mirror_y,
                                  cols, rows, 0);
        }
        return mat;
    }

    // Slow path: AGG sub-pixel AA (required when gamma > 0 or blur is active).
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

void expolygons_to_cvmat(
    cv::Mat                 &dst,
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
    const int rows = static_cast<int>(res.height_px);
    const int cols = static_cast<int>(res.width_px);

    // Allocate only when the size/type doesn't match; otherwise reuse the buffer.
    if (dst.rows != rows || dst.cols != cols || dst.type() != CV_8UC1 || !dst.isContinuous())
        dst.create(rows, cols, CV_8UC1);

    // Fast path: gamma≈0 and no blur → pure binary rasterization via cv::fillPoly.
    // Replaces AGG's 68 M-pixel sub-pixel coverage pass with integer scan-line fill.
    if (std::abs(gamma) < 1e-6 && blur_pixel == 0) {
        dst.setTo(0);  // clear to black (matches AGG's clear(background) in constructor)

        const double sx = SCALING_FACTOR / pxdim.w_mm;
        const double sy = SCALING_FACTOR / pxdim.h_mm;
        const double cx = trafo.center_x * sx;
        const double cy = trafo.center_y * sy;

        for (const ExPolygon &ep : polys) {
            fill_polygon_fast(dst, ep.contour, sx, sy, cx, cy,
                              trafo.flipXY, trafo.mirror_x, trafo.mirror_y,
                              cols, rows, 255);
            for (const Polygon &hole : ep.holes)
                fill_polygon_fast(dst, hole, sx, sy, cx, cy,
                                  trafo.flipXY, trafo.mirror_x, trafo.mirror_y,
                                  cols, rows, 0);
        }
        return;
    }

    // Slow path: AGG sub-pixel AA.
    auto raster = create_raster_grayscale_aa(res, pxdim, gamma, trafo);

    for (const ExPolygon &poly : polys)
        raster->draw(poly);

    // Encode directly into dst.data — no intermediate 65 MB vector, no clone.
    raster->encode([&dst](const void *ptr, size_t w, size_t h, size_t) {
        std::memcpy(dst.data, ptr, w * h);
        return EncodedRaster{};
    });

    if (aa_steps > 0) {
        const double gray_interval = 255.0 / double(aa_steps);
        const double range         = double(gray_hi) - double(gray_lo);
        const bool   need_remap    = (gray_lo != 0 || gray_hi != 255);
        uint8_t     *data          = dst.data;
        const int    total         = dst.rows * dst.cols;

        for (int i = 0; i < total; ++i) {
            uint8_t &c = data[i];
            if (c == 0 || c == 255) continue;

            c = (uint8_t)std::round(
                std::round(double(c) / gray_interval) / double(aa_steps) * 255.0);

            if (need_remap)
                c = (uint8_t)std::round(double(gray_lo) + range * (double(c) / 255.0));
        }
    }

    if (blur_pixel >= 2) {
        agg::rendering_buffer rbuf(dst.data,
                                   static_cast<unsigned>(dst.cols),
                                   static_cast<unsigned>(dst.rows),
                                   dst.cols);
        agg::pixfmt_gray8 pixf(rbuf);
        agg::stack_blur_gray8(pixf,
                              static_cast<unsigned>(blur_pixel),
                              static_cast<unsigned>(blur_pixel));
    }
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
