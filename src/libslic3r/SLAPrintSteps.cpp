#include <algorithm>
#include <unordered_set>

#include <libslic3r/Exception.hpp>
#include <libslic3r/SLAPrintSteps.hpp>
#include <libslic3r/MeshBoolean.hpp>
#include <libslic3r/CSGMesh/ModelToCSGMesh.hpp>
#include <libslic3r/CSGMesh/SliceCSGMesh.hpp>
#include <libslic3r/CSGMesh/PerformCSGMeshBooleans.hpp>
#include <libslic3r/TriangleMeshSlicer.hpp>

// Need the cylinder method for the the drainholes in hollowing step
#include <libslic3r/SLA/SupportTreeBuilder.hpp>

#include <libslic3r/SLA/Concurrency.hpp>
#include <libslic3r/SLA/Pad.hpp>
#include <libslic3r/SLA/SupportPointGenerator.hpp>
#include <libslic3r/SLA/SupportTreeBuildsteps.hpp>   // dir_to_spheric
#include <libslic3r/SLA/IndexedMesh.hpp>              // sla::normals
#include "libslic3r/SLA/SupportIslands/SampleConfigFactory.hpp"

#include <libslic3r/ElephantFootCompensation.hpp>
#include <libslic3r/SLA/ZCorrection.hpp>
#include <libslic3r/Format/SLAArchiveWriter.hpp>
#include <libslic3r/Format/PhrozenPRZ.hpp>
#include <libslic3r/Format/PhrozenPRZOrient.hpp>
#include <libslic3r/AABBTreeIndirect.hpp>
#include <libslic3r/SLA/RasterToCvMat.hpp>
#include <libslic3r/SLA/RasterCache.hpp>
#include <opencv2/imgproc.hpp>   // cv::resize for preview thumbnail downsample

#include <libslic3r/ClipperUtils.hpp>

#include <atomic>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <tbb/partitioner.h>
#include <tbb/task_arena.h>
#include <tbb/enumerable_thread_specific.h>

#include <boost/log/trivial.hpp>

#include "I18N.hpp"

//! macro used to mark string used at localization,
//! return same string
#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

namespace {

const std::array<unsigned, slaposCount> OBJ_STEP_LEVELS = {
    13, // slaposAssembly,
    10, // slaposHollowing,
    10, // slaposDrillHoles
    10, // slaposObjectSlice,
    20, // slaposSupportPoints,
    10, // slaposSupportTree,
    10, // slaposPad,
    30, // slaposSliceSupports,
};

std::string OBJ_STEP_LABELS(size_t idx)
{
    switch (idx) {
    case slaposAssembly:             return L("Assembling model from parts");
    case slaposHollowing:            return L("Hollowing model");
    case slaposDrillHoles:           return L("Drilling holes into model");
    case slaposObjectSlice:          return L("Slicing model");
    case slaposSupportPoints:        return L("Generating support points");
    case slaposSupportTree:          return L("Generating support tree");
    case slaposPad:                  return L("Generating pad");
    case slaposSliceSupports:        return L("Slicing supports");
    default:;
    }
    assert(false);
    return "Out of bounds!";
}

const std::array<unsigned, slapsCount> PRINT_STEP_LEVELS = {
    10, // slapsMergeSlicesAndEval
    90, // slapsRasterize
};

std::string PRINT_STEP_LABELS(size_t idx)
{
    switch (idx) {
    case slapsMergeSlicesAndEval:   return L("Merging slices and calculating statistics");
    case slapsRasterize:            return L("Rasterizing layers");
    default:;
    }
    assert(false); return "Out of bounds!";
}

} // anonymous namespace

static void clear_csg(std::multiset<CSGPartForStep> &s, SLAPrintObjectStep step)
{
    CSGPartForStep dummy{step};
    auto r = s.equal_range(dummy);
    s.erase(r.first, r.second);
}

struct csg_inserter {
    std::multiset<CSGPartForStep> &m;
    SLAPrintObjectStep key;

    csg_inserter &operator*() { return *this; }
    void operator=(csg::CSGPart &&part)
    {
        part.its_ptr.convert_unique_to_shared();
        m.emplace(key, std::move(part));
    }
    csg_inserter &operator++() { return *this; }
};

indexed_triangle_set SLAPrint::Steps::generate_preview_vdb(SLAPrintObject & /*po*/,
                                                            SLAPrintObjectStep /*step*/)
{
    // VDB voxelization fallback is not available in this build.
    // Returns empty mesh; the preview will be missing but slicing is unaffected.
    return {};
}

void SLAPrint::Steps::generate_preview(SLAPrintObject &po, SLAPrintObjectStep step)
{
    auto r = range(po.m_mesh_to_slice);
    indexed_triangle_set m;
    bool handled = false;

    if (csg::is_all_positive(r)) {
        m       = csg::csgmesh_merge_positive_parts(r);
        handled = true;
    } else {
        auto [reason, msg, it] = csg::check_csgmesh_booleans(r);
        if (it == r.end()) { // all parts passed boolean eligibility check
            MeshBoolean::cgal::CGALMeshPtr cgalmeshptr;
            try {
                cgalmeshptr = csg::perform_csgmesh_booleans(r);
            } catch (...) {}

            if (cgalmeshptr) {
                m       = MeshBoolean::cgal::cgal_to_indexed_triangle_set(*cgalmeshptr);
                handled = true;
            }
        }
    }

    if (!handled) {
        po.active_step_add_warning(
            PrintStateBase::WarningLevel::NON_CRITICAL,
            L("Some parts of the print will be previewed with approximated meshes. "
              "This does not affect the quality of slices or the physical print in any way."));
        m = generate_preview_vdb(po, step);
    }

    po.m_preview_meshes[step] =
        std::make_shared<const indexed_triangle_set>(std::move(m));

    for (size_t i = size_t(step) + 1; i < slaposCount; ++i)
        po.m_preview_meshes[i] = {};

    using namespace std::string_literals;
    report_status(-2, "Reload preview from step "s + std::to_string(int(step)),
                  SlicingStatus::RELOAD_SLA_PREVIEW);
}

void SLAPrint::Steps::mesh_assembly(SLAPrintObject &po)
{
    po.m_mesh_to_slice.clear();
    po.m_supportdata.reset();
    po.m_hollowing_data.reset();

    csg::model_to_csgmesh(*po.model_object(), po.trafo(),
                          csg_inserter{po.m_mesh_to_slice, slaposAssembly},
                          csg::mpartsPositive | csg::mpartsNegative | csg::mpartsDoSplits);

    BOOST_LOG_TRIVIAL(info) << "CSG assembly: " << po.m_mesh_to_slice.size() << " parts";

    generate_preview(po, slaposAssembly);
}

SLAPrint::Steps::Steps(SLAPrint *print)
    : m_print{print}
    , m_rng{std::random_device{}()}
    , objcount{m_print->m_objects.size()}
    , ilhd{m_print->m_objects.empty() ? m_print->m_material_config.initial_layer_height.getFloat()
                                      : m_print->m_objects.front()->m_config.layer_height.getFloat()}
    , ilh{float(ilhd)}
    , ilhs{scaled(ilhd)}
    , objectstep_scale{(max_objstatus - min_objstatus) / (objcount * 100.0)}
{}

void SLAPrint::Steps::apply_printer_corrections(SLAPrintObject &po, SliceOrigin o)
{
    if (o == soSupport && !po.m_supportdata) return;

    auto faded_lyrs = size_t(po.m_config.faded_layers.getInt());
    double min_w = m_print->m_printer_config.elefant_foot_min_width.getFloat() / 2.;
    double start_efc = m_print->m_printer_config.elefant_foot_compensation.getFloat();

    double doffs = m_print->m_printer_config.absolute_correction.getFloat();
    coord_t clpr_offs = scaled(doffs);

    faded_lyrs = std::min(po.m_slice_index.size(), faded_lyrs);
    size_t faded_lyrs_efc = std::max(size_t(1), faded_lyrs - 1);

    auto efc = [start_efc, faded_lyrs_efc](size_t pos) {
        return (faded_lyrs_efc - pos) * start_efc / faded_lyrs_efc;
    };

    std::vector<ExPolygons> &slices = o == soModel ?
                                          po.m_model_slices :
                                          po.m_supportdata->support_slices;

    if (clpr_offs != 0) for (size_t i = 0; i < po.m_slice_index.size(); ++i) {
        size_t idx = po.m_slice_index[i].get_slice_idx(o);
        if (idx < slices.size())
            slices[idx] = offset_ex(slices[idx], float(clpr_offs));
    }

    // --- Tolerance compensation: independent inner (a) and outer (b) diameter control ---
    // a > 0: holes shrink (solid grows into holes); b > 0: outer contour expands.
    auto apply_tc_layer = [](ExPolygons &layer_slices, coord_t ca, coord_t cb) {
        if (ca == 0 && cb == 0) return;
        ExPolygons result;
        result.reserve(layer_slices.size());
        for (const ExPolygon &ep : layer_slices) {
            Polygons new_contour = offset(ep.contour, float(cb));
            Polygons hole_solids;
            for (Polygon h : ep.holes) {
                h.reverse();                                    // CW hole → CCW solid area
                append(hole_solids, offset(h, float(-ca)));     // positive a = shrink hole area
            }
            append(result, diff_ex(new_contour, hole_solids));
        }
        layer_slices = std::move(result);
    };

    const int blc = m_print->m_tolerance_bottom_layer_count;

    // Normal layers (index >= bottom_layer_count)
    {
        coord_t ca = scaled(m_print->m_tolerance_compensation_a);
        coord_t cb = scaled(m_print->m_tolerance_compensation_b);
        if (m_print->m_tolerance_compensation && (ca != 0 || cb != 0)) {
            for (size_t i = (size_t)std::max(0, blc); i < po.m_slice_index.size(); ++i) {
                size_t idx = po.m_slice_index[i].get_slice_idx(o);
                if (idx < slices.size())
                    apply_tc_layer(slices[idx], ca, cb);
            }
        }
    }

    // Bottom layers (index < bottom_layer_count)
    {
        coord_t ca = scaled(m_print->m_bottom_tolerance_compensation_a);
        coord_t cb = scaled(m_print->m_bottom_tolerance_compensation_b);
        if (m_print->m_bottom_tolerance_compensation && (ca != 0 || cb != 0)) {
            size_t n = std::min((size_t)std::max(0, blc), po.m_slice_index.size());
            for (size_t i = 0; i < n; ++i) {
                size_t idx = po.m_slice_index[i].get_slice_idx(o);
                if (idx < slices.size())
                    apply_tc_layer(slices[idx], ca, cb);
            }
        }
    }

    if (start_efc > 0.) for (size_t i = 0; i < faded_lyrs; ++i) {
        size_t idx = po.m_slice_index[i].get_slice_idx(o);
        if (idx < slices.size())
            slices[idx] = elephant_foot_compensation(slices[idx], min_w, efc(i));
    }

    if (o == soModel) { // Z correction applies only to the model slices
        slices = sla::apply_zcorrection(slices,
                                        m_print->m_material_config.zcorrection_layers.getInt());
    }
}

void SLAPrint::Steps::hollow_model(SLAPrintObject &po)
{
    // Task 3.1: clear stale CSG parts so incremental re-runs stay correct.
    clear_csg(po.m_mesh_to_slice, slaposHollowing);

    po.m_hollowing_data.reset();

    if (! po.m_config.hollowing_enable.getBool()) {
        BOOST_LOG_TRIVIAL(info) << "Skipping hollowing step!";
        return;
    }

    BOOST_LOG_TRIVIAL(info) << "Performing hollowing step!";

    double thickness = po.m_config.hollowing_min_thickness.getFloat();
    double quality  = po.m_config.hollowing_quality.getFloat();
    double closing_d = po.m_config.hollowing_closing_distance.getFloat();
    sla::HollowingConfig hlwcfg{thickness, quality, closing_d};

    sla::InteriorPtr interior = generate_interior(po.transformed_mesh(), hlwcfg);

    if (!interior || sla::get_mesh(*interior).empty()) {
        BOOST_LOG_TRIVIAL(warning) << "Hollowed interior is empty!";
    } else {
        po.m_hollowing_data.reset(new SLAPrintObject::HollowingData());
        po.m_hollowing_data->interior = std::move(interior);

        // Task 3.1: register interior as CSG Difference part for Phase C/D.
        // swap_normals converts the inward-facing interior normals to outward-facing
        // so that slice_csgmesh_ex produces solid cross-sections for correct diff_ex.
        indexed_triangle_set its = sla::get_mesh(*po.m_hollowing_data->interior);
        sla::swap_normals(its);
        csg::CSGPart hpart{std::make_unique<indexed_triangle_set>(std::move(its)),
                           csg::CSGType::Difference};
        po.m_mesh_to_slice.emplace(CSGPartForStep{slaposHollowing, std::move(hpart)});

        BOOST_LOG_TRIVIAL(info) << "Hollowing CSG: interior registered as Difference part.";
    }
}

struct FaceHash {

    // A 64 bit number's max hex digits
    static constexpr size_t MAX_NUM_CHARS = 16;

    // A hash is created for each triangle to be identifiable. The hash uses
    // only the triangle's geometric traits, not the index in a particular mesh.
    std::unordered_set<std::string> facehash;

    // Returns the string in reverse, but that is ok for hashing
    static std::array<char, MAX_NUM_CHARS + 1> to_chars(int64_t val)
    {
        std::array<char, MAX_NUM_CHARS + 1> ret;

        static const constexpr char * Conv = "0123456789abcdef";

        auto ptr = ret.begin();
        auto uval = static_cast<uint64_t>(std::abs(val));
        while (uval) {
            *ptr = Conv[uval & 0xf];
            ++ptr;
            uval = uval >> 4;
        }
        if (val < 0) { *ptr = '-'; ++ptr; }
        *ptr = '\0'; // C style string ending

        return ret;
    }

    static std::string hash(const Vec<3, int64_t> &v)
    {
        std::string ret;
        ret.reserve(3 * MAX_NUM_CHARS);

        for (auto val : v)
            ret += to_chars(val).data();

        return ret;
    }

    static std::string facekey(const Vec3i32 &face, const std::vector<Vec3f> &vertices)
    {
        // Scale to integer to avoid floating points
        std::array<Vec<3, int64_t>, 3> pts = {
            scaled<int64_t>(vertices[face(0)]),
            scaled<int64_t>(vertices[face(1)]),
            scaled<int64_t>(vertices[face(2)])
        };

        // Get the first two sides of the triangle, do a cross product and move
        // that vector to the center of the triangle. This encodes all
        // information to identify an identical triangle at the same position.
        Vec<3, int64_t> a = pts[0] - pts[2], b = pts[1] - pts[2];
        Vec<3, int64_t> c = a.cross(b) + (pts[0] + pts[1] + pts[2]) / 3;

        // Return a concatenated string representation of the coordinates
        return hash(c);
    }

    FaceHash (const indexed_triangle_set &its): facehash(its.indices.size())
    {
        for (const Vec3i32 &face : its.indices)
            facehash.insert(facekey(face, its.vertices));
    }

    bool find(const std::string &key)
    {
        auto it = facehash.find(key);
        return it != facehash.end();
    }
};

// Create exclude mask for triangle removal inside hollowed interiors.
// This is necessary when the interior is already part of the mesh which was
// drilled using CGAL mesh boolean operation. Excluded will be the triangles
// originally part of the interior mesh and triangles that make up the drilled
// hole walls.
static std::vector<bool> create_exclude_mask(
        const indexed_triangle_set &its,
        const sla::Interior &interior,
        const std::vector<sla::DrainHole> &holes)
{
    FaceHash interior_hash{sla::get_mesh(interior)};

    std::vector<bool> exclude_mask(its.indices.size(), false);

    VertexFaceIndex neighbor_index{its};

    auto exclude_neighbors = [&neighbor_index, &exclude_mask](const Vec3i32 &face)
    {
        for (int i = 0; i < 3; ++i) {
            const auto &neighbors_range = neighbor_index[face(i)];
            for (size_t fi_n : neighbors_range)
                exclude_mask[fi_n] = true;
        }
    };

    for (size_t fi = 0; fi < its.indices.size(); ++fi) {
        auto &face = its.indices[fi];

        if (interior_hash.find(FaceHash::facekey(face, its.vertices))) {
            exclude_mask[fi] = true;
            continue;
        }

        if (exclude_mask[fi]) {
            exclude_neighbors(face);
            continue;
        }

        // Lets deal with the holes. All the triangles of a hole and all the
        // neighbors of these triangles need to be kept. The neigbors were
        // created by CGAL mesh boolean operation that modified the original
        // interior inside the input mesh to contain the holes.
        Vec3d tr_center = (
            its.vertices[face(0)] +
            its.vertices[face(1)] +
            its.vertices[face(2)]
        ).cast<double>() / 3.;

        // If the center is more than half a mm inside the interior,
        // it cannot possibly be part of a hole wall.
        if (sla::get_distance(tr_center, interior) < -0.5)
            continue;

        Vec3f U = its.vertices[face(1)] - its.vertices[face(0)];
        Vec3f V = its.vertices[face(2)] - its.vertices[face(0)];
        Vec3f C = U.cross(V);
        Vec3f face_normal = C.normalized();

        for (const sla::DrainHole &dh : holes) {
            if (dh.failed) continue;

            Vec3d dhpos = dh.pos.cast<double>();
            Vec3d dhend = dhpos + dh.normal.cast<double>() * dh.height;

            Linef3 holeaxis{dhpos, dhend};

            double D_hole_center = line_alg::distance_to(holeaxis, tr_center);
            double D_hole        = std::abs(D_hole_center - dh.radius);
            float dot            = dh.normal.dot(face_normal);

            // Empiric tolerances for center distance and normals angle.
            // For triangles that are part of a hole wall the angle of
            // triangle normal and the hole axis is around 90 degrees,
            // so the dot product is around zero.
            double D_tol = dh.radius / sla::DrainHole::steps;
            float normal_angle_tol = 1.f / sla::DrainHole::steps;

            if (D_hole < D_tol && std::abs(dot) < normal_angle_tol) {
                exclude_mask[fi] = true;
                exclude_neighbors(face);
            }
        }
    }

    return exclude_mask;
}

static indexed_triangle_set
remove_unconnected_vertices(const indexed_triangle_set &its)
{
    if (its.indices.empty()) {};

    indexed_triangle_set M;

    std::vector<int> vtransl(its.vertices.size(), -1);
    int vcnt = 0;
    for (auto &f : its.indices) {

        for (int i = 0; i < 3; ++i)
            if (vtransl[size_t(f(i))] < 0) {

                M.vertices.emplace_back(its.vertices[size_t(f(i))]);
                vtransl[size_t(f(i))] = vcnt++;
            }

        std::array<int, 3> new_f = {
            vtransl[size_t(f(0))],
            vtransl[size_t(f(1))],
            vtransl[size_t(f(2))]
        };

        M.indices.emplace_back(new_f[0], new_f[1], new_f[2]);
    }

    return M;
}

// Drill holes into the hollowed/original mesh.
void SLAPrint::Steps::drill_holes(SLAPrintObject &po)
{
    // Step 4.8: Restored from BBS block comment. drill_holes() was a no-op stub:
    // hollow_mesh_with_holes was never populated → get_mesh_to_slice() returned empty mesh
    // when hollowing was enabled → slice_model() threw "Inconsistent slice index".

    // Phase B Task 2.2: clear stale CSG parts from any previous run of this step.
    clear_csg(po.m_mesh_to_slice, slaposDrillHoles);

    bool needs_drilling = ! po.m_model_object->sla_drain_holes.empty();
    bool is_hollowed =
        (po.m_hollowing_data && po.m_hollowing_data->interior &&
         !sla::get_mesh(*po.m_hollowing_data->interior).empty());

    if (! is_hollowed && ! needs_drilling) {
        // In this case we can dump any data that might have been
        // generated on previous runs.
        po.m_hollowing_data.reset();
        generate_preview(po, slaposDrillHoles);
        return;
    }

    if (! po.m_hollowing_data)
        po.m_hollowing_data.reset(new SLAPrintObject::HollowingData());

    // Hollowing and/or drilling is active, m_hollowing_data is valid.

    // Regenerate hollowed mesh, even if it was there already. It may contain
    // holes that are no longer on the frontend.
    TriangleMesh &hollowed_mesh = po.m_hollowing_data->hollow_mesh_with_holes;
    hollowed_mesh = po.transformed_mesh();
    if (is_hollowed)
        sla::hollow_mesh(hollowed_mesh, *po.m_hollowing_data->interior);

    TriangleMesh &mesh_view = po.m_hollowing_data->hollow_mesh_with_holes_trimmed;

    if (! needs_drilling) {
        mesh_view = po.transformed_mesh();

        if (is_hollowed)
            sla::hollow_mesh(mesh_view, *po.m_hollowing_data->interior,
                             sla::hfRemoveInsideTriangles);

        BOOST_LOG_TRIVIAL(info) << "Drilling skipped (no holes).";
        generate_preview(po, slaposDrillHoles);
        return;
    }

    BOOST_LOG_TRIVIAL(info) << "Drilling drainage holes.";
    sla::DrainHoles drainholes = po.transformed_drainhole_points();

    //BBS: AABBTree optimization disabled — part_to_drill.indices is cleared each iteration
    //BBS: and never refilled (traverse is commented out below), so cgal_meshpart is always
    //BBS: an empty mesh. The self-intersection pre-check is effectively skipped; all holes
    //BBS: are accumulated into holes_mesh_cgal and subtracted from the hollowed mesh.
    //auto tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(
    //    hollowed_mesh.its.vertices,
    //    hollowed_mesh.its.indices
    //);

    std::uniform_real_distribution<float> dist(0., float(EPSILON));
    auto holes_mesh_cgal = MeshBoolean::cgal::triangle_mesh_to_cgal({}, {});
    indexed_triangle_set part_to_drill = hollowed_mesh.its;

    bool hole_fail = false;
    for (size_t i = 0; i < drainholes.size(); ++i) {
        sla::DrainHole holept = drainholes[i];

        holept.normal += Vec3f{dist(m_rng), dist(m_rng), dist(m_rng)};
        holept.normal.normalize();
        holept.pos += Vec3f{dist(m_rng), dist(m_rng), dist(m_rng)};
        indexed_triangle_set m = holept.to_mesh();

        part_to_drill.indices.clear();
        //BBS: bb/ebb only used in the disabled AABBTree traversal.
        //auto bb = bounding_box(m);
        //Eigen::AlignedBox<float, 3> ebb{bb.min.cast<float>(),
        //                                bb.max.cast<float>()};
        //BBS
        //AABBTreeIndirect::traverse(
        //            tree,
        //            AABBTreeIndirect::intersecting(ebb),
        //            [&part_to_drill, &hollowed_mesh](size_t faceid)
        //{
        //    part_to_drill.indices.emplace_back(hollowed_mesh.its.indices[faceid]);
        //});

        auto cgal_meshpart = MeshBoolean::cgal::triangle_mesh_to_cgal(
            remove_unconnected_vertices(part_to_drill));

        if (MeshBoolean::cgal::does_self_intersect(*cgal_meshpart)) {
            BOOST_LOG_TRIVIAL(error) << "Failed to drill hole";

            hole_fail = drainholes[i].failed =
                    po.model_object()->sla_drain_holes[i].failed = true;

            continue;
        }

        auto cgal_hole = MeshBoolean::cgal::triangle_mesh_to_cgal(m);
        MeshBoolean::cgal::plus(*holes_mesh_cgal, *cgal_hole);
    }

    if (MeshBoolean::cgal::does_self_intersect(*holes_mesh_cgal))
        throw Slic3r::SlicingError(L("Too many overlapping holes."));

    auto hollowed_mesh_cgal = MeshBoolean::cgal::triangle_mesh_to_cgal(hollowed_mesh);

    if (!MeshBoolean::cgal::does_bound_a_volume(*hollowed_mesh_cgal)) {
        po.active_step_add_warning(
            PrintStateBase::WarningLevel::NON_CRITICAL,
            L("Mesh to be hollowed is not suitable for hollowing (does not "
              "bound a volume)."));
    }

    if (!MeshBoolean::cgal::empty(*holes_mesh_cgal)
        && !MeshBoolean::cgal::does_bound_a_volume(*holes_mesh_cgal)) {
        po.active_step_add_warning(
            PrintStateBase::WarningLevel::NON_CRITICAL,
            L("Unable to drill the current configuration of holes into the "
              "model."));
    }

    try {
        if (!MeshBoolean::cgal::empty(*holes_mesh_cgal))
            MeshBoolean::cgal::minus(*hollowed_mesh_cgal, *holes_mesh_cgal);

        hollowed_mesh = MeshBoolean::cgal::cgal_to_triangle_mesh(*hollowed_mesh_cgal);
        mesh_view = hollowed_mesh;

        if (is_hollowed) {
            auto &interior = *po.m_hollowing_data->interior;
            std::vector<bool> exclude_mask =
                    create_exclude_mask(mesh_view.its, interior, drainholes);

            sla::remove_inside_triangles(mesh_view, interior, exclude_mask);
        }
    } catch (const Slic3r::RuntimeError &) {
        throw Slic3r::SlicingError(L(
            "Drilling holes into the mesh failed. "
            "This is usually caused by broken model. Try to fix it first."));
    }

    if (hole_fail)
        po.active_step_add_warning(PrintStateBase::WarningLevel::NON_CRITICAL,
                                   L("Failed to drill some holes into the model"));

    // Phase B Task 2.3: Register drain holes as CSG Difference parts in m_mesh_to_slice.
    // CGAL path above still produces hollow_mesh_with_holes (slice_model uses it until Phase C).
    // These CSG parts are registered for Phase C (slice_csgmesh_ex) and Phase D (ObjectClipper).
    {
        auto inserter = csg_inserter{po.m_mesh_to_slice, slaposDrillHoles};
        sla::DrainHoles clean_holes = po.transformed_drainhole_points();
        for (const sla::DrainHole &dhole : clean_holes) {
            csg::CSGPart part{
                std::make_unique<indexed_triangle_set>(dhole.to_mesh()),
                csg::CSGType::Difference
            };
            *inserter = std::move(part);
            ++inserter;
        }
        BOOST_LOG_TRIVIAL(info) << "DrillHoles CSG: " << clean_holes.size()
                                << " Difference parts registered in m_mesh_to_slice.";
    }

    // Phase B Task 2.4: update preview mesh to reflect drill holes.
    generate_preview(po, slaposDrillHoles);
}

// The slicing will be performed on an imaginary 1D grid which starts from
// the bottom of the bounding box created around the supported model. So
// the first layer which is usually thicker will be part of the supports
// not the model geometry. Exception is when the model is not in the air
// (elevation is zero) and no pad creation was requested. In this case the
// model geometry starts on the ground level and the initial layer is part
// of it. In any case, the model and the supports have to be sliced in the
// same imaginary grid (the height vector argument to TriangleMeshSlicer).
void SLAPrint::Steps::slice_model(SLAPrintObject &po)
{
    // Task 3.3: bounding box from base model (hollowing/drilling don't extend the Z range).
    auto && bb3d = po.transformed_mesh().bounding_box();

    // We need to prepare the slice index...

    double  lhd  = m_print->m_objects.front()->m_config.layer_height.getFloat();
    float   lh   = float(lhd);
    coord_t lhs  = scaled(lhd);
    double  minZ = bb3d.min(Z) - po.get_elevation();
    double  maxZ = bb3d.max(Z);
    auto    minZf = float(minZ);
    coord_t minZs = scaled(minZ);
    coord_t maxZs = scaled(maxZ);

    po.m_slice_index.clear();

    size_t cap = size_t(1 + (maxZs - minZs - ilhs) / lhs);
    po.m_slice_index.reserve(cap);

    po.m_slice_index.emplace_back(minZs + ilhs, minZf + ilh / 2.f, ilh);

    for(coord_t h = minZs + ilhs + lhs; h <= maxZs; h += lhs)
        po.m_slice_index.emplace_back(h, unscaled<float>(h) - lh / 2.f, lh);

    // Just get the first record that is from the model:
    auto slindex_it =
        po.closest_slice_record(po.m_slice_index, float(bb3d.min(Z)));

    if(slindex_it == po.m_slice_index.end())
        //TRN To be shown at the status bar on SLA slicing error.
        throw Slic3r::RuntimeError(
            L("Slicing had to be stopped due to an internal error: "
              "Inconsistent slice index."));

    po.m_model_height_levels.clear();
    po.m_model_height_levels.reserve(po.m_slice_index.size());
    for(auto it = slindex_it; it != po.m_slice_index.end(); ++it)
        po.m_model_height_levels.emplace_back(it->slice_level());

    po.m_model_slices.clear();
    MeshSlicingParamsEx params;
    params.closing_radius = float(po.config().slice_closing_radius.value);
    params.mode = MeshSlicingParams::SlicingMode::Regular;
    auto  thr        = [this]() { m_print->throw_if_canceled(); };
    auto &slice_grid = po.m_model_height_levels;

    // Task 3.3/3.4: CSG path — assembly + hollowing + drill holes combined in 2D.
    // Replaces: slice_mesh_ex(hollow_mesh_with_holes) + manual interior diff_ex.
    auto csg_range = Range{po.m_mesh_to_slice.cbegin(), po.m_mesh_to_slice.cend()};
    po.m_model_slices = csg::slice_csgmesh_ex(csg_range, slice_grid, params, thr);

    auto mit = slindex_it;
    for (size_t id = 0;
         id < po.m_model_slices.size() && mit != po.m_slice_index.end();
         id++) {
        mit->set_model_slice_idx(po, id); ++mit;
    }

    // We apply the printer correction offset here.
    apply_printer_corrections(po, soModel);

    if(po.m_config.generate_support.getBool() || po.m_config.pad_enable.getBool())
    {
        po.m_supportdata.reset(new SLAPrintObject::SupportData(po.get_mesh_to_print()));
    }

    // Step A5.2: Pre-compute support generator data when needed.
    // support_points() also has a fallback guard, but pre-computing here avoids
    // repeating the work when both generate_support and island detection are used.
    if (po.m_config.generate_support.getBool() || po.m_config.pad_enable.getBool())
        prepare_for_generate_supports(po);

    // Island contours are no longer extracted here.
    // They are built on demand via SLAPrint::redetect_islands() when the user
    // clicks "Detect Selected" in GLGizmoLcdOverhangDetection.
}

// Step A5.1: Pre-compute layer connectivity and island data for the support generator.
// Stores the result in po.m_support_point_generator_data for reuse by support_points().
// Slices are copied from po.m_model_slices so the original remains valid for other steps.
void SLAPrint::Steps::prepare_for_generate_supports(SLAPrintObject &po)
{
    sla::PrepareSupportConfig prep_cfg;

    // Copy slices: prepare_generator_data() moves them in, but po.m_model_slices
    // must remain valid for slice_supports and other downstream steps.
    std::vector<ExPolygons> slices_copy = po.get_model_slices();

    po.m_support_point_generator_data = sla::prepare_generator_data(
        std::move(slices_copy),
        po.m_model_height_levels,
        prep_cfg,
        [this]() { throw_if_canceled(); }
    );
}

// Build a uniform Z-height grid for island detection at a custom layer height.
// Mirrors the logic in slice_model() but uses detect_lh for all layers and
// does NOT write any result into po — the output is consumed by prepare_island_detection().
static std::vector<float> build_detection_height_levels(
    const BoundingBoxf3 &bb3d,
    float detect_lh,
    float elevation)
{
    const coord_t lhs  = scaled(double(detect_lh));
    const double  minZ = bb3d.min(Z) - double(elevation);
    const double  maxZ = bb3d.max(Z);
    const coord_t minZs = scaled(minZ);
    const coord_t maxZs = scaled(maxZ);

    std::vector<float> heights;
    heights.reserve(size_t(1 + (maxZs - minZs) / lhs));
    for (coord_t h = minZs + lhs; h <= maxZs; h += lhs)
        heights.push_back(unscaled<float>(h) - detect_lh / 2.f);
    return heights;
}

// On-demand island detection using a custom layer height.
// Re-slices po.m_mesh_to_slice at detect_lh, builds temporary SupportPointGeneratorData,
// extracts islands, and writes the result to po.m_island_contours.
// po.m_model_slices and po.m_support_point_generator_data are NOT modified.
void SLAPrint::Steps::prepare_island_detection(SLAPrintObject &po, float detect_lh)
{
    const auto bb3d = po.transformed_mesh().bounding_box();
    std::vector<float> detect_heights =
        build_detection_height_levels(bb3d, detect_lh, float(po.get_elevation()));

    if (detect_heights.empty()) {
        po.clear_island_contours();
        return;
    }

    MeshSlicingParamsEx params;
    params.closing_radius = float(po.config().slice_closing_radius.value);
    params.mode = MeshSlicingParams::SlicingMode::Regular;
    auto thr = [this]() { throw_if_canceled(); };

    auto csg_range = Range{po.m_mesh_to_slice.cbegin(), po.m_mesh_to_slice.cend()};
    std::vector<ExPolygons> detect_slices =
        csg::slice_csgmesh_ex(csg_range, detect_heights, params, thr);

    sla::PrepareSupportConfig prep_cfg;
    sla::SupportPointGeneratorData temp_data = sla::prepare_generator_data(
        std::move(detect_slices),
        detect_heights,
        prep_cfg,
        [this]() { throw_if_canceled(); }
    );

    sla::IslandContourSet cs;
    for (const sla::Layer &layer : temp_data.layers) {
        for (const sla::LayerPart &part : layer.parts) {
            if (part.shape == nullptr || !part.prev_parts.empty())
                continue;
            sla::IslandContour ic;
            ic.print_z = layer.print_z;
            ic.contour = *part.shape;
            ic.area    = float(unscale<double>(unscale<double>(part.shape->area())));
            cs.islands.push_back(std::move(ic));
        }
    }
    cs.valid = true;
    po.set_island_contours(std::move(cs));

    BOOST_LOG_TRIVIAL(info) << "Island detection (lh=" << detect_lh << "mm): "
                            << po.island_contours().islands.size() << " islands found";
}

// In this step we check the slices, identify island and cover them with
// support points. Then we sprinkle the rest of the mesh.
void SLAPrint::Steps::support_points(SLAPrintObject &po)
{
    // If supports are disabled, we can skip the model scan.
    if(!po.m_config.generate_support.getBool()) return;

    if (!po.m_supportdata)
        po.m_supportdata.reset(new SLAPrintObject::SupportData(po.get_mesh_to_print()));

    const ModelObject& mo = *po.m_model_object;

    BOOST_LOG_TRIVIAL(debug) << "Support point count "
                             << mo.sla_support_points.size();

    // Unless the user modified the points or we already did the calculation,
    // we will do the autoplacement. Otherwise we will just blindly copy the
    // frontend data into the backend cache.
    if (mo.sla_points_status != sla::PointsStatus::UserModified) {

        // Tell the mesh where drain holes are. Although the points are
        // calculated on slices, the algorithm then raycasts the points
        // so they actually lie on the mesh.
//        po.m_supportdata->emesh.load_holes(po.transformed_drainhole_points());

        throw_if_canceled();

        // Step A5.3: Use new free-function API (Voronoi Medial Axis + NearPoints KD-tree).
        // Replaces old class-based SupportPointGenerator (Poisson disk sampling).
        sla::SupportPointGeneratorConfig config;
        const SLAPrintObjectConfig& cfg = po.config();

        // Density config value is in percents.
        config.density_relative = float(cfg.support_points_density_relative / 100.f);
        config.head_diameter    = float(cfg.support_head_front_diameter);
        config.island_configuration = sla::SampleConfigFactory::apply_density(
            sla::SampleConfigFactory::create(config.head_diameter),
            config.density_relative);

        // scaling for the sub operations
        double d = objectstep_scale * OBJ_STEP_LEVELS[slaposSupportPoints] / 100.0;
        double init = current_status();

        auto statuscb = [this, d, init](int st)
        {
            double current = init + st * d;
            if(std::round(current_status()) < std::round(current))
                report_status(current, OBJ_STEP_LABELS(slaposSupportPoints));
        };

        // Ensure pre-computed data is ready (should have been done in slice_model step).
        if (po.m_support_point_generator_data.layers.empty())
            prepare_for_generate_supports(po);

        // Phase 1: Generate layer support points using Voronoi + NearPoints KD-tree.
        throw_if_canceled();
        sla::LayerSupportPoints layer_pts = sla::generate_support_points(
            po.m_support_point_generator_data,
            config,
            [this]() { throw_if_canceled(); },
            statuscb);

        // Phase 2: Project generated points from layer plane onto actual mesh surface.
        throw_if_canceled();
        const double allowed_move = (po.m_model_height_levels.size() > 1)
            ? double(po.m_model_height_levels[1] - po.m_model_height_levels[0])
              + double(std::numeric_limits<float>::epsilon())
            : double(cfg.support_head_front_diameter); // fallback for single-layer
        po.m_supportdata->pts = sla::move_on_mesh_surface(
            layer_pts, po.m_supportdata->emesh, allowed_move,
            [this]() { throw_if_canceled(); });

        // Phase 3: Filter support points by support_critical_angle so Auto Support Apply
        // shows the same points that support-tree generation will use.
        {
            const double critical_angle_deg = cfg.support_critical_angle.getFloat();
            sla::SupportPoints &pts = po.m_supportdata->pts;
            if (!pts.empty()) {
                sla::PointSet point_matrix(pts.size(), 3);
                for (size_t i = 0; i < pts.size(); ++i)
                    point_matrix.row(Eigen::Index(i)) = pts[i].pos.cast<double>();

                sla::PointSet nmls = sla::normals(point_matrix, po.m_supportdata->emesh,
                                                  cfg.support_head_front_diameter / 2.0,
                                                  [this]() { throw_if_canceled(); });

                sla::SupportPoints filtered;
                filtered.reserve(pts.size());
                for (size_t i = 0; i < pts.size(); ++i) {
                    auto n = nmls.row(Eigen::Index(i));
                    auto [polar, azimuth] = sla::dir_to_spheric(n);
                    (void) azimuth;
                    if (sla::sla_support_passes_overhang_filter(polar, critical_angle_deg))
                        filtered.push_back(pts[i]);
                }
                pts = std::move(filtered);
            }
        }

        BOOST_LOG_TRIVIAL(debug) << "Automatic support points: "
                                 << po.m_supportdata->pts.size();

        // Using RELOAD_SLA_SUPPORT_POINTS to tell the Plater to pass
        // the update status to GLGizmoSlaSupports
        report_status(-1, L("Generating support points"),
                      SlicingStatus::RELOAD_SLA_SUPPORT_POINTS);
    } else {
        // There are either some points on the front-end, or the user
        // removed them on purpose. No calculation will be done.
        po.m_supportdata->pts = po.transformed_support_points();
    }
}

void SLAPrint::Steps::support_tree(SLAPrintObject &po)
{
    if(!po.m_supportdata) return;

    sla::PadConfig pcfg = make_pad_cfg(po.m_config);

    if (pcfg.embed_object)
        po.m_supportdata->emesh.ground_level_offset(pcfg.wall_thickness_mm);

    // If the zero elevation mode is engaged, we have to filter out all the
    // points that are on the bottom of the object
    if (is_zero_elevation(po.config())) {
        remove_bottom_points(po.m_supportdata->pts,
                             float(po.m_supportdata->emesh.ground_level() + EPSILON));
    }

    po.m_supportdata->cfg = make_support_cfg(po.m_config);
//    po.m_supportdata->emesh.load_holes(po.transformed_drainhole_points());

    // scaling for the sub operations
    double d = objectstep_scale * OBJ_STEP_LEVELS[slaposSupportTree] / 100.0;
    double init = current_status();
    sla::JobController ctl;

    ctl.statuscb = [this, d, init](unsigned st, const std::string &logmsg) {
        double current = init + st * d;
        if (std::round(current_status()) < std::round(current))
            report_status(current, OBJ_STEP_LABELS(slaposSupportTree),
                          SlicingStatus::DEFAULT, logmsg);
    };
    ctl.stopcondition = [this]() { return canceled(); };
    ctl.cancelfn = [this]() { throw_if_canceled(); };

    po.m_supportdata->create_support_tree(ctl);

    if (!po.m_config.generate_support.getBool()) return;

    throw_if_canceled();

    // Create the unified mesh
    auto rc = SlicingStatus::RELOAD_SCENE;

    // This is to prevent "Done." being displayed during merged_mesh()
    report_status(-1, L("Visualizing supports"));

    BOOST_LOG_TRIVIAL(debug) << "Processed support point count "
                             << po.m_supportdata->pts.size();

    // Check the mesh for later troubleshooting.
    if(po.support_mesh().empty())
        BOOST_LOG_TRIVIAL(warning) << "Support mesh is empty";

    report_status(-1, L("Visualizing supports"), rc);
}

void SLAPrint::Steps::generate_pad(SLAPrintObject &po) {
    // this step can only go after the support tree has been created
    // and before the supports had been sliced. (or the slicing has to be
    // repeated)

    if(po.m_config.pad_enable.getBool()) {
        // Get the distilled pad configuration from the config
        sla::PadConfig pcfg = make_pad_cfg(po.m_config);

        ExPolygons bp; // This will store the base plate of the pad.
        double   pad_h             = pcfg.full_height();
        const TriangleMesh &trmesh = po.transformed_mesh();

        if (!po.m_config.generate_support.getBool() || pcfg.embed_object) {
            // No support (thus no elevation) or zero elevation mode
            // we sometimes call it "builtin pad" is enabled so we will
            // get a sample from the bottom of the mesh and use it for pad
            // creation.
            sla::pad_blueprint(trmesh.its, bp, float(pad_h),
                               float(po.m_config.layer_height.getFloat()),
                               [this](){ throw_if_canceled(); });
        }

        po.m_supportdata->create_pad(bp, pcfg);

        if (!validate_pad(po.m_supportdata->support_tree_ptr->retrieve_mesh(sla::MeshType::Pad), pcfg))
            throw Slic3r::SlicingError(
                    L("No pad can be generated for this model with the "
                      "current configuration"));

    } else if(po.m_supportdata && po.m_supportdata->support_tree_ptr) {
        po.m_supportdata->support_tree_ptr->remove_pad();
    }

    throw_if_canceled();
    report_status(-1, L("Visualizing supports"), SlicingStatus::RELOAD_SCENE);
}

// Slicing the support geometries similarly to the model slicing procedure.
// If the pad had been added previously (see step "base_pool" than it will
// be part of the slices)
void SLAPrint::Steps::slice_supports(SLAPrintObject &po) {
    auto& sd = po.m_supportdata;

    if(sd) sd->support_slices.clear();

    // Don't bother if no supports and no pad is present.
    if (!po.m_config.generate_support.getBool() && !po.m_config.pad_enable.getBool())
        return;

    if(sd && sd->support_tree_ptr) {
        auto heights = reserve_vector<float>(po.m_slice_index.size());

        for(auto& rec : po.m_slice_index) heights.emplace_back(rec.slice_level());

        // Step 2.5: Report that slice_supports is starting so the progress bar
        // advances from slaposSupportTree (prev step) into this step's range.
        // Without this, the UI stays stuck at the slaposSupportTree end-percent
        // until RELOAD_SLA_PREVIEW fires at the very end of this step.
        report_status(current_status(), OBJ_STEP_LABELS(slaposSliceSupports));

        sd->support_slices = sd->support_tree_ptr->slice(
            heights, float(po.config().slice_closing_radius.value));
    }

    for (size_t i = 0; i < sd->support_slices.size() && i < po.m_slice_index.size(); ++i)
        po.m_slice_index[i].set_support_slice_idx(po, i);

    apply_printer_corrections(po, soSupport);

    // Using RELOAD_SLA_PREVIEW to tell the Plater to pass the update
    // status to the 3D preview to load the SLA slices.
    report_status(-2, "", SlicingStatus::RELOAD_SLA_PREVIEW);
}

// get polygons for all instances in the object
static ExPolygons get_all_polygons(const SliceRecord& record, SliceOrigin o)
{
    if (!record.print_obj()) return {};

    ExPolygons polygons;
    auto &input_polygons = record.get_slice(o);
    auto &instances = record.print_obj()->instances();
    bool is_lefthanded = record.print_obj()->is_left_handed();
    polygons.reserve(input_polygons.size() * instances.size());

    for (const ExPolygon& polygon : input_polygons) {
        if(polygon.contour.empty()) continue;

        for (size_t i = 0; i < instances.size(); ++i)
        {
            ExPolygon poly;

            // We need to reverse if is_lefthanded is true but
            bool needreverse = is_lefthanded;

            // should be a move
            poly.contour.points.reserve(polygon.contour.size() + 1);

            auto& cntr = polygon.contour.points;
            if(needreverse)
                for(auto it = cntr.rbegin(); it != cntr.rend(); ++it)
                    poly.contour.points.emplace_back(it->x(), it->y());
            else
                for(auto& p : cntr)
                    poly.contour.points.emplace_back(p.x(), p.y());

            for(auto& h : polygon.holes) {
                poly.holes.emplace_back();
                auto& hole = poly.holes.back();
                hole.points.reserve(h.points.size() + 1);

                if(needreverse)
                    for(auto it = h.points.rbegin(); it != h.points.rend(); ++it)
                        hole.points.emplace_back(it->x(), it->y());
                else
                    for(auto& p : h.points)
                        hole.points.emplace_back(p.x(), p.y());
            }

            if(is_lefthanded) {
                for(auto& p : poly.contour) p.x() = -p.x();
                for(auto& h : poly.holes) for(auto& p : h) p.x() = -p.x();
            }

            poly.rotate(double(instances[i].rotation));
            poly.translate(Point{instances[i].shift.x(), instances[i].shift.y()});

            polygons.emplace_back(std::move(poly));
        }
    }

    return polygons;
}

void SLAPrint::Steps::initialize_printer_input()
{
    auto &printer_input = m_print->m_printer_input;

    // clear the rasterizer input
    printer_input.clear();

    size_t mx = 0;
    for(SLAPrintObject * o : m_print->m_objects) {
        if(auto m = o->get_slice_index().size() > mx) mx = m;
    }

    printer_input.reserve(mx);

    auto eps = coord_t(SCALED_EPSILON);

    for(SLAPrintObject * o : m_print->m_objects) {
        coord_t gndlvl = o->get_slice_index().front().print_level() - ilhs;

        for(const SliceRecord& slicerecord : o->get_slice_index()) {
            if (!slicerecord.is_valid())
                throw Slic3r::SlicingError(
                    L("There are unprintable objects. Try to "
                      "adjust support settings to make the "
                      "objects printable."));

            coord_t lvlid = slicerecord.print_level() - gndlvl;

            // Neat trick to round the layer levels to the grid.
            lvlid = eps * (lvlid / eps);

            auto it = std::lower_bound(printer_input.begin(),
                                       printer_input.end(),
                                       PrintLayer(lvlid));

            if(it == printer_input.end() || it->level() != lvlid)
                it = printer_input.insert(it, PrintLayer(lvlid));


            it->add(slicerecord);
        }
    }
}

// Merging the slices from all the print objects into one slice grid and
// calculating print statistics from the merge result.
void SLAPrint::Steps::merge_slices_and_eval_stats() {

    initialize_printer_input();

    auto &print_statistics = m_print->m_print_statistics;
    auto &printer_config   = m_print->m_printer_config;
    auto &material_config  = m_print->m_material_config;
    auto &printer_input    = m_print->m_printer_input;

    print_statistics.clear();

    const double area_fill = printer_config.area_fill.getFloat()*0.01;// 0.5 (50%);
    const double fast_tilt = printer_config.fast_tilt_time.getFloat();// 5.0;
    const double slow_tilt = printer_config.slow_tilt_time.getFloat();// 8.0;

    const double init_exp_time = material_config.initial_exposure_time.getFloat();
    const double exp_time      = material_config.exposure_time.getFloat();

    const int fade_layers_cnt = m_print->m_default_object_config.faded_layers.getInt();// 10 // [3;20]

    const auto width          = scaled<double>(printer_config.display_width.getFloat());
    const auto height         = scaled<double>(printer_config.display_height.getFloat());
    const double display_area = width*height;

    double supports_volume(0.0);
    double models_volume(0.0);

    double estim_time(0.0);
    std::vector<double> layers_times;
    layers_times.reserve(printer_input.size());

    size_t slow_layers = 0;
    size_t fast_layers = 0;

    const double delta_fade_time = (init_exp_time - exp_time) / (fade_layers_cnt + 1);
    double fade_layer_time = init_exp_time;

    sla::ccr::SpinningMutex mutex;
    using Lock = std::lock_guard<sla::ccr::SpinningMutex>;

    // Going to parallel:
    auto printlayerfn = [this,
            // functions and read only vars
            area_fill, display_area, exp_time, init_exp_time, fast_tilt, slow_tilt, delta_fade_time,

            // write vars
            &mutex, &models_volume, &supports_volume, &estim_time, &slow_layers,
            &fast_layers, &fade_layer_time, &layers_times](size_t sliced_layer_cnt)
    {
        PrintLayer &layer = m_print->m_printer_input[sliced_layer_cnt];

        // vector of slice record references
        auto& slicerecord_references = layer.slices();

        if(slicerecord_references.empty()) return;

        // Layer height should match for all object slices for a given level.
        const auto l_height = double(slicerecord_references.front().get().layer_height());

        // Calculation of the consumed material

        ExPolygons model_polygons;
        ExPolygons supports_polygons;

        size_t c = std::accumulate(layer.slices().begin(),
                                   layer.slices().end(),
                                   size_t(0),
                                   [](size_t a, const SliceRecord &sr) {
            return a + sr.get_slice(soModel).size();
        });

        model_polygons.reserve(c);

        c = std::accumulate(layer.slices().begin(),
                            layer.slices().end(),
                            size_t(0),
                            [](size_t a, const SliceRecord &sr) {
            return a + sr.get_slice(soModel).size();
        });

        supports_polygons.reserve(c);

        for(const SliceRecord& record : layer.slices()) {

            ExPolygons modelslices = get_all_polygons(record, soModel);
            for(ExPolygon& p_tmp : modelslices) model_polygons.emplace_back(std::move(p_tmp));

            ExPolygons supportslices = get_all_polygons(record, soSupport);
            for(ExPolygon& p_tmp : supportslices) supports_polygons.emplace_back(std::move(p_tmp));

        }

        model_polygons = union_ex(model_polygons);
        double layer_model_area = 0;
        for (const ExPolygon& polygon : model_polygons)
            layer_model_area += area(polygon);

        if (layer_model_area < 0 || layer_model_area > 0) {
            Lock lck(mutex); models_volume += layer_model_area * l_height;
        }

        if(!supports_polygons.empty()) {
            if(model_polygons.empty()) supports_polygons = union_ex(supports_polygons);
            else supports_polygons = diff_ex(supports_polygons, model_polygons);
            // allegedly, union of subject is done withing the diff according to the pftPositive polyFillType
        }

        double layer_support_area = 0;
        for (const ExPolygon& polygon : supports_polygons)
            layer_support_area += area(polygon);

        if (layer_support_area < 0 || layer_support_area > 0) {
            Lock lck(mutex); supports_volume += layer_support_area * l_height;
        }

        // Here we can save the expensively calculated polygons for printing
        ExPolygons trslices;
        trslices.reserve(model_polygons.size() + supports_polygons.size());
        for(ExPolygon& poly : model_polygons) trslices.emplace_back(std::move(poly));
        for(ExPolygon& poly : supports_polygons) trslices.emplace_back(std::move(poly));

        layer.transformed_slices(union_ex(trslices));

        // Calculation of the slow and fast layers to the future controlling those values on FW

        const bool is_fast_layer = (layer_model_area + layer_support_area) <= display_area*area_fill;
        const double tilt_time = is_fast_layer ? fast_tilt : slow_tilt;

        { Lock lck(mutex);
            if (is_fast_layer)
                fast_layers++;
            else
                slow_layers++;

            // Calculation of the printing time

            double layer_times = 0.0;
            if (sliced_layer_cnt < 3)
                layer_times += init_exp_time;
            else if (fade_layer_time > exp_time) {
                fade_layer_time -= delta_fade_time;
                layer_times += fade_layer_time;
            }
            else
                layer_times += exp_time;
            layer_times += tilt_time;

            layers_times.push_back(layer_times);
            estim_time += layer_times;
        }
    };

    // sequential version for debugging:
    // for(size_t i = 0; i < m_printer_input.size(); ++i) printlayerfn(i);
    sla::ccr::for_each(size_t(0), printer_input.size(), printlayerfn);

    auto SCALING2 = SCALING_FACTOR * SCALING_FACTOR;
    print_statistics.support_used_material = supports_volume * SCALING2;
    print_statistics.objects_used_material = models_volume  * SCALING2;

    // Estimated printing time
    // A layers count o the highest object
    if (printer_input.size() == 0)
        print_statistics.estimated_print_time = std::nan("");
    else {
        print_statistics.estimated_print_time = estim_time;
        print_statistics.layers_times = layers_times;
    }

    print_statistics.fast_layers_count = fast_layers;
    print_statistics.slow_layers_count = slow_layers;

    print_statistics.prz_print_time_s = adjusted_prz_print_time_seconds(
        static_cast<int>(printer_input.size()),
        m_print->full_print_config()
    );

    report_status(-2, "", SlicingStatus::RELOAD_SLA_PREVIEW);
}

// Rasterizing the model objects, and their supports
void SLAPrint::Steps::rasterize()
{
    if(canceled() || !m_print->m_printer) return;

    // Compute bed→display shift used as the bed→display translation in SLARasterParams.
    Points bed_pts;
    bed_pts.reserve(m_print->printer_config().printable_area.values.size());
    for (const Vec2d &v : m_print->printer_config().printable_area.values)
        bed_pts.emplace_back(scaled(v.x()), scaled(v.y()));
    BoundingBox bed_bb(bed_pts);
    const double display_cx = m_print->printer_config().display_width.getFloat()  / 2.0;
    const double display_cy = m_print->printer_config().display_height.getFloat() / 2.0;
    const Point  display_center{scaled(display_cx), scaled(display_cy)};
    const Point  raster_shift = display_center - bed_bb.center();

    if(canceled()) return;

    // Extract rasterization parameter snapshot for on-demand use (PRZ export, GUI preview).
    if (!canceled()) {
        const SLAPrinterConfig &cfg = m_print->printer_config();

        double w  = cfg.display_width.getFloat();
        double h  = cfg.display_height.getFloat();
        size_t pw = size_t(cfg.display_pixels_x.getInt());
        size_t ph = size_t(cfg.display_pixels_y.getInt());

        auto orient = cfg.display_orientation.value == sladoPortrait
            ? sla::RasterBase::roPortrait
            : sla::RasterBase::roLandscape;
        // For portrait mode: only swap the resolution (image shape), NOT the physical dimensions.
        // expolygons_to_cvmat with flipXY=true already swaps polygon x↔y axes internally,
        // so pxdim must keep the landscape ratios (display_width/pixels_x, display_height/pixels_y).
        sla::PixelDim pxdim{w / pw, h / ph};
        if (orient == sla::RasterBase::roPortrait) { std::swap(pw, ph); }

        sla::Resolution res{pw, ph};
        sla::RasterBase::Trafo trafo{orient,
            {cfg.display_mirror_x.getBool(), cfg.display_mirror_y.getBool()}};
        double gamma = cfg.gamma_correction.getFloat();

        if (cfg.anti_aliasing.value == spNone)
            gamma = 0.0;

        int     aa_steps = 0;
        uint8_t gray_lo  = 0;
        uint8_t gray_hi  = 255;

        if (cfg.anti_aliasing.value == spGrayScaleLevel) {
            const DynamicPrintConfig &full_cfg = m_print->full_print_config();
            if (auto *aa_lvl = full_cfg.option<ConfigOptionInt>("anti_aliasing_level"))
                aa_steps = std::max(1, aa_lvl->getInt());
            else
                aa_steps = 4;
            if (auto *gsl = full_cfg.option<ConfigOptionInts>("gray_scale_level");
                    gsl && gsl->values.size() >= 2) {
                gray_lo = (uint8_t)std::clamp(gsl->values[0], 0, 255);
                gray_hi = (uint8_t)std::clamp(gsl->values[1], 0, 255);
            }
        }

        int blur_pixel = 0;
        {
            const DynamicPrintConfig &full_cfg = m_print->full_print_config();
            if (auto *blur_en = full_cfg.option<ConfigOptionBool>("image_blur_enable");
                    blur_en && blur_en->getBool()) {
                if (auto *blur_px = full_cfg.option<ConfigOptionEnum<ImageBlurPixel>>("image_blur_pixel"))
                    blur_pixel = blur_px->getInt() + 2;
            }
        }

        SLARasterParams rp;
        rp.res               = res;
        rp.pxdim             = pxdim;
        rp.trafo             = trafo;
        rp.gamma             = gamma;
        rp.aa_steps          = aa_steps;
        rp.gray_lo           = gray_lo;
        rp.gray_hi           = gray_hi;
        rp.blur_pixel        = blur_pixel;
        rp.shift             = raster_shift;
        rp.picture_grayscale = static_cast<uint8_t>(
            std::clamp(cfg.picture_grayscale.getInt(), 0, 255));
        m_print->m_raster_params = rp;
    }

    // Phase 5: full-parallel PNG rasterization into RasterCache
    {
        auto       &printer_input = m_print->m_printer_input;
        const auto &rp            = *m_print->m_raster_params;
        const size_t N            = printer_input.size();

        if (N == 0 || canceled()) return;

        sla::RasterCacheKey cache_key =
            sla::RasterCache::compute_key(rp, printer_input);

        if (sla::RasterCache::is_valid(cache_key)) {
            BOOST_LOG_TRIVIAL(info)
                << "SLA rasterize: cache hit " << cache_key.hash;
            return;
        }

        BOOST_LOG_TRIVIAL(info)
            << "SLA rasterize: cache miss, rasterizing " << N << " layers";

        // Create the cache directory exactly once before the parallel loop.
        // Calling create_directories from every thread (inside write_layer) causes
        // severe NTFS MFT-entry lock contention on Windows, effectively serializing
        // all I/O and stalling progress around the 50-60% mark.
        sla::RasterCache::ensure_dir(cache_key);

        // 5.1 CAS progress counters
        std::atomic<int> completed_layers{0};
        std::atomic<int> last_reported_pct{-1};

        // PRZ-RLE encoding constants — identical to those in generate_prz().
        // static constexpr avoids lambda-capture requirements.
        static constexpr uchar RLE_BLACK          = 0x00;
        static constexpr uchar RLE_WHITE          = 0xc0;
        static constexpr uchar RLE_GRAY           = 0x40;
        static constexpr uchar RLE_BYTE_NUMBER[4] = { 0x00, 0x10, 0x20, 0x30 };
        static constexpr int   RLE_CONT_BOUND[4]  = { 1 << 4, 1 << 12, 1 << 20, 1 << 28 };
        static constexpr int   RLE_BOUND_0        = 0x0f;

        // 5.2 Cap concurrency to control cv::Mat memory pressure.
        // At 13320×5120, each layer's Mat is ~65 MB.
        // 8 threads × 65 MB = 520 MB working set — no paging, no heap contention.
        // RLE encoding is orders of magnitude faster than PNG so CPU is no longer
        // the bottleneck; the arena exists purely to bound peak memory.
        constexpr int RASTERIZE_CONCURRENCY = 8;
        tbb::task_arena raster_arena(RASTERIZE_CONCURRENCY);

        // 5.2a Thread-local storage: each of the 8 arena threads allocates its
        // cv::Mat (65 MB) and rle_buf (~34 MB worst-case) exactly once on first use,
        // then reuses both buffers for every subsequent layer on that thread.
        // Eliminates all per-layer malloc/free: previously ~100 MB of transient
        // allocations per layer caused CRT heap lock contention at the 55% peak.
        struct TLSData {
            cv::Mat            mat;
            cv::Mat            mat_rotated; // landscape buffer for PRZ RLE (Phase 1.5)
            std::vector<char>  rle_buf;
            cv::Mat            thumb;       // downsampled preview (panel orient.), reused per layer
            std::vector<uchar> thumb_rle;   // gray-RLE-encoded thumb bytes, reused per layer
        };
        tbb::enumerable_thread_specific<TLSData> tls;

        // Per-printer final X-mirror state — constant across all layers. Shared
        // single source of truth with the PRZ header Xmirror byte
        // (see PhrozenPRZOrient.hpp). Drives the post-rotate orientation flip.
        const bool prz_x_mirror = prz_final_x_mirror(m_print->printer_config());

        raster_arena.execute([&] {
            tbb::parallel_for(
                tbb::blocked_range<size_t>(0, N, /*grain_size=*/1),
                [&](const tbb::blocked_range<size_t> &range) {
                    for (size_t lid = range.begin(); lid < range.end(); ++lid) {
                        throw_if_canceled();

                        ExPolygons polys = printer_input[lid].transformed_slices();
                        for (ExPolygon &ep : polys)
                            ep.translate(rp.shift);

                        // Reuse thread-local Mat and RLE buffer — no malloc after first layer.
                        TLSData        &tls_data = tls.local();
                        cv::Mat        &mat      = tls_data.mat;
                        std::vector<char> &rle_buf = tls_data.rle_buf;

                        sla::expolygons_to_cvmat(
                            mat, polys, rp.res, rp.pxdim, rp.trafo,
                            rp.gamma, rp.aa_steps, rp.gray_lo, rp.gray_hi,
                            rp.blur_pixel);

                        sla::apply_picture_grayscale_lut(mat, rp.picture_grayscale);

                        // Preview thumbnail (sla-preview-thumb-cache): capture from the
                        // panel-orientation `mat` AFTER the picture_grayscale LUT and BEFORE
                        // the PRZ-only cv::rotate / X-mirror below, so the thumb is a drop-in
                        // for the GUI preview path (which applies its own display rotation and
                        // must NOT re-apply the LUT). Downsample so the long edge ≤ 4096
                        // (< 16 M px GL texture cap). thumb/thumb_rle are TLS — reused per layer,
                        // never freed (same zero-malloc contract as mat/rle_buf above).
                        // Encoded with the lightweight gray-RLE (NOT OpenCV imgcodecs/PNG, which
                        // would drag a second libjpeg-turbo into the link → LNK2005).
                        {
                            constexpr int THUMB_MAX_EDGE = 4096;
                            const int     long_edge = std::max(mat.cols, mat.rows);
                            const cv::Mat *thumb_src;
                            if (long_edge > THUMB_MAX_EDGE) {
                                const double scale = double(THUMB_MAX_EDGE) / double(long_edge);
                                cv::resize(mat, tls_data.thumb, cv::Size(), scale, scale,
                                           cv::INTER_AREA);
                                thumb_src = &tls_data.thumb;
                            } else {
                                // Already within the cap — encode the panel mat directly,
                                // no copy into tls_data.thumb needed.
                                thumb_src = &mat;
                            }
                            sla::RasterCache::rle_encode_gray(
                                thumb_src->data, thumb_src->cols, thumb_src->rows,
                                tls_data.thumb_rle);
                            // write_thumb throws on failure → propagates out of parallel_for →
                            // mark_complete() is skipped → cache judged invalid (liveness binding).
                            sla::RasterCache::write_thumb(cache_key, lid, tls_data.thumb_rle);
                        }

                        // Phase 1.5: portrait cv::Mat (rows=display_pixels_x,
                        // cols=display_pixels_y) → landscape via R₉₀cw so that
                        // the RLE byte stream has display_pixels_x pixels per row,
                        // matching PRZ header xr=display_pixels_x (Phase 1 change).
                        // See design.md §Decision 1.5 for the geometric derivation.
                        cv::rotate(mat, tls_data.mat_rotated, cv::ROTATE_90_CLOCKWISE);

                        // Compensate the axis projection introduced by the 90° CW
                        // rotation so the cached layer image matches Chitubox
                        // per-printer (see openspec fix-prz-image-mirror-axis-swap,
                        // design.md Decision 1). Same flip is applied at the
                        // cache-miss site in PhrozenPRZ.cpp to keep bytes identical.
                        prz_orient_after_rotate(tls_data.mat_rotated, prz_x_mirror);

                        // 5.3 PRZ-RLE encode — no DEFLATE, no PNG checksums.
                        // Produces the same przByte layout as generate_prz() so
                        // Phase 6 can stream cached bytes directly to the PRZ output.
                        const int    total = tls_data.mat_rotated.rows * tls_data.mat_rotated.cols;
                        const uchar *data  = tls_data.mat_rotated.data;

                        // Worst case: every pixel is its own gray run → 2 bytes each
                        // + 1 header + 1 checksum. Reserve once; subsequent layers reuse.
                        const size_t rle_cap = static_cast<size_t>(total) * 2 + 16;
                        if (rle_buf.size() < rle_cap)
                            rle_buf.resize(rle_cap);

                        size_t rle_pos = 0;
                        rle_buf[rle_pos++] = static_cast<char>(0x55); // PRZ layer head
                        int sum = 0;

                        auto flush_run = [&](uchar color, int count) {
                            const char *c = reinterpret_cast<const char *>(&count);
                            if (color == 0x00 || color == 0xff) {
                                uchar base = (color == 0x00) ? RLE_BLACK : RLE_WHITE;
                                for (int bid = 0; bid < 4; ++bid) {
                                    if (count < RLE_CONT_BOUND[bid]) {
                                        uchar b0 = base + RLE_BYTE_NUMBER[bid] +
                                                   (count & RLE_BOUND_0);
                                        count >>= 4;
                                        sum += static_cast<int>(b0);
                                        rle_buf[rle_pos++] = static_cast<char>(b0);
                                        for (int k = bid; k >= 1; --k) {
                                            rle_buf[rle_pos++] = c[k - 1];
                                            sum += static_cast<int>(
                                                static_cast<uchar>(c[k - 1]));
                                        }
                                        break;
                                    }
                                }
                            } else {
                                for (int bid = 0; bid < 4; ++bid) {
                                    if (count < RLE_CONT_BOUND[bid]) {
                                        uchar b0 = RLE_GRAY + RLE_BYTE_NUMBER[bid] +
                                                   (count & RLE_BOUND_0);
                                        count >>= 4;
                                        sum += static_cast<int>(b0);
                                        rle_buf[rle_pos++] = static_cast<char>(b0);
                                        rle_buf[rle_pos++] = static_cast<char>(color);
                                        sum += static_cast<int>(color);
                                        for (int k = bid; k >= 1; --k) {
                                            rle_buf[rle_pos++] = c[k - 1];
                                            sum += static_cast<int>(
                                                static_cast<uchar>(c[k - 1]));
                                        }
                                        break;
                                    }
                                }
                            }
                        };

                        if (total > 0) {
                            uchar cur   = data[0];
                            int   count = 1;
                            for (int j = 1; j < total; ++j) {
                                uchar px = data[j];
                                if (px == cur) {
                                    ++count;
                                } else {
                                    flush_run(cur, count);
                                    cur   = px;
                                    count = 1;
                                }
                            }
                            flush_run(cur, count);
                        }

                        rle_buf[rle_pos++] = static_cast<char>(
                            static_cast<uchar>((~sum) & 0xff)); // checksum

                        // Both mat and rle_buf are TLS — do NOT free them.
                        // They persist across all layers on this thread.

                        sla::RasterCache::write_layer(
                            cache_key, lid, rle_buf.data(), rle_pos);

                        // 5.4 CAS throttle: at most one report_status per 1% increment
                        int done = completed_layers.fetch_add(
                                       1, std::memory_order_relaxed) + 1;
                        int pct  = static_cast<int>(done * 100 /
                                       static_cast<int>(N));
                        int prev = last_reported_pct.load(
                                       std::memory_order_relaxed);
                        if (pct > prev &&
                            last_reported_pct.compare_exchange_strong(
                                prev, pct,
                                std::memory_order_relaxed,
                                std::memory_order_relaxed)) {
                            report_status(pct, L("Rasterizing layers..."));
                        }
                    }
                },
                tbb::auto_partitioner{});
        });

        // Write the completion sentinel after all layers are fully written.
        // is_valid() now checks for this file, so a process killed mid-rasterize
        // cannot leave a partially-written cache that appears valid on next run.
        sla::RasterCache::mark_complete(cache_key);
    }
}

std::string SLAPrint::Steps::label(SLAPrintObjectStep step)
{
    return OBJ_STEP_LABELS(step);
}

std::string SLAPrint::Steps::label(SLAPrintStep step)
{
    return PRINT_STEP_LABELS(step);
}

double SLAPrint::Steps::progressrange(SLAPrintObjectStep step) const
{
    return OBJ_STEP_LEVELS[step] * objectstep_scale;
}

double SLAPrint::Steps::progressrange(SLAPrintStep step) const
{
    return PRINT_STEP_LEVELS[step] * (100 - max_objstatus) / 100.0;
}

void SLAPrint::Steps::execute(SLAPrintObjectStep step, SLAPrintObject &obj)
{
    switch(step) {
    case slaposAssembly:      mesh_assembly(obj); break;
    case slaposHollowing:     hollow_model(obj); break;
    case slaposDrillHoles:    drill_holes(obj); break;
    case slaposObjectSlice:   slice_model(obj); break;
    case slaposSupportPoints: support_points(obj); break;
    case slaposSupportTree:   support_tree(obj); break;
    case slaposPad:           generate_pad(obj); break;
    case slaposSliceSupports: slice_supports(obj); break;
    case slaposCount: assert(false);
    }
}

void SLAPrint::Steps::execute(SLAPrintStep step)
{
    switch (step) {
    case slapsMergeSlicesAndEval: merge_slices_and_eval_stats(); break;
    case slapsRasterize: rasterize(); break;
    case slapsCount: assert(false);
    }
}

}
