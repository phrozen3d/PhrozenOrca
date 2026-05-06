#pragma once

#include <boost/filesystem/path.hpp>

#include "Job.hpp"
#include "libslic3r/GCode/ThumbnailData.hpp"

namespace Slic3r { class SLAPrint; }

namespace Slic3r { namespace GUI {

class Plater;

class ExportPRZJob : public Job {
    Plater                  *m_plater;
    const SLAPrint          &m_print;
    ThumbnailData            m_thumb;
    boost::filesystem::path  m_output_path;
    bool                     m_path_on_removable_media;

public:
    ExportPRZJob(Plater *plater, const SLAPrint &print, ThumbnailData thumb,
                 boost::filesystem::path output_path, bool removable);

    void process(Ctl &ctl) override;
    void finalize(bool canceled, std::exception_ptr &eptr) override;
};

}} // namespace Slic3r::GUI
