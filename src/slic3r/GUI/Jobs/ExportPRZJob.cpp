#include "ExportPRZJob.hpp"

#include <fstream>
#include <stdexcept>

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include "libslic3r/Format/PhrozenPRZ.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"

namespace fs = boost::filesystem;

namespace Slic3r { namespace GUI {

ExportPRZJob::ExportPRZJob(Plater *plater, const SLAPrint &print, ThumbnailData thumb,
                           fs::path output_path, bool removable)
    : m_plater{plater}
    , m_print{print}
    , m_thumb{std::move(thumb)}
    , m_output_path{std::move(output_path)}
    , m_path_on_removable_media{removable}
{}

void ExportPRZJob::process(Ctl &ctl)
{
    ctl.update_status(0, "Exporting PRZ...");

    std::ofstream ofs(m_output_path.string(), std::ios::binary);
    if (!ofs)
        throw std::runtime_error("Cannot open file for writing: " + m_output_path.string());

    Slic3r::generate_prz(ofs, m_print,
                         m_thumb.is_valid() ? &m_thumb : nullptr,
                         [&ctl](int pct) {
                             ctl.update_status(pct, "Exporting PRZ...");
                             return !ctl.was_canceled();
                         });

    if (!ofs)
        throw std::runtime_error("Failed to write PRZ file: " + m_output_path.string());

    ctl.update_status(100, "PRZ export complete");
}

void ExportPRZJob::finalize(bool canceled, std::exception_ptr &eptr)
{
    if (eptr) {
        try {
            std::rethrow_exception(eptr);
        } catch (const std::exception &ex) {
            show_error(m_plater, ex.what(), false);
            eptr = nullptr;
        }
        boost::system::error_code ec;
        fs::remove(m_output_path, ec);
        return;
    }

    if (canceled) {
        boost::system::error_code ec;
        fs::remove(m_output_path, ec);
        return;
    }

    wxGetApp().app_config->update_last_output_dir(
        m_output_path.parent_path().string(), m_path_on_removable_media);
}

}} // namespace Slic3r::GUI
