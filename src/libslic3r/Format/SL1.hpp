#ifndef ARCHIVETRAITS_HPP
#define ARCHIVETRAITS_HPP

#include <string>

#include "SLAArchiveWriter.hpp"
#include "SLAArchiveReader.hpp"

#include "libslic3r/Zipper.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {

class SL1Archive: public SLAArchiveWriter {
    SLAPrinterConfig m_cfg;

protected:
    std::unique_ptr<sla::RasterBase> create_raster() const override;
    sla::RasterEncoder get_encoder() const override;

    SLAPrinterConfig & cfg() { return m_cfg; }
    const SLAPrinterConfig & cfg() const { return m_cfg; }

    void export_print(Zipper &,
                      const SLAPrint       &print,
                      const ThumbnailsList &thumbnails,
                      const std::string    &projectname);

public:

    SL1Archive() = default;
    explicit SL1Archive(const SLAPrinterConfig &cfg): m_cfg(cfg) {}
    explicit SL1Archive(SLAPrinterConfig &&cfg): m_cfg(std::move(cfg)) {}

    // Legacy export (without thumbnails) - used by BackgroundSlicingProcess
    void export_print(Zipper &zipper, const SLAPrint &print, const std::string &projectname = "");
    void export_print(const std::string &fname, const SLAPrint &print, const std::string &projectname = "")
    {
        Zipper zipper(fname);
        export_print(zipper, print, projectname);
    }

    // SLAArchiveWriter override (with thumbnails)
    void export_print(const std::string     fname,
                      const SLAPrint       &print,
                      const ThumbnailsList &thumbnails,
                      const std::string    &projectname = "") override;

    void apply(const SLAPrinterConfig &cfg) override
    {
        auto diff = m_cfg.diff(cfg);
        if (!diff.empty()) {
            m_cfg.apply_only(cfg, diff);
            m_layers = {};
        }
    }
};

class SL1Reader: public SLAArchiveReader {
    SLAImportQuality m_quality = SLAImportQuality::Balanced;
    std::function<bool(int)> m_progr;
    std::string m_fname;

public:
    ConfigSubstitutions read(std::vector<ExPolygons> &slices,
                             DynamicPrintConfig      &profile_out) override;

    ConfigSubstitutions read(DynamicPrintConfig &profile) override;

    SL1Reader() = default;
    SL1Reader(const std::string       &fname,
              SLAImportQuality         quality,
              std::function<bool(int)> progr)
        : m_quality(quality), m_progr(progr), m_fname(fname)
    {}
};

// Legacy import functions (backward compatible, without format_id)
ConfigSubstitutions import_sla_archive(const std::string &zipfname, DynamicPrintConfig &out);

ConfigSubstitutions import_sla_archive(
    const std::string &      zipfname,
    Vec2i32                    windowsize,
    indexed_triangle_set &   out,
    DynamicPrintConfig &     profile,
    std::function<bool(int)> progr = [](int) { return true; });

inline ConfigSubstitutions import_sla_archive(
    const std::string &      zipfname,
    Vec2i32                    windowsize,
    indexed_triangle_set &   out,
    std::function<bool(int)> progr = [](int) { return true; })
{
    DynamicPrintConfig profile;
    return import_sla_archive(zipfname, windowsize, out, profile, progr);
}

} // namespace Slic3r::sla

#endif // ARCHIVETRAITS_HPP
