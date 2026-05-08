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

    // Write one layer's RLE bytes (przByte content, no length prefix or CRLF).
    // Creates key.dir if needed; writes atomically via temp-file + rename.
    static void write_layer(const RasterCacheKey &key, size_t lid,
                            const std::string    &prz_rle_bytes);

    // Write manifest after all layers are written.
    static void finalize(const RasterCacheKey  &key, size_t layer_count,
                         const SLARasterParams &rp);

    // Validate: manifest exists, layer_count matches, all layer files exist and non-zero.
    static bool is_valid(const RasterCacheKey &key, size_t expected_layers);

    // Read one layer's RLE bytes; throws std::runtime_error on failure.
    static std::string read_layer(const RasterCacheKey &key, size_t lid);

    // Delete cache subdirectories older than max_age_days; silent on errors.
    static void cleanup_old(int max_age_days = 7);

    // Exposed for generate_prz() to reconstruct a key from a stored hash string.
    static boost::filesystem::path base_dir();

private:
    static constexpr int CACHE_VERSION = 1;
};

} // namespace sla
} // namespace Slic3r