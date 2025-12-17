#include "PhrozenStatusPanel.hpp"

#include "../I18N.hpp"
#include "../Widgets/Label.hpp"
#include "../Widgets/Button.hpp"
#include "../Widgets/StepCtrl.hpp"
#include "PhrozenSideTools.hpp"
#include "PhrozenCalibrationDlg.hpp"
#include "../Widgets/WebView.hpp"
#include "../Utils/Phrozen/PhrozenNetworkAgent.hpp"

#include "../BitmapCache.hpp"
#include "../GUI_App.hpp"
#include "../MainFrame.hpp"

#include "../MsgDialog.hpp"
#include "slic3r/Utils/Http.hpp"
#include "libslic3r/Thread.hpp"

#include "../RecenterDialog.hpp"
#include "CalibUtils.hpp"
#include <slic3r/GUI/Widgets/ProgressDialog.hpp>
#include <wx/display.h>
#include <wx/mstream.h>
#include <wx/sstream.h>
#include <wx/zstream.h>
#include "PhrozenMonitorController.hpp"
#include "PhrozenDeviceManager.hpp"
#include "PhrozenFilamentControl.hpp"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cstring>

#define HideOriginUiWidget 0
//對應MonitorControl::ReceiveWebCameraView 的更新頻率，這裡設高一點點讓他不容易衝突
#define REFRESH_WEBCAM_UI_INTERVAL 15 

namespace Slic3r { namespace GUI {

#pragma region PanelParameter
static std::vector<std::string> phrozen_message_containing_retry{
    "0701 8004",
    "0701 8005",
    "0701 8006",
    "0701 8006",
    "0701 8007",
    "0700 8012",
    "0701 8012",
    "0702 8012",
    "0703 8012",
    "07FF 8003",
    "07FF 8004",
    "07FF 8005",
    "07FF 8006",
    "07FF 8007",
    "07FF 8010",
    "07FF 8011",
    "07FF 8012",
    "07FF 8013",
    "12FF 8007",
    "1200 8006"
};

static std::vector<std::string> phrozen_message_containing_done{
    "07FF 8007",
    "12FF 8007"
};

static std::vector<std::string> phrozen_message_containing_resume{
    "0300 8013"
};

#define TEMP_THRESHOLD_VAL 2
#define TEMP_THRESHOLD_ALLOW_E_CTRL 170.0f

/* const strings */
static const wxString PHROZEN_NA_STR         = _L("N/A");
static const wxString PHROZEN_TEMP_BLANK_STR = wxString("_");
static const wxFont   PHROZEN_SWITCH_FONT    = Label::Body_10;

/* const values */
static const int phrozen_bed_temp_range[2]    = {20, 120};
static const int phrozen_nozzle_temp_range[2] = {20, 300};
static const int phrozen_nozzle_chamber_range[2] = {20, 60};

/* colors */
static const wxColour PHROZEN_STATUS_PANEL_BG = wxColour(238, 238, 238);
static const wxColour PHROZEN_STATUS_TITLE_BG = wxColour(248, 248, 248);
static const wxColour PHROZEN_STATIC_BOX_LINE_COL = wxColour(238, 238, 238);

static const wxColour PHROZEN_BUTTON_NORMAL1_COL = wxColour(238, 238, 238);
static const wxColour PHROZEN_BUTTON_NORMAL2_COL = wxColour(206, 206, 206);
static const wxColour PHROZEN_BUTTON_PRESS_COL   = wxColour(172, 172, 172);
static const wxColour PHROZEN_BUTTON_HOVER_COL   = wxColour(255, 124, 63);

static const wxColour PHROZEN_DISCONNECT_TEXT_COL = wxColour(171, 172, 172);
static const wxColour PHROZEN_NORMAL_TEXT_COL     = wxColour(48, 58, 60);
static const wxColour PHROZEN_NORMAL_FAN_TEXT_COL = wxColour(107, 107, 107);
static const wxColour PHROZEN_WARNING_INFO_BG_COL = wxColour(255, 111, 0);
static const wxColour PHROZEN_STAGE_TEXT_COL      = wxColour(255, 124, 63);

static const wxColour PHROZEN_GROUP_STATIC_LINE_COL = wxColour(206, 206, 206);

/* font and foreground colors */
static const wxFont PHROZEN_PAGE_TITLE_FONT = Label::Body_14;
//static const wxFont GROUP_TITLE_FONT = Label::sysFont(17);

static wxColour PHROZEN_PAGE_TITLE_FONT_COL = wxColour(107, 107, 107);
static wxColour PHROZEN_GROUP_TITLE_FONT_COL = wxColour(172, 172, 172);
static wxColour PHROZEN_TEXT_LIGHT_FONT_COL  = wxColour(107, 107, 107);

/* size */
#define PAGE_TITLE_HEIGHT FromDIP(36)
#define PAGE_TITLE_TEXT_WIDTH FromDIP(200)
#define PAGE_TITLE_LEFT_MARGIN FromDIP(17)
#define GROUP_TITLE_LEFT_MARGIN FromDIP(15)
#define GROUP_TITLE_LINE_MARGIN FromDIP(11)
#define GROUP_TITLE_RIGHT_MARGIN FromDIP(15)

#define NORMAL_SPACING FromDIP(5)
#define PAGE_SPACING FromDIP(10)
#define PAGE_MIN_WIDTH FromDIP(574)
#define PROGRESSBAR_HEIGHT FromDIP(8)

#define SWITCH_BUTTON_SIZE (wxSize(FromDIP(40), -1))
#define TASK_THUMBNAIL_SIZE (wxSize(FromDIP(120), FromDIP(120)))
#define TASK_BUTTON_SIZE (wxSize(FromDIP(48), FromDIP(24)))
#define TASK_BUTTON_SIZE2 (wxSize(-1, FromDIP(24)))
#define Z_BUTTON_SIZE (wxSize(FromDIP(52), FromDIP(52)))
#define MISC_BUTTON_PANEL_SIZE (wxSize(FromDIP(136), FromDIP(55)))
#define MISC_BUTTON_1FAN_SIZE (wxSize(FromDIP(132), FromDIP(51)))
#define MISC_BUTTON_2FAN_SIZE (wxSize(FromDIP(66), FromDIP(51)))
#define MISC_BUTTON_3FAN_SIZE (wxSize(FromDIP(44), FromDIP(51)))
#define TEMP_CTRL_MIN_SIZE (wxSize(FromDIP(122), FromDIP(52)))
#define AXIS_MIN_SIZE (wxSize(FromDIP(220), FromDIP(220)))
#define EXTRUDER_IMAGE_SIZE (wxSize(FromDIP(48), FromDIP(76)))
#pragma endregion

#pragma region PhrozenPrintingTaskPanel


PhrozenPrintingTaskPanel::PhrozenPrintingTaskPanel(wxWindow* parent, PhrozenPrintingTaskType type)
    : wxPanel(parent, wxID_ANY,wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL)
{
    m_type = type;
    create_panel(this);
    SetBackgroundColour(*wxWHITE);
    m_bitmap_background = ScalableBitmap(this, "thumbnail_grid", m_bitmap_thumbnail->GetSize().y);

    m_bitmap_thumbnail->Bind(wxEVT_PAINT, &PhrozenPrintingTaskPanel::paint, this);
}

PhrozenPrintingTaskPanel::~PhrozenPrintingTaskPanel()
{
}

void PhrozenPrintingTaskPanel::create_panel(wxWindow* parent)
{
    wxBoxSizer *sizer                 = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *bSizer_printing_title = new wxBoxSizer(wxHORIZONTAL);

    m_panel_printing_title = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, PAGE_TITLE_HEIGHT), wxTAB_TRAVERSAL);
    m_panel_printing_title->SetBackgroundColour(PHROZEN_STATUS_TITLE_BG);

    m_staticText_printing = new wxStaticText(m_panel_printing_title, wxID_ANY ,_L("Printing Progress"));
    m_staticText_printing->Wrap(-1);
    //m_staticText_printing->SetFont(PAGE_TITLE_FONT);
    m_staticText_printing->SetForegroundColour(PHROZEN_PAGE_TITLE_FONT_COL);

    bSizer_printing_title->Add(m_staticText_printing, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, PAGE_TITLE_LEFT_MARGIN);
    bSizer_printing_title->Add(0, 0, 1, wxEXPAND, 0);

    m_panel_printing_title->SetSizer(bSizer_printing_title);
    m_panel_printing_title->Layout();
    bSizer_printing_title->Fit(m_panel_printing_title);

    m_bitmap_thumbnail = new wxStaticBitmap(parent, wxID_ANY, m_thumbnail_placeholder.bmp(), wxDefaultPosition, TASK_THUMBNAIL_SIZE, 0);
    m_bitmap_thumbnail->SetMaxSize(TASK_THUMBNAIL_SIZE);
    m_bitmap_thumbnail->SetMinSize(TASK_THUMBNAIL_SIZE);

    wxBoxSizer *bSizer_subtask_info = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *bSizer_task_name = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *bSizer_task_name_hor = new wxBoxSizer(wxHORIZONTAL);
    wxPanel*    task_name_panel      = new wxPanel(parent);

    m_staticText_subtask_value = new wxStaticText(task_name_panel, wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT | wxST_ELLIPSIZE_END);
    m_staticText_subtask_value->SetMaxSize(wxSize(FromDIP(600), -1));
    m_staticText_subtask_value->Wrap(-1);
    #ifdef __WXOSX_MAC__
    m_staticText_subtask_value->SetFont(::Label::Body_13);
    #else
    m_staticText_subtask_value->SetFont(wxFont(13, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC")));
    #endif
    m_staticText_subtask_value->SetForegroundColour(wxColour(44, 44, 46));

    m_bitmap_static_use_time = new wxStaticBitmap(task_name_panel, wxID_ANY, m_bitmap_use_time.bmp(), wxDefaultPosition, wxSize(FromDIP(16), FromDIP(16)));

    m_staticText_consumption_of_time = new wxStaticText(task_name_panel, wxID_ANY, "0m", wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_consumption_of_time->SetFont(::Label::Body_12);
    m_staticText_consumption_of_time->SetForegroundColour(wxColour(0x68, 0x68, 0x68));
    m_staticText_consumption_of_time->Wrap(-1);


    m_bitmap_static_use_weight = new wxStaticBitmap(task_name_panel, wxID_ANY, m_bitmap_use_weight.bmp(), wxDefaultPosition, wxSize(FromDIP(16), FromDIP(16)));


    m_staticText_consumption_of_weight = new wxStaticText(task_name_panel, wxID_ANY, "0g", wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_consumption_of_weight->SetFont(::Label::Body_12);
    m_staticText_consumption_of_weight->SetForegroundColour(wxColour(0x68, 0x68, 0x68));
    m_staticText_consumption_of_weight->Wrap(-1);

    bSizer_task_name_hor->Add(m_staticText_subtask_value, 1, wxALL | wxEXPAND, 0);
    bSizer_task_name_hor->Add(m_bitmap_static_use_time, 0, wxALIGN_CENTER_VERTICAL, 0);
    bSizer_task_name_hor->Add(m_staticText_consumption_of_time, 0, wxALIGN_CENTER_VERTICAL|wxLEFT, FromDIP(3));
    bSizer_task_name_hor->Add(0, 0, 0, wxLEFT, FromDIP(10));
    bSizer_task_name_hor->Add(m_bitmap_static_use_weight, 0, wxALIGN_CENTER_VERTICAL, 0);
    bSizer_task_name_hor->Add(m_staticText_consumption_of_weight, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(3));
    bSizer_task_name_hor->Add(0, 0, 0, wxRIGHT, FromDIP(10));


    task_name_panel->SetSizer(bSizer_task_name_hor);
    task_name_panel->Layout();
    task_name_panel->Fit();

    bSizer_task_name->Add(task_name_panel, 0, wxEXPAND, FromDIP(5));


   /* wxFlexGridSizer *fgSizer_task = new wxFlexGridSizer(2, 2, 0, 0);
     fgSizer_task->AddGrowableCol(0);
     fgSizer_task->SetFlexibleDirection(wxVERTICAL);
     fgSizer_task->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);*/

    m_printing_stage_value = new wxStaticText(parent, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT | wxST_ELLIPSIZE_END);
    m_printing_stage_value->Wrap(-1);
    m_printing_stage_value->SetMaxSize(wxSize(FromDIP(800),-1));
    #ifdef __WXOSX_MAC__
    m_printing_stage_value->SetFont(::Label::Body_11);
    #else
    m_printing_stage_value->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC")));
    #endif

    m_printing_stage_value->SetForegroundColour(PHROZEN_STAGE_TEXT_COL);


    m_staticText_profile_value = new wxStaticText(parent, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT | wxST_ELLIPSIZE_END);
    m_staticText_profile_value->Wrap(-1);
#ifdef __WXOSX_MAC__
    m_staticText_profile_value->SetFont(::Label::Body_11);
#else
    m_staticText_profile_value->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC")));
#endif

    m_staticText_profile_value->SetForegroundColour(0x6B6B6B);


    auto m_panel_progress = new wxPanel(parent, wxID_ANY);
    m_panel_progress->SetBackgroundColour(*wxWHITE);
    auto m_sizer_progressbar = new wxBoxSizer(wxHORIZONTAL);
    m_gauge_progress = new ProgressBar(m_panel_progress, wxID_ANY, 100, wxDefaultPosition, wxDefaultSize);
    m_gauge_progress->SetValue(0);
    m_gauge_progress->SetHeight(PROGRESSBAR_HEIGHT);
    m_gauge_progress->SetMaxSize(wxSize(FromDIP(600), -1));
    m_panel_progress->SetSizer(m_sizer_progressbar);
    m_panel_progress->Layout();
    m_panel_progress->SetSize(wxSize(-1, FromDIP(24)));
    m_panel_progress->SetMaxSize(wxSize(-1, FromDIP(24)));

    wxBoxSizer *bSizer_task_btn = new wxBoxSizer(wxHORIZONTAL);

    bSizer_task_btn->Add(FromDIP(10), 0, 0);

    m_button_pause_resume = new ScalableButton(m_panel_progress, wxID_ANY, "print_control_pause", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER,true);

    m_button_pause_resume->Bind(wxEVT_ENTER_WINDOW, [this](auto &e) {
        if (m_button_pause_resume->GetToolTipText() == _L("Pause")) {
            m_button_pause_resume->SetBitmap_("print_control_pause_hover");
        }

        if (m_button_pause_resume->GetToolTipText() == _L("Resume")) {
            m_button_pause_resume->SetBitmap_("print_control_resume_hover");
        }
    });

    m_button_pause_resume->Bind(wxEVT_LEAVE_WINDOW, [this](auto &e) {
        auto        buf = m_button_pause_resume->GetClientData();
        if (m_button_pause_resume->GetToolTipText() == _L("Pause")) {
            m_button_pause_resume->SetBitmap_("print_control_pause");
        }

        if (m_button_pause_resume->GetToolTipText() == _L("Resume")) {
            m_button_pause_resume->SetBitmap_("print_control_resume");
        }
    });

    m_button_abort = new ScalableButton(m_panel_progress, wxID_ANY, "print_control_stop", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, true);
    m_button_abort->SetToolTip(_L("Stop"));

    m_button_abort->Bind(wxEVT_ENTER_WINDOW, [this](auto &e) {
        m_button_abort->SetBitmap_("print_control_stop_hover");
    });

    m_button_abort->Bind(wxEVT_LEAVE_WINDOW, [this](auto &e) {
        m_button_abort->SetBitmap_("print_control_stop"); }
    );

    m_sizer_progressbar->Add(m_gauge_progress, 1, wxALIGN_CENTER_VERTICAL, 0);
    m_sizer_progressbar->Add(0, 0, 0, wxEXPAND|wxLEFT, FromDIP(18));
    m_sizer_progressbar->Add(m_button_pause_resume, 0, wxALL, FromDIP(5));
    m_sizer_progressbar->Add(0, 0, 0, wxEXPAND|wxLEFT, FromDIP(18));
    m_sizer_progressbar->Add(m_button_abort, 0, wxALL, FromDIP(5));

    wxBoxSizer *bSizer_buttons = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *bSizer_text = new wxBoxSizer(wxHORIZONTAL);
    wxPanel* penel_bottons = new wxPanel(parent);
    wxPanel* penel_text = new wxPanel(penel_bottons);

    penel_text->SetBackgroundColour(*wxWHITE);
    penel_bottons->SetBackgroundColour(*wxWHITE);

    wxBoxSizer *sizer_percent = new wxBoxSizer(wxVERTICAL);
    sizer_percent->Add(0, 0, 1, wxEXPAND, 0);

    wxBoxSizer *sizer_percent_icon  = new wxBoxSizer(wxVERTICAL);
    sizer_percent_icon->Add(0, 0, 1, wxEXPAND, 0);


    m_staticText_progress_percent = new wxStaticText(penel_text, wxID_ANY, "0", wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_progress_percent->SetFont(::Label::Head_18);
    m_staticText_progress_percent->SetMaxSize(wxSize(-1, FromDIP(20)));
    m_staticText_progress_percent->SetForegroundColour(wxColour(255, 124, 63));

    m_staticText_progress_percent_icon = new wxStaticText(penel_text, wxID_ANY, "%", wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_progress_percent_icon->SetFont(::Label::Body_11);
    m_staticText_progress_percent_icon->SetMaxSize(wxSize(-1, FromDIP(13)));
    m_staticText_progress_percent_icon->SetForegroundColour(wxColour(255, 124, 63));

    sizer_percent->Add(m_staticText_progress_percent, 0, 0, 0);

    #ifdef __WXOSX_MAC__
    sizer_percent_icon->Add(m_staticText_progress_percent_icon, 0, wxBOTTOM, FromDIP(2));
    #else
    sizer_percent_icon->Add(m_staticText_progress_percent_icon, 0, 0, 0);
    #endif


    m_staticText_progress_left = new wxStaticText(penel_text, wxID_ANY, L("N/A"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_progress_left->Wrap(-1);
    m_staticText_progress_left->SetFont(wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC")));
    m_staticText_progress_left->SetForegroundColour(wxColour(146, 146, 146));

    // Orca: display the end time of the print
    m_staticText_progress_end = new wxStaticText(penel_text, wxID_ANY, L("N/A"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_progress_end->Wrap(-1);
    m_staticText_progress_end->SetFont(
        wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC")));
    m_staticText_progress_end->SetForegroundColour(wxColour(146, 146, 146));

    //fgSizer_task->Add(bSizer_buttons, 0, wxEXPAND, 0);
    //fgSizer_task->Add(0, 0, 0, wxEXPAND, FromDIP(5));

    wxPanel* panel_button_block = new wxPanel(penel_bottons, wxID_ANY);
    panel_button_block->SetMinSize(wxSize(TASK_BUTTON_SIZE.x * 2 + FromDIP(5) * 4, -1));
    panel_button_block->SetMinSize(wxSize(TASK_BUTTON_SIZE.x * 2 + FromDIP(5) * 4, -1));
    panel_button_block->SetSize(wxSize(TASK_BUTTON_SIZE.x * 2 + FromDIP(5) * 2, -1));
    panel_button_block->SetBackgroundColour(*wxWHITE);

    m_staticText_layers = new wxStaticText(penel_text, wxID_ANY, _L("Layer: N/A"));
    m_staticText_layers->SetFont(wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC")));
    m_staticText_layers->SetForegroundColour(wxColour(146, 146, 146));
    m_staticText_layers->Hide();

    //bSizer_text->Add(m_staticText_progress_percent, 0,  wxALL, 0);
    bSizer_text->Add(sizer_percent, 0, wxEXPAND, 0);
    bSizer_text->Add(sizer_percent_icon, 0, wxEXPAND, 0);
    bSizer_text->Add(0, 0, 1, wxEXPAND, 0);
    bSizer_text->Add(m_staticText_layers, 0, wxALIGN_CENTER | wxALL, 0);
    bSizer_text->Add(0, 0, 0, wxLEFT, FromDIP(20));
    bSizer_text->Add(m_staticText_progress_left, 0, wxALIGN_CENTER | wxALL, 0);
    // Orca: display the end time of the print
    bSizer_text->Add(0, 0, 0, wxLEFT, FromDIP(8));
    bSizer_text->Add(m_staticText_progress_end, 0, wxALIGN_CENTER | wxALL, 0);

    penel_text->SetMaxSize(wxSize(FromDIP(600), -1));
    penel_text->SetSizer(bSizer_text);
    penel_text->Layout();

    bSizer_buttons->Add(penel_text, 1, wxEXPAND | wxALL, 0);
    bSizer_buttons->Add(panel_button_block, 0, wxALIGN_CENTER | wxALL, 0);

    penel_bottons->SetSizer(bSizer_buttons);
    penel_bottons->Layout();

    bSizer_subtask_info->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(14));
    bSizer_subtask_info->Add(bSizer_task_name, 0, wxEXPAND|wxRIGHT, FromDIP(18));
    bSizer_subtask_info->Add(m_staticText_profile_value, 0, wxEXPAND | wxTOP, FromDIP(5));
    bSizer_subtask_info->Add(m_printing_stage_value, 0, wxEXPAND | wxTOP, FromDIP(5));
    bSizer_subtask_info->Add(penel_bottons, 0, wxEXPAND | wxTOP, FromDIP(10));
    bSizer_subtask_info->Add(m_panel_progress, 0, wxEXPAND|wxRIGHT, FromDIP(25));


    m_printing_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_printing_sizer->SetMinSize(wxSize(PAGE_MIN_WIDTH, -1));
    m_printing_sizer->Add(m_bitmap_thumbnail, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxLEFT, FromDIP(12));
    m_printing_sizer->Add(FromDIP(8), 0, 0, wxEXPAND, 0);
    m_printing_sizer->Add(bSizer_subtask_info, 1, wxALL | wxEXPAND, 0);


    m_staticline = new wxPanel( parent, wxID_ANY);
    m_staticline->SetBackgroundColour(wxColour(238,238,238));
    m_staticline->Layout();
    m_staticline->Hide();

    m_panel_error_txt = new wxPanel(parent, wxID_ANY);
    m_panel_error_txt->SetBackgroundColour(*wxWHITE);

    wxBoxSizer *static_text_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_error_text = new Label(m_panel_error_txt, "", LB_AUTO_WRAP);
    m_error_text->SetForegroundColour(wxColour(255, 0, 0));
    static_text_sizer->Add(m_error_text, 1, wxEXPAND | wxLEFT, FromDIP(17));

    m_button_clean = new Button(m_panel_error_txt, _L("Clear"));
    StateColor clean_bg(std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Disabled), std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
                        std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered), std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Enabled),
                        std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));
    StateColor clean_bd(std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled), std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));
    StateColor clean_text(std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled), std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));


    m_button_clean->SetBackgroundColor(clean_bg);
    m_button_clean->SetBorderColor(clean_bd);
    m_button_clean->SetTextColor(clean_text);
    m_button_clean->SetFont(Label::Body_10);
    m_button_clean->SetMinSize(TASK_BUTTON_SIZE2);

    static_text_sizer->Add( FromDIP(10), 0, 0, 0, 0 );
    static_text_sizer->Add(m_button_clean, 0, wxALIGN_CENTRE_VERTICAL|wxRIGHT, FromDIP(5));

    m_panel_error_txt->SetSizer(static_text_sizer);
    m_panel_error_txt->Hide();

    sizer->Add(m_panel_printing_title, 0, wxEXPAND | wxALL, 0);
    sizer->Add(0, FromDIP(12), 0);
    sizer->Add(m_printing_sizer, 0, wxEXPAND | wxALL, 0);
    sizer->Add(0, 0, 0, wxTOP, FromDIP(15));
    sizer->Add(m_staticline, 0, wxEXPAND | wxALL, FromDIP(10));
    sizer->Add(m_panel_error_txt, 0, wxEXPAND | wxALL, 0);
    sizer->Add(0, FromDIP(12), 0);

    m_score_staticline = new wxPanel(parent, wxID_ANY);
    m_score_staticline->SetBackgroundColour(wxColour(238, 238, 238));
    m_score_staticline->Layout();
    m_score_staticline->Hide();
    sizer->Add(0, 0, 0, wxTOP, FromDIP(15));
    sizer->Add(m_score_staticline, 0, wxEXPAND | wxALL, FromDIP(10));
    m_request_failed_panel    = new wxPanel(parent, wxID_ANY);
    m_request_failed_panel->SetBackgroundColour(*wxWHITE);
    wxBoxSizer *static_request_failed_panel_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_request_failed_info = new wxStaticText(m_request_failed_panel, wxID_ANY, _L("You have completed printing the mall model, \nbut the synchronization of rating information has failed."), wxDefaultPosition, wxDefaultSize, 0);
    m_request_failed_info->Wrap(-1);
    m_request_failed_info->SetForegroundColour(*wxRED);
    m_request_failed_info->SetFont(::Label::Body_10);
    static_request_failed_panel_sizer->Add(m_request_failed_info, 0, wxEXPAND | wxALL, FromDIP(10));
    StateColor btn_bg_green(std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Disabled), std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed),
                            std::pair<wxColour, int>(wxColour(240, 94, 32), StateColor::Hovered), std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));
    StateColor btn_bd_green(std::pair<wxColour, int>(AMS_CONTROL_WHITE_COLOUR, StateColor::Disabled), std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Enabled));
    m_button_market_retry = new Button(m_request_failed_panel, _L("Retry"));
    m_button_market_retry->SetBackgroundColor(btn_bg_green);
    m_button_market_retry->SetBorderColor(btn_bd_green);
    m_button_market_retry->SetTextColor(wxColour("#FFFFFE"));
    m_button_market_retry->SetSize(wxSize(FromDIP(128), FromDIP(26)));
    m_button_market_retry->SetMinSize(wxSize(-1, FromDIP(26)));
    m_button_market_retry->SetCornerRadius(FromDIP(13));
    static_request_failed_panel_sizer->Add(0, 0, 1, wxEXPAND, 0);
    static_request_failed_panel_sizer->Add(m_button_market_retry, 0, wxEXPAND | wxALL, FromDIP(10));
    m_request_failed_panel->SetSizer(static_request_failed_panel_sizer);
    m_request_failed_panel->Hide();
    sizer->Add(m_request_failed_panel, 0, wxEXPAND | wxALL, FromDIP(10));


    m_score_subtask_info = new wxPanel(parent, wxID_ANY);
    m_score_subtask_info->SetBackgroundColour(*wxWHITE);

    wxBoxSizer *  static_score_sizer = new wxBoxSizer(wxVERTICAL);
    wxStaticText *static_score_text  = new wxStaticText(m_score_subtask_info, wxID_ANY, _L("How do you like this printing file?"), wxDefaultPosition, wxDefaultSize, 0);
    static_score_text->Wrap(-1);
    static_score_sizer->Add(static_score_text, 1, wxEXPAND | wxALL, FromDIP(10));
    m_has_rated_prompt = new wxStaticText(m_score_subtask_info, wxID_ANY, _L("(The model has already been rated. Your rating will overwrite the previous rating.)"), wxDefaultPosition, wxDefaultSize, 0);
    m_has_rated_prompt->Wrap(-1);
    m_has_rated_prompt->SetForegroundColour(*wxBLACK);
    m_has_rated_prompt->SetFont(::Label::Body_10);
    m_has_rated_prompt->Hide();

    m_star_count                        = 0;
    wxBoxSizer *static_score_star_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_score_star.resize(5);
    for (int i = 0; i < m_score_star.size(); ++i) {
        m_score_star[i] = new ScalableButton(m_score_subtask_info, wxID_ANY, "score_star_dark", wxEmptyString, wxSize(FromDIP(26), FromDIP(26)), wxDefaultPosition,
                                             wxBU_EXACTFIT | wxNO_BORDER, true, 26);
        m_score_star[i]->Bind(wxEVT_LEFT_DOWN, [this, i](auto &e) {
            for (int j = 0; j < m_score_star.size(); ++j) {
                ScalableBitmap light_star = ScalableBitmap(nullptr, "score_star_light", 26);
                m_score_star[j]->SetBitmap(light_star.bmp());
                if (m_score_star[j] == m_score_star[i]) {
                    m_star_count = j + 1;
                    break;
                }
            }
            for (int k = m_star_count; k < m_score_star.size(); ++k) {
                ScalableBitmap dark_star = ScalableBitmap(nullptr, "score_star_dark", 26);
                m_score_star[k]->SetBitmap(dark_star.bmp());
            }
            m_star_count_dirty = true;
            m_button_market_scoring->Enable(true);
        });
        static_score_star_sizer->Add(m_score_star[i], 0, wxEXPAND | wxLEFT, FromDIP(10));
    }

    m_button_market_scoring = new Button(m_score_subtask_info, _L("Rate"));
    m_button_market_scoring->SetBackgroundColor(btn_bg_green);
    m_button_market_scoring->SetBorderColor(btn_bd_green);
    m_button_market_scoring->SetTextColor(wxColour("#FFFFFE"));
    m_button_market_scoring->SetSize(wxSize(FromDIP(128), FromDIP(26)));
    m_button_market_scoring->SetMinSize(wxSize(-1, FromDIP(26)));
    m_button_market_scoring->SetCornerRadius(FromDIP(13));
    m_button_market_scoring->Enable(false);

    static_score_star_sizer->Add(0, 0, 1, wxEXPAND, 0);
    static_score_star_sizer->Add(m_button_market_scoring, 0, wxEXPAND | wxRIGHT, FromDIP(10));
    static_score_sizer->Add(static_score_star_sizer, 0, wxEXPAND, FromDIP(10));
    static_score_sizer->Add(m_has_rated_prompt, 1, wxEXPAND | wxALL, FromDIP(10));

    m_score_subtask_info->SetSizer(static_score_sizer);
    m_score_subtask_info->Layout();
    m_score_subtask_info->Hide();

    sizer->Add(m_score_subtask_info, 0, wxEXPAND | wxALL, 0);
    sizer->Add(0, FromDIP(12), 0);

    if (m_type == PhrozenPrintingTaskType::CALIBRATION) {
        m_panel_printing_title->Hide();
        m_bitmap_thumbnail->Hide();
        task_name_panel->Hide();
        m_staticText_profile_value->Hide();
    }

    parent->SetSizer(sizer);
    parent->Layout();
    parent->Fit();
}

void PhrozenPrintingTaskPanel::paint(wxPaintEvent&)
{
    wxPaintDC dc(m_bitmap_thumbnail);
    if (wxGetApp().dark_mode()) {
        if (m_brightness_value > 0 && m_brightness_value < SHOW_BACKGROUND_BITMAP_PIXEL_THRESHOLD) {
            dc.DrawBitmap(m_bitmap_background.bmp(), 0, 0);
            dc.SetTextForeground(*wxBLACK);
        }
        else
            dc.SetTextForeground(*wxWHITE);
    }
    else
        dc.SetTextForeground(*wxBLACK);
    dc.DrawBitmap(m_thumbnail_bmp_display, wxPoint(0, 0));
    dc.SetFont(Label::Body_12);
    
    if (m_plate_index >= 0) {
        wxString plate_id_str = wxString::Format("%d", m_plate_index);
        dc.DrawText(plate_id_str, wxPoint(4, 4));
    }
}

void PhrozenPrintingTaskPanel::set_has_reted_text(bool has_rated)
{
    if (has_rated) {
        m_has_rated_prompt->Show();
    } else {
        m_has_rated_prompt->Hide();
    }
    Layout();
    Fit();
}

void PhrozenPrintingTaskPanel::msw_rescale()
{
    m_panel_printing_title->SetSize(wxSize(-1, FromDIP(PAGE_TITLE_HEIGHT)));
    m_printing_sizer->SetMinSize(wxSize(PAGE_MIN_WIDTH, -1));
    //m_staticText_printing->SetMinSize(wxSize(PAGE_TITLE_TEXT_WIDTH, PAGE_TITLE_HEIGHT));
    m_gauge_progress->SetHeight(PROGRESSBAR_HEIGHT);
    m_gauge_progress->Rescale();
    m_button_abort->msw_rescale();
    m_bitmap_thumbnail->SetSize(TASK_THUMBNAIL_SIZE);
}

void PhrozenPrintingTaskPanel::init_bitmaps()
{
    m_thumbnail_placeholder     = ScalableBitmap(this, "monitor_placeholder", 120);
    m_bitmap_use_time           = ScalableBitmap(this, "print_info_time", 16);
    m_bitmap_use_weight         = ScalableBitmap(this, "print_info_weight", 16);
}

void PhrozenPrintingTaskPanel::init_scaled_buttons()
{
    m_button_clean->SetMinSize(wxSize(FromDIP(48), FromDIP(24)));
    m_button_clean->SetCornerRadius(FromDIP(12));
}

void PhrozenPrintingTaskPanel::error_info_reset()
{
    if (m_panel_error_txt->IsShown()) {
        m_staticline->Hide();
        m_panel_error_txt->Hide();
        m_panel_error_txt->GetParent()->Layout();
        m_error_text->SetLabel(wxEmptyString);
    }
}

void PhrozenPrintingTaskPanel::show_error_msg(wxString msg)
{
    m_staticline->Show();
    m_panel_error_txt->Show();
    m_error_text->SetLabel(msg);
}

void PhrozenPrintingTaskPanel::reset_printing_value()
{
    this->set_thumbnail_img(m_thumbnail_placeholder.bmp());
    this->set_plate_index(-1);
}

void PhrozenPrintingTaskPanel::enable_pause_resume_button(bool enable, std::string type)
{
    if (!enable) {
        m_button_pause_resume->Enable(false);

        if (type == "pause_disable") {
            m_button_pause_resume->SetBitmap_("print_control_pause_disable");
        }
        else if (type == "resume_disable") {
            m_button_pause_resume->SetBitmap_("print_control_resume_disable");
        }
    }
    else {
        m_button_pause_resume->Enable(true);
        if (type == "resume") {
        m_button_pause_resume->SetBitmap_("print_control_resume");
        if (m_button_pause_resume->GetToolTipText() != _L("Resume")) { m_button_pause_resume->SetToolTip(_L("Resume")); }
        }
        else if (type == "pause") {
        m_button_pause_resume->SetBitmap_("print_control_pause");
        if (m_button_pause_resume->GetToolTipText() != _L("Pause")) { m_button_pause_resume->SetToolTip(_L("Pause")); }
        }
    }
}

void PhrozenPrintingTaskPanel::enable_abort_button(bool enable)
{
    if (!enable) {
        m_button_abort->Enable(false);
        m_button_abort->SetBitmap_("print_control_stop_disable");
    }
    else {
        m_button_abort->Enable(true);
        m_button_abort->SetBitmap_("print_control_stop");
    }
}

void PhrozenPrintingTaskPanel::update_subtask_name(wxString name)
{
    m_staticText_subtask_value->SetLabelText(name);
}

void PhrozenPrintingTaskPanel::update_stage_value(wxString stage, int val)
{
    m_printing_stage_value->SetLabelText(stage);
    m_gauge_progress->SetValue(val);
}

void PhrozenPrintingTaskPanel::update_progress_percent(wxString percent, wxString icon)
{
    m_staticText_progress_percent->SetLabelText(percent);
    m_staticText_progress_percent_icon->SetLabelText(icon);
}

void PhrozenPrintingTaskPanel::update_left_time(wxString time)
{
    m_staticText_progress_left->SetLabelText(time);
}

void PhrozenPrintingTaskPanel::update_left_time(int mc_left_time)
{
    // update gcode progress
    std::string left_time;
    wxString    left_time_text = PHROZEN_NA_STR;

    try {
        left_time = get_bbl_monitor_time_dhm(mc_left_time);
    }
    catch (...) {
        ;
    }

    if (!left_time.empty()) left_time_text = wxString::Format("-%s", left_time);
    update_left_time(left_time_text);

    //Update end time
    std::string end_time;
    wxString    end_time_text = PHROZEN_NA_STR;
    try {
        end_time = get_bbl_monitor_end_time_dhm(mc_left_time);
    } catch (...) {
        ;
    }
    if (!end_time.empty())
        end_time_text = wxString::Format("%s", end_time);
    else
        end_time_text = PHROZEN_NA_STR;

    m_staticText_progress_end->SetLabelText(end_time_text);

}

void PhrozenPrintingTaskPanel::update_layers_num(bool show, wxString num)
{
    if (show) {
        m_staticText_layers->Show(true);
        m_staticText_layers->SetLabelText(num);
    }
    else {
        m_staticText_layers->Show(false);
        m_staticText_layers->SetLabelText(num);
    }
}

void PhrozenPrintingTaskPanel::show_priting_use_info(bool show, wxString time /*= wxEmptyString*/, wxString weight /*= wxEmptyString*/)
{
    if (show) {
        if (!m_staticText_consumption_of_time->IsShown()) {
            m_bitmap_static_use_time->Show();
            m_staticText_consumption_of_time->Show();
        }

        if (!m_staticText_consumption_of_weight->IsShown()) {
            m_bitmap_static_use_weight->Show();
            m_staticText_consumption_of_weight->Show();
        }

        m_staticText_consumption_of_time->SetLabelText(time);
        m_staticText_consumption_of_weight->SetLabelText(weight);
    }
    else {
        m_staticText_consumption_of_time->SetLabelText("0m");
        m_staticText_consumption_of_weight->SetLabelText("0g");
        if (m_staticText_consumption_of_time->IsShown()) {
            m_bitmap_static_use_time->Hide();
            m_staticText_consumption_of_time->Hide();
        }

        if (m_staticText_consumption_of_weight->IsShown()) {
            m_bitmap_static_use_weight->Hide();
            m_staticText_consumption_of_weight->Hide();
        }    }
}


void PhrozenPrintingTaskPanel::show_profile_info(bool show, wxString profile /*= wxEmptyString*/)
{
    if (show) {
        if (!m_staticText_profile_value->IsShown()) { m_staticText_profile_value->Show(); }
        m_staticText_profile_value->SetLabelText(profile);
    }
    else {
        m_staticText_profile_value->SetLabelText(wxEmptyString);
        m_staticText_profile_value->Hide();
    }
}

void PhrozenPrintingTaskPanel::set_thumbnail_img(const wxBitmap& bmp)
{
    m_thumbnail_bmp_display = bmp;
}

void PhrozenPrintingTaskPanel::set_plate_index(int plate_idx)
{
    m_plate_index = plate_idx;
}

void PhrozenPrintingTaskPanel::market_scoring_show()
{ 
    m_score_staticline->Show();
    m_score_subtask_info->Show();
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " show market scoring page";
}

void PhrozenPrintingTaskPanel::market_scoring_hide()
{
    m_score_staticline->Hide();
    m_score_subtask_info->Hide();
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " hide market scoring page";
}

void PhrozenPrintingTaskPanel::set_star_count(int star_count)
{
    m_star_count = star_count;

    for (int i = 0; i < m_score_star.size(); ++i) {
        if (i < star_count) {
            ScalableBitmap light_star = ScalableBitmap(nullptr, "score_star_light", 26);
            m_score_star[i]->SetBitmap(light_star.bmp());
        } else {
            ScalableBitmap dark_star = ScalableBitmap(nullptr, "score_star_dark", 26);
            m_score_star[i]->SetBitmap(dark_star.bmp());
        }
    }
}

#pragma endregion


#pragma region PhrozenStatusBasePanel
PhrozenStatusBasePanel::PhrozenStatusBasePanel(
    wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxString& name)
    : wxScrolledWindow(parent, id, pos, size, wxHSCROLL | wxVSCROLL)
{
    Initizlize();
}

PhrozenStatusBasePanel::~PhrozenStatusBasePanel()
{
    if ( m_media_play_ctrl ){
        delete m_media_play_ctrl;
        m_media_play_ctrl = nullptr;
    }

    if (m_custom_camera_view) {
        delete m_custom_camera_view;
        m_custom_camera_view = nullptr;
    }
}

void PhrozenStatusBasePanel::Initizlize() 
{
    this->SetScrollRate(5, 5);
    Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;
    obj = dev->get_selected_machine();

    init_bitmaps();

    this->SetBackgroundColour(wxColour(0xEE, 0xEE, 0xEE));

    wxBoxSizer *bSizer_status = new wxBoxSizer(wxVERTICAL);

    auto m_panel_separotor_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, PAGE_SPACING), wxTAB_TRAVERSAL);
    m_panel_separotor_top->SetBackgroundColour(PHROZEN_STATUS_PANEL_BG);

    bSizer_status->Add(m_panel_separotor_top, 0, wxEXPAND | wxALL, 0);

    wxBoxSizer *bSizer_status_below = new wxBoxSizer(wxHORIZONTAL);

    auto m_panel_separotor_left = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_panel_separotor_left->SetBackgroundColour(PHROZEN_STATUS_PANEL_BG);
    m_panel_separotor_left->SetMinSize(wxSize(PAGE_SPACING, -1));

    bSizer_status_below->Add(m_panel_separotor_left, 0, wxEXPAND | wxALL, 0);

    wxBoxSizer *bSizer_left = new wxBoxSizer(wxVERTICAL);

    auto m_monitoring_sizer = create_monitoring_page();
    bSizer_left->Add(m_monitoring_sizer, 1, wxEXPAND | wxALL, 0);

    auto m_panel_separotor1 = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_panel_separotor1->SetBackgroundColour(PHROZEN_STATUS_PANEL_BG);
    m_panel_separotor1->SetMinSize(wxSize(-1, PAGE_SPACING));
    m_panel_separotor1->SetMaxSize(wxSize(-1, PAGE_SPACING));
    m_monitoring_sizer->Add(m_panel_separotor1, 0, wxEXPAND, 0);

    m_project_task_panel = new PhrozenPrintingTaskPanel(this, PhrozenPrintingTaskType::PRINGINT);
    m_project_task_panel->init_bitmaps();
    m_monitoring_sizer->Add(m_project_task_panel, 0, wxALL | wxEXPAND , 0);

//    auto m_panel_separotor2 = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
//    m_panel_separotor2->SetBackgroundColour(STATUS_PANEL_BG);
//    m_panel_separotor2->SetMinSize(wxSize(-1, PAGE_SPACING));
//    bSizer_left->Add(m_panel_separotor2, 1, wxEXPAND, 0);

    bSizer_status_below->Add(bSizer_left, 1, wxALL | wxEXPAND, 0);

    auto m_panel_separator_middle = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTAB_TRAVERSAL);
    m_panel_separator_middle->SetBackgroundColour(PHROZEN_STATUS_PANEL_BG);
    m_panel_separator_middle->SetMinSize(wxSize(PAGE_SPACING, -1));

    bSizer_status_below->Add(m_panel_separator_middle, 0, wxEXPAND | wxALL, 0);

    m_machine_ctrl_panel = new wxPanel(this);
    m_machine_ctrl_panel->SetBackgroundColour(*wxWHITE);
    m_machine_ctrl_panel->SetDoubleBuffered(true);
    auto m_machine_control = create_machine_control_page(m_machine_ctrl_panel);
    m_machine_ctrl_panel->SetSizer(m_machine_control);
    m_machine_ctrl_panel->Layout();
    m_machine_control->Fit(m_machine_ctrl_panel);

    bSizer_status_below->Add(m_machine_ctrl_panel, 0, wxALL, 0);

    m_panel_separator_right = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(PAGE_SPACING, -1), wxTAB_TRAVERSAL);
    m_panel_separator_right->SetBackgroundColour(PHROZEN_STATUS_PANEL_BG);

    bSizer_status_below->Add(m_panel_separator_right, 0, wxEXPAND | wxALL, 0);

    bSizer_status->Add(bSizer_status_below, 1, wxALL | wxEXPAND, 0);

    m_panel_separotor_bottom = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, PAGE_SPACING), wxTAB_TRAVERSAL);
    m_panel_separotor_bottom->SetBackgroundColour(PHROZEN_STATUS_PANEL_BG);

    bSizer_status->Add(m_panel_separotor_bottom, 0, wxEXPAND | wxALL, 0);
    this->SetSizerAndFit(bSizer_status);
    this->Layout();
}

wxBoxSizer* PhrozenStatusBasePanel::create_monitoring_page() 
{
    
    m_panel_monitoring_title = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, PAGE_TITLE_HEIGHT), wxTAB_TRAVERSAL);
    m_panel_monitoring_title->SetBackgroundColour(PHROZEN_STATUS_TITLE_BG);

    wxBoxSizer *bSizer_monitoring_title;
    bSizer_monitoring_title = new wxBoxSizer(wxHORIZONTAL);

    m_staticText_monitoring = new Label(m_panel_monitoring_title, _L("Camera"));
    m_staticText_monitoring->Wrap(-1);
    //m_staticText_monitoring->SetFont(PAGE_TITLE_FONT);
    m_staticText_monitoring->SetForegroundColour(PHROZEN_PAGE_TITLE_FONT_COL);
    bSizer_monitoring_title->Add(m_staticText_monitoring, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, PAGE_TITLE_LEFT_MARGIN);


    bSizer_monitoring_title->Add(FromDIP(13), 0, 0, 0);
    bSizer_monitoring_title->AddStretchSpacer();

    m_staticText_timelapse = new wxStaticText(m_panel_monitoring_title, wxID_ANY, _L("Timelapse"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_timelapse->Wrap(-1);
    m_staticText_timelapse->Hide();
    bSizer_monitoring_title->Add(m_staticText_timelapse, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));

    m_bmToggleBtn_timelapse = new SwitchButton(m_panel_monitoring_title);
    m_bmToggleBtn_timelapse->SetMinSize(SWITCH_BUTTON_SIZE);
    m_bmToggleBtn_timelapse->Hide();
    bSizer_monitoring_title->Add(m_bmToggleBtn_timelapse, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));

#if !BBL_RELEASE_TO_PUBLIC
    m_staticText_timelapse->Show();
    m_bmToggleBtn_timelapse->Show();
    m_bmToggleBtn_timelapse->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &e) {
        if (e.IsChecked())
            wxGetApp().getAgent()->start_subscribe("tunnel");
        else
            wxGetApp().getAgent()->stop_subscribe("tunnel");
    });
#endif

    wxColour kBgNormal = wxColour( AMS_CONTROL_BRAND_COLOUR.Red(), AMS_CONTROL_BRAND_COLOUR.Green(), AMS_CONTROL_BRAND_COLOUR.Blue() );
    wxColour kBgDisabled = wxColour( AMS_CONTROL_BRAND_COLOUR.Red(), AMS_CONTROL_BRAND_COLOUR.Green(), AMS_CONTROL_BRAND_COLOUR.Blue(), 50 );
    wxColour kBgPressed = wxColour( AMS_CONTROL_BRAND_COLOUR.Red()/1.3, AMS_CONTROL_BRAND_COLOUR.Green()/1.3, AMS_CONTROL_BRAND_COLOUR.Blue()/1.3 );
    wxColour kBgChecked = wxColour( AMS_CONTROL_BRAND_COLOUR.Red()/1.8, AMS_CONTROL_BRAND_COLOUR.Green()/1.8, AMS_CONTROL_BRAND_COLOUR.Blue()/1.8 );
    StateColor btn_phrozen_bg( std::pair<wxColour, int>(kBgDisabled, StateColor::Disabled),
                               std::pair<wxColour, int>(kBgPressed, StateColor::Pressed),
                               std::pair<wxColour, int>(kBgChecked, StateColor::Checked),
                               std::pair<wxColour, int>(kBgNormal, StateColor::Normal));

    StateColor btn_phrozen_bd(std::pair<wxColour, int>(kBgPressed, StateColor::Hovered) );

    m_pCam_switch_button = new Button(m_panel_monitoring_title, _L(""), "PhrozenImages/Camera_Cam_Switch");
    m_pCam_switch_button->SetBackgroundColor(btn_phrozen_bg);
    m_pCam_switch_button->SetBorderColor(btn_phrozen_bd);
    m_pCam_switch_button->SetTextColor(wxColour("#FFFFFE"));
    m_pCam_switch_button->SetSize(wxSize(FromDIP(20), FromDIP(24)));
    m_pCam_switch_button->SetMinSize(wxSize(-1, FromDIP(24)));
    m_pCam_switch_button->SetCanFocus( false );
    m_pCam_switch_button->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusBasePanel::on_camera_button_triggered), NULL, this);


    m_pCam_light_switch_button = new Button(m_panel_monitoring_title, _L(""), "PhrozenImages/Camera_Light_Switch");
    m_pCam_light_switch_button->SetBackgroundColor(btn_phrozen_bg);
    m_pCam_light_switch_button->SetBorderColor(btn_phrozen_bd);
    m_pCam_light_switch_button->SetTextColor(wxColour("#FFFFFE"));
    m_pCam_light_switch_button->SetSize(wxSize(FromDIP(20), FromDIP(24)));
    m_pCam_light_switch_button->SetMinSize(wxSize(-1, FromDIP(24)));
    m_pCam_light_switch_button->SetCanFocus( false );
    m_pCam_light_switch_button->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusBasePanel::on_lighting_button_triggered), NULL, this);


    m_pCam_switch_button->SetToolTip(_L("Turn On/Off Camera"));
    m_pCam_light_switch_button->SetToolTip(_L("Turn On/Off Light"));

    bSizer_monitoring_title->Add(m_pCam_switch_button, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));
    bSizer_monitoring_title->Add(m_pCam_light_switch_button, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));


    bSizer_monitoring_title->Add(FromDIP(13), 0, 0);

    wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
    m_panel_monitoring_title->SetSizer(bSizer_monitoring_title);
    m_panel_monitoring_title->Layout();
    bSizer_monitoring_title->Fit(m_panel_monitoring_title);
    sizer->Add(m_panel_monitoring_title, 0, wxEXPAND | wxALL, 0);


    media_ctrl_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);
    media_ctrl_panel->SetBackgroundColour(*wxBLACK);

    auto fnUpdate = [this](auto& e)->void 
    {
        wxPaintDC dc(media_ctrl_panel);
        if ( m_kCurrentWebCamBitmap.IsOk()) {
            dc.DrawBitmap( m_kCurrentWebCamBitmap, 0, 0, false);
        }
    };
    media_ctrl_panel->Bind(wxEVT_PAINT, fnUpdate);//called by media_ctrl_panel->Refresh();


    sizer->Add(media_ctrl_panel, 1, wxEXPAND | wxALL, 1);

    if (wxGetApp().app_config->get("camera", "enable_custom_source") == "true") {
        handle_camera_source_change();
    }

    return sizer;
}

wxBoxSizer* PhrozenStatusBasePanel::create_machine_control_page(wxWindow* parent)
{

    wxBoxSizer* bSizer_right = new wxBoxSizer(wxVERTICAL);

    m_panel_control_title = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, PAGE_TITLE_HEIGHT), wxTAB_TRAVERSAL);
    m_panel_control_title->SetBackgroundColour(PHROZEN_STATUS_TITLE_BG);

    wxBoxSizer* bSizer_control_title = new wxBoxSizer(wxHORIZONTAL);
    m_staticText_control             = new Label(m_panel_control_title, _L("Control"));
    m_staticText_control->Wrap(-1);
    // m_staticText_control->SetFont(PAGE_TITLE_FONT);
    m_staticText_control->SetForegroundColour(PHROZEN_PAGE_TITLE_FONT_COL);

    StateColor btn_bg_green(std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Disabled),
                            std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed),
                            std::pair<wxColour, int>(wxColour(240, 94, 32), StateColor::Hovered),
                            std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));
    StateColor btn_bd_green(std::pair<wxColour, int>(AMS_CONTROL_WHITE_COLOUR, StateColor::Disabled),
                            std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Enabled));

    m_calibration_btn = new Button(m_panel_control_title, _L("Calibration"));
    m_calibration_btn->SetBackgroundColor(btn_bg_green);
    m_calibration_btn->SetBorderColor(btn_bd_green);
    m_calibration_btn->SetTextColor(wxColour("#FFFFFE"));
    m_calibration_btn->SetSize(wxSize(FromDIP(128), FromDIP(26)));
    m_calibration_btn->SetMinSize(wxSize(-1, FromDIP(26)));

    bSizer_control_title->Add(m_staticText_control, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, PAGE_TITLE_LEFT_MARGIN);
    bSizer_control_title->Add(0, 0, 1, wxEXPAND, 0);
    bSizer_control_title->Add(m_calibration_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));

    m_panel_control_title->SetSizer(bSizer_control_title);
    m_panel_control_title->Layout();
    bSizer_control_title->Fit(m_panel_control_title);
    bSizer_right->Add(m_panel_control_title, 0, wxALL | wxEXPAND, 0);

    wxBoxSizer* bSizer_control = new wxBoxSizer(wxVERTICAL);

    auto temp_axis_ctrl_sizer = create_temp_axis_group(parent);
    bSizer_control->Add(temp_axis_ctrl_sizer, 1, wxEXPAND, 0);

    auto m_ams_ctrl_sizer = create_ams_group(parent);
    bSizer_control->Add(m_ams_ctrl_sizer, 0, wxEXPAND | wxBOTTOM, FromDIP(10));

    bSizer_right->Add(bSizer_control, 1, wxEXPAND | wxALL, 0);

    return bSizer_right;
}

wxBoxSizer* PhrozenStatusBasePanel::create_temp_axis_group(wxWindow* parent)
{
    auto        sizer         = new wxBoxSizer(wxVERTICAL);
    auto        box           = new StaticBox(parent);

    StateColor box_colour(std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));
    StateColor box_border_colour(std::pair<wxColour, int>(PHROZEN_STATUS_PANEL_BG, StateColor::Normal));

    box->SetBackgroundColor(box_colour);
    box->SetBorderColor(box_border_colour);
    box->SetCornerRadius(5);

    box->SetMinSize(wxSize(FromDIP(600), -1));
    box->SetMaxSize(wxSize(FromDIP(600), -1));

    wxBoxSizer *content_sizer = new wxBoxSizer(wxHORIZONTAL);



    // the first column
    // Use wxFlexGridSizer for 2x2 grid layout to align rows
    // Row 0: Temperature+Speed | Manual Adjustment
    // Row 1: Cooling | Z-Offset
    auto sizerGrid = new wxFlexGridSizer(2, 3, 0, 0); // 2 rows, 3 cols (left, separator, right)
    sizerGrid->AddGrowableCol(0, 1);  // Left column - proportion 1
    sizerGrid->AddGrowableCol(2, 2);  // Right column - proportion 2 (needs more space for Z control)

    // ============ ROW 0: Temperature+Speed | Manual Adjustment ============
    
    // Left cell: Temperature + Speed
    wxSizer *sizerTopLeft = new wxBoxSizer(wxVERTICAL);
    
    //Temperature title
    wxPanel* line0 = new wxPanel(box, wxID_ANY, wxDefaultPosition, wxSize( -1, FromDIP(1) ) );
    line0->SetBackgroundColour(PHROZEN_STATUS_PANEL_BG); 
    sizerTopLeft->Add( line0, 0, wxALL | wxEXPAND, 0 );
    sizerTopLeft->Add(new wxStaticText(box, wxID_ANY, "Temperature"), 0, wxLEFT | wxEXPAND, 5);
    
    //Temerature body - wrapper sizer with internal spacers for even spacing
    wxPanel* line01 = new wxPanel(box, wxID_ANY, wxDefaultPosition, wxSize( -1, FromDIP(1)) );
    line01->SetBackgroundColour(PHROZEN_STATUS_PANEL_BG); 
    sizerTopLeft->Add( line01, 0, wxALL | wxEXPAND, 0 );
    
    // Create wrapper sizer for temperature controls with even spacing
    wxBoxSizer* tempBodySizer = new wxBoxSizer(wxVERTICAL);
    tempBodySizer->AddStretchSpacer(1);  // Top spacing
    tempBodySizer->Add(GenNozzleTempControllor(box), 0, wxEXPAND, 0);
    tempBodySizer->AddStretchSpacer(1);  // Middle spacing
    tempBodySizer->Add(GenHeatedBedTempControllor(box), 0, wxEXPAND, 0);
    tempBodySizer->AddStretchSpacer(1);  // Bottom spacing
    
    // Add wrapper with proportion=2 (same weight as Speed's proportion=2)
    sizerTopLeft->Add(tempBodySizer, 2, wxEXPAND, 0);

    //speed title
    wxPanel* line1 = new wxPanel(box, wxID_ANY, wxDefaultPosition, wxSize( -1, FromDIP(1)) );
    line1->SetBackgroundColour(PHROZEN_STATUS_PANEL_BG); 
    sizerTopLeft->Add( line1, 0, wxALL | wxEXPAND, 0 );
    sizerTopLeft->Add(new wxStaticText(box, wxID_ANY, "Speed"), 0, wxLEFT | wxEXPAND, 5);
    
    // speed body - proportion=2 for slightly more space, wxALIGN_CENTER_VERTICAL to center the 5 modes
    wxPanel* line11 = new wxPanel(box, wxID_ANY, wxDefaultPosition, wxSize( -1, FromDIP(1)) );
    line11->SetBackgroundColour(PHROZEN_STATUS_PANEL_BG); 
    sizerTopLeft->Add( line11, 0, wxALL | wxEXPAND, 0 );
    sizerTopLeft->Add(GenSpeed_PrintLevel(box), 2, wxALL | wxALIGN_CENTER_VERTICAL, 0);

    sizerGrid->Add(sizerTopLeft, 1, wxEXPAND);

    // Separator for Row 0
    wxPanel* sep0 = new wxPanel(box, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(1), -1));
    sep0->SetBackgroundColour(PHROZEN_STATUS_PANEL_BG);
    sizerGrid->Add(sep0, 0, wxEXPAND);

    // Right cell: Manual Adjustment - redesigned for vertical centering
    wxSizer *sizerTopRight = new wxBoxSizer(wxVERTICAL);
    
    // Title section (fixed height)
    wxPanel* line5 = new wxPanel(box, wxID_ANY, wxDefaultPosition, wxSize( -1, FromDIP(1)) );
    line5->SetBackgroundColour(PHROZEN_STATUS_PANEL_BG); 
    sizerTopRight->Add( line5, 0, wxALL | wxEXPAND, 0 );
    sizerTopRight->Add(new wxStaticText(box, wxID_ANY, "Manual Adjustment"), 0, wxLEFT, 5);

    wxPanel* line55 = new wxPanel(box, wxID_ANY, wxDefaultPosition, wxSize( -1, FromDIP(1)) );
    line55->SetBackgroundColour(PHROZEN_STATUS_PANEL_BG); 
    sizerTopRight->Add( line55, 0, wxALL | wxEXPAND, 0 );
    
    // Top spacing - centers sizerManualBody vertically (excluding title)
    sizerTopRight->AddStretchSpacer(1);

    // Content section - three controls with individual vertical centering
    auto sizerManualBody = new wxBoxSizer(wxHORIZONTAL);
    sizerManualBody->SetMinSize(wxSize(FromDIP(380), -1));  // Minimum width for all 3 controls
    
    // Left spacing
    sizerManualBody->AddStretchSpacer(1);
    
    // 0.1/1/10mm control - wrapped for vertical centering
    auto wrapper_moveRange = new wxBoxSizer(wxVERTICAL);
    //wrapper_moveRange->AddStretchSpacer(1);  // Top spacing
    wrapper_moveRange->Add( GenManualAdjustment_moveRange(box), 0, wxALIGN_CENTER_HORIZONTAL, 0 );
    //wrapper_moveRange->AddStretchSpacer(1);  // Bottom spacing
    sizerManualBody->Add( wrapper_moveRange, 0, wxALIGN_CENTER_VERTICAL, 0 );
    
    // Spacing between moveRange and XY
    sizerManualBody->AddStretchSpacer(1);
    
    // XY control - wrapped for vertical centering
    auto wrapper_xy = new wxBoxSizer(wxVERTICAL);
    //wrapper_xy->AddStretchSpacer(1);  // Top spacing
    wrapper_xy->Add( GenManualAdjustment_move_xy(box), 0, wxALIGN_CENTER_HORIZONTAL, 0 );
    //wrapper_xy->AddStretchSpacer(1);  // Bottom spacing
    sizerManualBody->Add( wrapper_xy, 0, wxALIGN_CENTER_VERTICAL, 0 );
    
    // Spacing between XY and Z
    sizerManualBody->AddStretchSpacer(1);
    
    // Z control - wrapped for vertical centering
    auto wrapper_z = new wxBoxSizer(wxVERTICAL);
    //wrapper_z->AddStretchSpacer(1);  // Top spacing
    wrapper_z->Add( GenManualAdjustment_move_z(box), 0, wxALIGN_CENTER_HORIZONTAL, 0 );
    //wrapper_z->AddStretchSpacer(1);  // Bottom spacing
    sizerManualBody->Add( wrapper_z, 0, wxALIGN_CENTER_VERTICAL, 0 );
    
    // Right spacing
    sizerManualBody->AddStretchSpacer(1);
    
    sizerTopRight->Add( sizerManualBody, 0, wxALIGN_CENTER_HORIZONTAL, 0 );
    
    // Bottom spacing - centers sizerManualBody vertically (excluding title)
    sizerTopRight->AddStretchSpacer(2);

    sizerGrid->Add(sizerTopRight, 1, wxEXPAND);

    // ============ ROW 1: Cooling | Z-Offset ============
    
    // Left cell: Cooling
    wxSizer *sizerBottomLeft = new wxBoxSizer(wxVERTICAL);
    
    // cooling title
    wxPanel* line2 = new wxPanel(box, wxID_ANY, wxDefaultPosition, wxSize( -1, FromDIP(1)) );
    line2->SetBackgroundColour(PHROZEN_STATUS_PANEL_BG); 
    sizerBottomLeft->Add( line2, 0, wxALL | wxEXPAND, 0 );

    wxStaticText* coolingTitle = new wxStaticText(box, wxID_ANY, "Cooling");
    sizerBottomLeft->Add(coolingTitle, 0, wxLEFT, 5);

    // cooling body - wrapper sizer with internal spacers for even spacing (4 equal parts)
    wxPanel* line22 = new wxPanel(box, wxID_ANY, wxDefaultPosition, wxSize( -1, FromDIP(1)) );
    line22->SetBackgroundColour(PHROZEN_STATUS_PANEL_BG); 
    sizerBottomLeft->Add( line22, 0, wxALL | wxEXPAND, 0 );

    // Create wrapper sizer for cooling controls with even spacing
    wxBoxSizer* coolingBodySizer = new wxBoxSizer(wxVERTICAL);
    coolingBodySizer->AddStretchSpacer(1);  // Top spacing
    coolingBodySizer->Add(GenCooling_Auxiliary(box), 0, wxEXPAND, 0);
    coolingBodySizer->AddStretchSpacer(1);  // Between Auxiliary and Part
    coolingBodySizer->Add(GenCooling_Part(box), 0, wxEXPAND, 0);
    coolingBodySizer->AddStretchSpacer(1);  // Between Part and Shield
    coolingBodySizer->Add(GenCooling_Shield(box), 0, wxEXPAND, 0);
    coolingBodySizer->AddStretchSpacer(1);  // Bottom spacing
    
    sizerBottomLeft->Add(coolingBodySizer, 1, wxEXPAND, 0);

    wxPanel* line3 = new wxPanel(box, wxID_ANY, wxDefaultPosition, wxSize( -1, FromDIP(1)) );
    line3->SetBackgroundColour(PHROZEN_STATUS_PANEL_BG); 
    sizerBottomLeft->Add( line3, 0, wxALL | wxEXPAND, 0 );

    sizerGrid->Add(sizerBottomLeft, 1, wxEXPAND);

    // Separator for Row 1
    wxPanel* sep1 = new wxPanel(box, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(1), -1));
    sep1->SetBackgroundColour(PHROZEN_STATUS_PANEL_BG);
    sizerGrid->Add(sep1, 0, wxEXPAND);

    // Right cell: Z-Offset
    wxSizer *sizerBottomRight = new wxBoxSizer(wxVERTICAL);

    // z-offset title
    wxPanel* line6 = new wxPanel(box, wxID_ANY, wxDefaultPosition, wxSize( -1, FromDIP(1)) );
    line6->SetBackgroundColour(PHROZEN_STATUS_PANEL_BG); 
    sizerBottomRight->Add( line6, 0, wxALL | wxEXPAND, 0 );

    wxStaticText* zOffsetTitle = new wxStaticText(box, wxID_ANY, "Z-Offset");
    sizerBottomRight->Add(zOffsetTitle, 0, wxLEFT, 5);

    // z-offset body
    wxPanel* line66 = new wxPanel(box, wxID_ANY, wxDefaultPosition, wxSize( -1, FromDIP(1)) );
    line66->SetBackgroundColour(PHROZEN_STATUS_PANEL_BG); 
    sizerBottomRight->Add( line66, 0, wxALL | wxEXPAND, 0 );

    // Top spacing - centers the button group vertically (excluding title)
    sizerBottomRight->AddStretchSpacer(1);

    // Wrap buttons in a horizontal sizer to center them horizontally
    auto sizerZOffsetButtons = new wxBoxSizer(wxHORIZONTAL);
    sizerZOffsetButtons->AddStretchSpacer(1);  // Left spacing
    sizerZOffsetButtons->Add(GenManualAdjustment_z_offset(box), 0, wxALIGN_CENTER_VERTICAL, 0);
    sizerZOffsetButtons->AddStretchSpacer(1);  // Right spacing
    sizerBottomRight->Add(sizerZOffsetButtons, 0, wxALIGN_CENTER_HORIZONTAL, 0);

    // Bottom spacing - centers the button group vertically (excluding title)
    sizerBottomRight->AddStretchSpacer(1);

    sizerGrid->Add(sizerBottomRight, 1, wxEXPAND);

    // Set the grid as box's sizer
    box->SetSizer(sizerGrid);
    sizer->Add(box, 0, wxEXPAND | wxALL, FromDIP(9));

    return sizer;
}

wxBoxSizer* PhrozenStatusBasePanel::create_temp_control(wxWindow* parent)
{
    auto sizer = new wxBoxSizer(wxVERTICAL);

    wxWindowID nozzle_id = wxWindow::NewControlId();
    m_tempCtrl_nozzle    = new TempInput(parent, 
                                          nozzle_id, 
                                          PHROZEN_TEMP_BLANK_STR, 
                                          PHROZEN_TEMP_BLANK_STR, 
                                          wxString("Phrozen_monitor_nozzle_temp"), 
                                          wxString("Phrozen_monitor_nozzle_temp_active"),
                                          wxDefaultPosition, 
                                          wxDefaultSize, 
                                          wxALIGN_CENTER);
    m_tempCtrl_nozzle->SetMinSize(TEMP_CTRL_MIN_SIZE);
    m_tempCtrl_nozzle->SetMinTemp(phrozen_nozzle_temp_range[0]);
    m_tempCtrl_nozzle->SetMaxTemp(phrozen_nozzle_temp_range[1]);
    m_tempCtrl_nozzle->SetBorderWidth(FromDIP(2));

    StateColor tempinput_text_colour(std::make_pair(PHROZEN_DISCONNECT_TEXT_COL, (int) StateColor::Disabled), std::make_pair(PHROZEN_NORMAL_TEXT_COL, (int) StateColor::Normal));
    StateColor tempinput_border_colour(std::make_pair(*wxWHITE, (int)StateColor::Disabled), std::make_pair(PHROZEN_BUTTON_HOVER_COL, (int)StateColor::Focused),
        std::make_pair(PHROZEN_BUTTON_HOVER_COL, (int)StateColor::Hovered), std::make_pair(*wxWHITE, (int)StateColor::Normal));

    m_tempCtrl_nozzle->SetTextColor(tempinput_text_colour);
    m_tempCtrl_nozzle->SetBorderColor(tempinput_border_colour);

    sizer->Add(m_tempCtrl_nozzle, 0, wxEXPAND | wxALL, 1);

    m_line_nozzle = new StaticLine(parent);
    m_line_nozzle->SetLineColour(PHROZEN_STATIC_BOX_LINE_COL);
    m_line_nozzle->SetSize(wxSize(FromDIP(1), -1));
    sizer->Add(m_line_nozzle, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);

    wxWindowID bed_id = wxWindow::NewControlId();
    m_tempCtrl_bed    = new TempInput(parent, 
                                       bed_id, 
                                       PHROZEN_TEMP_BLANK_STR, 
                                       PHROZEN_TEMP_BLANK_STR, 
                                       wxString("Phrozen_monitor_bed_temp"), 
                                       wxString("Phrozen_monitor_bed_temp_active"), 
                                       wxDefaultPosition,
                                       wxDefaultSize, wxALIGN_CENTER);
    m_tempCtrl_bed->SetMinTemp(phrozen_bed_temp_range[0]);
    m_tempCtrl_bed->SetMaxTemp(phrozen_bed_temp_range[1]);
    m_tempCtrl_bed->SetMinSize(TEMP_CTRL_MIN_SIZE);
    m_tempCtrl_bed->SetBorderWidth(FromDIP(2));
    m_tempCtrl_bed->SetTextColor(tempinput_text_colour);
    m_tempCtrl_bed->SetBorderColor(tempinput_border_colour);
    sizer->Add(m_tempCtrl_bed, 0, wxEXPAND | wxALL, 1);

    auto line = new StaticLine(parent);
    line->SetLineColour(PHROZEN_STATIC_BOX_LINE_COL);
    sizer->Add(line, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);

    wxWindowID frame_id = wxWindow::NewControlId();
    m_tempCtrl_chamber    = new TempInput(parent, 
                                           frame_id, 
                                           PHROZEN_TEMP_BLANK_STR, 
                                           PHROZEN_TEMP_BLANK_STR, 
                                           wxString("Phrozen_monitor_frame_temp"), 
                                           wxString("Phrozen_monitor_frame_temp_active"), 
                                           wxDefaultPosition,
                                           wxDefaultSize, 
                                           wxALIGN_CENTER);
    m_tempCtrl_chamber->SetReadOnly(true);
    m_tempCtrl_chamber->SetMinTemp(phrozen_nozzle_chamber_range[0]);
    m_tempCtrl_chamber->SetMaxTemp(phrozen_nozzle_chamber_range[1]);
    m_tempCtrl_chamber->SetMinSize(TEMP_CTRL_MIN_SIZE);
    m_tempCtrl_chamber->SetBorderWidth(FromDIP(2));
    m_tempCtrl_chamber->SetTextColor(tempinput_text_colour);
    m_tempCtrl_chamber->SetBorderColor(tempinput_border_colour);

    sizer->Add(m_tempCtrl_chamber, 0, wxEXPAND | wxALL, 1);
    line = new StaticLine(parent);
    line->SetLineColour(PHROZEN_STATIC_BOX_LINE_COL);
    sizer->Add(line, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);

    m_misc_ctrl_sizer = create_misc_control(parent);

    sizer->Add(m_misc_ctrl_sizer, 0, wxEXPAND, 0);
    return sizer;
}

wxBoxSizer* PhrozenStatusBasePanel::create_misc_control(wxWindow* parent)
{
    wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer *line_sizer = new wxBoxSizer(wxHORIZONTAL);

    /* create speed control */
    m_switch_speed = new ImageSwitchButton(parent, m_bitmap_speed_active, m_bitmap_speed);
    m_switch_speed->SetLabels("100%", "100%");
    m_switch_speed->SetMinSize(MISC_BUTTON_2FAN_SIZE);
    m_switch_speed->SetMaxSize(MISC_BUTTON_2FAN_SIZE);
    m_switch_speed->SetPadding(FromDIP(3));
    m_switch_speed->SetBorderWidth(FromDIP(2));
    m_switch_speed->SetFont(Label::Head_13);
    m_switch_speed->SetTextColor(StateColor(std::make_pair(PHROZEN_DISCONNECT_TEXT_COL, (int) StateColor::Disabled), std::make_pair(PHROZEN_NORMAL_TEXT_COL, (int) StateColor::Normal)));
    m_switch_speed->SetValue(false);

    line_sizer->Add(m_switch_speed, 1, wxALIGN_CENTER | wxALL, 0);

    auto line = new StaticLine(parent, true);
    line->SetLineColour(PHROZEN_STATIC_BOX_LINE_COL);
    line_sizer->Add(line, 0, wxEXPAND | wxTOP | wxBOTTOM, 4);

    /* create lamp control */
    m_switch_lamp = new ImageSwitchButton(parent, m_bitmap_lamp_on, m_bitmap_lamp_off);
    m_switch_lamp->SetLabels(_L("Lamp"), _L("Lamp"));
    m_switch_lamp->SetMinSize(MISC_BUTTON_2FAN_SIZE);
    m_switch_lamp->SetMaxSize(MISC_BUTTON_2FAN_SIZE);
    m_switch_lamp->SetPadding(FromDIP(3));
    m_switch_lamp->SetBorderWidth(FromDIP(2));
    m_switch_lamp->SetFont(Label::Head_13);
    m_switch_lamp->SetTextColor(StateColor(std::make_pair(PHROZEN_DISCONNECT_TEXT_COL, (int) StateColor::Disabled), std::make_pair(PHROZEN_NORMAL_TEXT_COL, (int) StateColor::Normal)));
    line_sizer->Add(m_switch_lamp, 1, wxALIGN_CENTER | wxALL, 0);

    sizer->Add(line_sizer, 0, wxEXPAND, FromDIP(5));
    line = new StaticLine(parent);
    line->SetLineColour(PHROZEN_STATIC_BOX_LINE_COL);
    sizer->Add(line, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);

    m_fan_panel = new StaticBox(parent);
    m_fan_panel->SetMinSize(MISC_BUTTON_PANEL_SIZE);
    m_fan_panel->SetMaxSize(MISC_BUTTON_PANEL_SIZE);
    m_fan_panel->SetBackgroundColor(*wxWHITE);
    m_fan_panel->SetBorderWidth(0);
    m_fan_panel->SetCornerRadius(0);

    auto fan_line_sizer          = new wxBoxSizer(wxHORIZONTAL);
    m_switch_nozzle_fan = new FanSwitchButton(m_fan_panel, m_bitmap_fan_on, m_bitmap_fan_off);
    m_switch_nozzle_fan->SetMinSize(MISC_BUTTON_3FAN_SIZE);
    m_switch_nozzle_fan->SetMaxSize(MISC_BUTTON_3FAN_SIZE);
    m_switch_nozzle_fan->SetValue(false);
    m_switch_nozzle_fan->SetLabels(_L("Part"), _L("Part"));
    m_switch_nozzle_fan->SetPadding(FromDIP(1));
    m_switch_nozzle_fan->SetBorderWidth(0);
    m_switch_nozzle_fan->SetCornerRadius(0);
    m_switch_nozzle_fan->SetFont(::Label::Body_10);
    m_switch_nozzle_fan->SetTextColor(StateColor(std::make_pair(PHROZEN_DISCONNECT_TEXT_COL, (int) StateColor::Disabled), std::make_pair(PHROZEN_NORMAL_FAN_TEXT_COL, (int) StateColor::Normal)));

    m_switch_nozzle_fan->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {
        m_fan_panel->SetBackgroundColor(wxColour(255, 124, 63));
    });

    m_switch_nozzle_fan->Bind(wxEVT_LEAVE_WINDOW, [this, parent](auto& e) {
        m_fan_panel->SetBackgroundColor(parent->GetBackgroundColour());
    });

    m_switch_printing_fan = new FanSwitchButton(m_fan_panel, m_bitmap_fan_on, m_bitmap_fan_off);
    m_switch_printing_fan->SetValue(false);
    m_switch_printing_fan->SetMinSize(MISC_BUTTON_3FAN_SIZE);
    m_switch_printing_fan->SetMaxSize(MISC_BUTTON_3FAN_SIZE);
    m_switch_printing_fan->SetPadding(FromDIP(1));
    m_switch_printing_fan->SetBorderWidth(0);
    m_switch_printing_fan->SetCornerRadius(0);
    m_switch_printing_fan->SetFont(::Label::Body_10);
    m_switch_printing_fan->SetLabels(_L("Aux"), _L("Aux"));
    m_switch_printing_fan->SetTextColor(
        StateColor(std::make_pair(PHROZEN_DISCONNECT_TEXT_COL, (int) StateColor::Disabled), std::make_pair(PHROZEN_NORMAL_FAN_TEXT_COL, (int) StateColor::Normal)));

    m_switch_printing_fan->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {
        m_fan_panel->SetBackgroundColor(wxColour(255, 124, 63));
    });

    m_switch_printing_fan->Bind(wxEVT_LEAVE_WINDOW, [this, parent](auto& e) {
        m_fan_panel->SetBackgroundColor(parent->GetBackgroundColour());
    });

    m_switch_cham_fan = new FanSwitchButton(m_fan_panel, m_bitmap_fan_on, m_bitmap_fan_off);
    m_switch_cham_fan->SetValue(false);
    m_switch_cham_fan->SetMinSize(MISC_BUTTON_3FAN_SIZE);
    m_switch_cham_fan->SetMaxSize(MISC_BUTTON_3FAN_SIZE);
    m_switch_cham_fan->SetPadding(FromDIP(1));
    m_switch_cham_fan->SetBorderWidth(0);
    m_switch_cham_fan->SetCornerRadius(0);
    m_switch_cham_fan->SetFont(::Label::Body_10);
    m_switch_cham_fan->SetLabels(_L("Cham"), _L("Cham"));
    m_switch_cham_fan->SetTextColor(
        StateColor(std::make_pair(PHROZEN_DISCONNECT_TEXT_COL, (int)StateColor::Disabled), std::make_pair(PHROZEN_NORMAL_FAN_TEXT_COL, (int)StateColor::Normal)));

    m_switch_cham_fan->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {
        m_fan_panel->SetBackgroundColor(wxColour(255, 124, 63));
    });

    m_switch_cham_fan->Bind(wxEVT_LEAVE_WINDOW, [this, parent](auto& e) {
        m_fan_panel->SetBackgroundColor(parent->GetBackgroundColour());
    });

    //m_switch_block_fan = new wxPanel(m_fan_panel);
    //m_switch_block_fan->SetBackgroundColour(parent->GetBackgroundColour());

    fan_line_sizer->Add(0, 0, 0, wxLEFT, FromDIP(2));
    fan_line_sizer->Add(m_switch_nozzle_fan, 0, wxALIGN_CENTER | wxTOP | wxBOTTOM , FromDIP(2));
    fan_line_sizer->Add(m_switch_printing_fan, 0, wxALIGN_CENTER | wxTOP | wxBOTTOM, FromDIP(2));
    fan_line_sizer->Add(m_switch_cham_fan, 0, wxALIGN_CENTER | wxTOP | wxBOTTOM , FromDIP(2));
    //fan_line_sizer->Add(m_switch_block_fan, 1, wxEXPAND | wxTOP | wxBOTTOM , FromDIP(2));
    fan_line_sizer->Add(0, 0, 0, wxLEFT, FromDIP(2));

    m_fan_panel->SetSizer(fan_line_sizer);
    m_fan_panel->Layout();
    m_fan_panel->Fit();
    sizer->Add(m_fan_panel, 0, wxEXPAND, FromDIP(5));


    return sizer;
}

wxBoxSizer* PhrozenStatusBasePanel::create_axis_control(wxWindow* parent)
{
    //return StatusBasePanel::create_axis_control(parent);

    auto sizer = new wxBoxSizer(wxVERTICAL);
    sizer->AddStretchSpacer();
    m_phButton_xy = new PhrozenAxisCtrlButton( parent );
    //m_bpButton_xy = new PhrozenAxisCtrlButton(parent, m_bitmap_axis_home);
    m_phButton_xy->SetMinSize(AXIS_MIN_SIZE);
    m_phButton_xy->SetSize(AXIS_MIN_SIZE);
    sizer->AddStretchSpacer();
    sizer->Add(m_phButton_xy, 0, wxALIGN_CENTER | wxALL, 0);
    sizer->AddStretchSpacer();

    /*m_staticText_xy = new wxStaticText(parent, wxID_ANY, _L("X/Y Axis"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_xy->Wrap(-1);

    m_staticText_xy->SetForegroundColour(TEXT_LIGHT_FONT_COL);
    sizer->Add(m_staticText_xy, 0, wxBOTTOM | wxALIGN_CENTER_HORIZONTAL, FromDIP(5));*/
    return sizer;
}

void PhrozenStatusBasePanel::init_bitmaps()
{
    static Slic3r::GUI::BitmapCache cache;
    m_bitmap_lamp_on         = ScalableBitmap(this, "monitor_lamp_on", 24);
    m_bitmap_lamp_off        = ScalableBitmap(this, "monitor_lamp_off", 24);
    m_bitmap_fan_on          = ScalableBitmap(this, "monitor_fan_on", 22);
    m_bitmap_fan_off         = ScalableBitmap(this, "monitor_fan_off", 22);
    m_bitmap_speed           = ScalableBitmap(this, "monitor_speed", 24);
    m_bitmap_speed_active    = ScalableBitmap(this, "monitor_speed_active", 24);

    m_thumbnail_brokenimg    = ScalableBitmap(this, "monitor_brokenimg", 120);
    m_thumbnail_sdcard       = ScalableBitmap(this, "monitor_sdcard_thumbnail", 120);
    m_bitmap_extruder_empty_load      = *cache.load_png("monitor_extruder_empty_load", FromDIP(28), FromDIP(70), false, false);
    m_bitmap_extruder_filled_load     = *cache.load_png("monitor_extruder_filled_load", FromDIP(28), FromDIP(70), false, false);
    m_bitmap_extruder_empty_unload    = *cache.load_png("monitor_extruder_empty_unload", FromDIP(28), FromDIP(70), false, false);
    m_bitmap_extruder_filled_unload   = *cache.load_png("monitor_extruder_filled_unload", FromDIP(28), FromDIP(70), false, false);


    m_ParamSeparator    = ScalableBitmap(this, "PhrozenImages/ControlPanel_Temp_Separator", FromDIP(12));
    m_Dot_c             = ScalableBitmap(this, "PhrozenImages/ControlPanel_Dot_C", FromDIP(12));
    m_Percent           = ScalableBitmap(this, "PhrozenImages/ControlPanel_Percent", FromDIP(12));
    m_Nozzle_temp       = ScalableBitmap(this, "PhrozenImages/ControlPanel_Nozzle_Temp", FromDIP(12));
    m_Heated_bed_temp   = ScalableBitmap(this, "PhrozenImages/ControlPanel_Bed_Temp", FromDIP(12));
    m_Fan               = ScalableBitmap(this, "PhrozenImages/ControlPanel_Fan", FromDIP(12));
                                        
    m_Speed             = ScalableBitmap(this, "PhrozenImages/ControlPanel_Speed", FromDIP(12));
    m_Speed_Level       = ScalableBitmap(this, "PhrozenImages/ControlPanel_Speed_Level", FromDIP(12));
                                        
    m_Control_xy_up     = ScalableBitmap(this, "PhrozenImages/ControlPanel_Controllor_Up", FromDIP(28));
    m_Control_xy_down   = ScalableBitmap(this, "PhrozenImages/ControlPanel_Controllor_Down", FromDIP(28));
    m_Control_xy_left   = ScalableBitmap(this, "PhrozenImages/ControlPanel_Controllor_Left", FromDIP(28));
    m_Control_xy_right  = ScalableBitmap(this, "PhrozenImages/ControlPanel_Controllor_Right", FromDIP(28));
    m_Control_xy_home   = ScalableBitmap(this, "PhrozenImages/ControlPanel_Controllor_Home", FromDIP(28));
    m_Control_xy_title  = ScalableBitmap(this, "PhrozenImages/ControlPanel_Controllor_Title_XY", FromDIP(10));

    m_Control_z_title   = ScalableBitmap(this, "PhrozenImages/ControlPanel_Controllor_Title_Z", FromDIP(10));
    m_Control_z_nozzle  = ScalableBitmap(this, "PhrozenImages/ControlPanel_Nozzle", FromDIP(24));

}

void PhrozenStatusBasePanel::on_webview_navigating(wxWebViewEvent& evt) {
    wxGetApp().CallAfter([this] {
        remove_controls();
    });
}

void PhrozenStatusBasePanel::reset_temp_misc_control()
{
#if 0
    // reset temp string
    m_tempCtrl_nozzle->SetLabel(TEMP_BLANK_STR);
    m_tempCtrl_nozzle->GetTextCtrl()->SetValue(TEMP_BLANK_STR);
    m_tempCtrl_bed->SetLabel(TEMP_BLANK_STR);
    m_tempCtrl_bed->GetTextCtrl()->SetValue(TEMP_BLANK_STR);
    m_tempCtrl_chamber->SetLabel(TEMP_BLANK_STR);
    m_tempCtrl_chamber->GetTextCtrl()->SetValue(TEMP_BLANK_STR);
    m_button_unload->Show();

    m_tempCtrl_nozzle->Enable(true);
    m_tempCtrl_chamber->Enable(true);
    m_tempCtrl_bed->Enable(true);

    // reset misc control
    m_switch_speed->SetLabels("100%", "100%");
    m_switch_speed->SetValue(false);
    m_switch_lamp->SetLabels(_L("Lamp"), _L("Lamp"));
    m_switch_lamp->SetValue(false);
    m_switch_nozzle_fan->SetValue(false);
    m_switch_printing_fan->SetValue(false);
    m_switch_cham_fan->SetValue(false);
    #endif
}

wxBoxSizer* PhrozenStatusBasePanel::create_ams_group(wxWindow* parent)
{
    auto sizer = new wxBoxSizer(wxVERTICAL);
    auto box = new StaticBox(parent);

    StateColor box_colour(std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));
    StateColor box_border_colour(std::pair<wxColour, int>(PHROZEN_STATUS_PANEL_BG, StateColor::Normal));

    box->SetBackgroundColor(box_colour);
    box->SetBorderColor(box_border_colour);
    box->SetCornerRadius(5);

    box->SetMinSize(wxSize(FromDIP(620), FromDIP(351)));

    // Title - FILAMENT
    auto title_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto title_text = new wxStaticText(box, wxID_ANY, _L("Filament"), wxDefaultPosition, wxDefaultSize, 0);
    title_text->SetFont(Label::Head_14);
    title_text->SetForegroundColour(PHROZEN_NORMAL_TEXT_COL);
    title_sizer->Add(title_text, 0, wxALL, FromDIP(10));

    // Main frame panel with border
    auto frame_panel = new wxPanel(box, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(600), FromDIP(320)));
    frame_panel->SetBackgroundColour(box_colour.colorForStates(StateColor::Normal));

    auto frame_sizer = new wxBoxSizer(wxHORIZONTAL);

    // Left side - Tips panel
    auto tips_panel = new wxPanel(frame_panel, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(200), FromDIP(296)));
    tips_panel->SetBackgroundColour(*wxBLACK);

    auto tips_sizer = new wxBoxSizer(wxVERTICAL);

    auto tips_title = new wxStaticText(tips_panel, wxID_ANY, _L("Tips"), wxDefaultPosition, wxDefaultSize, 0);
    tips_title->SetFont(Label::Head_13);
    tips_title->SetForegroundColour(*wxWHITE);
    tips_sizer->Add(tips_title, 0, wxALL, FromDIP(12));

    auto tips_text = new wxStaticText(tips_panel, wxID_ANY, _L("Filament Tips"), wxDefaultPosition, wxSize(FromDIP(176), -1), wxST_NO_AUTORESIZE);
    tips_text->SetFont(Label::Body_12);
    tips_text->SetForegroundColour(*wxWHITE);
    tips_text->Wrap(FromDIP(176));
    tips_sizer->Add(tips_text, 1, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(12));

    auto tips_warning = new wxStaticText(tips_panel, wxID_ANY, _L("Filament Tips"), wxDefaultPosition, wxSize(FromDIP(176), -1), wxST_NO_AUTORESIZE);
    tips_warning->SetFont(Label::Body_12);
    tips_warning->SetForegroundColour(wxColour(255, 182, 73));
    tips_warning->Wrap(FromDIP(176));
    tips_sizer->Add(tips_warning, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, FromDIP(12));

    tips_panel->SetSizer(tips_sizer);
    frame_sizer->Add(tips_panel, 0, wxALL, FromDIP(12));

    m_pFilamentControlPanel = new PhrozenFilamentControl(frame_panel, wxID_ANY);

    // Bind button event
    m_pFilamentControlPanel->GetUnloadAllButton()->Bind(wxEVT_BUTTON,   &PhrozenStatusBasePanel::on_ams_unload_all, this);
    m_pFilamentControlPanel->GetLoadButton()->Bind(wxEVT_BUTTON,        &PhrozenStatusBasePanel::on_ams_load_single_slot, this);
    m_pFilamentControlPanel->GetUnloadButton()->Bind(wxEVT_BUTTON,      &PhrozenStatusBasePanel::on_ams_unload_single_slot, this);


    frame_sizer->Add(m_pFilamentControlPanel, 1, wxEXPAND | wxALL, FromDIP(12));
    frame_panel->SetSizer(frame_sizer);

    // Main content sizer
    auto content_sizer = new wxBoxSizer(wxVERTICAL);
    content_sizer->Add(title_sizer, 0, wxEXPAND);
    content_sizer->Add(frame_panel, 1, wxALL | wxEXPAND, FromDIP(10));

    box->SetSizer(content_sizer);
    sizer->Add(box, 0, wxEXPAND | wxALL, FromDIP(9));

    return sizer;
}

void PhrozenStatusBasePanel::show_ams_group(bool show)
{
    m_pFilamentControlPanel->Show(true);
    if (m_show_ams_group != show) {
        Fit();
    }
    m_show_ams_group = show;
}

void PhrozenStatusBasePanel::on_camera_source_change(wxCommandEvent& event)
{
    handle_camera_source_change();
}

void PhrozenStatusBasePanel::on_ams_unload_all(wxCommandEvent& WXUNUSED(event))
{
    try {
    #ifdef __APPLE__
        if (!obj){
            obj = wxGetApp().GetPhrozenMachineObject();
        }
    #endif
        if (obj) {
            obj->SetPhrozenCommand_unload_all_slots();
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "on_ams_unload_all: Exception occurred: " << e.what();
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "on_ams_unload_all: Unknown exception occurred";
    }
}

void PhrozenStatusBasePanel::on_ams_unload_single_slot(wxCommandEvent& WXUNUSED(event))
{
    int nSlotId = m_pFilamentControlPanel->GetSelectedAmsSlotIndex()+1;
    // Log for macOS Xcode console and boost log
    std::cout << "[AMS Unload] Selected slot ID: " << nSlotId << std::endl;
    BOOST_LOG_TRIVIAL(info) << "on_ams_unload_single_slot: Selected slot ID: " << nSlotId;
    if ( nSlotId < 0 )
    {
        BOOST_LOG_TRIVIAL(warning) << "on_ams_unload_single_slot: Invalid slot ID (" << nSlotId << " < 0), returning";
        return;
    }
    
    try {
    #ifdef __APPLE__
        if (!obj){
            obj = wxGetApp().GetPhrozenMachineObject();
        }
    #endif
        if (obj) {
            obj->SetPhrozenCommand_unload(nSlotId);
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "on_ams_unload_single_slot: Exception occurred: " << e.what();
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "on_ams_unload_single_slot: Unknown exception occurred";
    }
}

void PhrozenStatusBasePanel::on_ams_load_single_slot(wxCommandEvent& WXUNUSED(event))
{
    int nSlotId = m_pFilamentControlPanel->GetSelectedAmsSlotIndex()+1;
    // Log for macOS Xcode console and boost log
    std::cout << "[AMS Load] Selected slot ID: " << nSlotId << std::endl;
    BOOST_LOG_TRIVIAL(info) << "on_ams_load_single_slot: Selected slot ID: " << nSlotId;
    if ( nSlotId < 0 )
    {
        BOOST_LOG_TRIVIAL(warning) << "on_ams_load_single_slot: Invalid slot ID (" << nSlotId << " < 0), returning";
        return;
    }
    
    try {
    #ifdef __APPLE__
        if (!obj){
            obj = wxGetApp().GetPhrozenMachineObject();
        }
    #endif
        if (obj) {
            obj->SetPhrozenCommand_load(nSlotId);
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "on_ams_load_single_slot: Exception occurred: " << e.what();
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "on_ams_load_single_slot: Unknown exception occurred";
    }
}

void PhrozenStatusBasePanel::on_camera_button_triggered( wxCommandEvent& event )
{
    auto pObj = dynamic_cast< Button* >( event.GetEventObject() );
    if ( !pObj ) return;
    bool bCurrent = pObj->GetValue();
    bool bTarget = !bCurrent;
    pObj->SetValue( bTarget );
}

void PhrozenStatusBasePanel::on_lighting_button_triggered( wxCommandEvent& event )
{
    auto pObj = dynamic_cast< Button* >( event.GetEventObject() );
    if ( !pObj ) return;
    bool bCurrent = pObj->GetValue();
    bool bTarget = !bCurrent;
    pObj->SetValue( bTarget );
}

bool PhrozenStatusBasePanel::IsWebcamUiEnabled()
{
    return m_pCam_switch_button->GetValue();
}

bool PhrozenStatusBasePanel::IsLightingUiEnabled()
{
    return m_pCam_light_switch_button->GetValue();
}


void PhrozenStatusBasePanel::handle_camera_source_change()
{
    const auto new_cam_url = wxGetApp().app_config->get("camera", "custom_source");
    const auto enabled = wxGetApp().app_config->get("camera", "enable_custom_source") == "true";
}

void PhrozenStatusBasePanel::toggle_builtin_camera()
{
    m_custom_camera_view->Hide();
    m_media_play_ctrl->Show();
}

void PhrozenStatusBasePanel::toggle_custom_camera()
{
    const auto enabled = wxGetApp().app_config->get("camera", "enable_custom_source") == "true";

    if (enabled) {
        m_custom_camera_view->Show();
        m_media_play_ctrl->Hide();
    }
}

void PhrozenStatusBasePanel::on_camera_switch_toggled(wxMouseEvent& event)
{
}

void PhrozenStatusBasePanel::remove_controls()
{
    const std::string js_cleanup_video_element = R"(
        document.body.style.overflow='hidden';
        const video = document.querySelector('video');
        video.setAttribute('style', 'width: 100% !important;');
        video.removeAttribute('controls');
        video.addEventListener('leavepictureinpicture', () => {
            window.wx.postMessage('leavepictureinpicture');
        });
        video.addEventListener('enterpictureinpicture', () => {
            window.wx.postMessage('enterpictureinpicture');
        });
    )";
    m_custom_camera_view->RunScript(js_cleanup_video_element);
}









//=========================================================//
// ================ phrozen style ui ===================== //

// Minimum width for value text (in DIP), enough to accommodate 3-digit numbers (0~999)
constexpr int VALUE_TEXT_MIN_WIDTH_DIP = 35;

void CreateValueText( std::unique_ptr<wxStaticText>& ptr, wxWindow* pParent )
{
    ptr = std::make_unique<wxStaticText>( pParent, wxID_ANY, "0", wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT | wxST_NO_AUTORESIZE );
    // Use FromDIP for DPI scaling to ensure consistent appearance across different platforms and DPI settings
    ptr->SetMinSize(wxSize(pParent->FromDIP(VALUE_TEXT_MIN_WIDTH_DIP), -1));
    assert( ptr );
}

void CreateValueTextCtrl( std::unique_ptr<wxSpinCtrl>& ptr, wxWindow* pParent, const PhrozenParamControl& eType )
{
    ptr = std::make_unique<wxSpinCtrl>( pParent, (int)eType, "", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS | wxTE_PROCESS_ENTER, 0, 100, 0  );

    // kill focus and press enter will trigger event
    //ptr->Bind(wxEVT_SPINCTRL, [&](wxSpinEvent& event ){
    //    OnPrintParameterChanged( (PhrozenParamControl)event.GetId(), event.GetValue() );
    //    //m_pMainPanel->SetFocus();
    //    //ptr->Navigate();
    //});

    //ptr->Bind(wxEVT_KILL_FOCUS, [&](wxFocusEvent& event ){
    //    OnPrintParameterChanged( (PhrozenParamControl)event.GetId(), ptr->GetValue() );
    //    //m_pMainPanel->SetFocus();
    //    //ptr->Navigate();
    //    event.Skip();
    //});

    //ptr->Bind(wxEVT_TEXT_ENTER, [&](wxCommandEvent& event ){
    //    OnPrintParameterChanged( (PhrozenParamControl)event.GetId(), ptr->GetValue() );
    //    //m_pMainPanel->SetFocus();
    //    //ptr->Navigate();
    //    event.Skip();
    //});
    assert( ptr );
}

wxFlexGridSizer* PhrozenStatusBasePanel::GenNozzleTempControllor(wxWindow* pParent)
{
    auto sizerFlex = new wxFlexGridSizer(1, 6, wxSize(0, 5));
    sizerFlex->AddGrowableCol(1);
    sizerFlex->AddGrowableRow(0);  // Allow row to expand vertically

    sizerFlex->Add( new wxStaticBitmap(pParent, wxID_ANY, m_Nozzle_temp.bmp() ), 0, wxALL | wxALIGN_CENTER_VERTICAL, 10 );
    sizerFlex->Add( new wxStaticText(pParent, wxID_ANY, "Nozzle"), 1, wxLEFT | wxALIGN_CENTER_VERTICAL, 10 );

    CreateValueText( m_spTemp_nozzle, pParent );
    sizerFlex->Add( m_spTemp_nozzle.get(), 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 0 );

    sizerFlex->Add( new wxStaticBitmap(pParent, wxID_ANY, m_ParamSeparator.bmp() ), 0, wxALL | wxALIGN_CENTER_VERTICAL, 5 );

    CreateValueTextCtrl( m_spTemp_nozzle_ctrl, pParent, PhrozenParamControl::Temperature_Nozzle );
    sizerFlex->Add( m_spTemp_nozzle_ctrl.get(), 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 0 );

    sizerFlex->Add( new wxStaticBitmap(pParent, wxID_ANY, m_Dot_c.bmp() ), 0, wxALL | wxALIGN_CENTER_VERTICAL, 10 );

    return sizerFlex;
}

wxFlexGridSizer* PhrozenStatusBasePanel::GenHeatedBedTempControllor(wxWindow* pParent)
{
    auto sizerFlex = new wxFlexGridSizer(1, 6, wxSize(0, 5));
    sizerFlex->AddGrowableCol(1);
    sizerFlex->AddGrowableRow(0);  // Allow row to expand vertically

    sizerFlex->Add( new wxStaticBitmap(pParent, wxID_ANY, m_Heated_bed_temp.bmp() ), 0, wxALL | wxALIGN_CENTER_VERTICAL, 10 );
    sizerFlex->Add( new wxStaticText(pParent, wxID_ANY, "Heated Bed"), 1, wxLEFT | wxALIGN_CENTER_VERTICAL, 10 );

    CreateValueText( m_spTemp_heatedBed, pParent ); 
    sizerFlex->Add( m_spTemp_heatedBed.get(), 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 0 );

    sizerFlex->Add( new wxStaticBitmap(pParent, wxID_ANY, m_ParamSeparator.bmp() ), 0, wxALL | wxALIGN_CENTER_VERTICAL, 5 );

    CreateValueTextCtrl( m_spTemp_heatedBed_ctrl, pParent, PhrozenParamControl::Temperature_HeatedBed ); 
    sizerFlex->Add( m_spTemp_heatedBed_ctrl.get(), 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 0 );

    sizerFlex->Add( new wxStaticBitmap(pParent, wxID_ANY, m_Dot_c.bmp() ), 0, wxALL | wxALIGN_CENTER_VERTICAL, 10 );

    return sizerFlex;
}

wxBoxSizer* PhrozenStatusBasePanel::GenSpeed_PrintLevel(wxWindow* pParent)
{
    // Use wxBoxSizer for vertical layout with even spacing (3 equal parts)
    auto sizerBox = new wxBoxSizer(wxVERTICAL);

    // Top spacing
    sizerBox->AddStretchSpacer(1);

    // Title row - aligned with other blocks (icon: wxALL 10, text: wxLEFT 10)
    auto title = new wxBoxSizer(wxHORIZONTAL);
    title->Add(new wxStaticBitmap(pParent, wxID_ANY, m_Speed.bmp()), 0, wxALL | wxALIGN_CENTER_VERTICAL, 10);
    title->Add(new wxStaticText(pParent, wxID_ANY, "Print"), 1, wxLEFT | wxALIGN_CENTER_VERTICAL, 10);
    sizerBox->Add(title, 0, wxEXPAND, 0);

    // Middle spacing (between title and buttons)
    sizerBox->AddStretchSpacer(1);

    // Button row - text above radio button, percentage below
    wxBoxSizer* buttonRow = new wxBoxSizer(wxHORIZONTAL);
    
    bool bIsFirst = true;
    auto fnCreateLabeledRadioButton = [&](const std::string& label, const PhrozenPrintSpeed eSpeedType, const std::string& percentage) {
        wxBoxSizer* itemSizer = new wxBoxSizer(wxVERTICAL);
        
        // Label on top (mode name)
        wxStaticText* text = new wxStaticText(pParent, wxID_ANY, label);
        text->SetFont(wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        itemSizer->Add(text, 0, wxALIGN_CENTER_HORIZONTAL, 0);
        
        // RadioButton in the middle (no label, only the circle)
        wxRadioButton* btn = bIsFirst 
            ? new wxRadioButton(pParent, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxRB_GROUP)
            : new wxRadioButton(pParent, wxID_ANY, "");
        itemSizer->Add(btn, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, 2);
        
        // Percentage label at the bottom (smaller font, gray color)
        wxStaticText* percentText = new wxStaticText(pParent, wxID_ANY, percentage);
        percentText->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        percentText->SetForegroundColour(wxColour(128, 128, 128)); // Gray color
        itemSizer->Add(percentText, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, 2);
        
        m_kPrintSpeedButtons.insert({eSpeedType, btn});
        buttonRow->Add(itemSizer, 0, wxLEFT | wxRIGHT, 8);
        
        bIsFirst = false;
    };

    fnCreateLabeledRadioButton("Silent", PhrozenPrintSpeed::Silent, "50%");
    fnCreateLabeledRadioButton("Quiet", PhrozenPrintSpeed::Quite, "80%");
    fnCreateLabeledRadioButton("Standard", PhrozenPrintSpeed::Standard, "100%");
    fnCreateLabeledRadioButton("Fast", PhrozenPrintSpeed::Fast, "120%");
    fnCreateLabeledRadioButton("Turbo", PhrozenPrintSpeed::Turbo, "150%");

    m_kPrintSpeedButtons[PhrozenPrintSpeed::Standard]->SetValue(true);
    sizerBox->Add(buttonRow, 0, wxALL, 5);

    // Bottom spacing
    sizerBox->AddStretchSpacer(1);

    return sizerBox;
}

wxFlexGridSizer* PhrozenStatusBasePanel::GenCooling_Auxiliary(wxWindow* pParent)
{
    auto sizerFlex = new wxFlexGridSizer(1, 6, wxSize(0, 5));
    sizerFlex->AddGrowableCol(1);

    sizerFlex->Add( new wxStaticBitmap(pParent, wxID_ANY, m_Fan.bmp() ), 0, wxALL | wxALIGN_CENTER_VERTICAL, 10 );
    sizerFlex->Add( new wxStaticText(pParent, wxID_ANY, "Auxiliary"), 1, wxLEFT | wxALIGN_CENTER_VERTICAL, 10 );

    CreateValueText( m_spCooling_auxiliary, pParent ); 
    sizerFlex->Add( m_spCooling_auxiliary.get(), 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 0 );

    sizerFlex->Add( new wxStaticBitmap(pParent, wxID_ANY, m_ParamSeparator.bmp() ), 0, wxALL | wxALIGN_CENTER_VERTICAL, 5 );

    CreateValueTextCtrl( m_spCooling_auxiliary_ctrl, pParent, PhrozenParamControl::Cooling_Auxiliary ); 
    sizerFlex->Add( m_spCooling_auxiliary_ctrl.get(), 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 0 );

    sizerFlex->Add( new wxStaticBitmap(pParent, wxID_ANY, m_Percent.bmp() ), 0, wxALL | wxALIGN_CENTER_VERTICAL, 10 );

    return sizerFlex;
}

wxFlexGridSizer* PhrozenStatusBasePanel::GenCooling_Part(wxWindow* pParent)
{
    auto sizerFlex = new wxFlexGridSizer(1, 6, wxSize(0, 5));
    sizerFlex->AddGrowableCol(1);

    sizerFlex->Add( new wxStaticBitmap(pParent, wxID_ANY, m_Fan.bmp() ), 0, wxALL | wxALIGN_CENTER_VERTICAL, 10 );
    sizerFlex->Add( new wxStaticText(pParent, wxID_ANY, "Part"), 1, wxLEFT | wxALIGN_CENTER_VERTICAL, 10 );

    CreateValueText( m_spCooling_part, pParent ); 
    sizerFlex->Add( m_spCooling_part.get(), 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 0 );

    sizerFlex->Add( new wxStaticBitmap(pParent, wxID_ANY, m_ParamSeparator.bmp() ), 0, wxALL | wxALIGN_CENTER_VERTICAL, 5 );

    CreateValueTextCtrl( m_spCooling_part_ctrl, pParent, PhrozenParamControl::Cooling_Part ); 
    sizerFlex->Add( m_spCooling_part_ctrl.get(), 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 0 );

    sizerFlex->Add( new wxStaticBitmap(pParent, wxID_ANY, m_Percent.bmp() ), 0, wxALL | wxALIGN_CENTER_VERTICAL, 10 );

    return sizerFlex;
}

wxFlexGridSizer* PhrozenStatusBasePanel::GenCooling_Shield(wxWindow* pParent)
{
    auto sizerFlex = new wxFlexGridSizer(1, 6, wxSize(0, 5));
    sizerFlex->AddGrowableCol(1);

    sizerFlex->Add( new wxStaticBitmap(pParent, wxID_ANY, m_Fan.bmp() ), 0, wxALL | wxALIGN_CENTER_VERTICAL, 10 );
    sizerFlex->Add( new wxStaticText(pParent, wxID_ANY, "Shield"), 1, wxLEFT | wxALIGN_CENTER_VERTICAL, 10 );

    CreateValueText( m_spCooling_shield, pParent ); 
    sizerFlex->Add( m_spCooling_shield.get(), 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 0 );

    sizerFlex->Add( new wxStaticBitmap(pParent, wxID_ANY, m_ParamSeparator.bmp() ), 0, wxALL | wxALIGN_CENTER_VERTICAL, 5 );

    CreateValueTextCtrl( m_spCooling_shield_ctrl, pParent, PhrozenParamControl::Cooling_Shield ); 
    sizerFlex->Add( m_spCooling_shield_ctrl.get(), 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 0 );

    sizerFlex->Add( new wxStaticBitmap(pParent, wxID_ANY, m_Percent.bmp() ), 0, wxALL | wxALIGN_CENTER_VERTICAL, 10 );

    return sizerFlex;
}

wxSizer* PhrozenStatusBasePanel::GenManualAdjustment_moveRange( wxWindow* pParent )
{
    auto sizer = new wxGridSizer(4, 1, wxSize(1, 1));  // Reduced gap from 2 to 1 (half)
    sizer->Add( new wxPanel(pParent), 0, wxALL | wxEXPAND, 0 );

    auto fnCreateToggleButton =[&] ( const std::string& strName,
                                     const PhrozenNozzleMoveRange eType ) -> void
    {
        auto spButton = new wxToggleButton( pParent, wxID_ANY, strName, wxDefaultPosition, wxSize(100, 36) );
        sizer->Add(spButton, 0, wxALL, 1);  // Reduced margin from 2 to 1 (half)
        m_kNozzleMovementRangeButtons.insert( { eType, spButton } );
    };

    fnCreateToggleButton( "0.1mm", PhrozenNozzleMoveRange::Range_01_MM );
    fnCreateToggleButton( "1mm", PhrozenNozzleMoveRange::Range_1_MM );
    fnCreateToggleButton( "10mm", PhrozenNozzleMoveRange::Range_10_MM );
    m_kNozzleMovementRangeButtons[ PhrozenNozzleMoveRange::Range_01_MM ]->SetValue( true );
    return sizer;
}

wxSizer* PhrozenStatusBasePanel::GenManualAdjustment_move_xy( wxWindow* pParent )
{
    wxBitmapButton* pButton{nullptr};
    auto sizer = new wxGridSizer(4, 3, wxSize(5, 5));

    // Row 0: empty | title | empty (smaller title to match Z)
    sizer->Add( new wxPanel(pParent), 0, wxALL, 0 );
    wxSize nozzleBmpTitleSize = m_Control_xy_title.GetBmpSize();
    auto* titleBitmap = new wxStaticBitmap(pParent, wxID_ANY, m_Control_xy_title.bmp(), wxDefaultPosition, nozzleBmpTitleSize);
    titleBitmap->SetMinSize(nozzleBmpTitleSize);
    sizer->Add( titleBitmap, 0, wxALIGN_CENTER, 0 );
    sizer->Add( new wxPanel(pParent), 0, wxALL, 0 );

    // Row 1: empty | up | empty
    sizer->Add( new wxPanel(pParent), 0, wxALL, 0 );
    pButton = CreateManualMovementButton( pParent, m_Control_xy_up.bmp(), PhrozenMovement::Nozzle_Y_Positive );
    pButton->SetMinSize(wxSize(28, 28));
    pButton->SetMaxSize(wxSize(28, 28));
    sizer->Add( pButton, 0, wxALIGN_CENTER, 0 );
    sizer->Add( new wxPanel(pParent), 0, wxALL, 0 );

    // Row 2: left | home | right
    pButton = CreateManualMovementButton( pParent, m_Control_xy_left.bmp(), PhrozenMovement::Nozzle_X_Negative );
    pButton->SetMinSize(wxSize(28, 28));
    pButton->SetMaxSize(wxSize(28, 28));
    sizer->Add( pButton, 0, wxALIGN_CENTER, 0 );
    pButton = CreateManualMovementButton( pParent, m_Control_xy_home.bmp(), PhrozenMovement::Nozzle_Home_XY );
    pButton->SetMinSize(wxSize(28, 28));
    pButton->SetMaxSize(wxSize(28, 28));
    sizer->Add( pButton, 0, wxALIGN_CENTER, 0 );
    pButton = CreateManualMovementButton( pParent, m_Control_xy_right.bmp(), PhrozenMovement::Nozzle_X_Positive );
    pButton->SetMinSize(wxSize(28, 28));
    pButton->SetMaxSize(wxSize(28, 28));
    sizer->Add( pButton, 0, wxALIGN_CENTER, 0 );

    // Row 3: empty | down | empty
    sizer->Add( new wxPanel(pParent), 0, wxALL, 0 );
    pButton = CreateManualMovementButton( pParent, m_Control_xy_down.bmp(), PhrozenMovement::Nozzle_Y_Negative );
    pButton->SetMinSize(wxSize(28, 28));
    pButton->SetMaxSize(wxSize(28, 28));
    sizer->Add( pButton, 0, wxALIGN_CENTER, 0 );
    sizer->Add( new wxPanel(pParent), 0, wxALL, 0 );
    
    return sizer;
}

wxSizer* PhrozenStatusBasePanel::GenManualAdjustment_move_z( wxWindow* pParent )
{
    wxBitmapButton* pButton{nullptr};
    auto sizer = new wxGridSizer(4, 1, wxSize(5, 5));
    sizer->SetMinSize(wxSize(46, -1));  // Ensure minimum width for Z control

    // Row 0: Z title (smaller to match XY title visually) - use bitmap's actual size
    wxSize zTitleBmpSize = m_Control_z_title.GetBmpSize();
    std::cout << "[Z Title Bitmap] Size: width=" << zTitleBmpSize.GetWidth() << ", height=" << zTitleBmpSize.GetHeight() << std::endl;
    auto* titleBitmap = new wxStaticBitmap(pParent, wxID_ANY, m_Control_z_title.bmp(), wxDefaultPosition, zTitleBmpSize);
    titleBitmap->SetMinSize(zTitleBmpSize);
    sizer->Add( titleBitmap, 0, wxALIGN_CENTER, 0 );

    // Row 1: Z+ up button
    pButton = CreateManualMovementButton( pParent, m_Control_xy_up.bmp(), PhrozenMovement::Nozzle_Z_Positive );
    pButton->SetMinSize(wxSize(28, 28));
    pButton->SetMaxSize(wxSize(28, 28));
    pButton->Show();
    sizer->Add( pButton, 0, wxALIGN_CENTER, 0 );

    // Row 2: Nozzle icon (slightly larger) - use bitmap's actual size
    wxSize nozzleBmpSize = m_Control_z_nozzle.GetBmpSize();
    auto* nozzleBitmap = new wxStaticBitmap( pParent, wxID_ANY, m_Control_z_nozzle.bmp(), wxDefaultPosition, nozzleBmpSize );
    nozzleBitmap->SetMinSize(nozzleBmpSize);
    sizer->Add( nozzleBitmap, 0, wxALIGN_CENTER, 0 );
    
    // Row 3: Z- down button
    pButton = CreateManualMovementButton( pParent, m_Control_xy_down.bmp(), PhrozenMovement::Nozzle_Z_Negative );
    pButton->SetMinSize(wxSize(28, 28));
    pButton->SetMaxSize(wxSize(28, 28));
    pButton->Show();
    sizer->Add( pButton, 0, wxALIGN_CENTER, 0 );
    
    return sizer;
}

wxSizer* PhrozenStatusBasePanel::GenManualAdjustment_z_offset( wxWindow* pParent )
{
    wxBitmapButton* pButton{nullptr};
    // Increased horizontal gap (40px) to separate 0.005/0.01 and 0.05/0.1 button groups, and increase spacing between buttons in same group
    // Reduced vertical gap (2px) to bring upper and lower button groups closer
    auto sizer = new wxGridSizer(2, 3, wxSize(40, 2));

    auto fnCreateToggleButton =[&] ( const std::string& strName,
                                     const PhrozenPrintNozzleOffsetRange eType ) -> void
    {
        // Same size as moveRange buttons: wxSize(100, 36)
        auto spButton = new wxToggleButton( pParent, wxID_ANY, strName, wxDefaultPosition, wxSize(100, 36) );
        sizer->Add(spButton, 0, wxALL, 1);  // Reduced margin to match moveRange buttons
        m_kNozzleOffsetRangeButtons.insert( { eType, spButton } );
    };

    // Add 0.005mm button
    auto spButton_0005 = new wxToggleButton( pParent, wxID_ANY, "0.005mm", wxDefaultPosition, wxSize(100, 36) );
    sizer->Add(spButton_0005, 0, wxALL, 1);
    m_kNozzleOffsetRangeButtons.insert( { PhrozenPrintNozzleOffsetRange::Range_0005_MM, spButton_0005 } );
    
    // Add 0.01mm button with extra left spacing to increase gap from 0.005mm
    auto spButton_001 = new wxToggleButton( pParent, wxID_ANY, "0.01mm", wxDefaultPosition, wxSize(100, 36) );
    auto wrapper_001 = new wxBoxSizer(wxHORIZONTAL);
    wrapper_001->AddSpacer(FromDIP(15));  // Extra left spacing to increase gap
    wrapper_001->Add(spButton_001, 0, wxALIGN_CENTER_VERTICAL, 0);
    sizer->Add(wrapper_001, 0, wxALL, 1);
    m_kNozzleOffsetRangeButtons.insert( { PhrozenPrintNozzleOffsetRange::Range_001_MM, spButton_001 } );

    pButton = CreateManualMovementButton( pParent, m_Control_xy_up.bmp(), PhrozenMovement::Nozzle_Offset_Positive );
    pButton->SetMinSize(wxSize(28, 28));
    pButton->SetMaxSize(wxSize(28, 28));
    sizer->Add( pButton, 0, wxALIGN_CENTER, 0 );

    // Add 0.05mm button
    auto spButton_005 = new wxToggleButton( pParent, wxID_ANY, "0.05mm", wxDefaultPosition, wxSize(100, 36) );
    sizer->Add(spButton_005, 0, wxALL, 1);
    m_kNozzleOffsetRangeButtons.insert( { PhrozenPrintNozzleOffsetRange::Range_005_MM, spButton_005 } );
    
    // Add 0.1mm button with extra left spacing to increase gap from 0.05mm
    auto spButton_01 = new wxToggleButton( pParent, wxID_ANY, "0.1mm", wxDefaultPosition, wxSize(100, 36) );
    auto wrapper_01 = new wxBoxSizer(wxHORIZONTAL);
    wrapper_01->AddSpacer(FromDIP(15));  // Extra left spacing to increase gap
    wrapper_01->Add(spButton_01, 0, wxALIGN_CENTER_VERTICAL, 0);
    sizer->Add(wrapper_01, 0, wxALL, 1);
    m_kNozzleOffsetRangeButtons.insert( { PhrozenPrintNozzleOffsetRange::Range_01_MM, spButton_01 } );

    pButton = CreateManualMovementButton( pParent, m_Control_xy_down.bmp(), PhrozenMovement::Nozzle_Offset_Negative );
    pButton->SetMinSize(wxSize(28, 28));
    pButton->SetMaxSize(wxSize(28, 28));
    sizer->Add( pButton, 0, wxALIGN_CENTER, 0 );

    m_kNozzleOffsetRangeButtons[ PhrozenPrintNozzleOffsetRange::Range_0005_MM ]->SetValue( true );
    
    // Wrap sizer in a horizontal sizer to add left spacing
    auto wrapperSizer = new wxBoxSizer(wxHORIZONTAL);
    wrapperSizer->AddSpacer(FromDIP(20));  // Left spacing to shift buttons to the right
    wrapperSizer->Add(sizer, 0, wxALIGN_CENTER_VERTICAL, 0);
    return wrapperSizer;
}

wxBitmapButton* PhrozenStatusBasePanel::CreateManualMovementButton( wxWindow* pParent,
                                                                    wxBitmap& kIcon, 
                                                                    const PhrozenMovement eType )
{
    auto pButton =  new wxBitmapButton(pParent, wxID_ANY, kIcon, wxDefaultPosition, wxSize(28, 28), wxBU_AUTODRAW | wxBORDER_NONE );
    m_kManualMovementButtons.insert( { eType, pButton } );
    return pButton;
}

// control panel
void PhrozenStatusBasePanel::update_nozzle_current_temp( int nTemp )
{
    m_spTemp_nozzle->SetLabelText( std::to_string( nTemp ) );
}

void PhrozenStatusBasePanel::update_nozzle_target_temp( int nTemp )
{
    m_spTemp_nozzle_ctrl->SetValue( nTemp );
}

void PhrozenStatusBasePanel::update_bed_current_temp( int nTemp )
{
    m_spTemp_heatedBed->SetLabelText( std::to_string( nTemp ) );
}

void PhrozenStatusBasePanel::update_bed_target_temp( int nTemp )
{
    m_spTemp_heatedBed_ctrl->SetValue( nTemp );
}

void PhrozenStatusBasePanel::update_cooling_auxiliary_current_power( int nPower )
{
    m_spCooling_auxiliary->SetLabelText( std::to_string( nPower ) );
}

void PhrozenStatusBasePanel::update_cooling_auxiliary_target_power( int nPower )
{
    m_spCooling_auxiliary_ctrl->SetValue( nPower );
}

void PhrozenStatusBasePanel::update_cooling_part_current_power( int nPower )
{
    m_spCooling_part->SetLabelText( std::to_string( nPower ) );
}

void PhrozenStatusBasePanel::update_cooling_part_target_power( int nPower )
{
    m_spCooling_part_ctrl->SetValue( nPower );
}

void PhrozenStatusBasePanel::update_cooling_shield_current_power( int nPower )
{
    m_spCooling_shield->SetLabelText( std::to_string( nPower ) );
}

void PhrozenStatusBasePanel::update_cooling_shield_target_power( int nPower )
{
    m_spCooling_shield_ctrl->SetValue( nPower );
}

void PhrozenStatusBasePanel::update_print_speed_level( PhrozenPrintSpeed eLevel )
{
    auto pItem = m_kPrintSpeedButtons.find( eLevel );
    if ( pItem == m_kPrintSpeedButtons.end() )
    {
        assert( 0 && "not implement" );
        return;
    }
    pItem->second->SetValue( true );
}

PhrozenPrintSpeed PhrozenStatusBasePanel::print_speed_percent_to_enum( float fPercentage )
{
    int print_speed = (int) (fPercentage * 100.0f);
    PhrozenPrintSpeed eLevel = PhrozenPrintSpeed::Standard;
    if ( print_speed <= 50 ) eLevel = PhrozenPrintSpeed::Silent;
    else if ( print_speed > 50 && print_speed <= 80 ) eLevel = PhrozenPrintSpeed::Quite;
    else if ( print_speed > 80 && print_speed <= 100 ) eLevel = PhrozenPrintSpeed::Standard;
    else if ( print_speed > 100 && print_speed <= 120 ) eLevel = PhrozenPrintSpeed::Fast;
    else if ( print_speed > 120 && print_speed <= 150 ) eLevel = PhrozenPrintSpeed::Turbo;
    else { assert( 0 && "out of range" ); eLevel = PhrozenPrintSpeed::Turbo; }
    return eLevel;
}

float PhrozenStatusBasePanel::print_speed_enum_to_percent( PhrozenPrintSpeed eLevel )
{
    float fPercentage = 0.9f;
    switch( eLevel )
    {
        case PhrozenPrintSpeed::Silent: fPercentage = 0.5f; break;
        case PhrozenPrintSpeed::Quite: fPercentage = 0.8f; break;
        case PhrozenPrintSpeed::Standard: fPercentage = 1.0f; break;
        case PhrozenPrintSpeed::Fast: fPercentage = 1.2f; break;
        case PhrozenPrintSpeed::Turbo: fPercentage = 1.5f; break;
        default:
            assert( 0 && "not implement" );
    }
    return fPercentage;
}

float PhrozenStatusBasePanel::get_selected_nozzle_movement_range()
{
    for ( auto kItem : m_kNozzleMovementRangeButtons )
    {
        if ( kItem.second->GetValue() )
        {
            if ( kItem.second->GetValue() )
            {
                switch( kItem.first )
                {
                    case PhrozenNozzleMoveRange::Range_01_MM:   return 0.1f; break;
                    case PhrozenNozzleMoveRange::Range_1_MM:    return 1.0f; break;
                    case PhrozenNozzleMoveRange::Range_10_MM:   return 10.0f; break;
                }
            }
        }
    }
    assert( 0 && "has wrong, no selected? " );
    return 0.0f;
}

float PhrozenStatusBasePanel::get_selected_nozzle_offset_range()
{
    for ( auto kItem : m_kNozzleOffsetRangeButtons )
    {
        if ( kItem.second->GetValue() )
        {
            switch( kItem.first )
            {
                case PhrozenPrintNozzleOffsetRange::Range_0005_MM:   return 0.005f; break;
                case PhrozenPrintNozzleOffsetRange::Range_001_MM:    return 0.01f; break;
                case PhrozenPrintNozzleOffsetRange::Range_005_MM:    return 0.05f; break;
                case PhrozenPrintNozzleOffsetRange::Range_01_MM:     return 0.1f; break;
            }
        }
    }
    assert( 0 && "has wrong, no selected? " );
    return 0.0f;
}

#pragma endregion




#pragma region PhrozenStatusPanel
void PhrozenStatusPanel::update_camera_state(MachineObject* obj)
{
    if (!obj)
        return;

    // m_bitmap_sdcard_abnormal_img->SetToolTip(_L("SD Card Abnormal"));
    // sdcard
    if (m_last_sdcard != (int) obj->get_sdcard_state()) {
        if (obj->get_sdcard_state() == MachineObject::SdcardState::NO_SDCARD) {
        } else if (obj->get_sdcard_state() == MachineObject::SdcardState::HAS_SDCARD_NORMAL) {
        } else if (obj->get_sdcard_state() == MachineObject::SdcardState::HAS_SDCARD_ABNORMAL) {
        } else {
        }
        m_last_sdcard = (int) obj->get_sdcard_state();
    }

    // recording
    if (m_last_recording != (obj->is_recording() ? 1 : 0)) {
        if (obj->is_recording()) {
        } else {
        }
        m_last_recording = obj->is_recording() ? 1 : 0;
    }


    // timelapse
    if (obj->is_support_timelapse) {
        if (m_last_timelapse != (obj->is_timelapse() ? 1 : 0)) {
            if (obj->is_timelapse()) {
            } else {
            }
            m_last_timelapse = obj->is_timelapse() ? 1 : 0;
        }
    } else {
    }

    // vcamera
    //if (obj->virtual_camera) {
    //    if (m_last_vcamera != (m_media_play_ctrl->IsStreaming() ? 1 : 0)) {
    //        if (m_media_play_ctrl->IsStreaming()) {
    //        } else {
    //        }
    //        m_last_vcamera = m_media_play_ctrl->IsStreaming() ? 1 : 0;
    //    }
    //} else {
    //}

    // camera setting
    //if (m_camera_popup && m_camera_popup->IsShown()) {
    //    bool show_vcamera = m_media_play_ctrl->IsStreaming();
    //    m_camera_popup->update(show_vcamera);
    //}

    m_panel_monitoring_title->Layout();
}

PhrozenStatusPanel::PhrozenStatusPanel(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxString& name)
    : PhrozenStatusBasePanel(parent, id, pos, size, style)
    , m_fan_control_popup(new FanControlPopup(this))
{
    init_scaled_buttons();
    m_buttons.push_back(m_button_unload);
    m_buttons.push_back(m_bpButton_z_10);
    m_buttons.push_back(m_bpButton_z_1);
    m_buttons.push_back(m_bpButton_z_down_1);
    m_buttons.push_back(m_bpButton_z_down_10);
    m_buttons.push_back(m_bpButton_e_10);
    m_buttons.push_back(m_bpButton_e_down_10);

    obj = nullptr;
    m_score_data         = new ScoreData;
    m_score_data->rating_id = -1;
    /* set default values */
    #if HideOriginUiWidget
    m_switch_lamp->SetValue(false);
    m_switch_printing_fan->SetValue(false);
    m_switch_nozzle_fan->SetValue(false);
    m_switch_cham_fan->SetValue(false);
    #endif

    /* set default enable state */
    m_project_task_panel->enable_pause_resume_button(false, "resume_disable");
    m_project_task_panel->enable_abort_button(false);


    Bind(wxEVT_WEBREQUEST_STATE, &PhrozenStatusPanel::on_webrequest_state, this);

    Bind(wxCUSTOMEVT_SET_TEMP_FINISH, [this](wxCommandEvent e) {
        PhrozenParamControl eParamType = (PhrozenParamControl)e.GetInt();
        switch( eParamType )
        {
            case PhrozenParamControl::Temperature_Nozzle:       on_set_nozzle_temp(); break;
            case PhrozenParamControl::Temperature_HeatedBed:    on_set_bed_temp(); break;
            case PhrozenParamControl::Cooling_Auxiliary:        on_set_cooling_auxiliary(); break;
            case PhrozenParamControl::Cooling_Part:             on_set_cooling_part(); break;
            case PhrozenParamControl::Cooling_Shield:           on_set_cooling_shield(); break;
            default:
                assert( 0 && "not implement" );
        }
    });


#ifdef __APPLE__
    m_spTemp_nozzle_ctrl->GetText()->Connect(wxEVT_KILL_FOCUS, wxFocusEventHandler(PhrozenStatusPanel::on_nozzle_temp_kill_focus), NULL, this);
    m_spTemp_nozzle_ctrl->GetText()->Connect(wxEVT_SET_FOCUS, wxFocusEventHandler(PhrozenStatusPanel::on_nozzle_temp_set_focus), NULL, this);
    m_spTemp_heatedBed_ctrl->GetText()->Connect(wxEVT_KILL_FOCUS, wxFocusEventHandler(PhrozenStatusPanel::on_bed_temp_kill_focus), NULL, this);
    m_spTemp_heatedBed_ctrl->GetText()->Connect(wxEVT_SET_FOCUS, wxFocusEventHandler(PhrozenStatusPanel::on_bed_temp_set_focus), NULL, this);

    m_spCooling_auxiliary_ctrl->GetText()->Connect(wxEVT_KILL_FOCUS, wxFocusEventHandler(PhrozenStatusPanel::on_cooling_auxiliary_kill_focus), NULL, this);
    m_spCooling_auxiliary_ctrl->GetText()->Connect(wxEVT_SET_FOCUS, wxFocusEventHandler(PhrozenStatusPanel::on_cooling_auxiliary_set_focus), NULL, this);

    m_spCooling_part_ctrl->GetText()->Connect(wxEVT_KILL_FOCUS, wxFocusEventHandler(PhrozenStatusPanel::on_cooling_part_kill_focus), NULL, this);
    m_spCooling_part_ctrl->GetText()->Connect(wxEVT_SET_FOCUS, wxFocusEventHandler(PhrozenStatusPanel::on_cooling_part_set_focus), NULL, this);

    m_spCooling_shield_ctrl->GetText()->Connect(wxEVT_KILL_FOCUS, wxFocusEventHandler(PhrozenStatusPanel::on_cooling_shield_kill_focus), NULL, this);
    m_spCooling_shield_ctrl->GetText()->Connect(wxEVT_SET_FOCUS, wxFocusEventHandler(PhrozenStatusPanel::on_cooling_shield_set_focus), NULL, this);
#else
    m_spTemp_nozzle_ctrl->Connect(wxEVT_KILL_FOCUS, wxFocusEventHandler(PhrozenStatusPanel::on_nozzle_temp_kill_focus), NULL, this);
    m_spTemp_nozzle_ctrl->Connect(wxEVT_SET_FOCUS, wxFocusEventHandler(PhrozenStatusPanel::on_nozzle_temp_set_focus), NULL, this);
    m_spTemp_heatedBed_ctrl->Connect(wxEVT_KILL_FOCUS, wxFocusEventHandler(PhrozenStatusPanel::on_bed_temp_kill_focus), NULL, this);
    m_spTemp_heatedBed_ctrl->Connect(wxEVT_SET_FOCUS, wxFocusEventHandler(PhrozenStatusPanel::on_bed_temp_set_focus), NULL, this);

    m_spCooling_auxiliary_ctrl->Connect(wxEVT_KILL_FOCUS, wxFocusEventHandler(PhrozenStatusPanel::on_cooling_auxiliary_kill_focus), NULL, this);
    m_spCooling_auxiliary_ctrl->Connect(wxEVT_SET_FOCUS, wxFocusEventHandler(PhrozenStatusPanel::on_cooling_auxiliary_set_focus), NULL, this);

    m_spCooling_part_ctrl->Connect(wxEVT_KILL_FOCUS, wxFocusEventHandler(PhrozenStatusPanel::on_cooling_part_kill_focus), NULL, this);
    m_spCooling_part_ctrl->Connect(wxEVT_SET_FOCUS, wxFocusEventHandler(PhrozenStatusPanel::on_cooling_part_set_focus), NULL, this);

    m_spCooling_shield_ctrl->Connect(wxEVT_KILL_FOCUS, wxFocusEventHandler(PhrozenStatusPanel::on_cooling_shield_kill_focus), NULL, this);
    m_spCooling_shield_ctrl->Connect(wxEVT_SET_FOCUS, wxFocusEventHandler(PhrozenStatusPanel::on_cooling_shield_set_focus), NULL, this);

#endif
    //m_tempCtrl_chamber->Connect(wxEVT_KILL_FOCUS, wxFocusEventHandler(PhrozenStatusPanel::on_cham_temp_kill_focus), NULL, this);
    //m_tempCtrl_chamber->Connect(wxEVT_SET_FOCUS, wxFocusEventHandler(PhrozenStatusPanel::on_cham_temp_set_focus), NULL, this);

    for ( auto kItem : m_kPrintSpeedButtons )
    {
        kItem.second->Bind( wxEVT_RADIOBUTTON, [=](wxCommandEvent& WXUNUSED(event)){ 
            on_print_speed_changed( kItem.first ); 
        } );
    }

    for ( auto kItem : m_kNozzleMovementRangeButtons )
    {
        kItem.second->Bind(wxEVT_LEFT_DOWN, &PhrozenStatusPanel::on_nozzle_movement_range_mouse_left_down, this);
    }

    for ( auto kItem : m_kNozzleOffsetRangeButtons )
    {
        kItem.second->Bind(wxEVT_LEFT_DOWN, &PhrozenStatusPanel::on_nozzle_offset_range_mouse_left_down, this);
    }

    for ( auto kItem : m_kManualMovementButtons )
    {
        kItem.second->Bind( wxEVT_BUTTON, [=](wxCommandEvent& WXUNUSED(event)){ 
            on_manual_movement_changed( kItem.first ); 
        } );
    }


    m_project_task_panel->get_bitmap_thumbnail()->Connect(wxEVT_LEFT_DOWN, wxMouseEventHandler(PhrozenStatusPanel::refresh_thumbnail_webrequest), NULL, this);
    m_project_task_panel->get_pause_resume_button()->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_subtask_pause_resume), NULL, this);
    m_project_task_panel->get_abort_button()->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_subtask_abort), NULL, this);
    m_project_task_panel->get_market_scoring_button()->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_market_scoring), NULL, this);
    m_project_task_panel->get_market_retry_buttom()->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_market_retry), NULL, this); 
    m_project_task_panel->get_clean_button()->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_print_error_clean), NULL, this);




    #if HideOriginUiWidget
    m_pCam_switch_button->Connect(wxEVT_LEFT_DOWN, wxMouseEventHandler(PhrozenStatusPanel::on_camera_enter), NULL, this);
    m_pCam_switch_button->Connect(wxEVT_LEFT_DCLICK, wxMouseEventHandler(PhrozenStatusPanel::on_camera_enter), NULL, this);
    m_switch_lamp->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_lamp_switch), NULL, this);
    m_switch_nozzle_fan->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_nozzle_fan_switch), NULL, this); // TODO
    m_switch_printing_fan->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_nozzle_fan_switch), NULL, this);
    m_switch_cham_fan->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_nozzle_fan_switch), NULL, this); 
    m_phButton_xy->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_axis_ctrl_xy), NULL, this); // TODO
    m_bpButton_z_10->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_axis_ctrl_z_up_10), NULL, this);
    m_bpButton_z_1->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_axis_ctrl_z_up_1), NULL, this);
    m_bpButton_z_down_1->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_axis_ctrl_z_down_1), NULL, this);
    m_bpButton_z_down_10->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_axis_ctrl_z_down_10), NULL, this);
    m_bpButton_e_10->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_axis_ctrl_e_up_10), NULL, this);
    m_bpButton_e_down_10->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_axis_ctrl_e_down_10), NULL, this);
    m_button_unload->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_start_unload), NULL, this);
    #endif
    // record some event created but not use here, maybe future will use it.
    /*
        EVT_AMS_LOAD
        EVT_AMS_UNLOAD
        EVT_AMS_EXTRUSION_CALI
        EVT_AMS_FILAMENT_BACKUP
        EVT_AMS_SETTINGS
        EVT_AMS_REFRESH_RFID
        EVT_AMS_ON_SELECTED
        EVT_AMS_ON_FILAMENT_EDIT
        EVT_VAMS_ON_FILAMENT_EDIT
        EVT_AMS_RETRY
        EVT_LOAD_VAMS_TRAY
    */

    Bind(EVT_AMS_GUIDE_WIKI, &PhrozenStatusPanel::on_ams_guide, this);
    Bind(EVT_FAN_CHANGED, &PhrozenStatusPanel::on_fan_changed, this);
    Bind(EVT_SECONDARY_CHECK_DONE, &PhrozenStatusPanel::on_print_error_done, this);
    Bind(EVT_SECONDARY_CHECK_RESUME, &PhrozenStatusPanel::on_subtask_pause_resume, this);
    Bind(EVT_PRINT_ERROR_STOP, &PhrozenStatusPanel::on_subtask_abort, this);
    Bind(EVT_JUMP_TO_LIVEVIEW, [this](wxCommandEvent& e) {
        assert( 0 );
        //m_media_play_ctrl->jump_to_play();
        //if (m_print_error_dlg)
        //    m_print_error_dlg->on_hide();
    });

    m_calibration_btn->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_start_calibration), NULL, this);

    #if HideOriginUiWidget
    m_switch_speed->Connect(wxEVT_LEFT_DOWN, wxCommandEventHandler(PhrozenStatusPanel::on_switch_speed), NULL, this);
    #endif
}

PhrozenStatusPanel::~PhrozenStatusPanel()
{
    // Disconnect Events
    //m_project_task_panel->get_bitmap_thumbnail()->Disconnect(wxEVT_LEFT_DOWN, wxMouseEventHandler(PhrozenStatusPanel::refresh_thumbnail_webrequest), NULL, this);
    //m_project_task_panel->get_pause_resume_button()->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_subtask_pause_resume), NULL, this);
    //m_project_task_panel->get_abort_button()->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_subtask_abort), NULL, this);
    //m_project_task_panel->get_market_scoring_button()->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_market_scoring), NULL, this);
    //m_project_task_panel->get_market_retry_buttom()->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_market_retry), NULL, this); 
    //m_project_task_panel->get_clean_button()->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_print_error_clean), NULL, this);

    for ( auto kItem : m_kPrintSpeedButtons )
    {
        delete kItem.second;
    }

    for ( auto kItem : m_kNozzleMovementRangeButtons )
    {
        delete kItem.second;
    }

    for ( auto kItem : m_kNozzleOffsetRangeButtons )
    {
        delete kItem.second;
    }

    for ( auto kItem : m_kManualMovementButtons )
    {
        delete kItem.second;
    }
    
    m_calibration_btn->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_start_calibration), NULL, this);

    #if HideOriginUiWidget
    m_pCam_switch_button->Disconnect(wxEVT_LEFT_DOWN, wxMouseEventHandler(PhrozenStatusPanel::on_camera_enter), NULL, this);
    m_pCam_switch_button->Disconnect(wxEVT_LEFT_DCLICK, wxMouseEventHandler(PhrozenStatusPanel::on_camera_enter), NULL, this);
    m_switch_lamp->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_lamp_switch), NULL, this);
    m_switch_nozzle_fan->Disconnect(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_nozzle_fan_switch), NULL, this);
    m_switch_printing_fan->Disconnect(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_nozzle_fan_switch), NULL, this);
    m_switch_cham_fan->Disconnect(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_nozzle_fan_switch), NULL, this);
    m_phButton_xy->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_axis_ctrl_xy), NULL, this);
    m_bpButton_z_10->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_axis_ctrl_z_up_10), NULL, this);
    m_bpButton_z_1->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_axis_ctrl_z_up_1), NULL, this);
    m_bpButton_z_down_1->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_axis_ctrl_z_down_1), NULL, this);
    m_bpButton_z_down_10->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_axis_ctrl_z_down_10), NULL, this);
    m_bpButton_e_10->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_axis_ctrl_e_up_10), NULL, this);
    m_bpButton_e_down_10->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_axis_ctrl_e_down_10), NULL, this);
    m_switch_speed->Disconnect(wxEVT_LEFT_DOWN, wxCommandEventHandler(PhrozenStatusPanel::on_switch_speed), NULL, this);
    m_button_unload->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(PhrozenStatusPanel::on_start_unload), NULL, this);
    #endif
    // remove warning dialogs
    if (m_print_error_dlg != nullptr)
        delete m_print_error_dlg;

    if (abort_dlg != nullptr)
        delete abort_dlg;

    if (ctrl_e_hint_dlg != nullptr)
        delete ctrl_e_hint_dlg;

    if (sdcard_hint_dlg != nullptr)
        delete sdcard_hint_dlg;

    if (m_score_data != nullptr) { 
        delete m_score_data;
    }
}

void PhrozenStatusPanel::init_scaled_buttons()
{
#if HideOriginUiWidget //origin
    m_project_task_panel->init_scaled_buttons();
    m_button_unload->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_unload->SetCornerRadius(FromDIP(12));
    m_bpButton_z_10->SetMinSize(Z_BUTTON_SIZE);
    m_bpButton_z_10->SetCornerRadius(0);
    m_bpButton_z_1->SetMinSize(Z_BUTTON_SIZE);
    m_bpButton_z_1->SetCornerRadius(0);
    m_bpButton_z_down_1->SetMinSize(Z_BUTTON_SIZE);
    m_bpButton_z_down_1->SetCornerRadius(0);
    m_bpButton_z_down_10->SetMinSize(Z_BUTTON_SIZE);
    m_bpButton_z_down_10->SetCornerRadius(0);
    m_bpButton_e_10->SetMinSize(wxSize(FromDIP(40), FromDIP(40)));
    m_bpButton_e_10->SetCornerRadius(FromDIP(12));
    m_bpButton_e_down_10->SetMinSize(wxSize(FromDIP(40), FromDIP(40)));
    m_bpButton_e_down_10->SetCornerRadius(FromDIP(12));
#endif
}

void PhrozenStatusPanel::on_market_scoring(wxCommandEvent &event) { 
    if (obj && obj->is_makeworld_subtask() && obj->rating_info && obj->rating_info->request_successful) { // model is mall model and has rating_id
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": on_market_scoring" ;
        if (m_score_data && m_score_data->rating_id == obj->rating_info->rating_id) { // current score data for model is same as mall model
            if (m_score_data->star_count != m_project_task_panel->get_star_count()) m_score_data->star_count = m_project_task_panel->get_star_count();
            ScoreDialog m_score_dlg(this, m_score_data);
            int ret = m_score_dlg.ShowModal();
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": old data";

            if (ret == wxID_OK) { 
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": old data is upload";
                m_score_data->rating_id = -1;
                m_project_task_panel->set_star_count_dirty(false);
                if (obj) obj->get_model_mall_result_need_retry = true;
                return;
            }
            if (m_score_data != nullptr) {
                delete m_score_data;
                m_score_data = nullptr;
            }
            m_score_data = new ScoreData(m_score_dlg.get_score_data()); // when user do not submit score, store the data for next opening the score dialog
            m_project_task_panel->set_star_count(m_score_data->star_count);
        } else {
            int star_count      = m_project_task_panel->get_star_count_dirty() ? m_project_task_panel->get_star_count() : obj->rating_info->start_count;
            bool        success_print = obj->rating_info->success_printed;
            ScoreDialog m_score_dlg(this, obj->get_modeltask()->design_id, obj->get_modeltask()->model_id, obj->get_modeltask()->profile_id, obj->rating_info->rating_id,
                                    success_print, star_count);
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": new data";

            std::string comment = obj->rating_info->content;
            if (!comment.empty()) { m_score_dlg.set_comment(comment); }
            
            std::vector<std::string> images_json_array;
            images_json_array = obj->rating_info->image_url_paths;
            if (!images_json_array.empty()) m_score_dlg.set_cloud_bitmap(images_json_array);
            
            int ret = m_score_dlg.ShowModal();

            if (ret == wxID_OK) {
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": new data is upload";
                m_score_data->rating_id = -1;
                m_project_task_panel->set_star_count_dirty(false);
                if (obj) obj->get_model_mall_result_need_retry = true;
                return;
            }
            if (m_score_data != nullptr) {
                delete m_score_data;
                m_score_data = nullptr;
            }
            m_score_data = new ScoreData(m_score_dlg.get_score_data());
            m_project_task_panel->set_star_count(m_score_data->star_count);
        }
    }
}

void PhrozenStatusPanel::on_market_retry(wxCommandEvent &event)
{
    if (obj) {
    obj->get_model_mall_result_need_retry = true;
    } else {
        BOOST_LOG_TRIVIAL(info)<< __FUNCTION__ << "retury failed";
    }
}

void PhrozenStatusPanel::on_subtask_pause_resume(wxCommandEvent &event)
{
    // ============================================
    // 防呆檢查：確保物件有效
    // ============================================
#ifdef __APPLE__
    if (!obj){
        obj = wxGetApp().GetPhrozenMachineObject();
    }
#endif
    if (!obj) {
        BOOST_LOG_TRIVIAL(warning) << "PhrozenStatusPanel::on_subtask_pause_resume: obj is nullptr, operation ignored";
        return;
    }
    
    // ============================================
    // 獲取當前列印狀態並執行對應操作
    // ============================================
    bool is_paused = obj->IsPrintPaused();
    
    if (is_paused) {
        // ============================================
        // 執行續印操作
        // ============================================
        BOOST_LOG_TRIVIAL(info) << "PhrozenStatusPanel::on_subtask_pause_resume: "
                                << "Resuming print task, dev_id=" << obj->dev_id;
        
        bool result = obj->SetPhrozenCommand_resume();
        if (!result) {
            BOOST_LOG_TRIVIAL(error) << "PhrozenStatusPanel::on_subtask_pause_resume: "
                                     << "Failed to execute resume command, dev_id=" << obj->dev_id;
        }
    }
    else {
        // ============================================
        // 執行暫停操作
        // ============================================
        BOOST_LOG_TRIVIAL(info) << "PhrozenStatusPanel::on_subtask_pause_resume: "
                                << "Pausing print task, dev_id=" << obj->dev_id;
        
        bool result = obj->SetPhrozenCommand_pause();
        if (!result) {
            BOOST_LOG_TRIVIAL(error) << "PhrozenStatusPanel::on_subtask_pause_resume: "
                                     << "Failed to execute pause command, dev_id=" << obj->dev_id;
        }
    }
    
    // ============================================
    // TODO: 錯誤對話框處理邏輯
    // 需要進一步確認用法與整合可行性
    // ============================================
    // 這段代碼的作用是在暫停/續印時隱藏錯誤對話框，但需要確認：
    // 1. 是否應該在暫停/續印時自動隱藏錯誤對話框？
    // 2. 錯誤對話框可能包含重要信息或操作按鈕，隱藏後用戶可能無法操作
    // 3. 與 on_subtask_abort() 中的處理邏輯是否應該保持一致？
    // 4. 是否應該根據錯誤類型或對話框狀態來決定是否隱藏？
    /*
    if (m_print_error_dlg) {
        m_print_error_dlg->on_hide();
    }
    if (m_print_error_dlg_no_action) {
        m_print_error_dlg_no_action->on_hide();
    }
    */
}

void PhrozenStatusPanel::on_subtask_abort(wxCommandEvent &event)
{
    // ============================================
    // 防呆檢查：確保父視窗有效，保留原本的orca設計不動，但補上註解
    // ============================================
    // 為什麼需要這個檢查？
    // 1. wxWidgets 對話框必須有一個有效的父視窗才能正確顯示和定位
    // 2. 如果父視窗為 nullptr，創建對話框會導致程序崩潰或對話框無法正確顯示
    // 3. 雖然在正常情況下 PhrozenStatusPanel 應該總是有父視窗，但在以下情況可能為 nullptr：
    //    - 視窗正在銷毀過程中
    //    - 視窗創建失敗或異常狀態
    //    - 測試環境或特殊場景
    // 4. 這是防禦性編程的最佳實踐，可以避免程序崩潰並提供清晰的錯誤日誌
    wxWindow* parent = this->GetParent();
    if (!parent) {
        BOOST_LOG_TRIVIAL(error) << "PhrozenStatusPanel::on_subtask_abort: "
                                 << "Parent window is nullptr, cannot create abort dialog";
        return;
    }
    
    // ============================================
    // 創建或獲取取消列印確認對話框（單例模式），保留原本的orca設計不動，但補上註解
    // ============================================
    // 為什麼需要檢查 abort_dlg == nullptr？
    // 1. 單例模式：確保對話框只創建一次，避免重複創建造成資源浪費
    // 2. 避免內存洩漏：如果每次點擊都創建新對話框而不刪除舊的，會導致內存洩漏
    // 3. 避免多個對話框同時顯示：如果對話框已存在，應該復用現有實例而不是創建新的
    // 4. 保持對話框狀態：復用現有對話框可以保留之前的配置和狀態
    // 5. 性能考量：創建對話框是相對成本較高的操作，重復使用可以提升性能
    if (abort_dlg == nullptr) {
        BOOST_LOG_TRIVIAL(debug) << "PhrozenStatusPanel::on_subtask_abort: "
                                 << "Creating new abort confirmation dialog";
        
        abort_dlg = new SecondaryCheckDialog(parent, wxID_ANY, _L("Cancel print"));
        
        // ============================================
        // 綁定確認事件：執行取消列印操作，保留原本的orca設計不動，但補上註解
        // ============================================
        // 為什麼事件綁定只在創建時執行一次？
        // 1. 避免重複綁定：如果每次創建都綁定，會導致同一個事件被多次處理
        // 2. 事件處理器會累積：重複綁定會導致回調函數被多次調用，造成重複執行命令
        // 3. 內存管理：每次綁定都會增加引用計數，重複綁定可能導致內存無法釋放
        abort_dlg->Bind(EVT_SECONDARY_CHECK_CONFIRM, [this](wxCommandEvent &e) {
            // ============================================
            // 防呆檢查：確保物件有效（在回調函數中）
            // ============================================
            // 為什麼在回調函數中還需要檢查 obj？
            // 1. 異步執行：回調函數可能在按鈕點擊後延遲執行，此時 obj 可能已被清空或改變
            // 2. 視窗生命週期：如果 PhrozenStatusPanel 正在銷毀，obj 可能已被設為 nullptr
            // 3. 設備斷線：如果設備斷線，obj 可能被清空
            // 4. 狀態變化：在用戶點擊確認和回調執行之間，obj 可能因為其他操作而改變
            // 5. 防禦性編程：即使對話框是模態的，這個檢查也能防止潛在的崩潰
            // 6. 與其他代碼保持一致：代碼庫中其他類似回調（如 show_error_message）也檢查 obj
#ifdef __APPLE__
            if (!obj){
                obj = wxGetApp().GetPhrozenMachineObject();
            }
#endif
            if (!obj) {
                BOOST_LOG_TRIVIAL(warning) << "PhrozenStatusPanel::on_subtask_abort: "
                                           << "obj is nullptr in confirmation callback, operation ignored";
                return;
            }
            
            BOOST_LOG_TRIVIAL(info) << "PhrozenStatusPanel::on_subtask_abort: "
                                    << "User confirmed abort, executing stop command, dev_id=" << obj->dev_id;
            
            // ============================================
            // 執行取消列印命令並檢查結果
            // ============================================
            // 為什麼需要檢查命令執行結果？
            // 1. 錯誤處理：如果命令執行失敗，需要記錄錯誤日誌以便調試
            // 2. 用戶反饋：雖然這裡沒有直接顯示錯誤給用戶，但日誌可以幫助後續改進
            // 3. 狀態追蹤：了解命令執行成功率，有助於發現系統問題
            bool result = obj->SetPhrozenCommand_abort();
            if (!result) {
                BOOST_LOG_TRIVIAL(error) << "PhrozenStatusPanel::on_subtask_abort: "
                                         << "Failed to execute abort command, dev_id=" << obj->dev_id;
            }
        });
    }
    
    // ============================================
    // 更新對話框文字並顯示
    // ============================================
    // 為什麼每次都要更新文字和顯示？
    // 1. 確保文字是最新的：對話框文字可能需要根據當前狀態更新
    // 2. 確保對話框可見：如果對話框之前被隱藏，需要重新顯示
    // 3. 重置對話框狀態：確保對話框處於正確的初始狀態
    // 4. 用戶體驗：即使對話框已存在，也要確保它正確顯示給用戶
    abort_dlg->update_text(_L("Are you sure you want to cancel this print?"));
    abort_dlg->on_show();
    
    BOOST_LOG_TRIVIAL(debug) << "PhrozenStatusPanel::on_subtask_abort: "
                             << "Abort confirmation dialog shown";
}

void PhrozenStatusPanel::error_info_reset()
{
    m_project_task_panel->error_info_reset();
    before_error_code = 0;
}

void PhrozenStatusPanel::on_print_error_clean(wxCommandEvent &event)
{
    error_info_reset();
    skip_print_error = obj->print_error;
    char buf[32];
    ::sprintf(buf, "%08X", skip_print_error);
    BOOST_LOG_TRIVIAL(info) << "skip_print_error: " << buf;
    before_error_code = obj->print_error;
}

void PhrozenStatusPanel::on_webrequest_state(wxWebRequestEvent &evt)
{
    BOOST_LOG_TRIVIAL(trace) << "monitor: monitor_panel web request state = " << evt.GetState();
    switch (evt.GetState()) {
        case wxWebRequest::State_Completed: {
            if (m_current_print_mode != PrintingTaskType::CALIBRATION ||(m_calib_mode == CalibMode::Calib_Flow_Rate && m_calib_method == CalibrationMethod::CALI_METHOD_MANUAL)) {
                wxImage img(*evt.GetResponse().GetStream());
                img_list.insert(std::make_pair(m_request_url, img));
                wxImage resize_img = img.Scale(m_project_task_panel->get_bitmap_thumbnail()->GetSize().x, m_project_task_panel->get_bitmap_thumbnail()->GetSize().y, wxIMAGE_QUALITY_HIGH);
                m_project_task_panel->set_thumbnail_img(resize_img);
                m_project_task_panel->set_brightness_value(get_brightness_value(resize_img));
            }
            if (obj) {
                m_project_task_panel->set_plate_index(obj->m_plate_index);
            } else {
                m_project_task_panel->set_plate_index(-1);
            }
            task_thumbnail_state = ThumbnailState::TASK_THUMBNAIL;
            break;
        }
        case wxWebRequest::State_Failed:
        case wxWebRequest::State_Cancelled:
        case wxWebRequest::State_Unauthorized: {
            m_project_task_panel->set_thumbnail_img(m_thumbnail_brokenimg.bmp());
            m_project_task_panel->set_plate_index(-1);
            task_thumbnail_state = ThumbnailState::BROKEN_IMG;
            break;
        }
        case wxWebRequest::State_Active:
        case wxWebRequest::State_Idle: break;
        default: break;
    }
}

void PhrozenStatusPanel::refresh_thumbnail_webrequest(wxMouseEvent& event)
{
    if (!obj) return;
    if (task_thumbnail_state != ThumbnailState::BROKEN_IMG) return;

    if (obj->slice_info) {
        m_request_url = wxString(obj->slice_info->thumbnail_url);
        if (!m_request_url.IsEmpty()) {
            web_request = wxWebSession::GetDefault().CreateRequest(this, m_request_url);
            BOOST_LOG_TRIVIAL(trace) << "monitor: create new webrequest, state = " << web_request.GetState() << ", url = " << m_request_url;
            if (web_request.GetState() == wxWebRequest::State_Idle)
                web_request.Start();
            BOOST_LOG_TRIVIAL(trace) << "monitor: start new webrequest, state = " << web_request.GetState() << ", url = " << m_request_url;
        }
    }
}


bool PhrozenStatusPanel::is_task_changed(MachineObject* obj)
{
    //todo: remove, now not support task management
    return false;
}

void PhrozenStatusPanel::update(MachineObject *obj)
{
    if (!obj) return;
    m_project_task_panel->Freeze();
    //update_subtask(obj);
    // ======================== //
    // 更新各項列印相關資訊與狀態入口
    // ======================== //
    m_project_task_panel->Thaw();
    update_print_states(obj);

#if !BBL_RELEASE_TO_PUBLIC
    auto delay1  = std::chrono::duration_cast<std::chrono::milliseconds>(obj->last_utc_time - std::chrono::system_clock::now()).count();
    auto delay2  = std::chrono::duration_cast<std::chrono::milliseconds>(obj->last_push_time - std::chrono::system_clock::now()).count();
    auto delay = wxString::Format(" %ld/%ld", delay1, delay2);
    m_staticText_timelapse
        ->SetLabel((obj->is_lan_mode_printer() ? "Local Mqtt" : obj->is_tunnel_mqtt ? "Tunnel Mqtt" : "Cloud Mqtt") + delay);
    m_bmToggleBtn_timelapse
        ->Enable(!obj->is_lan_mode_printer());
    m_bmToggleBtn_timelapse
        ->SetValue(obj->is_tunnel_mqtt);
#endif

    m_machine_ctrl_panel->Freeze();

    //if (obj->is_in_printing() && !obj->can_resume())
    //    show_printing_status(false, true);
    //else
    //    show_printing_status();

    update_temp_ctrl(obj);
    update_print_speed_ctrl(obj);
    update_fan_cooling_speed_ctrl(obj);
    update_webcam_lighting_status( obj );
    
    if ( !IsWebCamRefreshTimerInitialized() )
    {
        InitWebCamUiUpdateTimer();
    }
    //update_misc_ctrl(obj);

    update_ams(obj);
    //update_cali(obj);

#if 0
    if (obj) {
        // update extrusion calibration
        if (m_extrusion_cali_dlg) {
            m_extrusion_cali_dlg->update_machine_obj(obj);
            m_extrusion_cali_dlg->update();
        }

        // update calibration status
        if (calibration_dlg != nullptr) {
            calibration_dlg->update_machine_obj(obj);
            calibration_dlg->update_cali(obj);
        }
        


        if (obj->is_support_first_layer_inspect
            || obj->is_support_ai_monitoring
            || obj->is_support_build_plate_marker_detect
            || obj->is_support_auto_recovery_step_loss) {
            m_options_btn->Show();
            if (print_options_dlg) {
                print_options_dlg->update_machine_obj(obj);
                print_options_dlg->update_options(obj);
            }
        } else {
            m_options_btn->Hide();
        }

        m_parts_btn->Show();

        //support edit chamber temp
        if (obj->is_support_chamber_edit) {
            m_tempCtrl_chamber->SetReadOnly(false);
            m_tempCtrl_chamber->Enable();
            wxCursor cursor(wxCURSOR_IBEAM);
            m_tempCtrl_chamber->GetTextCtrl()->SetCursor(cursor);
        } else {
            m_tempCtrl_chamber->SetReadOnly(true);

            wxCursor cursor(wxCURSOR_ARROW);
            m_tempCtrl_chamber->GetTextCtrl()->SetCursor(cursor);

            if (obj->get_printer_series() == PrinterSeries::SERIES_X1) {
                m_tempCtrl_chamber->SetTagTemp(PHROZEN_TEMP_BLANK_STR);
            }if (obj->get_printer_series() == PrinterSeries::SERIES_P1P)
            {
                m_tempCtrl_chamber->SetLabel(PHROZEN_TEMP_BLANK_STR);
                m_tempCtrl_chamber->GetTextCtrl()->SetValue(PHROZEN_TEMP_BLANK_STR);
            }

            //m_tempCtrl_chamber->Disable();

        }

        if (!obj->dev_connection_type.empty()) {
            auto iter_connect_type = m_print_connect_types.find(obj->dev_id);
            if (iter_connect_type != m_print_connect_types.end()) {
                if (iter_connect_type->second != obj->dev_connection_type) {

                    if (iter_connect_type->second == "lan" && obj->dev_connection_type == "cloud") {
                        m_print_connect_types[obj->dev_id] = obj->dev_connection_type;
                    }

                    if (iter_connect_type->second == "cloud" && obj->dev_connection_type == "lan") {
                        m_print_connect_types[obj->dev_id] = obj->dev_connection_type;
                    }
                }
            }
             m_print_connect_types[obj->dev_id] = obj->dev_connection_type;
        }

        update_error_message();
    }

    update_camera_state(obj);
#endif

    m_machine_ctrl_panel->Thaw();
}

void PhrozenStatusPanel::InitWebCamUiUpdateTimer()
{
    if ( !m_spWebCam_refresh_timer )
    {
        m_spWebCam_refresh_timer = std::make_unique< wxTimer >();
        m_spWebCam_refresh_timer->SetOwner(this);
        m_spWebCam_refresh_timer->Start(REFRESH_WEBCAM_UI_INTERVAL);
        Bind(wxEVT_TIMER, &PhrozenStatusPanel::on_update_webcam_ui_timer, this);
        wxPostEvent(this, wxTimerEvent());
    }
}

void PhrozenStatusPanel::on_update_webcam_ui_timer(wxTimerEvent& event)
{
    if ( m_pMachineObj ) UpdateWebCameraView( m_pMachineObj );
}

void PhrozenStatusPanel::on_lighting_button_triggered( wxCommandEvent& event )
{
    PhrozenStatusBasePanel::on_lighting_button_triggered( event );

    if ( MonitorControl::IsStartReceiving() ) 
    {
        auto pPhrozenMachineObj = wxGetApp().GetPhrozenMachineObject();
        if ( !pPhrozenMachineObj ) return;
        pPhrozenMachineObj->SetPhrozenCommand_lighting_enabled( IsLightingUiEnabled() );
        set_hold_count( m_lighting_state_timeout );
    }
}

void PhrozenStatusPanel::UpdateWebCameraView( PhrozenMachineObject_Dev* obj)
{
    if ( !obj || IsWebcamUiEnabled() ) 
    {
        ResetWebcamView();// clear image result (black)
        return;
    }
    if ( !m_pMachineObj->ReadDataFromWebcamSnapshot( m_kWebCameraImageData ) )
    {
        return;
    }
    
    wxMemoryInputStream memStream(&m_kWebCameraImageData[0], m_kWebCameraImageData.size());
    wxImage image(memStream, wxBITMAP_TYPE_JPEG);
    image = image.Rotate180(); //水平+垂直翻轉
    int x, y;
    media_ctrl_panel->GetSize( &x, &y );
    image.Rescale(x, y);

    m_kCurrentWebCamBitmap = wxBitmap(image);
    media_ctrl_panel->Refresh();
}

void PhrozenStatusPanel::ResetWebcamView()
{
    int x, y;
    media_ctrl_panel->GetSize( &x, &y );
    wxImage image(x,y);
    m_kCurrentWebCamBitmap = wxBitmap(image);
    media_ctrl_panel->Refresh();
}

void PhrozenStatusPanel::show_recenter_dialog() {
    RecenterDialog dlg(this);
    if (dlg.ShowModal() == wxID_OK)
        obj->command_go_home();
}

void PhrozenStatusPanel::show_error_message(MachineObject *obj, bool is_exist, wxString msg, std::string print_error_str, wxString image_url, std::vector<int> used_button)
{
    if (is_exist && msg.IsEmpty()) {
        error_info_reset();
    } else {
        m_project_task_panel->show_error_msg(msg);

        if (!used_button.empty()) {
            BOOST_LOG_TRIVIAL(info) << "show print error! error_msg = " << msg;
            if (m_print_error_dlg == nullptr) {
                m_print_error_dlg = new PrintErrorDialog(this->GetParent(), wxID_ANY, _L("Error"));
            }

            m_print_error_dlg->update_title_style(_L("Error"), used_button, this);
            m_print_error_dlg->update_text_image(msg, print_error_str, image_url);
            m_print_error_dlg->Bind(EVT_SECONDARY_CHECK_CONFIRM, [this, obj](wxCommandEvent& e) {
                if (obj) {
                    obj->command_clean_print_error(obj->subtask_id_, obj->print_error);
                }
                });

            m_print_error_dlg->on_show();
        }
        else {
            //old error code dialog
            auto it_retry  = std::find(phrozen_message_containing_retry.begin(), phrozen_message_containing_retry.end(), print_error_str);
            auto it_done   = std::find(phrozen_message_containing_done.begin(), phrozen_message_containing_done.end(), print_error_str);
            auto it_resume = std::find(phrozen_message_containing_resume.begin(), phrozen_message_containing_resume.end(), print_error_str);

            BOOST_LOG_TRIVIAL(info) << "show print error! error_msg = " << msg;

            wxDateTime now = wxDateTime::Now();
            wxString show_time = now.Format("%H%M%d");
            wxString error_code_msg = wxString::Format("%S\n[%S %S]", msg, print_error_str, show_time);

            if (m_print_error_dlg_no_action == nullptr) {
                m_print_error_dlg_no_action = new SecondaryCheckDialog(this->GetParent(), wxID_ANY, _L("Warning"), SecondaryCheckDialog::ButtonStyle::ONLY_CONFIRM);
            }

            if (it_done != phrozen_message_containing_done.end() && it_retry != phrozen_message_containing_retry.end()) {
                m_print_error_dlg_no_action->update_title_style(_L("Warning"), SecondaryCheckDialog::ButtonStyle::DONE_AND_RETRY, this);
            }
            else if (it_done != phrozen_message_containing_done.end()) {
                m_print_error_dlg_no_action->update_title_style(_L("Warning"), SecondaryCheckDialog::ButtonStyle::CONFIRM_AND_DONE, this);
            }
            else if (it_retry != phrozen_message_containing_retry.end()) {
                m_print_error_dlg_no_action->update_title_style(_L("Warning"), SecondaryCheckDialog::ButtonStyle::CONFIRM_AND_RETRY, this);
            }
            else if (it_resume != phrozen_message_containing_resume.end()) {
                m_print_error_dlg_no_action->update_title_style(_L("Warning"), SecondaryCheckDialog::ButtonStyle::CONFIRM_AND_RESUME, this);
            }
            else {
                m_print_error_dlg_no_action->update_title_style(_L("Warning"), SecondaryCheckDialog::ButtonStyle::ONLY_CONFIRM, this);
            }
            m_print_error_dlg_no_action->update_text(error_code_msg);
            m_print_error_dlg_no_action->Bind(EVT_SECONDARY_CHECK_CONFIRM, [this, obj](wxCommandEvent& e) {
                if (obj) {
                    obj->command_clean_print_error(obj->subtask_id_, obj->print_error);
                }
                });

            m_print_error_dlg_no_action->on_show();
        }
        wxGetApp().mainframe->RequestUserAttention(wxUSER_ATTENTION_ERROR);
    }
}

void PhrozenStatusPanel::update_error_message()
{
    if (obj->print_error <= 0) {
        before_error_code = obj->print_error;
        show_error_message(obj, true, wxEmptyString);
        return;
    } else if (before_error_code != obj->print_error && obj->print_error != skip_print_error) {
        before_error_code = obj->print_error;

        if (wxGetApp().get_hms_query()) {
            char buf[32];
            ::sprintf(buf, "%08X", obj->print_error);
            std::string print_error_str = std::string(buf);
            if (print_error_str.size() > 4) { print_error_str.insert(4, " "); }

            wxString error_msg;
            bool is_errocode_exist = wxGetApp().get_hms_query()->query_print_error_msg(obj->print_error, error_msg);
            std::vector<int> used_button;
            wxString error_image_url = wxGetApp().get_hms_query()->query_print_error_url_action(obj->print_error, obj->dev_id, used_button);
            // special case
            if (print_error_str == "0300 8003" || print_error_str == "0300 8002" || print_error_str == "0300 800A") {
                used_button.emplace_back(PrintErrorDialog::PrintErrorButton::JUMP_TO_LIVEVIEW);
            }
            show_error_message(obj, is_errocode_exist, error_msg, print_error_str, error_image_url, used_button);
        }
    }
}


void PhrozenStatusPanel::update_temp_ctrl(MachineObject *obj)
{
    if (!obj) return;


    // bed
    int nTempBedCurrent = (int)obj->GetPhrozenBedTemperature();
    int nTempBedTarget = (int) obj->GetPhrozenBedTargetTemperature();

    update_bed_current_temp( nTempBedCurrent );
    //todo set max bed temp

    // update temprature if not input temp target
    if (m_temp_bed_timeout > 0) {
        m_temp_bed_timeout--;
    } else {
        if (!bed_temp_input) { update_bed_target_temp( nTempBedTarget ); }
    }

    if ((nTempBedTarget - nTempBedCurrent) >= TEMP_THRESHOLD_VAL) {
        //todo m_spTemp_heatedBed set active
        m_spTemp_heatedBed;//m_tempCtrl_bed->SetIconActive();
    } else {
        //todo m_spTemp_heatedBed set normal
        m_spTemp_heatedBed;//m_tempCtrl_bed->SetIconNormal();
    }


    // nozzle
    int nTempNozzleCurrent = (int)obj->GetPhrozenNozzleTemperature();
    int nTempNozzleTarget = (int) obj->GetPhrozenNozzleTargetTemperature();

    update_nozzle_current_temp( nTempNozzleCurrent );
    //todo set max nozzle temp
    //reference:
    //if (obj->nozzle_max_temperature > -1) {
    //    if (m_tempCtrl_nozzle) m_tempCtrl_nozzle->SetMaxTemp(obj->nozzle_max_temperature);
    //}
    //else {
    //    if (m_tempCtrl_nozzle) m_tempCtrl_nozzle->SetMaxTemp(nozzle_temp_range[1]);
    //}

    if (m_temp_nozzle_timeout > 0) {
        m_temp_nozzle_timeout--;
    } else {
        if (!nozzle_temp_input) { update_nozzle_target_temp( nTempNozzleTarget ); }
    }

    if ((nTempNozzleTarget - nTempNozzleCurrent) >= TEMP_THRESHOLD_VAL) {
        //todo m_spTemp_nozzle set active
        m_spTemp_nozzle; //m_tempCtrl_nozzle->SetIconActive();
    } else {
        //todo m_spTemp_nozzle set normal
        m_spTemp_nozzle; //m_tempCtrl_nozzle->SetIconNormal();
    }

#if 0 //chamber setting -> todo
    m_tempCtrl_chamber->SetCurrTemp(obj->chamber_temp);
    // update temprature if not input temp target
    if (m_temp_chamber_timeout > 0) {
        m_temp_chamber_timeout--;
    }
    else {
        if (!cham_temp_input) { m_tempCtrl_chamber->SetTagTemp(obj->chamber_temp_target); }
    }

    if ((obj->chamber_temp_target - obj->chamber_temp) >= TEMP_THRESHOLD_VAL) {
        m_tempCtrl_chamber->SetIconActive();
    }
    else {
        m_tempCtrl_chamber->SetIconNormal();
    }
#endif
}

void PhrozenStatusPanel::update_print_speed_ctrl(MachineObject *obj)
{
    if (!obj) return;
    
    PhrozenPrintSpeed eLevel = print_speed_percent_to_enum( obj->GetPhrozenPrintSpeed() );
    if (m_print_speed_timeout > 0) {
        m_print_speed_timeout--;
    } else {
        if (!bed_temp_input) { update_print_speed_level( eLevel ); }
    }
}

void PhrozenStatusPanel::update_fan_cooling_speed_ctrl(MachineObject *obj)
{
    if (!obj) return;

    int current_auxiliary_cooling = (int)( obj->GetPhrozenAuxiliaryCoolingSpeed()* 100 );
    int current_part_cooling = (int)( obj->GetPhrozenPartCoolingSpeed()* 100 );
    int current_shield_cooling = (int)( obj->GetPhrozenShieldCoolingSpeed()* 100 );

    update_cooling_auxiliary_current_power( current_auxiliary_cooling );
    if (m_cooling_auxiliary_timeout > 0) {
        m_cooling_auxiliary_timeout--;
    } else {
        if (!cooling_auxiliary_input) { update_cooling_auxiliary_target_power( current_auxiliary_cooling ); }
    }

    update_cooling_part_current_power( current_part_cooling );
    if (m_cooling_part_timeout > 0) {
        m_cooling_part_timeout--;
    } else {
        if (!cooling_part_input) { update_cooling_part_target_power( current_part_cooling ); }
    }

    update_cooling_shield_current_power( current_shield_cooling );
    if (m_cooling_shield_timeout > 0) {
        m_cooling_shield_timeout--;
    } else {
        if (!cooling_shield_input) { update_cooling_shield_target_power( current_shield_cooling ); }
    }
    
}

void PhrozenStatusPanel::update_webcam_lighting_status( MachineObject *obj )
{
    if (!obj) return;

    if (m_lighting_state_timeout > 0) {
        m_lighting_state_timeout--;
    } else {
        bool bIsLighingEnabled = obj->GetPhrozenCommand_lighting_enabled();
        m_pCam_light_switch_button->SetValue( bIsLighingEnabled );
    }
}

void PhrozenStatusPanel::update_misc_ctrl(MachineObject *obj)
{
    if (!obj) return;

    if (obj->can_unload_filament()) {
        if (!m_button_unload->IsShown()) {
            m_button_unload->Show();
            m_button_unload->GetParent()->Layout();
        }
    } else {
        if (m_button_unload->IsShown()) {
            m_button_unload->Hide();
            m_button_unload->GetParent()->Layout();
        }
    }

    if (obj->is_core_xy()) {
        m_staticText_z_tip->SetLabel(_L("Bed"));
    } else {
        m_staticText_z_tip->SetLabel("Z");
    }

    // update extruder icon
    update_extruder_status(obj);

    bool is_suppt_aux_fun = obj->is_support_aux_fan;
    bool is_suppt_cham_fun = obj->is_support_chamber_fan;

    //update cham fan
    if (m_current_support_cham_fan != is_suppt_cham_fun) {
        if (is_suppt_cham_fun) {
            m_switch_cham_fan->Show();
            m_switch_nozzle_fan->SetMinSize(MISC_BUTTON_3FAN_SIZE);
            m_switch_nozzle_fan->SetMaxSize(MISC_BUTTON_3FAN_SIZE);
            m_switch_printing_fan->SetMinSize(MISC_BUTTON_3FAN_SIZE);
            m_switch_printing_fan->SetMaxSize(MISC_BUTTON_3FAN_SIZE);
        }
        else {
            m_switch_cham_fan->Hide();
            m_switch_nozzle_fan->SetMinSize(MISC_BUTTON_2FAN_SIZE);
            m_switch_nozzle_fan->SetMaxSize(MISC_BUTTON_2FAN_SIZE);
            m_switch_printing_fan->SetMinSize(MISC_BUTTON_2FAN_SIZE);
            m_switch_printing_fan->SetMaxSize(MISC_BUTTON_2FAN_SIZE);
        }

        m_misc_ctrl_sizer->Layout();
    }

    if (m_current_support_aux_fan != is_suppt_aux_fun) {
        if (is_suppt_aux_fun) {
            m_switch_printing_fan->Show();
            m_switch_nozzle_fan->SetMinSize(MISC_BUTTON_3FAN_SIZE);
            m_switch_nozzle_fan->SetMaxSize(MISC_BUTTON_3FAN_SIZE);
            m_switch_cham_fan->SetMinSize(MISC_BUTTON_3FAN_SIZE);
            m_switch_cham_fan->SetMaxSize(MISC_BUTTON_3FAN_SIZE);
        }
        else {
            m_switch_printing_fan->Hide();
            m_switch_nozzle_fan->SetMinSize(MISC_BUTTON_2FAN_SIZE);
            m_switch_nozzle_fan->SetMaxSize(MISC_BUTTON_2FAN_SIZE);
            m_switch_cham_fan->SetMinSize(MISC_BUTTON_2FAN_SIZE);
            m_switch_cham_fan->SetMaxSize(MISC_BUTTON_2FAN_SIZE);
        }

        m_misc_ctrl_sizer->Layout();
    }

    if (!is_suppt_aux_fun && !is_suppt_cham_fun) {
        m_switch_nozzle_fan->SetMinSize(MISC_BUTTON_1FAN_SIZE);
        m_switch_nozzle_fan->SetMaxSize(MISC_BUTTON_1FAN_SIZE);
        m_misc_ctrl_sizer->Layout();
    }


    // nozzle fan
    if (m_switch_nozzle_fan_timeout > 0) {
        m_switch_nozzle_fan_timeout--;
    }  else{
        int speed = round(obj->cooling_fan_speed / float(25.5));
        m_switch_nozzle_fan->SetValue(speed > 0 ? true : false);
        m_switch_nozzle_fan->setFanValue(speed * 10);
        if (m_fan_control_popup) {
            m_fan_control_popup->update_fan_data(MachineObject::FanType::COOLING_FAN, obj);
        }
    }

    // printing fan
    if (m_switch_printing_fan_timeout > 0) {
        m_switch_printing_fan_timeout--;
    }else{
        int speed = round(obj->big_fan1_speed / float(25.5));
        m_switch_printing_fan->SetValue(speed > 0 ? true : false);
        m_switch_printing_fan->setFanValue(speed * 10);
        if (m_fan_control_popup) {
            m_fan_control_popup->update_fan_data(MachineObject::FanType::BIG_COOLING_FAN, obj);
        }
    }

    // cham fan
    if (m_switch_cham_fan_timeout > 0) {
        m_switch_cham_fan_timeout--;
    }else{
        int speed = round(obj->big_fan2_speed / float(25.5));
        m_switch_cham_fan->SetValue(speed > 0 ? true : false);
        m_switch_cham_fan->setFanValue(speed * 10);
        if (m_fan_control_popup) {
            m_fan_control_popup->update_fan_data(MachineObject::FanType::CHAMBER_FAN, obj);
        }
    }

    bool light_on = obj->chamber_light != MachineObject::LIGHT_EFFECT::LIGHT_EFFECT_OFF;
    BOOST_LOG_TRIVIAL(trace) << "light: " << (light_on ? "on" : "off");
    if (m_switch_lamp_timeout > 0)
        m_switch_lamp_timeout--;
    else {
        m_switch_lamp->SetValue(light_on);
        /*wxString label = light_on ? "On" : "Off";
        m_switch_lamp->SetLabels(label, label);*/
    }

    if (speed_lvl_timeout > 0)
        speed_lvl_timeout--;
    else {
        // update speed
        this->speed_lvl = obj->printing_speed_lvl;
            wxString text_speed = wxString::Format("%d%%", obj->printing_speed_mag);
            m_switch_speed->SetLabels(text_speed, text_speed);
    }

    m_current_support_aux_fan = is_suppt_aux_fun;
    m_current_support_cham_fan = is_suppt_cham_fun;
}

void PhrozenStatusPanel::update_extruder_status(MachineObject* obj)
{
    if (!obj) return;
    if (obj->is_filament_at_extruder()) {
        if (obj->extruder_axis_status == MachineObject::ExtruderAxisStatus::LOAD) {
            m_bitmap_extruder_img->SetBitmap(m_bitmap_extruder_filled_load);
        }
        else {
            m_bitmap_extruder_img->SetBitmap(m_bitmap_extruder_filled_unload);
        }
    }
    else {
        if (obj->extruder_axis_status == MachineObject::ExtruderAxisStatus::LOAD) {
            m_bitmap_extruder_img->SetBitmap(m_bitmap_extruder_empty_load);
        } else {
            m_bitmap_extruder_img->SetBitmap(m_bitmap_extruder_empty_unload);
        }
    }
}

void PhrozenStatusPanel::update_ams(MachineObject *obj)
{
    if ( !m_pFilamentControlPanel || !m_pFilamentControlPanel->IsShown() )
    {
        return;
    }

    FilamentSystemState kStatus;
    if ( !obj )
    {
        //initialize is disable all filament.
        m_pFilamentControlPanel->UpdateFilamentState( kStatus );
        return;
    }

    //┌─────────────────────────────────────────────────────┐
    //│         update_ams() 函數中調用
    //│
    //│  MonitorControl::IsConnectedToAMS()
    //│  └─> 返回 m_bIsConnetedToAMS -> bool
    //│      └─> 機器是否連接AMS  返回 ture / false
    //│
    //│
    //│  MonitorControl::GetNozzleInfo()
    //│  └─> 返回 nozzleInfo (NozzleInfo)
    //│          └─> 成員函數 (Member Functions)
    //│              ├─> isFilamentExisting() -> bool
    //│                  └─> 檢查線材是否存在於噴頭  返回 ture / false
    //│
    //│
    //│  MonitorControl::GetAMSList()
    //│  └─> 返回 m_kAMSList (vector<AMSInfo>)
    //│      └─> ams_list[0-3] (AMSInfo)
    //│          └─> 成員函數 (Member Functions)
    //│              ├─> getEntryState() -> bool
    //│              │   └─> 線材是否在AMS入口處  返回 entry 狀態 ture / false
    //│              ├─> getParkState() -> bool
    //│              │   └─> 線材是否在緩衝區位置  返回 park 狀態 ture / false
    //│              ├─> getLoadingState() -> bool
    //│              │   └─> 是否已進料  返回 loading 狀態 ture / false
    //│              ├─> isLoadingStart() -> bool
    //│              │   └─> 檢查進料是否開始  返回 ture / false
    //│              ├─> isUnloadStart() -> bool
    //│              │   └─> 檢查退料是否開始  返回 ture / false
    //└─────────────────────────────────────────────────────┘
    
    kStatus.SetNozzleFilamentDetected( MonitorControl::GetNozzleInfo().isFilamentExisting() );
    if ( MonitorControl::IsConnectedToAMS() )
    {
        kStatus.SetAMSSystemDetected( MonitorControl::IsConnectedToAMS() );
        auto& kAmsList = MonitorControl::GetAMSList();
        //const std::vector<AMSInfo>& GetAMSList();
        assert( kAmsList.size() == 4 && "ams slot list must be 4" );
        for ( size_t nIndex = 0; nIndex < kAmsList.size(); ++nIndex )
        {
            kStatus.SetAMSSlotState( nIndex, kAmsList[nIndex].getEntryState(),
                                             kAmsList[nIndex].getParkState(),
                                             kAmsList[nIndex].getLoadingState(),
                                             kAmsList[nIndex].isLoadingStart(),
                                             kAmsList[nIndex].isUnloadStart() );
        }
    }

    m_pFilamentControlPanel->UpdateFilamentState( kStatus );
    return;

}

void PhrozenStatusPanel::update_cali(MachineObject *obj)
{
    if (!obj) return;
    //[TODO] check state: no connect or is printing(running or pause) or is open calib dlg
    //       need disable it
    //if (obj->is_calibration_running()) {
    //    m_calibration_btn->SetLabel(_L("Calibrating"));
    //    if (calibration_dlg && calibration_dlg->IsShown()) {
    //        m_calibration_btn->Disable();
    //    } else {
    //        m_calibration_btn->Enable();
    //    }
    //} else {
    //    // IDLE
    //    m_calibration_btn->SetLabel(_L("Calibration"));
    //    // disable in printing
    //    if (obj->is_in_printing()) {
    //        m_calibration_btn->Disable();
    //    } else {
    //        m_calibration_btn->Enable();
    //    }
    //}
}

void PhrozenStatusPanel::update_calib_bitmap() {
    m_current_print_mode = PrintingTaskType::NOT_CLEAR;  //printing task might be changed when updating.
    if (calib_bitmap != nullptr) {
        delete calib_bitmap;
        calib_bitmap = nullptr;
    }
}

void PhrozenStatusPanel::update_basic_print_data(bool def)
{
    if (def) {
        if (!obj) return;
        if (!obj->slice_info) return;
        wxString prediction = wxString::Format("%s", get_bbl_time_dhms(obj->slice_info->prediction));
        wxString weight = wxString::Format("%.2fg", obj->slice_info->weight);

        m_project_task_panel->show_priting_use_info(true, prediction, weight);
    }
    else {
        m_project_task_panel->show_priting_use_info(false, "0m", "0g");
    }
}

void PhrozenStatusPanel::update_model_info()
{
    auto get_subtask_fn = [this](BBLModelTask* subtask) {
        CallAfter([this, subtask]() { 
            if (obj && obj->subtask_id_ == subtask->task_id) {
                obj->set_modeltask(subtask);
            }
        });
    };

     
    if (wxGetApp().getAgent() && obj) {
        BBLSubTask* curr_task = obj->get_subtask();
        if (curr_task) {
            BBLModelTask* curr_model_task = obj->get_modeltask();
            if (!curr_model_task && !request_model_info_flag) {
                curr_model_task = new BBLModelTask();
                curr_model_task->task_id = curr_task->task_id;
                request_model_info_flag = true;
                if (!curr_model_task->task_id.empty() && curr_model_task->task_id.compare("0") != 0) {
                    wxGetApp().getAgent()->get_subtask(curr_model_task,  get_subtask_fn);
                }
            }
        }
    }
}

void PhrozenStatusPanel::update_subtask(MachineObject *obj)
{
    if (!obj) return;
    if (m_current_print_mode != PRINGINT) {
        if (calib_bitmap == nullptr) {
            m_calib_mode = get_obj_calibration_mode(obj, m_calib_method, cali_stage);
            if (m_calib_mode == CalibMode::Calib_None)
                m_current_print_mode = PRINGINT;
            // the printing task is calibrattion, not normal printing.
            else if (m_calib_mode != CalibMode::Calib_None) {
                m_current_print_mode = CALIBRATION;
                auto get_bitmap = [](wxString& png_path, int width, int height) {
                    wxImage image(width, height);
                    image.LoadFile(png_path, wxBITMAP_TYPE_PNG);
                    image = image.Scale(width, height, wxIMAGE_QUALITY_NORMAL);
                    return wxBitmap(image);
                };
                wxString png_path = "";
                int width = m_project_task_panel->get_bitmap_thumbnail()->GetSize().x;
                int height = m_project_task_panel->get_bitmap_thumbnail()->GetSize().y;
                if (m_calib_method == CALI_METHOD_AUTO) {
                    if (m_calib_mode == CalibMode::Calib_PA_Line) {
                        png_path = (boost::format("%1%/images/fd_calibration_auto.png") % resources_dir()).str();
                    }
                    else if (m_calib_mode == CalibMode::Calib_Flow_Rate) {
                        png_path = (boost::format("%1%/images/flow_rate_calibration_auto.png") % resources_dir()).str();
                    }

                }
                else if (m_calib_method == CALI_METHOD_MANUAL) {
                    if (m_calib_mode== CalibMode::Calib_PA_Line) {
                        if (cali_stage == 0) {  // Line mode
                            png_path = (boost::format("%1%/images/fd_calibration_manual.png") % resources_dir()).str();
                        }
                        else if (cali_stage == 1) { // Pattern mode
                            png_path = (boost::format("%1%/images/fd_pattern_manual_device.png") % resources_dir()).str();
                        }
                    }
                }
                if (png_path != "") {
                    calib_bitmap = new wxBitmap;
                    *calib_bitmap = get_bitmap(png_path, width, height);
                }
            }
        }
        if (calib_bitmap != nullptr)
            m_project_task_panel->set_thumbnail_img(*calib_bitmap);
    }
    
    if (obj->is_support_layer_num) {
        m_project_task_panel->update_layers_num(true);
    }
    else {
        m_project_task_panel->update_layers_num(false);
    }

    update_model_info();

    if (obj->is_system_printing()
        || obj->is_in_calibration()) {
        reset_printing_values();
    } else if (obj->is_in_printing() || obj->print_status == "FINISH") {
        if (obj->is_in_prepare() || obj->print_status == "SLICING") {
            m_project_task_panel->market_scoring_hide();
            m_project_task_panel->get_request_failed_panel()->Hide();
            m_project_task_panel->enable_abort_button(false);
            m_project_task_panel->enable_pause_resume_button(false, "pause_disable");
            wxString prepare_text;
            bool show_percent = true;

            if (obj->is_in_prepare()) {
                prepare_text = wxString::Format(_L("Downloading..."));
            }
            else if (obj->print_status == "SLICING") {
                if (obj->queue_number <= 0) {
                    prepare_text = wxString::Format(_L("Cloud Slicing..."));
                } else {
                    prepare_text = wxString::Format(_L("In Cloud Slicing Queue, there are %s tasks ahead."), std::to_string(obj->queue_number));
                    show_percent = false;
                }
            } else
                prepare_text = wxString::Format(_L("Downloading..."));

            if (obj->gcode_file_prepare_percent >= 0 && obj->gcode_file_prepare_percent <= 100 && show_percent)
                prepare_text += wxString::Format("(%d%%)", obj->gcode_file_prepare_percent);

            m_project_task_panel->update_stage_value(prepare_text, 0);
            m_project_task_panel->update_progress_percent(PHROZEN_NA_STR, wxEmptyString);
            m_project_task_panel->update_left_time(PHROZEN_NA_STR);
            m_project_task_panel->update_layers_num(true, wxString::Format(_L("Layer: %s"), PHROZEN_NA_STR));
            m_project_task_panel->update_subtask_name(wxString::Format("%s", GUI::from_u8(obj->subtask_name)));


            if (obj->get_modeltask() && obj->get_modeltask()->design_id > 0) {
                m_project_task_panel->show_profile_info(true, wxString::FromUTF8(obj->get_modeltask()->profile_name));
            }
            else {
                m_project_task_panel->show_profile_info(false);
            }
            update_basic_print_data(false);
        } else {
            if (obj->can_resume()) {
                m_project_task_panel->enable_pause_resume_button(true, "resume");
            } else {
                 m_project_task_panel->enable_pause_resume_button(true, "pause");
            }

            // update printing stage
            m_project_task_panel->update_left_time(obj->mc_left_time);
            if (obj->subtask_) {
                m_project_task_panel->update_stage_value(obj->get_curr_stage(), obj->subtask_->task_progress);
                m_project_task_panel->update_progress_percent(wxString::Format("%d", obj->subtask_->task_progress), "%");
                m_project_task_panel->update_layers_num(true, wxString::Format(_L("Layer: %d/%d"), obj->curr_layer, obj->total_layers));

            } else {
                m_project_task_panel->update_stage_value(obj->get_curr_stage(), 0);
                m_project_task_panel->update_progress_percent(PHROZEN_NA_STR, wxEmptyString);
                m_project_task_panel->update_layers_num(true, wxString::Format(_L("Layer: %s"), PHROZEN_NA_STR));
            }

            if (obj->is_printing_finished()) {
                obj->update_model_task();
                m_project_task_panel->enable_abort_button(false);
                m_project_task_panel->enable_pause_resume_button(false, "resume_disable");
                // is makeworld subtask
                if (wxGetApp().has_model_mall() && obj->is_makeworld_subtask()) {
                    // has model mall rating result
                    if (obj && obj->rating_info && obj->rating_info->request_successful) {
                        m_project_task_panel->get_request_failed_panel()->Hide();
                        BOOST_LOG_TRIVIAL(info) << "model mall result request successful";
                        // has start count
                        if (!m_project_task_panel->get_star_count_dirty()) {
                            if (obj->rating_info->start_count > 0) {
                                m_project_task_panel->set_star_count(obj->rating_info->start_count);
                                m_project_task_panel->set_star_count_dirty(true);
                                BOOST_LOG_TRIVIAL(info) << "Initialize scores";
                                m_project_task_panel->get_market_scoring_button()->Enable(true);
                                m_project_task_panel->set_has_reted_text(true);
                            } else {
                                m_project_task_panel->set_star_count(0);
                                m_project_task_panel->set_star_count_dirty(false);
                                m_project_task_panel->get_market_scoring_button()->Enable(false);
                                m_project_task_panel->set_has_reted_text(false);
                            }
                        }
                        m_project_task_panel->market_scoring_show();
                    } else if (obj && obj->rating_info && !obj->rating_info->request_successful) {
                        BOOST_LOG_TRIVIAL(info) << "model mall result request failed";
                        if (403 != obj->rating_info->http_code) {
                            BOOST_LOG_TRIVIAL(info) << "Request need retry";
                            m_project_task_panel->get_market_retry_buttom()->Enable(!obj->get_model_mall_result_need_retry);
                            m_project_task_panel->get_request_failed_panel()->Show();
                        } else {
                            BOOST_LOG_TRIVIAL(info) << "Request rejected";
                        }
                    }
                } else {
                    m_project_task_panel->market_scoring_hide();
                }
            } else { // model printing is not finished, hide scoring page
                m_project_task_panel->enable_abort_button(true);
                m_project_task_panel->market_scoring_hide();
                m_project_task_panel->get_request_failed_panel()->Hide();
            }
        }

        m_project_task_panel->update_subtask_name(wxString::Format("%s", GUI::from_u8(obj->subtask_name)));

        if (obj->get_modeltask() && obj->get_modeltask()->design_id > 0) {
            m_project_task_panel->show_profile_info(true, wxString::FromUTF8(obj->get_modeltask()->profile_name));
        }
        else {
            m_project_task_panel->show_profile_info(false);
        }

        //update thumbnail
        if (obj->is_sdcard_printing()) {
            update_basic_print_data(false);
            update_sdcard_subtask(obj);
        } else {
            update_basic_print_data(true);
            update_cloud_subtask(obj);
        }
    } else {
        reset_printing_values();
    }

    Layout();
}

void PhrozenStatusPanel::update_print_states(MachineObject *obj)
{
    // ============================================
    // 防呆檢查：確保物件有效
    // ============================================
    if (!obj) {
        BOOST_LOG_TRIVIAL(warning) << "PhrozenStatusPanel::update_print_status: obj is nullptr";
        return;
    }
    
    update_print_status(obj);
    update_print_progress(obj);
    update_print_file(obj);
    update_print_time(obj);
    update_print_stage(obj);
    update_print_filament(obj);
    update_thumbnail(obj);
}

void PhrozenStatusPanel::update_print_status(MachineObject *obj)
{
    // ============================================
    // 防呆檢查：確保物件有效
    // ============================================
    if (!obj) {
        BOOST_LOG_TRIVIAL(warning) << "PhrozenStatusPanel::update_print_status: obj is nullptr";
        return;
    }
    
    if (!m_project_task_panel) {
        BOOST_LOG_TRIVIAL(error) << "PhrozenStatusPanel::update_print_status: m_project_task_panel is nullptr";
        return;
    }
    
    // ============================================
    // 獲取列印狀態
    // ============================================
    std::string print_status = obj->GetPhrozenPrintStatus();
    
    // 防呆檢查：確保狀態不為空
    if (print_status.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "PhrozenStatusPanel::update_print_status: print_status is empty";
        print_status = "offline";  // 使用預設狀態
    }
    
    // ============================================
    // 查找表：狀態字串對應按鈕配置
    // 格式：<按鈕類型字串, 是否啟用停止按鈕>
    // ============================================
    const std::unordered_map<std::string, std::pair<const char*, bool>> status_config_map = {
        // 列印中：顯示暫停按鈕和停止按鈕
        {"printing", {"pause", true}},
        
        // 已暫停：顯示續印按鈕和停止按鈕
        {"paused", {"resume", true}},
        
        // 完成：禁用所有按鈕（顯示灰色續印按鈕）
        {"complete", {"resume_disable", false}},
        
        // 已取消：禁用所有按鈕（顯示灰色續印按鈕）
        {"cancelled", {"resume_disable", false}},
        
        // 離線：禁用所有按鈕（顯示灰色暫停按鈕）
        {"offline", {"pause_disable", false}},
        
        // 待機：禁用所有按鈕（顯示灰色暫停按鈕）
        {"standby", {"pause_disable", false}}
    };
    
    // ============================================
    // 根據狀態查找配置並更新按鈕
    // ============================================
    auto it = status_config_map.find(print_status);
    
    if (it != status_config_map.end()) {
        // 找到配置：使用查找表的配置
        const char* button_type = it->second.first;
        bool enable_abort = it->second.second;
        bool enable_pause_resume = (enable_abort == true);  // pause 和 resume 為啟用，其他為禁用
        
        m_project_task_panel->enable_pause_resume_button(enable_pause_resume, button_type);
        m_project_task_panel->enable_abort_button(enable_abort);
    } else {
        // 未找到：使用預設配置（防呆設計）
        BOOST_LOG_TRIVIAL(warning) << "PhrozenStatusPanel::update_print_status: "
                                    << "Unknown print status: \"" << print_status 
                                    << "\", using default safe configuration (pause_disable, abort disabled)";
        m_project_task_panel->enable_pause_resume_button(false, "pause_disable");
        m_project_task_panel->enable_abort_button(false);
    }
    
    BOOST_LOG_TRIVIAL(debug) << "PhrozenStatusPanel::update_print_status: status=" << print_status;
}

void PhrozenStatusPanel::update_print_progress(MachineObject *obj)
{
    // ============================================
    // 防呆檢查：確保物件有效
    // ============================================
    if (!obj) {
        BOOST_LOG_TRIVIAL(warning) << "PhrozenStatusPanel::update_print_progress: obj is nullptr";
        return;
    }
    
    if (!m_project_task_panel) {
        BOOST_LOG_TRIVIAL(error) << "PhrozenStatusPanel::update_print_progress: m_project_task_panel is nullptr";
        return;
    }
    
    // ============================================
    // 獲取列印進度
    // ============================================
    float print_progress = obj->GetPhrozenPrintProgress();
    
    // ============================================
    // 驗證進度值範圍並更新 UI
    // 進度值範圍：0.0 ~ 1.0（0% ~ 100%）
    // ============================================
    if (print_progress >= 0.0f && print_progress <= 1.0f) {
        // 有效進度值：轉換為百分比並更新 UI
        // print_progress 在 0.0-1.0 範圍內，progress_percent 必然在 0-100 範圍內
        int progress_percent = static_cast<int>(print_progress * 100.0f);
        
        // 確保百分比在有效範圍內（0-100）
        // 由於 print_progress 已經在 0.0-1.0 範圍內，progress_percent 理論上應該在 0-100 範圍內
        // 但為了防呆，仍然進行邊界檢查（處理浮點數精度問題）
        if (progress_percent < 0) {
            progress_percent = 0;
            BOOST_LOG_TRIVIAL(warning) << "PhrozenStatusPanel::update_print_progress: "
                                       << "Progress percent < 0, clamped to 0. Original value: " << print_progress;
        } else if (progress_percent > 100) {
            progress_percent = 100;
            BOOST_LOG_TRIVIAL(warning) << "PhrozenStatusPanel::update_print_progress: "
                                       << "Progress percent > 100, clamped to 100. Original value: " << print_progress;
        }
        
        m_project_task_panel->update_progress_percent(
            wxString::Format("%d", progress_percent), "%");
    } else {
        // 無效進度值：顯示為 N/A
        BOOST_LOG_TRIVIAL(warning) << "PhrozenStatusPanel::update_print_progress: "
                                    << "Invalid progress value: " << print_progress 
                                    << " (expected range: 0.0 ~ 1.0), displaying N/A";
        m_project_task_panel->update_progress_percent(PHROZEN_NA_STR, wxEmptyString);
    }
    
    // ============================================
    // 調試日誌
    // ============================================
    BOOST_LOG_TRIVIAL(debug) << "PhrozenStatusPanel::update_print_progress: progress=" << print_progress;
}

void PhrozenStatusPanel::update_print_file(MachineObject *obj)
{
    // ============================================
    // 防呆檢查：確保物件有效
    // ============================================
    if (!obj) {
        BOOST_LOG_TRIVIAL(warning) << "PhrozenStatusPanel::update_print_file: obj is nullptr";
        return;
    }
    
    if (!m_project_task_panel) {
        BOOST_LOG_TRIVIAL(error) << "PhrozenStatusPanel::update_print_file: m_project_task_panel is nullptr";
        return;
    }
    
    // ============================================
    // 獲取列印文件名
    // ============================================
    std::string print_file = obj->GetPhrozenPrintFile();
    
    // ============================================
    // 驗證文件名並更新 UI
    // ============================================
    if (!print_file.empty()) {
        // 有效文件名：轉換並更新 UI
        try {
            wxString file_name = GUI::from_u8(print_file);
            
            // 驗證轉換後的字串是否有效
            if (file_name.IsEmpty()) {
                BOOST_LOG_TRIVIAL(warning) << "PhrozenStatusPanel::update_print_file: "
                                           << "File name conversion resulted in empty string. Original: \"" 
                                           << print_file << "\", displaying N/A";
                m_project_task_panel->update_subtask_name(PHROZEN_NA_STR);
            } else {
                m_project_task_panel->update_subtask_name(wxString::Format("%s", file_name));
            }
        } catch (const std::exception& e) {
            // 字串轉換異常：記錄錯誤並顯示 N/A
            BOOST_LOG_TRIVIAL(error) << "PhrozenStatusPanel::update_print_file: "
                                     << "Exception during file name conversion: " << e.what()
                                     << ", Original: \"" << print_file << "\", displaying N/A";
            m_project_task_panel->update_subtask_name(PHROZEN_NA_STR);
        } catch (...) {
            // 未知異常：記錄錯誤並顯示 N/A
            BOOST_LOG_TRIVIAL(error) << "PhrozenStatusPanel::update_print_file: "
                                     << "Unknown exception during file name conversion. Original: \"" 
                                     << print_file << "\", displaying N/A";
            m_project_task_panel->update_subtask_name(PHROZEN_NA_STR);
        }
    } else {
        // 文件名為空：顯示 N/A
        BOOST_LOG_TRIVIAL(warning) << "PhrozenStatusPanel::update_print_file: "
                                    << "Print file name is empty, displaying N/A";
        m_project_task_panel->update_subtask_name(PHROZEN_NA_STR);
    }
    
    // ============================================
    // 調試日誌
    // ============================================
    BOOST_LOG_TRIVIAL(debug) << "PhrozenStatusPanel::update_print_file: file=\"" << print_file << "\"";
}

void PhrozenStatusPanel::update_print_time(MachineObject *obj)
{
    // ============================================
    // 防呆檢查：確保物件有效
    // ============================================
    if (!obj) {
        BOOST_LOG_TRIVIAL(warning) << "PhrozenStatusPanel::update_print_time: obj is nullptr";
        return;
    }
    
    if (!m_project_task_panel) {
        BOOST_LOG_TRIVIAL(error) << "PhrozenStatusPanel::update_print_time: m_project_task_panel is nullptr";
        return;
    }
    
    // ============================================
    // 獲取列印時間資訊
    // ============================================
    // print time
    float print_time = obj->GetPhrozenPrintTime();
    // total process time = pre-process time + print time + end time
    float total_time = obj->GetPhrozenTotalTime();
    
    // ============================================
    // 更新已列印時間（已耗費時間）
    // ============================================
    if (print_time >= 0.0f) {
        // 有效列印時間：轉換為秒數
        int print_time_seconds = static_cast<int>(print_time);
        
        // 確保秒數為非負數（防呆設計）
        if (print_time_seconds < 0) {
            BOOST_LOG_TRIVIAL(warning) << "PhrozenStatusPanel::update_print_time: "
                                       << "Print time seconds < 0, clamping to 0. "
                                       << "print_time=" << print_time;
            print_time_seconds = 0;
        }
        
        // TODO: 顯示已列印時間（已耗費時間）
        // 例如：將 print_time_seconds 格式化為 "時:分:秒" 或 "X小時Y分鐘" 格式
        // 然後更新到 UI 控件（需要確認 PrintingTaskPanel 是否有對應的 API）
        // 例如：m_project_task_panel->update_elapsed_time(print_time_seconds);
        // 或：m_project_task_panel->update_elapsed_time(format_time_string(print_time_seconds));
        
        BOOST_LOG_TRIVIAL(debug) << "PhrozenStatusPanel::update_print_time: "
                                 << "Elapsed time: " << print_time_seconds << " seconds";
    } else {
        // 無效列印時間：記錄警告
        BOOST_LOG_TRIVIAL(warning) << "PhrozenStatusPanel::update_print_time: "
                                    << "Invalid print_time: " << print_time 
                                    << ", skipping elapsed time update";
    }
    
    // ============================================
    // 驗證時間值並計算剩餘時間
    // ============================================
    if (total_time > 0.0f && print_time >= 0.0f) {
        // 有效時間值：計算剩餘時間
        float left_time = total_time - print_time;
        
        if (left_time > 0.0f) {
            // 剩餘時間為正：轉換為秒數並更新 UI
            int left_time_seconds = static_cast<int>(print_time);
            
            // 確保秒數為非負數（防呆設計）
            if (left_time_seconds < 0) {
                BOOST_LOG_TRIVIAL(warning) << "PhrozenStatusPanel::update_print_time: "
                                           << "Left time seconds < 0, clamping to 0. "
                                           << "print_time=" << print_time 
                                           << ", total_time=" << total_time
                                           << ", left_time=" << left_time;
                left_time_seconds = 0;
            }
            
            m_project_task_panel->update_left_time(left_time_seconds);
        } else {
            // 剩餘時間為負或零：顯示 N/A
            BOOST_LOG_TRIVIAL(warning) << "PhrozenStatusPanel::update_print_time: "
                                       << "Left time <= 0, displaying N/A. "
                                       << "print_time=" << print_time 
                                       << ", total_time=" << total_time
                                       << ", left_time=" << left_time;
            m_project_task_panel->update_left_time(PHROZEN_NA_STR);
        }
    } else {
        // 無效時間值：顯示 N/A
        BOOST_LOG_TRIVIAL(warning) << "PhrozenStatusPanel::update_print_time: "
                                    << "Invalid time values, displaying N/A. "
                                    << "print_time=" << print_time 
                                    << ", total_time=" << total_time;
        m_project_task_panel->update_left_time(PHROZEN_NA_STR);
    }
    
    // ============================================
    // 調試日誌
    // ============================================
    BOOST_LOG_TRIVIAL(debug) << "PhrozenStatusPanel::update_print_time: "
                             << "print_time=" << print_time 
                             << ", total_time=" << total_time;
}

void PhrozenStatusPanel::update_print_stage(MachineObject *obj)
{
    // ============================================
    // 防呆檢查：確保物件有效
    // ============================================
    if (!obj) {
        BOOST_LOG_TRIVIAL(warning) << "PhrozenStatusPanel::update_print_stage: obj is nullptr";
        return;
    }
    
    if (!m_project_task_panel) {
        BOOST_LOG_TRIVIAL(error) << "PhrozenStatusPanel::update_print_stage: m_project_task_panel is nullptr";
        return;
    }
    
    // ============================================
    // 獲取列印狀態和進度
    // ============================================
    std::string print_status = obj->GetPhrozenPrintStatus();
    float print_progress = obj->GetPhrozenPrintProgress();
    
    // 防呆檢查：確保狀態不為空
    if (print_status.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "PhrozenStatusPanel::update_print_stage: print_status is empty";
        print_status = "offline";  // 使用預設狀態
    }
    
    // ============================================
    // 根據列印狀態顯示對應的文字
    // ============================================
    wxString stage_text;
    if (print_status == "printing") {
        stage_text = _L("Printing...");
    }
    else if (print_status == "paused") {
        stage_text = _L("Paused");
    }
    else if (print_status == "complete") {
        stage_text = _L("Print Complete");
    }
    else if (print_status == "cancelled") {
        stage_text = _L("Print Cancelled");
    }
    else if (print_status == "error") {
        stage_text = _L("Print Error");
    }
    else if (print_status == "offline") {
        stage_text = _L("Offline");
    }
    else if (print_status == "standby") {
        stage_text = _L("Standby");
    }
    else {
        // 未知狀態：不顯示文字
        stage_text = wxEmptyString;
        BOOST_LOG_TRIVIAL(warning) << "PhrozenStatusPanel::update_print_stage: "
                                    << "Unknown print status: \"" << print_status 
                                    << "\", displaying empty stage text";
    }
    
    // ============================================
    // 更新階段狀態文字和進度值
    // ============================================
    if (!stage_text.IsEmpty()) {
        // 計算進度百分比值
        int progress_val = 0;
        if (print_progress >= 0.0f && print_progress <= 1.0f) {
            progress_val = static_cast<int>(print_progress * 100.0f);
            
            // 確保進度值在有效範圍內（0-100）
            if (progress_val < 0) {
                progress_val = 0;
            } else if (progress_val > 100) {
                progress_val = 100;
            }
        } else {
            // 無效進度值：使用 0
            BOOST_LOG_TRIVIAL(warning) << "PhrozenStatusPanel::update_print_stage: "
                                       << "Invalid print_progress: " << print_progress 
                                       << ", using 0 for progress value";
            progress_val = 0;
        }
        
        m_project_task_panel->update_stage_value(stage_text, progress_val);
    } else {
        // 階段文字為空：顯示空文字和 0 進度
        m_project_task_panel->update_stage_value(wxEmptyString, 0);
    }
    
    // ============================================
    // 調試日誌
    // ============================================
    BOOST_LOG_TRIVIAL(debug) << "PhrozenStatusPanel::update_print_stage: "
                             << "status=" << print_status 
                             << ", progress=" << print_progress
                             << ", stage_text=\"" << stage_text << "\"";
}

void PhrozenStatusPanel::update_thumbnail(MachineObject *obj)
{
    std::cout << "[PhrozenStatusPanel] update_thumbnail: Function called" << std::endl;
    
    // ============================================
    // 防呆檢查：確保物件有效
    // ============================================
    if (!obj) {
        std::cout << "[PhrozenStatusPanel] update_thumbnail: ERROR - obj is nullptr" << std::endl;
        BOOST_LOG_TRIVIAL(warning) << "PhrozenStatusPanel::update_thumbnail: obj is nullptr";
        return;
    }
    
    if (!m_project_task_panel) {
        std::cout << "[PhrozenStatusPanel] update_thumbnail: ERROR - m_project_task_panel is nullptr" << std::endl;
        BOOST_LOG_TRIVIAL(error) << "PhrozenStatusPanel::update_thumbnail: m_project_task_panel is nullptr";
        return;
    }
    
    // ============================================
    // 獲取當前打印的 GCode 文件名
    // ============================================
    std::string gcode_name = obj->GetPhrozenPrintFile();
    std::cout << "[PhrozenStatusPanel] update_thumbnail: GCode name = \"" << gcode_name << "\"" << std::endl;
    
    if (gcode_name.empty()) {
        std::cout << "[PhrozenStatusPanel] update_thumbnail: WARNING - GCode file name is empty" << std::endl;
        BOOST_LOG_TRIVIAL(warning) << "PhrozenStatusPanel::update_thumbnail: "
                                    << "GCode file name is empty, skipping thumbnail update";
        return;
    }
    
    // ============================================
    // 檢查緩存：如果已經有相同的縮略圖，直接使用
    // ============================================
    std::cout << "[PhrozenStatusPanel] update_thumbnail: Checking cache, cached_gcode_name = \""
              << obj->m_cached_gcode_name << "\", gcode_name = \"" << gcode_name 
              << "\", cache_size = " << obj->m_thumbnail_cache.size() << std::endl;
    BOOST_LOG_TRIVIAL(debug) << "update_thumbnail: Checking cache - cached_gcode_name=\"" 
                             << obj->m_cached_gcode_name << "\", gcode_name=\"" << gcode_name 
                             << "\", cache_size=" << obj->m_thumbnail_cache.size();
    
    if (gcode_name == obj->m_cached_gcode_name) {
        std::cout << "[PhrozenStatusPanel] update_thumbnail: Cache match found, checking cache entry..." << std::endl;
        auto cache_it = obj->m_thumbnail_cache.find(gcode_name);
        if (cache_it != obj->m_thumbnail_cache.end() && cache_it->second.IsOk()) {
            std::cout << "[PhrozenStatusPanel] update_thumbnail_path: Cache hit! Using cached thumbnail" << std::endl;
            // 緩存命中：直接使用緩存的縮略圖
            try {
                wxSize thumbnail_size = m_project_task_panel->get_bitmap_thumbnail()->GetSize();
                std::cout << "[PhrozenStatusPanel] update_thumbnail: Target thumbnail size = "
                          << thumbnail_size.x << "x" << thumbnail_size.y << std::endl;
                
                if (thumbnail_size.x > 0 && thumbnail_size.y > 0) {
                    // 從緩存獲取原始縮略圖
                    wxBitmap cached_bmp = cache_it->second;
                    wxImage cached_img = cached_bmp.ConvertToImage();
                    
                    if (cached_img.IsOk()) {
                        wxImage display_img = cached_img;
                        
                        // 記錄原始尺寸和目標尺寸，用於診斷
                        int orig_width = cached_img.GetWidth();
                        int orig_height = cached_img.GetHeight();
                        std::cout << "[PhrozenStatusPanel] update_thumbnail: Cached thumbnail size = "
                                  << orig_width << "x" << orig_height << ", target = "
                                  << thumbnail_size.x << "x" << thumbnail_size.y << std::endl;
                        
                        BOOST_LOG_TRIVIAL(info) << "PhrozenStatusPanel::update_thumbnail: "
                                                << "Cached thumbnail original size: " << orig_width << "x" << orig_height
                                                << ", target size: " << thumbnail_size.x << "x" << thumbnail_size.y;
                        
                        // 檢查是否需要縮放
                        // 如果原始圖片尺寸與控件尺寸相同，直接使用原始圖片
                        // 否則使用改進的超採樣技術：根據原始圖片大小動態調整策略
                        if (orig_width != thumbnail_size.x || orig_height != thumbnail_size.y) {
                            std::cout << "[PhrozenStatusPanel] update_thumbnail: Scaling cached thumbnail from "
                                      << orig_width << "x" << orig_height << " to "
                                      << thumbnail_size.x << "x" << thumbnail_size.y 
                                      << " (using improved supersampling)" << std::endl;
                            
                            // 改進的超採樣策略：
                            // 如果原始圖片比目標大，使用更大的超採樣因子（3x 或 4x）
                            // 這樣可以更好地保留細節
                            float scale_ratio_x = float(orig_width) / float(thumbnail_size.x);
                            float scale_ratio_y = float(orig_height) / float(thumbnail_size.y);
                            float scale_ratio = std::max(scale_ratio_x, scale_ratio_y);
                            
                            int supersample_factor = 2;
                            if (scale_ratio > 1.5f) {
                                // 原始圖片明顯大於目標，使用更大的超採樣因子
                                supersample_factor = 3;
                            }
                            if (scale_ratio > 2.0f) {
                                // 原始圖片遠大於目標，使用最大超採樣因子
                                supersample_factor = 4;
                            }
                            
                            int intermediate_width = thumbnail_size.x * supersample_factor;
                            int intermediate_height = thumbnail_size.y * supersample_factor;
                            
                            std::cout << "[PhrozenStatusPanel] update_thumbnail: Using supersample factor "
                                      << supersample_factor << " (scale ratio: " << scale_ratio << ")" << std::endl;
                            
                            // 使用 ResampleBicubic 進行更高質量的縮放（類似 GLTexture 中的方法）
                            // ResampleBicubic 提供比 Scale 更好的質量，特別適合縮小操作
                            // 第一步：放大到中間尺寸（使用雙三次插值）
                            wxImage intermediate_img = cached_img.ResampleBicubic(
                                intermediate_width, 
                                intermediate_height
                            );
                            
                            // 第二步：縮小到目標尺寸（使用雙三次插值）
                            display_img = intermediate_img.ResampleBicubic(
                                thumbnail_size.x, 
                                thumbnail_size.y
                            );
                            
                            BOOST_LOG_TRIVIAL(info) << "PhrozenStatusPanel::update_thumbnail: "
                                                     << "Rescaled cached thumbnail from "
                                                     << orig_width << "x" << orig_height
                                                     << " to " << thumbnail_size.x << "x" << thumbnail_size.y;
                        } else {
                            std::cout << "[PhrozenStatusPanel] update_thumbnail: Using original cached thumbnail size (exact match)" << std::endl;
                            BOOST_LOG_TRIVIAL(debug) << "PhrozenStatusPanel::update_thumbnail: "
                                                     << "Using original cached thumbnail size (exact match)";
                        }
                        
                        // 直接傳入 wxImage
                        m_project_task_panel->set_thumbnail_img(display_img);
                        // 設置亮度值（用於暗色模式顯示）
                        m_project_task_panel->set_brightness_value(get_brightness_value(display_img));
                        
                        std::cout << "[PhrozenStatusPanel] update_thumbnail: Successfully updated UI with cached thumbnail" << std::endl;
                        BOOST_LOG_TRIVIAL(debug) << "PhrozenStatusPanel::update_thumbnail: "
                                                 << "Using cached thumbnail for GCode: \"" << gcode_name << "\"";
                        return;  // 成功使用緩存，直接返回
                    } else {
                        std::cout << "[PhrozenStatusPanel] update_thumbnail: WARNING - Cached image is not OK: \""
                                  << gcode_name << "\", displaying broken image" << std::endl;
                        BOOST_LOG_TRIVIAL(warning) << "PhrozenStatusPanel::update_thumbnail_path: "
                                                 << "Failed to convert bitmap to image for GCode: \""
                                                 << gcode_name << "\", displaying broken image";
                        m_project_task_panel->set_thumbnail_img(m_thumbnail_brokenimg.bmp());
                    }
                } else {
                    // 縮略圖控件尺寸無效：記錄錯誤
                    std::cout << "[PhrozenStatusPanel] update_thumbnail: ERROR - Invalid thumbnail size: "
                              << thumbnail_size.x << "x" << thumbnail_size.y << ", GCode: \"" << gcode_name << "\"" << std::endl;
                    BOOST_LOG_TRIVIAL(warning) << "PhrozenStatusPanel::update_thumbnail_path: "
                                             << "Invalid thumbnail size: " << thumbnail_size.x
                                             << "x" << thumbnail_size.y
                                             << ", GCode: \"" << gcode_name << "\"";
                    m_project_task_panel->set_thumbnail_img(m_thumbnail_brokenimg.bmp());
                }
            } catch (const std::exception& e) {
                std::cout << "[PhrozenStatusPanel] update_thumbnail: EXCEPTION while using cached thumbnail: "
                          << e.what() << std::endl;
                BOOST_LOG_TRIVIAL(warning) << "PhrozenStatusPanel::update_thumbnail: "
                                           << "Exception while using cached thumbnail: " << e.what()
                                           << ", will fetch new thumbnail";
                // 緩存使用失敗，繼續獲取新的縮略圖
            }
        } else {
            std::cout << "[PhrozenStatusPanel] update_thumbnail: Cache entry not found or invalid" << std::endl;
        }
    } else {
        std::cout << "[PhrozenStatusPanel] update_thumbnail: Cache miss (different GCode name)" << std::endl;
        // Check if cache actually has the gcode_name but m_cached_gcode_name is empty
        auto cache_it = obj->m_thumbnail_cache.find(gcode_name);
        if (cache_it != obj->m_thumbnail_cache.end()) {
            std::cout << "[PhrozenStatusPanel] update_thumbnail: WARNING - Cache HAS entry for gcode_name=\"" 
                      << gcode_name << "\" but m_cached_gcode_name is empty!" << std::endl;
            BOOST_LOG_TRIVIAL(warning) << "update_thumbnail: WARNING - Cache HAS entry for gcode_name=\"" 
                                       << gcode_name << "\" but m_cached_gcode_name is empty!";
        } else {
            std::cout << "[PhrozenStatusPanel] update_thumbnail: Cache does NOT have entry for gcode_name=\"" 
                      << gcode_name << "\"" << std::endl;
        }
        m_project_task_panel->set_thumbnail_img(m_project_task_panel->get_bitmap_thumbnail_placeholder().bmp());
    }
    
    std::cout << "[PhrozenStatusPanel] update_thumbnail: Function completed for GCode=\"" << gcode_name << "\"" << std::endl;
}

void PhrozenStatusPanel::update_print_filament(MachineObject *obj)
{
    // ============================================
    // 防呆檢查：確保物件有效
    // ============================================
    if (!obj) {
        BOOST_LOG_TRIVIAL(warning) << "PhrozenStatusPanel::update_print_filament: obj is nullptr";
        return;
    }
    
    if (!m_project_task_panel) {
        BOOST_LOG_TRIVIAL(error) << "PhrozenStatusPanel::update_print_filament: m_project_task_panel is nullptr";
        return;
    }
    
    // ============================================
    // 獲取線材使用量
    // ============================================
    float print_filament = obj->GetPhrozenPrintFilamentAmount();
    
    // ============================================
    // 驗證耗材使用量並更新 UI
    // ============================================
    if (print_filament >= 0.0f) {
        // 有效耗材使用量：更新 UI
        // 注意：耗材使用量更新邏輯可以根據需要實作
        // 目前保留為預留功能，待後續實作
        
        // 驗證耗材使用量是否在合理範圍內（例如：0-10000mm）
        const float MAX_FILAMENT_AMOUNT = 10000.0f;
        if (print_filament > MAX_FILAMENT_AMOUNT) {
            BOOST_LOG_TRIVIAL(warning) << "PhrozenStatusPanel::update_print_filament: "
                                       << "Filament amount exceeds maximum (" << MAX_FILAMENT_AMOUNT 
                                       << " mm), value: " << print_filament << " mm";
        }
        
        BOOST_LOG_TRIVIAL(debug) << "PhrozenStatusPanel::update_print_filament: "
                                 << "Filament amount: " << print_filament << " mm";
        
        // TODO: 實作耗材使用量顯示邏輯
        // if (m_project_task_panel->update_filament_used) {
        //     m_project_task_panel->update_filament_used(
        //         wxString::Format("%.2f mm", print_filament));
        // }
    } else {
        // 無效耗材使用量：記錄警告
        BOOST_LOG_TRIVIAL(warning) << "PhrozenStatusPanel::update_print_filament: "
                                    << "Invalid filament amount: " << print_filament 
                                    << " mm (expected >= 0), skipping update";
    }
    
    // ============================================
    // 調試日誌
    // ============================================
    BOOST_LOG_TRIVIAL(debug) << "PhrozenStatusPanel::update_print_filament: amount=" << print_filament << " mm";
}

void PhrozenStatusPanel::update_cloud_subtask(MachineObject *obj)
{
    if (!obj) return;
    if (!obj->subtask_) return;

    if (is_task_changed(obj)) {
        obj->set_modeltask(nullptr);
        reset_printing_values();
        BOOST_LOG_TRIVIAL(info) << "monitor: change to sub task id = " << obj->subtask_->task_id;
        if (web_request.IsOk() && web_request.GetState() == wxWebRequest::State_Active) {
            BOOST_LOG_TRIVIAL(info) << "web_request: cancelled";
            web_request.Cancel();
        }
        m_start_loading_thumbnail = true;
    }

    if (m_start_loading_thumbnail) {
        update_calib_bitmap();
        if (obj->slice_info) {
            m_request_url = wxString(obj->slice_info->thumbnail_url);
            if (!m_request_url.IsEmpty()) {
                wxImage                               img;
                std::map<wxString, wxImage>::iterator it = img_list.find(m_request_url);
                if (it != img_list.end()) {
                    if (m_current_print_mode != PrintingTaskType::CALIBRATION  ||(m_calib_mode == CalibMode::Calib_Flow_Rate && m_calib_method == CalibrationMethod::CALI_METHOD_MANUAL)) {
                        img = it->second;
                        wxImage resize_img = img.Scale(m_project_task_panel->get_bitmap_thumbnail()->GetSize().x, m_project_task_panel->get_bitmap_thumbnail()->GetSize().y);
                        m_project_task_panel->set_thumbnail_img(resize_img);
                        m_project_task_panel->set_brightness_value(get_brightness_value(resize_img));
                    }
                    if (this->obj) {
                        m_project_task_panel->set_plate_index(obj->m_plate_index);
                    } else {
                        m_project_task_panel->set_plate_index(-1);
                    }
                    task_thumbnail_state = ThumbnailState::TASK_THUMBNAIL;
                    BOOST_LOG_TRIVIAL(trace) << "web_request: use cache image";
                } else {
                    web_request = wxWebSession::GetDefault().CreateRequest(this, m_request_url);
                    BOOST_LOG_TRIVIAL(trace) << "monitor: start request thumbnail, url = " << m_request_url;
                    web_request.Start();
                    m_start_loading_thumbnail = false;
                }
            }
        }
    }
}

void PhrozenStatusPanel::update_sdcard_subtask(MachineObject *obj)
{
    if (!obj) return;

    if (!m_load_sdcard_thumbnail) {
        update_calib_bitmap();
        if (m_current_print_mode != PrintingTaskType::CALIBRATION) {
            m_project_task_panel->get_bitmap_thumbnail()->SetBitmap(m_thumbnail_sdcard.bmp());
        }
        task_thumbnail_state = ThumbnailState::SDCARD_THUMBNAIL;
        m_load_sdcard_thumbnail = true;
    }
}

void PhrozenStatusPanel::reset_printing_values()
{
    m_project_task_panel->enable_pause_resume_button(false, "pause_disable");
    m_project_task_panel->enable_abort_button(false);
    m_project_task_panel->reset_printing_value();
    m_project_task_panel->update_subtask_name(PHROZEN_NA_STR);
    m_project_task_panel->show_profile_info(false);
    m_project_task_panel->update_stage_value(wxEmptyString, 0);
    m_project_task_panel->update_progress_percent(PHROZEN_NA_STR, wxEmptyString);

    m_project_task_panel->market_scoring_hide();
    m_project_task_panel->get_request_failed_panel()->Hide();
    update_basic_print_data(false);
    m_project_task_panel->update_left_time(PHROZEN_NA_STR);
    m_project_task_panel->update_layers_num(true, wxString::Format(_L("Layer: %s"), PHROZEN_NA_STR));
    update_calib_bitmap();
    
    task_thumbnail_state = ThumbnailState::PLACE_HOLDER;
    m_start_loading_thumbnail = false;
    m_load_sdcard_thumbnail   = false;
    skip_print_error = 0;
    this->Layout();
}

void PhrozenStatusPanel::on_axis_ctrl_xy(wxCommandEvent &event)
{
    if (!obj) return;

    std::string axis;
    double unit = 1.0f;
    double dir;
    double input_val;
    int speed = 3000;
    switch( event.GetInt() )
    {
        case PhrozenAxisCtrlButton::CurrentPos::AXIS_UP:    axis = "Y"; dir = 1.0f; break;
        case PhrozenAxisCtrlButton::CurrentPos::AXIS_LEFT:  axis = "X"; dir = -1.0f; break;
        case PhrozenAxisCtrlButton::CurrentPos::AXIS_DOWN:  axis = "Y"; dir = -1.0f; break;
        case PhrozenAxisCtrlButton::CurrentPos::AXIS_RIGHT: axis = "X"; dir = 1.0f; break;
        case PhrozenAxisCtrlButton::CurrentPos::AXIS_HOME:  axis = "Home"; break;
        default:
            assert( 0 && "not allow" );
            return;
    }

    if ( axis == "Home" )
    {
        if (obj->is_support_command_homing) {
            obj->command_go_home2();
        } else {
            obj->command_go_home();
        }
    }
    else
    {
        double move_range;
        switch ( event.GetExtraLong() )
        {
            case PhrozenAxisCtrlButton::CurrentPos::MOVE_STEP_01MM: move_range = 0.1f;  break;
            case PhrozenAxisCtrlButton::CurrentPos::MOVE_STEP_1MM:  move_range = 1.0f;  break;
            case PhrozenAxisCtrlButton::CurrentPos::MOVE_STEP_10MM: move_range = 10.0f; break;
            default: 
                assert(0 && "not allow"); 
                return;
        }
        move_range *= dir;
        obj->command_axis_control( axis, unit, move_range, speed);
    }

    //check is at home
    if (event.GetInt() == PhrozenAxisCtrlButton::CurrentPos::AXIS_LEFT
        || event.GetInt() == PhrozenAxisCtrlButton::CurrentPos::AXIS_RIGHT ) 
    {
        if (!obj->is_axis_at_home("X")) {
            BOOST_LOG_TRIVIAL(info) << "axis x is not at home";
            show_recenter_dialog();
            return;
        }
    }
    else if ( PhrozenAxisCtrlButton::CurrentPos::AXIS_UP
        || PhrozenAxisCtrlButton::CurrentPos::AXIS_DOWN ) \
    {
        if (!obj->is_axis_at_home("Y")) {
            BOOST_LOG_TRIVIAL(info) << "axis y is not at home";
            show_recenter_dialog();
            return;
        }
    }
}

bool PhrozenStatusPanel::check_axis_z_at_home(MachineObject* obj)
{
    if (obj) {
        if (!obj->is_axis_at_home("Z")) {
            BOOST_LOG_TRIVIAL(info) << "axis z is not at home";
            show_recenter_dialog();
            return false;
        }
        return true;
    }
    return false;
}

void PhrozenStatusPanel::on_axis_ctrl_z_up_10(wxCommandEvent &event)
{    
    if (obj) {
        obj->command_axis_control("Z", 1.0, -10.0f, 900);
        if (!check_axis_z_at_home(obj))
            return;
    }
}

void PhrozenStatusPanel::on_axis_ctrl_z_up_1(wxCommandEvent &event)
{
    if (obj) {
        obj->command_axis_control("Z", 1.0, -1.0f, 900);
        if (!check_axis_z_at_home(obj))
            return;
    }
}

void PhrozenStatusPanel::on_axis_ctrl_z_down_1(wxCommandEvent &event)
{
    if (obj) {
        obj->command_axis_control("Z", 1.0, 1.0f, 900);
        if (!check_axis_z_at_home(obj))
            return;
    }
}

void PhrozenStatusPanel::on_axis_ctrl_z_down_10(wxCommandEvent &event)
{
    if (obj) {
        obj->command_axis_control("Z", 1.0, 10.0f, 900);
        if (!check_axis_z_at_home(obj))
            return;
    }
}

void PhrozenStatusPanel::axis_ctrl_e_hint(bool up_down)
{
    if (ctrl_e_hint_dlg == nullptr) {
        ctrl_e_hint_dlg = new SecondaryCheckDialog(this->GetParent(), wxID_ANY, _L("Warning"), SecondaryCheckDialog::ButtonStyle::CONFIRM_AND_CANCEL, wxDefaultPosition, wxDefaultSize, wxCLOSE_BOX | wxCAPTION, true);
        ctrl_e_hint_dlg->update_text(_L("Please heat the nozzle to above 170°C before loading or unloading filament."));
        ctrl_e_hint_dlg->show_again_config_text = std::string("not_show_ectrl_hint");
    }
    if (up_down) {
        ctrl_e_hint_dlg->update_btn_label(_L("Confirm"), _L("Still unload"));
        ctrl_e_hint_dlg->Bind(EVT_SECONDARY_CHECK_CANCEL, [this](wxCommandEvent& e) {
            obj->command_axis_control("E", 1.0, -10.0f, 900);
            });
    }
    else {
        ctrl_e_hint_dlg->update_btn_label(_L("Confirm"), _L("Still load"));
        ctrl_e_hint_dlg->Bind(EVT_SECONDARY_CHECK_CANCEL, [this](wxCommandEvent& e) {
            obj->command_axis_control("E", 1.0, 10.0f, 900);
            });
    }
    ctrl_e_hint_dlg->on_show();
}

void PhrozenStatusPanel::on_axis_ctrl_e_up_10(wxCommandEvent &event)
{
    if (obj) {
        if (obj->m_extder_data.extders[0].temp >= TEMP_THRESHOLD_ALLOW_E_CTRL || (wxGetApp().app_config->get("not_show_ectrl_hint") == "1"))
            obj->command_axis_control("E", 1.0, -10.0f, 900);
        else
            axis_ctrl_e_hint(true);
    }
}

void PhrozenStatusPanel::on_axis_ctrl_e_down_10(wxCommandEvent &event)
{
    if (obj) {
        if (obj->m_extder_data.extders[0].temp >= TEMP_THRESHOLD_ALLOW_E_CTRL || (wxGetApp().app_config->get("not_show_ectrl_hint") == "1"))
            obj->command_axis_control("E", 1.0, 10.0f, 900);
        else
            axis_ctrl_e_hint(false);
    }
}

void PhrozenStatusPanel::on_start_unload(wxCommandEvent &event)
{
    if (obj) obj->command_ams_change_filament(false, "255", "255");
}

void PhrozenStatusPanel::on_set_chamber_temp()
{
    wxString str = m_tempCtrl_chamber->GetTextCtrl()->GetValue();
    try {
        long chamber_temp;
        if (str.ToLong(&chamber_temp) && obj) {
            set_hold_count(m_temp_chamber_timeout);
            if (chamber_temp > m_tempCtrl_chamber->get_max_temp()) {
                chamber_temp = m_tempCtrl_chamber->get_max_temp();
                m_tempCtrl_chamber->SetTagTemp(wxString::Format("%d", chamber_temp));
                m_tempCtrl_chamber->Warning(false);
            }
            obj->command_set_chamber(chamber_temp);
        }
    }
    catch (...) {
        ;
    }
}

void PhrozenStatusPanel::on_ams_guide(wxCommandEvent& event)
{
    //todo: direct to phrozen webside
    wxString ams_wiki_url = "https://wiki.bambulab.com/en/software/bambu-studio/use-ams-on-bambu-studio";
    wxLaunchDefaultBrowser(ams_wiki_url);
}

void PhrozenStatusPanel::on_print_error_done(wxCommandEvent& event)
{
    BOOST_LOG_TRIVIAL(info) << "on_print_error_done";
    if (obj) {
        obj->command_ams_control("done");
        if (m_print_error_dlg) {
            m_print_error_dlg->on_hide();
        }if (m_print_error_dlg_no_action) {
            m_print_error_dlg_no_action->on_hide();
        }
    }
}

void PhrozenStatusPanel::on_fan_changed(wxCommandEvent& event)
{
    auto type = event.GetInt();
    auto speed = atoi(event.GetString().c_str());

    if (type == MachineObject::FanType::COOLING_FAN) {
        set_hold_count(this->m_switch_nozzle_fan_timeout);
        m_switch_nozzle_fan->SetValue(speed > 0 ? true : false);
        m_switch_nozzle_fan->setFanValue(speed * 10);
    }
    else if (type == MachineObject::FanType::BIG_COOLING_FAN) {
        set_hold_count(this->m_switch_printing_fan_timeout);
        m_switch_printing_fan->SetValue(speed > 0 ? true : false);
        m_switch_printing_fan->setFanValue(speed * 10);
    }
    else if (type == MachineObject::FanType::CHAMBER_FAN) {
        set_hold_count(this->m_switch_cham_fan_timeout);
        m_switch_cham_fan->SetValue(speed > 0 ? true : false);
        m_switch_cham_fan->setFanValue(speed * 10);
    }
}

void PhrozenStatusPanel::on_cham_temp_kill_focus(wxFocusEvent& event)
{
    event.Skip();
    cham_temp_input = false;
}

void PhrozenStatusPanel::on_cham_temp_set_focus(wxFocusEvent& event)
{
    event.Skip();
    cham_temp_input = true;
}

void PhrozenStatusPanel::on_switch_speed(wxCommandEvent &event)
{
    auto now = boost::posix_time::microsec_clock::universal_time();
    if ((now - speed_dismiss_time).total_milliseconds() < 200) {
        speed_dismiss_time = now - boost::posix_time::seconds(1);
        return;
    }
#if __WXOSX__
    // MacOS has focus problem
    PopupWindow *popUp = new PopupWindow(nullptr);
#else
    PopupWindow *popUp = new PopupWindow(m_switch_speed);
#endif
    popUp->SetBackgroundColour(StateColor::darkModeColorFor(0xeeeeee));
    StepCtrl *step = new StepCtrl(popUp, wxID_ANY);
    wxSizer *sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(step, 1, wxEXPAND, 0);
    popUp->SetSizer(sizer);
    auto em = em_unit(this);
    popUp->SetSize(em * 36, em * 8);
    step->SetHint(_L("This only takes effect during printing"));
    step->AppendItem(_L("Silent"), "");
    step->AppendItem(_L("Standard"), "");
    step->AppendItem(_L("Sport"), "");
    step->AppendItem(_L("Ludicrous"), "");

    // default speed lvl
    int selected_item = 1;
    if (obj) {
        int speed_lvl_idx = obj->printing_speed_lvl - 1;
        if (speed_lvl_idx >= 0 && speed_lvl_idx < 4) {
            selected_item = speed_lvl_idx;
        }
    }
    step->SelectItem(selected_item);

    if (!obj->is_in_printing()) {
        step->Bind(wxEVT_LEFT_DOWN, [](auto& e) {
            return; });
    }

    step->Bind(EVT_STEP_CHANGED, [this](auto &e) {
        this->speed_lvl        = e.GetInt() + 1;
        if (obj) {
            set_hold_count(this->speed_lvl_timeout);
            obj->command_set_printing_speed((PrintingSpeedLevel)this->speed_lvl);
        }
    });
    popUp->Bind(wxEVT_SHOW, [this, popUp](auto &e) {
        if (!e.IsShown()) {
            popUp->Destroy();
            m_showing_speed_popup = false;
            speed_dismiss_time = boost::posix_time::microsec_clock::universal_time();
        }
        });
    
    wxPoint pos = m_switch_speed->ClientToScreen(wxPoint(0, -6));
    popUp->Position(pos, {0, m_switch_speed->GetSize().y + 12});
    popUp->Popup();
    m_showing_speed_popup = true;
}

void PhrozenStatusPanel::on_printing_fan_switch(wxCommandEvent &event)
{
   /* if (!obj) return;

    bool value = m_switch_printing_fan->GetValue();

    if (value) {
        obj->command_control_fan(MachineObject::FanType::BIG_COOLING_FAN, true);
        m_switch_printing_fan->SetValue(true);
        set_hold_count(this->m_switch_printing_fan_timeout);
    } else {
        obj->command_control_fan(MachineObject::FanType::BIG_COOLING_FAN, false);
        m_switch_printing_fan->SetValue(false);
        set_hold_count(this->m_switch_printing_fan_timeout);
    }*/
}

void PhrozenStatusPanel::on_nozzle_fan_switch(wxCommandEvent &event)
{
    m_fan_control_popup->Destroy();
    m_fan_control_popup = nullptr;
    m_fan_control_popup = new FanControlPopup(this);

    if (obj) {
        m_fan_control_popup->show_cham_fan(obj->is_support_chamber_fan);
        m_fan_control_popup->show_aux_fan(obj->is_support_aux_fan);
    }

    auto pos = m_switch_nozzle_fan->GetScreenPosition();
    pos.y = pos.y + m_switch_nozzle_fan->GetSize().y;

    int display_idx = wxDisplay::GetFromWindow(this);
    auto display = wxDisplay(display_idx).GetClientArea();


    wxSize screenSize = wxSize(display.GetWidth(), display.GetHeight());
    auto fan_popup_size = m_fan_control_popup->GetSize();

    if (screenSize.y - fan_popup_size.y < FromDIP(300)) {
        pos.x += FromDIP(50);
        pos.y = (screenSize.y - fan_popup_size.y) / 2;
    }
    m_fan_control_popup->SetPosition(pos);
    m_fan_control_popup->Popup();



    /*if (!obj) return;

    bool value = m_switch_nozzle_fan->GetValue();

    if (value) {
        obj->command_control_fan(MachineObject::FanType::COOLING_FAN, true);
        m_switch_nozzle_fan->SetValue(true);
        set_hold_count(this->m_switch_nozzle_fan_timeout);
    } else {
        obj->command_control_fan(MachineObject::FanType::COOLING_FAN, false);
        m_switch_nozzle_fan->SetValue(false);
        set_hold_count(this->m_switch_nozzle_fan_timeout);
    }*/
}
void PhrozenStatusPanel::on_lamp_switch(wxCommandEvent &event)
{
    if (!obj) return;

    bool value = m_switch_lamp->GetValue();

    if (value) {
        m_switch_lamp->SetValue(true);
        // do not update when timeout > 0
        set_hold_count(this->m_switch_lamp_timeout);
        obj->command_set_chamber_light(MachineObject::LIGHT_EFFECT::LIGHT_EFFECT_ON);
    } else {
        m_switch_lamp->SetValue(false);
        set_hold_count(this->m_switch_lamp_timeout);
        obj->command_set_chamber_light(MachineObject::LIGHT_EFFECT::LIGHT_EFFECT_OFF);
    }
}

void PhrozenStatusPanel::on_switch_vcamera(wxMouseEvent &event)
{
    assert( 0 );
    //if (!obj) return;
    //bool value = m_recording_button->get_switch_status();
    //obj->command_ipcam_record(!value);
    //m_media_play_ctrl->ToggleStream();
    //show_vcamera = m_media_play_ctrl->IsStreaming();
    if (m_camera_popup)
        m_camera_popup->sync_vcamera_state(show_vcamera);
}

void PhrozenStatusPanel::on_camera_enter(wxMouseEvent& event)
{
    assert( 0 );
    //if (obj) {
    //    if (m_camera_popup == nullptr)
    //        m_camera_popup = std::make_shared<CameraPopup>(this);
    //    m_camera_popup->check_func_supported(obj);
    //    m_camera_popup->sync_vcamera_state(show_vcamera);
    //    m_camera_popup->Bind(EVT_VCAMERA_SWITCH, &PhrozenStatusPanel::on_switch_vcamera, this);
    //    m_camera_popup->Bind(EVT_SDCARD_ABSENT_HINT, [this](wxCommandEvent &e) {
    //        if (sdcard_hint_dlg == nullptr) {
    //            sdcard_hint_dlg = new SecondaryCheckDialog(this->GetParent(), wxID_ANY, _L("Warning"), SecondaryCheckDialog::ButtonStyle::ONLY_CONFIRM);
    //            sdcard_hint_dlg->update_text(_L("Can't start this without SD card."));
    //        }
    //        sdcard_hint_dlg->on_show();
    //        });
    //    m_camera_popup->Bind(EVT_CAM_SOURCE_CHANGE, &PhrozenStatusPanel::on_camera_source_change, this);
    //    wxWindow* ctrl = (wxWindow*)event.GetEventObject();
    //    wxPoint   pos = ctrl->ClientToScreen(wxPoint(0, 0));
    //    wxSize    sz = ctrl->GetSize();
    //    pos.x += sz.x;
    //    pos.y += sz.y;
    //    m_camera_popup->SetPosition(pos);
    //    m_camera_popup->update(m_media_play_ctrl->IsStreaming());
    //    m_camera_popup->Popup();
    //}
}

void PhrozenStatusPanel::on_camera_leave(wxMouseEvent& event)
{
    if (obj && m_camera_popup) {
        m_camera_popup->Dismiss();
    }
}

void PhrozenStatusPanel::on_auto_leveling(wxCommandEvent &event)
{
    if (obj) obj->command_auto_leveling();
}

void PhrozenStatusPanel::on_xyz_abs(wxCommandEvent &event)
{
    if (obj) obj->command_xyz_abs();
}

void PhrozenStatusPanel::on_start_calibration(wxCommandEvent &event)
{
    if (calibration_dlg == nullptr) {
        calibration_dlg = new PhrozenCalibrationDlg();
        calibration_dlg->ShowModal();
    } else {
        calibration_dlg->ShowModal();
    }


    //if (obj) {
    //    if (calibration_dlg == nullptr) {
    //        calibration_dlg = new CalibrationDialog();
    //        calibration_dlg->update_machine_obj(obj);
    //        calibration_dlg->update_cali(obj);
    //        calibration_dlg->ShowModal();
    //    } else {
    //        calibration_dlg->update_machine_obj(obj);
    //        calibration_dlg->update_cali(obj);
    //        calibration_dlg->ShowModal();
    //    }
    //}
}

bool PhrozenStatusPanel::is_stage_list_info_changed(MachineObject *obj)
{
    if (!obj) return true;

    if (last_stage_list_info.size() != obj->stage_list_info.size()) return true;

    for (int i = 0; i < last_stage_list_info.size(); i++) {
        if (last_stage_list_info[i] != obj->stage_list_info[i]) return true;
    }
    last_stage_list_info = obj->stage_list_info;
    return false;
}

void PhrozenStatusPanel::set_default()
{
    BOOST_LOG_TRIVIAL(trace) << "status_panel: set_default";
    obj                  = nullptr;
    speed_lvl         = 1;
    speed_lvl_timeout = 0;
    m_switch_lamp_timeout = 0;
    m_temp_nozzle_timeout = 0;
    m_temp_bed_timeout = 0;
    m_temp_chamber_timeout = 0;
    m_switch_nozzle_fan_timeout = 0;
    m_switch_printing_fan_timeout = 0;
    m_switch_cham_fan_timeout = 0;
    m_lighting_state_timeout = 0;
    m_show_ams_group = false;
    reset_printing_values();

    #if HideOriginUiWidget
    m_pCam_switch_button->Show();
    m_tempCtrl_chamber->Show();
    #endif

    reset_temp_misc_control();
    m_pFilamentControlPanel->Hide();
    error_info_reset();
    SetFocus();
}

void PhrozenStatusPanel::show_status(int status)
{
    //[TODO] setting button enable
    //if (last_status == status) return;
    //last_status = status;
    //
    //if (((status & (int) MonitorStatus::MONITOR_DISCONNECTED) != 0)
    // || ((status & (int) MonitorStatus::MONITOR_DISCONNECTED_SERVER) != 0)
    // || ((status & (int)MonitorStatus::MONITOR_CONNECTING) != 0)
    // || ((status & (int)MonitorStatus::MONITOR_NO_PRINTER) != 0)
    //    ) {
    //    show_printing_status(false, false);
    //    m_calibration_btn->Disable();
    //    m_panel_monitoring_title->Disable();
    //} else if ((status & (int) MonitorStatus::MONITOR_NORMAL) != 0) {
    //    show_printing_status(true, true);
    //    m_calibration_btn->Disable();
    //    m_panel_monitoring_title->Enable();
    //}
}

void PhrozenStatusPanel::set_hold_count(int& count)
{
    count = COMMAND_TIMEOUT;
}

void PhrozenStatusPanel::rescale_camera_icons()
{
    m_pCam_switch_button->Rescale();
    m_pCam_light_switch_button->Rescale();
}

void PhrozenStatusPanel::on_sys_color_changed()
{
    m_project_task_panel->msw_rescale();
    m_bitmap_speed.msw_rescale();
    m_bitmap_speed_active.msw_rescale();
    m_switch_speed->SetImages(m_bitmap_speed, m_bitmap_speed);
    m_pFilamentControlPanel->msw_rescale();
    if (m_print_error_dlg) { m_print_error_dlg->msw_rescale(); }
    rescale_camera_icons();
}

void PhrozenStatusPanel::msw_rescale()
{
    init_bitmaps();
    m_project_task_panel->init_bitmaps();
    m_project_task_panel->msw_rescale();
    m_panel_monitoring_title->SetSize(wxSize(-1, FromDIP(PAGE_TITLE_HEIGHT)));
    //m_staticText_monitoring->SetMinSize(wxSize(PAGE_TITLE_TEXT_WIDTH, PAGE_TITLE_HEIGHT));
    m_bmToggleBtn_timelapse->Rescale();
    m_panel_control_title->SetSize(wxSize(-1, FromDIP(PAGE_TITLE_HEIGHT)));
    //m_staticText_control->SetMinSize(wxSize(-1, PAGE_TITLE_HEIGHT));
    //m_media_play_ctrl->msw_rescale();
    m_phButton_xy->SetMinSize(AXIS_MIN_SIZE);
    m_phButton_xy->SetSize(AXIS_MIN_SIZE);
    m_temp_extruder_line->SetSize(wxSize(FromDIP(1), -1));
    update_extruder_status(obj);
    m_bitmap_extruder_img->SetMinSize(EXTRUDER_IMAGE_SIZE);

    for (Button *btn : m_buttons) { btn->Rescale(); }
    init_scaled_buttons();


    m_phButton_xy->Rescale();
    m_tempCtrl_nozzle->SetMinSize(TEMP_CTRL_MIN_SIZE);
    m_tempCtrl_nozzle->Rescale();
    m_line_nozzle->SetSize(wxSize(-1, FromDIP(1)));
    m_tempCtrl_bed->SetMinSize(TEMP_CTRL_MIN_SIZE);
    m_tempCtrl_bed->Rescale();
    m_tempCtrl_chamber->SetMinSize(TEMP_CTRL_MIN_SIZE);
    m_tempCtrl_chamber->Rescale();

    m_bitmap_speed.msw_rescale();
    m_bitmap_speed_active.msw_rescale();

    m_switch_speed->SetImages(m_bitmap_speed, m_bitmap_speed);
    m_switch_speed->SetMinSize(MISC_BUTTON_2FAN_SIZE);
    m_switch_speed->Rescale();
    m_switch_lamp->SetImages(m_bitmap_lamp_on, m_bitmap_lamp_off);
    m_switch_lamp->SetMinSize(MISC_BUTTON_2FAN_SIZE);
    m_switch_lamp->Rescale();
    m_switch_nozzle_fan->SetImages(m_bitmap_fan_on, m_bitmap_fan_off);
    m_switch_nozzle_fan->Rescale();
    m_switch_printing_fan->SetImages(m_bitmap_fan_on, m_bitmap_fan_off);
    m_switch_printing_fan->Rescale();
    m_switch_cham_fan->SetImages(m_bitmap_fan_on, m_bitmap_fan_off);
    m_switch_cham_fan->Rescale();

    m_pFilamentControlPanel->msw_rescale();

    m_calibration_btn->SetMinSize(wxSize(-1, FromDIP(26)));
    m_calibration_btn->Rescale();

    rescale_camera_icons();

    Layout();
    Refresh();
}


// =============== phrozen checked done =============== //

void PhrozenStatusPanel::on_nozzle_temp_kill_focus(wxFocusEvent &event)
{
    event.Skip();
    nozzle_temp_input = false;

    wxCommandEvent finishEvent(wxCUSTOMEVT_SET_TEMP_FINISH);
    finishEvent.SetInt( (int)PhrozenParamControl::Temperature_Nozzle );
    wxPostEvent(this, finishEvent);
}

void PhrozenStatusPanel::on_nozzle_temp_set_focus(wxFocusEvent &event)
{
    event.Skip();
    nozzle_temp_input = true;
}

void PhrozenStatusPanel::on_bed_temp_kill_focus(wxFocusEvent &event)
{
    event.Skip();
    bed_temp_input = false;

    wxCommandEvent finishEvent(wxCUSTOMEVT_SET_TEMP_FINISH);
    finishEvent.SetInt( (int)PhrozenParamControl::Temperature_HeatedBed );
    wxPostEvent(this, finishEvent);
}

void PhrozenStatusPanel::on_bed_temp_set_focus(wxFocusEvent &event)
{
    event.Skip();
    bed_temp_input = true;
}

void PhrozenStatusPanel::on_cooling_auxiliary_kill_focus(wxFocusEvent &event)
{
    event.Skip();
    cooling_auxiliary_input = false;

    wxCommandEvent finishEvent(wxCUSTOMEVT_SET_TEMP_FINISH);
    finishEvent.SetInt( (int)PhrozenParamControl::Cooling_Auxiliary );
    wxPostEvent(this, finishEvent);
}

void PhrozenStatusPanel::on_cooling_auxiliary_set_focus(wxFocusEvent &event)
{
    event.Skip();
    cooling_auxiliary_input = true;
}

void PhrozenStatusPanel::on_cooling_part_kill_focus(wxFocusEvent &event)
{
    event.Skip();
    cooling_part_input = false;

    wxCommandEvent finishEvent(wxCUSTOMEVT_SET_TEMP_FINISH);
    finishEvent.SetInt( (int)PhrozenParamControl::Cooling_Part );
    wxPostEvent(this, finishEvent);
}

void PhrozenStatusPanel::on_cooling_part_set_focus(wxFocusEvent &event)
{
    event.Skip();
    cooling_part_input = true;
}

void PhrozenStatusPanel::on_cooling_shield_kill_focus(wxFocusEvent &event)
{
    event.Skip();
    cooling_shield_input = false;

    wxCommandEvent finishEvent(wxCUSTOMEVT_SET_TEMP_FINISH);
    finishEvent.SetInt( (int)PhrozenParamControl::Cooling_Shield );
    wxPostEvent(this, finishEvent);
}

void PhrozenStatusPanel::on_cooling_shield_set_focus(wxFocusEvent &event)
{
    event.Skip();
    cooling_shield_input = true;
}


void PhrozenStatusPanel::on_set_nozzle_temp()
{
    long nozzle_temp = m_spTemp_nozzle_ctrl->GetValue();
    try {
#ifdef __APPLE__
        if (!obj){
            obj = wxGetApp().GetPhrozenMachineObject();
        }
#endif
        if (obj) {
            set_hold_count(m_temp_nozzle_timeout);
            
            if (nozzle_temp > obj->GetPhrozenNozzleTemperature_limit()) {
                nozzle_temp = obj->GetPhrozenNozzleTemperature_limit();
                //todo add warning for phrozen
                //m_tempCtrl_nozzle->SetTagTemp(wxString::Format("%d", nozzle_temp));
                //m_tempCtrl_nozzle->Warning(false);
            }
            obj->SetPhrozenCommand_nozzle_temp(nozzle_temp);
        }
    } catch (...) {
        ;
    }

#if 0
    wxString str = m_tempCtrl_nozzle->GetTextCtrl()->GetValue();
    try {
        long nozzle_temp;
        if (str.ToLong(&nozzle_temp) && obj) {
            set_hold_count(m_temp_nozzle_timeout);
            if (nozzle_temp > m_tempCtrl_nozzle->get_max_temp()) {
                nozzle_temp = m_tempCtrl_nozzle->get_max_temp();
                m_tempCtrl_nozzle->SetTagTemp(wxString::Format("%d", nozzle_temp));
                m_tempCtrl_nozzle->Warning(false);
            }
            obj->command_set_nozzle(nozzle_temp);
        }
    } catch (...) {
        ;
    }
#endif
}

void PhrozenStatusPanel::on_set_bed_temp()
{
    long bed_temp = m_spTemp_heatedBed_ctrl->GetValue();
    try {
#ifdef __APPLE__
        if (!obj){
            obj = wxGetApp().GetPhrozenMachineObject();
        }
#endif
        if (obj) {
            set_hold_count(m_temp_bed_timeout);
            int limit = obj->GetPhrozenBedTemperature_limit();
            if (bed_temp >= limit) {
                BOOST_LOG_TRIVIAL(info) << "can not set over limit = " << limit << ", set temp = " << bed_temp;
                bed_temp = limit;
                //Todo add warning for phrozen
                //m_tempCtrl_bed->SetTagTemp(wxString::Format("%d", bed_temp));
                //m_tempCtrl_bed->Warning(false);
            }
            obj->SetPhrozenCommand_bed_temp(bed_temp);
        }
    } catch (...) {
        ;
    }

#if 0 //ref 
    wxString str = m_tempCtrl_bed->GetTextCtrl()->GetValue();
    try {
        long bed_temp;
        if (str.ToLong(&bed_temp) && obj) {
            set_hold_count(m_temp_bed_timeout);
            int limit = obj->get_bed_temperature_limit();
            if (bed_temp >= limit) {
                BOOST_LOG_TRIVIAL(info) << "can not set over limit = " << limit << ", set temp = " << bed_temp;
                bed_temp = limit;
                m_tempCtrl_bed->SetTagTemp(wxString::Format("%d", bed_temp));
                m_tempCtrl_bed->Warning(false);
            }
            obj->command_set_bed(bed_temp);
        }
    } catch (...) {
        ;
    }
#endif
}

void PhrozenStatusPanel::on_set_cooling_auxiliary()
{
    long bed_temp = m_spCooling_auxiliary_ctrl->GetValue();
    try {
#ifdef __APPLE__
        if (!obj){
            obj = wxGetApp().GetPhrozenMachineObject();
        }
#endif
        if (obj) {
            set_hold_count(m_cooling_auxiliary_timeout);
            int limit = obj->GetPhrozenCoolingPower_limit();
            if (bed_temp >= limit) {
                BOOST_LOG_TRIVIAL(info) << "can not set over limit = " << limit << ", set temp = " << bed_temp;
                bed_temp = limit;
                //Todo add warning for phrozen
                //m_tempCtrl_bed->SetTagTemp(wxString::Format("%d", bed_temp));
                //m_tempCtrl_bed->Warning(false);
            }
            obj->SetPhrozenCommand_cooling_auxiliary(bed_temp);
        }
    } catch (...) {
        ;
    }
}

void PhrozenStatusPanel::on_set_cooling_part()
{
    long bed_temp = m_spCooling_part_ctrl->GetValue();
    try {
#ifdef __APPLE__
        if (!obj){
            obj = wxGetApp().GetPhrozenMachineObject();
        }
#endif
        if (obj) {
            set_hold_count(m_cooling_part_timeout);
            int limit = obj->GetPhrozenCoolingPower_limit();
            if (bed_temp >= limit) {
                BOOST_LOG_TRIVIAL(info) << "can not set over limit = " << limit << ", set temp = " << bed_temp;
                bed_temp = limit;
                //Todo add warning for phrozen
                //m_tempCtrl_bed->SetTagTemp(wxString::Format("%d", bed_temp));
                //m_tempCtrl_bed->Warning(false);
            }
            obj->SetPhrozenCommand_cooling_part(bed_temp);
        }
    } catch (...) {
        ;
    }
}

void PhrozenStatusPanel::on_set_cooling_shield()
{
    long bed_temp = m_spCooling_shield_ctrl->GetValue();
    try {
#ifdef __APPLE__
        if (!obj){
            obj = wxGetApp().GetPhrozenMachineObject();
        }
#endif
        if (obj) {
            set_hold_count(m_cooling_shield_timeout);
            int limit = obj->GetPhrozenCoolingPower_limit();
            if (bed_temp >= limit) {
                BOOST_LOG_TRIVIAL(info) << "can not set over limit = " << limit << ", set temp = " << bed_temp;
                bed_temp = limit;
                //Todo add warning for phrozen
                //m_tempCtrl_bed->SetTagTemp(wxString::Format("%d", bed_temp));
                //m_tempCtrl_bed->Warning(false);
            }
            obj->SetPhrozenCommand_cooling_shield(bed_temp);
        }
    } catch (...) {
        ;
    }
}

void PhrozenStatusPanel::on_print_speed_changed( PhrozenPrintSpeed eLevel )
{
    try {
#ifdef __APPLE__
        if (!obj){
            obj = wxGetApp().GetPhrozenMachineObject();
        }
#endif
        if (obj) {
            set_hold_count(m_print_speed_timeout);
            obj->SetPhrozenCommand_print_speed( print_speed_enum_to_percent( eLevel ) );
        }
    } catch (...) {
        ;
    }
}

void PhrozenStatusPanel::on_nozzle_movement_range_mouse_left_down( wxMouseEvent& event )
{
    wxToggleButton* btn = dynamic_cast<wxToggleButton*>(event.GetEventObject());
    if (!btn) {
        event.Skip();
        return;
    }

    if (btn->GetValue()) {
        // is checked state, so ignore
        event.Skip(false); // block state change
    } else {
        for ( auto kItem : m_kNozzleMovementRangeButtons )
        {
            if ( kItem.second->GetValue() ) {
                kItem.second->SetValue( false );
                break;
            }
        }
        // button toggled not checked, let it continue pass event to change state
        event.Skip();
    }

}

void PhrozenStatusPanel::on_nozzle_offset_range_mouse_left_down( wxMouseEvent& event )
{
    wxToggleButton* btn = dynamic_cast<wxToggleButton*>(event.GetEventObject());
    if (!btn) {
        event.Skip();
        return;
    }

    if (btn->GetValue()) {
        // is checked state, so ignore
        event.Skip(false); // block state change
    } else {
        for ( auto kItem : m_kNozzleOffsetRangeButtons )
        {
            if ( kItem.second->GetValue() ) {
                kItem.second->SetValue( false );
                break;
            }
        }
        // button toggled not checked, let it continue pass event to change state
        event.Skip();
    }
}

void PhrozenStatusPanel::on_manual_movement_changed( PhrozenMovement eMoveType )
{
    try {
#ifdef __APPLE__
        if (!obj){
            obj = wxGetApp().GetPhrozenMachineObject();
        }
#endif
        if (obj) {
             float fMoveRange = 0.0f;
            switch( eMoveType )
            {
                case PhrozenMovement::Nozzle_X_Positive:
                    fMoveRange = get_selected_nozzle_movement_range();
                    obj->SetPhrozenCommand_nozzle_movement("x", fMoveRange);
                    break;
                case PhrozenMovement::Nozzle_X_Negative:
                    fMoveRange = get_selected_nozzle_movement_range();
                    obj->SetPhrozenCommand_nozzle_movement("x", -fMoveRange);
                    break;
                case PhrozenMovement::Nozzle_Y_Positive:
                    fMoveRange = get_selected_nozzle_movement_range();
                    obj->SetPhrozenCommand_nozzle_movement("y", fMoveRange);
                    break;
                case PhrozenMovement::Nozzle_Y_Negative:
                    fMoveRange = get_selected_nozzle_movement_range();
                    obj->SetPhrozenCommand_nozzle_movement("y", -fMoveRange);
                    break;
                case PhrozenMovement::Nozzle_Z_Positive:
                    fMoveRange = get_selected_nozzle_movement_range();
                    obj->SetPhrozenCommand_nozzle_movement("z", fMoveRange);
                    break;
                case PhrozenMovement::Nozzle_Z_Negative:
                    fMoveRange = get_selected_nozzle_movement_range();
                    obj->SetPhrozenCommand_nozzle_movement("z", -fMoveRange);
                    break;
                case PhrozenMovement::Nozzle_Offset_Positive:
                    fMoveRange = get_selected_nozzle_offset_range();
                    obj->SetPhrozenCommand_nozzle_offset(fMoveRange);
                    break;
                case PhrozenMovement::Nozzle_Offset_Negative:
                    fMoveRange = get_selected_nozzle_offset_range();
                    obj->SetPhrozenCommand_nozzle_offset(-fMoveRange);
                    break;
                case PhrozenMovement::Nozzle_Home_XY:
                    obj->SetPhrozenCommand_nozzle_movement("home_xy", -fMoveRange);
                    break;
                default:
                    assert( 0 && "not implement" );
            }
        }
    } catch (...) {
        ;
    }

    
}

#pragma endregion

} // namespace GUI
} // namespace Slic3r
