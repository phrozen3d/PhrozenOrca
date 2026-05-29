#pragma once

// PRZ layer-image orientation helpers (shared single source of truth).
//
// These helpers exist to keep the two ROTATE_90_CLOCKWISE sites — the main
// raster path (SLAPrintSteps.cpp, fills the raster cache) and the cache-miss
// fallback (PhrozenPRZ.cpp) — byte-identical when correcting the per-printer
// image orientation against the Chitubox ground truth.
//
// This header is intentionally included ONLY by the two .cpp files that own a
// rotation site, so OpenCV is not pulled into the lightweight PhrozenPRZ.hpp
// (which GUI translation units include).

#include <opencv2/core.hpp>

#include <libslic3r/PrintConfig.hpp>

namespace Slic3r {

// The printer's final X-mirror state, single source shared with the PRZ header
// Xmirror byte. Derived from display_mirror_mode (lcd_mirror → true;
// normal / dlp_normal → false), falling back to the raw display_mirror_x bool
// for legacy presets without an explicit mode.
inline bool prz_final_x_mirror(const SLAPrinterConfig &pcfg)
{
    switch (pcfg.display_mirror_mode.value) {
    case slammLCDMirror: return true;
    case slammNormal:    return false;
    case slammDLPNormal: return false;
    default:             return pcfg.display_mirror_x.getBool();
    }
}

// Compensate the axis projection introduced by the post-rasterization
// ROTATE_90_CLOCKWISE. Call this IMMEDIATELY AFTER that rotate, on the
// landscape buffer.
//
// Rationale (see openspec change fix-prz-image-mirror-axis-swap, design.md
// "Decision 1" D4 derivation): the 90° rotation rotates the mirror axes, so the
// config mirror_x ends up controlling the final image's vertical axis. A single
// compensating flip restores per-printer alignment with Chitubox:
//   final_x_mirror == false  → vertical flip   (cv::flip code 0)   e.g. Mega (normal)
//   final_x_mirror == true   → horizontal flip (cv::flip code 1)   e.g. Revo (lcd_mirror)
//
// cv::flip preserves the matrix dimensions (the landscape 7680x4320 layout) and
// the CV_8UC1 row-major layout, so the downstream RLE scan is unaffected.
inline void prz_orient_after_rotate(cv::Mat &mat, bool final_x_mirror)
{
    cv::flip(mat, mat, final_x_mirror ? 1 : 0);
}

} // namespace Slic3r