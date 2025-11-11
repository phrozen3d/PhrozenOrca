#ifndef slic3r_AMSGroupPanel_hpp_
#define slic3r_AMSGroupPanel_hpp_

#include <wx/panel.h>
#include <wx/dcclient.h>
#include <wx/dcgraph.h>
#include <wx/graphics.h>
#include <vector>
#include "../wxExtensions.hpp"
#include "../Widgets/ImageSwitchButton.hpp"

namespace Slic3r {
namespace GUI {

// AMS Slot State
enum class AMSSlotState {
    Empty,      // No filament installed
    Installed,  // Filament installed (now no use)
    Loading     // Currently loading
};

// AMS Slot Configuration
struct AMSSlotConfig {
    AMSSlotState state;
    wxColour light_color;     // Color of the indicator light

    AMSSlotConfig()
        : state(AMSSlotState::Empty)
        , light_color(wxColour(0x00, 0xFF, 0x00))
    {}
};

enum class FilementInputType {
    Empty,      // No filament connect
    AMS,        // multi filament
    Spool       // single filament
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

    // Set slot light color
    void SetSlotLightColor(int slot_index, const wxColour& color);

    // Set slot state
    void SetSlotState(int slot_index, AMSSlotState state);
    
    void msw_rescale();

    void SetFilamentInputType( const FilementInputType& eType );

protected:
    // Event handlers
    void OnPaint(wxPaintEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnEraseBackground(wxEraseEvent& event);

    // Drawing functions
    void DrawPanel(wxGraphicsContext* gc);
    void DrawSlotGroup(wxGraphicsContext* gc, int slot_index, double x_offset);
    void DrawLightCircle(wxGraphicsContext* gc, double cx, double cy, int slot_index, bool is_enabled );
    void DrawSlotOutline(wxGraphicsContext* gc, double x, double y, bool is_selected, bool is_enabled);
    void DrawSlotTitle(wxGraphicsContext* gc, double x, double y, int slot_number, bool is_enabled);
    void DrawConnectionLine(wxGraphicsContext* gc, int slot_index, bool is_enabled, bool is_loading );
    void DrawFeedPortRectangle(wxGraphicsContext* gc);

    // Helper functions
    wxRect GetSlotRect(int slot_index) const;
    int HitTestSlot(const wxPoint& pos) const;
    void DrawDashedRoundedRect(wxGraphicsContext* gc, double x, double y, double w, double h,
                               double radius, const wxColour& color, double width,
                               double dash_on, double dash_off);
    void DrawSpoolHolder(wxGraphicsContext* gc);

private:
    static const int SLOT_COUNT = 4;

    FilementInputType m_eInputType;

    // Slot configurations
    AMSSlotConfig m_slot_configs[SLOT_COUNT];

    // Current selected slot (-1 for none)
    int m_selected_slot;

    // Panel dimensions
    // Original: 289x168, now extended to include SpoolHolder on the left (76 width) + 10px gap
    static constexpr double SPOOL_HOLDER_WIDTH = 76.0;
    static constexpr double GAP_WIDTH = 10.0;
    static constexpr double ORIGINAL_PANEL_WIDTH = 289.0;
    static constexpr double PANEL_WIDTH = SPOOL_HOLDER_WIDTH + GAP_WIDTH + ORIGINAL_PANEL_WIDTH;  // 375.0
    static constexpr double PANEL_HEIGHT = 168.0;

    // Colors
    wxColour m_panel_bg;
    wxColour m_slot_outline_enabled;
    wxColour m_slot_outline_disabled;
    wxColour m_slot_outline_selected;
    wxColour m_line_color;
    wxColour m_feed_port_color;

    wxColour m_text_color;

    double line_width_enable = 6.0;
    double line_width_disable = 2.0;

    wxDECLARE_EVENT_TABLE();
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_AMSGroupPanel_hpp_
