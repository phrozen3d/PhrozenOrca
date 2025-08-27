#include "AxisCtrlButton.hpp"
#include "Label.hpp"
#include "libslic3r/libslic3r.h"

#include <wx/dcclient.h>
#include <wx/dcgraph.h>

StateColor blank_bg(StateColor(std::make_pair(wxColour("#FFFFFF"), (int)StateColor::Normal)));
static const wxColour BUTTON_BG_COL = wxColour("#EEEEEE");
static const wxColour BUTTON_IN_BG_COL = wxColour("#CECECE");

static const wxColour bd = wxColour(255, 124, 63);
static const wxColour text_num_color = wxColour(0x898989);
static const wxColour BUTTON_PRESS_COL = wxColour(172, 172, 172);
static const double sqrt2 = std::sqrt(2);

BEGIN_EVENT_TABLE(AxisCtrlButton, wxWindow)
EVT_LEFT_DOWN(AxisCtrlButton::mouseDown)
EVT_LEFT_UP(AxisCtrlButton::mouseReleased)
EVT_MOTION(AxisCtrlButton::mouseMoving)
EVT_PAINT(AxisCtrlButton::paintEvent)   
END_EVENT_TABLE()

#define OUTER_SIZE      FromDIP(105)
#define INNER_SIZE      FromDIP(58)
#define HOME_SIZE       FromDIP(23)
#define BLANK_SIZE      FromDIP(24)
#define GAP_SIZE        FromDIP(4)

#pragma region AxisCtrlButton
AxisCtrlButton::AxisCtrlButton(wxWindow *parent, ScalableBitmap &icon, long stlye)
    : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, stlye)
    , r_outer(OUTER_SIZE)
    , r_inner(INNER_SIZE)
    , r_home(HOME_SIZE)
    , r_blank(BLANK_SIZE)
    , gap(GAP_SIZE)
    , last_pos(UNDEFINED)
    , current_pos(UNDEFINED) // don't change init value
    , text_color(std::make_pair(0x6B6B6B, (int) StateColor::Disabled), std::make_pair(*wxBLACK, (int) StateColor::Normal))
	, state_handler(this)
{
    m_icon = icon;
	wxWindow::SetBackgroundColour(parent->GetBackgroundColour());

    border_color.append(bd, StateColor::Hovered);

    background_color.append(BUTTON_BG_COL, StateColor::Disabled);
    background_color.append(BUTTON_PRESS_COL, StateColor::Pressed);
    background_color.append(BUTTON_BG_COL, StateColor::Hovered);
    background_color.append(BUTTON_BG_COL, StateColor::Normal);
    background_color.append(BUTTON_BG_COL, StateColor::Enabled);

    inner_background_color.append(BUTTON_IN_BG_COL, StateColor::Disabled);
    inner_background_color.append(BUTTON_PRESS_COL, StateColor::Pressed);
    inner_background_color.append(BUTTON_IN_BG_COL, StateColor::Hovered);
    inner_background_color.append(BUTTON_IN_BG_COL, StateColor::Normal);
    inner_background_color.append(BUTTON_IN_BG_COL, StateColor::Enabled);

    state_handler.attach({ &border_color, &background_color });
    state_handler.update_binds();
}

void AxisCtrlButton::updateParams() {
    r_outer = OUTER_SIZE;
    r_inner = INNER_SIZE;
    r_home = HOME_SIZE;
    r_blank = BLANK_SIZE;
    gap = GAP_SIZE;
}

void AxisCtrlButton::SetMinSize(const wxSize& size)
{
	wxSize cur_size = GetSize();
    if (size.GetWidth() > 0 && size.GetHeight() > 0) {
        stretch = std::min((double)size.GetWidth() / cur_size.x,(double)size.GetHeight() / cur_size.y);
		minSize = size;
        updateParams();
    }
    else if (size.GetWidth() > 0) {
		stretch = (double)size.GetWidth() / cur_size.x;
		minSize.x = size.x;
        updateParams();
    }
    else if (size.GetHeight() > 0) {
		stretch = (double)size.GetHeight() / cur_size.y;
		minSize.y = size.y;
        updateParams();
    }
    else {
		stretch = 1.0;
        minSize = wxSize(228, 228);
    }
    wxWindow::SetMinSize(minSize);
    center = wxPoint(minSize.x / 2, minSize.y / 2);
}

void AxisCtrlButton::SetTextColor(StateColor const &color)
{
    text_color = color;
    state_handler.update_binds();
    Refresh();
}

void AxisCtrlButton::SetBorderColor(StateColor const& color)
{
    border_color = color;
    state_handler.update_binds();
    Refresh();
}

void AxisCtrlButton::SetBackgroundColor(StateColor const& color)
{
    background_color = color;
    state_handler.update_binds();
    Refresh();
}

void AxisCtrlButton::SetInnerBackgroundColor(StateColor const& color)
{
    inner_background_color = color;
    state_handler.update_binds();
    Refresh();
}

void AxisCtrlButton::SetBitmap(ScalableBitmap &bmp)
{
    m_icon = bmp;
}

void AxisCtrlButton::Rescale() {
	Refresh();
}

void AxisCtrlButton::paintEvent(wxPaintEvent& evt)
{
    // depending on your system you may need to look at double-buffered dcs
    wxPaintDC dc(this);
    wxGCDC gcdc(dc);
    render(gcdc);
}

/*
 * Here we do the actual rendering. I put it in a separate
 * method so that it can work no matter what type of DC
 * (e.g. wxPaintDC or wxClientDC) is used.
 */
void AxisCtrlButton::render(wxDC& dc)
{
    wxGraphicsContext* gc = dc.GetGraphicsContext();

    int states = state_handler.states();
	wxSize size = GetSize();

    gc->PushState();
    gc->Translate(center.x, center.y);
	
	//draw the outer ring
    wxGraphicsPath outer_path = gc->CreatePath();
    outer_path.AddCircle(0, 0, r_outer);
    outer_path.AddCircle(0, 0, r_inner);
    gc->SetPen(StateColor::darkModeColorFor(BUTTON_BG_COL));
    gc->SetBrush(StateColor::darkModeColorFor(BUTTON_BG_COL));
    gc->DrawPath(outer_path);

	//draw the inner ring
    wxGraphicsPath inner_path = gc->CreatePath();
    inner_path.AddCircle(0, 0, r_inner);
    inner_path.AddCircle(0, 0, r_blank);
    gc->SetPen(StateColor::darkModeColorFor(BUTTON_IN_BG_COL));
    gc->SetBrush(StateColor::darkModeColorFor(BUTTON_IN_BG_COL));
	gc->DrawPath(inner_path);

	//draw an arc in corresponding position
	if (current_pos != CurrentPos::UNDEFINED) {
		wxGraphicsPath path = gc->CreatePath();
		if (current_pos < 4) {
			path.AddArc(0, 0, r_outer, (5 - 2 * current_pos) * PI / 4, (7 - 2 * current_pos) * PI / 4, true);
			path.AddArc(0, 0, r_inner, (7 - 2 * current_pos) * PI / 4, (5 - 2 * current_pos) * PI / 4, false);
			path.CloseSubpath();
			gc->SetBrush(wxBrush(background_color.colorForStates(states)));
		}
		else if (current_pos < 8) {
			path.AddArc(0, 0, r_inner, (5 - 2 * current_pos) * PI / 4, (7 - 2 * current_pos) * PI / 4, true);
			path.AddArc(0, 0, r_blank, (7 - 2 * current_pos) * PI / 4, (5 - 2 * current_pos) * PI / 4, false);
			path.CloseSubpath();
			gc->SetBrush(wxBrush(inner_background_color.colorForStates(states)));
        }
		gc->SetPen(wxPen(border_color.colorForStates(states),2));
		gc->DrawPath(path);
	}

	//draw rectangle gap
	gc->SetPen(blank_bg.colorForStates(StateColor::Normal));
	gc->SetBrush(blank_bg.colorForStates(StateColor::Normal));
	gc->PushState();
	gc->Rotate(-PI / 4);
	gc->DrawRectangle(-sqrt2 * size.x / 2, -sqrt2 * gap / 2, sqrt2 * size.x, sqrt2 * gap);
	gc->Rotate(-PI / 2);
	gc->DrawRectangle(-sqrt2 * size.x / 2, -sqrt2 * gap / 2, sqrt2 * size.x, sqrt2 * gap);
	gc->PopState();

	// draw the home circle
    wxGraphicsPath home_path = gc->CreatePath();
    home_path.AddCircle(0, 0, r_home);
    home_path.CloseSubpath();
    gc->PushState();
    if (current_pos == 8) {
        gc->SetPen(wxPen(border_color.colorForStates(states), 2));
        gc->SetBrush(wxBrush(background_color.colorForStates(states)));
    } else {
        gc->SetPen(StateColor::darkModeColorFor(BUTTON_BG_COL));
        gc->SetBrush(StateColor::darkModeColorFor(BUTTON_BG_COL));
    }
    gc->DrawPath(home_path);

    if (m_icon.bmp().IsOk()) {
        gc->DrawBitmap(m_icon.bmp(), -1 * m_icon.GetBmpWidth() / 2, -1 * m_icon.GetBmpHeight() / 2, m_icon.GetBmpWidth(), m_icon.GetBmpHeight());
    }
    gc->PopState();

	//draw linear border of the arc
	if (current_pos != CurrentPos::UNDEFINED) {
        gc->PushState();
        gc->SetPen(wxPen(border_color.colorForStates(states), 2));

        if (current_pos == 8) {
            wxGraphicsPath line_path = gc->CreatePath();
            line_path.AddCircle(0, 0, r_home);
            gc->StrokePath(line_path);
        } else {
            wxGraphicsPath line_path1 = gc->CreatePath();
            wxGraphicsPath line_path2 = gc->CreatePath();
            if (current_pos < 4) {
                line_path1.MoveToPoint(r_inner, -sqrt2 * gap / 2);
                line_path1.AddLineToPoint(r_outer, -sqrt2 * gap / 2);
                line_path2.MoveToPoint(-r_inner, -sqrt2 * gap / 2);
                line_path2.AddLineToPoint(-r_outer, -sqrt2 * gap / 2);
            } else if (current_pos < 8) {
                line_path1.MoveToPoint(r_blank, -sqrt2 * gap / 2);
                line_path1.AddLineToPoint(r_inner, -sqrt2 * gap / 2);
                line_path2.MoveToPoint(-r_blank, -sqrt2 * gap / 2);
                line_path2.AddLineToPoint(-r_inner, -sqrt2 * gap / 2);
            }
            gc->Rotate(-(1 + 2 * current_pos) * PI / 4);
            gc->StrokePath(line_path1);
            gc->Rotate(PI / 2);
            gc->StrokePath(line_path2);
        }
        gc->PopState();
	}

	//draw text
    if (!IsEnabled())
        gc->SetFont(Label::Body_12, text_color.colorForStates(StateColor::Disabled));
    else
	    gc->SetFont(Label::Head_12, text_color.colorForStates(states));
	wxDouble w, h;
	gc->GetTextExtent("Y", &w, &h);
	gc->DrawText(wxT("Y"), -w / 2, -r_outer + (r_outer - r_inner) / 2 - h / 2);
	gc->GetTextExtent("-X", &w, &h);
	gc->DrawText(wxT("-X"), -r_outer + (r_outer - r_inner) / 2 - w / 2, - h / 2);
	gc->GetTextExtent("-Y", &w, &h);
	gc->DrawText(wxT("-Y"), -w / 2, r_outer - (r_outer - r_inner) / 2 - h / 2);
	gc->GetTextExtent("X", &w, &h);
	gc->DrawText(wxT("X"), r_outer - (r_outer - r_inner) / 2 - w / 2, -h / 2);

	gc->SetFont(Label::Body_12, text_num_color);

	gc->PushState();
	gc->Rotate(PI / 4);
	gc->GetTextExtent("+10", &w, &h);
	gc->DrawText(wxT("+10"), sqrt2 * gap, -r_outer + (r_outer - r_inner) / 2 - h / 2);
	gc->GetTextExtent("+1", &w, &h);
	gc->DrawText(wxT("+1"), sqrt2 * gap, -r_inner + (r_inner - r_blank) / 2 - h / 2);
	gc->GetTextExtent("-1", &w, &h);
	gc->DrawText(wxT("-1"), sqrt2 * gap, r_inner - (r_inner - r_blank) / 2 - h / 2);
	gc->GetTextExtent("-10", &w, &h);
	gc->DrawText(wxT("-10"), sqrt2 * gap, r_outer - (r_outer - r_inner) / 2 - h / 2);
	gc->PopState();


	gc->PopState();
}

void AxisCtrlButton::mouseDown(wxMouseEvent& event)
{
    event.Skip();
    pressedDown = true;
    SetFocus();
    CaptureMouse();
}

void AxisCtrlButton::mouseReleased(wxMouseEvent& event)
{
    event.Skip();
    if (pressedDown) {
        pressedDown = false;
        ReleaseMouse();
        if (wxRect({ 0, 0 }, GetSize()).Contains(event.GetPosition()))
            sendButtonEvent();
    }
}

void AxisCtrlButton::mouseMoving(wxMouseEvent& event)
{
    if (pressedDown)
        return;
	wxPoint mouse_pos(event.GetX(), event.GetY());
	wxPoint transformed_mouse_pos = mouse_pos - center;
	double r_temp = transformed_mouse_pos.x * transformed_mouse_pos.x + transformed_mouse_pos.y * transformed_mouse_pos.y;
	if (r_temp > r_outer * r_outer) {
		current_pos = CurrentPos::UNDEFINED;
	}
	else if (r_temp > r_inner * r_inner) {
		if (transformed_mouse_pos.y < transformed_mouse_pos.x - gap && transformed_mouse_pos.y < -transformed_mouse_pos.x - gap)
		{
			current_pos = CurrentPos::OUTER_UP;
		}
		else if (transformed_mouse_pos.y > transformed_mouse_pos.x + gap && transformed_mouse_pos.y < -transformed_mouse_pos.x - gap)
		{
			current_pos = CurrentPos::OUTER_LEFT;
		}
		else if (transformed_mouse_pos.y > transformed_mouse_pos.x + gap && transformed_mouse_pos.y > -transformed_mouse_pos.x + gap)
		{
			current_pos = CurrentPos::OUTER_DOWN;
		}
		else if (transformed_mouse_pos.y < transformed_mouse_pos.x - gap && transformed_mouse_pos.y > -transformed_mouse_pos.x + gap)
		{
			current_pos = CurrentPos::OUTER_RIGHT;
		}
        else {
            current_pos = CurrentPos::UNDEFINED;
        }
	}
	else if (r_temp > r_blank * r_blank) {
		if (transformed_mouse_pos.y < transformed_mouse_pos.x - gap && transformed_mouse_pos.y < -transformed_mouse_pos.x - gap)
		{
			current_pos = CurrentPos::INNER_UP;
		}
		else if (transformed_mouse_pos.y > transformed_mouse_pos.x + gap && transformed_mouse_pos.y < -transformed_mouse_pos.x - gap)
		{
			current_pos = CurrentPos::INNER_LEFT;
		}
		else if (transformed_mouse_pos.y > transformed_mouse_pos.x + gap && transformed_mouse_pos.y > -transformed_mouse_pos.x + gap)
		{
			current_pos = CurrentPos::INNER_DOWN;
		}
		else if (transformed_mouse_pos.y < transformed_mouse_pos.x - gap && transformed_mouse_pos.y > -transformed_mouse_pos.x + gap)
		{
			current_pos = CurrentPos::INNER_RIGHT;
		}
        else {
            current_pos = CurrentPos::UNDEFINED;
        }
    } else if (r_temp <= r_home * r_home) {
        current_pos = INNER_HOME;
    }
	if (last_pos != current_pos) {
		last_pos = current_pos;
		Refresh();
	}
}

void AxisCtrlButton::sendButtonEvent()
{
    wxCommandEvent event(wxEVT_COMMAND_BUTTON_CLICKED, GetId());
    event.SetEventObject(this);
    event.SetInt(current_pos);
    GetEventHandler()->ProcessEvent(event);
}
#pragma endregion


#pragma region PhrozenAxisCtrlButton
StateColor phrozen_blank_bg(StateColor(std::make_pair(wxColour("#FFFFFF"), (int)StateColor::Normal)));
static const wxColour PHROZEN_BUTTON_BG_COL = wxColour("#FF7C3F");
static const wxColour PHROZEN_BUTTON_IN_BG_COL = wxColour("#F05E20");

static const wxColour phrozen_bd = wxColour(255, 124, 63);
static const wxColour phrozen_text_num_color = wxColour(0x898989);
static const wxColour PHROZEN_BUTTON_PRESS_COL = wxColour(172, 172, 172);

BEGIN_EVENT_TABLE(PhrozenAxisCtrlButton, wxWindow)
EVT_LEFT_DOWN(PhrozenAxisCtrlButton::mouseDown)
EVT_LEFT_UP(PhrozenAxisCtrlButton::mouseReleased)
EVT_MOTION(PhrozenAxisCtrlButton::mouseMoving)
EVT_PAINT(PhrozenAxisCtrlButton::paintEvent)   
END_EVENT_TABLE()

#define AXIS_BUTTON_SIZE_X            FromDIP(32)
#define AXIS_BUTTON_SIZE_Y            FromDIP(32)
#define MOVE_STEP_BUTTON_SIZE_X       FromDIP(32 * 2)
#define MOVE_STEP_BUTTON_SIZE_Y       FromDIP(32)
#define AXIS_BUTTON_GAP_SIZE          FromDIP(10)

//#define INNER_SIZE      FromDIP(58)
//#define HOME_SIZE       FromDIP(23)
//#define BLANK_SIZE      FromDIP(24)
//#define GAP_SIZE        FromDIP(4)

#pragma region PhrozenAxisCtrlButton
PhrozenAxisCtrlButton::PhrozenAxisCtrlButton(wxWindow *parent, long style) 
    : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, style)
    , gap( AXIS_BUTTON_GAP_SIZE )
    , m_kAxisButtonSize( AXIS_BUTTON_SIZE_X, AXIS_BUTTON_SIZE_Y )
    , m_kMoveStepButtonSize( MOVE_STEP_BUTTON_SIZE_X, MOVE_STEP_BUTTON_SIZE_Y )
    , last_pos(UNDEFINED)
    , current_pos(UNDEFINED) // don't change init value
{
    wxWindow::SetBackgroundColour(parent->GetBackgroundColour());
}

void PhrozenAxisCtrlButton::updateParams() 
{
    m_kAxisButtonSize     = wxSize(AXIS_BUTTON_SIZE_X, AXIS_BUTTON_SIZE_Y);
    m_kMoveStepButtonSize = wxSize(MOVE_STEP_BUTTON_SIZE_X, MOVE_STEP_BUTTON_SIZE_Y);
    gap = AXIS_BUTTON_GAP_SIZE;
}

void PhrozenAxisCtrlButton::SetMinSize(const wxSize& size) 
{
    wxSize cur_size = GetSize();
    if (size.GetWidth() > 0 && size.GetHeight() > 0) {
        stretch = std::min((double)size.GetWidth() / cur_size.x,(double)size.GetHeight() / cur_size.y);
		minSize = size;
        updateParams();
    }
    else if (size.GetWidth() > 0) {
		stretch = (double)size.GetWidth() / cur_size.x;
		minSize.x = size.x;
        updateParams();
    }
    else if (size.GetHeight() > 0) {
		stretch = (double)size.GetHeight() / cur_size.y;
		minSize.y = size.y;
        updateParams();
    }
    else {
		stretch = 1.0;
        minSize = wxSize(228, 228);
    }
    wxWindow::SetMinSize(minSize);
    center = wxPoint(minSize.x / 2, minSize.y / 2);
    initializeDrawButtonInfo();
}

void PhrozenAxisCtrlButton::Rescale() 
{
	Refresh();
}

void PhrozenAxisCtrlButton::paintEvent(wxPaintEvent& evt)
{
    // depending on your system you may need to look at double-buffered dcs
    wxPaintDC dc(this);
    wxGCDC gcdc(dc);
    render(gcdc);
}

void PhrozenAxisCtrlButton::render(wxDC& dc)
{
    renderPhrozenStyle2( dc );
    //renderPhrozenStyle3( dc );
}

void PhrozenAxisCtrlButton::initializeDrawButtonInfo()
{
    wxSize size = GetSize();
    double fLCenterX = -80;
    double fLCenterY = -( gap + m_kMoveStepButtonSize.GetY() );

    CButtonInfo kMove01( CurrentPos::MOVE_STEP_01MM, wxSize( fLCenterX, fLCenterY ), m_kMoveStepButtonSize );
    kMove01.SetImage( "Phrozen_AxisControl_01mm", "Phrozen_AxisControl_01mm_Hover", "Phrozen_AxisControl_01mm_Pressed" );
    m_kDrawButtonInfo.push_back( kMove01 );

    fLCenterY += ( gap + m_kMoveStepButtonSize.GetY() );
    CButtonInfo kMove1( CurrentPos::MOVE_STEP_1MM, wxSize( fLCenterX, fLCenterY ), m_kMoveStepButtonSize );
    kMove1.SetImage( "Phrozen_AxisControl_1mm", "Phrozen_AxisControl_1mm_Hover", "Phrozen_AxisControl_1mm_Pressed" );
    m_kDrawButtonInfo.push_back( kMove1 );

    fLCenterY += ( gap + m_kMoveStepButtonSize.GetY() );
    CButtonInfo kMove10( CurrentPos::MOVE_STEP_10MM, wxSize( fLCenterX, fLCenterY ), m_kMoveStepButtonSize );
    kMove10.SetImage( "Phrozen_AxisControl_10mm", "Phrozen_AxisControl_10mm_Hover", "Phrozen_AxisControl_10mm_Pressed" );
    m_kDrawButtonInfo.push_back( kMove10 );

    double fRCenterX = 60;
    double fRCenterY = -( gap + m_kMoveStepButtonSize.GetY() );

    CButtonInfo kUp( CurrentPos::AXIS_UP, wxSize( fRCenterX, fRCenterY ), m_kAxisButtonSize );
    kUp.SetImage( "Phrozen_AxisControl_Up", "Phrozen_AxisControl_Up_Hover", "Phrozen_AxisControl_Up_Pressed" );
    m_kDrawButtonInfo.push_back( kUp );

    fRCenterY += ( gap + m_kAxisButtonSize.GetY() );
    CButtonInfo kHome( CurrentPos::AXIS_HOME, wxSize( fRCenterX, fRCenterY ), m_kAxisButtonSize );
    kHome.SetImage( "Phrozen_AxisControl_Home", "Phrozen_AxisControl_Home_Hover", "Phrozen_AxisControl_Home_Pressed" );
    m_kDrawButtonInfo.push_back( kHome );

    fRCenterY += ( gap + m_kAxisButtonSize.GetY() );
    CButtonInfo kDown( CurrentPos::AXIS_DOWN, wxSize( fRCenterX, fRCenterY ), m_kAxisButtonSize );
    kDown.SetImage( "Phrozen_AxisControl_Down", "Phrozen_AxisControl_Down_Hover", "Phrozen_AxisControl_Down_Pressed" );
    m_kDrawButtonInfo.push_back( kDown );

    fRCenterY -= ( gap + m_kAxisButtonSize.GetY() );
    fRCenterX += ( gap + m_kAxisButtonSize.GetX() );
    CButtonInfo kRight( CurrentPos::AXIS_RIGHT, wxSize( fRCenterX, fRCenterY ), m_kAxisButtonSize );
    kRight.SetImage( "Phrozen_AxisControl_Right", "Phrozen_AxisControl_Right_Hover", "Phrozen_AxisControl_Right_Pressed" );
    m_kDrawButtonInfo.push_back( kRight );

    fRCenterX -= 2 * ( gap + m_kAxisButtonSize.GetX() );
    CButtonInfo kLeft( CurrentPos::AXIS_LEFT, wxSize( fRCenterX, fRCenterY ), m_kAxisButtonSize );
    kLeft.SetImage( "Phrozen_AxisControl_Left", "Phrozen_AxisControl_Left_Hover", "Phrozen_AxisControl_Left_Pressed" );
    m_kDrawButtonInfo.push_back( kLeft );
}

void PhrozenAxisCtrlButton::renderPhrozenStyle2(wxDC& dc)
{
    wxGraphicsContext* gc = dc.GetGraphicsContext();
    wxSize size = GetSize();
    
    gc->PushState();
    gc->Translate(center.x, center.y);

    // Helper function to load and draw SVG image
    auto drawImageButton = [&]( const CButtonInfo& kInfo ) 
     {

        double x = kInfo.m_kCenter.GetX();
        double y = kInfo.m_kCenter.GetY();
        double w = kInfo.m_kSize.GetX(); 
        double h = kInfo.m_kSize.GetY();
        // Create ScalableBitmap from SVG
        //ScalableBitmap btn_bitmap = create_scaled_bitmap(imageName);
        ScalableBitmap btn_bitmap;
        bool bIsHover = current_pos == kInfo.m_kType;
        if ( pressedDown && bIsHover )
        {
            btn_bitmap = ScalableBitmap(this, kInfo.m_strPressedName, 32);
        }
        else if ( kInfo.m_kType == current_move_step )
        {
            btn_bitmap = ScalableBitmap(this, kInfo.m_strPressedName, 32);
        }
        else
        {
            btn_bitmap = bIsHover ? 
                             ScalableBitmap(this, kInfo.m_strNormalName, 32) : ScalableBitmap(this, kInfo.m_strHoverName, 32);
        }

        if (btn_bitmap.bmp().IsOk()) {
            // Calculate scaling to fit button size
            double scale_x = w / btn_bitmap.GetBmpWidth();
            double scale_y = h / btn_bitmap.GetBmpHeight();
            double scale = std::min(scale_x, scale_y) * 0.9; // 90% of button size
            
            double img_w = btn_bitmap.GetBmpWidth() * scale;
            double img_h = btn_bitmap.GetBmpHeight() * scale;
            
            // Draw highlight border if active (draw behind image)
            if (bIsHover) {
                wxGraphicsPath highlight_path = gc->CreatePath();
                // Use the actual image dimensions for the highlight border
                double border_padding = 4; // Small padding around image
                highlight_path.AddRoundedRectangle(
                    x - img_w/2 - border_padding, 
                    y - img_h/2 - border_padding, 
                    img_w + 2 * border_padding, 
                    img_h + 2 * border_padding, 
                    8); // Fixed corner radius
                gc->SetPen(wxPen(wxColour(255, 140, 90), 3));
                gc->SetBrush(wxBrush(wxColour(0, 0, 0, 0))); // Transparent fill
                gc->DrawPath(highlight_path);
            }
            
            // Draw the image centered in the button area
            gc->DrawBitmap(btn_bitmap.bmp(), 
                          x - img_w/2, y - img_h/2, 
                          img_w, img_h);
        }
    };
    
    auto uCount = m_kDrawButtonInfo.size();
    for ( auto i = 0; i < uCount; ++i )
    {
        drawImageButton( m_kDrawButtonInfo[ i ]);
    }
    gc->PopState();
}

void PhrozenAxisCtrlButton::mouseDown(wxMouseEvent& event)
{
    event.Skip();
    pressedDown = true;

    switch ( current_pos )
    {
        case CurrentPos::MOVE_STEP_01MM:  
        case CurrentPos::MOVE_STEP_1MM:   
        case CurrentPos::MOVE_STEP_10MM:  
            current_move_step = current_pos; 
            Refresh();
            break;
    }

    SetFocus();
    CaptureMouse();
}

void PhrozenAxisCtrlButton::mouseReleased(wxMouseEvent& event)
{
    event.Skip();
    if (pressedDown) {
        pressedDown = false;
        ReleaseMouse();
        if (wxRect({ 0, 0 }, GetSize()).Contains(event.GetPosition()))
            sendButtonEvent();
    }
}

void PhrozenAxisCtrlButton::mouseMoving(wxMouseEvent& event)
{
    if (pressedDown)
    return;
        
    wxPoint mouse_pos(event.GetX(), event.GetY());
    wxPoint transformed_mouse_pos = mouse_pos - center;
    
    current_pos = CurrentPos::UNDEFINED;
    auto fnHit =[&] ( const CButtonInfo& kInfo ) -> bool
    {
        int nHalfSizeX = kInfo.m_kSize.GetX() / 2;
        int nHalfSizeY = kInfo.m_kSize.GetY() / 2;
        bool bHitX = transformed_mouse_pos.x >= kInfo.m_kCenter.GetX() - nHalfSizeX
                     && transformed_mouse_pos.x <= kInfo.m_kCenter.GetX() + nHalfSizeX;
        bool bHitY = transformed_mouse_pos.y >= kInfo.m_kCenter.GetY() - nHalfSizeY
                     && transformed_mouse_pos.y <= kInfo.m_kCenter.GetY() + nHalfSizeY;
        return bHitX && bHitY;
    };

    auto uCount = m_kDrawButtonInfo.size();
    for ( auto i = 0; i < uCount; ++i )
    {
        if ( fnHit( m_kDrawButtonInfo[ i ] ) )
        {
            current_pos = m_kDrawButtonInfo[ i ].m_kType;
            break;
        }
    }
    // Trigger repaint if position changed
    if (current_pos != last_pos) {
        last_pos = current_pos;
        Refresh();
    }
}

void PhrozenAxisCtrlButton::sendButtonEvent()
{
    if ( current_pos > CurrentPos::AXIS_HOME )
    {
        return;
    }
    wxCommandEvent event(wxEVT_COMMAND_BUTTON_CLICKED, GetId());
    event.SetEventObject(this);
    event.SetInt(current_pos);
    event.SetExtraLong(current_move_step);
    GetEventHandler()->ProcessEvent(event);
}

#pragma endregion
