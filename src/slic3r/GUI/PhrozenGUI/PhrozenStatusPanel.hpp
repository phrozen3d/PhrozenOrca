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

class PhrozenStatusBasePanel : public StatusBasePanel
{

public:
    PhrozenStatusBasePanel(wxWindow*       parent,
                         wxWindowID      id    = wxID_ANY,
                         const wxPoint&  pos   = wxDefaultPosition,
                         const wxSize&   size  = wxDefaultSize,
                         long            style = wxTAB_TRAVERSAL,
                         const wxString& name  = wxEmptyString);

    ~PhrozenStatusBasePanel();

    virtual void Initizlize() override;
    virtual wxBoxSizer* create_monitoring_page() override;
    virtual wxBoxSizer* create_machine_control_page(wxWindow* parent) override;
    virtual wxBoxSizer* create_temp_axis_group(wxWindow* parent) override;
    virtual wxBoxSizer* create_temp_control(wxWindow* parent) override;
    virtual wxBoxSizer* create_misc_control(wxWindow* parent) override;
    virtual wxBoxSizer* create_axis_control(wxWindow* parent) override;
    virtual wxBoxSizer* create_bed_control(wxWindow* parent) override;
    virtual wxBoxSizer* create_extruder_control(wxWindow* parent) override;
    virtual void init_bitmaps() override;
    //MachineObject* obj{nullptr};
    
    //wxBoxSizer*    create_monitoring_page();
    //wxBoxSizer*    create_machine_control_page(wxWindow* parent);
    //
    //wxBoxSizer* create_temp_axis_group(wxWindow* parent);
    //wxBoxSizer* create_temp_control(wxWindow* parent);
    //wxBoxSizer* create_misc_control(wxWindow* parent);
    //wxBoxSizer* create_axis_control(wxWindow* parent);
    //wxBoxSizer* create_bed_control(wxWindow* parent);
    //wxBoxSizer* create_extruder_control(wxWindow* parent);
    //
    //void        reset_temp_misc_control();
    //int         before_error_code = 0;
    //int         skip_print_error  = 0;
    //wxBoxSizer* create_ams_group(wxWindow* parent);
    //wxBoxSizer* create_settings_group(wxWindow* parent);
    //
    //void           show_ams_group(bool show = true);
    //MediaPlayCtrl* get_media_play_ctrl() { return m_media_play_ctrl; };
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
    AMSSetting *m_ams_setting_dlg{nullptr};
    PrinterPartsDialog*  print_parts_dlg { nullptr };
    PrintOptionsDialog*  print_options_dlg { nullptr };
    CalibrationDialog*   calibration_dlg {nullptr};
    AMSMaterialsSetting *m_filament_setting_dlg{nullptr};

    PrintErrorDialog* m_print_error_dlg = nullptr;
    SecondaryCheckDialog* m_print_error_dlg_no_action = nullptr;
    SecondaryCheckDialog* abort_dlg = nullptr;
    SecondaryCheckDialog* con_load_dlg = nullptr;
    SecondaryCheckDialog* ctrl_e_hint_dlg = nullptr;
    SecondaryCheckDialog* sdcard_hint_dlg = nullptr;

    FanControlPopup* m_fan_control_popup{nullptr};

    ExtrusionCalibration *m_extrusion_cali_dlg{nullptr};

    wxString     m_request_url;
    bool         m_start_loading_thumbnail = false;
    bool         m_load_sdcard_thumbnail = false;
    int          m_last_sdcard    = -1;
    int          m_last_recording = -1;
    int          m_last_timelapse = -1;
    int          m_last_extrusion = -1;
    int          m_last_vcamera   = -1;
    int          m_model_mall_request_count = 0;
    bool         m_is_load_with_temp = false;
    json         m_rating_result;

    wxWebRequest web_request;
    bool bed_temp_input    = false;
    bool nozzle_temp_input = false;
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
    /* temp control */
    void on_bed_temp_kill_focus(wxFocusEvent &event);
    void on_bed_temp_set_focus(wxFocusEvent &event);
    void on_set_bed_temp();
    void on_nozzle_temp_kill_focus(wxFocusEvent &event);
    void on_nozzle_temp_set_focus(wxFocusEvent &event);
    void on_set_nozzle_temp();
    void on_set_chamber_temp();

    /* extruder apis */
    void on_ams_load(SimpleEvent &event);
    void update_filament_step();
    void on_ams_load_curr();
    void on_ams_load_vams(wxCommandEvent& event);
    void on_ams_unload(SimpleEvent &event);
    void on_ams_filament_backup(SimpleEvent& event);
    void on_ams_setting_click(SimpleEvent& event);
    void on_filament_edit(wxCommandEvent &event);
    void on_ext_spool_edit(wxCommandEvent &event);
    void on_filament_extrusion_cali(wxCommandEvent &event);
    void on_ams_refresh_rfid(wxCommandEvent &event);
    void on_ams_selected(wxCommandEvent &event);
    void on_ams_guide(wxCommandEvent &event);
    void on_ams_retry(wxCommandEvent &event);
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

    void on_show_parts_options(wxCommandEvent& event);
    /* print options */
    void on_show_print_options(wxCommandEvent &event);

    /* calibration */
    void on_start_calibration(wxCommandEvent &event);


    /* update apis */
    void update(MachineObject* obj);
    void show_printing_status(bool ctrl_area = true, bool temp_area = true);
    void update_left_time(int mc_left_time);
    void update_basic_print_data(bool def = false);
    void update_model_info();
    void update_subtask(MachineObject* obj);
    void update_cloud_subtask(MachineObject *obj);
    void update_sdcard_subtask(MachineObject *obj);
    void update_temp_ctrl(MachineObject *obj);
    void update_misc_ctrl(MachineObject *obj);
    void update_ams(MachineObject* obj);
    void update_ams_insert_material(MachineObject* obj);
    void update_extruder_status(MachineObject* obj);
    void update_ams_control_state(bool is_curr_tray_selected);
    void update_cali(MachineObject* obj);
    void update_calib_bitmap();

    void reset_printing_values();
    void on_webrequest_state(wxWebRequestEvent &evt);
    bool is_task_changed(MachineObject* obj);

    /* camera */
    void update_camera_state(MachineObject* obj);
    bool show_vcamera = false;

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

    BBLSubTask *   last_subtask{nullptr};
    std::string    last_profile_id;
    std::string    last_task_id;
    long           last_tray_exist_bits { -1 };
    long           last_ams_exist_bits { -1 };
    long           last_tray_is_bbl_bits{ -1 };
    long           last_read_done_bits{ -1 };
    long           last_reading_bits { -1 };
    long           last_ams_version { -1 };
    int            last_cali_version{-1};

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
