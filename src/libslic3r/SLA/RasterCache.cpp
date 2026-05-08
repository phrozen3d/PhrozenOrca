#include "RasterCache.hpp"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <cstring>
#include <ctime>

#include <miniz.h>
#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>

namespace Slic3r {
namespace sla {

namespace fs = boost::filesystem;

// Task 3.1
boost::filesystem::path RasterCache::base_dir()
{
    return fs::temp_directory_path() / "phrozen_sla_cache";
}

// Task 3.2
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
            // Contour
            const Points &pts = ep.contour.points;
            if (!pts.empty())
                crc = mz_crc32(crc,
                    reinterpret_cast<const unsigned char *>(pts.data()),
                    static_cast<mz_ulong>(pts.size() * sizeof(Point)));
            // Holes
            for (const Polygon &hole : ep.holes) {
                const Points &hpts = hole.points;
                if (!hpts.empty())
                    crc = mz_crc32(crc,
                        reinterpret_cast<const unsigned char *>(hpts.data()),
                        static_cast<mz_ulong>(hpts.size() * sizeof(Point)));
            }
        }
    }

    // Hash CACHE_VERSION to invalidate on code changes
    crc = mz_crc32(crc,
        reinterpret_cast<const unsigned char *>(&CACHE_VERSION),
        sizeof(CACHE_VERSION));

    std::ostringstream oss;
    oss << std::hex << std::setw(8) << std::setfill('0') << static_cast<uint32_t>(crc);
    std::string hash = oss.str();

    return {hash, base_dir() / hash};
}

// Task 3.3
void RasterCache::write_layer(const RasterCacheKey &key, size_t lid,
                              const std::string    &prz_rle_bytes)
{
    fs::create_directories(key.dir);

    std::ostringstream name;
    name << "layer_" << std::setw(4) << std::setfill('0') << lid << ".prz_rle";

    fs::path tmp_path  = key.dir / (name.str() + ".tmp");
    fs::path dest_path = key.dir / name.str();

    {
        std::ofstream f(tmp_path.string(), std::ios::binary | std::ios::trunc);
        if (!f)
            throw std::runtime_error("RasterCache: cannot open " + tmp_path.string());
        f.write(prz_rle_bytes.data(), static_cast<std::streamsize>(prz_rle_bytes.size()));
    }
    fs::rename(tmp_path, dest_path);
}

// Task 3.4
void RasterCache::finalize(const RasterCacheKey  &key, size_t layer_count,
                           const SLARasterParams & /*rp*/)
{
    fs::path manifest = key.dir / "manifest.txt";
    std::ofstream f(manifest.string(), std::ios::trunc);
    if (!f)
        throw std::runtime_error("RasterCache: cannot write manifest");
    f << "version=" << CACHE_VERSION << "\n";
    f << "layer_count=" << layer_count << "\n";
}

// Task 3.5
bool RasterCache::is_valid(const RasterCacheKey &key, size_t expected_layers)
{
    try {
        fs::path manifest = key.dir / "manifest.txt";
        if (!fs::exists(manifest))
            return false;

        std::ifstream f(manifest.string());
        if (!f) return false;

        size_t stored_count = 0;
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("layer_count=", 0) == 0)
                stored_count = static_cast<size_t>(std::stoul(line.substr(12)));
        }
        if (stored_count != expected_layers)
            return false;

        for (size_t lid = 0; lid < expected_layers; ++lid) {
            std::ostringstream name;
            name << "layer_" << std::setw(4) << std::setfill('0') << lid << ".prz_rle";
            fs::path p = key.dir / name.str();
            if (!fs::exists(p) || fs::file_size(p) == 0)
                return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

// Task 3.6
std::string RasterCache::read_layer(const RasterCacheKey &key, size_t lid)
{
    std::ostringstream name;
    name << "layer_" << std::setw(4) << std::setfill('0') << lid << ".prz_rle";
    fs::path p = key.dir / name.str();

    std::ifstream f(p.string(), std::ios::binary);
    if (!f)
        throw std::runtime_error("RasterCache: cannot read " + p.string());

    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

// Task 3.7
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
            } catch (...) {
                // Skip entries we cannot read or delete
            }
        }
    } catch (...) {
        // Silently ignore all errors from cleanup
    }
}

} // namespace sla
} // namespace Slic3r