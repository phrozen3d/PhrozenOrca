#include "RasterCache.hpp"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <ctime>

#include <miniz.h>
#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>

namespace Slic3r {
namespace sla {

namespace fs = boost::filesystem;

boost::filesystem::path RasterCache::base_dir()
{
    return fs::temp_directory_path() / "phrozen_sla_cache";
}

RasterCacheKey RasterCache::compute_key(
    const SLARasterParams                  &rp,
    const std::vector<SLAPrint::PrintLayer> &printer_input)
{
    mz_ulong crc = mz_crc32(0, nullptr, 0);

    // Hash SLARasterParams as raw bytes
    crc = mz_crc32(crc, reinterpret_cast<const unsigned char *>(&rp), sizeof(rp));

    // Hash all layer ExPolygons point data
    for (const SLAPrint::PrintLayer &layer : printer_input) {
        const ExPolygons &slices = layer.transformed_slices();
        for (const ExPolygon &ep : slices) {
            const Points &pts = ep.contour.points;
            if (!pts.empty())
                crc = mz_crc32(crc,
                    reinterpret_cast<const unsigned char *>(pts.data()),
                    static_cast<mz_ulong>(pts.size() * sizeof(Point)));
            for (const Polygon &hole : ep.holes) {
                const Points &hpts = hole.points;
                if (!hpts.empty())
                    crc = mz_crc32(crc,
                        reinterpret_cast<const unsigned char *>(hpts.data()),
                        static_cast<mz_ulong>(hpts.size() * sizeof(Point)));
            }
        }
    }

    // Include CACHE_VERSION so any format change produces a new key.
    crc = mz_crc32(crc,
        reinterpret_cast<const unsigned char *>(&CACHE_VERSION),
        sizeof(CACHE_VERSION));

    std::ostringstream oss;
    oss << std::hex << std::setw(8) << std::setfill('0') << static_cast<uint32_t>(crc);
    std::string hash = oss.str();

    return {hash, base_dir() / hash};
}

void RasterCache::ensure_dir(const RasterCacheKey &key)
{
    // Call this ONCE (single-threaded) before any parallel write_layer() calls.
    // Doing create_directories inside write_layer() from many threads causes
    // severe NTFS directory-lock contention on Windows, serializing all I/O.
    fs::create_directories(key.dir);
}

void RasterCache::write_layer(const RasterCacheKey &key, size_t lid,
                              const char *data, size_t size)
{
    // Direct write — caller must call ensure_dir() first; no temp+rename.
    // Each lid maps to a unique filename so concurrent calls for different
    // lids never collide.
    std::ostringstream name;
    name << "layer_" << std::setw(4) << std::setfill('0') << lid << ".rle";
    fs::path dest_path = key.dir / name.str();

    std::ofstream f(dest_path.string(), std::ios::binary | std::ios::trunc);
    if (!f)
        throw std::runtime_error("RasterCache: cannot open " + dest_path.string());
    f.write(data, static_cast<std::streamsize>(size));
}

void RasterCache::write_layer(const RasterCacheKey &key, size_t lid,
                              const std::string    &rle_bytes)
{
    write_layer(key, lid, rle_bytes.data(), rle_bytes.size());
}

void RasterCache::mark_complete(const RasterCacheKey &key)
{
    // Write a zero-byte sentinel after all layers are fully written.
    // is_valid() checks for this file, not layer_0000.png, so a partial write
    // (process killed mid-rasterize) never looks like a valid cache.
    fs::path sentinel = key.dir / "cache_complete";
    std::ofstream f(sentinel.string(), std::ios::binary | std::ios::trunc);
    if (!f)
        BOOST_LOG_TRIVIAL(warning)
            << "RasterCache: could not write sentinel " << sentinel.string();
}

std::string RasterCache::read_layer(const RasterCacheKey &key, size_t lid)
{
    std::ostringstream name;
    name << "layer_" << std::setw(4) << std::setfill('0') << lid << ".rle";
    fs::path p = key.dir / name.str();

    std::ifstream f(p.string(), std::ios::binary);
    if (!f)
        throw std::runtime_error("RasterCache: cannot read " + p.string());

    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

bool RasterCache::is_valid(const RasterCacheKey &key)
{
    // Check for the sentinel written by mark_complete(), not layer_0000.png.
    // This prevents a process-killed partial cache from appearing valid.
    try {
        return fs::exists(key.dir / "cache_complete");
    } catch (...) {
        return false;
    }
}

void RasterCache::cleanup_old(int max_age_days)
{
    try {
        fs::path root = base_dir();
        if (!fs::exists(root)) return;

        const auto now = std::time(nullptr);
        const auto max_age_sec = static_cast<std::time_t>(max_age_days * 86400);

        for (const auto &entry : fs::directory_iterator(root)) {
            if (!fs::is_directory(entry.status())) continue;
            try {
                auto mtime = fs::last_write_time(entry.path());
                if (now - mtime > max_age_sec)
                    fs::remove_all(entry.path());
            } catch (...) {}
        }
    } catch (...) {}
}

} // namespace sla
} // namespace Slic3r
