#include "RasterToCvMat.hpp"
#include "libslic3r/SLA/RasterBase.hpp"

#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>

namespace Slic3r {
namespace sla {

cv::Mat expolygons_to_cvmat(
    const ExPolygons        &polys,
    const Resolution        &res,
    const PixelDim          &pxdim,
    const RasterBase::Trafo &trafo,
    double                   gamma)
{
    auto raster = create_raster_grayscale_aa(res, pxdim, gamma, trafo);

    for (const ExPolygon &poly : polys)
        raster->draw(poly);

    // Extract the raw pixel buffer without PNG compression using a passthrough encoder.
    auto raw_enc = [](const void *ptr, size_t w, size_t h, size_t /*num_components*/) {
        const auto *buf = static_cast<const uint8_t *>(ptr);
        return EncodedRaster(std::vector<uint8_t>(buf, buf + w * h), "raw");
    };

    EncodedRaster enc = raster->encode(raw_enc);

    // Wrap buffer in a cv::Mat and clone to take ownership before enc goes out of scope.
    cv::Mat mat(int(res.height_px), int(res.width_px), CV_8UC1,
                const_cast<void *>(enc.data()));
    return mat.clone();
}

std::vector<cv::Mat> expolygons_layers_to_cvmat(
    const std::vector<ExPolygons> &layer_polys,
    const Resolution              &res,
    const PixelDim                &pxdim,
    const RasterBase::Trafo       &trafo,
    double                         gamma)
{
    std::vector<cv::Mat> result(layer_polys.size());

    tbb::parallel_for(tbb::blocked_range<size_t>(0, layer_polys.size()),
        [&](const tbb::blocked_range<size_t> &r) {
            for (size_t i = r.begin(); i < r.end(); ++i)
                result[i] = expolygons_to_cvmat(layer_polys[i], res, pxdim, trafo, gamma);
        });

    return result;
}

} // namespace sla
} // namespace Slic3r
