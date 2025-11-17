#ifndef slic3r_PhrozenStatusPanel_hpp_
#define slic3r_PhrozenStatusPanel_hpp_

#include "libslic3r/ProjectTask.hpp"
#include <wx/panel.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/sizer.h>
#include <wx/gbsizer.h>
#include <wx/webrequest.h>
#include "../DeviceManager.hpp"
#include "../MonitorPage.hpp"
#include "../SliceInfoPanel.hpp"
#include "../CameraPopup.hpp"
#include "../GUI.hpp"
#include "../wxMediaCtrl2.h"
#include "../MediaPlayCtrl.h"
#include "../AMSSetting.hpp"
#include "../Calibration.hpp"
#include "../CalibrationWizardPage.hpp"
#include "../PrintOptionsDialog.hpp"
#include "../AMSMaterialsSetting.hpp"
#include "../ExtrusionCalibration.hpp"
#include "../ReleaseNote.hpp"
#include "../Widgets/SwitchButton.hpp"
#include "../Widgets/AxisCtrlButton.hpp"
#include "../Widgets/TextInput.hpp"
#include "../Widgets/TempInput.hpp"
#include "../Widgets/StaticLine.hpp"
#include "../Widgets/ProgressBar.hpp"
#include "../Widgets/ImageSwitchButton.hpp"
#include "../Widgets/AMSControl.hpp"
#include "../Widgets/FanControl.hpp"
#include "../HMS.hpp"
#include "../StatusPanel.hpp"

namespace Slic3r {
namespace GUI {
class PhrozenFilamentControl;

enum class PhrozenParamControl : int32_t
{
    Temperature_Nozzle,
    Temperature_HeatedBed,
    Cooling_Auxiliary,
    Cooling_Part,
    Cooling_Shield
};

enum class PhrozenPrintSpeed : int32_t
{
    Silent,
    Quite,
    Standard,
    Fast,
    Turbo
};

enum class PhrozenNozzleMoveRange : int32_t
{
    Range_01_MM,
    Range_1_MM,
    Range_10_MM
};

enum class PhrozenPrintNozzleOffsetRange : int32_t
{
    Range_0005_MM,
    Range_001_MM,
    Range_005_MM,
    Range_01_MM
};

enum class PhrozenMovement : int32_t
{
    Nozzle_X_Positive,
    Nozzle_X_Negative,
    Nozzle_Y_Positive,
    Nozzle_Y_Negative,
    Nozzle_Z_Positive,
    Nozzle_Z_Negative,
    Nozzle_Home,
    Nozzle_Home_XY,
    Nozzle_Offset_Positive,
    Nozzle_Offset_Negative,
};

class PhrozenStatusBasePanel : public wxScrolledWindow//StatusBasePanel
{
public:
    PhrozenStatusBasePanel(wxWindow*       parent,
                         wxWindowID      id    = wxID_ANY,
                         const wxPoint&  pos   = wxDefaultPosition,
                         const wxSize&   size  = wxDefaultSize,
                         long            style = wxTAB_TRAVERSAL,
                         const wxString& name  = wxEmptyString);

    ~PhrozenStatusBasePanel();

    void Initizlize();
    void init_bitmaps();

    wxBoxSizer* create_monitoring_page();
    wxBoxSizer* create_machine_control_page(wxWindow* parent);
    wxBoxSizer* create_temp_axis_group(wxWindow* parent);
    wxBoxSizer* create_temp_control(wxWindow* parent);
    wxBoxSizer* create_misc_control(wxWindow* parent);
    wxBoxSizer* create_axis_control(wxWindow* parent);
    wxBoxSizer* create_bed_control(wxWindow* parent);
    wxBoxSizer* create_extruder_control(wxWindow* parent);
    
    std::vector<unsigned char> m_kWebCameraImageData;
    wxBitmap m_kCurrentWebCamBitmap;
    std::unique_ptr< wxTimer > m_spWebCam_refresh_timer = nullptr;
    bool IsWebCamRefreshTimerInitialized() { return m_spWebCam_refresh_timer != nullptr; }

#pragma region OrcaOriginalMember
    protected:
    wxBitmap m_item_placeholder;
    ScalableBitmap m_thumbnail_placeholder;
    ScalableBitmap m_thumbnail_brokenimg;
    ScalableBitmap m_thumbnail_sdcard;
    wxBitmap m_bitmap_item_prediction;
    wxBitmap m_bitmap_item_cost;
    wxBitmap m_bitmap_item_print;
    ScalableBitmap m_bitmap_speed;
    ScalableBitmap m_bitmap_speed_active;
    ScalableBitmap m_bitmap_axis_home;
    ScalableBitmap m_bitmap_lamp_on;
    ScalableBitmap m_bitmap_lamp_off;
    ScalableBitmap m_bitmap_fan_on;
    ScalableBitmap m_bitmap_fan_off;
    ScalableBitmap m_bitmap_use_time;
    ScalableBitmap m_bitmap_use_weight;
    wxBitmap m_bitmap_extruder_empty_load;
    wxBitmap m_bitmap_extruder_filled_load;
    wxBitmap m_bitmap_extruder_empty_unload;
    wxBitmap m_bitmap_extruder_filled_unload;

    CameraRecordingStatus m_state_recording{CameraRecordingStatus::RECORDING_NONE};
    CameraTimelapseStatus m_state_timelapse{CameraTimelapseStatus::TIMELAPSE_NONE};


    CameraItem *m_setting_button;

    wxBitmap m_bitmap_camera;
    ScalableBitmap m_bitmap_sdcard_state_normal;
    ScalableBitmap m_bitmap_sdcard_state_abnormal;
    ScalableBitmap m_bitmap_sdcard_state_no;
    ScalableBitmap m_bitmap_recording_on;
    ScalableBitmap m_bitmap_recording_off;
    ScalableBitmap m_bitmap_timelapse_on;
    ScalableBitmap m_bitmap_timelapse_off;
    ScalableBitmap m_bitmap_vcamera_on;
    ScalableBitmap m_bitmap_vcamera_off;
    ScalableBitmap m_bitmap_switch_camera;

    /* title panel */
    wxPanel *       media_ctrl_panel;
    wxPanel *       m_panel_monitoring_title;
    wxPanel *       m_panel_printing_title;
    wxPanel *       m_panel_control_title;

    wxStaticText*   m_staticText_consumption_of_time;
    wxStaticText *  m_staticText_consumption_of_weight;
    Label *         m_staticText_monitoring;
    wxStaticText *  m_staticText_timelapse;
    SwitchButton *  m_bmToggleBtn_timelapse;

    wxStaticBitmap *m_bitmap_camera_img;
    wxStaticBitmap *m_bitmap_recording_img;
    wxStaticBitmap *m_bitmap_timelapse_img;
    wxStaticBitmap* m_bitmap_vcamera_img;
    wxStaticBitmap *m_bitmap_sdcard_img;
    wxStaticBitmap *m_bitmap_static_use_time;
    wxStaticBitmap *m_bitmap_static_use_weight;
    wxStaticBitmap* m_camera_switch_button;


    wxMediaCtrl2 *  m_media_ctrl;
    MediaPlayCtrl * m_media_play_ctrl{nullptr};

    Label *         m_staticText_printing;
    wxStaticBitmap *m_bitmap_thumbnail;
    wxStaticText *  m_staticText_subtask_value;
    wxStaticText *  m_printing_stage_value;
    wxStaticText *  m_staticText_profile_value;
    ProgressBar*    m_gauge_progress;
    wxStaticText *  m_staticText_progress_percent;
    wxStaticText *  m_staticText_progress_percent_icon;
    wxStaticText *  m_staticText_progress_left;
    wxStaticText *  m_staticText_layers;
    Button *        m_button_report;
    ScalableButton *m_button_pause_resume;
    ScalableButton *m_button_abort;
    Button *        m_button_clean;
    wxWebView *     m_custom_camera_view{nullptr};

    wxStaticText *  m_text_tasklist_caption;

    Label *  m_staticText_control;
    ImageSwitchButton *m_switch_lamp;
    int               m_switch_lamp_timeout{0};
    ImageSwitchButton *m_switch_speed;

    /* TempInput */
    wxBoxSizer *    m_misc_ctrl_sizer;
    StaticBox*      m_fan_panel; 
    StaticLine *    m_line_nozzle;
    TempInput* m_tempCtrl_nozzle;
    TempInput *     m_tempCtrl_bed; //remove later
    TempInput *     m_tempCtrl_chamber; //remove later
    int             m_temp_chamber_timeout {0};
    bool             m_current_support_cham_fan{true};
    bool             m_current_support_aux_fan{true};
    FanSwitchButton *m_switch_nozzle_fan;
    int             m_switch_nozzle_fan_timeout{0};
    FanSwitchButton *m_switch_printing_fan;
    int             m_switch_printing_fan_timeout{0};
    FanSwitchButton *m_switch_cham_fan;
    int             m_switch_cham_fan_timeout{0};
    wxPanel*        m_switch_block_fan;

    float           m_fixed_aspect_ratio{1.8};

    AxisCtrlButton *m_bpButton_xy;
    PhrozenAxisCtrlButton *m_phButton_xy;

    //wxStaticText *  m_staticText_xy;
    Button *        m_bpButton_z_10;
    Button *        m_bpButton_z_1;
    Button *        m_bpButton_z_down_1;
    Button *        m_bpButton_z_down_10;
    Button *        m_button_unload;
    wxStaticText *  m_staticText_z_tip;
    wxStaticText *  m_staticText_e;
    Button *        m_bpButton_e_10;
    Button *        m_bpButton_e_down_10;
    StaticLine *    m_temp_extruder_line;
    wxBoxSizer*     m_ams_list;
    wxStaticText *  m_ams_debug;
    bool            m_show_ams_group{false};
    wxStaticBitmap *m_ams_extruder_img;
    wxStaticBitmap* m_bitmap_extruder_img;
    wxPanel *       m_panel_separator_right;
    wxPanel *       m_panel_separotor_bottom;
    wxGridBagSizer *m_tasklist_info_sizer{nullptr};
    wxBoxSizer *    m_printing_sizer;
    wxBoxSizer *    m_tasklist_sizer;
    wxBoxSizer *    m_tasklist_caption_sizer;
    wxPanel*        m_panel_error_txt;
    wxPanel*        m_staticline;
    Label *         m_error_text;
    wxStaticText*   m_staticText_calibration_caption;
    wxStaticText*   m_staticText_calibration_caption_top;
    wxStaticText*   m_calibration_text;
    Button*         m_parts_btn;
    Button*         m_options_btn;
    Button*         m_calibration_btn;
    StepIndicator*  m_calibration_flow;

    wxPanel *       m_machine_ctrl_panel;
    PrintingTaskPanel *       m_project_task_panel;

    // Virtual event handlers, override them in your derived class
    virtual void on_subtask_pause_resume(wxCommandEvent &event) { event.Skip(); }
    virtual void on_subtask_abort(wxCommandEvent &event) { event.Skip(); }
    virtual void on_lamp_switch(wxCommandEvent &event) { event.Skip(); }
    virtual void on_bed_temp_kill_focus(wxFocusEvent &event) { event.Skip(); }
    virtual void on_bed_temp_set_focus(wxFocusEvent &event) { event.Skip(); }
    virtual void on_nozzle_temp_kill_focus(wxFocusEvent &event) { event.Skip(); }
    virtual void on_nozzle_temp_set_focus(wxFocusEvent &event) { event.Skip(); }    
    virtual void on_nozzle_fan_switch(wxCommandEvent &event) { event.Skip(); }
    virtual void on_printing_fan_switch(wxCommandEvent &event) { event.Skip(); }
    virtual void on_axis_ctrl_z_up_10(wxCommandEvent &event) { event.Skip(); }
    virtual void on_axis_ctrl_z_up_1(wxCommandEvent &event) { event.Skip(); }
    virtual void on_axis_ctrl_z_down_1(wxCommandEvent &event) { event.Skip(); }
    virtual void on_axis_ctrl_z_down_10(wxCommandEvent &event) { event.Skip(); }
    virtual void on_axis_ctrl_e_up_10(wxCommandEvent &event) { event.Skip(); }
    virtual void on_axis_ctrl_e_down_10(wxCommandEvent &event) { event.Skip(); }
    void on_ams_unload_all(wxCommandEvent& event);
    
public:   
    void on_camera_source_change(wxCommandEvent& event);
    void handle_camera_source_change();
    void remove_controls();
    void on_webview_navigating(wxWebViewEvent& evt);
    void on_camera_switch_toggled(wxMouseEvent& event);
    void toggle_custom_camera();
    void toggle_builtin_camera();

public:

    void SetMachineObject( MachineObject* pObj ) { obj = pObj; }
    MachineObject* obj{nullptr};

    void reset_temp_misc_control();
    int before_error_code = 0;
    int skip_print_error = 0;
    wxBoxSizer *create_ams_group(wxWindow *parent);
    wxBoxSizer *create_settings_group(wxWindow *parent);

    void show_ams_group(bool show = true);
    MediaPlayCtrl* get_media_play_ctrl() {return m_media_play_ctrl;};

#pragma endregion


public:

#pragma region ui_control_panel
    wxFlexGridSizer* GenNozzleTempControllor( wxWindow* pParent );
    wxFlexGridSizer* GenHeatedBedTempControllor( wxWindow* pParent );
    wxFlexGridSizer* GenSpeed_PrintLevel( wxWindow* pParent );
    wxFlexGridSizer* GenCooling_Auxiliary( wxWindow* pParent );
    wxFlexGridSizer* GenCooling_Part( wxWindow* pParent );
    wxFlexGridSizer* GenCooling_Shield( wxWindow* pParent );
    wxSizer* GenManualAdjustment_moveRange( wxWindow* pParent );
    wxSizer* GenManualAdjustment_move_xy( wxWindow* pParent );
    wxSizer* GenManualAdjustment_move_z( wxWindow* pParent );
    wxSizer* GenManualAdjustment_z_offset( wxWindow* pParent );
    wxBitmapButton* CreateManualMovementButton( wxWindow* pParent, wxBitmap& kIcon, const PhrozenMovement eType );

    //icon
    ScalableBitmap m_ParamSeparator ;
    ScalableBitmap m_Dot_c          ;
    ScalableBitmap m_Percent        ;
    ScalableBitmap m_Nozzle_temp    ;
    ScalableBitmap m_Heated_bed_temp;
    ScalableBitmap m_Fan            ;
                                    
    ScalableBitmap m_Speed          ;
    ScalableBitmap m_Speed_Level    ;
                                    
    ScalableBitmap m_Control_xy_up  ;
    ScalableBitmap m_Control_xy_down;
    ScalableBitmap m_Control_xy_left;
    ScalableBitmap m_Control_xy_right;
    ScalableBitmap m_Control_xy_home;
    ScalableBitmap m_Control_xy_title;
                                    
    ScalableBitmap m_Control_z_title;
    ScalableBitmap m_Control_z_nozzle;

    //AMS
    PhrozenFilamentControl* m_pFilamentControlPanel{nullptr};

    //Temperature
    std::unique_ptr< wxStaticText > m_spTemp_nozzle;
    std::unique_ptr< wxStaticText > m_spTemp_heatedBed;
    std::unique_ptr< wxSpinCtrl > m_spTemp_nozzle_ctrl;
    std::unique_ptr< wxSpinCtrl > m_spTemp_heatedBed_ctrl;
    int m_nNozzle_temperature = 0;
    int m_nHeatedBed_temperature = 0;

    //Cooling
    std::unique_ptr< wxStaticText > m_spCooling_auxiliary;
    std::unique_ptr< wxStaticText > m_spCooling_part;
    std::unique_ptr< wxStaticText > m_spCooling_shield;
    std::unique_ptr< wxSpinCtrl > m_spCooling_auxiliary_ctrl;
    std::unique_ptr< wxSpinCtrl > m_spCooling_part_ctrl;
    std::unique_ptr< wxSpinCtrl > m_spCooling_shield_ctrl;
    int m_nCooling_auxiliary = 0;
    int m_nCooling_part = 0;
    int m_nCooling_shield = 0;

    //Speed
    std::unordered_map< PhrozenPrintSpeed, wxRadioButton* > m_kPrintSpeedButtons;

    //nozzle movement range
    std::unordered_map< PhrozenNozzleMoveRange, wxToggleButton* > m_kNozzleMovementRangeButtons;

    //bed(z-offet) movement ragne
    std::unordered_map< PhrozenPrintNozzleOffsetRange, wxToggleButton* > m_kNozzleOffsetRangeButtons;

    // manual movement
    std::unordered_map< PhrozenMovement, wxBitmapButton* > m_kManualMovementButtons;
    
#pragma endregion


    // control panel
    void update_nozzle_current_temp( int nTemp );
    void update_nozzle_target_temp( int nTemp );

    void update_bed_current_temp( int nTemp );
    void update_bed_target_temp( int nTemp );

    void update_cooling_auxiliary_current_power( int nPower );
    void update_cooling_auxiliary_target_power( int nPower );

    void update_cooling_part_current_power( int nPower );
    void update_cooling_part_target_power( int nPower );

    void update_cooling_shield_current_power( int nPower );
    void update_cooling_shield_target_power( int nPower );

    void update_print_speed_level( PhrozenPrintSpeed eLevel );
    PhrozenPrintSpeed print_speed_percent_to_enum( float fPercentage );
    float print_speed_enum_to_percent( PhrozenPrintSpeed eLevel );

    float get_selected_nozzle_movement_range();
    float get_selected_nozzle_offset_range();



};

class PhrozenStatusPanel : public PhrozenStatusBasePanel
{
private:
    friend class PhrozenMonitorPanel;

protected:
    std::shared_ptr<SliceInfoPopup> m_slice_info_popup;
    std::shared_ptr<ImageTransientPopup> m_image_popup;
    std::shared_ptr<CameraPopup> m_camera_popup;
    std::set<int> rated_model_id;
    PrinterPartsDialog*  print_parts_dlg { nullptr };
    PrintOptionsDialog*  print_options_dlg { nullptr };
    CalibrationDialog*   calibration_dlg {nullptr};

    PrintErrorDialog* m_print_error_dlg = nullptr;
    SecondaryCheckDialog* m_print_error_dlg_no_action = nullptr;
    SecondaryCheckDialog* abort_dlg = nullptr;
    SecondaryCheckDialog* con_load_dlg = nullptr;
    SecondaryCheckDialog* ctrl_e_hint_dlg = nullptr;
    SecondaryCheckDialog* sdcard_hint_dlg = nullptr;
     
    FanControlPopup* m_fan_control_popup{nullptr};

    wxString     m_request_url;
    bool         m_start_loading_thumbnail = false;
    bool         m_load_sdcard_thumbnail = false;
    int          m_last_sdcard    = -1;
    int          m_last_recording = -1;
    int          m_last_timelapse = -1;
    int          m_last_extrusion = -1;
    int          m_last_vcamera   = -1;
    int          m_model_mall_request_count = 0;
    json         m_rating_result;

    wxWebRequest web_request;

    bool cham_temp_input   = false;
    bool request_model_info_flag = false;
    int speed_lvl = 1; // 0 - 3
    int speed_lvl_timeout {0};
    boost::posix_time::ptime speed_dismiss_time;
    bool m_showing_speed_popup = false;
    bool m_show_mode_changed = false;
    std::map<wxString, wxImage> img_list; // key: url, value: wxBitmap png Image
    std::map<std::string, std::string> m_print_connect_types;
    std::vector<Button *>       m_buttons;
    int last_status;
    ScoreData *m_score_data;
    wxBitmap* calib_bitmap = nullptr;
    CalibMode m_calib_mode;
    CalibrationMethod m_calib_method;
    int cali_stage;
    PrintingTaskType m_current_print_mode = PrintingTaskType::NOT_CLEAR;

    void init_scaled_buttons();
    void create_tasklist_info();
    void show_task_list_info(bool show = true);
    void update_tasklist_info();

    void on_market_scoring(wxCommandEvent &event);
    void on_market_retry(wxCommandEvent &event);
    void on_subtask_pause_resume(wxCommandEvent &event);
    void on_subtask_abort(wxCommandEvent &event);
    void on_print_error_clean(wxCommandEvent &event);
    void show_error_message(
        MachineObject *obj, bool is_exist, wxString msg, std::string print_error_str = "", wxString image_url = "", std::vector<int> used_button = std::vector<int>());
    void error_info_reset();
    void show_recenter_dialog();

    /* axis control */
    bool check_axis_z_at_home(MachineObject* obj);
    void on_axis_ctrl_xy(wxCommandEvent &event);
    void on_axis_ctrl_z_up_10(wxCommandEvent &event);
    void on_axis_ctrl_z_up_1(wxCommandEvent &event);
    void on_axis_ctrl_z_down_1(wxCommandEvent &event);
    void on_axis_ctrl_z_down_10(wxCommandEvent &event);
    void on_axis_ctrl_e_up_10(wxCommandEvent &event);
    void on_axis_ctrl_e_down_10(wxCommandEvent &event);
    void axis_ctrl_e_hint(bool up_down);

	void on_start_unload(wxCommandEvent &event);

    /* extruder apis */
    void on_ams_guide(wxCommandEvent &event);
    void on_print_error_done(wxCommandEvent& event);

    void on_fan_changed(wxCommandEvent& event);
    void on_cham_temp_kill_focus(wxFocusEvent& event);
    void on_cham_temp_set_focus(wxFocusEvent& event);
    void on_switch_speed(wxCommandEvent& event);
    void on_lamp_switch(wxCommandEvent &event);
    void on_printing_fan_switch(wxCommandEvent &event);
    void on_nozzle_fan_switch(wxCommandEvent &event);
    void on_thumbnail_enter(wxMouseEvent &event);
    void on_thumbnail_leave(wxMouseEvent &event);
    void refresh_thumbnail_webrequest(wxMouseEvent& event);
    void on_switch_vcamera(wxMouseEvent &event);
    void on_camera_enter(wxMouseEvent &event);
    void on_camera_leave(wxMouseEvent& event);
    void on_auto_leveling(wxCommandEvent &event);
    void on_xyz_abs(wxCommandEvent &event);

    /* calibration */
    void on_start_calibration(wxCommandEvent &event);


    /* update apis */
    void update(MachineObject* obj);
    void UpdateWebCameraView(MachineObject* obj);
    void show_printing_status(bool ctrl_area = true, bool temp_area = true);
    void update_left_time(int mc_left_time);
    void update_basic_print_data(bool def = false);
    void update_model_info();
    void update_subtask(MachineObject* obj);
    void update_cloud_subtask(MachineObject *obj);
    void update_sdcard_subtask(MachineObject *obj);
    void update_temp_ctrl(MachineObject *obj);
    void update_print_speed_ctrl(MachineObject *obj);
    void update_fan_cooling_speed_ctrl(MachineObject *obj);
    void update_misc_ctrl(MachineObject *obj);
    void update_ams(MachineObject* obj);
    void update_extruder_status(MachineObject* obj);
    void update_cali(MachineObject* obj);
    void update_calib_bitmap();

    void reset_printing_values();
    void on_webrequest_state(wxWebRequestEvent &evt);
    bool is_task_changed(MachineObject* obj);

    /* camera */
    void update_camera_state(MachineObject* obj);
    bool show_vcamera = false;

    void on_update_webcam_ui_timer(wxTimerEvent& event);
    void InitWebCamUiUpdateTimer();

    // ======= phrozen checked done ========== //
    bool bed_temp_input    = false;
    bool nozzle_temp_input = false;
    bool cooling_auxiliary_input   = false;
    bool cooling_part_input   = false;
    bool cooling_shield_input   = false;
    bool print_speed_input    = false;

    int  m_temp_nozzle_timeout{ 0 };
    int  m_temp_bed_timeout {0};
    int  m_cooling_auxiliary_timeout {0};
    int  m_cooling_part_timeout {0};
    int  m_cooling_shield_timeout {0};
    int  m_print_speed_timeout {0};

#pragma region Event_from_ui
    void on_nozzle_temp_kill_focus(wxFocusEvent &event);
    void on_nozzle_temp_set_focus(wxFocusEvent &event);
    void on_set_nozzle_temp();

    void on_bed_temp_kill_focus(wxFocusEvent &event);
    void on_bed_temp_set_focus(wxFocusEvent &event);
    void on_set_bed_temp();
    
    void on_cooling_auxiliary_kill_focus(wxFocusEvent &event);
    void on_cooling_auxiliary_set_focus(wxFocusEvent &event);
    void on_set_cooling_auxiliary();

    void on_cooling_part_kill_focus(wxFocusEvent &event);
    void on_cooling_part_set_focus(wxFocusEvent &event);
    void on_set_cooling_part();

    void on_cooling_shield_kill_focus(wxFocusEvent &event);
    void on_cooling_shield_set_focus(wxFocusEvent &event);
    void on_set_cooling_shield();

    void on_print_speed_changed( PhrozenPrintSpeed eLevel ); 

    void on_nozzle_movement_range_mouse_left_down( wxMouseEvent& event );
    void on_nozzle_offset_range_mouse_left_down( wxMouseEvent& event );
    void on_manual_movement_changed( PhrozenMovement eMoveType );


    void on_set_chamber_temp();// no use maybe future
#pragma endregion

public:
    void update_error_message();

public:
    PhrozenStatusPanel(wxWindow*       parent,
                       wxWindowID      id    = wxID_ANY,
                       const wxPoint & pos   = wxDefaultPosition,
                       const wxSize  & size  = wxDefaultSize,
                       long            style = wxTAB_TRAVERSAL,
                       const wxString &name  = wxEmptyString);
    ~PhrozenStatusPanel();

    enum ThumbnailState {
        PLACE_HOLDER = 0,
        BROKEN_IMG = 1,
        TASK_THUMBNAIL = 2,
        SDCARD_THUMBNAIL = 3,
        STATE_COUNT = 4
    };

    std::string    last_profile_id;
    std::string    last_task_id;

    enum ThumbnailState task_thumbnail_state {ThumbnailState::PLACE_HOLDER};
    std::vector<int> last_stage_list_info;

    bool is_stage_list_info_changed(MachineObject* obj);

    void set_default();
    void show_status(int status);
    void set_hold_count(int& count);

    void rescale_camera_icons();
    void on_sys_color_changed();
    void msw_rescale();
};
}
}
#endif
