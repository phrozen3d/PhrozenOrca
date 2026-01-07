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

// AMS Slot Configuration
struct AMSSlotConfig {
    
    AMSSlotConfig() {}
    AMSSlotConfig( bool bIsEntry, bool bIsParking, bool bIsLoading, bool bIsStartLoading, bool bIsStartUnload )
    : m_bIsEntry(bIsEntry)
    , m_bIsParking(bIsParking)
    , m_bIsLoading(bIsLoading)
    , m_bStartLoading(bIsStartLoading)
    , m_bStartUnloading(bIsStartUnload)
    {}

    bool IsEmpty() const { return !m_bIsEntry && !m_bIsParking && !m_bIsLoading; }
    bool IsEntry() const { return m_bIsEntry; }
    bool IsParking() const { return m_bIsParking; }
    bool IsLoading()const { return m_bIsLoading; }
    bool IsStartLoading()const { return m_bStartLoading; }
    bool IsStartUnload()const { return m_bStartUnloading; }

    bool m_bIsEntry = false;
    bool m_bIsParking = false;
    bool m_bIsLoading = false;
    bool m_bStartLoading = false;
    bool m_bStartUnloading = false;
};

struct FilamentSystemState {
    
    FilamentSystemState(){}

    void SetNozzleFilamentDetected( bool bDetected ){ bNozzleFilamentDetected = bDetected; }
    void SetAMSSystemDetected( bool bDetected ) { bAMSSystemDetected = bDetected; }
    void SetAMSSlotState( int nIndex, bool bEntry, bool bParking, bool bLoading, bool bStartLoading, bool bStartUnload ) 
    { m_slot_configs[ nIndex ] = AMSSlotConfig( bEntry, bParking, bLoading, bStartLoading, bStartUnload ); }

    bool IsNozzleFilamentDetected() { return bNozzleFilamentDetected; }
    bool IsAMSSystemDetected() { return bAMSSystemDetected; }

    // state info
    bool bNozzleFilamentDetected = false;   // has filament in nazzle sensor.
    // AMS
    bool bAMSSystemDetected = false;        // detect AMS system is connected.
    static const int SLOT_COUNT = 4;
    AMSSlotConfig m_slot_configs[ SLOT_COUNT ];
};

enum class FilamentInputType {
    Empty,      // No filament connect
    AMS,        // multi filament
    Spool       // single filament
};

class FilamentStatusPanel : public wxPanel
{
public:
    FilamentStatusPanel(wxWindow* parent,
                  wxWindowID id = wxID_ANY,
                  const wxPoint& pos = wxDefaultPosition,
                  const wxSize& size = wxDefaultSize);

    virtual ~FilamentStatusPanel();

    void UpdateFilamentState( const FilamentSystemState& kFilamentState );

    // Set configuration for specific slot (0-3 for A1-A4)
    void SetSlotConfig(int slot_index, const AMSSlotConfig& config);

    // Get configuration for specific slot
    AMSSlotConfig GetSlotConfig(int slot_index) const;

    // Get selected slot
    int GetSelectedSlot() const { return m_selected_slot; }

    void msw_rescale();

    // Event handlers
    void OnPaint(wxPaintEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnEraseBackground(wxEraseEvent& event);

protected:


    // Drawing functions
    void DrawPanel(wxGraphicsContext* gc);
    void DrawSlotGroup(wxGraphicsContext* gc, int slot_index, double x_offset);
    void DrawLightCircle(wxGraphicsContext* gc, double cx, double cy, const AMSSlotConfig& config );
    void DrawSlotOutline(wxGraphicsContext* gc, double x, double y, const AMSSlotConfig& config, bool is_selected );
    void DrawSlotTitle(wxGraphicsContext* gc, double x, double y, const AMSSlotConfig& config, int slot_number );
    void DrawConnectionLine(wxGraphicsContext* gc, int slot_index, const AMSSlotConfig& config );
    void DrawFeedPortRectangle(wxGraphicsContext* gc);
    void DrawFilamentToNozzle( wxGraphicsContext* gc, const wxPoint& pos_spool, 
                                                      const wxPoint& pos_ams, 
                                                      const wxPoint& pos_nozzle  );

    // Helper functions
    wxRect GetSlotRect(int slot_index) const;
    int HitTestSlot(const wxPoint& pos) const;
    void DrawDashedRoundedRect(wxGraphicsContext* gc, double x, double y, double w, double h,
                               double radius, const wxColour& color, double width,
                               double dash_on, double dash_off);
    void DrawSpoolHolder(wxGraphicsContext* gc);

    // Set selected slot (-1 for none)
    void SetSelectedSlot(int slot_index);
    void SetFilamentInputType( const FilamentInputType& eType );

    // return true if any filament loaded from slot to nazzle
    bool IsAnyAMSFilamentLoading();

    bool IsAMSEnabled(){ return m_eInputType == FilamentInputType::AMS; }
    bool IsSpoolEnabled(){ return m_eInputType == FilamentInputType::Spool; }


private:
    

    FilamentInputType m_eInputType;

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
    wxColour m_loading_line_color;
    wxColour m_feed_port_color;
    wxColour light_color_entry;
    wxColour light_color_error;

    wxColour m_text_color;

    double line_width_enable = 6.0;
    double line_width_disable = 2.0;

    FilamentSystemState m_kFilamentState;

    wxStaticBitmap* m_spool_tip = {nullptr};
    wxStaticBitmap* m_ams_tip = {nullptr};

    wxDECLARE_EVENT_TABLE();
};

class PhrozenFilamentControl : public wxPanel
{
public:
    PhrozenFilamentControl(wxWindow* parent,
                    wxWindowID id = wxID_ANY,
                    const wxPoint& pos = wxDefaultPosition,
                    const wxSize& size = wxDefaultSize);

    virtual ~PhrozenFilamentControl();

    // Get the unload all button
    Button* GetUnloadAllButton() { return m_ams_unload_all_btn; }

    // Get the upper buttons
    Button* GetLoadButton() { return m_load_button; }
    Button* GetUnloadButton() { return m_unload_button; }

    void msw_rescale();

    void UpdateFilamentState( const FilamentSystemState& kState );

    int GetSelectedAmsSlotIndex();


private:
    
    wxSizer* create_ams_control_button( wxWindow* pParent );
    wxSizer* create_nozzle_image( wxWindow* pParent );

    void OnUnloadAllButtonClicked( wxMouseEvent &evt );
    
    FilamentStatusPanel* m_filament_status_panel{nullptr};
    Button* m_ams_unload_all_btn{nullptr};
    Button* m_load_button{nullptr};
    Button* m_unload_button{nullptr};
    wxStaticBitmap* m_nozzle_staticBitmap{ nullptr };

    int m_nMinWidth; //= 375;
    int m_nMinHeight; //= 168 + FromDIP(32) + FromDIP(20));

    int m_nButtonGroupMinWidth; //=FromDIP(200)
    int n_nButtonGroupMinHight; //=FromDIP(68)

    FilamentSystemState m_kFilamentState;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_AMSGroupPanel_hpp_
