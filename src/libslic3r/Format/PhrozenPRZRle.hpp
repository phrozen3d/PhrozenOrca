#pragma once

// PRZ Band/Tiled fused rotate + RLE encoder
// (openspec change: prz-band-tiled-rle-fusion).
//
// DEPENDENCY BOUNDARY (rule): OpenCV core only. This header and its .cpp MUST
// NOT include PhrozenPRZOrient.hpp or PrintConfig.hpp — pulling in libslic3r
// would break the standalone single-file compile used by the `#ifdef
// PRZ_RLE_TEST` inline test hook in PhrozenPRZRle.cpp. The per-tile flip is
// therefore done with cv::flip directly (code = final_x_mirror ? 1 : 0), which
// is byte-equivalent to prz_orient_after_rotate(); callers still compute the
// bool via prz_final_x_mirror() and pass it in.

#include <cstddef>
#include <vector>

#include <opencv2/core.hpp>

namespace Slic3r {

// Band width K (number of portrait columns per tile), shared by both call sites
// (SLAPrintSteps.cpp main raster loop and PhrozenPRZ.cpp cache-miss path).
//
// Pinned at 256 after profiling. Rationale (capacity trade-off):
//   - The K x M tile is ~K * display_pixels_x bytes. At the 16K-class panel
//     (M = display_pixels_x = 13320), K=256 -> ~3.4 MB, which stays resident in
//     L2/L3 so the rotate output is consumed by the RLE scan while still hot —
//     no DRAM round-trip, and no 65 MB full-frame rotated copy.
//   - K too small: more cv::rotate calls + run-boundary churn per layer.
//   - K too large: the tile spills out of L3 and degrades to DRAM traffic.
//   - Byte output is INDEPENDENT of K (verified by the sandbox K-invariance
//     sweep), so this is a pure performance/footprint knob, never a correctness
//     one — it can be retuned freely without touching CACHE_VERSION.
// Measured impact: 16K plate slice peak RAM ~3.6 GB -> ~1.2 GB (eliminating the
// 8 x 65 MB resident mat_rotated copies), wall-clock not regressed.
constexpr int PRZ_RLE_BAND_COLS = 256;

// Fused band/tiled ROTATE_90_CW + per-tile flip + PRZ-RLE encode of a portrait
// CV_8UC1 layer (rows = display_pixels_x, cols = display_pixels_y). Produces the
// PRZ layer byte stream (0x55 head + runs + checksum) into `out`, byte-identical
// to the legacy full-frame rotate->flip->linear-RLE path. Returns the number of
// bytes written. `out` is caller-owned and reused across layers (zero-malloc
// contract). The landscape rotation/flip is performed band-by-band so no
// full-frame rotated copy (mat_rotated) is ever materialized.
std::size_t prz_encode_layer_banded(const cv::Mat&     portrait,
                                    bool               final_x_mirror,
                                    int                band_cols,
                                    std::vector<char>& out);

} // namespace Slic3r