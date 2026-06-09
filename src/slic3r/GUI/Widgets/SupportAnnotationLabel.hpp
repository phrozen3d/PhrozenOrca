#ifndef slic3r_GUI_SupportAnnotationLabel_hpp_
#define slic3r_GUI_SupportAnnotationLabel_hpp_

#include "StaticBox.hpp"

#include <wx/string.h>

class wxPaintEvent;

namespace Slic3r {
namespace GUI {

/// Figma-style callout chip: white fill, #dbdbdb border, 4px radius; text painted inside the box.
class SupportAnnotationLabel : public StaticBox
{
public:
    SupportAnnotationLabel(wxWindow* parent, const wxString& text);

    static wxSize preferred_size(wxWindow* dpi_ref, const wxString& text);

    void set_annotation_text(const wxString& text);
    wxString annotation_text() const { return m_text; }

protected:
    void doRender(wxDC& dc) override;
    void on_paint(wxPaintEvent& evt);

private:
    wxString m_text;

    DECLARE_EVENT_TABLE()
};

} // namespace GUI
} // namespace Slic3r

#endif
