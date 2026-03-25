///|/ Copyright (c) Prusa Research 2022 - 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
// Step 3.3: Ported from PrusaSlicer's SLA/SupportTreeUtils.hpp.
// Adaptations for PhrozenOrca:
//   - AABBMesh -> IndexedMesh (same ray-cast API)
//   - using Hit = IndexedMesh::hit_result
//   - ground_level(sm) simplified (no pad_cfg / zoffset in PhrozenOrca)
//   - get_normal() implemented via IndexedMesh::squared_distance + normal_by_face_id
//   - solver.set_loc_criteria() removed (not in PhrozenOrca's NLoptAlgComb)
//   - AlgNLoptMLSL_Subplx added to NLoptOptimizer.hpp (Step 3.3 prerequisite)
//   - DiffBridge(Junction,Junction) constructor added to SupportTreeBuilder.hpp (Step 3.3)
#ifndef SLASUPPORTTREEUTILS_HPP
#define SLASUPPORTTREEUTILS_HPP

#include <cstdint>
#include <optional>
#include <array>
#include <algorithm>
#include <map>

#include <libslic3r/Execution/Execution.hpp>
#include <libslic3r/Optimize/NLoptOptimizer.hpp>
#include <libslic3r/Optimize/BruteforceOptimizer.hpp>
#include <libslic3r/KDTreeIndirect.hpp>
#include <libslic3r/SLA/SupportTreeBuilder.hpp>
#include <libslic3r/SLA/SupportTreeBuildsteps.hpp>

#include <boost/container/small_vector.hpp>
#include <boost/log/trivial.hpp>

namespace Slic3r { namespace sla {

using Slic3r::opt::initvals;
using Slic3r::opt::bounds;
using Slic3r::opt::StopCriteria;
using Slic3r::opt::Optimizer;
using Slic3r::opt::AlgNLoptSubplex;
using Slic3r::opt::AlgNLoptGenetic;
using Slic3r::opt::AlgNLoptMLSL_Subplx;

// Use IndexedMesh as the mesh type for PhrozenOrca (corresponds to AABBMesh in PrusaSlicer).
using Hit = IndexedMesh::hit_result;

template<class It> Hit min_hit(It from, It to)
{
    auto mit = std::min_element(from, to, [](const Hit &h1, const Hit &h2) {
        return h1.distance() < h2.distance();
    });
    return *mit;
}

// Step 3.3: Declaration only — definition lives in SupportTreeBuildsteps.cpp.
// Do NOT add an inline body here: SupportTreeBuildsteps.cpp already defines
// get_criteria as a non-inline external symbol, and a second inline definition
// in this header causes LNK2005 when BranchingTreeSLA.obj is linked.
StopCriteria get_criteria(const SupportTreeConfig &cfg);

// ground_level: simplified from PrusaSlicer (no pad_cfg / zoffset in PhrozenOrca).
// PrusaSlicer accounts for embed_object pad wall_thickness; PhrozenOrca doesn't have this.
inline double ground_level(const SupportableMesh &sm)
{
    return sm.emesh.ground_level() - sm.cfg.object_elevation_mm;
}

// get_normal: implemented via IndexedMesh API (replaces MeshNormals.hpp get_normal(AABBMesh)).
inline Vec3d get_normal(const IndexedMesh &mesh, const Vec3d &pos)
{
    int   face_id = -1;
    Vec3d closest;
    mesh.squared_distance(pos, face_id, closest);
    if (face_id < 0)
        return Vec3d::UnitZ(); // fallback
    return mesh.normal_by_face_id(face_id);
}

/* ************************************************************************** */
/* Beam / Ball structs + beam_mesh_hit (ported from PrusaSlicer)              */
/* ************************************************************************** */

// A simple sphere with a center and a radius
struct Ball { Vec3d p; double R; };

template<size_t Samples = 8>
struct Beam_ { // Defines a set of rays displaced along a cone's surface
    static constexpr size_t SAMPLES = Samples;

    Vec3d  src;
    Vec3d  dir;
    double r1;
    double r2; // radius of the beam 1 unit further from src in dir direction

    Beam_(const Vec3d &s, const Vec3d &d, double R1, double R2)
        : src{s}, dir{d}, r1{R1}, r2{R2} {}

    Beam_(const Ball &src_ball, const Ball &dst_ball)
        : src{src_ball.p}, dir{(dst_ball.p - src_ball.p).normalized()}, r1{src_ball.R}
    {
        r2 = src_ball.R;
        double d = (dst_ball.p - src_ball.p).norm();
        if (d > EPSILON)
            r2 += (dst_ball.R - src_ball.R) / d;
    }

    Beam_(const Vec3d &s, const Vec3d &d, double R)
        : src{s}, dir{d}, r1{R}, r2{R}
    {}
};

using Beam = Beam_<>;

template<class Ex, size_t RayCount = Beam::SAMPLES>
Hit beam_mesh_hit(Ex policy,
                  const IndexedMesh &mesh,
                  const Beam_<RayCount> &beam,
                  double sd)
{
    Vec3d  src   = beam.src;
    Vec3d  dst   = src + beam.dir;
    double r_src = beam.r1;
    double r_dst = beam.r2;

    Vec3d D   = (dst - src);
    Vec3d dir = D.normalized();
    PointRing<RayCount> ring{dir};

    std::array<Hit, RayCount> hits;

    execution::for_each(
        policy, size_t(0), hits.size(),
        [&mesh, r_src, r_dst, src, dst, &ring, dir, sd, &hits](size_t i) {
            Hit &hit = hits[i];

            Vec3d p_src = ring.get(i, src, r_src + sd);
            Vec3d p_dst = ring.get(i, dst, r_dst + sd);
            Vec3d raydir = (p_dst - p_src).normalized();

            auto hr = mesh.query_ray_hit(p_src + r_src * raydir, raydir);

            if (hr.is_inside()) {
                if (hr.distance() > 2 * r_src + sd)
                    hit = Hit(0.0);
                else {
                    auto q = p_src + (hr.distance() + EPSILON) * raydir;
                    hit    = mesh.query_ray_hit(q, raydir);
                }
            } else
                hit = hr;
        },
        std::min(execution::max_concurrency(policy), RayCount));

    return min_hit(hits.begin(), hits.end());
}

/* ************************************************************************** */
/* pinhead_mesh_hit                                                            */
/* ************************************************************************** */

template<class Ex>
Hit pinhead_mesh_hit(Ex                ex,
                     const IndexedMesh &mesh,
                     const Vec3d       &s,
                     const Vec3d       &dir,
                     double             r_pin,
                     double             r_back,
                     double             width,
                     double             sd)
{
    static const size_t SAMPLES = 16;

    auto &m = mesh;

    std::array<Hit, SAMPLES> hits;

    struct Rings {
        double              rpin;
        double              rback;
        Vec3d               spin;
        Vec3d               sback;
        PointRing<SAMPLES>  ring;

        Vec3d backring(size_t idx) { return ring.get(idx, sback, rback); }
        Vec3d pinring(size_t idx)  { return ring.get(idx, spin,  rpin);  }
    } rings{r_pin + sd, r_back + sd, s, s + (r_pin + width + r_back) * dir, dir};

    execution::for_each(
        ex, size_t(0), hits.size(), [&m, &rings, sd, &hits](size_t i) {
            Vec3d ps = rings.pinring(i);
            Vec3d p  = rings.backring(i);
            auto &hit = hits[i];

            Vec3d n = (p - ps).normalized();
            auto  q = m.query_ray_hit(ps + sd * n, n);

            if (q.is_inside()) {
                if (q.distance() > rings.rpin)
                    hit = Hit(0.0);
                else {
                    auto q2 = m.query_ray_hit(ps + (q.distance() + 2 * sd) * n, n);
                    hit     = q2;
                }
            } else
                hit = q;
        },
        std::min(execution::max_concurrency(ex), SAMPLES));

    return min_hit(hits.begin(), hits.end());
}

template<class Ex>
Hit pinhead_mesh_hit(Ex                ex,
                     const IndexedMesh &mesh,
                     const Head        &head,
                     double             safety_d)
{
    return pinhead_mesh_hit(ex, mesh, head.pos, head.dir, head.r_pin_mm,
                            head.r_back_mm, head.width_mm, safety_d);
}

/* ************************************************************************** */
/* non_duplicate_suppt_indices                                                 */
/* ************************************************************************** */

inline double distance(const SupportPoint &a, const SupportPoint &b)
{
    return (a.pos - b.pos).norm();
}

template<class PtIndex>
std::vector<size_t> non_duplicate_suppt_indices(const PtIndex       &index,
                                                const SupportPoints &suppts,
                                                double               eps)
{
    std::vector<bool> to_remove(suppts.size(), false);

    for (size_t i = 0; i < suppts.size(); ++i) {
        size_t closest_idx =
            find_closest_point(index, suppts[i].pos,
                               [&i, &to_remove](size_t i_closest) {
                                   return i_closest != i &&
                                          !to_remove[i_closest];
                               });

        if (closest_idx < suppts.size() &&
            (suppts[i].pos - suppts[closest_idx].pos).norm() < eps)
            to_remove[i] = true;
    }

    auto ret = reserve_vector<size_t>(suppts.size());
    for (size_t i = 0; i < to_remove.size(); i++)
        if (!to_remove[i])
            ret.emplace_back(i);

    return ret;
}

/* ************************************************************************** */
/* optimize_pinhead_placement + calculate_pinhead_placement                   */
/* ************************************************************************** */

template<class Ex>
bool optimize_pinhead_placement(Ex                     policy,
                                const SupportableMesh &m,
                                Head                  &head)
{
    Vec3d n = get_normal(m.emesh, head.pos);
    assert(std::abs(n.norm() - 1.0) < EPSILON);

    auto [polar, azimuth] = dir_to_spheric(n);

    double back_r = head.r_back_mm;

    if (polar < PI - m.cfg.normal_cutoff_angle) return false;

    // skip if the surface is not steep enough to need support
    if (polar < M_PI / 2.0 + m.cfg.overhang_angle_threshold) return false;

    polar = std::max(polar, PI - m.cfg.bridge_slope);

    Vec3d  hp   = head.pos;
    double lmin = m.cfg.head_width_mm, lmax = lmin;

    if (back_r < m.cfg.head_back_radius_mm) {
        lmin = 0., lmax = m.cfg.head_penetration_mm;
    }

    double w     = lmin + 2 * back_r + 2 * m.cfg.head_front_radius_mm -
                   m.cfg.head_penetration_mm;
    double pin_r = head.r_pin_mm;

    auto nn = spheric_to_dir(polar, azimuth).normalized();
    double sd = m.cfg.safety_distance(back_r);

    Hit t = pinhead_mesh_hit(policy, m.emesh, hp, nn, pin_r, back_r, w, sd);

    if (t.distance() < w) {
        // AlgNLoptMLSL_Subplx: global+local combined search (added to NLoptOptimizer.hpp).
        // Note: set_loc_criteria not available in PhrozenOrca's NLopt wrapper;
        // the global criteria is used for both global and local search phases.
        Optimizer<opt::AlgNLoptMLSL_Subplx> solver(
            get_criteria(m.cfg).stop_score(w).max_iterations(100));
        solver.seed(0);

        auto oresult = solver.to_max().optimize(
            [&m, pin_r, back_r, hp, sd, policy](const opt::Input<3> &input) {
                auto &[plr, azm, l] = input;
                auto dir = spheric_to_dir(plr, azm).normalized();
                return pinhead_mesh_hit(policy, m.emesh, hp, dir, pin_r,
                                        back_r, l, sd)
                    .distance();
            },
            initvals({polar, azimuth, (lmin + lmax) / 2.}),
            bounds({{PI - m.cfg.bridge_slope, PI},
                    {-PI, PI},
                    {lmin, lmax}}));

        if (oresult.score > w) {
            polar   = std::get<0>(oresult.optimum);
            azimuth = std::get<1>(oresult.optimum);
            nn      = spheric_to_dir(polar, azimuth).normalized();
            lmin    = std::get<2>(oresult.optimum);
            t       = Hit(oresult.score);
        }
    }

    bool ret = false;
    if (t.distance() > w && hp.z() + w * nn.z() >= ground_level(m)) {
        head.dir       = nn;
        head.width_mm  = lmin;
        head.r_back_mm = back_r;
        ret = true;
    } else if (back_r > m.cfg.head_fallback_radius_mm) {
        head.r_back_mm = m.cfg.head_fallback_radius_mm;
        ret = optimize_pinhead_placement(policy, m, head);
    }

    return ret;
}

template<class Ex>
std::optional<Head> calculate_pinhead_placement(Ex                     policy,
                                                const SupportableMesh &sm,
                                                size_t                 suppt_idx)
{
    if (suppt_idx >= sm.pts.size())
        return {};

    const SupportPoint &sp = sm.pts[suppt_idx];
    Head head{
        sm.cfg.head_back_radius_mm,
        sp.head_front_radius,
        0.,
        sm.cfg.head_penetration_mm,
        Vec3d::Zero(),
        sp.pos.cast<double>()
    };

    if (optimize_pinhead_placement(policy, sm, head)) {
        head.id = long(suppt_idx);
        return head;
    }

    return {};
}

/* ************************************************************************** */
/* GroundConnection + build_ground_connection                                  */
/* ************************************************************************** */

struct GroundConnection {
    // At most 2-3 junctions on the path to ground
    static constexpr size_t MaxExpectedJunctions = 3;

    boost::container::small_vector<Junction, MaxExpectedJunctions> path;
    std::optional<Pedestal> pillar_base;

    operator bool() const { return pillar_base.has_value() && !path.empty(); }
};

// build_ground_connection: simplified from PrusaSlicer (no pad_cfg in PhrozenOrca).
inline long build_ground_connection(SupportTreeBuilder    &builder,
                                    const SupportableMesh &sm,
                                    const GroundConnection &conn)
{
    long ret = SupportTreeNode::ID_UNSET;

    if (!conn)
        return ret;

    auto it   = conn.path.begin();
    auto itnx = std::next(it);

    while (itnx != conn.path.end()) {
        builder.add_diffbridge(*it, *itnx);
        builder.add_junction(*itnx);
        ++it; ++itnx;
    }

    auto   gp = conn.path.back().pos;
    gp.z()    = ground_level(sm);
    double h  = conn.path.back().pos.z() - gp.z();

    // Note: PrusaSlicer adds pad wall_thickness_mm here for embed-object mode.
    // PhrozenOrca does not support embed_object pads, so this adjustment is omitted.

    // Note: PrusaSlicer's add_pillar supports tapered pillars (r_start, r_end).
    // PhrozenOrca's Pillar only has a single radius; use conn.path.back().r.
    ret = builder.add_pillar(gp, h, conn.path.back().r);

    if (conn.pillar_base->r_top >= sm.cfg.head_back_radius_mm)
        builder.add_pillar_base(ret, conn.pillar_base->height, conn.pillar_base->r_bottom);

    return ret;
}

/* ************************************************************************** */
/* check_ground_route + deepsearch_ground_connection                          */
/* ************************************************************************** */

constexpr bool IsWideningFnHelper = true; // used below in static_assert

template<class Fn>
constexpr bool IsWideningFn = std::is_invocable_r_v<double, Fn, Ball, Vec3d, double>;

template<class WFn> struct BeamSamples { static constexpr size_t Value = 8; };
template<class WFn> constexpr size_t BeamSamplesV = BeamSamples<std::remove_cv_t<std::remove_reference_t<WFn>>>::Value;

enum class GroundRouteCheck { Full, PillarOnly };

template<class Ex, class WideningFn,
         class = std::enable_if_t<IsWideningFn<WideningFn>>>
Vec3d check_ground_route(
    Ex                     policy,
    const SupportableMesh &sm,
    const Junction        &source,
    const Vec3d           &dir,
    double                 bridge_len,
    WideningFn           &&wideningfn,
    GroundRouteCheck       type = GroundRouteCheck::Full)
{
    static const constexpr auto Samples = BeamSamplesV<WideningFn>;

    Vec3d ret;

    const auto sd     = sm.cfg.safety_distance(source.r);
    const auto gndlvl = ground_level(sm);

    double t  = (gndlvl - source.pos.z()) / dir.z();
    bridge_len = std::min(t, bridge_len);

    Vec3d  bridge_end = source.pos + bridge_len * dir;
    double down_l     = bridge_end.z() - gndlvl;
    double bridge_r   = wideningfn(Ball{source.pos, source.r}, dir, bridge_len);
    double brhit_dist = 0.;

    if (bridge_len > EPSILON && type == GroundRouteCheck::Full) {
        Beam_<Samples> bridgebeam{Ball{source.pos, source.r},
                                  Ball{bridge_end, bridge_r}};
        auto brhit  = beam_mesh_hit(policy, sm.emesh, bridgebeam, sd);
        brhit_dist  = brhit.distance();
    } else {
        brhit_dist = bridge_len;
    }

    if (brhit_dist < bridge_len) {
        ret = (source.pos + brhit_dist * dir);
    } else if (down_l > 0.) {
        auto   gp         = Vec3d{bridge_end.x(), bridge_end.y(), gndlvl};
        double end_radius = wideningfn(
            Ball{bridge_end, bridge_r}, DOWN, bridge_end.z() - gndlvl);

        Beam_<Samples> gndbeam{{bridge_end, bridge_r}, {gp, end_radius}};
        auto   gndhit    = beam_mesh_hit(policy, sm.emesh, gndbeam, sd);
        double gnd_hit_d = std::min(gndhit.distance(), down_l + EPSILON);

        if (source.r >= sm.cfg.head_back_radius_mm &&
            gndhit.distance() > down_l &&
            sm.cfg.object_elevation_mm < EPSILON) {
            double gap     = std::sqrt(sm.emesh.squared_distance(gp));
            double base_r  = std::max(sm.cfg.base_radius_mm, end_radius);
            double min_gap = sm.cfg.pillar_base_safety_distance_mm + base_r;
            if (gap < min_gap)
                gnd_hit_d = down_l - min_gap + gap;
        }

        ret = Vec3d{bridge_end.x(), bridge_end.y(), bridge_end.z() - gnd_hit_d};
    } else {
        ret = bridge_end;
    }

    return ret;
}

template<class Ex, class WideningFn,
         class = std::enable_if_t<IsWideningFn<WideningFn>>>
GroundConnection deepsearch_ground_connection(
    Ex                     policy,
    const SupportableMesh &sm,
    const Junction        &source,
    WideningFn           &&wideningfn,
    const Vec3d           &init_dir = DOWN)
{
    constexpr unsigned MaxIterationsGlobal = 5000;
    constexpr unsigned MaxIterationsLocal  = 100;
    constexpr double   RelScoreDiff        = 0.05;

    const auto gndlvl = ground_level(sm);

    // Note: PrusaSlicer sets separate local criteria via solver.set_loc_criteria().
    // PhrozenOrca's NLoptAlgComb does not expose set_loc_criteria, so we use
    // a single criteria for both the global and local search phases.
    auto criteria = get_criteria(sm.cfg);
    criteria.max_iterations(MaxIterationsGlobal);
    criteria.abs_score_diff(std::nan(""));  // disable abs diff stop (use NaN)
    criteria.rel_score_diff(std::nan("")); // disable rel diff stop
    criteria.stop_score(gndlvl);

    Optimizer<opt::AlgNLoptMLSL_Subplx> solver(criteria);
    solver.seed(0);

    auto z_fn = [&](const opt::Input<3> &input) {
        auto &[plr, azm, bridge_len] = input;
        Vec3d n = spheric_to_dir(plr, azm);
        Vec3d hitpt = check_ground_route(policy, sm, source, n, bridge_len, wideningfn);
        return hitpt.z();
    };

    auto [plr_init, azm_init] = dir_to_spheric(init_dir);
    plr_init = std::max(plr_init, PI - sm.cfg.bridge_slope);

    auto bound_constraints =
        bounds({
            {PI - sm.cfg.bridge_slope, PI},
            {-PI, PI},
            {0., sm.cfg.max_bridge_length_mm}
        });

    auto oresult = solver.to_min().optimize(
        z_fn,
        initvals({plr_init, azm_init, 0.}),
        bound_constraints);

    GroundConnection conn;

    auto [plr, azm, bridge_l] = oresult.optimum;
    Vec3d n = spheric_to_dir(plr, azm);
    assert(std::abs(n.norm() - 1.) < EPSILON);

    double t = (gndlvl - source.pos.z()) / n.z();
    bridge_l = std::min(t, bridge_l);

    double l = 0., l_max = bridge_l;
    double zlvl = std::numeric_limits<double>::infinity();
    while (zlvl > gndlvl && l <= l_max) {
        zlvl = check_ground_route(policy, sm, source, n, l, wideningfn,
                                  GroundRouteCheck::PillarOnly)
                   .z();
        if (zlvl <= gndlvl)
            bridge_l = l;
        l += source.r;
    }

    Vec3d  bridge_end = source.pos + bridge_l * n;
    Vec3d  gp{bridge_end.x(), bridge_end.y(), gndlvl};
    double bridge_r   = wideningfn(Ball{source.pos, source.r}, n, bridge_l);
    double down_l     = bridge_end.z() - gndlvl;
    double end_radius = wideningfn(Ball{bridge_end, bridge_r}, DOWN, down_l);
    double base_r     = std::max(sm.cfg.base_radius_mm, end_radius);

    conn.path.emplace_back(source);
    if (bridge_l > EPSILON)
        conn.path.emplace_back(Junction{bridge_end, bridge_r});

    if (z_fn(opt::Input<3>({plr, azm, bridge_l})) <= gndlvl)
        conn.pillar_base = Pedestal{gp, sm.cfg.base_height_mm, base_r, end_radius};

    return conn;
}

// deepsearch_ground_connection with predefined end radius
template<class Ex>
GroundConnection deepsearch_ground_connection(Ex                     policy,
                                              const SupportableMesh &sm,
                                              const Junction        &source,
                                              double                 end_radius,
                                              const Vec3d           &init_dir = DOWN)
{
    double gndlvl = ground_level(sm);
    auto   wfn    = [end_radius, gndlvl](const Ball &src, const Vec3d &dir, double len) {
        if (len < EPSILON)
            return src.R;
        Vec3d  dst     = src.p + len * dir;
        double widening = end_radius - src.R;
        double zlen    = dst.z() - gndlvl;
        double full_len = len + zlen;
        double r       = src.R + widening * len / full_len;
        return r;
    };

    static_assert(IsWideningFn<decltype(wfn)>, "Not a widening function");

    return deepsearch_ground_connection(policy, sm, source, wfn, init_dir);
}

struct DefaultWideningModel {
    static constexpr double WIDENING_SCALE = 0.02;
    const SupportableMesh &sm;

    double operator()(const Ball &src, const Vec3d & /*dir*/, double len) {
        double w = WIDENING_SCALE * sm.cfg.pillar_widening_factor * len;
        return std::max(src.R, sm.cfg.head_back_radius_mm) + w;
    }
};

template<> struct BeamSamples<DefaultWideningModel> {
    static constexpr size_t Value = 16;
};

// deepsearch_ground_connection with DefaultWideningModel
template<class Ex>
GroundConnection deepsearch_ground_connection(Ex                     policy,
                                              const SupportableMesh &sm,
                                              const Junction        &source,
                                              const Vec3d           &init_dir = DOWN)
{
    return deepsearch_ground_connection(policy, sm, source,
                                        DefaultWideningModel{sm}, init_dir);
}

/* ************************************************************************** */
/* calculate_anchor_placement                                                  */
/* ************************************************************************** */

template<class Ex>
bool optimize_anchor_placement(Ex                     policy,
                               const SupportableMesh &sm,
                               const Junction        &from,
                               Anchor                &anchor)
{
    Vec3d n = get_normal(sm.emesh, anchor.pos);

    auto [polar, azimuth] = dir_to_spheric(n);

    polar = std::min(polar, sm.cfg.bridge_slope);

    double lmin = 0;
    double lmax = std::min(sm.cfg.head_width_mm,
                           (from.pos - anchor.pos).norm() - 2 * from.r);

    double sd = sm.cfg.safety_distance(anchor.r_back_mm);

    Optimizer<AlgNLoptGenetic> solver(get_criteria(sm.cfg)
                                          .stop_score(anchor.fullwidth())
                                          .max_iterations(100));
    solver.seed(0);

    auto oresult = solver.to_max().optimize(
        [&sm, &anchor, sd, policy](const opt::Input<3> &input) {
            auto &[plr, azm, l] = input;
            auto dir = spheric_to_dir(plr, azm).normalized();
            anchor.width_mm = l;
            anchor.dir      = dir;
            return pinhead_mesh_hit(policy, sm.emesh, anchor, sd).distance();
        },
        initvals({polar, azimuth, (lmin + lmax) / 2.}),
        bounds({{0., sm.cfg.bridge_slope},
                {-PI, PI},
                {lmin, lmax}}));

    polar   = std::get<0>(oresult.optimum);
    azimuth = std::get<1>(oresult.optimum);
    anchor.dir      = spheric_to_dir(polar, azimuth).normalized();
    anchor.width_mm = std::get<2>(oresult.optimum);

    return oresult.score >= anchor.fullwidth();
}

template<class Ex>
std::optional<Anchor> calculate_anchor_placement(Ex                     policy,
                                                 const SupportableMesh &sm,
                                                 const Junction        &from,
                                                 const Vec3d           &to_hint)
{
    double back_r    = from.r;
    double pin_r     = sm.cfg.head_front_radius_mm;
    double penetr    = sm.cfg.head_penetration_mm;
    double hwidth    = sm.cfg.head_width_mm;
    Vec3d  anchordir = (from.pos - to_hint).normalized(); // pointing from mesh toward junction

    Anchor anchor(back_r, pin_r, hwidth, penetr, anchordir, to_hint);

    if (optimize_anchor_placement(policy, sm, from, anchor))
        return anchor;

    // Retry with fallback radius
    anchor.r_back_mm = sm.cfg.head_fallback_radius_mm;
    if (optimize_anchor_placement(policy, sm, from, anchor))
        return anchor;

    return {};
}

}} // namespace Slic3r::sla

#endif // SLASUPPORTTREEUTILS_HPP
