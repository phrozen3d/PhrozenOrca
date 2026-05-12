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

    // Write the "cache_complete" sentinel after all layers are written.
    // is_valid() returns true only after this is called.
    static void mark_complete(const RasterCacheKey &key);

    // Read one layer's PRZ-RLE bytes; throws std::runtime_error on failure.
    // Filename: layer_{lid:04d}.rle
    static std::string read_layer(const RasterCacheKey &key, size_t lid);

    // Returns true iff the "cache_complete" sentinel exists in key.dir,
    // indicating a fully-written cache.
    static bool is_valid(const RasterCacheKey &key);

    // Delete cache subdirectories older than max_age_days; silent on errors.
    static void cleanup_old(int max_age_days = 7);

    // Exposed for generate_prz() to reconstruct a key from a stored hash string.
    static boost::filesystem::path base_dir();

private:
    // Bump this whenever the cache format changes to invalidate old entries.
    static constexpr int CACHE_VERSION = 3;
};

} // namespace sla
} // namespace Slic3r
