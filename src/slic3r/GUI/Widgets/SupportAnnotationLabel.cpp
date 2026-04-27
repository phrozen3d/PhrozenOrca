#include "SupportAnnotationLabel.hpp"

#include "Label.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>

namespace Slic3r {
namespace GUI {

SupportAnnotationLabel::SupportAnnotationLabel(wxWindow* parent, const wxString& text)
    : StaticBox(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0)
{
    const int pad = parent->FromDIP(4);
    SetCornerRadius(static_cast<double>(parent->FromDIP(4)));
    SetBorderWidth(1);
    SetBorderColorNormal(wxColour(0xDB, 0xDB, 0xDB));
    SetBackgroundColorNormal(wxColour(255, 255, 255));

    m_label = new wxStaticText(this, wxID_ANY, text);
    m_label->SetFont(Label::Body_12);
    m_label->SetForegroundColour(wxColour(0x36, 0x36, 0x36));
    m_label->SetBackgroundColour(wxColour(255, 255, 255));

    auto* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(m_label, 0, wxALIGN_CENTER_VERTICAL | wxALL, pad);
    SetSizer(row);
    row->Fit(this);
}

void SupportAnnotationLabel::set_annotation_text(const wxString& text)
{
    if (m_label)
        m_label->SetLabel(text);
    if (GetSizer()) {
        GetSizer()->Fit(this);
        Layout();
    }
}

wxString SupportAnnotationLabel::annotation_text() const
{
    return m_label ? m_label->GetLabel() : wxString();
}

} // namespace GUI
} // namespace Slic3r
