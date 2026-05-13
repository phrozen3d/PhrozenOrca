// Phase 4 correctness tests for the ROI-based slow path introduced in
// optimize-slice-performance.
//
// Strategy: the returning overload  expolygons_to_cvmat(polys, ...) → cv::Mat
//           still uses the original full-frame AGGRaster path (no ROI).
//           The in-place overload  expolygons_to_cvmat(dst, polys, ...)
//           is the new ROI path.
// Every test verifies they produce bit-identical output.

#include <catch2/catch.hpp>

#include "libslic3r/SLA/RasterToCvMat.hpp"
#include "libslic3r/SLA/RasterBase.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/libslic3r.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

using namespace Slic3r;
using namespace Slic3r::sla;

namespace {

// Build a trafo centred exactly at (W/2, H/2) in pixel space.
static RasterBase::Trafo make_trafo(int W, int H, const PixelDim &pxdim,
                                    bool mirror_x = false,
                                    bool mirror_y = false,
                                    bool flipXY   = false)
{
    RasterBase::Trafo t;
    t.center_x = static_cast<coord_t>(W / 2.0 * pxdim.w_mm / SCALING_FACTOR);
    t.center_y = static_cast<coord_t>(H / 2.0 * pxdim.h_mm / SCALING_FACTOR);
    t.mirror_x = mirror_x;
    t.mirror_y = mirror_y;
    t.flipXY   = flipXY;
    return t;
}

// Convert a pixel position (relative to image centre) into Slic3r coord_t.
// With centre trafo: pixel p maps to world coord (p - W/2) * pxdim / SCALING_FACTOR.
static coord_t px_to_coord(double pixel_offset, double pxdim_mm)
{
    return static_cast<coord_t>(pixel_offset * pxdim_mm / SCALING_FACTOR);
}

// Build a rectangle whose corners map to pixels [px0, px1] x [py0, py1]
// in the full-frame raster (before any mirror/flip transform).
static ExPolygon make_rect(double px0, double px1, double py0, double py1,
                           int W, int H, const PixelDim &pxdim)
{
    coord_t x0 = px_to_coord(px0 - W / 2.0, pxdim.w_mm);
    coord_t x1 = px_to_coord(px1 - W / 2.0, pxdim.w_mm);
    coord_t y0 = px_to_coord(py0 - H / 2.0, pxdim.h_mm);
    coord_t y1 = px_to_coord(py1 - H / 2.0, pxdim.h_mm);
    ExPolygon ep;
    ep.contour.points = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    return ep;
}

// Count pixels that differ between a and b.
static int count_diff(const cv::Mat &a, const cv::Mat &b)
{
    if (a.size() != b.size() || a.type() != b.type()) return INT_MAX;
    cv::Mat diff;
    cv::absdiff(a, b, diff);
    return cv::countNonZero(diff);
}

// Run both overloads with the same parameters and verify pixel-identical output.
static void check_roi_vs_ref(
    const ExPolygons        &polys,
    int W, int H,
    double gamma, int aa_steps, uint8_t gray_lo, uint8_t gray_hi, int blur_pixel,
    bool mirror_x = false, bool mirror_y = false, bool flipXY = false)
{
    const PixelDim          pxdim{0.05, 0.05};
    const Resolution        res{static_cast<size_t>(W), static_cast<size_t>(H)};
    const RasterBase::Trafo trafo = make_trafo(W, H, pxdim, mirror_x, mirror_y, flipXY);

    // Reference: returning overload — original full-frame AGGRaster path.
    cv::Mat ref = expolygons_to_cvmat(
        polys, res, pxdim, trafo, gamma, aa_steps, gray_lo, gray_hi, blur_pixel);

    // ROI path: in-place overload — new ROI-optimised path.
    cv::Mat dst;
    expolygons_to_cvmat(
        dst, polys, res, pxdim, trafo, gamma, aa_steps, gray_lo, gray_hi, blur_pixel);

    REQUIRE(ref.rows == H);
    REQUIRE(ref.cols == W);
    REQUIRE(dst.rows == H);
    REQUIRE(dst.cols == W);
    CHECK(count_diff(ref, dst) == 0);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Task 4.1 — Pixel-level comparison: ROI path vs. reference full-frame path
// ---------------------------------------------------------------------------
TEST_CASE("ROI path matches full-frame reference", "[RasterToCvMat][ROI][4.1]")
{
    const int W = 200, H = 160;
    const PixelDim pxdim{0.05, 0.05};

    ExPolygons polys;
    // Asymmetric rectangle: pixels [40,140] x [30,110] — not centred, exercises ROI offset.
    polys.push_back(make_rect(40, 140, 30, 110, W, H, pxdim));

    SECTION("gamma=1.0, no blur, no aa_steps") {
        check_roi_vs_ref(polys, W, H, 1.0, 0, 0, 255, 0);
    }
    SECTION("gamma=1.0, blur=3") {
        check_roi_vs_ref(polys, W, H, 1.0, 0, 0, 255, 3);
    }
    SECTION("gamma=1.0, aa_steps=4") {
        check_roi_vs_ref(polys, W, H, 1.0, 4, 0, 255, 0);
    }
    SECTION("gamma=1.5, aa_steps=4, blur=2") {
        check_roi_vs_ref(polys, W, H, 1.5, 4, 0, 255, 2);
    }
    SECTION("gamma=1.0, aa_steps=4, gray remap [50,200]") {
        check_roi_vs_ref(polys, W, H, 1.0, 4, 50, 200, 0);
    }
    SECTION("gamma threshold (gamma=-1)") {
        // gamma <= 0 → gamma_threshold(0.5) branch
        check_roi_vs_ref(polys, W, H, -1.0, 0, 0, 255, 0);
    }
}

// ---------------------------------------------------------------------------
// Task 4.2 — Empty layer: all-black output, fast exit (no raster work)
// ---------------------------------------------------------------------------
TEST_CASE("Empty layer produces all-black output", "[RasterToCvMat][EmptyLayer][4.2]")
{
    const int W = 200, H = 160;
    const PixelDim          pxdim{0.05, 0.05};
    const Resolution        res{static_cast<size_t>(W), static_cast<size_t>(H)};
    const RasterBase::Trafo trafo = make_trafo(W, H, pxdim);

    // In-place overload with empty polys
    cv::Mat dst;
    expolygons_to_cvmat(dst, {}, res, pxdim, trafo, 1.0, 0, 0, 255, 0);

    REQUIRE(dst.rows == H);
    REQUIRE(dst.cols == W);
    CHECK(cv::countNonZero(dst) == 0);

    // Also verify: calling twice reuses the same Mat (no residual from a previous call)
    ExPolygons polys;
    polys.push_back(make_rect(40, 140, 30, 110, W, H, pxdim));
    expolygons_to_cvmat(dst, polys, res, pxdim, trafo, 1.0, 0, 0, 255, 0);  // draw something
    expolygons_to_cvmat(dst, {},    res, pxdim, trafo, 1.0, 0, 0, 255, 0);  // clear again
    CHECK(cv::countNonZero(dst) == 0);  // no residual pixels from previous layer
}

// ---------------------------------------------------------------------------
// Task 4.3 — Graceful fallback: BBox ≈ full image (roi_w ≈ W, roi_h ≈ H)
// ---------------------------------------------------------------------------
TEST_CASE("Graceful fallback when geometry fills almost the entire image",
          "[RasterToCvMat][Fallback][4.3]")
{
    const int W = 200, H = 160;
    const PixelDim pxdim{0.05, 0.05};

    ExPolygons polys;
    // Rectangle covering 96% of image from each side (blur will push roi to full extent)
    polys.push_back(make_rect(2, 198, 2, 158, W, H, pxdim));

    SECTION("no blur — roi_w=196, roi_h=156") {
        check_roi_vs_ref(polys, W, H, 1.0, 0, 0, 255, 0);
    }
    SECTION("blur=4 — roi_w/h clamp to W/H") {
        // With blur=4 expansion, roi extends beyond image → clamped to W×H
        check_roi_vs_ref(polys, W, H, 1.0, 0, 0, 255, 4);
    }
}

// ---------------------------------------------------------------------------
// Task 4.4 — mirror_x, mirror_y, flipXY (single and combined)
// ---------------------------------------------------------------------------
TEST_CASE("ROI path matches reference under mirror/flip transforms",
          "[RasterToCvMat][Mirror][4.4]")
{
    const int W = 200, H = 160;
    const PixelDim pxdim{0.05, 0.05};

    ExPolygons polys;
    // Deliberately asymmetric: more to the left/top so mirror is visually distinguishable
    polys.push_back(make_rect(30, 100, 20, 90, W, H, pxdim));

    SECTION("mirror_x") {
        check_roi_vs_ref(polys, W, H, 1.0, 0, 0, 255, 0, true, false, false);
    }
    SECTION("mirror_y") {
        check_roi_vs_ref(polys, W, H, 1.0, 0, 0, 255, 0, false, true, false);
    }
    SECTION("flipXY") {
        check_roi_vs_ref(polys, W, H, 1.0, 0, 0, 255, 0, false, false, true);
    }
    SECTION("mirror_x + mirror_y") {
        check_roi_vs_ref(polys, W, H, 1.0, 0, 0, 255, 0, true, true, false);
    }
    SECTION("mirror_x + flipXY") {
        check_roi_vs_ref(polys, W, H, 1.0, 0, 0, 255, 0, true, false, true);
    }
    SECTION("mirror_y + flipXY") {
        check_roi_vs_ref(polys, W, H, 1.0, 0, 0, 255, 0, false, true, true);
    }
    SECTION("mirror_x + mirror_y + flipXY") {
        check_roi_vs_ref(polys, W, H, 1.0, 0, 0, 255, 0, true, true, true);
    }
    // Stress-test the trafo correction formula with blur active
    SECTION("mirror_x + blur=3") {
        check_roi_vs_ref(polys, W, H, 1.0, 0, 0, 255, 3, true, false, false);
    }
    SECTION("all three + blur=3") {
        check_roi_vs_ref(polys, W, H, 1.0, 0, 0, 255, 3, true, true, true);
    }
}

// ---------------------------------------------------------------------------
// Task 4.5 — Fast path (gamma≈0, blur=0) is unaffected by the ROI refactor
// ---------------------------------------------------------------------------
TEST_CASE("Fast path (gamma=0, blur=0) output unchanged by ROI refactor",
          "[RasterToCvMat][FastPath][4.5]")
{
    const int W = 200, H = 160;
    const PixelDim pxdim{0.05, 0.05};

    ExPolygons polys;
    polys.push_back(make_rect(40, 140, 30, 110, W, H, pxdim));

    // gamma≈0, blur=0 → cv::fillPoly fast path; ROI logic is never entered.
    // Both overloads should still produce identical binary output.
    SECTION("no mirror/flip") {
        check_roi_vs_ref(polys, W, H, 0.0, 0, 0, 255, 0);
    }
    SECTION("mirror_x") {
        check_roi_vs_ref(polys, W, H, 0.0, 0, 0, 255, 0, true, false, false);
    }
    SECTION("mirror_y") {
        check_roi_vs_ref(polys, W, H, 0.0, 0, 0, 255, 0, false, true, false);
    }
    SECTION("flipXY") {
        check_roi_vs_ref(polys, W, H, 0.0, 0, 0, 255, 0, false, false, true);
    }
    SECTION("empty polys — all-black even in fast path") {
        check_roi_vs_ref({}, W, H, 0.0, 0, 0, 255, 0);
    }
}
