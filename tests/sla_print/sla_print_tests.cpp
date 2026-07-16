#include <unordered_set>
#include <unordered_map>
#include <random>
#include <numeric>
#include <cstdint>

#include "sla_test_utils.hpp"

#include <libslic3r/TriangleMeshSlicer.hpp>
#include <libslic3r/SLA/SupportTreeMesher.hpp>
#include <libslic3r/SLA/Concurrency.hpp>
#include <libslic3r/SLA/ConcaveHull.hpp>

namespace {

const char *const BELOW_PAD_TEST_OBJECTS[] = {
    "20mm_cube.obj",
    "V.obj",
};

const char *const AROUND_PAD_TEST_OBJECTS[] = {
    "20mm_cube.obj",
    "V.obj",
    "frog_legs.obj",
    "cube_with_concave_hole_enlarged.obj",
};

const char *const SUPPORT_TEST_MODELS[] = {
    "cube_with_concave_hole_enlarged_standing.obj",
    "A_upsidedown.obj",
    "extruder_idler.obj"
};

// An axis-aligned square island for the edge-gap unit tests. Arguments in mm.
ExPolygon make_square_island(double cx_mm, double cy_mm, double size_mm)
{
    const coord_t h  = scaled(size_mm / 2.);
    const coord_t cx = scaled(cx_mm), cy = scaled(cy_mm);

    Polygon p;
    p.points = {{cx - h, cy - h}, {cx + h, cy - h}, {cx + h, cy + h}, {cx - h, cy + h}};

    return ExPolygon{p};
}

} // namespace

TEST_CASE("Pillar pairhash should be unique", "[SLASupportGeneration]") {
    test_pairhash<int, int>();
    test_pairhash<int, long>();
    test_pairhash<unsigned, unsigned>();
    test_pairhash<unsigned, unsigned long>();
}

TEST_CASE("Support point generator should be deterministic if seeded", 
          "[SLASupportGeneration], [SLAPointGen]") {
    TriangleMesh mesh = load_model("A_upsidedown.obj");
    
    sla::IndexedMesh emesh{mesh};
    
    sla::SupportTreeConfig supportcfg;
    sla::SupportPointGenerator::Config autogencfg;
    autogencfg.head_diameter = float(2 * supportcfg.head_front_radius_mm);
    sla::SupportPointGenerator point_gen{emesh, autogencfg, [] {}, [](int) {}};
        
    auto   bb      = mesh.bounding_box();
    double zmin    = bb.min.z();
    double zmax    = bb.max.z();
    double gnd     = zmin - supportcfg.object_elevation_mm;
    auto   layer_h = 0.05f;
    
    auto slicegrid = grid(float(gnd), float(zmax), layer_h);
    std::vector<ExPolygons> slices = slice_mesh_ex(mesh.its, slicegrid, CLOSING_RADIUS);
    
    point_gen.seed(0);
    point_gen.execute(slices, slicegrid);
    
    auto get_chksum = [](const std::vector<sla::SupportPoint> &pts){
        int64_t chksum = 0;
        for (auto &pt : pts) {
            auto p = scaled(pt.pos);
            chksum += p.x() + p.y() + p.z();
        }
        
        return chksum;
    };
    
    int64_t checksum = get_chksum(point_gen.output());
    size_t ptnum = point_gen.output().size();
    REQUIRE(point_gen.output().size() > 0);
    
    for (int i = 0; i < 20; ++i) {
        point_gen.output().clear();
        point_gen.seed(0);
        point_gen.execute(slices, slicegrid);
        REQUIRE(point_gen.output().size() == ptnum);
        REQUIRE(checksum == get_chksum(point_gen.output()));
    }
}

TEST_CASE("Flat pad geometry is valid", "[SLASupportGeneration]") {
    sla::PadConfig padcfg;

    // Disable wings
    padcfg.wall_height_mm = .0;

    // Exercise the legacy centroid merge path explicitly (split_rafts is on by default)
    padcfg.split_rafts = false;

    for (auto &fname : BELOW_PAD_TEST_OBJECTS) test_pad(fname, padcfg);
}

TEST_CASE("WingedPadGeometryIsValid", "[SLASupportGeneration]") {
    sla::PadConfig padcfg;

    // Add some wings to the pad to test the cavity
    padcfg.wall_height_mm = 1.;

    // Exercise the legacy centroid merge path explicitly (split_rafts is on by default)
    padcfg.split_rafts = false;

    for (auto &fname : BELOW_PAD_TEST_OBJECTS) test_pad(fname, padcfg);
}

TEST_CASE("FlatPadAroundObjectIsValid", "[SLASupportGeneration]") {
    sla::PadConfig padcfg;

    // Add some wings to the pad to test the cavity
    padcfg.wall_height_mm = 0.;
    // padcfg.embed_object.stick_stride_mm = 0.;
    padcfg.embed_object.enabled = true;
    padcfg.embed_object.everywhere = true;

    // Exercise the legacy centroid merge path explicitly (split_rafts is on by default)
    padcfg.split_rafts = false;

    for (auto &fname : AROUND_PAD_TEST_OBJECTS) test_pad(fname, padcfg);
}

TEST_CASE("WingedPadAroundObjectIsValid", "[SLASupportGeneration]") {
    sla::PadConfig padcfg;

    // Add some wings to the pad to test the cavity
    padcfg.wall_height_mm = 1.;
    padcfg.embed_object.enabled = true;
    padcfg.embed_object.everywhere = true;

    // Exercise the legacy centroid merge path explicitly (split_rafts is on by default)
    padcfg.split_rafts = false;

    for (auto &fname : AROUND_PAD_TEST_OBJECTS) test_pad(fname, padcfg);
}

// pad_split_rafts is ON by default: the pad tests above opt out explicitly to keep
// covering the legacy centroid path. The tests below cover the default edge-gap
// path on both pad routes and its core decisions.

TEST_CASE("SplitRaftsPadGeometryIsValid", "[SLASupportGeneration]") {
    sla::PadConfig padcfg;

    padcfg.wall_height_mm        = .0;
    padcfg.split_rafts           = true;
    padcfg.raft_gap_threshold_mm = 5.;
    padcfg.raft_bridge_width_mm  = 2.;

    for (auto &fname : BELOW_PAD_TEST_OBJECTS) test_pad(fname, padcfg);
}

TEST_CASE("SplitRaftsPadAroundObjectIsValid", "[SLASupportGeneration]") {
    sla::PadConfig padcfg;

    padcfg.wall_height_mm          = .0;
    padcfg.split_rafts             = true;
    padcfg.raft_gap_threshold_mm   = 5.;
    padcfg.raft_bridge_width_mm    = 2.;
    padcfg.embed_object.enabled    = true;
    padcfg.embed_object.everywhere = true;

    for (auto &fname : AROUND_PAD_TEST_OBJECTS) test_pad(fname, padcfg);
}

TEST_CASE("Edge gap raft splitting decides merging by real contour gap",
          "[SLASupportGeneration]") {
    // Two 10mm squares 33mm apart centre to centre => 23mm contour gap.
    ExPolygons islands = {make_square_island(0., 0., 10.),
                          make_square_island(33., 0., 10.)};

    const coord_t bridge_w = scaled(2.);
    auto          nocancel = [] {};

    SECTION("threshold below the gap keeps the islands as separate rafts") {
        sla::ConcaveHull hull{islands, scaled(8.2), bridge_w,
                              sla::EdgeGapMerge{}, nocancel};

        REQUIRE(hull.polygons().size() == 2);
    }

    SECTION("threshold above the gap bridges them into a single raft") {
        sla::ConcaveHull hull{islands, scaled(30.), bridge_w,
                              sla::EdgeGapMerge{}, nocancel};

        REQUIRE(hull.polygons().size() == 1);
    }
}

TEST_CASE("Edge gap raft splitting is deterministic", "[SLASupportGeneration]") {
    // Three islands within the threshold of each other: every pair is bridged
    // (no MST pruning), which closes a loop.
    ExPolygons islands = {make_square_island(0., 0., 10.),
                          make_square_island(14., 0., 10.),
                          make_square_island(7., 14., 10.)};

    const coord_t raw_thresh = scaled(8.2), bridge_w = scaled(2.);
    auto          nocancel   = [] {};

    sla::ConcaveHull a{islands, raw_thresh, bridge_w, sla::EdgeGapMerge{}, nocancel};
    sla::ConcaveHull b{islands, raw_thresh, bridge_w, sla::EdgeGapMerge{}, nocancel};

    REQUIRE(a.polygons().size() == b.polygons().size());

    for (size_t i = 0; i < a.polygons().size(); ++i) {
        const Points &pa = a.polygons()[i].points;
        const Points &pb = b.polygons()[i].points;

        REQUIRE(pa.size() == pb.size());

        for (size_t k = 0; k < pa.size(); ++k) {
            REQUIRE(pa[k].x() == pb[k].x());
            REQUIRE(pa[k].y() == pb[k].y());
        }
    }
}

TEST_CASE("Edge gap raft bridges are clamped to tiny islands",
          "[SLASupportGeneration]") {
    // A 10mm island and a 1mm island 2.5mm apart, asked for a bridge five times
    // wider than the tiny island. Without the clamp the bridge would penetrate by
    // the full bridge width and shoot straight through the tiny island.
    ExPolygons islands = {make_square_island(0., 0., 10.),
                          make_square_island(8., 0., 1.)};

    const coord_t raw_thresh = scaled(8.);   // 2.5mm gap is well within it
    const coord_t bridge_w   = scaled(5.);
    auto          nocancel   = [] {};

    sla::ConcaveHull hull{islands, raw_thresh, bridge_w, sla::EdgeGapMerge{},
                          nocancel};

    // Within the threshold, so the two islands end up bridged into one raft.
    REQUIRE(hull.polygons().size() == 1);

    // The tiny island spans x = 7.5..8.5mm. The clamp caps the penetration at
    // half its size, so the bridge must stop inside it and never widen the raft
    // past the island's own far edge.
    const BoundingBox bb = hull.polygons().front().bounding_box();

    REQUIRE(bb.max.x() <= scaled(8.5));
}

TEST_CASE("Edge gap raft bridges penetrate the islands and leave no sliver",
          "[SLASupportGeneration]") {
    // Two 10mm islands with a 2mm gap: x = -5..5 and x = 7..17.
    ExPolygons islands = {make_square_island(0., 0., 10.),
                          make_square_island(12., 0., 10.)};

    const coord_t raw_thresh = scaled(8.);
    const coord_t bridge_w   = scaled(2.);
    auto          nocancel   = [] {};

    sla::ConcaveHull hull{islands, raw_thresh, bridge_w, sla::EdgeGapMerge{},
                          nocancel};

    // One connected raft: a vertex-only touch would leave separate pieces here.
    REQUIRE(hull.polygons().size() == 1);

    ExPolygons ex = hull.to_expolygons();

    REQUIRE(ex.size() == 1);

    // The middle of the gap is solid, so the bridge really spans it.
    REQUIRE(ex.front().contains(Point{scaled(6.), scaled(0.)}));

    // The bridge adds material in the gap on top of the two islands, which a
    // degenerate zero-overlap connector would not.
    const double islands_area = 2. * double(scaled(10.)) * double(scaled(10.));

    REQUIRE(ex.front().area() > islands_area);
}

TEST_CASE("ElevatedSupportGeometryIsValid", "[SLASupportGeneration]") {
    sla::SupportTreeConfig supportcfg;
    supportcfg.object_elevation_mm = 10.;
    
    for (auto fname : SUPPORT_TEST_MODELS) test_supports(fname, supportcfg);
}

TEST_CASE("FloorSupportGeometryIsValid", "[SLASupportGeneration]") {
    sla::SupportTreeConfig supportcfg;
    supportcfg.object_elevation_mm = 0;
    
    for (auto &fname: SUPPORT_TEST_MODELS) test_supports(fname, supportcfg);
}

TEST_CASE("ElevatedSupportsDoNotPierceModel", "[SLASupportGeneration]") {
    
    sla::SupportTreeConfig supportcfg;
    
    for (auto fname : SUPPORT_TEST_MODELS)
        test_support_model_collision(fname, supportcfg);
}

TEST_CASE("FloorSupportsDoNotPierceModel", "[SLASupportGeneration]") {
    
    sla::SupportTreeConfig supportcfg;
    supportcfg.object_elevation_mm = 0;
    
    for (auto fname : SUPPORT_TEST_MODELS)
        test_support_model_collision(fname, supportcfg);
}

TEST_CASE("InitializedRasterShouldBeNONEmpty", "[SLARasterOutput]") {
    // Default SL1 display parameters
    sla::RasterBase::Resolution res{2560, 1440};
    sla::RasterBase::PixelDim   pixdim{120. / res.width_px, 68. / res.height_px};
    
    sla::RasterGrayscaleAAGammaPower raster(res, pixdim, {}, 1.);
    REQUIRE(raster.resolution().width_px == res.width_px);
    REQUIRE(raster.resolution().height_px == res.height_px);
    REQUIRE(raster.pixel_dimensions().w_mm == Approx(pixdim.w_mm));
    REQUIRE(raster.pixel_dimensions().h_mm == Approx(pixdim.h_mm));
}

TEST_CASE("MirroringShouldBeCorrect", "[SLARasterOutput]") {
    sla::RasterBase::TMirroring mirrorings[] = {sla::RasterBase::NoMirror,
                                                sla::RasterBase::MirrorX,
                                                sla::RasterBase::MirrorY,
                                                sla::RasterBase::MirrorXY};

    sla::RasterBase::Orientation orientations[] =
        {sla::RasterBase::roLandscape, sla::RasterBase::roPortrait};
    
    for (auto orientation : orientations)
        for (auto &mirror : mirrorings)
            check_raster_transformations(orientation, mirror);
}


TEST_CASE("RasterizedPolygonAreaShouldMatch", "[SLARasterOutput]") {
    double disp_w = 120., disp_h = 68.;
    sla::RasterBase::Resolution res{2560, 1440};
    sla::RasterBase::PixelDim pixdim{disp_w / res.width_px, disp_h / res.height_px};
    
    double gamma = 1.;
    sla::RasterGrayscaleAAGammaPower raster(res, pixdim, {}, gamma);
    auto bb = BoundingBox({0, 0}, {scaled(disp_w), scaled(disp_h)});
    
    ExPolygon poly = square_with_hole(10.);
    poly.translate(bb.center().x(), bb.center().y());
    raster.draw(poly);
    
    double a = poly.area() / (scaled<double>(1.) * scaled(1.));
    double ra = raster_white_area(raster);
    double diff = std::abs(a - ra);
    
    REQUIRE(diff <= predict_error(poly, pixdim));
    
    raster.clear();
    poly = square_with_hole(60.);
    poly.translate(bb.center().x(), bb.center().y());
    raster.draw(poly);
    
    a = poly.area() / (scaled<double>(1.) * scaled(1.));
    ra = raster_white_area(raster);
    diff = std::abs(a - ra);
    
    REQUIRE(diff <= predict_error(poly, pixdim));
    
    sla::RasterGrayscaleAA raster0(res, pixdim, {}, [](double) { return 0.; });
    REQUIRE(raster_pxsum(raster0) == 0);
    
    raster0.draw(poly);
    ra = raster_white_area(raster);
    REQUIRE(raster_pxsum(raster0) == 0);
}


TEST_CASE("halfcone test", "[halfcone]") {
    sla::DiffBridge br{Vec3d{1., 1., 1.}, Vec3d{10., 10., 10.}, 0.25, 0.5};

    indexed_triangle_set m = sla::get_mesh(br, 45);

    its_merge_vertices(m);
    its_write_obj(m, "Halfcone.obj");
}

TEST_CASE("Test concurrency")
{
    std::vector<double> vals = grid(0., 100., 10.);

    double ref = std::accumulate(vals.begin(), vals.end(), 0.);

    double s = execution::accumulate(ex_tbb, vals.begin(), vals.end(), 0.);

    REQUIRE(s == Approx(ref));
}
