#ifndef SLA_CONCAVEHULL_HPP
#define SLA_CONCAVEHULL_HPP

#include <libslic3r/ExPolygon.hpp>

namespace Slic3r {
namespace sla {

inline Polygons get_contours(const ExPolygons &poly)
{
    Polygons ret; ret.reserve(poly.size());
    for (const ExPolygon &p : poly) ret.emplace_back(p.contour);

    return ret;
}

using ThrowOnCancel = std::function<void()>;

/// Tag selecting the optional edge-gap raft-splitting construction path
/// (pad_split_rafts). Disambiguates the constructor overload below.
struct EdgeGapMerge {};

/// A fake concave hull that is constructed by connecting separate shapes
/// with explicit bridges. Bridges are generated from each shape's centroid
/// to the center of the "scene" which is the centroid calculated from the shape
/// centroids (a star is created...)
class ConcaveHull {
    Polygons m_polys;

    static Point centroid(const Points& pp);

    static inline Point centroid(const Polygon &poly) { return poly.centroid(); }

    Points calculate_centroids() const;

    void merge_polygons();

    void add_connector_rectangles(const Points &centroids,
                                  coord_t       max_dist,
                                  ThrowOnCancel thr);
public:

    ConcaveHull(const ExPolygons& polys, double merge_dist, ThrowOnCancel thr)
        : ConcaveHull{to_polygons(polys), merge_dist, thr} {}

    ConcaveHull(const Polygons& polys, double mergedist, ThrowOnCancel thr);

    /// Edge-gap raft-splitting path (pad_split_rafts=true). Islands whose real
    /// contour gap exceeds the threshold stay as separate rafts. raw_gap_threshold
    /// is the scaled raw-contour gap threshold (already folded with 2*waffle_offset,
    /// see design D-2). bridge_width is the scaled controlled-bridge width used to
    /// join island pairs within the threshold (nearest-point quad with penetration).
    ConcaveHull(const ExPolygons& islands, coord_t raw_gap_threshold, coord_t bridge_width, EdgeGapMerge, ThrowOnCancel thr);

    const Polygons & polygons() const { return m_polys; }

    ExPolygons to_expolygons() const;
};

ExPolygons offset_waffle_style_ex(const ConcaveHull &ccvhull, coord_t delta);
Polygons   offset_waffle_style(const ConcaveHull &polys, coord_t delta);

}}     // namespace Slic3r::sla
#endif // CONCAVEHULL_HPP
