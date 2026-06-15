#include "RasterCache.hpp"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <ctime>
#include <algorithm>
#include <cstring>
#include <cstdint>

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

    // Hash SLARasterParams field-by-field (NOT raw `&rp, sizeof(rp)`): the struct
    // carries padding bytes whose value is indeterminate unless every instance is
    // value-initialized, which made the old raw-byte hash non-deterministic across
    // processes (the on-disk cache is shared between runs). Hashing each scalar
    // field skips padding entirely.
    auto hash_scalar = [&crc](auto v) {
        crc = mz_crc32(crc, reinterpret_cast<const unsigned char *>(&v),
                       static_cast<mz_ulong>(sizeof(v)));
    };
    hash_scalar(rp.res.width_px);
    hash_scalar(rp.res.height_px);
    hash_scalar(rp.pxdim.w_mm);
    hash_scalar(rp.pxdim.h_mm);
    hash_scalar(rp.trafo.mirror_x);
    hash_scalar(rp.trafo.mirror_y);
    hash_scalar(rp.trafo.flipXY);
    hash_scalar(rp.gamma);
    hash_scalar(rp.aa_steps);
    hash_scalar(rp.gray_lo);
    hash_scalar(rp.gray_hi);
    hash_scalar(rp.blur_pixel);
    hash_scalar(rp.bottom_layer_count);
    hash_scalar(static_cast<coord_t>(rp.shift.x()));
    hash_scalar(static_cast<coord_t>(rp.shift.y()));
    hash_scalar(rp.picture_grayscale);

    // Hash all layer ExPolygons point data.
    // Dual-track (change: prz-support-binary-output): hash the model-track and
    // support-track point sets separately rather than the merged union, so a
    // model<->support reclassification (which changes the rendered output but
    // can leave the union geometry identical) yields a different key. A 1-byte
    // track marker between the two sets prevents boundary ambiguity.
    auto hash_expolygons = [&crc](const ExPolygons &slices) {
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
    };
    static constexpr unsigned char MODEL_MARK   = 0x4D; // 'M' — model track
    static constexpr unsigned char SUPPORT_MARK = 0x53; // 'S' — support track
    for (const SLAPrint::PrintLayer &layer : printer_input) {
        crc = mz_crc32(crc, &MODEL_MARK, 1);
        hash_expolygons(layer.transformed_model_slices());

        crc = mz_crc32(crc, &SUPPORT_MARK, 1);
        hash_expolygons(layer.transformed_support_slices());
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

void RasterCache::write_thumb(const RasterCacheKey &key, size_t lid,
                              const unsigned char *png, size_t size)
{
    // Direct write — caller must call ensure_dir() first; no temp+rename.
    // On failure we throw: the exception must propagate out of the parallel
    // rasterize loop so mark_complete() is never called and the cache is judged
    // invalid (liveness binding — RLE and thumb live or die together).
    std::ostringstream name;
    name << "layer_" << std::setw(4) << std::setfill('0') << lid << "_preview.rle";
    fs::path dest_path = key.dir / name.str();

    std::ofstream f(dest_path.string(), std::ios::binary | std::ios::trunc);
    if (!f)
        throw std::runtime_error("RasterCache: cannot open " + dest_path.string());
    f.write(reinterpret_cast<const char *>(png),
            static_cast<std::streamsize>(size));
    if (!f)
        throw std::runtime_error("RasterCache: write failed " + dest_path.string());
}

void RasterCache::write_thumb(const RasterCacheKey             &key, size_t lid,
                              const std::vector<unsigned char> &png_bytes)
{
    write_thumb(key, lid, png_bytes.data(), png_bytes.size());
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

std::vector<unsigned char> RasterCache::read_thumb(const RasterCacheKey &key,
                                                   size_t                lid)
{
    std::ostringstream name;
    name << "layer_" << std::setw(4) << std::setfill('0') << lid << "_preview.rle";
    fs::path p = key.dir / name.str();

    std::ifstream f(p.string(), std::ios::binary);
    if (!f)
        return {};  // missing/unreadable → empty; GUI falls back to vector preview

    return std::vector<unsigned char>(std::istreambuf_iterator<char>(f),
                                      std::istreambuf_iterator<char>());
}

void RasterCache::rle_encode_gray(const unsigned char *data, int w, int h,
                                  std::vector<unsigned char> &out)
{
    out.clear();  // keep capacity → no realloc after the first layer on a thread
    if (data == nullptr || w <= 0 || h <= 0)
        return;

    const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);

    auto put32 = [&out](uint32_t v) {
        out.push_back(static_cast<unsigned char>( v        & 0xff));
        out.push_back(static_cast<unsigned char>((v >> 8)  & 0xff));
        out.push_back(static_cast<unsigned char>((v >> 16) & 0xff));
        out.push_back(static_cast<unsigned char>((v >> 24) & 0xff));
    };
    put32(static_cast<uint32_t>(w));
    put32(static_cast<uint32_t>(h));

    size_t i = 0;
    while (i < n) {
        const unsigned char v = data[i];
        size_t run = 1;
        while (i + run < n && data[i + run] == v && run < 255)
            ++run;
        out.push_back(v);
        out.push_back(static_cast<unsigned char>(run));
        i += run;
    }
}

bool RasterCache::rle_decode_gray(const std::vector<unsigned char> &in,
                                  std::vector<unsigned char> &out,
                                  int &w, int &h)
{
    out.clear();
    w = 0;
    h = 0;
    if (in.size() < 8)
        return false;

    auto get32 = [&in](size_t off) -> uint32_t {
        return  static_cast<uint32_t>(in[off])             |
               (static_cast<uint32_t>(in[off + 1]) << 8)   |
               (static_cast<uint32_t>(in[off + 2]) << 16)  |
               (static_cast<uint32_t>(in[off + 3]) << 24);
    };
    const uint32_t ww = get32(0);
    const uint32_t hh = get32(4);
    const size_t   n  = static_cast<size_t>(ww) * static_cast<size_t>(hh);
    if (n == 0)
        return false;

    out.resize(n);
    size_t pos = 8, o = 0;
    while (pos + 1 < in.size() && o < n) {
        const unsigned char v   = in[pos++];
        const unsigned char run = in[pos++];
        if (run == 0)
            return false;
        const size_t cnt = std::min<size_t>(run, n - o);
        std::memset(out.data() + o, v, cnt);
        o += cnt;
    }
    if (o != n)
        return false;

    w = static_cast<int>(ww);
    h = static_cast<int>(hh);
    return true;
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
