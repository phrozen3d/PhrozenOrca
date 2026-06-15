// Unit tests for the preview-thumbnail API added to RasterCache
// (change: sla-preview-thumb-cache, task 1.6).
//
// Covers the three contractual paths:
//   1. Normal      — write_thumb then read_thumb returns identical bytes.
//   2. Empty       — read_thumb of a missing lid returns an empty vector
//                    (so the GUI can fall back to vector preview, never AGG).
//   3. Exceptional — write_thumb into an invalid/absent directory throws
//                    std::runtime_error (liveness binding: the exception must
//                    propagate so mark_complete() is skipped).

#include <catch2/catch.hpp>

#include "libslic3r/SLA/RasterCache.hpp"
#include "libslic3r/SLAPrint.hpp"

#include <boost/filesystem.hpp>

#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Slic3r;
using namespace Slic3r::sla;

namespace fs = boost::filesystem;

TEST_CASE("RasterCache thumb round-trip and empty paths", "[SLA][RasterCache]")
{
    RasterCacheKey key{
        "deadbeef",
        fs::temp_directory_path() / fs::unique_path("phrozen_thumb_test_%%%%%%%%")};
    RasterCache::ensure_dir(key);

    SECTION("normal path: read_thumb returns the exact bytes written")
    {
        const std::vector<unsigned char> png = {
            0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a,
            0x00, 0x01, 0x7f, 0x80, 0xfe, 0xff, 0x10, 0x42};

        REQUIRE_NOTHROW(RasterCache::write_thumb(key, 7, png));

        const std::vector<unsigned char> got = RasterCache::read_thumb(key, 7);
        REQUIRE(got == png);
    }

    SECTION("empty path: read_thumb of a missing lid returns an empty vector")
    {
        const std::vector<unsigned char> got = RasterCache::read_thumb(key, 999);
        REQUIRE(got.empty());
    }

    fs::remove_all(key.dir);
}

// --- compute_key tests (change: prz-support-binary-output, stage 1) -----------
// These intentionally use an EMPTY printer_input so only the SLARasterParams
// contribution to the key is exercised — track-geometry hashing arrives in
// stage 2. Tagged [raster_cache] so `sla_print_tests "[raster_cache]"` selects
// them.

namespace {
// Build a fully-populated SLARasterParams with deterministic field values.
SLARasterParams make_rp()
{
    SLARasterParams rp{};
    rp.res                = sla::Resolution{13320, 5120};
    rp.pxdim              = sla::PixelDim{0.0123, 0.0123};
    rp.trafo              = sla::RasterBase::Trafo{sla::RasterBase::roPortrait};
    rp.gamma              = 1.0;
    rp.aa_steps           = 4;
    rp.gray_lo            = 0;
    rp.gray_hi            = 255;
    rp.blur_pixel         = 0;
    rp.bottom_layer_count = 4;
    rp.shift              = Point{12345, -6789};
    rp.picture_grayscale  = 255;
    return rp;
}
} // namespace

TEST_CASE("compute_key reacts to bottom_layer_count", "[SLA][RasterCache][raster_cache]")
{
    const std::vector<SLAPrint::PrintLayer> no_layers;

    SLARasterParams a = make_rp();
    SLARasterParams b = make_rp();
    b.bottom_layer_count = a.bottom_layer_count + 1; // the only difference

    const RasterCacheKey ka = RasterCache::compute_key(a, no_layers);
    const RasterCacheKey kb = RasterCache::compute_key(b, no_layers);

    REQUIRE(ka.hash != kb.hash);
}

TEST_CASE("compute_key is deterministic regardless of struct padding", "[SLA][RasterCache][raster_cache]")
{
    const std::vector<SLAPrint::PrintLayer> no_layers;

    // Reference: value-initialized (padding zeroed).
    SLARasterParams clean = make_rp();

    // Adversarial: scribble 0xAB over the whole struct first (including padding
    // bytes), THEN assign identical field values. With field-wise hashing the
    // padding noise must not leak into the key; with the old raw-byte hash it
    // would have produced a different key.
    SLARasterParams dirty;
    std::memset(&dirty, 0xAB, sizeof(dirty));
    dirty.res                = clean.res;
    dirty.pxdim              = clean.pxdim;
    dirty.trafo              = clean.trafo;
    dirty.gamma              = clean.gamma;
    dirty.aa_steps           = clean.aa_steps;
    dirty.gray_lo            = clean.gray_lo;
    dirty.gray_hi            = clean.gray_hi;
    dirty.blur_pixel         = clean.blur_pixel;
    dirty.bottom_layer_count = clean.bottom_layer_count;
    dirty.shift              = clean.shift;
    dirty.picture_grayscale  = clean.picture_grayscale;

    const RasterCacheKey kc = RasterCache::compute_key(clean, no_layers);
    const RasterCacheKey kd = RasterCache::compute_key(dirty, no_layers);

    REQUIRE(kc.hash == kd.hash);
}

TEST_CASE("compute_key reacts to picture_grayscale", "[SLA][RasterCache][raster_cache]")
{
    const std::vector<SLAPrint::PrintLayer> no_layers;

    SLARasterParams a = make_rp();
    SLARasterParams b = make_rp();
    b.picture_grayscale = 200; // differs from a (255)

    REQUIRE(RasterCache::compute_key(a, no_layers).hash !=
            RasterCache::compute_key(b, no_layers).hash);
}

TEST_CASE("RasterCache write_thumb into an invalid directory throws", "[SLA][RasterCache]")
{
    // Directory deliberately NOT created (ensure_dir not called) and nested under
    // a non-existent parent → the underlying ofstream open must fail → write_thumb
    // must throw std::runtime_error.
    RasterCacheKey key{
        "nodir",
        fs::temp_directory_path() / fs::unique_path("phrozen_thumb_missing_%%%%%%%%") / "deeper"};

    const std::vector<unsigned char> png = {0x01, 0x02, 0x03};

    REQUIRE_THROWS_AS(RasterCache::write_thumb(key, 0, png), std::runtime_error);
}