// Tests for PhrozenPRZ header field mapping logic.
// Exercises pure mapping functions without requiring a full SLAPrint object.

#include <catch2/catch.hpp>

// ---------------------------------------------------------------------------
// AA Level mapping: anti_aliasing_level index → PRZ aaLevel (pixels per axis)
// ---------------------------------------------------------------------------
static short prz_aa_level(int aa_idx)
{
    switch (aa_idx) {
        case 0:  return 2;
        case 1:  return 4;
        case 2:  return 8;
        default: return 4;
    }
}

TEST_CASE("PRZ AA level mapping", "[prz_header]")
{
    CHECK(prz_aa_level(0) == 2);
    CHECK(prz_aa_level(1) == 4);
    CHECK(prz_aa_level(2) == 8);
    // unknown indices fall back to safe Mid value (4)
    CHECK(prz_aa_level(5) == 4);
    CHECK(prz_aa_level(-1) == 4);
}

// ---------------------------------------------------------------------------
// second_speed guard: speed must be 0 when distance == 0
// ---------------------------------------------------------------------------
static float guarded_second_speed(float dist, float speed)
{
    return (dist == 0.f) ? 0.f : speed;
}

TEST_CASE("PRZ second_speed distance=0 forces speed=0", "[prz_header]")
{
    // distance > 0: speed passes through
    CHECK(guarded_second_speed(5.f, 60.f) == Approx(60.f));
    CHECK(guarded_second_speed(0.1f, 30.f) == Approx(30.f));

    // distance == 0: speed forced to 0 regardless of configured value
    CHECK(guarded_second_speed(0.f, 60.f) == Approx(0.f));
    CHECK(guarded_second_speed(0.f, 0.f)  == Approx(0.f));
}
