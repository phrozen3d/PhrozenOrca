#include <catch2/catch.hpp>

#include <sstream>
#include <cereal/archives/binary.hpp>
#include <cereal/types/vector.hpp>

#include "libslic3r/SLA/Hollowing.hpp"

using namespace Slic3r;
using namespace Catch::Matchers;

// ---------------------------------------------------------------------------
// Layer 1: SLA data serialization tests
// These verify that sla_drain_holes data captured by UndoRedo::Stack
// snapshots survives a cereal round-trip correctly.
// Regression baseline: if anyone breaks DrainHole serialization, these fail.
// ---------------------------------------------------------------------------

TEST_CASE("sla::DrainHole single hole survives cereal round-trip", "[SLA][UndoRedo][L1]")
{
    sla::DrainHole original{
        Vec3f{1.f, 2.f, 3.f},   // pos
        Vec3f{0.f, 0.f, -1.f},  // normal
        1.5f,                    // radius
        8.0f                     // height
    };

    std::ostringstream oss;
    {
        cereal::BinaryOutputArchive ar(oss);
        ar(original);
    }

    sla::DrainHole restored;
    std::istringstream iss(oss.str());
    {
        cereal::BinaryInputArchive ar(iss);
        ar(restored);
    }

    REQUIRE_THAT(restored.radius,   WithinAbs(1.5f, 1e-6f));
    REQUIRE_THAT(restored.height,   WithinAbs(8.0f, 1e-6f));
    REQUIRE_THAT(restored.pos.x(),  WithinAbs(1.f,  1e-6f));
    REQUIRE_THAT(restored.pos.y(),  WithinAbs(2.f,  1e-6f));
    REQUIRE_THAT(restored.pos.z(),  WithinAbs(3.f,  1e-6f));
    REQUIRE_THAT(restored.normal.z(), WithinAbs(-1.f, 1e-6f));
    REQUIRE(restored.failed == false);
}

TEST_CASE("sla::DrainHoles vector preserves count and order after round-trip", "[SLA][UndoRedo][L1]")
{
    sla::DrainHoles original;
    original.emplace_back(Vec3f{0.f, 0.f, 0.f}, Vec3f{0.f, 0.f, -1.f}, 1.0f, 5.0f);
    original.emplace_back(Vec3f{5.f, 0.f, 0.f}, Vec3f{0.f, 0.f, -1.f}, 2.0f, 6.0f);
    original.emplace_back(Vec3f{0.f, 5.f, 0.f}, Vec3f{0.f, 0.f, -1.f}, 3.0f, 7.0f);

    std::ostringstream oss;
    {
        cereal::BinaryOutputArchive ar(oss);
        ar(original);
    }

    sla::DrainHoles restored;
    std::istringstream iss(oss.str());
    {
        cereal::BinaryInputArchive ar(iss);
        ar(restored);
    }

    REQUIRE(restored.size() == 3);
    REQUIRE_THAT(restored[0].radius, WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(restored[1].radius, WithinAbs(2.0f, 1e-6f));
    REQUIRE_THAT(restored[2].radius, WithinAbs(3.0f, 1e-6f));
    REQUIRE_THAT(restored[0].height, WithinAbs(5.0f, 1e-6f));
    REQUIRE_THAT(restored[1].height, WithinAbs(6.0f, 1e-6f));
    REQUIRE_THAT(restored[2].height, WithinAbs(7.0f, 1e-6f));
    REQUIRE_THAT(restored[2].pos.y(), WithinAbs(5.f, 1e-6f));
}

TEST_CASE("sla::DrainHoles round-trip preserves empty vector", "[SLA][UndoRedo][L1]")
{
    sla::DrainHoles original;

    std::ostringstream oss;
    { cereal::BinaryOutputArchive ar(oss); ar(original); }

    sla::DrainHoles restored;
    std::istringstream iss(oss.str());
    { cereal::BinaryInputArchive ar(iss); ar(restored); }

    REQUIRE(restored.empty());
}
