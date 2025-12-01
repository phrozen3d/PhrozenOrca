#include "PhrozenCalibrationDlg.hpp"
#include "../I18N.hpp"

#include "../libslic3r/Utils.hpp"
#include "../libslic3r/Thread.hpp"
#include "../GUI.hpp"
#include "../GUI_App.hpp"
#include "../GUI_Preview.hpp"
#include "../MainFrame.hpp"
#include "../format.hpp"
#include "../Widgets/RoundedRectangle.hpp"
#include "../Widgets/StaticBox.hpp"

namespace Slic3r { namespace GUI {
wxDEFINE_EVENT(EVT_PHROZEN_CALIBRATION_SELECTED, wxCommandEvent);

// CalibrationProgressBar implementation
wxBEGIN_EVENT_TABLE(CalibrationProgressBar, wxWindow)
    EVT_PAINT(CalibrationProgressBar::OnPaint)
    EVT_ENTER_WINDOW(CalibrationProgressBar::OnMouseEnter)
    EVT_LEAVE_WINDOW(CalibrationProgressBar::OnMouseLeave)
    EVT_LEFT_DOWN(CalibrationProgressBar::OnMouseDown)
    EVT_LEFT_UP(CalibrationProgressBar::OnMouseUp)
wxEND_EVENT_TABLE()

CalibrationProgressBar::CalibrationProgressBar(wxWindow* parent, wxString label)
    : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
    , m_label(label)
{
    SetMinSize(wxSize(FromDIP(660), FromDIP(60)));
    SetMaxSize(wxSize(FromDIP(660), FromDIP(60)));
    SetBackgroundStyle(wxBG_STYLE_PAINT);
}

CalibrationProgressBar::~CalibrationProgressBar()
{
}

void CalibrationProgressBar::SetProgress(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    if (m_progress != percent) {
        m_progress = percent;
        Refresh();
    }
}

void CalibrationProgressBar::OnPaint(wxPaintEvent& event)
{
    wxAutoBufferedPaintDC dc(this);
    wxSize size = GetSize();
    double corner_radius = FromDIP(8);

    // Background color - use parent's background color
    wxColour bg_color = GetParent() ? GetParent()->GetBackgroundColour() : wxColour(0x5E, 0x64, 0x6B);

    dc.SetBackground(bg_color);
    dc.Clear();

    // Create graphics context for anti-aliased drawing
    wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
    if (!gc) return;

    // Define border parameters
    double border_width = FromDIP(2);
    double inset = border_width / 2;
    double inner_corner_radius = corner_radius - inset;

    // Draw progress fill with anti-aliasing (aligned with border)
    if (m_progress > 0) {
        wxColour fill_color;
        if (m_pressed) {
            fill_color = wxColour(0xC8, 0x4C, 0x10); // Darker orange when pressed
        } else if (m_hover) {
            fill_color = wxColour(0xF0, 0x70, 0x20); // Brighter orange on hover
        } else {
            fill_color = wxColour(0xF0, 0x5E, 0x20); // Normal orange (240, 94, 32)
        }

        // Calculate fill width accounting for border inset
        double max_fill_width = size.x - border_width;
        double fill_width = (max_fill_width * m_progress) / 100.0;

        gc->SetBrush(wxBrush(fill_color));
        gc->SetPen(*wxTRANSPARENT_PEN);

        // Draw rounded rectangle for progress fill
        wxGraphicsPath path = gc->CreatePath();
        if (m_progress >= 100) {
            // Full progress - round all corners, fit within border
            path.AddRoundedRectangle(inset, inset, fill_width, size.y - border_width, inner_corner_radius);
        } else {
            // Partial progress - round left corners only
            // Create path for left rounded part and right straight part
            double x = inset;
            double y = inset;
            double width = fill_width;
            double height = size.y - border_width;
            double radius = inner_corner_radius;

            path.MoveToPoint(x + radius, y);
            path.AddLineToPoint(x + width, y);
            path.AddLineToPoint(x + width, y + height);
            path.AddLineToPoint(x + radius, y + height);
            path.AddArc(x + radius, y + height - radius, radius, M_PI * 0.5, M_PI, true);
            path.AddLineToPoint(x, y + radius);
            path.AddArc(x + radius, y + radius, radius, M_PI, M_PI * 1.5, true);
            path.CloseSubpath();
        }
        gc->FillPath(path);
    }

    // Draw border with anti-aliasing
    wxColour border_color;
    if (m_pressed) {
        border_color = wxColour(0xD0, 0xD0, 0xD0); // Slightly darker white when pressed
    } else if (m_hover) {
        border_color = wxColour(0xFF, 0xFF, 0xFF); // Bright white on hover
    } else {
        border_color = wxColour(0xE0, 0xE0, 0xE0); // Normal white
    }

    gc->SetPen(wxPen(border_color, border_width));
    gc->SetBrush(*wxTRANSPARENT_BRUSH);

    wxGraphicsPath border_path = gc->CreatePath();
    border_path.AddRoundedRectangle(inset, inset,
                                   size.x - border_width, size.y - border_width,
                                   corner_radius);
    gc->StrokePath(border_path);

    // Draw text with Roboto font
    wxFont font(wxFontInfo(FromDIP(14)).FaceName("Roboto"));
    if (!font.IsOk()) {
        font = wxFont(wxFontInfo(FromDIP(14)).Family(wxFONTFAMILY_DEFAULT));
    }

    gc->SetFont(font, *wxWHITE);

    // Draw label
    double text_width, text_height;
    gc->GetTextExtent(m_label, &text_width, &text_height);
    double text_x = FromDIP(20);
    double text_y = (size.y - text_height) / 2.0;
    gc->DrawText(m_label, text_x, text_y);

    // Draw percentage on the right side if progress > 0
    if (m_progress > 0) {
        wxString percent_text = wxString::Format("%d%%", m_progress);
        double percent_width, percent_height;
        gc->GetTextExtent(percent_text, &percent_width, &percent_height);
        double percent_x = size.x - percent_width - FromDIP(20);
        double percent_y = (size.y - percent_height) / 2.0;
        gc->DrawText(percent_text, percent_x, percent_y);
    }

    delete gc;
}

void CalibrationProgressBar::OnMouseEnter(wxMouseEvent& event)
{
    m_hover = true;
    Refresh();
}

void CalibrationProgressBar::OnMouseLeave(wxMouseEvent& event)
{
    m_hover = false;
    m_pressed = false;
    Refresh();
}

void CalibrationProgressBar::OnMouseDown(wxMouseEvent& event)
{
    m_pressed = true;
    Refresh();
}

void CalibrationProgressBar::OnMouseUp(wxMouseEvent& event)
{
    m_pressed = false;
    Refresh();

    wxCommandEvent sendEvent(EVT_PHROZEN_CALIBRATION_SELECTED);
    sendEvent.SetEventObject(this);
    wxPostEvent(this, sendEvent);
}

// PhrozenCalibrationDlg implementation
PhrozenCalibrationDlg::PhrozenCalibrationDlg(Plater *plater)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe),
                wxID_ANY,
                _L("Calibration"),
                wxDefaultPosition,
                wxDefaultSize,
                wxCAPTION | wxCLOSE_BOX)
{
    SetBackgroundColour(wxColour(0x5E, 0x64, 0x6B)); // Dark gray background

    wxBoxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);

    // Top separator line
    auto line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    line_top->SetBackgroundColour(wxColour(0x80, 0x80, 0x80));
    main_sizer->Add(line_top, 0, wxEXPAND, 0);

    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(30));

    // Description text
    m_description_text = new wxStaticText(this, wxID_ANY,
        _L("Automatically check your 3D printer conditions and detect\nany errors to maintain its optimal performance."),
        wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);

    wxFont font = GetParent()->GetFont();
    font.SetPointSize(14); 
    m_description_text->SetFont(font);
    m_description_text->SetForegroundColour(wxColour(0xE0, 0xE0, 0xE0));
    main_sizer->Add(m_description_text, 0, wxLEFT | wxRIGHT, FromDIP(30));

    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(30));

    // Auto-Leveling progress bar
    m_auto_leveling = new CalibrationProgressBar(this, _L("Auto-Leveling"));
    main_sizer->Add(m_auto_leveling, 0, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(30));

    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(20));

    // Resonance Compensation progress bar
    m_resonance_compensation = new CalibrationProgressBar(this, _L("Resonance Compensation"));
    main_sizer->Add(m_resonance_compensation, 0, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(30));

    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(20));

    // Temperature Calibration progress bar
    m_temperature_calibration = new CalibrationProgressBar(this, _L("Temperature Calibration"));
    main_sizer->Add(m_temperature_calibration, 0, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(30));

    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(30));

    SetSizer(main_sizer);
    Layout();
    Fit();

    // Set minimum size
    SetMinSize(wxSize(FromDIP(720), FromDIP(400)));

    // bind event
    m_auto_leveling->Bind( EVT_PHROZEN_CALIBRATION_SELECTED, &PhrozenCalibrationDlg::OnCalibrationSelected, this);
    m_resonance_compensation->Bind( EVT_PHROZEN_CALIBRATION_SELECTED, &PhrozenCalibrationDlg::OnCalibrationSelected, this);
    m_temperature_calibration->Bind( EVT_PHROZEN_CALIBRATION_SELECTED, &PhrozenCalibrationDlg::OnCalibrationSelected, this);
}

PhrozenCalibrationDlg::~PhrozenCalibrationDlg()
{
}

void PhrozenCalibrationDlg::on_dpi_changed(const wxRect &suggested_rect)
{
    if (m_auto_leveling) {
        m_auto_leveling->SetMinSize(wxSize(FromDIP(660), FromDIP(60)));
        m_auto_leveling->SetMaxSize(wxSize(FromDIP(660), FromDIP(60)));
    }
    if (m_resonance_compensation) {
        m_resonance_compensation->SetMinSize(wxSize(FromDIP(660), FromDIP(60)));
        m_resonance_compensation->SetMaxSize(wxSize(FromDIP(660), FromDIP(60)));
    }
    if (m_temperature_calibration) {
        m_temperature_calibration->SetMinSize(wxSize(FromDIP(660), FromDIP(60)));
        m_temperature_calibration->SetMaxSize(wxSize(FromDIP(660), FromDIP(60)));
    }

    Layout();
    Fit();
}

void PhrozenCalibrationDlg::SetAutoLevelingProgress(int percent)
{
    if (m_auto_leveling) {
        m_auto_leveling->SetProgress(percent);
    }
}

void PhrozenCalibrationDlg::SetResonanceCompensationProgress(int percent)
{
    if (m_resonance_compensation) {
        m_resonance_compensation->SetProgress(percent);
    }
}

void PhrozenCalibrationDlg::SetTemperatureCalibrationProgress(int percent)
{
    if (m_temperature_calibration) {
        m_temperature_calibration->SetProgress(percent);
    }
}

bool PhrozenCalibrationDlg::Show(bool show)
{
    if (show) {
        wxGetApp().UpdateDlgDarkUI(this);
        CentreOnParent();
    }
    return DPIDialog::Show(show);
}

void PhrozenCalibrationDlg::OnCalibrationSelected( wxCommandEvent& event )
{
    auto pObj = event.GetEventObject();
    if ( !pObj ) return;
    if ( pObj == m_auto_leveling )
    {
        m_auto_leveling->SetProgress(80);
    }
    else if ( pObj == m_temperature_calibration )
    {
        m_temperature_calibration->SetProgress(40);
    }
    else if ( pObj == m_temperature_calibration )
    {
        m_temperature_calibration->SetProgress(20);
    }
}




}} // namespace Slic3r::GUI
