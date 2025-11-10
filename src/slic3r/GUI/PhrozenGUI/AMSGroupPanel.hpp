#ifndef slic3r_AMSGroupPanel_hpp_
#define slic3r_AMSGroupPanel_hpp_

#include <wx/panel.h>
#include <wx/dcclient.h>
#include <wx/dcgraph.h>
#include <wx/graphics.h>
#include <vector>
#include "../wxExtensions.hpp"

namespace Slic3r {
namespace GUI {

// AMS Slot State
enum class AMSSlotState {
    Empty,      // No filament installed
    Installed,  // Filament installed
    Loading     // Currently loading
};

// AMS Slot Configuration
struct AMSSlotConfig {
    AMSSlotState state;
    wxColour light_color;     // Color of the indicator light
    bool is_enabled;          // Whether the slot is enabled

    AMSSlotConfig()
        : state(AMSSlotState::Empty)
        , light_color(wxColour(0x00, 0xFF, 0x00))
        , is_enabled(true)
    {}
};

class AMSGroupPanel : public wxPanel
{
public:
    AMSGroupPanel(wxWindow* parent,
                  wxWindowID id = wxID_ANY,
                  const wxPoint& pos = wxDefaultPosition,
                  const wxSize& size = wxDefaultSize);

    virtual ~AMSGroupPanel();

    // Set configuration for specific slot (0-3 for A1-A4)
    void SetSlotConfig(int slot_index, const AMSSlotConfig& config);

    // Get configuration for specific slot
    AMSSlotConfig GetSlotConfig(int slot_index) const;

    // Set selected slot (-1 for none)
    void SetSelectedSlot(int slot_index);

    // Get selected slot
    int GetSelectedSlot() const { return m_selected_slot; }

    // Enable/disable slot
    void EnableSlot(int slot_index, bool enaOnLeftDownble);

    // Set slot light color
    void SetSlotLightColor(int slot_index, const wxColour& color);

    // Set slot state
    void SetSlotState(int slot_index, AMSSlotState state);

    void SetEnable( bool bEnable ) { m_bEnable = bEnable; }
    
    void msw_rescale();

protected:
    // Event handlers
    void OnPaint(wxPaintEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnEraseBackground(wxEraseEvent& event);

    // Drawing functions
    void DrawPanel(wxGraphicsContext* gc);
    void DrawSlotGroup(wxGraphicsContext* gc, int slot_index, double x_offset);
    void DrawLightCircle(wxGraphicsContext* gc, double cx, double cy, int slot_index );
    void DrawSlotOutline(wxGraphicsContext* gc, double x, double y, bool is_selected, bool is_enabled);
    void DrawSlotTitle(wxGraphicsContext* gc, double x, double y, int slot_number, bool is_enabled);
    void DrawConnectionLine(wxGraphicsContext* gc, int slot_index, bool is_enabled);
    void DrawFeedPortRectangle(wxGraphicsContext* gc);

    // Helper functions
    wxRect GetSlotRect(int slot_index) const;
    int HitTestSlot(const wxPoint& pos) const;
    void DrawDashedRoundedRect(wxGraphicsContext* gc, double x, double y, double w, double h,
                               double radius, const wxColour& color, double width,
                               double dash_on, double dash_off);

private:
    static const int SLOT_COUNT = 4;

    // Slot configurations
    AMSSlotConfig m_slot_configs[SLOT_COUNT];

    // Current selected slot (-1 for none)
    int m_selected_slot;

    // Panel dimensions (matching SVG viewBox: 289x220, but actual panel height is 168)
    static constexpr double PANEL_WIDTH = 289.0;
    static constexpr double PANEL_HEIGHT = 168.0;

    // Colors
    wxColour m_panel_bg;
    wxColour m_slot_outline_enabled;
    wxColour m_slot_outline_disabled;
    wxColour m_slot_outline_selected;
    wxColour m_line_color;
    wxColour m_feed_port_color;

    double line_width_enable = 6.0;
    double line_width_disable = 2.0;

    bool m_bEnable = false;

    // AMS Monitor images
    ScalableBitmap m_AMS_frame;
    ScalableBitmap m_AMS_spool_holder;
    ScalableBitmap m_AMS_initial;
    ScalableBitmap m_AMS_tooltips;
    ScalableBitmap m_AMS_tooltipsframe;
    ScalableBitmap m_AMS_tooltips_cartridge;
    ScalableBitmap m_AMS_tooltips_ams;
    ScalableBitmap m_AMS_A1;
    ScalableBitmap m_AMS_A2;
    ScalableBitmap m_AMS_A3;
    ScalableBitmap m_AMS_A4;
    ScalableBitmap m_AMS_install_light;
    ScalableBitmap m_AMS_line1;
    ScalableBitmap m_AMS_line2;
    ScalableBitmap m_AMS_line3;
    ScalableBitmap m_AMS_line4;
    ScalableBitmap m_AMS_load_A1;
    ScalableBitmap m_AMS_load_A2;
    ScalableBitmap m_AMS_load_A3;
    ScalableBitmap m_AMS_load_A4;
    ScalableBitmap m_AMS_head_A1;
    ScalableBitmap m_AMS_head_A2;
    ScalableBitmap m_AMS_head_A3;
    ScalableBitmap m_AMS_head_A4;
    ScalableBitmap m_AMS_Head;
    ScalableBitmap m_AMS_rectangle;
    ScalableBitmap m_AMS_selected;

    wxDECLARE_EVENT_TABLE();
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_AMSGroupPanel_hpp_
