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

        //Phrozen Style Control Button
        PHROZEN_UP = 9,
        PHROZEN_LEFT = 10,
        PHROZEN_DOWN = 11,
        PHROZEN_RIHGT = 12,
        MOVE_STEP_01MM = 13,
        MOVE_STEP_1MM = 14,
        MOVE_STEP_10MM = 15,


        UNDEFINED = 16
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
    void render(wxDC& dc);
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
class PhrozenAxisCtrlButton : public wxWindow
{

public:

    enum CurrentPos 
    {
        AXIS_UP           = 0,
        AXIS_LEFT         = 1,
        AXIS_DOWN         = 2,
        AXIS_RIGHT        = 3,
        AXIS_HOME         = 4,
        MOVE_STEP_01MM    = 5,
        MOVE_STEP_1MM     = 6,
        MOVE_STEP_10MM    = 7,
        BUTTON_COUNT      = 8,

        UNDEFINED           = 255
    };

protected:
    wxSize minSize;
    double stretch;
    double gap;

    wxSize m_kAxisButtonSize;
    wxSize m_kMoveStepButtonSize;
	wxPoint center;

	bool pressedDown = false;

    unsigned char last_pos = CurrentPos::UNDEFINED;
    unsigned char current_pos = CurrentPos::UNDEFINED;
    unsigned char current_move_step = CurrentPos::MOVE_STEP_1MM;

    class CButtonInfo
    {
    public:
        CButtonInfo( const CurrentPos& kType, const wxSize& kCenter, const wxSize& kSize )
        : m_kType( kType ), m_kCenter( kCenter ), m_kSize( kSize ){}

        void SetImage( const std::string& strNormal, const std::string& strHover, const std::string& strPressed  )
        {
            m_strNormalName = strNormal;
            m_strHoverName = strHover;
            m_strPressedName = strPressed;
        }

        CurrentPos m_kType = CurrentPos::UNDEFINED;
        wxSize m_kCenter;
        wxSize m_kSize;
        std::string m_strNormalName;
        std::string m_strHoverName;
        std::string m_strPressedName;
    };
    std::vector< CButtonInfo > m_kDrawButtonInfo;

public:
    PhrozenAxisCtrlButton(wxWindow *parent, long style = 0 );

    void SetMinSize(const wxSize& size) override;

    void SetTextColor(StateColor const& color);

    void SetBorderColor(StateColor const& color);

    void SetBackgroundColor(StateColor const& color);

    void SetInnerBackgroundColor(StateColor const& color);

    void SetBitmap(ScalableBitmap &bmp);

    void Rescale();

protected:
    void render(wxDC& dc);
    void renderPhrozenStyle2(wxDC& dc);

private:
    void updateParams();
    void initializeDrawButtonInfo();

    void paintEvent(wxPaintEvent& evt);

    void mouseDown(wxMouseEvent& event);
    void mouseReleased(wxMouseEvent& event);
    void mouseMoving(wxMouseEvent& event);
    

    void sendButtonEvent();

    DECLARE_EVENT_TABLE()
};
#pragma endregion


#endif // !slic3r_GUI_Button_hpp_
