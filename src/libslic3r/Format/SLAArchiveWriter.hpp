#ifndef SLAARCHIVEWRITER_HPP
#define SLAARCHIVEWRITER_HPP

#include <stddef.h>
#include <vector>
#include <memory>
#include <string>

#include "libslic3r/SLA/RasterBase.hpp"
#include "libslic3r/Execution/Execution.hpp"

namespace Slic3r {

class SLAPrint;
class SLAPrinterConfig;

// Forward declaration — full definition in GCode/ThumbnailData.hpp
struct ThumbnailData;
using ThumbnailsList = std::vector<ThumbnailData>;

class SLAArchiveWriter {
protected:
    std::vector<sla::EncodedRaster> m_layers;

    virtual std::unique_ptr<sla::RasterBase> create_raster() const = 0;
    virtual sla::RasterEncoder get_encoder() const = 0;

public:
    virtual ~SLAArchiveWriter() = default;

    // Apply printer config changes. Implementations should clear m_layers
    // if the config affects rasterization parameters.
    virtual void apply(const SLAPrinterConfig &cfg) = 0;

    // Fn have to be thread safe: void(sla::RasterBase& raster, size_t lyrid);
    template<class Fn, class CancelFn, class EP>
    void draw_layers(
        size_t     layer_num,
        Fn &&      drawfn,
        CancelFn   cancelfn,
        const EP & ep)
    {
        m_layers.resize(layer_num);
        execution::for_each(
            ep, size_t(0), m_layers.size(),
            [this, &drawfn, &cancelfn](size_t idx) {
                if (cancelfn()) return;

                sla::EncodedRaster &enc = m_layers[idx];
                auto                rst = create_raster();
                drawfn(*rst, idx);
                enc = rst->encode(get_encoder());
            },
            execution::max_concurrency(ep));
    }

    // Export the print into an archive using the provided filename.
    virtual void export_print(const std::string     fname,
                              const SLAPrint       &print,
                              const ThumbnailsList &thumbnails,
                              const std::string    &projectname = "") = 0;

    // Factory method to create an archiver instance based on format ID
    static std::unique_ptr<SLAArchiveWriter> create(
        const std::string &archtype, const SLAPrinterConfig &);
};

} // namespace Slic3r

#endif // SLAARCHIVEWRITER_HPP
