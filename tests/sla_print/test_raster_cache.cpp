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

#include <boost/filesystem.hpp>

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