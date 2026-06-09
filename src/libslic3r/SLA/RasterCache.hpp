#pragma once

#include <string>
#include <cstddef>
#include <vector>

#include <boost/filesystem.hpp>

#include "libslic3r/SLAPrint.hpp"

namespace Slic3r {
namespace sla {

// Cache key derived from CRC32 of raster params + all layer ExPolygons.
struct RasterCacheKey {
    std::string                  hash;  // 8-char hex (32-bit CRC32)
    boost::filesystem::path      dir;   // temp_dir / "phrozen_sla_cache" / hash
};

class RasterCache {
public:
    // Compute key from raster params and printer input layers.
    static RasterCacheKey compute_key(
        const SLARasterParams                  &rp,
        const std::vector<SLAPrint::PrintLayer> &printer_input);

    // Create the cache directory. MUST be called once (single-threaded) before
    // any parallel write_layer() calls to avoid concurrent create_directories
    // contention on Windows NTFS.
    static void ensure_dir(const RasterCacheKey &key);

    // Write one layer's PRZ-RLE bytes to layer_{lid:04d}.rle.
    // REQUIRES: ensure_dir(key) has already been called.
    // Each lid produces a unique filename; concurrent calls for different lids
    // are safe without locks.
    static void write_layer(const RasterCacheKey &key, size_t lid,
                            const char           *data,
                            size_t                size);
    static void write_layer(const RasterCacheKey &key, size_t lid,
                            const std::string    &rle_bytes);

    // Write one layer's preview thumbnail (lightweight gray-RLE bytes, see
    // rle_encode_gray) to layer_{lid:04d}_preview.rle.
    // REQUIRES: ensure_dir(key) has already been called.
    // Throws std::runtime_error on open/write failure: the exception must
    // propagate out of the parallel loop so mark_complete() is skipped and the
    // cache is judged invalid (liveness binding — see design.md D4).
    static void write_thumb(const RasterCacheKey &key, size_t lid,
                            const unsigned char  *data,
                            size_t                size);
    static void write_thumb(const RasterCacheKey             &key, size_t lid,
                            const std::vector<unsigned char> &rle_bytes);

    // Write the "cache_complete" sentinel after all layers are written.
    // is_valid() returns true only after this is called.
    static void mark_complete(const RasterCacheKey &key);

    // Read one layer's PRZ-RLE bytes; throws std::runtime_error on failure.
    // Filename: layer_{lid:04d}.rle
    static std::string read_layer(const RasterCacheKey &key, size_t lid);

    // Read one layer's preview thumbnail gray-RLE bytes.
    // Filename: layer_{lid:04d}_preview.rle
    // Returns an empty vector when the file is missing/unreadable so the GUI
    // preview can fall back to vector rendering without exception handling.
    static std::vector<unsigned char> read_thumb(const RasterCacheKey &key,
                                                 size_t                lid);

    // Lightweight, self-contained grayscale RLE used ONLY for the preview thumb
    // (deliberately NOT the firmware PRZ-RLE, and NOT OpenCV imgcodecs/PNG — the
    // latter drags a second libjpeg-turbo into the link and causes LNK2005).
    // Format: [u32 w LE][u32 h LE] then (value:u8, run:u8 in 1..255) pairs.
    // encode fills `out` (reusing its capacity → no per-layer realloc after warmup).
    static void rle_encode_gray(const unsigned char        *data, int w, int h,
                                std::vector<unsigned char> &out);
    // Decodes into `out` (raw CV_8UC1 bytes) and reports w/h. Returns false on
    // malformed input. `out` capacity is reused across calls.
    static bool rle_decode_gray(const std::vector<unsigned char> &in,
                                std::vector<unsigned char>       &out,
                                int &w, int &h);

    // Returns true iff the "cache_complete" sentinel exists in key.dir,
    // indicating a fully-written cache.
    static bool is_valid(const RasterCacheKey &key);

    // Delete cache subdirectories older than max_age_days; silent on errors.
    static void cleanup_old(int max_age_days = 7);

    // Exposed for generate_prz() to reconstruct a key from a stored hash string.
    static boost::filesystem::path base_dir();

private:
    // Bump this whenever the cache format changes to invalidate old entries.
    static constexpr int CACHE_VERSION = 6; // bumped: added per-layer preview thumbnail (layer_{lid:04d}_preview.rle, gray-RLE) - old caches lack thumbs
};

} // namespace sla
} // namespace Slic3r
