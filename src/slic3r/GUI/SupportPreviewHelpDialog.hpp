#ifndef slic3r_GUI_SupportPreviewHelpDialog_hpp_
#define slic3r_GUI_SupportPreviewHelpDialog_hpp_

#include "GUI_Utils.hpp"

namespace Slic3r {
namespace GUI {

/// Modal diagram for SLA Support tab — title string: "Support preview description" (gettext). Image from contact type (sphere vs none).
class SupportPreviewHelpDialog : public DPIDialog
{
public:
    /// @param sphere_mode  true → 球體 (Figma 2:79), false → 無 (Figma 2:9)
    SupportPreviewHelpDialog(wxWindow* parent, bool sphere_mode);
    ~SupportPreviewHelpDialog() override = default;

private:
    bool m_sphere_mode{ false };
    void build_ui();
    void on_dpi_changed(const wxRect& suggested_rect) override;
};

} // namespace GUI
} // namespace Slic3r

#endif
