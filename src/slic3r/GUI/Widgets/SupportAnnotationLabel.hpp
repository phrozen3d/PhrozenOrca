#ifndef slic3r_GUI_SupportAnnotationLabel_hpp_
#define slic3r_GUI_SupportAnnotationLabel_hpp_

#include "StaticBox.hpp"

#include <wx/string.h>

class wxStaticText;

namespace Slic3r {
namespace GUI {

/// Figma-style callout chip: white fill, #dbdbdb border, 4px radius, padding; width follows label text.
class SupportAnnotationLabel : public StaticBox
{
public:
    SupportAnnotationLabel(wxWindow* parent, const wxString& text);

    void set_annotation_text(const wxString& text);
    wxString annotation_text() const;

private:
    wxStaticText* m_label{ nullptr };
};

} // namespace GUI
} // namespace Slic3r

#endif
