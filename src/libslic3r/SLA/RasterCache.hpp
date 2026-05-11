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

    // Write one layer's PNG bytes atomically via temp-file + rename.
    // Creates key.dir if needed. Filename: layer_{lid:04d}.png
    static void write_layer(const RasterCacheKey &key, size_t lid,
                            const std::string    &png_bytes);

    // Read one layer's PNG bytes; throws std::runtime_error on failure.
    // Filename: layer_{lid:04d}.png
    static std::string read_layer(const RasterCacheKey &key, size_t lid);

    // Quick sentinel check: returns true iff layer_0000.png exists in key.dir.
    static bool is_valid(const RasterCacheKey &key);

    // Delete cache subdirectories older than max_age_days; silent on errors.
    static void cleanup_old(int max_age_days = 7);

    // Exposed for generate_prz() to reconstruct a key from a stored hash string.
    static boost::filesystem::path base_dir();

private:
    // Bump this whenever the cache format changes to invalidate old entries.
    static constexpr int CACHE_VERSION = 2;
};

} // namespace sla
} // namespace Slic3r
