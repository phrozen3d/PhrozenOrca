// Regression tests for the SLA support contact-spacing calibration.
// Change: sla-support-contact-spacing-4mm
//
// These lock the calibrated island sampling baseline produced by
// SampleConfigFactory::create(): at the reference 0.4 mm head and density 100%
// the thin-region contact spacing must be ~4 mm, the geometry chain must scale
// self-similarly, the physical head fields must stay unscaled, head-diameter
// coupling must be preserved, verify() must stay consistent across the head
// domain, and apply_density() semantics must be unchanged.
//
// Each TEST_CASE maps to a scenario in
// openspec/changes/sla-support-contact-spacing-4mm/specs/sla-support-contact-spacing/spec.md
//
// NOTE: per tests/CLAUDE.md, floating-point comparisons use WithinAbs/WithinRel
// matchers, never Approx. Values are compared in millimetres via unscale_() so the
// result is independent of the active SCALING_FACTOR.

#include <catch2/catch.hpp>

#include <libslic3r/libslic3r.h>
#include <libslic3r/SLA/SupportIslands/SampleConfigFactory.hpp>
#include <libslic3r/SLA/SupportIslands/SampleConfig.hpp>

using namespace Slic3r;
using namespace Catch::Matchers;

namespace {
// Contact spacing of a field, in millimetres, regardless of SCALING_FACTOR.
inline double mm(coord_t scaled) { return unscale_(static_cast<double>(scaled)); }
} // namespace

// Spec: "Baseline contact spacing calibrated at reference head"
//        scenario "Thin spacing at reference head and 100% density"
TEST_CASE("SLA contact spacing: thin spacing at reference head is ~4mm", "[sla][ContactSpacing]")
{
    const sla::SampleConfig cfg = sla::SampleConfigFactory::create(0.4f);
    REQUIRE_THAT(mm(cfg.thin_max_distance), WithinAbs(4.0, 0.01));
}

// Spec: "Self-similar scaling of the geometry chain"
//        scenarios "Inner and outline spacing scale proportionally" +
//                  "Ratios between spacing fields are preserved"
TEST_CASE("SLA contact spacing: inner and outline scale proportionally", "[sla][ContactSpacing]")
{
    const sla::SampleConfig cfg = sla::SampleConfigFactory::create(0.4f);

    // thin:inner:outline follow the create() constants 0.8 : 1.0 : 0.75 (relative to L2),
    // i.e. inner = thin/0.8, outline = inner*0.75. With thin ~4mm => inner ~5mm, outline ~3.75mm.
    REQUIRE_THAT(mm(cfg.thick_inner_max_distance),   WithinAbs(5.0,  0.01));
    REQUIRE_THAT(mm(cfg.thick_outline_max_distance), WithinAbs(3.75, 0.01));

    // Ratios are preserved regardless of the absolute calibration.
    REQUIRE_THAT(mm(cfg.thin_max_distance) / mm(cfg.thick_inner_max_distance),
                 WithinRel(0.8, 1e-3));
    REQUIRE_THAT(mm(cfg.thick_outline_max_distance) / mm(cfg.thick_inner_max_distance),
                 WithinRel(0.75, 1e-3));
}

// Spec: "Physical head fields remain unscaled"
//        scenario "Head radius unchanged by calibration"
TEST_CASE("SLA contact spacing: physical head fields are unscaled", "[sla][ContactSpacing]")
{
    // head_radius must equal the pure head geometry (d/2), independent of the
    // calibration factor; minimal_distance_from_outline mirrors head_radius.
    const sla::SampleConfig cfg04 = sla::SampleConfigFactory::create(0.4f);
    REQUIRE_THAT(mm(cfg04.head_radius), WithinAbs(0.2, 1e-6));
    REQUIRE(cfg04.minimal_distance_from_outline == cfg04.head_radius);

    const sla::SampleConfig cfg06 = sla::SampleConfigFactory::create(0.6f);
    REQUIRE_THAT(mm(cfg06.head_radius), WithinAbs(0.3, 1e-6));
    REQUIRE(cfg06.minimal_distance_from_outline == cfg06.head_radius);
}

// Spec: "Head-diameter coupling preserved"
//        scenarios "Larger head yields larger spacing" +
//                  "Smaller head yields smaller spacing"
TEST_CASE("SLA contact spacing: head-diameter coupling preserved", "[sla][ContactSpacing]")
{
    const double thin02 = mm(sla::SampleConfigFactory::create(0.2f).thin_max_distance);
    const double thin04 = mm(sla::SampleConfigFactory::create(0.4f).thin_max_distance);
    const double thin08 = mm(sla::SampleConfigFactory::create(0.8f).thin_max_distance);

    // 4 mm is anchored only at 0.4 mm; other heads scale proportionally.
    REQUIRE(thin08 > 4.0);
    REQUIRE(thin02 < 4.0);
    REQUIRE(thin02 < thin04);
    REQUIRE(thin04 < thin08);
}

// Spec: "verify() consistency across the head-diameter domain"
//        scenario "verify passes at extreme head diameters"
TEST_CASE("SLA contact spacing: verify passes across the head domain", "[sla][ContactSpacing]")
{
    for (float d : {0.2f, 0.4f, 0.8f}) {
        DYNAMIC_SECTION("head diameter " << d) {
            // create() already runs verify() internally and leaves cfg stable;
            // a fresh verify() on the result must return true (no clamp needed).
            sla::SampleConfig cfg = sla::SampleConfigFactory::create(d);
            REQUIRE(sla::SampleConfigFactory::verify(cfg));
        }
    }
}

// Spec: "Density semantics unchanged"
//        scenario "200% density halves the thin spacing"
TEST_CASE("SLA contact spacing: 200% density halves thin spacing", "[sla][ContactSpacing]")
{
    const sla::SampleConfig base = sla::SampleConfigFactory::create(0.4f);
    const sla::SampleConfig dense = sla::SampleConfigFactory::apply_density(base, 2.0f);

    // thin_max_distance is scaled linearly by density: 4mm / 2 = 2mm.
    REQUIRE_THAT(mm(dense.thin_max_distance), WithinAbs(2.0, 0.01));
}
