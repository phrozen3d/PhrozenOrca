#include "SupportPreviewHelpDialog.hpp"

#include "BitmapCache.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "Widgets/SupportAnnotationLabel.hpp"
#include "Widgets/Label.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <wx/dcclient.h>
#include <wx/panel.h>
#include <wx/settings.h>
#include <wx/sizer.h>

namespace Slic3r {
namespace GUI {

namespace {

/// wxStaticBitmap (native SS_BITMAP) can paint over sibling custom windows on MSW — use painted panel instead.
class DiagramBitmapPanel : public wxPanel
{
public:
    DiagramBitmapPanel(wxWindow* parent, wxBitmap bitmap)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, bitmap.GetSize(), wxBORDER_NONE)
        , m_bitmap(std::move(bitmap))
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent& e) {});
        Bind(wxEVT_PAINT, [this](wxPaintEvent& e) {
            wxPaintDC dc(this);
            if (m_bitmap.IsOk())
                dc.DrawBitmap(m_bitmap, 0, 0);
            e.Skip();
        });
    }

private:
    wxBitmap m_bitmap;
};

static const char* k_svg_sphere = "PhrozenImages_Resin/support_preview_diagram_contactType_sphere";
static const char* k_svg_none   = "PhrozenImages_Resin/support_preview_diagram_contactType_none";

static constexpr int k_canvas_design_w = 760;
static constexpr int k_canvas_design_h = 400;

static constexpr double k_svg_view_w = 358.469;
static constexpr double k_svg_view_h = 319.492;
static constexpr int      k_figma_diagram_top_y = 26;
static constexpr double   k_figma_top_link_chip_center_x_frac = 0.58;

enum class AnnotationSide : uint8_t
{
    LeftOfDiagram,
    RightOfDiagram,
    CenterOnDiagram,
};

struct AnnotationSpec
{
    int            design_y;
    AnnotationSide side;
    const char*    msgid;
    int            h_nudge{ 0 };
};

static const AnnotationSpec k_sphere_annotations[] = {
    { 70, AnnotationSide::LeftOfDiagram, L("Contact Depth"), 0 },
    { 150, AnnotationSide::LeftOfDiagram, L("Pillar Diameter"), 0 },
    { 299, AnnotationSide::LeftOfDiagram, L("Bottom Contact Diameter"), 0 },
    { 326, AnnotationSide::LeftOfDiagram, L("Raft Thickness"), 0 },
    { 70, AnnotationSide::RightOfDiagram, L("Contact Diameter"), 0 },
    { 98, AnnotationSide::RightOfDiagram, L("Upper Diameter"), 0 },
    { 127, AnnotationSide::CenterOnDiagram, L("Segment Length"), 0 },
    { 299, AnnotationSide::RightOfDiagram, L("Support Boss Height"), 0 },
    { 326, AnnotationSide::RightOfDiagram, L("Support Bottom Diameter"), 0 },
};

static const AnnotationSpec k_none_annotations[] = {
    { 70, AnnotationSide::LeftOfDiagram, L("Contact Depth"), 0 },
    { 150, AnnotationSide::LeftOfDiagram, L("Pillar Diameter"), 0 },
    {299, AnnotationSide::LeftOfDiagram, L("Bottom Contact Diameter"), 0},
    { 326, AnnotationSide::LeftOfDiagram, L("Raft Thickness"), 0 },
    { 76, AnnotationSide::RightOfDiagram, L("Upper Diameter"), 0 },
    { 127, AnnotationSide::CenterOnDiagram, L("Segment Length"), 0 },
    { 299, AnnotationSide::RightOfDiagram, L("Support Boss Height"), 0 },
    { 326, AnnotationSide::RightOfDiagram, L("Support Bottom Diameter"), 0 },
};

struct SideMargins
{
    int left{ 0 };
    int right{ 0 };
};

static SideMargins measure_annotation_margins(wxWindow* canvas, const std::vector<AnnotationSpec>& specs)
{
    const int margin = canvas->FromDIP(10);
    const int gap    = canvas->FromDIP(8);

    int max_left  = 0;
    int max_right = 0;

    for (const AnnotationSpec& s : specs) {
        const wxSize sz = SupportAnnotationLabel::preferred_size(canvas, _L(s.msgid));
        switch (s.side) {
        case AnnotationSide::LeftOfDiagram:
            max_left = std::max(max_left, sz.x);
            break;
        case AnnotationSide::RightOfDiagram:
            max_right = std::max(max_right, sz.x);
            break;
        case AnnotationSide::CenterOnDiagram:
            break;
        }
    }

    SideMargins out;
    out.left  = max_left + margin + gap;
    out.right = max_right + margin + gap;
    return out;
}

static bool load_diagram(wxWindow* host,
                         bool         sphere_mode,
                         int          canvas_w_px,
                         int          margin_left_px,
                         int          margin_right_px,
                         wxBitmap&    diagram,
                         int&         diagram_w,
                         int&         diagram_h)
{
    const int max_bw = std::max(host->FromDIP(140), canvas_w_px - margin_left_px - margin_right_px);
    unsigned  target_h =
        static_cast<unsigned>(std::max(100, static_cast<int>(std::lround(max_bw * k_svg_view_h / k_svg_view_w))));

    BitmapCache       cache;
    const char* const svg_key = sphere_mode ? k_svg_sphere : k_svg_none;

    for (int attempt = 0; attempt < 2; ++attempt) {
        if (wxBitmap* bp = cache.load_svg(svg_key, 0, target_h, false); bp != nullptr && bp->IsOk()) {
            diagram   = *bp;
            diagram_w = diagram.GetWidth();
            diagram_h = diagram.GetHeight();
            if (diagram_w <= max_bw || attempt == 1)
                return true;
            target_h = static_cast<unsigned>(std::max(100, static_cast<int>(std::lround(
                static_cast<double>(target_h) * max_bw / static_cast<double>(diagram_w)))));
            continue;
        }
        return false;
    }
    return false;
}

static int annotation_y(int oy, int bh, int design_y)
{
    const double y_scale = static_cast<double>(bh) / k_svg_view_h;
    return oy + static_cast<int>(std::lround((design_y - k_figma_diagram_top_y) * y_scale));
}

static int required_canvas_height(wxWindow* canvas,
                                  const std::vector<AnnotationSpec>& specs,
                                  int                                oy,
                                  int                                bh,
                                  int                                gap)
{
    int max_bottom = oy + bh;
    for (const AnnotationSpec& s : specs) {
        const wxSize sz = SupportAnnotationLabel::preferred_size(canvas, _L(s.msgid));
        const int      y  = annotation_y(oy, bh, s.design_y);
        max_bottom        = std::max(max_bottom, y + sz.GetHeight());
    }
    return max_bottom + gap;
}

static SupportAnnotationLabel* place_annotation(wxWindow* canvas,
                                                int       canvas_w,
                                                int       canvas_h,
                                                int       ox,
                                                int       oy,
                                                int       bw,
                                                int       bh,
                                                const AnnotationSpec& spec)
{
    const wxString text = _L(spec.msgid);
    const wxSize   sz   = SupportAnnotationLabel::preferred_size(canvas, text);

    auto* chip = new SupportAnnotationLabel(canvas, text);

    const int gap_canvas = canvas->FromDIP(8);
    const int margin     = canvas->FromDIP(10);

    int y = annotation_y(oy, bh, spec.design_y);
    y     = std::max(gap_canvas, std::min(y, canvas_h - sz.GetHeight() - gap_canvas));

    int x = 0;
    switch (spec.side) {
    case AnnotationSide::LeftOfDiagram:
        x = ox - margin - sz.GetWidth();
        x = std::max(gap_canvas, x);
        break;
    case AnnotationSide::RightOfDiagram:
        x = ox + bw + margin;
        break;
    case AnnotationSide::CenterOnDiagram: {
        const int anchor_x = ox + static_cast<int>(std::lround(static_cast<double>(bw) * k_figma_top_link_chip_center_x_frac));
        x = anchor_x - sz.GetWidth() / 2 + canvas->FromDIP(spec.h_nudge);
        x = std::max(gap_canvas, x);
        break;
    }
    }

    if (x + sz.GetWidth() > canvas_w - gap_canvas)
        x = canvas_w - gap_canvas - sz.GetWidth();
    x = std::max(gap_canvas, x);

    chip->SetPosition(wxPoint(x, y));
    return chip;
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

    auto* canvas = new wxPanel(this, wxID_ANY);
    canvas->SetBackgroundColour(wxColour(255, 255, 255));
    canvas->SetFont(Label::Body_11);

    const std::vector<AnnotationSpec> specs(
        m_sphere_mode ? std::vector<AnnotationSpec>(std::begin(k_sphere_annotations), std::end(k_sphere_annotations))
                      : std::vector<AnnotationSpec>(std::begin(k_none_annotations), std::end(k_none_annotations)));

    const SideMargins margins = measure_annotation_margins(canvas, specs);
    const int         gap     = canvas->FromDIP(8);

    int canvas_w = std::max(canvas->FromDIP(k_canvas_design_w),
                            margins.left + canvas->FromDIP(140) + margins.right);

    wxBitmap diagram;
    int      diagram_w = 0;
    int      diagram_h = 0;
    load_diagram(this, m_sphere_mode, canvas_w, margins.left, margins.right, diagram, diagram_w, diagram_h);

    canvas_w = std::max(canvas_w, margins.left + diagram_w + margins.right);

    int oy       = gap;
    int canvas_h = canvas->FromDIP(k_canvas_design_h);
    if (diagram.IsOk()) {
        oy       = std::max(gap, (canvas_h - diagram_h) / 2);
        canvas_h = std::max(canvas_h, required_canvas_height(canvas, specs, oy, diagram_h, gap));
        oy       = std::max(gap, (canvas_h - diagram_h) / 2);
    }

    canvas->SetMinSize(wxSize(canvas_w, canvas_h));
    canvas->SetSize(wxSize(canvas_w, canvas_h));

    const int ox = margins.left;

    wxWindow* diagram_host = nullptr;
    if (diagram.IsOk()) {
        diagram_host = new DiagramBitmapPanel(canvas, diagram);
        diagram_host->SetPosition(wxPoint(ox, oy));
    }

    std::vector<SupportAnnotationLabel*> chips;
    chips.reserve(specs.size());
    for (const AnnotationSpec& s : specs)
        chips.push_back(place_annotation(canvas, canvas_w, canvas_h, ox, oy, diagram_w, diagram_h, s));

    if (diagram_host != nullptr) {
        diagram_host->Lower();
        for (SupportAnnotationLabel* chip : chips)
            chip->Raise();
        // Center labels sit on the diagram art — keep them above every other chip.
        for (size_t i = 0; i < specs.size(); ++i) {
            if (specs[i].side == AnnotationSide::CenterOnDiagram)
                chips[i]->Raise();
        }
    }

    main->Add(canvas, 0, wxALIGN_CENTER | wxALL, FromDIP(10));

    SetSizer(main);
    main->Fit(this);
    SetMinClientSize(main->GetMinSize());
}

} // namespace GUI
} // namespace Slic3r
