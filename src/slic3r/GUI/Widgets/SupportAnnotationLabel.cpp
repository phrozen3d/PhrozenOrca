#include "SupportAnnotationLabel.hpp"

#include "Label.hpp"

#include <algorithm>
#include <wx/dcclient.h>
#include <wx/event.h>

namespace Slic3r {
namespace GUI {

namespace {

// Body_11 = Regular HarmonyOS at ~11pt design size (not Head/bold). Text is drawn on wxPaintDC,
// not wxGCDC — GDI+ text inside StaticBox::render() looks artificially heavy on MSW.
static wxFont annotation_font()
{
    wxFont font = Label::Body_11;
    font.SetWeight(wxFONTWEIGHT_NORMAL);
    font.SetStyle(wxFONTSTYLE_NORMAL);
    return font;
}

static wxSize text_extent(wxWindow* dpi_ref, const wxString& text)
{
    dpi_ref->SetFont(annotation_font());
    return dpi_ref->GetTextExtent(text);
}

} // namespace

BEGIN_EVENT_TABLE(SupportAnnotationLabel, StaticBox)
    EVT_PAINT(SupportAnnotationLabel::on_paint)
END_EVENT_TABLE()

wxSize SupportAnnotationLabel::preferred_size(wxWindow* dpi_ref, const wxString& text)
{
    const wxSize te     = text_extent(dpi_ref, text);
    const int    pad    = dpi_ref->FromDIP(4);
    const int    border = dpi_ref->FromDIP(1);
    const int    fudge  = dpi_ref->FromDIP(6);
    return wxSize(te.x + 2 * pad + 2 * border + fudge,
                  te.y + 2 * pad + 2 * border + fudge);
}

SupportAnnotationLabel::SupportAnnotationLabel(wxWindow* parent, const wxString& text)
    : StaticBox(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0)
    , m_text(text)
{
    SetCornerRadius(static_cast<double>(parent->FromDIP(4)));
    SetBorderWidth(1);
    SetBorderColorNormal(wxColour(0xDB, 0xDB, 0xDB));
    SetBackgroundColorNormal(wxColour(255, 255, 255));

    const wxSize box = preferred_size(parent, text);
    SetSize(box);
    SetMinSize(box);
    SetMaxSize(box);
}

void SupportAnnotationLabel::set_annotation_text(const wxString& text)
{
    m_text = text;
    if (wxWindow* parent = GetParent()) {
        const wxSize box = preferred_size(parent, text);
        SetSize(box);
        SetMinSize(box);
        SetMaxSize(box);
    }
    Refresh();
}

void SupportAnnotationLabel::doRender(wxDC& dc)
{
    StaticBox::doRender(dc);
}

void SupportAnnotationLabel::on_paint(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    render(dc);

    dc.SetFont(annotation_font());
    dc.SetTextForeground(wxColour(0x36, 0x36, 0x36));

    const int    pad = FromDIP(4);
    const wxSize te  = dc.GetTextExtent(m_text);
    const wxSize cs  = GetClientSize();
    const int    x   = pad;
    const int    y   = std::max(0, (cs.y - te.y) / 2);
    dc.DrawText(m_text, x, y);
}

} // namespace GUI
} // namespace Slic3r
