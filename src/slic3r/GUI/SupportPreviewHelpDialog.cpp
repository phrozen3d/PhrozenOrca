#include "SupportPreviewHelpDialog.hpp"

#include "BitmapCache.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "Widgets/SupportAnnotationLabel.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <wx/panel.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>

namespace Slic3r {
namespace GUI {

namespace {

// var_dir is resources/images — same convention as ScalableBitmap (no leading "images/").
static const char* k_svg_sphere = "PhrozenImages_Resin/support_preview_diagram_2_79";
static const char* k_svg_none   = "PhrozenImages_Resin/support_preview_diagram_2_9";

// Content area: wider than Figma 600 so long English labels fit in side margins without covering the diagram.
static constexpr int k_canvas_design_w = 760;
static constexpr int k_canvas_design_h = 400;

// SVG viewBox (support_preview_diagram_2_79.svg / 2_9.svg) — vertical layout uses Figma diagram top + height.
static constexpr double k_svg_view_w = 358.469;
static constexpr double k_svg_view_h = 319.492;
// Figma: diagram group 2:79 top in frame 2:78 (design px).
static constexpr int k_figma_diagram_top_y = 26;
// "頂部連接長度" / pinhead row: Figma chip center ≈(300−36)/358.469≈0.74 of diagram width, but our bitmap is canvas-centered so that reads too far right; split the difference vs. geometric center (0.5).
static constexpr double k_figma_top_link_chip_center_x_frac = 0.58;

enum class AnnotationSide : uint8_t
{
    LeftOfDiagram,   // chip stays in left margin; never overlaps bitmap
    RightOfDiagram,  // chip stays in right margin
    /// Top link / pinhead row: X from `k_figma_top_link_chip_center_x_frac` (not geometric bitmap center).
    CenterOnDiagram,
};

struct AnnotationSpec
{
    int            design_y; // Figma 2:78 absolute Y (design px) — chip top, same as design file
    AnnotationSide side;
    /// gettext msgid (English); `L()` is picked up by xgettext (see scripts/run_gettext.* --keyword=L).
    const char*    msgid;
    /// Extra DIP (after anchor); only used for `CenterOnDiagram` fine-tuning.
    int            h_nudge{ 0 };
};

// Figma 2:72 — Y only; X comes from layout (margins), so English/i18n width does not push into the art.
static const AnnotationSpec k_sphere_annotations[] = {
    { 70, AnnotationSide::LeftOfDiagram, L("Contact Depth"), 0 },
    { 150, AnnotationSide::LeftOfDiagram, L("Pillar Diameter"), 0 },
    { 299, AnnotationSide::LeftOfDiagram, L("Bottom Contact Diameter"), 0 },
    { 326, AnnotationSide::LeftOfDiagram, L("Bottom Contact Diameter"), 0 },
    { 70, AnnotationSide::RightOfDiagram, L("Sphere size"), 0 },
    { 98, AnnotationSide::RightOfDiagram, L("Upper Diameter"), 0 },
    { 127, AnnotationSide::CenterOnDiagram, L("Pinhead Width"), 0 },
    { 299, AnnotationSide::RightOfDiagram, L("Support Boss Height"), 0 },
    { 326, AnnotationSide::RightOfDiagram, L("Support Bottom Diameter"), 0 },
};

static const AnnotationSpec k_none_annotations[] = {
    { 70, AnnotationSide::LeftOfDiagram, L("Contact Depth"), 0 },
    { 150, AnnotationSide::LeftOfDiagram, L("Pillar Diameter"), 0 },
    { 299, AnnotationSide::LeftOfDiagram, L("Bottom Contact Diameter"), 0 },
    { 326, AnnotationSide::LeftOfDiagram, L("Bottom Contact Diameter"), 0 },
    { 76, AnnotationSide::RightOfDiagram, L("Upper Diameter"), 0 },
    { 127, AnnotationSide::CenterOnDiagram, L("Pinhead Width"), 0 },
    { 299, AnnotationSide::RightOfDiagram, L("Support Boss Height"), 0 },
    { 326, AnnotationSide::RightOfDiagram, L("Support Bottom Diameter"), 0 },
};

/// Bitmap height (DIP) so that diagram width leaves ~margin_side DIP on each side of the canvas for labels.
static unsigned diagram_target_height_for_canvas(wxWindow* w)
{
    const int cw          = w->FromDIP(k_canvas_design_w);
    const int margin_side = w->FromDIP(218); // reserve each side for longest one-line label + padding (English)
    const int max_bw      = std::max(w->FromDIP(140), cw - 2 * margin_side);
    const unsigned h      = static_cast<unsigned>(std::max(100, static_cast<int>(std::lround(max_bw * k_svg_view_h / k_svg_view_w))));
    return h;
}

static void place_annotation(wxWindow* canvas, int ox, int oy, int bw, int bh, const AnnotationSpec& spec)
{
    auto* chip = new SupportAnnotationLabel(canvas, _L(spec.msgid));
    chip->Layout();
    const wxSize sz = chip->GetSize();
    const int    gap_canvas = canvas->FromDIP(8);
    const int    margin     = canvas->FromDIP(10);

    const double y_scale = bh / k_svg_view_h;
    int          y       = oy + static_cast<int>(std::lround((spec.design_y - k_figma_diagram_top_y) * y_scale));
    y                    = std::max(gap_canvas, std::min(y, canvas->FromDIP(k_canvas_design_h) - sz.GetHeight() - gap_canvas));

    int x = 0;
    switch (spec.side) {
    case AnnotationSide::LeftOfDiagram:
        // Entire chip to the left of the diagram — avoids overlap for any translation length (until canvas edge).
        x = ox - margin - sz.GetWidth();
        if (x < gap_canvas)
            x = gap_canvas;
        break;
    case AnnotationSide::RightOfDiagram:
        x = ox + bw + margin;
        if (x + sz.GetWidth() > canvas->FromDIP(k_canvas_design_w) - gap_canvas)
            x = canvas->FromDIP(k_canvas_design_w) - gap_canvas - sz.GetWidth();
        break;
    case AnnotationSide::CenterOnDiagram: {
        const int anchor_x = ox + static_cast<int>(std::lround(static_cast<double>(bw) * k_figma_top_link_chip_center_x_frac));
        x = anchor_x - sz.GetWidth() / 2 + canvas->FromDIP(spec.h_nudge);
        if (x < gap_canvas)
            x = gap_canvas;
        if (x + sz.GetWidth() > canvas->FromDIP(k_canvas_design_w) - gap_canvas)
            x = canvas->FromDIP(k_canvas_design_w) - gap_canvas - sz.GetWidth();
        break;
    }
    }

    chip->SetPosition(wxPoint(x, y));
}

} // namespace

SupportPreviewHelpDialog::SupportPreviewHelpDialog(wxWindow* parent, bool sphere_mode)
    : DPIDialog(parent,
                wxID_ANY,
                _L(L("Support preview description")),
                wxDefaultPosition,
                wxDefaultSize,
                wxDEFAULT_DIALOG_STYLE)
    , m_sphere_mode(sphere_mode)
{
    SetFont(wxGetApp().normal_font());
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    build_ui();
    if (MainFrame* mf = wxGetApp().mainframe) {
        const wxRect r = mf->GetScreenRect();
        const wxSize d = GetSize();
        const int    nx = r.x + std::max(0, (r.width - d.x) / 2);
        const int    ny = r.y + std::max(0, (r.height - d.y) / 2);
        SetPosition(wxPoint(nx, ny));
    } else {
        CenterOnParent();
    }
    wxGetApp().UpdateDlgDarkUI(this);
}

void SupportPreviewHelpDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    (void) suggested_rect;
    Refresh();
}

void SupportPreviewHelpDialog::build_ui()
{
    auto* main = new wxBoxSizer(wxVERTICAL);

    wxBitmap diagram;
    int      diagram_w = 0;
    int      diagram_h = 0;
    {
        BitmapCache    cache;
        const char* const svg_key = m_sphere_mode ? k_svg_sphere : k_svg_none;
        const unsigned    target_h = diagram_target_height_for_canvas(this);
        if (wxBitmap* bp = cache.load_svg(svg_key, 0, target_h, false);
            bp != nullptr && bp->IsOk()) {
            diagram   = *bp;
            diagram_w = diagram.GetWidth();
            diagram_h = diagram.GetHeight();
        }
    }

    auto* canvas = new wxPanel(this, wxID_ANY, wxDefaultPosition,
                               wxSize(FromDIP(k_canvas_design_w), FromDIP(k_canvas_design_h)));
    canvas->SetBackgroundColour(wxColour(255, 255, 255));
    canvas->SetMinSize(wxSize(FromDIP(k_canvas_design_w), FromDIP(k_canvas_design_h)));

    const int laid_w = canvas->FromDIP(k_canvas_design_w);
    const int laid_h = canvas->FromDIP(k_canvas_design_h);

    int ox = 0;
    int oy = 0;
    if (diagram.IsOk()) {
        ox = std::max(canvas->FromDIP(8), (laid_w - diagram_w) / 2);
        oy = std::max(canvas->FromDIP(10), (laid_h - diagram_h) / 2);
        auto* img = new wxStaticBitmap(canvas, wxID_ANY, diagram);
        img->SetPosition(wxPoint(ox, oy));
    }

    const std::vector<AnnotationSpec> specs(
        m_sphere_mode ? std::vector<AnnotationSpec>(std::begin(k_sphere_annotations), std::end(k_sphere_annotations))
                      : std::vector<AnnotationSpec>(std::begin(k_none_annotations), std::end(k_none_annotations)));
    for (const AnnotationSpec& s : specs)
        place_annotation(canvas, ox, oy, diagram_w, diagram_h, s);

    main->Add(canvas, 0, wxALIGN_CENTER | wxALL, FromDIP(10));

    SetSizer(main);
    main->Fit(this);
    SetMinClientSize(main->GetMinSize());
}

} // namespace GUI
} // namespace Slic3r
