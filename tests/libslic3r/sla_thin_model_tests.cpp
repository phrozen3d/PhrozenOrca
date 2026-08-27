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
#include <optional>
#include <utility>
#include <vector>

#include <libslic3r/libslic3r.h>
#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/SLA/IndexedMesh.hpp>
#include <libslic3r/SLA/SupportPoint.hpp>
#include <libslic3r/SLA/SupportPointGenerator.hpp>
#include <libslic3r/SLA/SupportTreeMesher.hpp>
#include <libslic3r/SLA/SupportTreeUtils.hpp>

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

// Tolerance for pure double arithmetic on short-decimal inputs: the clamp
// formulas, the front <-> penetration conversions, junction offsets. Nothing
// here has passed through single precision, so the only slack needed is the
// binary representation of values like 0.1 and 0.3.
//
// NOTE: Catch2 prints doubles with 10 significant digits by default, so a
// failure message for this margin reads "is within 0.0 of ...". That is the
// printer, not the value. Do not "fix" it by loosening this constant -- if an
// assertion using it fails, the quantity being checked is not pure arithmetic
// and belongs on MEASURED_TOL instead.
constexpr double CLAMP_TOL = 1e-12;

// Tolerance for any value that has been through single precision: a raycast
// against float mesh vertices, or a float-returning helper. The plate's top
// face is really at 0.20000000298, not 0.2, and igl's hit parameter is a float
// as well, so measure_available_depth() comes back carrying a few times 1e-9 of
// error -- far more than CLAMP_TOL allows and far below anything printable.
constexpr double MEASURED_TOL = 1e-6;

indexed_triangle_set make_thin_plate(double thickness = PLATE_THICKNESS)
{
    return its_make_cube(PLATE_XY, PLATE_XY, thickness);
}

// sla::IndexedMesh keeps a NON-OWNING pointer to the indexed_triangle_set it was
// built from (IndexedMesh.hpp: `const indexed_triangle_set *m_tm`). Building one
// straight from a temporary compiles and even constructs cleanly -- the AABB
// tree is built while the temporary is still alive -- and then dereferences
// freed memory on the first query_ray_hit(). That is a segfault, not a failed
// assertion.
//
// This holder keeps the two together so no test can make that mistake. Member
// order matters: `its` is declared first, so it is initialised before `mesh`
// binds to it.
struct OwnedMesh
{
    indexed_triangle_set its;
    sla::IndexedMesh     mesh;

    explicit OwnedMesh(indexed_triangle_set &&t)
        : its(std::move(t)), mesh(its)
    {}

    OwnedMesh(const OwnedMesh &)            = delete;
    OwnedMesh &operator=(const OwnedMesh &) = delete;
};

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

    CHECK_THAT(am_single, WithinAbs(HEAD_FRONT_DIAMETER, CLAMP_TOL));
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

// ---------------------------------------------------------------------------
// Phase 3: dynamic anti-penetration clamp (pure functions + depth probe).
// ---------------------------------------------------------------------------

namespace {

// Deepest point of the emitted pinhead geometry, per the spec table.
double deepest_point(double front_mm, double pin_r_mm, double contact_r_mm)
{
    return front_mm + sla::head_deepest_point_offset(pin_r_mm, contact_r_mm);
}

// A SupportPoint carrying an explicit front depth, for exercising the existing
// front -> Head::penetration_mm conversion with an already-clamped value.
sla::SupportPoint point_with_front(double front_mm)
{
    sla::SupportPoint sp(Vec3f(2.f, 5.f, 0.f), HEAD_FRONT_RADIUS,
                         sla::SupportPointType::island);
    sp.head_penetration_mm = float(front_mm);
    return sp;
}

} // namespace

// Spec: "Contact Sphere 的最深點換算必須正確"
//        scenario "未啟用接觸球時換算為恆等"
//   and "支撐頭刺入深度必須依局部可用深度動態夾限"
//        scenario "刺入深度大於可用深度"
//
// Configuration 1 of the spec table: no contact sphere. The offset term
// vanishes and the head stops at the plate's mid-plane.
TEST_CASE("SLA thin model: clamp with no contact sphere", "[sla][ThinModel]")
{
    const double r_pin      = 0.2;
    const double r_contact  = 0.0;
    const double configured = 0.3;

    CHECK_THAT(sla::head_deepest_point_offset(r_pin, r_contact),
               WithinAbs(0.0, CLAMP_TOL));

    const double front =
        sla::clamp_front_depth(configured, PLATE_THICKNESS, r_pin, r_contact);

    CHECK_THAT(front, WithinAbs(0.1, CLAMP_TOL));

    // Nothing pokes out of the top face; it stops at the mid-plane.
    CHECK(deepest_point(front, r_pin, r_contact) <= PLATE_THICKNESS + CLAMP_TOL);
    CHECK_THAT(deepest_point(front, r_pin, r_contact),
               WithinAbs(PLATE_MID, CLAMP_TOL));

    // With no sphere the mesh-space conversion is the identity.
    CHECK_THAT(double(sla::point_head_penetration_mesh_mm(
                   point_with_front(front), configured, r_pin, r_contact)),
               WithinAbs(front, MEASURED_TOL));
}

// Spec: "Contact Sphere 的最深點換算必須正確"
//        scenario "啟用接觸球時的夾限"
//
// Configuration 2: the contact sphere is live (r_contact > r_pin). The offset
// still vanishes -- the sphere's top lands exactly on `front` -- but the
// mesh-space penetration goes NEGATIVE, which is exactly why the clamp must
// never be applied to Head::penetration_mm directly.
TEST_CASE("SLA thin model: clamp with a live contact sphere", "[sla][ThinModel]")
{
    const double r_pin      = 0.2;
    const double r_contact  = 0.4;
    const double configured = 0.3;

    REQUIRE(r_contact > r_pin); // establish that a sphere is actually emitted

    CHECK_THAT(sla::head_deepest_point_offset(r_pin, r_contact),
               WithinAbs(0.0, CLAMP_TOL));

    const double front =
        sla::clamp_front_depth(configured, PLATE_THICKNESS, r_pin, r_contact);

    CHECK_THAT(front, WithinAbs(0.1, CLAMP_TOL));
    CHECK_THAT(deepest_point(front, r_pin, r_contact),
               WithinAbs(PLATE_MID, CLAMP_TOL));

    // 0.1 + 0.2 - 0.4 = -0.1. Clamping this value instead of the front depth
    // would be wrong by exactly (r_pin - r_contact).
    CHECK_THAT(double(sla::point_head_penetration_mesh_mm(
                   point_with_front(front), configured, r_pin, r_contact)),
               WithinAbs(-0.1, MEASURED_TOL));
}

// Spec: "Contact Sphere 的最深點換算必須正確"
//        scenario "退化帶的夾限"
//
// Configuration 3: 0 < r_contact <= r_pin. head_mesh_body() emits NO contact
// sphere here, yet point_head_penetration_mesh_mm() converts anyway -- a
// pre-existing inconsistency this change does not fix. The offset term is what
// keeps the clamp correct in spite of it.
TEST_CASE("SLA thin model: clamp inside the degenerate contact-sphere band",
          "[sla][ThinModel]")
{
    const double r_pin      = 0.3;
    const double r_contact  = 0.1;
    const double configured = 0.3;

    // Establish the band rather than assuming it.
    REQUIRE(r_contact > EPSILON);
    REQUIRE(r_contact <= r_pin);

    CHECK_THAT(sla::head_deepest_point_offset(r_pin, r_contact),
               WithinAbs(0.2, CLAMP_TOL));

    // min(0.3, 0.1 - 0.2) = -0.1, clamped up to 0: the head only touches the
    // surface, it does not enter at all.
    const double front =
        sla::clamp_front_depth(configured, PLATE_THICKNESS, r_pin, r_contact);
    CHECK_THAT(front, WithinAbs(0.0, CLAMP_TOL));

    // The offset alone (0.2) already spans the whole plate, so the pin ball top
    // lands exactly ON the top face. It does not protrude -- the policy holds --
    // but the margin is zero, and it cannot be improved without changing
    // SupportTreeMesher, which is explicitly out of scope for this change.
    const double deepest = deepest_point(front, r_pin, r_contact);
    CHECK(deepest <= PLATE_THICKNESS + CLAMP_TOL);
    CHECK_THAT(deepest, WithinAbs(PLATE_THICKNESS, CLAMP_TOL));
}

// Spec: "支撐頭刺入深度必須依局部可用深度動態夾限"
//        scenario "刺入深度小於可用深度一半"
//
// On ordinary geometry the clamp must be completely inert -- this is what backs
// the "thick models are unchanged point for point" claim.
TEST_CASE("SLA thin model: clamp is inert on a thick model", "[sla][ThinModel]")
{
    const double r_pin      = 0.2;
    const double configured = 0.4;
    const double thickness  = 10.0;

    // Inert in all three contact-sphere configurations.
    CHECK_THAT(sla::clamp_front_depth(configured, thickness, r_pin, 0.0),
               WithinAbs(configured, CLAMP_TOL));
    CHECK_THAT(sla::clamp_front_depth(configured, thickness, r_pin, 0.4),
               WithinAbs(configured, CLAMP_TOL));
    CHECK_THAT(sla::clamp_front_depth(configured, thickness, r_pin, 0.1),
               WithinAbs(configured, CLAMP_TOL));

    // The clamp never lifts a depth above what was configured, however much
    // room there is.
    CHECK(sla::clamp_front_depth(configured, thickness, r_pin, 0.0) <= configured);
}

// Guard on std::clamp's precondition: a non-positive configured depth leaves
// the interval [0, configured] empty, which would be undefined behaviour. Such
// a depth cannot penetrate anything, so it passes through untouched.
TEST_CASE("SLA thin model: clamp passes a non-positive configured depth through",
          "[sla][ThinModel]")
{
    CHECK_THAT(sla::clamp_front_depth(0.0, PLATE_THICKNESS, 0.2, 0.0),
               WithinAbs(0.0, CLAMP_TOL));
    CHECK_THAT(sla::clamp_front_depth(-0.05, PLATE_THICKNESS, 0.2, 0.0),
               WithinAbs(-0.05, CLAMP_TOL));
}

// Companion coverage for measure_available_depth(). Not named by task 3.4, but
// the sign of its ray-origin offset is the one detail in this phase that fails
// SILENTLY -- a -eps origin would report ~2*eps on every model in existence
// rather than erroring -- so it is locked here instead of being left to the
// geometry tests in 3.12-3.17.
TEST_CASE("SLA thin model: available depth is measured along the head axis",
          "[sla][ThinModel]")
{
    const indexed_triangle_set its = make_thin_plate();
    const sla::IndexedMesh     mesh{its};

    // A head sitting on the plate's underside points away from the material,
    // so head_dir is -Z and the probe travels +Z into it.
    const Vec3d contact{2.0, 5.0, 0.0};

    SECTION("straight up through a 0.2 mm plate")
    {
        const std::optional<double> depth =
            sla::measure_available_depth(mesh, contact, Vec3d(0., 0., -1.));

        REQUIRE(depth.has_value());
        // The plate thickness, up to single-precision error: the eps the origin
        // advanced by is added back. A -eps origin would report ~2e-3 here
        // instead, which MEASURED_TOL is nowhere near wide enough to hide.
        CHECK_THAT(*depth, WithinAbs(PLATE_THICKNESS, MEASURED_TOL));
        CHECK(*depth > 10.0 * sla::HEAD_DEPTH_PROBE_EPS_MM);
    }

    SECTION("tilted head sees more axial room than the plate is thick")
    {
        // 45 degrees in the XZ plane: the axial path through a 0.2 mm plate is
        // 0.2 * sqrt(2). Measuring along the surface normal instead would
        // under-report this and clamp more tightly than necessary.
        const Vec3d dir_in   = Vec3d(1., 0., 1.).normalized();
        const Vec3d head_dir = -dir_in;

        const std::optional<double> depth =
            sla::measure_available_depth(mesh, contact, head_dir);

        REQUIRE(depth.has_value());
        CHECK_THAT(*depth, WithinAbs(PLATE_THICKNESS * std::sqrt(2.), MEASURED_TOL));
        CHECK(*depth > PLATE_THICKNESS);
    }

    SECTION("a ray that leaves the model reports no hit")
    {
        // head_dir = +Z means the probe travels -Z, i.e. downward and away from
        // a plate that sits above the origin. It never enters material -- the
        // broken-mesh signature the fail-safe keys on.
        const std::optional<double> depth =
            sla::measure_available_depth(mesh, Vec3d{2.0, 5.0, -1.0},
                                         Vec3d(0., 0., 1.));

        CHECK_FALSE(depth.has_value());
    }
}

// ---------------------------------------------------------------------------
// Phase 3 geometry tests: the clamp acting on real emitted pinhead meshes.
//
// SCOPE. These drive the clamp and the mesher, not the whole support tree.
// SupportTree::create() runs a genetic optimizer per point and would make these
// slow and non-deterministic in a shared test binary, and the "identical to
// before the change" claim has no stored golden to compare against. Instead
// each test builds the Head exactly as its commit point leaves it and inspects
// the mesh SupportTreeMesher actually emits. Tree-level acceptance (both tree
// kinds end to end, on real models) is Phase 5's manual run.
//
// The load-bearing trick throughout: changing Head::penetration_mm is a RIGID
// TRANSLATION of the emitted mesh. head_mesh_body() shifts every vertex by
// -(real_width() - penetration - r_back) and real_width() does not depend on
// penetration, and the contact sphere's centre is likewise penetration - r_pin.
// So a delta in penetration moves every vertex by exactly that delta along the
// head axis -- which lets these tests compare meshes exactly, with no
// dependence on how the spheres happen to be facetted.
// ---------------------------------------------------------------------------

namespace {

// Facet count for the emitted heads. Any value works: every assertion below is
// either a bound or a difference between two meshes built at the same detail.
constexpr size_t HEAD_STEPS = 45;

// Head size parameters shared by the geometry tests. Only r_pin and r_contact
// vary between the Contact Sphere configurations.
constexpr double HEAD_BACK_R = 0.5;
constexpr double HEAD_WIDTH  = 1.0;

// Build a head sitting on the plate's underside, pointing straight down (away
// from the material), with an already-clamped mesh-space penetration.
sla::Head make_head(double penetration_mm,
                    double pin_r_mm,
                    double contact_r_mm,
                    const Vec3d &pos = Vec3d{2.0, 5.0, 0.0},
                    const Vec3d &dir = Vec3d{0., 0., -1.})
{
    sla::Head h{HEAD_BACK_R, pin_r_mm, HEAD_WIDTH, penetration_mm, dir, pos};
    h.r_contact_mm = contact_r_mm;
    return h;
}

// Highest point of the emitted head geometry in world Z. For a plate whose top
// face is at z = PLATE_THICKNESS, "does the head poke out of the top surface"
// is exactly "is this greater than PLATE_THICKNESS".
double head_mesh_max_z(const sla::Head &h)
{
    const indexed_triangle_set mesh = sla::get_mesh(h, HEAD_STEPS);
    REQUIRE_FALSE(mesh.vertices.empty());

    double zmax = -std::numeric_limits<double>::infinity();
    for (const Vec3f &v : mesh.vertices)
        zmax = std::max(zmax, double(v.z()));
    return zmax;
}

// The clamp path a Default-tree main pinhead takes (commit point 1): resolve
// the configured front depth from the point, clamp it, convert to mesh space.
double default_tree_penetration(double configured_front_mm,
                                double local_thickness_mm,
                                double pin_r_mm,
                                double contact_r_mm)
{
    const double front = sla::clamp_front_depth(configured_front_mm,
                                                local_thickness_mm,
                                                pin_r_mm, contact_r_mm);
    return sla::front_depth_to_mesh_penetration(front, pin_r_mm, contact_r_mm);
}

// The clamp path a Branching-tree main pinhead takes (commit point 3): the head
// already holds a mesh-space penetration, so it round-trips through the
// inverse conversion first. Anchors on both trees take the same shape.
double branching_tree_penetration(double head_penetration_mm,
                                  double local_thickness_mm,
                                  double pin_r_mm,
                                  double contact_r_mm)
{
    const double front = sla::mesh_penetration_to_front_depth(head_penetration_mm,
                                                              pin_r_mm, contact_r_mm);
    const double clamped = sla::clamp_front_depth(front, local_thickness_mm,
                                                  pin_r_mm, contact_r_mm);
    return sla::front_depth_to_mesh_penetration(clamped, pin_r_mm, contact_r_mm);
}

// A plate with its top face removed: a ray cast upward from inside it hits
// nothing. This is the broken-mesh signature the fail-safe keys on -- an open,
// non-manifold surface.
//
// Selected by geometry rather than by triangle index so it does not silently
// stop removing the right faces if its_make_cube()'s emission order ever
// changes; the caller additionally REQUIREs that the probe really does miss.
indexed_triangle_set make_plate_without_top(size_t *removed = nullptr)
{
    const indexed_triangle_set src = make_thin_plate();

    indexed_triangle_set out;
    out.vertices = src.vertices;
    size_t dropped = 0;

    for (const Vec3i32 &t : src.indices) {
        const bool on_top =
            double(src.vertices[size_t(t(0))].z()) > PLATE_THICKNESS - 1e-6 &&
            double(src.vertices[size_t(t(1))].z()) > PLATE_THICKNESS - 1e-6 &&
            double(src.vertices[size_t(t(2))].z()) > PLATE_THICKNESS - 1e-6;

        if (on_top)
            ++dropped;
        else
            out.indices.push_back(t);
    }

    if (removed) *removed = dropped;
    return out;
}

} // namespace

// Task 3.12 / spec "支撐頭刺入深度必須依局部可用深度動態夾限"
//               scenario "刺入深度大於可用深度"
//           and scenario "支撐幾何與模型的布林交集驗證"
//
// The reference case: 0.2 mm plate, support_head_penetration = 0.3. The
// un-clamped head demonstrably breaks through the top face; the clamped one
// does not.
TEST_CASE("SLA thin model: clamped head does not break through a 0.2 mm plate",
          "[sla][ThinModel]")
{
    const double r_pin      = 0.2;
    const double r_contact  = 0.0;
    const double configured = 0.3;

    const double front = sla::clamp_front_depth(configured, PLATE_THICKNESS,
                                                r_pin, r_contact);
    CHECK_THAT(front, WithinAbs(0.1, CLAMP_TOL));

    const double pen_clamped =
        default_tree_penetration(configured, PLATE_THICKNESS, r_pin, r_contact);
    const double pen_configured =
        sla::front_depth_to_mesh_penetration(configured, r_pin, r_contact);

    const double z_clamped    = head_mesh_max_z(make_head(pen_clamped, r_pin, r_contact));
    const double z_configured = head_mesh_max_z(make_head(pen_configured, r_pin, r_contact));

    // The test geometry really does exercise the defect: without the clamp the
    // head pokes out of the top face.
    CHECK(z_configured > PLATE_THICKNESS);

    // ... and with it, nothing of the head lies above the load-bearing face's
    // far side.
    CHECK(z_clamped <= PLATE_THICKNESS + POS_TOL);

    // Penetration is a rigid translation, so the two meshes differ by exactly
    // the amount the clamp took off. Independent of facet count.
    // Mesh vertices are floats, so this carries ~1e-7 of single-precision
    // noise; POS_TOL is the file-wide slack for exactly that.
    CHECK_THAT(z_configured - z_clamped,
               WithinAbs(configured - front, POS_TOL));
}

// Task 3.13 / spec "四個夾限提交點的語意與時序"
//
// Default and Branching main pinheads reach the clamp by different routes --
// the Default tree resolves a per-point front depth, the Branching tree
// round-trips a mesh-space penetration through the inverse conversion. Both
// must land on the same answer and neither may protrude.
TEST_CASE("SLA thin model: default and branching heads agree and neither protrudes",
          "[sla][ThinModel]")
{
    const double r_pin      = 0.2;
    const double configured = 0.3;

    // Branching heads are constructed with Head's default r_contact of 0 --
    // establish that rather than assuming it, since the two paths only coincide
    // while it holds.
    const sla::Head fresh{HEAD_BACK_R, r_pin, HEAD_WIDTH, configured,
                          Vec3d{0., 0., -1.}, Vec3d::Zero()};
    REQUIRE_THAT(fresh.r_contact_mm, WithinAbs(0.0, CLAMP_TOL));

    const double pen_default =
        default_tree_penetration(configured, PLATE_THICKNESS, r_pin, 0.0);
    const double pen_branching =
        branching_tree_penetration(configured, PLATE_THICKNESS, r_pin, 0.0);

    CHECK_THAT(pen_default, WithinAbs(pen_branching, CLAMP_TOL));

    CHECK(head_mesh_max_z(make_head(pen_default,   r_pin, 0.0)) <= PLATE_THICKNESS + POS_TOL);
    CHECK(head_mesh_max_z(make_head(pen_branching, r_pin, 0.0)) <= PLATE_THICKNESS + POS_TOL);

    SECTION("a tilted head is bounded too")
    {
        // The clamp measures along the head axis, so a head crossing the plate
        // at 45 degrees legitimately gets a deeper bite (0.2 * sqrt(2) of
        // material instead of 0.2). Its pin ball still must not reach past the
        // top face -- note the ball bulges sideways off the axis, so this is
        // not implied by the axial bound and is checked on the real mesh.
        const Vec3d dir_in   = Vec3d(1., 0., 1.).normalized();
        const Vec3d head_dir = -dir_in;

        const double axial = PLATE_THICKNESS * std::sqrt(2.);
        const double pen   = default_tree_penetration(configured, axial, r_pin, 0.0);

        CHECK(pen > 0.1);   // deeper than the vertical case, as intended
        CHECK(head_mesh_max_z(make_head(pen, r_pin, 0.0,
                                        Vec3d{2.0, 5.0, 0.0}, head_dir))
              <= PLATE_THICKNESS + POS_TOL);
    }
}

// Task 3.14 / spec "Contact Sphere 的最深點換算必須正確"
//               scenarios "啟用接觸球時的夾限" + "退化帶的夾限"
TEST_CASE("SLA thin model: no protrusion in either contact-sphere configuration",
          "[sla][ThinModel]")
{
    SECTION("contact sphere live (r_contact > r_pin)")
    {
        const double r_pin     = 0.2;
        const double r_contact = 0.4;
        REQUIRE(r_contact > r_pin); // the sphere is actually emitted

        const double pen_clamped =
            default_tree_penetration(0.3, PLATE_THICKNESS, r_pin, r_contact);
        const double pen_configured =
            sla::front_depth_to_mesh_penetration(0.3, r_pin, r_contact);

        // The mesh-space value goes negative here; the emitted geometry is what
        // matters, and the contact sphere's top is what reaches highest.
        CHECK_THAT(pen_clamped, WithinAbs(-0.1, CLAMP_TOL));

        CHECK(head_mesh_max_z(make_head(pen_configured, r_pin, r_contact))
              > PLATE_THICKNESS);
        CHECK(head_mesh_max_z(make_head(pen_clamped, r_pin, r_contact))
              <= PLATE_THICKNESS + POS_TOL);
    }

    SECTION("degenerate band (0 < r_contact <= r_pin)")
    {
        const double r_pin     = 0.3;
        const double r_contact = 0.1;
        REQUIRE(r_contact > EPSILON);
        REQUIRE(r_contact <= r_pin); // NO contact sphere is emitted here

        const double pen_clamped =
            default_tree_penetration(0.3, PLATE_THICKNESS, r_pin, r_contact);

        // front clamps to 0, but the mesh-space conversion still adds
        // (r_pin - r_contact), so the pin ball's top lands exactly ON the top
        // face. Zero margin -- see the unit test for why that cannot be
        // improved without changing SupportTreeMesher.
        CHECK_THAT(pen_clamped, WithinAbs(0.2, CLAMP_TOL));
        CHECK(head_mesh_max_z(make_head(pen_clamped, r_pin, r_contact))
              <= PLATE_THICKNESS + POS_TOL);
    }
}

// Task 3.15 / spec "支撐頭刺入深度必須依局部可用深度動態夾限"
//               scenario "刺入深度小於可用深度一半"
//
// On a thick model the clamp must be a no-op, and the emitted mesh must be
// identical VERTEX FOR VERTEX to what the un-clamped code produced. This is the
// concrete form of the "ordinary models are unchanged" guarantee.
TEST_CASE("SLA thin model: thick model head mesh is unchanged vertex for vertex",
          "[sla][ThinModel]")
{
    const double r_pin      = 0.2;
    const double configured = 0.4;
    const double thickness  = 10.0;

    const double pen_clamped =
        default_tree_penetration(configured, thickness, r_pin, 0.0);
    const double pen_configured =
        sla::front_depth_to_mesh_penetration(configured, r_pin, 0.0);

    CHECK_THAT(pen_clamped, WithinAbs(pen_configured, CLAMP_TOL));
    CHECK_THAT(pen_clamped, WithinAbs(0.4, CLAMP_TOL));

    const indexed_triangle_set a = sla::get_mesh(make_head(pen_clamped,    r_pin, 0.0), HEAD_STEPS);
    const indexed_triangle_set b = sla::get_mesh(make_head(pen_configured, r_pin, 0.0), HEAD_STEPS);

    REQUIRE(a.vertices.size() == b.vertices.size());
    REQUIRE(a.indices.size() == b.indices.size());
    REQUIRE_FALSE(a.vertices.empty());

    size_t vertex_mismatches = 0;
    for (size_t i = 0; i < a.vertices.size(); ++i)
        if (a.vertices[i] != b.vertices[i])
            ++vertex_mismatches;
    CHECK(vertex_mismatches == 0);

    size_t index_mismatches = 0;
    for (size_t i = 0; i < a.indices.size(); ++i)
        if (a.indices[i] != b.indices[i])
            ++index_mismatches;
    CHECK(index_mismatches == 0);
}

// Task 3.16 / spec "夾限失敗時必須採 fail-safe 並彙總回報"
//
// SCOPE NOTE on "exactly one log line": that property is structural, not
// asserted here. log_depth_probe_misses() returns early when the count is zero
// and is called from exactly one place per tree driver
// (SupportTreeBuildsteps::execute and create_branching_tree), so the count of
// lines is 0 or 1 by construction. Asserting it for real would mean installing
// a boost::log sink into a shared, randomly-ordered test binary -- global state
// that would leak into every other test. What IS asserted here is the value the
// single line reports, and the fail-safe depth that goes with it.
TEST_CASE("SLA thin model: fail-safe drives the depth to zero and is counted",
          "[sla][ThinModel]")
{
    const Vec3d  contact{2.0, 5.0, 0.0};
    const Vec3d  head_dir{0., 0., -1.};
    const double r_pin      = 0.2;
    const double configured = 0.3;

    SECTION("a broken mesh trips the fail-safe and increments the counter")
    {
        size_t removed = 0;
        // OwnedMesh, not a bare IndexedMesh over a temporary -- see the note on
        // the holder. This exact line used to segfault.
        OwnedMesh broken{make_plate_without_top(&removed)};

        // Establish that the fixture is what it claims to be, and that it
        // really does defeat the probe.
        REQUIRE(removed > 0);
        REQUIRE_FALSE(sla::measure_available_depth(broken.mesh, contact, head_dir).has_value());

        sla::DepthProbeMissCounter misses;
        REQUIRE(misses.count() == 0);

        const double front = sla::clamped_front_depth(broken.mesh, contact, head_dir,
                                                      configured, r_pin, 0.0, misses);

        CHECK_THAT(front, WithinAbs(0.0, CLAMP_TOL));
        CHECK(misses.count() == 1);

        // The head then just touches the surface and cannot protrude.
        const double pen = sla::front_depth_to_mesh_penetration(front, r_pin, 0.0);
        CHECK(head_mesh_max_z(make_head(pen, r_pin, 0.0)) <= PLATE_THICKNESS + POS_TOL);

        // Every further failure accumulates into the same single report.
        sla::clamped_front_depth(broken.mesh, contact, head_dir, configured, r_pin, 0.0, misses);
        sla::clamped_front_depth(broken.mesh, contact, head_dir, configured, r_pin, 0.0, misses);
        CHECK(misses.count() == 3);
    }

    SECTION("a sound mesh never trips it, so nothing is reported")
    {
        OwnedMesh sound{make_thin_plate()};

        sla::DepthProbeMissCounter misses;
        const double front = sla::clamped_front_depth(sound.mesh, contact, head_dir,
                                                      configured, r_pin, 0.0, misses);

        // MEASURED_TOL, not CLAMP_TOL: this front depth came out of a raycast
        // against float vertices, so it is 0.1000000005 rather than 0.1.
        CHECK_THAT(front, WithinAbs(0.1, MEASURED_TOL));
        // Zero is what makes log_depth_probe_misses() emit nothing at all.
        CHECK(misses.count() == 0);
    }

    SECTION("a non-unit head direction is treated as a measurement failure")
    {
        // The degenerate taildir in connect_to_model_body() reaches the probe
        // exactly like this. It must take the fail-safe rather than feeding a
        // bad direction into query_ray_hit().
        OwnedMesh   sound{make_thin_plate()};
        const Vec3d degenerate{0., 0., 0.};

        sla::DepthProbeMissCounter misses;
        const double front = sla::clamped_front_depth(sound.mesh, contact, degenerate,
                                                      configured, r_pin, 0.0, misses);

        CHECK_THAT(front, WithinAbs(0.0, CLAMP_TOL));
        CHECK(misses.count() == 1);
    }

    SECTION("an empty mesh is a measurement failure, not a crash")
    {
        // Reachable in production: a fully hollowed object, or a mesh that
        // failed to load. The probe must report failure rather than walking an
        // AABB tree that has nothing in it.
        OwnedMesh empty{indexed_triangle_set{}};
        REQUIRE(empty.its.indices.empty());

        REQUIRE_FALSE(
            sla::measure_available_depth(empty.mesh, contact, head_dir).has_value());

        sla::DepthProbeMissCounter misses;
        const double front = sla::clamped_front_depth(empty.mesh, contact, head_dir,
                                                      configured, r_pin, 0.0, misses);

        CHECK_THAT(front, WithinAbs(0.0, CLAMP_TOL));
        CHECK(misses.count() == 1);
    }
}

// Task 3.17 / spec "四個夾限提交點的語意與時序"
//
// The Branching anchor clamp MUST run after `toj` is read from
// junction_point(). This test proves why: it shows that clamping genuinely
// MOVES the junction, so doing it earlier would relocate the bridge endpoint
// and change which bridges the beam check accepts.
TEST_CASE("SLA thin model: branching anchor clamp leaves the bridge endpoint intact",
          "[sla][ThinModel]")
{
    const double r_pin      = 0.2;
    const double configured = 0.3;
    const Vec3d  anchor_pos{2.0, 5.0, 0.0};
    const Vec3d  anchor_dir{0., 0., -1.};

    sla::Anchor anchor{HEAD_BACK_R, r_pin, HEAD_WIDTH, configured,
                       anchor_dir, anchor_pos};
    REQUIRE_THAT(anchor.r_contact_mm, WithinAbs(0.0, CLAMP_TOL));

    // BranchingTreeSLA.cpp reads this BEFORE the clamp; it is the bridge
    // endpoint and the input to the beam feasibility check.
    const Vec3d  toj_pos = anchor.junction_point();
    const double toj_r   = anchor.r_back_mm;

    // Now the clamp, exactly as commit point 4 applies it.
    const double front = sla::clamp_front_depth(
        sla::mesh_penetration_to_front_depth(anchor.penetration_mm, r_pin,
                                             anchor.r_contact_mm),
        PLATE_THICKNESS, r_pin, anchor.r_contact_mm);
    anchor.penetration_mm =
        sla::front_depth_to_mesh_penetration(front, r_pin, anchor.r_contact_mm);

    CHECK_THAT(front, WithinAbs(0.1, CLAMP_TOL));

    // The endpoint already captured is untouched: the bridge that gets built
    // and the feasibility decision already made both stand.
    CHECK_THAT(toj_pos.z(), WithinAbs(anchor_pos.z() + (2 * r_pin + HEAD_WIDTH +
                                                        HEAD_BACK_R - configured) *
                                                           anchor_dir.z(),
                                      1e-9));
    CHECK_THAT(toj_r, WithinAbs(HEAD_BACK_R, CLAMP_TOL));

    // And here is why the ordering is not a matter of taste: after the clamp
    // the junction has MOVED, by exactly the depth the clamp took off, along
    // the head axis. Reading toj from junction_point() after this point would
    // have handed the beam check a different endpoint.
    const Vec3d toj_after = anchor.junction_point();
    const Vec3d shift     = toj_after - toj_pos;

    CHECK(shift.norm() > POS_TOL);                       // it really does move
    CHECK_THAT(shift.norm(), WithinAbs(configured - front, 1e-9));
    // The move is purely along the head axis.
    CHECK_THAT(shift.normalized().dot(anchor_dir), WithinAbs(1.0, 1e-9));

    // The anchor's own geometry stays inside the plate, which is the point of
    // the clamp in the first place. Anchor derives from Head, so this passes it
    // by reference rather than slicing a copy.
    CHECK(head_mesh_max_z(anchor) <= PLATE_THICKNESS + POS_TOL);
}
