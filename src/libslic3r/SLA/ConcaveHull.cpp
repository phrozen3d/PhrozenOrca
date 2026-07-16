#include <libslic3r/SLA/ConcaveHull.hpp>
#include <libslic3r/SLA/SpatIndex.hpp>

#include <libslic3r/MTUtils.hpp>
#include <libslic3r/ClipperUtils.hpp>
#include <libslic3r/AABBTreeLines.hpp>

#include <boost/log/trivial.hpp>

#include <set>
#include <cmath>
#include <limits>
#include <algorithm>

namespace Slic3r {
namespace sla {

inline Vec3d to_vec3(const Vec2crd &v2) { return {double(v2(X)), double(v2(Y)), 0.}; }
inline Vec3d to_vec3(const Vec2d &v2) { return {v2(X), v2(Y), 0.}; }
inline Vec2crd to_vec2(const Vec3d &v3) { return {coord_t(v3(X)), coord_t(v3(Y))}; }

Point ConcaveHull::centroid(const Points &pp)
{
    Point c;
    switch(pp.size()) {
    case 0: break;
    case 1: c = pp.front(); break;
    case 2: c = (pp[0] + pp[1]) / 2; break;
    default: {
        auto MAX = std::numeric_limits<Point::coord_type>::max();
        auto MIN = std::numeric_limits<Point::coord_type>::min();
        Point min = {MAX, MAX}, max = {MIN, MIN};

        for(auto& p : pp) {
            if(p(0) < min(0)) min(0) = p(0);
            if(p(1) < min(1)) min(1) = p(1);
            if(p(0) > max(0)) max(0) = p(0);
            if(p(1) > max(1)) max(1) = p(1);
        }
        c(0) = min(0) + (max(0) - min(0)) / 2;
        c(1) = min(1) + (max(1) - min(1)) / 2;
        break;
    }
    }

    return c;
}

Points ConcaveHull::calculate_centroids() const
{
    // We get the centroids of all the islands in the 2D slice
    Points centroids;
    centroids.reserve(m_polys.size());
    std::transform(m_polys.begin(), m_polys.end(),
                   std::back_inserter(centroids),
                   [](const Polygon &poly) { return centroid(poly); });

    return centroids;
}

void ConcaveHull::merge_polygons() { m_polys = get_contours(union_ex(m_polys)); }

void ConcaveHull::add_connector_rectangles(const Points &centroids,
                                           coord_t       max_dist,
                                           ThrowOnCancel thr)
{
    // Centroid of the centroids of islands. This is where the additional
    // connector sticks are routed.
    Point cc = centroid(centroids);

    PointIndex ctrindex;
    unsigned  idx = 0;
    for(const Point &ct : centroids) ctrindex.insert(to_vec3(ct), idx++);

    m_polys.reserve(m_polys.size() + centroids.size());

    idx = 0;
    for (const Point &c : centroids) {
        thr();

        double dx = c.x() - cc.x(), dy = c.y() - cc.y();
        double l  = std::sqrt(dx * dx + dy * dy);
        double nx = dx / l, ny = dy / l;

        const Point &ct = centroids[idx];

        std::vector<PointIndexEl> result = ctrindex.nearest(to_vec3(ct), 2);

        double dist = max_dist;
        for (const PointIndexEl &el : result)
            if (el.second != idx) {
                dist = Line(to_vec2(el.first), ct).length();
                break;
            }

        idx++;

        if (dist >= max_dist) return;

        Polygon r;
        r.points.reserve(3);
        r.points.emplace_back(cc);

        Point n(scaled(nx), scaled(ny));
        r.points.emplace_back(c + Point(n.y(), -n.x()));
        r.points.emplace_back(c + Point(-n.y(), n.x()));
        offset(r, scaled<float>(1.));

        m_polys.emplace_back(r);
    }
}

ConcaveHull::ConcaveHull(const Polygons &polys, double mergedist, ThrowOnCancel thr)
{
    if(polys.empty()) return;

    m_polys = polys;
    merge_polygons();

    if(m_polys.size() == 1) return;

    Points centroids = calculate_centroids();

    add_connector_rectangles(centroids, scaled(mergedist), thr);

    merge_polygons();
}

namespace {

// A candidate merge between two islands (i < j) with the nearest point pair
// (pa on island i, pb on island j), the raw-contour shortest gap g_raw, and the
// unit contour normal at the closest feature (used for the g->0 guard, A3).
struct MergePair {
    size_t i, j;
    Point  pa, pb;
    double g_raw;
    Vec2d  normal;
};

// Unit normal of a contour edge (perpendicular to the edge direction). Used as
// the fallback bridge axis when the nearest point pair degenerates (g->0, A3).
inline Vec2d edge_normal(const Line &e)
{
    Vec2d ev = (e.b - e.a).cast<double>();
    double l = ev.norm();
    if (l < 1e-9) return Vec2d(0., 0.);
    ev /= l;
    return Vec2d(-ev.y(), ev.x());
}

// Returns the island pairs that should be joined by a controlled bridge:
//   1. broad phase  -- BoxIndex, bboxes expanded by raw_thresh -> candidate pairs
//   2. narrow phase -- per-island LinesDistancer, bidirectional vertex->edge,
//                      keeping pairs whose raw-contour gap is within raw_thresh,
//                      along with the nearest point pair and contour normal
//   3. cross-island filter -- drop bridges passing through a third island
// Islands are assumed disjoint simple polygons (holes already discarded); for
// such polygons the minimum distance is always realized at a vertex->edge pair,
// so the bidirectional query is exact.
//
// Determinism (A6) -- the result must not depend on container or query order:
//   * candidates live in a std::set<pair<i,j>>, so they are always visited in
//     (i, j) order regardless of the order BoxIndex returns its hits in;
//   * narrow-phase ties use a strict `d < best`, so the first hit wins: within a
//     pair the lower island index (the i-loop) wins, and within an island the
//     lower vertex index wins;
//   * the returned vector is sorted by (i, j), and the caller appends the bridge
//     geometry in that same order before the final union;
//   * the whole pass is sequential -- if it is ever parallelised, the results
//     must be re-sorted by (i, j) before the union to keep this guarantee.
std::vector<MergePair> find_merge_pairs(const Polygons     &islands,
                                        coord_t             raw_thresh,
                                        const ThrowOnCancel &thr)
{
    std::vector<MergePair> out;
    const size_t n = islands.size();
    if (n < 2) return out;

    // --- broad phase: index unexpanded bboxes, query with bbox expanded by
    //     raw_thresh; boxes within raw_thresh (each axis) => candidate pair. ---
    BoxIndex bindex;
    std::vector<BoundingBox> bboxes(n);
    for (size_t i = 0; i < n; ++i) {
        bboxes[i] = islands[i].bounding_box();
        bindex.insert(bboxes[i], unsigned(i));
    }

    std::set<std::pair<size_t, size_t>> candidates;
    for (size_t i = 0; i < n; ++i) {
        BoundingBox q = bboxes[i];
        q.min -= Point(raw_thresh, raw_thresh);
        q.max += Point(raw_thresh, raw_thresh);
        for (const BoxIndexEl &el : bindex.query(q, BoxIndex::qtIntersects)) {
            size_t j = el.second;
            if (j > i) candidates.emplace(i, j);   // ordered, unique
        }
    }

    // --- narrow phase: build each island's LinesDistancer once (cached). ---
    std::vector<AABBTreeLines::LinesDistancer<Line>> distancers(n);
    std::vector<char> built(n, 0);
    auto distancer_of = [&](size_t k) -> AABBTreeLines::LinesDistancer<Line> & {
        if (!built[k]) {
            distancers[k] = AABBTreeLines::LinesDistancer<Line>(islands[k].lines());
            built[k] = 1;
        }
        return distancers[k];
    };

    std::vector<MergePair> accepted;
    accepted.reserve(candidates.size());

    const double raw_thresh_d = double(raw_thresh);
    for (const auto &c : candidates) {
        thr();
        const size_t i = c.first, j = c.second;
        auto &di = distancer_of(i);
        auto &dj = distancer_of(j);

        double best = std::numeric_limits<double>::infinity();
        Point  best_pa, best_pb;

        Vec2d best_normal(0., 0.);

        // island i vertices vs island j edges
        for (const Point &v : islands[i].points) {
            auto [d, ei, np] = dj.distance_from_lines_extra<false>(v);
            if (d < best) {
                best = d;
                best_pa = v;
                best_pb = Point(coord_t(std::llround(np.x())), coord_t(std::llround(np.y())));
                best_normal = edge_normal(dj.get_line(ei));
            }
        }
        // island j vertices vs island i edges
        for (const Point &v : islands[j].points) {
            auto [d, ei, np] = di.distance_from_lines_extra<false>(v);
            if (d < best) {
                best = d;
                best_pb = v;
                best_pa = Point(coord_t(std::llround(np.x())), coord_t(std::llround(np.y())));
                best_normal = edge_normal(di.get_line(ei));
            }
        }

        if (best <= raw_thresh_d)
            accepted.push_back(MergePair{i, j, best_pa, best_pb, best, best_normal});
    }

    // --- cross-island filter (design D-6): drop a bridge whose segment passes
    //     through a third island. AABB is only a broad-phase pre-filter picking
    //     which islands to test; the decision is made by exact segment vs island
    //     contour intersection, because an AABB-only test is too loose (a long
    //     diagonal island's box would swallow unrelated bridges) and would delete
    //     necessary bridges, breaking a group apart.
    //     Correctness: if bridge A-B really crosses island C, then C lies between
    //     A and B, so gap(A,C) and gap(C,B) are both < gap(A,B) <= threshold, and
    //     A,B stay connected through C. Dropping A-B loses no connectivity.
    //     No MST / union-find pruning is applied (5.2): every pair within the
    //     threshold that does not cross a third island is kept, so natural closed
    //     loops (e.g. a ring raft keeping its central hole) are allowed. MST would
    //     cut a ring into a C shape, whose uneven post-cure shrinkage warps.
    for (const MergePair &mp : accepted) {
        thr();

        const Line  seg(mp.pa, mp.pb);
        BoundingBox segbb(Points{mp.pa, mp.pb});

        bool crosses_third = false;
        for (const BoxIndexEl &el : bindex.query(segbb, BoxIndex::qtIntersects)) {
            const size_t k = el.second;
            if (k == mp.i || k == mp.j) continue;   // endpoints legitimately touch i and j
            if (!distancer_of(k).intersections_with_line<false>(seg).empty()) {
                crosses_third = true;
                break;
            }
        }

        if (!crosses_third) out.push_back(mp);
    }

    std::sort(out.begin(), out.end(), [](const MergePair &a, const MergePair &b) {
        return a.i != b.i ? a.i < b.i : a.j < b.j;
    });

    return out;
}

// Build the controlled connector quad between the nearest point pair. The quad
// is `width` wide (perpendicular to `dir`) and each end is extended by a
// penetration (pen_i into island i behind pa, pen_j into island j past pb) so
// the bridge overlaps both island bodies -- Clipper union then leaves no
// vertex-only sliver (breakstick_holes spirit, design D-4).
inline Polygon make_bridge_quad(const Point &pa, const Point &pb,
                                const Vec2d &dir, coord_t width,
                                coord_t pen_i, coord_t pen_j)
{
    const Vec2d  n(-dir.y(), dir.x());          // perpendicular unit
    const double hw = 0.5 * double(width);
    const Vec2d  pae = pa.cast<double>() - double(pen_i) * dir;  // into island i
    const Vec2d  pbe = pb.cast<double>() + double(pen_j) * dir;  // into island j

    auto P = [](const Vec2d &v) {
        return Point(coord_t(std::llround(v.x())), coord_t(std::llround(v.y())));
    };

    Polygon q;
    q.points = { P(pae + hw * n), P(pbe + hw * n), P(pbe - hw * n), P(pae - hw * n) };
    q.make_counter_clockwise();
    return q;
}

} // namespace

ConcaveHull::ConcaveHull(const ExPolygons &islands,
                         coord_t           raw_gap_threshold,
                         coord_t           bridge_width,
                         EdgeGapMerge,
                         ThrowOnCancel     thr)
{
    if (islands.empty()) return;

    // Merge overlapping support/model contours into disjoint island groups.
    // Measure on outer contours only (holes are irrelevant to edge gaps).
    m_polys = to_polygons(islands);
    merge_polygons();

    if (m_polys.size() <= 1) return;

    // Decide which island pairs to join: within the finished-gap threshold and
    // not crossing a third island. Pairs beyond the threshold get no bridge and
    // stay as separate rafts; the rest are joined by a controlled connector below.
    std::vector<MergePair> pairs = find_merge_pairs(m_polys, raw_gap_threshold, thr);

    BOOST_LOG_TRIVIAL(debug)
        << "sla split-rafts: " << m_polys.size() << " islands, "
        << pairs.size() << " bridge(s) after cross-island filter";

    if (pairs.empty()) return;   // all islands beyond threshold -> stay separate

    // Per-island size (min bbox dimension) for the tiny-island clamp (A4).
    std::vector<coord_t> minsize(m_polys.size());
    for (size_t k = 0; k < m_polys.size(); ++k) {
        Point s = m_polys[k].bounding_box().size();
        minsize[k] = std::min(s.x(), s.y());
    }

    // Default penetration is the (unclamped) bridge width: self-scaling and deep
    // enough to guarantee overlap with the island body.
    const coord_t pen_base = bridge_width;
    constexpr double g0_eps = 1.0;   // scaled: ~sub-micron, treats g_raw~0 as degenerate

    Polygons bridges;
    bridges.reserve(pairs.size());
    for (const MergePair &mp : pairs) {
        thr();

        // Bridge axis, with the g->0 divide-by-zero guard (A3): when the nearest
        // points nearly coincide, fall back to the local contour normal.
        Vec2d  dv  = (mp.pb - mp.pa).cast<double>();
        double len = dv.norm();
        Vec2d  dir = (len < g0_eps) ? mp.normal : (dv / len);
        double dl  = dir.norm();
        if (dl < 1e-9) dir = Vec2d(1., 0.);   // ultimate fallback
        else           dir /= dl;

        // Tiny-island clamp (A4): keep the bridge from eating through a small
        // island. Width capped by the smaller island; each penetration capped by
        // half of its own island's size.
        coord_t w = std::min<coord_t>(bridge_width, std::min(minsize[mp.i], minsize[mp.j]));
        if (w <= 0) w = bridge_width;
        coord_t pen_i = std::min<coord_t>(pen_base, minsize[mp.i] / 2);
        coord_t pen_j = std::min<coord_t>(pen_base, minsize[mp.j] / 2);

        bridges.emplace_back(make_bridge_quad(mp.pa, mp.pb, dir, w, pen_i, pen_j));
    }

    // Union islands + bridges (deterministic: bridges appended in sorted-pair
    // order). Keep outer contours only, matching the legacy path.
    Polygons all = m_polys;
    all.insert(all.end(), bridges.begin(), bridges.end());
    m_polys = get_contours(union_ex(all));
}

ExPolygons ConcaveHull::to_expolygons() const
{
    auto ret = reserve_vector<ExPolygon>(m_polys.size());
    for (const Polygon &p : m_polys) ret.emplace_back(ExPolygon(p));
    return ret;
}

ExPolygons offset_waffle_style_ex(const ConcaveHull &hull, coord_t delta)
{
    return to_expolygons(offset_waffle_style(hull, delta));
}

Polygons offset_waffle_style(const ConcaveHull &hull, coord_t delta)
{
    auto arc_tolerance = scaled<double>(0.01);
    Polygons res = closing(hull.polygons(), 2 * delta, delta, ClipperLib::jtRound, arc_tolerance);

    auto it = std::remove_if(res.begin(), res.end(), [](Polygon &p) { return p.is_clockwise(); });
    res.erase(it, res.end());

    return res;
}

}} // namespace Slic3r::sla
