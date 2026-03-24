#ifndef SLA_SUPPORTPOINTGENERATOR_HPP
#define SLA_SUPPORTPOINTGENERATOR_HPP

// Step A1: Rewritten to align with PrusaSlicer 2024 free-function architecture.
// Removes old class SupportPointGenerator (class-based, Poisson disk).
// Adds new structs + free functions (Voronoi Medial Axis + NearPoints KD-tree).

#include <vector>
#include <functional>

#include <boost/container/small_vector.hpp>

#include "libslic3r/Point.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/SLA/SupportPoint.hpp"
#include "libslic3r/SLA/SupportIslands/SampleConfig.hpp"  // PrepareSupportConfig, SampleConfig

namespace Slic3r { namespace sla {

class IndexedMesh; // forward declaration for move_on_mesh_surface()

// Factory functions for default configuration values.
std::vector<Vec2f> create_default_support_curve();
SampleConfig create_default_island_configuration(float head_diameter_in_mm);

/// Configuration for automatic support placement (replaces old Config struct).
struct SupportPointGeneratorConfig {
    // Density multiplier:
    //   0   = only one support point per island
    //   < 1 = fewer support points
    //   1   = fine-tuned default sampling
    //   > 1 = denser support points
    float density_relative {1.f};

    // Size of the support point interface (head) [in mm]
    float head_diameter = 0.4f;

    // Maximum distance to the nearest support point, defined as a curve.
    // X axis = XY distance on layer [mm], Y axis = Z height difference [mm].
    std::vector<Vec2f> support_curve = create_default_support_curve();

    // Configuration for sampling islands
    SampleConfig island_configuration = create_default_island_configuration(head_diameter);

    // Maximal allowed distance to a layer part for permanent (manually placed)
    // support points. Helps identify unwanted points during auto generation.
    double max_allowed_distance_sq = scale_(1) * scale_(1); // 1mm
};

struct LayerPart; // forward decl.
using LayerParts = std::vector<LayerPart>;

using PartLink = LayerParts::const_iterator;
#ifdef NDEBUG
// In release mode, use the optimized container.
using PartLinks = boost::container::small_vector<PartLink, 4>;
#else
// In debug mode, use the standard vector, which is well handled by debugger visualizer.
using PartLinks = std::vector<PartLink>;
#endif

// Large one-layer overhang connected on one side to already-supported geometry.
// Treated as a special island that only needs support along the overhanging edge.
struct Peninsula {
    // Part of the layer that is unsupported (treated as an island).
    ExPolygon unsuported_area;

    // Flag per line of unsuported_area (same size as to_lines(unsuported_area)).
    // True  = peninsula outline (coast side — needs support).
    // False = connection to land (already supported by the previous layer).
    std::vector<bool> is_outline;
};
using Peninsulas = std::vector<Peninsula>;

// One part of a layer, defined by its ExPolygon shape.
struct LayerPart {
    // Pointer to the ExPolygon stored in SupportPointGeneratorData::slices.
    const ExPolygon *shape;

    // Extended shape used to detect irrelevant support points
    // (points that fall outside the influence of this part).
    ExPolygons extend_shape;

    // Axis-aligned bounding box of shape.
    BoundingBox shape_extent;

    // Uniformly sampled contour points of shape.
    Points samples;

    // Parts from the previous printed layer connected to (overlapping) this part.
    PartLinks prev_parts;
    // Parts from the next printed layer connected to this part.
    PartLinks next_parts;

    // Peninsula-style partial overhangs that require edge-constrained sampling.
    Peninsulas peninsulas;
};

// Extended SupportPoint carrying per-layer generation metadata.
struct LayerSupportPoint : public SupportPoint {
    // 2D position on the layer [scaled unit].
    // Valid only when the point belongs to an active LayerPart.
    Point position_on_layer;

    // Index into SupportPointGeneratorConfig::support_curve for O(1) radius lookup.
    size_t radius_curve_index = 0;
    coord_t current_radius = 0; // [scaled mm]

    // False when no LayerPart exists within radius 'r' of this support point
    // on the current layer. Used to allow multiple coverage of overlapping overhangs.
    bool active_in_part = true;

    // True = point was placed manually by the user (permanent support).
    // Its 3D position is not modified by the algorithm.
    bool is_permanent = false;
};
using LayerSupportPoints = std::vector<LayerSupportPoint>;

// One slice layer divided into its constituent LayerParts.
struct Layer {
    // Absolute height from print bed [in mm].
    float print_z;

    // One LayerPart per ExPolygon in this slice.
    LayerParts parts;
};
using Layers = std::vector<Layer>;

// Pre-computed data for support point generation.
// Populated once by prepare_generator_data() during the slice_model step,
// then reused by generate_support_points() without re-slicing the mesh.
struct SupportPointGeneratorData {
    // Input slices of the mesh (moved in from slice_model output).
    std::vector<ExPolygons> slices;

    // Layer data with inter-layer connectivity.
    // NOTE: LayerPart::shape pointers reference ExPolygons stored in slices.
    Layers layers;

    // Manually placed support points that must be preserved as-is.
    SupportPoints permanent_supports;
};

// Function type: called periodically during generation to check for cancel.
using ThrowOnCancel = std::function<void(void)>;
// Function type: called to report generation progress to the GUI (range 0..100).
using StatusFunction = std::function<void(int)>;

// Configuration for the prepare_generator_data() preparation step.
struct PrepareGeneratorDataConfig {
    // Discretisation step for overhang outline sampling [in mm].
    // Smaller values are slower but more precise.
    double discretize_overhang_sample_in_mm = 2.;

    // Minimum overhang width to be treated as a peninsula [in scaled mm].
    coord_t peninsula_width = scale_(2.);
};

/// <summary>
/// Prepare data for support point generation.
/// Must be called at the end of the slice_model step (when generate_support is true).
/// Re-run only when the mesh changes or slicing heights change.
/// </summary>
/// <param name="slices">Contour cuts from the mesh (consumed by move).</param>
/// <param name="heights">Z heights of the slices — same count as slices.</param>
/// <param name="config">Preparation parameters.</param>
/// <param name="throw_on_cancel">Called periodically to check cancellation.</param>
/// <param name="statusfn">Reports progress to the GUI (0..100).</param>
/// <returns>Data ready for use in generate_support_points().</returns>
SupportPointGeneratorData prepare_generator_data(
    std::vector<ExPolygons> &&slices,
    const std::vector<float> &heights,
    const PrepareSupportConfig &config = {},
    ThrowOnCancel throw_on_cancel = []() {},
    StatusFunction statusfn = [](int) {}
);

/// <summary>
/// Generate support points on islands using pre-computed layer data.
/// Can be called repeatedly with different config without re-slicing.
/// </summary>
/// <param name="data">Preprocessed data from prepare_generator_data().</param>
/// <param name="config">Density and sampling configuration.</param>
/// <param name="throw_on_cancel">Called periodically to check cancellation.</param>
/// <param name="statusfn">Reports progress to the GUI (0..100).</param>
/// <returns>Generated support points with layer metadata.</returns>
LayerSupportPoints generate_support_points(
    const SupportPointGeneratorData &data,
    const SupportPointGeneratorConfig &config,
    ThrowOnCancel throw_on_cancel = []() {},
    StatusFunction statusfn = [](int) {}
);

/// <summary>
/// Project support points from their layer plane onto the actual mesh surface.
/// PhrozenOrca adaptation: uses IndexedMesh instead of PrusaSlicer's AABBMesh.
/// Both share the same query_ray_hit() + squared_distance() API.
/// </summary>
/// <param name="points">Layer support points to project.</param>
/// <param name="mesh">Mesh surface to project onto.</param>
/// <param name="allowed_move">Maximum allowed XY displacement during projection [mm].</param>
/// <param name="throw_on_cancel">Called periodically to check cancellation.</param>
/// <returns>Support points lying on the mesh surface.</returns>
SupportPoints move_on_mesh_surface(
    const LayerSupportPoints &points,
    const IndexedMesh &mesh,
    double allowed_move,
    ThrowOnCancel throw_on_cancel = []() {}
);

// Remove support points below a given Z level.
// Called by SLAPrintSteps after auto-generation to exclude base-plate points.
void remove_bottom_points(std::vector<SupportPoint> &pts, float lvl);

}} // namespace Slic3r::sla

#endif // SLA_SUPPORTPOINTGENERATOR_HPP
