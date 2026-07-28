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

// ---------------------------------------------------------------------------
// GLGizmoHollow::on_save/on_load serialize exactly this tuple of primitives
// (m_pending_offset, m_pending_quality, m_pending_closing_d, m_enable_hollowing).
// GLGizmoHollow itself lives in slic3r/GUI (wx-dependent, not linkable here), so
// this test pins the cereal round-trip fidelity of that exact primitive tuple shape
// directly: if anyone changes the field order/types in on_save/on_load without a
// matching change on both sides, a real (non-libslic3r-visible) bug would result,
// but this at least guards the serialization mechanics those calls rely on.
// ---------------------------------------------------------------------------

TEST_CASE("Hollow pending-params primitive tuple survives cereal round-trip", "[SLA][UndoRedo][L1]")
{
    float original_offset    = 3.25f;
    float original_quality   = 0.75f;
    float original_closing_d = 1.5f;
    bool  original_enable    = true;

    std::ostringstream oss;
    {
        cereal::BinaryOutputArchive ar(oss);
        ar(original_offset, original_quality, original_closing_d, original_enable);
    }

    float restored_offset = 0.f, restored_quality = 0.f, restored_closing_d = 0.f;
    bool restored_enable = false;
    std::istringstream iss(oss.str());
    {
        cereal::BinaryInputArchive ar(iss);
        ar(restored_offset, restored_quality, restored_closing_d, restored_enable);
    }

    REQUIRE_THAT(restored_offset,    WithinAbs(3.25f, 1e-6f));
    REQUIRE_THAT(restored_quality,   WithinAbs(0.75f, 1e-6f));
    REQUIRE_THAT(restored_closing_d, WithinAbs(1.5f,  1e-6f));
    REQUIRE(restored_enable == true);
}

TEST_CASE("Hollow pending-params tuple round-trip preserves disabled/zero state", "[SLA][UndoRedo][L1]")
{
    float original_offset = 0.f, original_quality = 0.f, original_closing_d = 0.f;
    bool original_enable = false;

    std::ostringstream oss;
    {
        cereal::BinaryOutputArchive ar(oss);
        ar(original_offset, original_quality, original_closing_d, original_enable);
    }

    float restored_offset = 9.f, restored_quality = 9.f, restored_closing_d = 9.f;
    bool restored_enable = true;
    std::istringstream iss(oss.str());
    {
        cereal::BinaryInputArchive ar(iss);
        ar(restored_offset, restored_quality, restored_closing_d, restored_enable);
    }

    REQUIRE_THAT(restored_offset,    WithinAbs(0.f, 1e-6f));
    REQUIRE_THAT(restored_quality,   WithinAbs(0.f, 1e-6f));
    REQUIRE_THAT(restored_closing_d, WithinAbs(0.f, 1e-6f));
    REQUIRE(restored_enable == false);
}
