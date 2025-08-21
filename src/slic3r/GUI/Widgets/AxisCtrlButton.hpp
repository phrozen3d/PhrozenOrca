#ifndef slic3r_GUI_AxisCtrlButton_hpp_
#define slic3r_GUI_AxisCtrlButton_hpp_

#include <wx/stattext.h>
#include <wx/vlbox.h>
#include <wx/combo.h>
#include "../wxExtensions.hpp"
#include "StateHandler.hpp"

#pragma region AxisCtrlButton
class AxisCtrlButton : public wxWindow
{

protected:
    wxSize minSize;
    double stretch;
    double r_outer;
    double r_inner;
    double r_home;
    double r_blank;
    double gap;
	wxPoint center;

    StateHandler    state_handler;
    StateColor      text_color;
    StateColor      border_color;
    StateColor      background_color;
	StateColor      inner_background_color;

    ScalableBitmap m_icon;

	bool pressedDown = false;

    unsigned char last_pos;
    unsigned char current_pos;
    enum CurrentPos {
        OUTER_UP = 0,
        OUTER_LEFT = 1,
        OUTER_DOWN = 2,
        OUTER_RIGHT = 3,
        INNER_UP = 4,
        INNER_LEFT = 5,
        INNER_DOWN = 6,
        INNER_RIGHT = 7,
        INNER_HOME = 8,
        UNDEFINED = 9
    };

public:
    AxisCtrlButton(wxWindow *parent, ScalableBitmap &icon, long style = 0);

    void SetMinSize(const wxSize& size) override;

    void SetTextColor(StateColor const& color);

    void SetBorderColor(StateColor const& color);

    void SetBackgroundColor(StateColor const& color);

    void SetInnerBackgroundColor(StateColor const& color);

    void SetBitmap(ScalableBitmap &bmp);

    void Rescale();

protected:
    virtual void render(wxDC& dc);
private:
    void updateParams();

    void paintEvent(wxPaintEvent& evt);

    void mouseDown(wxMouseEvent& event);
    void mouseReleased(wxMouseEvent& event);
    void mouseMoving(wxMouseEvent& event);

    void sendButtonEvent();

    DECLARE_EVENT_TABLE()
};
#pragma endregion

#pragma region PhrozenAxisCtrlButton
class PhrozenAxisCtrlButton : public AxisCtrlButton
{
public:
    PhrozenAxisCtrlButton(wxWindow *parent, ScalableBitmap &icon, long style = 0);
protected:
    virtual void render(wxDC& dc) override;

private:
};
#pragma endregion


#endif // !slic3r_GUI_Button_hpp_
