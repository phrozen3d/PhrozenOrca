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
#include "../MediaPlayCtrl.h"
#include "../AMSSetting.hpp"
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
class PhrozenCalibrationDlg;

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

enum class PhrozenPrintingTaskType : int32_t {
    PRINGINT,
    CALIBRATION,
    NOT_CLEAR
};



class PhrozenPrintingTaskPanel : public wxPanel
{
public:
    PhrozenPrintingTaskPanel(wxWindow* parent, PhrozenPrintingTaskType type);
    ~PhrozenPrintingTaskPanel();
    void create_panel(wxWindow* parent);
    

private:
    MachineObject*  m_obj;
    ScalableBitmap  m_thumbnail_placeholder;
    wxBitmap        m_thumbnail_bmp_display;
    ScalableBitmap  m_bitmap_use_time;
    ScalableBitmap  m_bitmap_use_weight;
    ScalableBitmap  m_bitmap_background;

    wxPanel *       m_panel_printing_title;
    wxPanel*        m_staticline;
    wxPanel*        m_panel_error_txt;

    wxBoxSizer*     m_printing_sizer;
    wxStaticText *  m_staticText_printing;
    wxStaticText*   m_staticText_subtask_value;
    wxStaticText*   m_staticText_consumption_of_time;
    wxStaticText*   m_staticText_consumption_of_weight;
    wxStaticText*   m_printing_stage_value;
    wxStaticText*   m_staticText_profile_value;
    wxStaticText*   m_staticText_progress_percent;
    wxStaticText*   m_staticText_progress_percent_icon;
    wxStaticText*   m_staticText_progress_left;
    // Orca: show print end time
    wxStaticText * m_staticText_progress_end;
    wxStaticText*   m_staticText_layers;
    wxStaticText *  m_has_rated_prompt;
    wxStaticText *  m_request_failed_info;
    wxStaticBitmap* m_bitmap_thumbnail;
    int             m_plate_index { -1 };
    wxStaticBitmap* m_bitmap_static_use_time;
    wxStaticBitmap* m_bitmap_static_use_weight;
    ScalableButton* m_button_pause_resume;
    ScalableButton* m_button_abort;
    Button*         m_button_market_scoring;
    Button*         m_button_clean;
    Button *                      m_button_market_retry;
    wxPanel *                     m_score_subtask_info;
    wxPanel *                     m_score_staticline;
    wxPanel *                     m_request_failed_panel;
    // score page
    int                           m_star_count;
    std::vector<ScalableButton *> m_score_star;
    bool                          m_star_count_dirty = false;

    ProgressBar*    m_gauge_progress;
    Label* m_error_text;
    PhrozenPrintingTaskType m_type;
    int m_brightness_value{ -1 };

public:
    void init_bitmaps();
    void init_scaled_buttons();
    void error_info_reset();
    void show_error_msg(wxString msg);
    void reset_printing_value();
    void msw_rescale();

public:
    void enable_pause_resume_button(bool enable, std::string type);
    void enable_abort_button(bool enable);
    void update_subtask_name(wxString name);
    void update_stage_value(wxString stage, int val);
    void update_progress_percent(wxString percent, wxString icon);
    void update_left_time(wxString time);
    void update_left_time(int mc_left_time);
    void update_layers_num(bool show, wxString num = wxEmptyString);
    void show_priting_use_info(bool show, wxString time = wxEmptyString, wxString weight = wxEmptyString);
    void show_profile_info(bool show, wxString profile = wxEmptyString);
    void set_thumbnail_img(const wxBitmap& bmp);
    void set_brightness_value(int value) { m_brightness_value = value; }
    void set_plate_index(int plate_idx = -1);
    void market_scoring_show();
    void market_scoring_hide();
    ScalableBitmap get_bitmap_thumbnail_placeholder() {return m_thumbnail_placeholder;};
    
public:
    ScalableButton* get_abort_button() {return m_button_abort;};
    ScalableButton* get_pause_resume_button() {return m_button_pause_resume;};
    Button* get_market_scoring_button() {return m_button_market_scoring;};
    Button * get_market_retry_buttom() { return m_button_market_retry; };
    Button* get_clean_button() {return m_button_clean;};
    wxStaticBitmap* get_bitmap_thumbnail() {return m_bitmap_thumbnail;};
    wxPanel *  get_request_failed_panel() { return m_request_failed_panel; }
    int get_star_count() { return m_star_count; }
    void set_star_count(int star_count);
    std::vector<ScalableButton *> &get_score_star() { return m_score_star; }
    bool get_star_count_dirty() { return m_star_count_dirty; }
    void set_star_count_dirty(bool dirty) { m_star_count_dirty = dirty; }
    void                           set_has_reted_text(bool has_rated);
    void paint(wxPaintEvent&);
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
    
    std::vector<unsigned char> m_kWebCameraImageData;
    wxBitmap m_kCurrentWebCamBitmap;
    std::unique_ptr< wxTimer > m_spWebCam_refresh_timer = nullptr;
    bool IsWebCamRefreshTimerInitialized() { return m_spWebCam_refresh_timer != nullptr; }

#pragma region OrcaOriginalMember
    protected:
    ScalableBitmap m_thumbnail_placeholder;
    ScalableBitmap m_thumbnail_brokenimg;
    ScalableBitmap m_thumbnail_sdcard;
    ScalableBitmap m_bitmap_speed;
    ScalableBitmap m_bitmap_speed_active;
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

    Button* m_pCam_light_switch_button;
    Button* m_pCam_switch_button;

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

    wxStaticBitmap *m_bitmap_static_use_time;
    wxStaticBitmap *m_bitmap_static_use_weight;

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
    ScalableButton *m_button_pause_resume;
    ScalableButton *m_button_abort;
    Button *        m_button_clean;
    wxWebView *     m_custom_camera_view{nullptr};

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

    PhrozenAxisCtrlButton *m_phButton_xy;
    Button *        m_bpButton_z_10;
    Button *        m_bpButton_z_1;
    Button *        m_bpButton_z_down_1;
    Button *        m_bpButton_z_down_10;
    Button *        m_button_unload;
    wxStaticText *  m_staticText_z_tip;
    Button *        m_bpButton_e_10;
    Button *        m_bpButton_e_down_10;
    StaticLine *    m_temp_extruder_line;
    bool            m_show_ams_group{false};
    wxStaticBitmap *m_ams_extruder_img;
    wxStaticBitmap* m_bitmap_extruder_img;
    wxPanel *       m_panel_separator_right;
    wxPanel *       m_panel_separotor_bottom;
    wxBoxSizer *    m_printing_sizer;
    wxPanel*        m_panel_error_txt;
    wxPanel*        m_staticline;
    Label *         m_error_text;
    Button*         m_calibration_btn;

    wxPanel *       m_machine_ctrl_panel;
    PhrozenPrintingTaskPanel *       m_project_task_panel;

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
    void on_ams_unload_single_slot(wxCommandEvent& event);
    void on_ams_load_single_slot(wxCommandEvent& event);
    void on_camera_button_triggered( wxCommandEvent& event );
    virtual void on_lighting_button_triggered( wxCommandEvent& event );
    bool IsWebcamUiEnabled();
    bool IsLightingUiEnabled();
    
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

    //Cooling
    std::unique_ptr< wxStaticText > m_spCooling_auxiliary;
    std::unique_ptr< wxStaticText > m_spCooling_part;
    std::unique_ptr< wxStaticText > m_spCooling_shield;
    std::unique_ptr< wxSpinCtrl > m_spCooling_auxiliary_ctrl;
    std::unique_ptr< wxSpinCtrl > m_spCooling_part_ctrl;
    std::unique_ptr< wxSpinCtrl > m_spCooling_shield_ctrl;

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
    std::shared_ptr<CameraPopup> m_camera_popup;
    PrintOptionsDialog*  print_options_dlg { nullptr };
    PhrozenCalibrationDlg* calibration_dlg {nullptr};

    PrintErrorDialog* m_print_error_dlg = nullptr;
    SecondaryCheckDialog* m_print_error_dlg_no_action = nullptr;
    SecondaryCheckDialog* abort_dlg = nullptr;
    SecondaryCheckDialog* ctrl_e_hint_dlg = nullptr;
    SecondaryCheckDialog* sdcard_hint_dlg = nullptr;
     
    FanControlPopup* m_fan_control_popup{nullptr};

    wxString     m_request_url;
    bool         m_start_loading_thumbnail = false;
    bool         m_load_sdcard_thumbnail = false;
    int          m_last_sdcard    = -1;
    int          m_last_recording = -1;
    int          m_last_timelapse = -1;
    int          m_last_vcamera   = -1;

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
    ScoreData *m_score_data;
    wxBitmap* calib_bitmap = nullptr;
    CalibMode m_calib_mode;
    CalibrationMethod m_calib_method;
    int cali_stage;
    PrintingTaskType m_current_print_mode = PrintingTaskType::NOT_CLEAR;

    void init_scaled_buttons();

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
    void update_left_time(int mc_left_time);
    void update_basic_print_data(bool def = false);
    void update_model_info();
    void update_subtask(MachineObject* obj);
    void update_print_states(MachineObject* obj);
    void update_print_status(MachineObject* obj);
    void update_print_progress(MachineObject* obj);
    void update_print_file(MachineObject* obj);
    void update_print_time(MachineObject* obj);
    void update_print_stage(MachineObject* obj);
    void update_print_filament(MachineObject *obj);
    void update_thumbnail(MachineObject *obj);
    void update_cloud_subtask(MachineObject *obj);
    void update_sdcard_subtask(MachineObject *obj);
    void update_temp_ctrl(MachineObject *obj);
    void update_print_speed_ctrl(MachineObject *obj);
    void update_fan_cooling_speed_ctrl(MachineObject *obj);
    void update_webcam_lighting_status( MachineObject *obj );
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

    /* lighting(LED) */
    void on_lighting_button_triggered( wxCommandEvent& event ) override;

    // ======= phrozen checked done ========== //
    bool bed_temp_input    = false;
    bool nozzle_temp_input = false;
    bool cooling_auxiliary_input   = false;
    bool cooling_part_input   = false;
    bool cooling_shield_input   = false;
    bool print_speed_input    = false;
    bool lighting_state_input = false;

    int  m_temp_nozzle_timeout{ 0 };
    int  m_temp_bed_timeout {0};
    int  m_cooling_auxiliary_timeout {0};
    int  m_cooling_part_timeout {0};
    int  m_cooling_shield_timeout {0};
    int  m_print_speed_timeout {0};
    int  m_lighting_state_timeout {0};

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
