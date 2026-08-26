// Regression tests for thin-model SLA support point placement.
// Change: fix-sla-thin-model-support-points
//
// These lock the directional constraint added to sla::move_on_mesh_surface():
// a support point must be projected onto a *downward-facing* load-bearing face,
// not merely onto the geometrically nearest one. Without it, a support point
// sampled above a thin plate's mid-plane snaps to the plate's top surface, its
// normal becomes (0,0,+1), and the critical-angle filter in
// SLAPrint::Steps::support_points() then discards every point -- the model
// slices with zero automatic supports, and whether that happens depends only on
// the slicing-grid phase.
//
// Each TEST_CASE maps to a scenario in
// openspec/changes/fix-sla-thin-model-support-points/specs/
//   sla-support-point-placement/spec.md
//
// SCOPE: these are unit tests on the projection stage (move_on_mesh_surface)
// plus a faithful re-derivation of the slicing grid built by
// SLAPrint::Steps::slice_model(). They deliberately do NOT drive the whole SLA
// pipeline: tests/sla_print is disabled and its harness targets an API this fork
// removed (see design.md, "建置與測試環境結論"). Pipeline-level claims -- the
// end-to-end automatic point count per phase -- are covered by the manual
// acceptance run in Phase 5 instead.
//
// NOTE: per tests/CLAUDE.md, floating-point comparisons use WithinAbs/WithinRel
// matchers, never Approx, and loop bodies use DYNAMIC_SECTION so no two sections
// ever share a name.

#include <catch2/catch.hpp>

#include <cmath>
#include <limits>
#include <vector>

#include <libslic3r/libslic3r.h>
#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/SLA/IndexedMesh.hpp>
#include <libslic3r/SLA/SupportPoint.hpp>
#include <libslic3r/SLA/SupportPointGenerator.hpp>

using namespace Slic3r;
using namespace Catch::Matchers;

namespace {

// The spec's reference geometry: a thin horizontal plate whose underside is at
// z = 0.00 and whose top surface is at z = 0.20. its_make_cube() spans
// (0,0,0)..(x,y,z), so the plate lands exactly there with no extra transform.
constexpr double PLATE_XY        = 10.0;
constexpr double PLATE_THICKNESS = 0.2;
constexpr double PLATE_MID       = PLATE_THICKNESS / 2.0;

// Support head front radius carried by every point below. Only present so the
// points are well formed; the projection itself never reads it.
constexpr float HEAD_FRONT_RADIUS = 0.2f;

// SLAPrintSteps.cpp uses support_head_front_diameter as the allowed_move
// fallback when the model spans a single slice level.
constexpr double HEAD_FRONT_DIAMETER = 0.4;

// Tolerance for comparing millimetre positions that should land exactly on a
// modelled face. move_on_mesh_surface() accumulates one float add, so single
// precision is the limit here.
constexpr double POS_TOL = 1e-5;

// Sampling levels closer than this to the underside are skipped: the probe ray
// would then start on the face it is meant to hit, which is a ray-origin
// degeneracy of the test rather than of the code under test. Such a point is
// already on the load-bearing surface and has nothing left to project.
constexpr double ON_SURFACE_TOL = 1e-6;

// The coordinate quantum of scaled()/unscaled(): the slice grid is built on
// coord_t, so no two distinct grid levels can differ by less than this. Used as
// the slack when comparing a float grid level against a double setting value.
constexpr double GRID_TOL = 1e-6;

indexed_triangle_set make_thin_plate(double thickness = PLATE_THICKNESS)
{
    return its_make_cube(PLATE_XY, PLATE_XY, thickness);
}

// Same plate, but with the bottom face wound the wrong way round so its
// geometric normal points *up*. This is the only way to reach the third tier of
// the decision ("neither hit faces downward") with axis-aligned geometry: with
// correct winding, a downward ray from inside the material always lands on a
// genuinely downward-facing face. The spec names exactly this class of input --
// "vertical walls or degenerate geometry".
indexed_triangle_set make_plate_with_upward_bottom(double thickness = PLATE_THICKNESS)
{
    indexed_triangle_set its = make_thin_plate(thickness);
    // its_make_cube() emits the bottom face as triangles 0 and 1.
    std::swap(its.indices[0](1), its.indices[0](2));
    std::swap(its.indices[1](1), its.indices[1](2));
    return its;
}

sla::LayerSupportPoints points_at_z(float z, size_t count = 1)
{
    sla::LayerSupportPoints pts;
    pts.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        // Spread the points across the plate's interior so none of them sits on
        // an edge where a vertical ray could graze a side wall.
        const float x = float(2.0 + double(i) * 1.5);
        sla::LayerSupportPoint p;
        static_cast<sla::SupportPoint &>(p) =
            sla::SupportPoint(Vec3f(x, 5.f, z), HEAD_FRONT_RADIUS,
                              sla::SupportPointType::island);
        pts.push_back(p);
    }
    return pts;
}

// Faithful re-derivation of the slice grid built by SLAPrint::Steps::slice_model().
// Returns SLAPrintObject::m_model_height_levels for the given phase: the slice
// levels from the record closest to the model's underside to the end of the
// grid. Empty means this phase produces no slice level at all, which
// slice_model() aborts on before support points are ever generated -- the spec
// excludes such phases from the consistency requirement.
std::vector<float> model_height_levels(double model_min_z,
                                       double model_max_z,
                                       double elevation,
                                       double layer_height,
                                       double initial_layer_height)
{
    const coord_t lhs    = scaled(layer_height);
    const coord_t ilhs   = scaled(initial_layer_height);
    const double  min_z  = model_min_z - elevation;
    const coord_t min_zs = scaled(min_z);
    const coord_t max_zs = scaled(model_max_z);

    std::vector<float> all_levels;
    all_levels.push_back(float(min_z) + float(initial_layer_height) / 2.f);
    for (coord_t h = min_zs + ilhs + lhs; h <= max_zs; h += lhs)
        all_levels.push_back(unscaled<float>(h) - float(layer_height) / 2.f);

    // closest_slice_record(bb.min(Z)) picks the record whose slice level is
    // *closest* to the model's underside -- which may be the one just below it.
    size_t start = all_levels.size();
    for (size_t i = 0; i < all_levels.size(); ++i) {
        if (double(all_levels[i]) >= model_min_z) { start = i; break; }
    }
    if (start == all_levels.size()) return {};
    if (start > 0) {
        const double diff      = std::abs(double(all_levels[start]) - model_min_z);
        const double diff_prev = std::abs(double(all_levels[start - 1]) - model_min_z);
        if (diff_prev < diff) --start;
    }

    return std::vector<float>(all_levels.begin() + long(start), all_levels.end());
}

// The level the island sampler actually places points on: the first grid level
// that lies inside the model and is not already sitting on its underside.
bool sampling_level(const std::vector<float> &levels,
                    double model_min_z,
                    double model_max_z,
                    float &out)
{
    for (float lvl : levels) {
        const double z = double(lvl);
        if (z > model_min_z + ON_SURFACE_TOL && z <= model_max_z) {
            out = lvl;
            return true;
        }
    }
    return false;
}

// SLAPrintSteps.cpp:
//   allowed_move = levels.size() > 1 ? (levels[1] - levels[0]) + float_eps
//                                    : support_head_front_diameter
double allowed_move_for(const std::vector<float> &levels)
{
    return levels.size() > 1
               ? double(levels[1] - levels[0]) +
                     double(std::numeric_limits<float>::epsilon())
               : HEAD_FRONT_DIAMETER;
}

// One entry per (layer height, elevation) phase that actually yields a usable
// sampling level. Enumerated up front, outside any SECTION, so the section
// bodies below stay free of control flow.
struct Phase {
    double layer_height;
    double elevation;
    float  sample_z;
    double allowed_move;
};

std::vector<Phase> enumerate_phases(const std::vector<double> &layer_heights,
                                    double initial_layer_height)
{
    std::vector<Phase> phases;
    for (double lh : layer_heights) {
        for (int step = 0; step <= 15; ++step) {
            const double elevation = 5.00 + 0.01 * double(step);
            const std::vector<float> levels = model_height_levels(
                0.0, PLATE_THICKNESS, elevation, lh, initial_layer_height);

            float sample_z = 0.f;
            if (!sampling_level(levels, 0.0, PLATE_THICKNESS, sample_z))
                continue; // no valid slice level for this phase; spec excludes it

            phases.push_back(Phase{lh, elevation, sample_z,
                                   allowed_move_for(levels)});
        }
    }
    return phases;
}

} // namespace

// Spec: "支撐點必須投影至朝下的承載面"
//        scenario "取樣層落在薄板中面之上"
//
// This is the defect itself: the top surface is nearer (0.075 < 0.125) yet the
// point must still land on the underside.
TEST_CASE("SLA thin model: sampling layer above mid-plane projects to the underside",
          "[sla][ThinModel]")
{
    const indexed_triangle_set its = make_thin_plate();
    const sla::IndexedMesh     mesh{its};

    const float sample_z = 0.125f;
    REQUIRE(double(sample_z) > PLATE_MID);

    const sla::SupportPoints out =
        sla::move_on_mesh_surface(points_at_z(sample_z), mesh, 0.4);

    REQUIRE(out.size() == 1);
    const double z = double(out.front().pos.z());
    CHECK_THAT(z, WithinAbs(0.0, POS_TOL));
    // Explicitly not the top surface, even though that was the closer face.
    CHECK(std::abs(z - PLATE_THICKNESS) > POS_TOL);
}

// Spec: "支撐點必須投影至朝下的承載面"
//        scenario "取樣層落在薄板中面之下"
//
// Here the underside is already the nearer face, so this locks that the change
// did not disturb the case that used to work.
TEST_CASE("SLA thin model: sampling layer below mid-plane projects to the underside",
          "[sla][ThinModel]")
{
    const indexed_triangle_set its = make_thin_plate();
    const sla::IndexedMesh     mesh{its};

    const float sample_z = 0.075f;
    REQUIRE(double(sample_z) < PLATE_MID);

    const sla::SupportPoints out =
        sla::move_on_mesh_surface(points_at_z(sample_z), mesh, 0.4);

    REQUIRE(out.size() == 1);
    CHECK_THAT(double(out.front().pos.z()), WithinAbs(0.0, POS_TOL));
}

// Spec: "支撐點必須投影至朝下的承載面"
//        scenario "兩個方向皆為朝下面時取較近者" (tier 1 vs. the old distance rule)
//
// A point in the gap between two stacked plates: the upward ray hits the upper
// plate's underside (downward-facing), the downward ray hits the lower plate's
// top face (upward-facing). Tier 1 must pick the downward-facing hit even
// though it is the *farther* of the two -- which is precisely what the old
// distance-only rule got wrong.
TEST_CASE("SLA thin model: the downward-facing hit wins over the nearer one",
          "[sla][ThinModel]")
{
    // Lower plate 0.0..0.2, upper plate 1.0..1.2.
    indexed_triangle_set its   = make_thin_plate();
    indexed_triangle_set upper = make_thin_plate();
    for (auto &v : upper.vertices) v.z() += 1.f;
    its_merge(its, upper);

    const sla::IndexedMesh mesh{its};

    // 0.5 is 0.3 above the lower plate's top face and 0.5 below the upper
    // plate's underside, so the downward-facing face is the far one.
    const Vec3d probe{2.0, 5.0, 0.5};
    const sla::IndexedMesh::hit_result up_hit =
        mesh.query_ray_hit(probe, Vec3d(0., 0., 1.));
    const sla::IndexedMesh::hit_result down_hit =
        mesh.query_ray_hit(probe, Vec3d(0., 0., -1.));

    // Establish the premise rather than assuming it.
    REQUIRE(up_hit.is_hit());
    REQUIRE(down_hit.is_hit());
    REQUIRE(up_hit.normal().z() < 0.);       // upper plate underside
    REQUIRE(down_hit.normal().z() >= 0.);    // lower plate top face
    REQUIRE(down_hit.distance() < up_hit.distance());

    const sla::SupportPoints out =
        sla::move_on_mesh_surface(points_at_z(0.5f), mesh, 1.0);

    REQUIRE(out.size() == 1);
    CHECK_THAT(double(out.front().pos.z()), WithinAbs(1.0, POS_TOL));
}

// Spec: "支撐點必須投影至朝下的承載面"
//        scenario "兩個方向皆非朝下面時回退既有行為"
//
// Degenerate geometry: the plate's bottom face is wound so its geometric normal
// points up. Neither ray then finds a downward-facing face, and the result must
// be identical to the pre-change "take the nearer hit" rule.
TEST_CASE("SLA thin model: neither face downward falls back to the nearest hit",
          "[sla][ThinModel]")
{
    const indexed_triangle_set its = make_plate_with_upward_bottom();
    const sla::IndexedMesh     mesh{its};

    const float sample_z = 0.125f;
    const Vec3d probe{2.0, 5.0, double(sample_z)};

    const sla::IndexedMesh::hit_result up_hit =
        mesh.query_ray_hit(probe, Vec3d(0., 0., 1.));
    const sla::IndexedMesh::hit_result down_hit =
        mesh.query_ray_hit(probe, Vec3d(0., 0., -1.));

    // Establish the premise: both hits report a non-downward face normal.
    REQUIRE(up_hit.is_hit());
    REQUIRE(down_hit.is_hit());
    REQUIRE(up_hit.normal().z() >= 0.);
    REQUIRE(down_hit.normal().z() >= 0.);

    // Pre-change rule: whichever hit is nearer.
    const double nearest_z =
        up_hit.distance() < down_hit.distance() ? PLATE_THICKNESS : 0.0;

    const sla::SupportPoints out =
        sla::move_on_mesh_surface(points_at_z(sample_z), mesh, 0.4);

    REQUIRE(out.size() == 1);
    CHECK_THAT(double(out.front().pos.z()), WithinAbs(nearest_z, POS_TOL));
}

// Spec: "投影位移上限維持現行取值"
//        scenario "射線分支必須在薄板全相位下可達"
//
// Verification assumptions A1 and A2 from design.md. A1: the sampling level
// sits strictly less than one layer height above the model's underside, so the
// chosen hit distance never exceeds allowed_move and the unconstrained
// squared_distance fallback stays unreachable. A2: keeping
// support_head_front_diameter as the single-level fallback is at least as
// generous as upstream's layer_height.
TEST_CASE("SLA thin model: ray branch stays reachable across every grid phase",
          "[sla][ThinModel]")
{
    const indexed_triangle_set its = make_thin_plate();
    const sla::IndexedMesh     mesh{its};

    const std::vector<double> layer_heights{0.05, 0.10, 0.15};
    const std::vector<Phase>  phases = enumerate_phases(layer_heights, 0.05);

    // A vacuous sweep would silently pass, so require it to have found work.
    REQUIRE(phases.size() >= layer_heights.size());

    for (const Phase &ph : phases) {
        DYNAMIC_SECTION("lh=" << ph.layer_height
                              << " elevation=" << ph.elevation) {
            // A1: the sampling level sits at most one layer height above the
            // model's underside. The bound is CLOSED, not open: when a grid
            // level lands exactly on the underside that level is degenerate
            // (the slice plane is coplanar with the bottom face and yields no
            // island), so sampling falls to the next level, exactly one layer
            // height up. That happens at lh=0.10 with elevation 5.00 and 5.10.
            // design.md's first derivation said "strictly less" and was wrong
            // here; see the corrected derivation under assumption A1.
            //
            // Compared with GRID_TOL slack because the grid levels are floats:
            // the sampling height in that worst case is 0.1f, which sits about
            // 9e-9 mm *above* the double 0.1. GRID_TOL is the coordinate
            // quantum of scaled(), so the slack stays below the resolution the
            // grid itself is built at.
            CHECK(double(ph.sample_z) <= ph.layer_height + GRID_TOL);

            // A2: the bound actually in use is never smaller than one layer.
            CHECK(ph.allowed_move >= ph.layer_height);

            // The inequality the whole assumption really rests on, asserted
            // strictly. allowed_move is defined as (levels[1] - levels[0]) +
            // FLT_EPSILON, so even in the worst case above it stays one
            // FLT_EPSILON clear of the sampling height -- by construction, not
            // by luck. Dropping that epsilon would make the fallback branch
            // reachable for thin plates at those phases.
            CHECK(ph.allowed_move > double(ph.sample_z));

            // The downward-facing hit is the plate's underside; its distance is
            // the value compared against allowed_move inside
            // move_on_mesh_surface(). Staying at or below it is exactly the
            // condition for taking the ray branch rather than the
            // squared_distance fallback.
            const Vec3d probe{2.0, 5.0, double(ph.sample_z)};
            const sla::IndexedMesh::hit_result down_hit =
                mesh.query_ray_hit(probe, Vec3d(0., 0., -1.));
            REQUIRE(down_hit.is_hit());
            REQUIRE(down_hit.normal().z() < 0.);
            CHECK(down_hit.distance() <= ph.allowed_move);

            // And the projection really does land on the underside.
            const sla::SupportPoints out = sla::move_on_mesh_surface(
                points_at_z(ph.sample_z), mesh, ph.allowed_move);
            REQUIRE(out.size() == 1);
            CHECK_THAT(double(out.front().pos.z()), WithinAbs(0.0, POS_TOL));
        }
    }
}

// Spec: "支撐點產出必須與切片網格相位無關"
//        scenarios "elevation 全相位掃描結果一致" + "多種層高下皆產出支撐點"
//
// Scoped to the projection stage: for every phase that has a valid slice level,
// the input set must survive intact and every point must end up on the
// underside, regardless of layer height or elevation phase.
TEST_CASE("SLA thin model: projection result is independent of the grid phase",
          "[sla][ThinModel]")
{
    const indexed_triangle_set its = make_thin_plate();
    const sla::IndexedMesh     mesh{its};

    const size_t              point_count = 4;
    const std::vector<double> layer_heights{0.05, 0.10, 0.15};
    const std::vector<Phase>  phases = enumerate_phases(layer_heights, 0.05);

    REQUIRE(phases.size() >= layer_heights.size());

    // Every layer height under test must contribute at least one usable phase,
    // otherwise the sweep would be vacuously true for that layer height.
    for (double lh : layer_heights) {
        size_t n = 0;
        for (const Phase &ph : phases)
            if (ph.layer_height == lh) ++n;
        CHECK(n > 0);
    }

    for (const Phase &ph : phases) {
        DYNAMIC_SECTION("lh=" << ph.layer_height
                              << " elevation=" << ph.elevation) {
            const sla::SupportPoints out = sla::move_on_mesh_surface(
                points_at_z(ph.sample_z, point_count), mesh, ph.allowed_move);

            CHECK(out.size() == point_count);
            for (size_t i = 0; i < out.size(); ++i)
                CHECK_THAT(double(out[i].pos.z()), WithinAbs(0.0, POS_TOL));
        }
    }
}

// Spec: "支撐點產出必須與切片網格相位無關"
//        scenario "模型僅橫跨單一切片層"
//   and "投影位移上限維持現行取值"
//        scenario "層級數量不足時使用支撐頭直徑"
//
// With a single model level, allowed_move falls back to
// support_head_front_diameter. The projection must converge on exactly the same
// answer as the multi-level bound -- that convergence is what makes the result
// phase independent.
TEST_CASE("SLA thin model: single slice level converges with the multi-level case",
          "[sla][ThinModel]")
{
    const indexed_triangle_set its = make_thin_plate();
    const sla::IndexedMesh     mesh{its};

    // A sampling level above the plate's mid-plane, i.e. the case that used to
    // fail, evaluated under both bounds.
    const float sample_z = 0.125f;

    const std::vector<float> single_level{sample_z};
    const std::vector<float> two_levels{sample_z, sample_z + 0.15f};

    const double am_single = allowed_move_for(single_level);
    const double am_multi  = allowed_move_for(two_levels);

    CHECK_THAT(am_single, WithinAbs(HEAD_FRONT_DIAMETER, 1e-12));
    CHECK(am_multi >= 0.15);

    const sla::SupportPoints out_single =
        sla::move_on_mesh_surface(points_at_z(sample_z), mesh, am_single);
    const sla::SupportPoints out_multi =
        sla::move_on_mesh_surface(points_at_z(sample_z), mesh, am_multi);

    // Non-empty under the single-level fallback ...
    REQUIRE(out_single.size() == 1);
    REQUIRE(out_multi.size() == 1);

    // ... on the underside in both cases, and identical to each other.
    CHECK_THAT(double(out_single.front().pos.z()), WithinAbs(0.0, POS_TOL));
    CHECK_THAT(double(out_multi.front().pos.z()), WithinAbs(0.0, POS_TOL));
    CHECK_THAT(double(out_single.front().pos.z()),
               WithinAbs(double(out_multi.front().pos.z()), POS_TOL));
}
