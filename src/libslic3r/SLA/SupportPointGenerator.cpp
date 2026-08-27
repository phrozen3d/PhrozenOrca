// Step A2: Rewritten to align with PrusaSlicer 2024 free-function architecture.
// Ports: NearPoints KD-tree, prepare_generator_data(), generate_support_points(),
//        move_on_mesh_surface() from PrusaSlicer/SLA/SupportPointGenerator.cpp.
//
// Key PhrozenOrca adaptations:
//   - SupportPointType::island / slope now used directly (is_new_island bool removed)
//   - AABBMesh -> sla::IndexedMesh  (same API: query_ray_hit + squared_distance)

#include "SupportPointGenerator.hpp"
#include "IndexedMesh.hpp"                               // move_on_mesh_surface

#include "libslic3r/Execution/ExecutionTBB.hpp"
#include "libslic3r/Execution/Execution.hpp"
#include "libslic3r/KDTreeIndirect.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/AABBTreeLines.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Line.hpp"
// SupportIslands
#include "libslic3r/SLA/SupportIslands/UniformSupportIsland.hpp"
#include "libslic3r/SLA/SupportIslands/SampleConfigFactory.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <mutex>
#include <numeric>
#include <optional>
#include <functional>

using namespace Slic3r;
using namespace Slic3r::sla;

namespace {

#ifndef NDEBUG
bool exist_point_in_distance(const Vec3f &p, float distance, const LayerSupportPoints &pts) {
    float distance_sq = sqr(distance);
    return std::any_of(pts.begin(), pts.end(), [&p, distance_sq](const LayerSupportPoint &sp) {
        return (sp.pos - p).squaredNorm() < distance_sq;
    });
}
#endif // NDEBUG

// ---------------------------------------------------------------------------
// NearPoints: KD-tree wrapper for fast nearest-support-point queries.
// Ported from PrusaSlicer/SLA/SupportPointGenerator.cpp.
// ---------------------------------------------------------------------------
class NearPoints
{
    struct PointAccessor {
        LayerSupportPoints *m_supports_ptr;
        explicit PointAccessor(LayerSupportPoints *supports_ptr) : m_supports_ptr(supports_ptr) {}
        const coord_t &operator()(size_t idx, size_t dimension) const {
            return m_supports_ptr->at(idx).position_on_layer[dimension];
        }
    };

    PointAccessor m_points;
    using Tree = KDTreeIndirect<2, coord_t, PointAccessor>;
    Tree m_tree;
    // PhrozenOrca adaptation: KDTreeIndirect here lacks get_nodes() / get_copy().
    // Track active indices explicitly instead.
    std::vector<size_t> m_indices;

public:
    explicit NearPoints(LayerSupportPoints *supports_ptr)
        : m_points(supports_ptr), m_tree(m_points) {}

    NearPoints get_copy() const {
        NearPoints copy(m_points.m_supports_ptr);
        copy.m_indices = m_indices;
        // PhrozenOrca fix: KDTreeIndirect::build() clears its argument vector.
        // Pass a copy so that copy.m_indices is preserved after the build.
        std::vector<size_t> tmp = copy.m_indices;
        copy.m_tree.build(tmp);
        return copy;
    }

    void remove_out_of(const ExPolygons &shapes, float current_z) {
        std::vector<size_t> indices = m_indices;
        auto it = std::remove_if(indices.begin(), indices.end(),
            [&pts = *m_points.m_supports_ptr, &shapes, current_z](size_t point_index) {
                const LayerSupportPoint &lsp = pts.at(point_index);
                if (lsp.is_permanent && lsp.pos.z() >= current_z)
                    return false;
                return !std::any_of(shapes.begin(), shapes.end(),
                    [&p = lsp.position_on_layer](const ExPolygon &shape) {
                        return shape.contains(p);
                    });
            });
        if (it == indices.end())
            return;
        indices.erase(it, indices.end());
        m_indices = indices;
        m_tree.clear();
        // PhrozenOrca fix: build() clears its argument; pass a copy to preserve m_indices.
        std::vector<size_t> tmp = m_indices;
        m_tree.build(tmp);
    }

    void add(LayerSupportPoint &&point) {
        LayerSupportPoints &pts = *m_points.m_supports_ptr;
        assert(!exist_point_in_distance(point.pos, point.head_front_radius, pts));
        size_t index = pts.size();
        pts.emplace_back(std::move(point));
        m_indices.push_back(index);
        m_tree.clear();
        // PhrozenOrca fix: build() clears its argument; pass a copy to preserve m_indices.
        std::vector<size_t> tmp = m_indices;
        m_tree.build(tmp);
    }

    using CheckFnc = std::function<bool(const LayerSupportPoint &, const Point &)>;

    bool exist_true_in_radius(const Point &pos, coord_t radius, const CheckFnc &fnc) const {
        std::vector<size_t> point_indices = find_nearby_points(m_tree, pos, radius);
        return std::any_of(point_indices.begin(), point_indices.end(),
            [&points = *m_points.m_supports_ptr, &pos, &fnc](size_t point_index) {
                return fnc(points.at(point_index), pos);
            });
    }

    void merge(NearPoints &&near_point) {
        assert(m_points.m_supports_ptr == near_point.m_points.m_supports_ptr);
        std::vector<size_t> indices = m_indices;
        indices.insert(indices.end(),
            near_point.m_indices.begin(),
            near_point.m_indices.end());
        std::sort(indices.begin(), indices.end());
        auto it = std::unique(indices.begin(), indices.end());
        indices.erase(it, indices.end());
        m_indices = std::move(indices);
        m_tree.clear();
        // PhrozenOrca fix: build() clears its argument; pass a copy to preserve m_indices.
        std::vector<size_t> tmp = m_indices;
        m_tree.build(tmp);
    }

    std::vector<size_t> get_indices() const {
        return m_indices;
    }
};
using NearPointss = std::vector<NearPoints>;

// ---------------------------------------------------------------------------
// Math helpers
// ---------------------------------------------------------------------------

// Intersection of a line segment and a circle.
// p1 must be inside, p2 outside (or on) the circle.
Point intersection_line_circle(const Point &p1, const Point &p2, const Point &cnt, double r2) {
    Vec2d dp_d((p2 - p1).cast<double>());
    Vec2d f_d((p1 - cnt).cast<double>());
    double a = dp_d.squaredNorm();
    double b = 2 * (f_d.x() * dp_d.x() + f_d.y() * dp_d.y());
    double c = f_d.squaredNorm() - r2;
    double discriminant = b * b - 4 * a * c;
    assert(discriminant >= 0);
    if (discriminant < 0)
        return {};
    discriminant = sqrt(discriminant);
    double t1 = (-b - discriminant) / (2 * a);
    if (t1 >= 0 && t1 <= 1)
        return {p1.x() + static_cast<coord_t>(t1 * dp_d.x()),
                p1.y() + static_cast<coord_t>(t1 * dp_d.y())};
    double t2 = (-b + discriminant) / (2 * a);
    if (t2 >= 0 && t2 <= 1 && t1 != t2)
        return {p1.x() + static_cast<coord_t>(t2 * dp_d.x()),
                p1.y() + static_cast<coord_t>(t2 * dp_d.y())};
    return {};
}

// ---------------------------------------------------------------------------
// Layer linking helpers
// ---------------------------------------------------------------------------

ExPolygons get_shapes(const PartLinks &part_links) {
    ExPolygons out;
    out.reserve(part_links.size());
    for (const PartLink &part_link : part_links)
        out.push_back(*part_link->shape);
    return out;
}

// Migrate NearPoints from previous layer into current part's grid.
NearPoints create_near_points(
    const LayerParts &prev_layer_parts,
    const LayerPart  &part,
    NearPointss      &prev_grids
) {
    const LayerParts::const_iterator &prev_part_it = part.prev_parts.front();
    size_t index_of_prev_part = prev_part_it - prev_layer_parts.begin();
    NearPoints near_points = (prev_part_it->next_parts.size() == 1) ?
        std::move(prev_grids[index_of_prev_part]) :
        prev_grids[index_of_prev_part].get_copy();

    for (size_t i = 1; i < part.prev_parts.size(); ++i) {
        const LayerParts::const_iterator &pp = part.prev_parts[i];
        size_t idx = pp - prev_layer_parts.begin();
        if (pp->next_parts.size() == 1) {
            near_points.merge(std::move(prev_grids[idx]));
        } else {
            NearPoints grid_ = prev_grids[idx].get_copy();
            near_points.merge(std::move(grid_));
        }
    }
    return near_points;
}

// ---------------------------------------------------------------------------
// Support generation: per-part processing
// ---------------------------------------------------------------------------

// Support overhang samples that are not covered by existing support points.
void support_part_overhangs(
    const LayerPart            &part,
    const SupportPointGeneratorConfig &config,
    NearPoints                 &near_points,
    float                       part_z,
    coord_t                     maximal_radius
) {
    NearPoints::CheckFnc is_supported = [](const LayerSupportPoint &sp, const Point &p) -> bool {
        coord_t r  = sp.current_radius;
        Point   dp = sp.position_on_layer - p;
        if (std::abs(dp.x()) > r) return false;
        if (std::abs(dp.y()) > r) return false;
        double r2 = sqr(static_cast<double>(r));
        return dp.cast<double>().squaredNorm() < r2;
    };

    for (const Point &p : part.samples) {
        if (!near_points.exist_true_in_radius(p, maximal_radius, is_supported)) {
            // Freeze the remaining Top fields at generation time too, not just
            // head_front_radius (fix-sla-support-auto-points-top-field-freeze).
            SupportPoint sp{
                Vec3f{unscale<float>(p.x()), unscale<float>(p.y()), part_z},
                /* head_front_radius */ config.head_diameter / 2,
                SupportPointType::slope
            };
            sp.head_back_radius_mm   = config.head_back_radius_mm;
            sp.head_width_mm         = config.head_width_mm;
            sp.head_penetration_mm   = config.head_penetration_mm;
            sp.contact_sphere_radius = config.contact_sphere_radius;
            near_points.add(LayerSupportPoint{
                std::move(sp),
                /* position_on_layer */ p,
                /* radius_curve_index */ 0,
                /* current_radius */ static_cast<coord_t>(scale_(config.support_curve.front().x()))
            });
        }
    }
}

// Support a completely new island using Voronoi Medial Axis.
void support_island(
    const LayerPart            &part,
    NearPoints                 &near_points,
    float                       part_z,
    const Points               &permanent,
    const SupportPointGeneratorConfig &cfg
) {
    SupportIslandPoints samples = uniform_support_island(*part.shape, permanent, cfg.island_configuration);
    for (const SupportIslandPointPtr &sample : samples) {
        // Freeze the remaining Top fields at generation time too, not just
        // head_front_radius (fix-sla-support-auto-points-top-field-freeze).
        SupportPoint sp{
            Vec3f{
                unscale<float>(sample->point.x()),
                unscale<float>(sample->point.y()),
                part_z
            },
            /* head_front_radius */ cfg.head_diameter / 2,
            SupportPointType::island
        };
        sp.head_back_radius_mm   = cfg.head_back_radius_mm;
        sp.head_width_mm         = cfg.head_width_mm;
        sp.head_penetration_mm   = cfg.head_penetration_mm;
        sp.contact_sphere_radius = cfg.contact_sphere_radius;
        near_points.add(LayerSupportPoint{
            std::move(sp),
            /* position_on_layer */ sample->point,
            /* radius_curve_index */ 0,
            /* current_radius */ static_cast<coord_t>(scale_(cfg.support_curve.front().x()))
        });
    }
}

// Support peninsula (partial overhang) using edge-constrained sampling.
void support_peninsulas(
    const Peninsulas            &peninsulas,
    NearPoints                  &near_points,
    float                        part_z,
    const Points                &permanent,
    const SupportPointGeneratorConfig &cfg
) {
    for (const Peninsula &peninsula : peninsulas) {
        SupportIslandPoints peninsula_supports =
            uniform_support_peninsula(peninsula, permanent, cfg.island_configuration);
        for (const SupportIslandPointPtr &support : peninsula_supports) {
            // Freeze the remaining Top fields at generation time too, not just
            // head_front_radius (fix-sla-support-auto-points-top-field-freeze).
            SupportPoint sp{
                Vec3f{
                    unscale<float>(support->point.x()),
                    unscale<float>(support->point.y()),
                    part_z
                },
                /* head_front_radius */ cfg.head_diameter / 2,
                SupportPointType::island
            };
            sp.head_back_radius_mm   = cfg.head_back_radius_mm;
            sp.head_width_mm         = cfg.head_width_mm;
            sp.head_penetration_mm   = cfg.head_penetration_mm;
            sp.contact_sphere_radius = cfg.contact_sphere_radius;
            near_points.add(LayerSupportPoint{
                std::move(sp),
                /* position_on_layer */ support->point,
                /* radius_curve_index */ 0,
                /* current_radius */ static_cast<coord_t>(scale_(cfg.support_curve.front().x()))
            });
        }
    }
}

// ---------------------------------------------------------------------------
// Overhang outline sampling (prepare_generator_data step)
// ---------------------------------------------------------------------------

// Uniformly sample a polyline [b, e) at squared distance dist2.
Slic3r::Points sample(Points::const_iterator b, Points::const_iterator e, double dist2) {
    assert(e - b >= 2);
    if (e - b < 2)
        return {};
    Slic3r::Points r;
    r.push_back(*b);
    const Point *prev_pt = nullptr;
    for (Points::const_iterator it = b; it + 1 < e; ++it) {
        const Point &pt = *(it + 1);
        double p_dist2 = (r.back() - pt).cast<double>().squaredNorm();
        while (p_dist2 > dist2) {
            if (prev_pt == nullptr)
                prev_pt = &(*it);
            r.push_back(intersection_line_circle(*prev_pt, pt, r.back(), dist2));
            p_dist2 = (r.back() - pt).cast<double>().squaredNorm();
            prev_pt = &r.back();
        }
        prev_pt = nullptr;
    }
    return r;
}

bool contain_point(const Point &p, const Points &sorted_points) {
    auto it = std::lower_bound(sorted_points.begin(), sorted_points.end(), p);
    if (it == sorted_points.end())
        return false;
    ++it;
    if (it == sorted_points.end())
        return false;
    return it->x() == p.x() && it->y() == p.y();
}

#ifndef NDEBUG
bool exist_same_points(const ExPolygon &shape, const Points &prev_points) {
    auto shape_points = to_points(shape);
    return shape_points.end() !=
        std::find_if(shape_points.begin(), shape_points.end(), [&prev_points](const Point &p) {
            return contain_point(p, prev_points);
        });
}
#endif // NDEBUG

// Sample the overhanging outline of a layer part.
Points sample_overhangs(const LayerPart &part, double dist2) {
    const ExPolygon &shape = *part.shape;
    ExPolygons prev_shapes = get_shapes(part.prev_parts);
    assert(!prev_shapes.empty());
    ExPolygons overhangs = diff_ex(shape, prev_shapes, ApplySafetyOffset::Yes);
    if (overhangs.empty())
        return {};

    Points prev_points = to_points(prev_shapes);
    std::sort(prev_points.begin(), prev_points.end());
    assert(!exist_same_points(shape, prev_points));

    auto sample_overhang = [&prev_points, dist2](const Polygon &polygon, Points &samples) {
        const Points &pts = polygon.points;
        Points::const_iterator first_bad = pts.end();
        Points::const_iterator start_it  = pts.end();
        for (auto it = pts.begin(); it != pts.end(); ++it) {
            const Point &p = *it;
            if (contain_point(p, prev_points)) {
                if (first_bad == pts.end())
                    first_bad = it;
                if (start_it != pts.end()) {
                    append(samples, sample(start_it, it, dist2));
                    start_it = pts.end();
                }
            } else if (start_it == pts.end()) {
                start_it = it;
            }
        }
        if (start_it == pts.end()) {
            if (first_bad != pts.begin())
                append(samples, sample(pts.begin(), first_bad, dist2));
        } else {
            if (first_bad == pts.begin()) {
                append(samples, sample(start_it, pts.end(), dist2));
            } else if (start_it == pts.begin()) {
                assert(first_bad == pts.end());
                Points pts2 = pts;
                pts2.push_back(pts.front());
                append(samples, sample(pts2.begin(), pts2.end(), dist2));
            } else {
                Points pts2;
                pts2.reserve((pts.end() - start_it) + (first_bad - pts.begin()));
                for (auto it = start_it; it < pts.end(); ++it)
                    pts2.push_back(*it);
                for (auto it = pts.begin(); it < first_bad; ++it)
                    pts2.push_back(*it);
                append(samples, sample(pts2.begin(), pts2.end(), dist2));
            }
        }
    };

    Points samples;
    for (const ExPolygon &overhang : overhangs) {
        sample_overhang(overhang.contour, samples);
        for (const Polygon &hole : overhang.holes)
            sample_overhang(hole, samples);
    }
    return samples;
}

// ---------------------------------------------------------------------------
// Support radius calculation
// ---------------------------------------------------------------------------

coord_t calc_influence_radius(float z_distance, const SupportPointGeneratorConfig &config) {
    float island_support_distance_sq = sqr(config.support_curve.front().x());
    if (!is_approx(config.density_relative, 1.f, 1e-4f))
        island_support_distance_sq /= config.density_relative;
    float z_distance_sq = sqr(z_distance);
    if (z_distance_sq >= island_support_distance_sq)
        return 0;
    return static_cast<coord_t>(scale_(std::sqrt(island_support_distance_sq - z_distance_sq)));
}

void prepare_supports_for_layer(LayerSupportPoints &supports, float layer_z,
    const NearPointss &activ_points, const SupportPointGeneratorConfig &config)
{
    auto set_radius = [&config](LayerSupportPoint &support, float radius) {
        if (!is_approx(config.density_relative, 1.f, 1e-4f))
            radius = std::sqrt(sqr(radius) / config.density_relative);
        support.current_radius = static_cast<coord_t>(scale_(radius));
    };

    std::vector<bool> is_active(supports.size(), false);
    for (const NearPoints &pts : activ_points) {
        for (size_t i : pts.get_indices())
            is_active[i] = true;
    }

    const std::vector<Vec2f> &curve = config.support_curve;
    for (LayerSupportPoint &support : supports) {
        size_t &index = support.radius_curve_index;
        if (index + 1 >= curve.size())
            continue;
        if (!is_active[&support - &supports.front()])
            continue;

        float diff_z = layer_z - support.pos.z();
        if (diff_z < 0.) {
            support.current_radius = calc_influence_radius(-diff_z, config);
            continue;
        }
        while ((index + 1) < curve.size() && diff_z > curve[index + 1].y())
            ++index;
        if ((index + 1) >= curve.size()) {
            set_radius(support, curve.back().x());
            continue;
        }
        Vec2f a = curve[index];
        Vec2f b = curve[index + 1];
        assert(a.y() <= diff_z && diff_z <= b.y());
        float t = (diff_z - a.y()) / (b.y() - a.y());
        assert(0 <= t && t <= 1);
        set_radius(support, a.x() + t * (b.x() - a.x()));
    }
}

void remove_supports_out_of_part(NearPoints &near_points, const LayerPart &part, float current_z) {
    near_points.remove_out_of(part.extend_shape, current_z);
}

// ---------------------------------------------------------------------------
// Peninsula detection (prepare_generator_data step)
// ---------------------------------------------------------------------------

void create_peninsulas(LayerPart &part, const PrepareSupportConfig &config) {
    assert(config.peninsula_min_width > config.peninsula_self_supported_width);
    const ExPolygons below_shapes   = get_shapes(part.prev_parts);
    const ExPolygons below_expanded = offset_ex(below_shapes, config.peninsula_min_width, ClipperLib::jtSquare);
    const ExPolygon &part_shape     = *part.shape;
    ExPolygons over_peninsula        = diff_ex(part_shape, below_expanded);
    if (over_peninsula.empty())
        return;

    ExPolygons below_self_supported = offset_ex(below_shapes, config.peninsula_self_supported_width, ClipperLib::jtSquare);
    // NOTE: weird edge case where expand returns empty - no assert here.

    ExPolygons peninsulas_shape = diff_ex(part_shape, below_self_supported);
    Lines below_lines = to_lines(below_self_supported);

    auto get_angle = [](const Line &l) {
        Point diff = l.b - l.a;
        if (diff.x() < 0)
            diff = -diff;
        return atan2(diff.y(), diff.x());
    };

    std::vector<double> below_line_angle;
    below_line_angle.reserve(below_lines.size());
    for (const Line &l : below_lines)
        below_line_angle.push_back(get_angle(l));

    std::vector<size_t> idx(below_lines.size());
    std::iota(idx.begin(), idx.end(), 0);
    auto is_lower = [&below_line_angle](size_t i1, size_t i2) {
        return below_line_angle[i1] < below_line_angle[i2];
    };
    std::sort(idx.begin(), idx.end(), is_lower);

    auto exist_below = [&get_angle, &idx, &below_lines, &below_line_angle](const Line &l) {
        if (below_lines.empty())
            return false;
        const double angle_epsilon   = 1e-3;
        const double parallel_epsilon = scale_(1e-2);
        double angle    = get_angle(l);
        double low_angle = angle - angle_epsilon;
        bool is_over = false;
        if (low_angle <= -M_PI_2) {
            low_angle += M_PI;
            is_over = true;
        }
        double hi_angle = angle + angle_epsilon;
        if (hi_angle >= M_PI_2) {
            hi_angle -= M_PI;
            is_over = true;
        }
        int majority_idx = 0;
        if (Point d = l.a - l.b; std::abs(d.x()) < std::abs(d.y()))
            majority_idx = 1;
        coord_t low  = l.a[majority_idx];
        coord_t high = l.b[majority_idx];
        if (low > high)
            std::swap(low, high);

        auto is_lower_angle = [&below_line_angle](size_t index, double angle_) {
            return below_line_angle[index] < angle_;
        };
        auto it_idx = std::lower_bound(idx.begin(), idx.end(), low_angle, is_lower_angle);
        if (it_idx == idx.end()) {
            if (is_over) {
                it_idx   = idx.begin();
                is_over  = false;
            } else {
                return false;
            }
        }
        while (is_over || below_line_angle[*it_idx] < hi_angle) {
            const Line &l2 = below_lines[*it_idx];
            coord_t l2_low  = l2.a[majority_idx];
            coord_t l2_high = l2.b[majority_idx];
            if (l2_low > l2_high)
                std::swap(l2_low, l2_high);
            if ((l2_high >= low && l2_low <= high) &&
                (((l2.a == l.a && l2.b == l.b) || (l2.a == l.b && l2.b == l.a)) ||
                  l.perp_distance_to(l2.a) < parallel_epsilon))
                return true;
            ++it_idx;
            if (it_idx == idx.end()) {
                if (is_over) {
                    it_idx  = idx.begin();
                    is_over = false;
                } else {
                    break;
                }
            }
        }
        return false;
    };

    for (const ExPolygon &peninsula : peninsulas_shape) {
        if (intersection_ex(ExPolygons{peninsula}, over_peninsula).empty())
            continue;
        Lines lines = to_lines(peninsula);
        std::vector<bool> is_outline(lines.size());
        for (size_t i = 0; i < lines.size(); i++)
            is_outline[i] = !exist_below(lines[i]);
        part.peninsulas.push_back(Peninsula{peninsula, is_outline});
    }
}

// ---------------------------------------------------------------------------
// Small part detection and removal (prepare_generator_data step)
// ---------------------------------------------------------------------------

struct LayerPartIndex {
    size_t layer_index;
    size_t part_index;
    bool operator<(const LayerPartIndex &o) const {
        return layer_index < o.layer_index ||
               (layer_index == o.layer_index && part_index < o.part_index);
    }
    bool operator==(const LayerPartIndex &o) const {
        return layer_index == o.layer_index && part_index == o.part_index;
    }
};
using SmallPart  = std::vector<LayerPartIndex>;
using SmallParts = std::vector<SmallPart>;

std::optional<SmallPart> create_small_part(
    const Layers &layers, const LayerPartIndex &island, float radius_in_mm)
{
    const LayerPart &part  = layers[island.layer_index].parts[island.part_index];
    coord_t radius         = static_cast<coord_t>(scale_(radius_in_mm));
    assert(part.prev_parts.empty());
    assert(part.shape_extent.size().x() <= 2 * radius &&
           part.shape_extent.size().y() <= 2 * radius);

    Point center    = part.shape_extent.center();
    Point range{radius, radius};
    BoundingBox range_bb{center - range, center + range};

    std::function<bool(const LayerPartIndex &, size_t, const LayerPartIndex &)> check_parts;
    check_parts = [&range_bb, &check_parts, &layers, &island, radius_in_mm]
    (const LayerPartIndex &check, size_t allowed_depth, const LayerPartIndex &prev_check) -> bool {
        const Layer    &check_layer = layers[check.layer_index];
        const LayerPart &check_part = check_layer.parts[check.part_index];
        for (const PartLink &link : check_part.next_parts)
            if (!range_bb.contains(link->shape_extent.min) ||
                !range_bb.contains(link->shape_extent.max))
                return false;
        if ((check_layer.print_z - layers[island.layer_index].print_z) > radius_in_mm)
            return false;
        if (--allowed_depth == 0)
            return true;
        size_t next_layer_i = check.layer_index + 1;
        for (const PartLink &link : check_part.next_parts) {
            size_t next_part_i = link - layers[next_layer_i].parts.cbegin();
            if (next_layer_i == prev_check.layer_index &&
                next_part_i  == prev_check.part_index)
                continue;
            if (!check_parts({next_layer_i, next_part_i}, allowed_depth, check))
                return false;
        }
        if (check.layer_index == island.layer_index) {
            if (!check_part.prev_parts.empty())
                return false;
            if (check.part_index < island.part_index)
                return false;
        }
        for (const PartLink &link : check_part.prev_parts) {
            if (!range_bb.contains(link->shape_extent.min) ||
                !range_bb.contains(link->shape_extent.max))
                return false;
        }
        for (const PartLink &link : check_part.prev_parts) {
            size_t prev_layer_i = check.layer_index - 1;
            size_t prev_part_i  = link - layers[prev_layer_i].parts.cbegin();
            if (prev_layer_i == prev_check.layer_index &&
                prev_part_i  == prev_check.part_index)
                continue;
            if (!check_parts({prev_layer_i, prev_part_i}, allowed_depth, check))
                return false;
        }
        return true;
    };

    float layer_height = (island.layer_index == 0) ?
        layers[1].print_z - layers[0].print_z :
        layers[island.layer_index].print_z - layers[island.layer_index - 1].print_z;
    assert(layer_height > 0.f);
    float  safe_mult   = 1.4f;
    size_t allowed_depth = static_cast<size_t>(
        std::ceil((radius_in_mm / layer_height + 1) * safe_mult));

    if (!check_parts(island, allowed_depth, island))
        return {};

    SmallPart collected;
    std::vector<LayerPartIndex> queue_next;
    LayerPartIndex curr = island;
    do {
        if (curr.layer_index >= layers.size()) {
            if (queue_next.empty())
                break;
            curr = queue_next.back();
            queue_next.pop_back();
        }
        auto collected_it = std::lower_bound(collected.begin(), collected.end(), curr);
        if (collected_it != collected.end() && *collected_it == curr)
            continue;
        collected.insert(collected_it, curr);

        const LayerPart &curr_part = layers[curr.layer_index].parts[curr.part_index];
        LayerPartIndex next{layers.size(), 0};
        for (const PartLink &link : curr_part.next_parts) {
            size_t next_layer_i = curr.layer_index + 1;
            size_t part_i       = link - layers[next_layer_i].parts.begin();
            LayerPartIndex next_{next_layer_i, part_i};
            auto it = std::lower_bound(collected.begin(), collected.end(), next_);
            if (it != collected.end() && *it == next_)
                continue;
            if (next.layer_index >= layers.size())
                next = next_;
            else
                queue_next.push_back(next_);
        }
        for (const PartLink &link : curr_part.prev_parts) {
            size_t prev_layer_i = curr.layer_index - 1;
            size_t part_i       = link - layers[prev_layer_i].parts.begin();
            LayerPartIndex next_{prev_layer_i, part_i};
            auto it = std::lower_bound(collected.begin(), collected.end(), next_);
            if (it != collected.end() && *it == next_)
                continue;
            if (next.layer_index >= layers.size())
                next = next_;
            else
                queue_next.push_back(next_);
        }
        curr = next;
    } while (true);

    float print_z = layers[island.layer_index].print_z;
    for (const LayerPartIndex &part_id : collected) {
        const Layer &layer = layers[part_id.layer_index];
        double radius_sq = (sqr(radius_in_mm) - sqr(layer.print_z - print_z)) / sqr(SCALING_FACTOR);
        const LayerPart &layer_part = layer.parts[part_id.part_index];
        for (const Point &p : layer_part.shape->contour.points) {
            Vec2d diff2d = (p - center).cast<double>();
            if (sqr(diff2d.x()) + sqr(diff2d.y()) > radius_sq)
                return {};
        }
    }
    return collected;
}

SmallParts get_small_parts(const Layers &layers, float radius_in_mm) {
    coord_t diameter = static_cast<coord_t>(2 * scale_(radius_in_mm));
    std::vector<LayerPartIndex> islands;
    for (size_t layer_i = 0; layer_i < layers.size(); ++layer_i) {
        const Layer &layer = layers[layer_i];
        for (size_t part_i = 0; part_i < layer.parts.size(); ++part_i) {
            const LayerPart &part = layer.parts[part_i];
            if (!part.prev_parts.empty())
                continue;
            if (const Point size = part.shape_extent.size();
                size.x() > diameter || size.y() > diameter)
                continue;
            islands.push_back({layer_i, part_i});
        }
    }
    std::mutex m;
    SmallParts result;
    execution::for_each(ex_tbb, size_t(0), islands.size(),
    [&layers, radius_in_mm, &islands, &result, &m](size_t island_i) {
        std::optional<SmallPart> sp = create_small_part(layers, islands[island_i], radius_in_mm);
        if (!sp.has_value())
            return;
        std::lock_guard lock(m);
        result.push_back(*sp);
    }, 8);
    return result;
}

void erase(const SmallParts &small_parts, Layers &layers) {
    std::vector<LayerPartIndex> to_erase;
    for (const SmallPart &sp : small_parts)
        to_erase.insert(to_erase.end(), sp.begin(), sp.end());

    auto cmp = [](const LayerPartIndex &a, const LayerPartIndex &b) {
        return a.layer_index < b.layer_index ||
               (a.layer_index == b.layer_index && a.part_index > b.part_index);
    };
    std::sort(to_erase.begin(), to_erase.end(), cmp);
    assert(std::unique(to_erase.begin(), to_erase.end()) == to_erase.end());

    size_t erase_to;
    for (size_t erase_from = 0; erase_from < to_erase.size(); erase_from = erase_to) {
        erase_to = erase_from + 1;
        size_t layer_index = to_erase[erase_from].layer_index;
        while (erase_to < to_erase.size() &&
               to_erase[erase_to].layer_index == layer_index)
            ++erase_to;

        Layer &layer = layers[layer_index];
        LayerParts layer_parts = layer.parts; // copy
        std::swap(layer_parts, layer.parts);  // swap copy in

        for (size_t i = erase_from; i < erase_to; ++i)
            layer.parts.erase(layer.parts.begin() + to_erase[i].part_index);

        if (layer_index > 0) {
            Layer &prev_layer = layers[layer_index - 1];
            for (LayerPart &prev_part : prev_layer.parts) {
                for (PartLink &next_part : prev_part.next_parts) {
                    size_t part_i = next_part - layer_parts.cbegin();
                    for (size_t i = erase_from; i < erase_to; ++i)
                        if (part_i >= to_erase[i].part_index)
                            --part_i;
                    assert(part_i < layer.parts.size());
                    next_part = layer.parts.begin() + part_i;
                }
            }
        }
        if (layer_index < layers.size() - 1) {
            Layer &next_layer = layers[layer_index + 1];
            for (LayerPart &next_part : next_layer.parts) {
                for (PartLink &prev_part : next_part.prev_parts) {
                    size_t part_i = prev_part - layer_parts.cbegin();
                    for (size_t i = erase_from; i < erase_to; ++i)
                        if (part_i >= to_erase[i].part_index)
                            --part_i;
                    assert(part_i < layer.parts.size());
                    prev_part = layer.parts.begin() + part_i;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Permanent support point processing
// ---------------------------------------------------------------------------

size_t get_index_of_closest_part(
    const Point &coor, const LayerParts &parts, double max_allowed_distance_sq)
{
    size_t count_lines = 0;
    std::vector<size_t> part_lines_ends;
    part_lines_ends.reserve(parts.size());
    for (const LayerPart &part : parts) {
        count_lines += count_points(*part.shape);
        part_lines_ends.push_back(count_lines);
    }
    Linesf lines;
    lines.reserve(count_lines);
    for (const LayerPart &part : parts)
        append(lines, to_linesf(ExPolygons{*part.shape}));
    AABBTreeIndirect::Tree<2, double> tree =
        AABBTreeLines::build_aabb_tree_over_indexed_lines(lines);

    size_t line_idx = std::numeric_limits<size_t>::max();
    Vec2d coor_d    = coor.cast<double>();
    Vec2d hit_point;
    [[maybe_unused]] double distance_sq =
        AABBTreeLines::squared_distance_to_indexed_lines(lines, tree, coor_d, line_idx, hit_point);

    if (distance_sq >= max_allowed_distance_sq)
        return parts.size();

    for (size_t part_index = 0; part_index < part_lines_ends.size(); ++part_index)
        if (line_idx < part_lines_ends[part_index]) {
            assert(union_ex(
                get_shapes(parts[part_index].prev_parts),
                get_shapes(parts[part_index].next_parts))[0].contains(coor));
            return part_index;
        }
    assert(false);
    return parts.size();
}

MinMax<float> get_layer_range(const Layers &layers, size_t layer_id) {
    assert(layer_id < layers.size());
    if (layer_id >= layers.size())
        return MinMax<float>{0.f, 0.f};
    float print_z = layers[layer_id].print_z;
    float min = (layer_id == 0) ? 0.f : (layers[layer_id - 1].print_z + print_z) / 2.f;
    float max = ((layer_id + 1) < layers.size()) ?
        (layers[layer_id + 1].print_z + print_z) / 2.f :
        print_z + (print_z - min);
    return MinMax<float>{min, max};
}

size_t get_index_of_layer_part(
    const Point &coor, const LayerParts &parts, double max_allowed_distance_sq)
{
    size_t part_index = parts.size();
    for (const LayerPart &part : parts) {
        if (part.shape_extent.contains(coor) && part.shape->contains(coor)) {
            assert(part_index >= parts.size());
            part_index = &part - &parts.front();
        }
    }
    if (part_index >= parts.size())
        part_index = get_index_of_closest_part(coor, parts, max_allowed_distance_sq);
    return part_index;
}

LayerParts::const_iterator get_closest_part(const PartLinks &links, Vec2d &coor) {
    if (links.size() == 1)
        return links.front();
    Point coor_p = coor.cast<coord_t>();
    for (const PartLink &link : links) {
        LayerParts::const_iterator part_it = link;
        if (part_it->shape_extent.contains(coor_p) &&
            part_it->shape->contains(coor_p))
            return part_it;
    }
    size_t count_lines = 0;
    std::vector<size_t> part_lines_ends;
    part_lines_ends.reserve(links.size());
    for (const PartLink &link : links) {
        count_lines += count_points(*link->shape);
        part_lines_ends.push_back(count_lines);
    }
    Linesf lines;
    lines.reserve(count_lines);
    for (const PartLink &link : links)
        append(lines, to_linesf(ExPolygons{*link->shape}));
    AABBTreeIndirect::Tree<2, double> tree =
        AABBTreeLines::build_aabb_tree_over_indexed_lines(lines);
    size_t line_idx = std::numeric_limits<size_t>::max();
    Vec2d hit_point;
    [[maybe_unused]] double distance_sq =
        AABBTreeLines::squared_distance_to_indexed_lines(lines, tree, coor, line_idx, hit_point);
    for (size_t part_index = 0; part_index < part_lines_ends.size(); ++part_index) {
        if (line_idx >= part_lines_ends[part_index])
            continue;
        assert(union_ex(
            get_shapes(links[part_index]->prev_parts),
            get_shapes(links[part_index]->next_parts))[0].contains(coor.cast<coord_t>()));
        coor = hit_point;
        return links[part_index];
    }
    assert(false);
    return links.front();
}

struct PartId { size_t layer_id; size_t part_id; };

PartId get_index_of_first_influence(
    const PartId &partid,
    const SupportPoint &p,
    const Point &coor,
    const Layers &layers,
    const SupportPointGeneratorConfig &config)
{
    float max_influence_distance = std::max(
        2 * p.head_front_radius, config.support_curve.front().x());
    const LayerParts &parts = layers[partid.layer_id].parts;
    LayerParts::const_iterator current_part_it = parts.cbegin() + partid.part_id;
    LayerParts::const_iterator prev_part_it    = current_part_it;
    Vec2d coor_d = coor.cast<double>();

    auto get_part_id = [&layers](size_t layer_index, const LayerParts::const_iterator &part_it) {
        const LayerParts &parts = layers[layer_index].parts;
        size_t part_index = part_it - parts.cbegin();
        assert(part_index < parts.size());
        return PartId{layer_index, part_index};
    };

    for (size_t i = 0; i <= partid.layer_id; ++i) {
        size_t current_layer_id = partid.layer_id - i;
        const Layer &layer      = layers[current_layer_id];
        float z_distance        = p.pos.z() - layer.print_z;
        if (z_distance >= max_influence_distance)
            return get_part_id(current_layer_id, current_part_it);
        const PartLinks &prev_parts = current_part_it->prev_parts;
        if (prev_parts.empty()) {
            return (z_distance < p.head_front_radius) ?
                get_part_id(current_layer_id, current_part_it) :
                get_part_id(current_layer_id + 1, prev_part_it);
        }
        prev_part_it    = current_part_it;
        current_part_it = get_closest_part(prev_parts, coor_d);
    }
    assert(false);
    return PartId{std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max()};
}

struct PermanentSupport {
    SupportPoints::const_iterator point_it;
    PartId influence;
    PartId part;
    Point  layer_position;
};
using PermanentSupports = std::vector<PermanentSupport>;

PermanentSupports prepare_permanent_supports(
    const SupportPoints &permanent_supports,
    const Layers &layers,
    const SupportPointGeneratorConfig &config)
{
    if (permanent_supports.empty())
        return {};
    assert(std::is_sorted(permanent_supports.begin(), permanent_supports.end(),
        [](const SupportPoint &a, const SupportPoint &b) { return a.pos.z() < b.pos.z(); }));

    size_t permanent_index = 0;
    PermanentSupports result;
    for (size_t layer_id = 0; layer_id < layers.size(); ++layer_id) {
        float layer_max_z = get_layer_range(layers, layer_id).max;
        if (permanent_index >= permanent_supports.size())
            break;
        if (permanent_supports[permanent_index].pos.z() >= layer_max_z)
            continue;
        const Layer &layer = layers[layer_id];
        for (; permanent_index < permanent_supports.size(); ++permanent_index) {
            SupportPoints::const_iterator point_it = permanent_supports.begin() + permanent_index;
            if (point_it->pos.z() > layer_max_z)
                break;
            Point coor(static_cast<coord_t>(scale_(point_it->pos.x())),
                       static_cast<coord_t>(scale_(point_it->pos.y())));
            double allowed_distance_sq = std::max(config.max_allowed_distance_sq,
                sqr(scale_(point_it->head_front_radius)));
            size_t part_index = get_index_of_layer_part(coor, layer.parts, allowed_distance_sq);
            if (part_index >= layer.parts.size())
                continue;
            PartId part_id{layer_id, part_index};
            PartId influence = get_index_of_first_influence(part_id, *point_it, coor, layers, config);
            result.push_back(PermanentSupport{point_it, influence, part_id, coor});
        }
    }
    std::sort(result.begin(), result.end(), [](const PermanentSupport &s1, const PermanentSupport &s2) {
        return s1.influence.layer_id != s2.influence.layer_id ?
            s1.influence.layer_id < s2.influence.layer_id :
            s1.influence.part_id  < s2.influence.part_id;
    });
    return result;
}

bool exist_permanent_support(
    const PermanentSupports &supports, size_t current_support_index,
    size_t layer_index, size_t part_index)
{
    if (current_support_index >= supports.size())
        return false;
    const PartId &influence = supports[current_support_index].influence;
    assert(influence.layer_id >= layer_index);
    return influence.layer_id == layer_index && influence.part_id == part_index;
}

void copy_permanent_supports(
    NearPoints &near_points, const PermanentSupports &supports,
    size_t &support_index, float print_z,
    size_t layer_index, size_t part_index,
    const SupportPointGeneratorConfig &config)
{
    while (exist_permanent_support(supports, support_index, layer_index, part_index)) {
        const PermanentSupport &support = supports[support_index];
        near_points.add(LayerSupportPoint{
            /* SupportPoint */       *support.point_it,
            /* position_on_layer */  support.layer_position,
            /* radius_curve_index */ 0,
            /* current_radius */     calc_influence_radius(std::fabs(support.point_it->pos.z() - print_z), config),
            /* active_in_part */     true,
            /* is_permanent */       true
        });
        ++support_index;
    }
}

Points get_permanents(
    const PermanentSupports &supports, size_t support_index,
    size_t layer_index, size_t part_index)
{
    Points result;
    while (exist_permanent_support(supports, support_index, layer_index, part_index)) {
        result.push_back(supports[support_index].layer_position);
        ++support_index;
    }
    return result;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public free functions  (namespace Slic3r::sla)
// ---------------------------------------------------------------------------

namespace Slic3r {
namespace sla {

std::vector<Vec2f> create_default_support_curve() {
    return std::vector<Vec2f>{
        Vec2f{3.2f,  0.f},
        Vec2f{4.f,   3.9f},
        Vec2f{5.f,  15.f},
        Vec2f{6.f,  40.f},
    };
}

SampleConfig create_default_island_configuration(float head_diameter_in_mm) {
    return SampleConfigFactory::create(head_diameter_in_mm);
}

SupportPointGeneratorData prepare_generator_data(
    std::vector<ExPolygons> &&slices,
    const std::vector<float> &heights,
    const PrepareSupportConfig &config,
    ThrowOnCancel throw_on_cancel,
    StatusFunction statusfn
) {
    assert(!slices.empty());
    assert(slices.size() == heights.size());
    if (slices.empty() || slices.size() != heights.size())
        return SupportPointGeneratorData{};

    SupportPointGeneratorData result;
    result.slices = std::move(slices);
    result.layers = Layers(result.slices.size());

    // Build LayerParts from slices (parallel).
    execution::for_each(ex_tbb, size_t(0), result.slices.size(),
    [&result, &heights, throw_on_cancel](size_t layer_id) {
        if ((layer_id % 128) == 0)
            throw_on_cancel();
        Layer &layer = result.layers[layer_id];
        layer.print_z = heights[layer_id];
        const ExPolygons &islands = result.slices[layer_id];
        layer.parts.reserve(islands.size());
        for (const ExPolygon &island : islands)
            layer.parts.push_back(LayerPart{
                &island, {},
                get_extents(island.contour)
            });
    }, 4);

    // Link parts between adjacent layers (parallel).
    execution::for_each(ex_tbb, size_t(1), result.slices.size(),
    [&result, throw_on_cancel](size_t layer_id) {
        if ((layer_id % 16) == 0)
            throw_on_cancel();
        LayerParts &parts_above = result.layers[layer_id].parts;
        LayerParts &parts_below = result.layers[layer_id - 1].parts;
        for (auto it_above = parts_above.begin(); it_above < parts_above.end(); ++it_above) {
            for (auto it_below = parts_below.begin(); it_below < parts_below.end(); ++it_below) {
                if (!it_above->shape_extent.overlap(it_below->shape_extent))
                    continue;
                Polygons polys = intersection(*it_above->shape, *it_below->shape);
                if (polys.empty())
                    continue;
                it_above->prev_parts.push_back(it_below);
                it_below->next_parts.push_back(it_above);
            }
        }
    }, 8);

    // Remove unsupportable tiny model parts.
    SmallParts small_parts = get_small_parts(result.layers, config.minimal_bounding_sphere_radius);
    if (!small_parts.empty())
        erase(small_parts, result.layers);

    // Sample overhang outlines (parallel).
    double sample_dist_um2 = sqr(scale_(config.discretize_overhang_step));
    execution::for_each(ex_tbb, size_t(1), result.layers.size(),
    [&result, sample_dist_um2, throw_on_cancel](size_t layer_id) {
        if ((layer_id % 32) == 0)
            throw_on_cancel();
        LayerParts &parts = result.layers[layer_id].parts;
        for (auto it_part = parts.begin(); it_part < parts.end(); ++it_part) {
            if (it_part->prev_parts.empty())
                continue;
            it_part->samples = sample_overhangs(*it_part, sample_dist_um2);
        }
    }, 8);

    // Detect peninsula overhangs (parallel).
    execution::for_each(ex_tbb, size_t(1), result.layers.size(),
    [&layers = result.layers, &config, throw_on_cancel](size_t layer_id) {
        if ((layer_id % 32) == 0)
            throw_on_cancel();
        LayerParts &parts = layers[layer_id].parts;
        for (auto it_part = parts.begin(); it_part < parts.end(); ++it_part) {
            if (it_part->prev_parts.empty())
                continue;
            create_peninsulas(*it_part, config);
        }
    }, 8);

    // Compute extended shapes for support point invalidation (parallel).
    execution::for_each(ex_tbb, size_t(1), result.layers.size(),
    [&layers = result.layers, delta = config.removing_delta, throw_on_cancel](size_t layer_id) {
        if ((layer_id % 16) == 0)
            throw_on_cancel();
        LayerParts &parts = layers[layer_id].parts;
        for (auto it_part = parts.begin(); it_part < parts.end(); ++it_part)
            it_part->extend_shape = offset_ex(*it_part->shape, delta, ClipperLib::jtSquare);
    }, 8);

    return result;
}

LayerSupportPoints generate_support_points(
    const SupportPointGeneratorData    &data,
    const SupportPointGeneratorConfig  &config,
    ThrowOnCancel throw_on_cancel,
    StatusFunction statusfn
) {
    const Layers &layers = data.layers;
    double increment  = 100.0 / static_cast<double>(layers.size());
    double status     = 0.;
    int    status_int = 0;

    double max_support_radius = config.support_curve.back().x();
    coord_t maximal_radius    = static_cast<coord_t>(scale_(max_support_radius));

    LayerSupportPoints result;

    size_t permanent_index = 0;
    PermanentSupports permanent_supports =
        prepare_permanent_supports(data.permanent_supports, layers, config);

    NearPointss prev_grids;
    for (size_t layer_id = 0; layer_id < layers.size(); ++layer_id) {
        const Layer &layer = layers[layer_id];
        prepare_supports_for_layer(result, layer.print_z, prev_grids, config);

        NearPointss grids;
        grids.reserve(layer.parts.size());

        for (const LayerPart &part : layer.parts) {
            size_t part_id = &part - &layer.parts.front();
            if (part.prev_parts.empty()) {
                // New island: sample using Voronoi Medial Axis.
                grids.emplace_back(&result);
                Points permanent = get_permanents(permanent_supports, permanent_index, layer_id, part_id);
                support_island(part, grids.back(), layer.print_z, permanent, config);
                copy_permanent_supports(
                    grids.back(), permanent_supports, permanent_index,
                    layer.print_z, layer_id, part_id, config);
                continue;
            }

            assert(layer_id != 0);
            const LayerParts &prev_layer_parts = layers[layer_id - 1].parts;
            NearPoints near_points = create_near_points(prev_layer_parts, part, prev_grids);
            remove_supports_out_of_part(near_points, part, layer.print_z);
            assert(!near_points.get_indices().empty());

            if (!part.peninsulas.empty()) {
                Points permanent = get_permanents(permanent_supports, permanent_index, layer_id, part_id);
                support_peninsulas(part.peninsulas, near_points, layer.print_z, permanent, config);
            }
            copy_permanent_supports(
                near_points, permanent_supports, permanent_index,
                layer.print_z, layer_id, part_id, config);
            support_part_overhangs(part, config, near_points, layer.print_z, maximal_radius);
            grids.push_back(std::move(near_points));
        }
        prev_grids = std::move(grids);

        throw_on_cancel();

        int old_status_int = status_int;
        status    += increment;
        status_int = static_cast<int>(std::round(status));
        if (old_status_int < status_int)
            statusfn(status_int);
    }

    // Remove permanent supports from result.
    // Their 3D positions are preserved by appending after move_on_mesh_surface.
    result.erase(
        std::remove_if(result.begin(), result.end(),
            [](const LayerSupportPoint &p) { return p.is_permanent; }),
        result.end());
    return result;
}

SupportPoints move_on_mesh_surface(
    const LayerSupportPoints &points,
    const IndexedMesh        &mesh,
    double                    allowed_move,
    ThrowOnCancel             throw_on_cancel
) {
    SupportPoints pts;
    pts.reserve(points.size());
    for (const LayerSupportPoint &p : points)
        pts.push_back(static_cast<SupportPoint>(p));

    execution::for_each(
        ex_tbb, size_t(0), pts.size(),
        [&pts, &mesh, &throw_on_cancel, allowed_move](size_t idx) {
            if ((idx % 16) == 0)
                throw_on_cancel();
            Vec3f &p = pts[idx].pos;
            Vec3d p_double = p.cast<double>();
            const Vec3d up_vec(0., 0., 1.);
            const Vec3d down_vec(0., 0., -1.);

            IndexedMesh::hit_result hit_up   = mesh.query_ray_hit(p_double, up_vec);
            IndexedMesh::hit_result hit_down = mesh.query_ray_hit(p_double, down_vec);

            bool up   = hit_up.is_hit();
            bool down = hit_down.is_hit();
            if (!up && !down)
                return;

            // fix-sla-thin-model-support-points (#1): prefer the downward-facing
            // face over the merely nearest one.
            //
            // UPSTREAM: this defect is present in PrusaSlicer 2.9.6 as well --
            // the nearest-hit rule below (kept verbatim as tiers 2 and 3) is
            // upstream's, not a fork divergence. This fix is fork-local. On a
            // future rebase, check whether upstream has since introduced a
            // directional rule of its own before re-applying this hunk.
            //
            // A support point means "hold the model up from below", so the face
            // it must land on is the one whose material sits above it -- a
            // downward-facing face -- regardless of distance. Picking purely by
            // distance breaks on very thin plates: when the sampling layer falls
            // above the plate's mid-plane, the *top* surface is nearer, every
            // point gets snapped to z = thickness, and the resulting (0,0,+1)
            // normals are then discarded wholesale by the critical-angle filter
            // in SLAPrint::Steps::support_points(). Result: 0 automatic support
            // points, and the outcome flips purely on the slicing-grid phase.
            //
            // hit_result::normal() comes from IndexedMesh::normal_by_face_id(),
            // i.e. it is the triangle's *geometric* face normal. It is NOT
            // flipped to oppose the ray direction, so hitting the top surface
            // from inside the material still reports (0,0,+1) and is correctly
            // rejected here.
            //
            // Three tiers:
            //   1. exactly one hit faces downward -> take it
            //   2. both face downward            -> take the nearer one
            //   3. neither faces downward        -> take the nearer one
            // Tiers 2 and 3 are the pre-change rule, so ordinary geometry (where
            // the downward hit is also the nearer one) is unchanged point for
            // point. Tier 3 is a deliberate compatibility exit for vertical
            // walls and degenerate faces.
            //
            // is_hit() must be checked before normal(): a miss leaves m_normal
            // unset while is_valid() still holds.
            //
            // One knock-on effect of tier 1: when the downward-facing hit is the
            // farther one AND exceeds allowed_move while the nearer hit does
            // not, this now falls through to the squared_distance branch rather
            // than projecting onto the nearer (wrong-facing) hit. That branch
            // snaps to the geometrically closest surface point, so the outcome
            // is comparable but not bit-identical to the old one. It can only
            // arise for points more than allowed_move away from the mesh, and
            // support points are generated on layer slices, i.e. already on the
            // surface -- so the case is not expected in practice.
            const bool up_faces_down   = up   && hit_up.normal().z()   < 0.;
            const bool down_faces_down = down && hit_down.normal().z() < 0.;

            IndexedMesh::hit_result &nearest =
                (!down || hit_up.distance() < hit_down.distance()) ? hit_up : hit_down;

            IndexedMesh::hit_result &hit =
                (up_faces_down != down_faces_down)
                    ? (up_faces_down ? hit_up : hit_down)   // tier 1
                    : nearest;                              // tiers 2 and 3
            if (hit.distance() <= allowed_move) {
                p[2] += static_cast<float>(hit.distance() * hit.direction()[2]);
                return;
            }

            int   triangle_index;
            Vec3d closest_point;
            double distance = mesh.squared_distance(p_double, triangle_index, closest_point);
            if (distance <= std::numeric_limits<float>::epsilon())
                return;
            p = closest_point.cast<float>();
        },
        64);
    return pts;
}

// Remove support points below a given Z level.
void remove_bottom_points(std::vector<SupportPoint> &pts, float lvl) {
    auto endit = std::remove_if(pts.begin(), pts.end(),
        [lvl](const sla::SupportPoint &sp) { return sp.pos.z() <= lvl; });
    pts.erase(endit, pts.end());
}

} // namespace sla
} // namespace Slic3r
