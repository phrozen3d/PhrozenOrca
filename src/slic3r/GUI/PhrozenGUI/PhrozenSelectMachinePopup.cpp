#include "PhrozenSelectMachinePopup.hpp"
#include "../I18N.hpp"

#include "libslic3r/Utils.hpp"
#include "libslic3r/Thread.hpp"

#include "slic3r/Utils/WxFontUtils.hpp"

#include "../GUI.hpp"
#include "../GUI_App.hpp"
#include "../GUI_Preview.hpp"
#include "../MainFrame.hpp"
#include "../format.hpp"
#include "../Widgets/ProgressDialog.hpp"
#include "../Widgets/RoundedRectangle.hpp"
#include "../Widgets/StaticBox.hpp"
#include "../ConnectPrinter.hpp"


#include <wx/progdlg.h>
#include <wx/clipbrd.h>
#include <wx/dcgraph.h>
#include <wx/mstream.h>
#include <miniz.h>
#include <algorithm>
#include "../Plater.hpp"
#include "../Notebook.hpp"
#include "../BitmapCache.hpp"
#include "../BindDialog.hpp"
#include "PhrozenDeviceManager.hpp"

namespace Slic3r { namespace GUI {



wxDEFINE_EVENT(EVT_PHROZEN_UPDATE_USER_MACHINE_LIST, wxCommandEvent);
wxDEFINE_EVENT(EVT_PHROZEN_CONNECT_MACHINE_BY_IP, wxCommandEvent);
wxDEFINE_EVENT(EVT_PHROZEN_DISCONNECT_MACHINE, wxCommandEvent);
wxDEFINE_EVENT(EVT_PHROZEN_UPDATE_MACHINE_TITLE, wxCommandEvent);

//PhrozenIpConnectDialog
wxDEFINE_EVENT(EVT_PHROZEN_UPDATE_CONNECT_MSG, wxCommandEvent);
wxDEFINE_EVENT(EVT_PHROZEN_IP_CONNECT_SUCCESS, wxCommandEvent);


#define INITIAL_NUMBER_OF_MACHINES 0
#define MACHINE_LIST_REFRESH_INTERVAL 1100
#define STRING_PHROZEN_MACHINE_SEARCHING _L("Searching...");
wxString strSearching = STRING_PHROZEN_MACHINE_SEARCHING;

#define WRAP_GAP FromDIP(2)

#pragma region  PhrozenInputIpAddressDialog
PhrozenInputIpAddressDialog::PhrozenInputIpAddressDialog(wxWindow *parent)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe),
                wxID_ANY,
                _L("Connect the printer using IP and access code"),
                wxDefaultPosition,
                wxDefaultSize,
                wxCAPTION | wxCLOSE_BOX)
{
    SetBackgroundColour(*wxWHITE);
    m_result                       = -1;
    wxBoxSizer *m_sizer_body       = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *m_sizer_main       = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *m_sizer_main_left  = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *m_sizer_main_right = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *m_sizer_msg        = new wxBoxSizer(wxHORIZONTAL);
    auto        m_line_top         = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));

    comfirm_before_enter_text = _L("Step 1. Please confirm Phrozen Orca and your printer are in the same LAN.");
    comfirm_after_enter_text  = _L("Step 2. Check ip address from your phrozen machine and key in to connect.");

    m_tip1 = new Label(this, ::Label::Body_13, comfirm_before_enter_text, LB_AUTO_WRAP);
    m_tip1->SetMinSize(wxSize(FromDIP(352), -1));
    m_tip1->SetMaxSize(wxSize(FromDIP(352), -1));
    m_tip1->Wrap(FromDIP(352));

    m_tip2 = new Label(this, ::Label::Body_13, comfirm_after_enter_text, LB_AUTO_WRAP);
    m_tip2->SetMinSize(wxSize(FromDIP(352), -1));
    m_tip2->SetMaxSize(wxSize(FromDIP(352), -1));

    ip_input_top_panel = new wxPanel(this);

    ip_input_top_panel->SetBackgroundColour(*wxWHITE);

    auto m_input_top_sizer = new wxBoxSizer(wxVERTICAL);

    /*top input*/
    auto m_input_tip_area = new wxBoxSizer(wxHORIZONTAL);
    auto m_input_area     = new wxBoxSizer(wxHORIZONTAL);

    m_tips_ip = new Label(ip_input_top_panel, _L("IP"));
    m_tips_ip->SetMinSize(wxSize(FromDIP(352), -1));
    m_tips_ip->SetMaxSize(wxSize(FromDIP(352), -1));

    m_input_ip = new TextInput(ip_input_top_panel, wxEmptyString, wxEmptyString);
    m_input_ip->Bind(wxEVT_TEXT, &PhrozenInputIpAddressDialog::on_text, this);
    m_input_ip->SetMinSize(wxSize(FromDIP(352), FromDIP(28)));
    m_input_ip->SetMaxSize(wxSize(FromDIP(352), FromDIP(28)));

    m_input_tip_area->Add(m_tips_ip, 0, wxALIGN_CENTER, 0);

    m_input_area->Add(m_input_ip, 0, wxALIGN_CENTER, 0);

    m_input_top_sizer->Add(m_input_tip_area, 0, wxRIGHT | wxEXPAND, FromDIP(18));
    m_input_top_sizer->Add(0, 0, 0, wxTOP, FromDIP(4));
    m_input_top_sizer->Add(m_input_area, 0, wxRIGHT | wxEXPAND, FromDIP(18));

    ip_input_top_panel->SetSizer(m_input_top_sizer);
    ip_input_top_panel->Layout();
    ip_input_top_panel->Fit();

    /*other*/
    m_test_right_msg = new Label(this, Label::Body_13, wxEmptyString, LB_AUTO_WRAP);
    m_test_right_msg->SetForegroundColour(wxColour(240, 94, 32));
    m_test_right_msg->Hide();

    m_test_wrong_msg = new Label(this, Label::Body_13, wxEmptyString, LB_AUTO_WRAP);
    m_test_wrong_msg->SetForegroundColour(wxColour(208, 27, 27));
    m_test_wrong_msg->Hide();

    m_tip4 = new Label(this, Label::Body_12, _L("Where to find your printer's IP and Access Code?"), LB_AUTO_WRAP);
    m_tip4->SetMinSize(wxSize(FromDIP(352), -1));
    m_tip4->SetMaxSize(wxSize(FromDIP(352), -1));

    m_trouble_shoot = new wxHyperlinkCtrl(this, wxID_ANY, "How to trouble shooting", "");

    m_img_help = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("PhrozenImages/machine_ip_location", this, 198), wxDefaultPosition, wxSize(FromDIP(352), -1), 0);
    auto m_sizer_button = new wxBoxSizer(wxHORIZONTAL);

    m_button_ok = new Button(this, _L("Connect"));
    m_button_ok->SetBorderColor(*wxWHITE);
    m_button_ok->SetTextColor(wxColour(0xFFFFFE));
    m_button_ok->SetFont(Label::Body_12);
    m_button_ok->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetCornerRadius(FromDIP(12));
    m_button_ok->Bind(wxEVT_LEFT_DOWN, &PhrozenInputIpAddressDialog::on_ok, this);
    m_button_ok->Enable(false);
    m_button_ok->SetBackgroundColor(wxColour(0x90, 0x90, 0x90));
    m_button_ok->SetBorderColor(wxColour(0x90, 0x90, 0x90));

    m_sizer_button->AddStretchSpacer();
    m_sizer_button->Add(m_button_ok, 0, wxALL, FromDIP(5));
    m_sizer_button->Layout();

    auto m_step_icon_panel1 = new wxWindow(this, wxID_ANY);
    auto m_step_icon_panel2 = new wxWindow(this, wxID_ANY);

    m_step_icon_panel1->SetBackgroundColour(*wxWHITE);
    m_step_icon_panel2->SetBackgroundColour(*wxWHITE);

    auto m_sizer_step_icon_panel1 = new wxBoxSizer(wxVERTICAL);
    auto m_sizer_step_icon_panel2 = new wxBoxSizer(wxVERTICAL);

    m_img_step1 = new wxStaticBitmap(m_step_icon_panel1, wxID_ANY, create_scaled_bitmap("ip_address_step", this, 6), wxDefaultPosition, wxSize(FromDIP(6), FromDIP(6)), 0);
    m_img_step2 = new wxStaticBitmap(m_step_icon_panel2, wxID_ANY, create_scaled_bitmap("ip_address_step", this, 6), wxDefaultPosition, wxSize(FromDIP(6), FromDIP(6)), 0);

    m_step_icon_panel1->SetSizer(m_sizer_step_icon_panel1);
    m_step_icon_panel1->Layout();
    m_step_icon_panel1->Fit();

    m_step_icon_panel2->SetSizer(m_sizer_step_icon_panel2);
    m_step_icon_panel2->Layout();
    m_step_icon_panel2->Fit();

    m_sizer_step_icon_panel1->Add(m_img_step1, 0, wxALIGN_CENTER | wxALL, FromDIP(5));
    m_sizer_step_icon_panel2->Add(m_img_step2, 0, wxALIGN_CENTER | wxALL, FromDIP(5));

    m_step_icon_panel1->SetMinSize(wxSize(-1, m_tip1->GetBestSize().y));
    m_step_icon_panel1->SetMaxSize(wxSize(-1, m_tip1->GetBestSize().y));

    m_step_icon_panel2->SetMinSize(wxSize(-1, m_tip2->GetBestSize().y));
    m_step_icon_panel2->SetMaxSize(wxSize(-1, m_tip2->GetBestSize().y));

    
    m_sizer_msg->Layout();

    m_sizer_main_left->Add(m_step_icon_panel1, 0, wxEXPAND, 0);
    m_sizer_main_left->Add(0, 0, 0, wxTOP, FromDIP(20));
    m_sizer_main_left->Add(m_step_icon_panel2, 0, wxEXPAND, 0);

    m_sizer_main_left->Layout();

    m_trouble_shoot->Hide();

    m_sizer_main_right->Add(m_tip1, 0, wxRIGHT | wxEXPAND, FromDIP(18));
    m_sizer_main_right->Add(0, 0, 0, wxTOP, FromDIP(20));
    m_sizer_main_right->Add(m_tip2, 0, wxRIGHT | wxEXPAND, FromDIP(18));
    m_sizer_main_right->Add(0, 0, 0, wxTOP, FromDIP(12));
    m_sizer_main_right->Add(m_tip4, 0, wxRIGHT | wxEXPAND, FromDIP(18));
    m_sizer_main_right->Add(0, 0, 0, wxTOP, FromDIP(3));
    m_sizer_main_right->Add(m_img_help, 0, 0, 0);
    m_sizer_main_right->Add(0, 0, 0, wxTOP, FromDIP(12));
    m_sizer_main_right->Add(ip_input_top_panel, 0, wxRIGHT|wxEXPAND, FromDIP(18));
    m_sizer_main_right->Add(0, 0, 0, wxTOP, FromDIP(4));
    //m_sizer_main_right->Add(m_button_ok, 0,  wxRIGHT, FromDIP(18));
    m_sizer_main_right->Add(0, 0, 0, wxTOP, FromDIP(4));
    m_sizer_main_right->Add(m_test_right_msg, 0, wxRIGHT|wxEXPAND, FromDIP(18));
    m_sizer_main_right->Add(m_test_wrong_msg, 0, wxRIGHT|wxEXPAND, FromDIP(18));

    m_sizer_main_right->Add(0, 0, 0, wxTOP, FromDIP(4));
    m_sizer_main_right->Layout();
   
    m_sizer_main->Add(m_sizer_main_left, 0, wxLEFT, FromDIP(18));
    m_sizer_main->Add(m_sizer_main_right, 0, wxLEFT|wxEXPAND, FromDIP(4));
    m_sizer_main->Layout();

    m_sizer_body->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_body->Add(0, 0, 0, wxTOP, FromDIP(10));
    m_sizer_body->Add(m_sizer_main, 0, wxRIGHT, FromDIP(10));
    m_sizer_body->Add(0, 0, 0, wxTOP, FromDIP(4));
    m_sizer_body->Add(m_sizer_msg, 0, wxLEFT|wxEXPAND, FromDIP(18));
    m_sizer_body->Add(0, 0, 0, wxTOP, FromDIP(4));
    m_sizer_body->Add(m_trouble_shoot, 0, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(40));
    m_sizer_body->Add(0, 0, 0, wxTOP, FromDIP(8));
    m_sizer_body->Add(m_sizer_button, 0, wxRIGHT | wxEXPAND, FromDIP(25));
    m_sizer_body->Add(0, 0, 0, wxTOP, FromDIP(10));
    m_sizer_body->Layout();

    SetSizer(m_sizer_body);
    Layout();
    Fit();

    CentreOnParent(wxBOTH);
    Move(wxPoint(GetScreenPosition().x, GetScreenPosition().y - FromDIP(50)));
    wxGetApp().UpdateDlgDarkUI(this);
}

void PhrozenInputIpAddressDialog::on_cancel()
{
    if (m_thread) {
        m_thread->interrupt();
        m_thread->detach();
        delete m_thread;
        m_thread = nullptr;
    }
    
    EndModal(wxID_CANCEL);
}


void PhrozenInputIpAddressDialog::update_title(wxString title)
{
    SetTitle(title);
}

void PhrozenInputIpAddressDialog::update_test_msg(wxString msg,bool connected)
{
    if (msg.empty()) {
        m_test_right_msg->Hide();
        m_test_wrong_msg->Hide();
    }
    else {
         if(connected){
             m_test_right_msg->Show();
             m_test_right_msg->SetLabelText(msg);
             m_test_right_msg->SetMinSize(wxSize(FromDIP(352), -1));
             m_test_right_msg->SetMaxSize(wxSize(FromDIP(352), -1));
         }
         else{
             m_test_wrong_msg->Show();
             m_test_wrong_msg->SetLabelText(msg);
             m_test_wrong_msg->SetMinSize(wxSize(FromDIP(352), -1));
             m_test_wrong_msg->SetMaxSize(wxSize(FromDIP(352), -1));
             wxCommandEvent e;
             on_text(e);
         }
    }

    Layout();
    Fit();
}

bool PhrozenInputIpAddressDialog::isIp(std::string ipstr)
{
    istringstream ipstream(ipstr);
    int num[4];
    char point[3];
    string end;
    ipstream >> num[0] >> point[0] >> num[1] >> point[1] >> num[2] >> point[2] >> num[3] >> end;
    for (int i = 0; i < 3; ++i) {
        if (num[i] < 0 || num[i]>255) return false;
        if (point[i] != '.') return false;
    }
    if (num[3] < 0 || num[3]>255) return false;
    if (!end.empty()) return false;
    return true;
}

void PhrozenInputIpAddressDialog::on_ok(wxMouseEvent& evt)
{
    m_test_right_msg->Hide();
    m_test_wrong_msg->Hide();
    m_trouble_shoot->Hide();
    std::string str_ip = m_input_ip->GetTextCtrl()->GetValue().ToStdString();

    m_button_ok->Enable(false);
    m_button_ok->SetBackgroundColor(wxColour(0x90, 0x90, 0x90));
    m_button_ok->SetBorderColor(wxColour(0x90, 0x90, 0x90));

    Refresh();
    Layout();
    Fit();

    PhrozenIpConnectDialog kConnect(this);
    kConnect.set_ip_address( str_ip );
    kConnect.Show();
    //auto kState = kConnect.ShowModal();
    if (  kConnect.ShowModal() == wxID_YES  )//( kConnect.IsConnectSuccess() )
    {
        on_cancel();
    }
    else
    {
        SetFocus();
        // Connection failed, re-enable button with proper styling
        m_button_ok->Enable(true);
    
        wxColour kPressedColor = wxColour( AMS_CONTROL_BRAND_COLOUR.Red()/2, AMS_CONTROL_BRAND_COLOUR.Green()/2, AMS_CONTROL_BRAND_COLOUR.Blue()/2 );
        wxColour kHoveredColor = wxColour( AMS_CONTROL_BRAND_COLOUR.Red(), AMS_CONTROL_BRAND_COLOUR.Green()/2, AMS_CONTROL_BRAND_COLOUR.Blue() );
        StateColor btn_bg_phrozen(//std::pair<wxColour, int>(kPressedColor, StateColor::Pressed),
                                  std::pair<wxColour, int>(kHoveredColor, StateColor::Hovered),
                                  std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));
    
        m_button_ok->SetBackgroundColor(btn_bg_phrozen);
        m_button_ok->SetBorderColor(*wxWHITE);
        m_button_ok->SetTextColor(StateColor::darkModeColorFor("#FFFFFE"));
    
        Refresh();
        Layout();
        Fit();
    }

    //m_thread = new boost::thread(boost::bind(&PhrozenInputIpAddressDialog::workerPhrozenMonitorThreadFunc, this, str_ip));
}

void PhrozenInputIpAddressDialog::on_text(wxCommandEvent &evt)
{
    auto str_ip = m_input_ip->GetTextCtrl()->GetValue();

    const auto enable_btn = [](Button* btn, bool enabled) {
        btn->Enable(enabled);
        if (enabled) {
            wxColour kPressedColor = wxColour( AMS_CONTROL_BRAND_COLOUR.Red()/2, AMS_CONTROL_BRAND_COLOUR.Green()/2, AMS_CONTROL_BRAND_COLOUR.Blue()/2 );
            wxColour kHoveredColor = wxColour( AMS_CONTROL_BRAND_COLOUR.Red(), AMS_CONTROL_BRAND_COLOUR.Green()/2, AMS_CONTROL_BRAND_COLOUR.Blue() );
            StateColor btn_bg_phrozen(//std::pair<wxColour, int>(kPressedColor, StateColor::Pressed), 
                                      std::pair<wxColour, int>(kHoveredColor, StateColor::Hovered),
                                      std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));

            btn->SetTextColor(StateColor::darkModeColorFor("#FFFFFE"));
            btn->SetBackgroundColor(btn_bg_phrozen);
        } else {
            btn->SetBackgroundColor(wxColour(0x90, 0x90, 0x90));
            btn->SetBorderColor(wxColour(0x90, 0x90, 0x90));
        }
    };

    if (isIp(str_ip.ToStdString())) {
        enable_btn(m_button_ok, true);
    } else {
        enable_btn(m_button_ok, false);
    }
}

PhrozenInputIpAddressDialog::~PhrozenInputIpAddressDialog()
{

}

void PhrozenInputIpAddressDialog::on_dpi_changed(const wxRect& suggested_rect)
{

}
#pragma endregion 

 #pragma region  PhrozenIpConnectDialog
PhrozenIpConnectDialog::PhrozenIpConnectDialog(wxWindow *parent)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe),
                wxID_ANY,
                _L("Connecting to printer"),
                wxDefaultPosition,
                wxDefaultSize,
                wxCAPTION )
{
    SetBackgroundColour(*wxWHITE);

    wxBoxSizer *m_sizer_body = new wxBoxSizer(wxVERTICAL);
    auto m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));

    m_test_msg = new Label(this, Label::Body_13, wxEmptyString, LB_AUTO_WRAP);
    m_test_msg->SetForegroundColour(wxColour(240, 94, 32));
    m_test_msg->SetMinSize(wxSize(FromDIP(352), -1));
    m_test_msg->SetMaxSize(wxSize(FromDIP(352), -1));

    m_status_bar = std::make_shared<BBLStatusBarSend>(this);
    m_status_bar->get_panel()->Hide();

    m_sizer_body->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_body->Add(0, 0, 0, wxTOP, FromDIP(20));
    m_sizer_body->Add(m_test_msg, 0, wxALL | wxEXPAND, FromDIP(18));
    m_sizer_body->Add(0, 0, 0, wxTOP, FromDIP(10));
    m_sizer_body->Add(m_status_bar->get_panel(), 0, wxALL | wxEXPAND, FromDIP(18));
    m_sizer_body->Add(0, 0, 0, wxTOP, FromDIP(20));

    SetSizer(m_sizer_body);
    Layout();
    Fit();

    CentreOnParent(wxBOTH);
    wxGetApp().UpdateDlgDarkUI(this);

    m_kSuccessCloseTimer = new wxTimer();
    m_kSuccessCloseTimer->SetOwner(this);
    Bind(wxEVT_TIMER, &PhrozenIpConnectDialog::OnTimer, this);

    Bind(EVT_PHROZEN_IP_CONNECT_SUCCESS, [this](auto& e) {
        m_status_bar->reset();
        EndModal(wxID_YES);
    });

    Bind(wxEVT_CLOSE_WINDOW, [this](auto& e) {
        on_cancel();
        m_kSuccessCloseTimer->Stop();
    });

    Bind(EVT_PHROZEN_UPDATE_CONNECT_MSG, &PhrozenIpConnectDialog::update_msg_event, this);

}

PhrozenIpConnectDialog::~PhrozenIpConnectDialog()
{
    if (m_thread) {
        m_thread->interrupt();
        m_thread->detach();
        delete m_thread;
        m_thread = nullptr;
    }
}

void PhrozenIpConnectDialog::set_ip_address(const std::string& ip)
{
    m_ip_address = ip;
}

int PhrozenIpConnectDialog::ShowModal()
{
    // Auto-start connection when dialog is shown
    start_connect();
    return DPIDialog::ShowModal();
}

void PhrozenIpConnectDialog::start_connect()
{
    if (m_ip_address.empty()) {
        post_update_msg(_L("IP address not set"), true);
        return;
    }

    m_thread = new boost::thread(boost::bind(&PhrozenIpConnectDialog::workerConnectThreadFunc, this, m_ip_address));
}

void PhrozenIpConnectDialog::on_cancel()
{
    if (m_thread) {
        m_thread->interrupt();
        m_thread->detach();
        delete m_thread;
        m_thread = nullptr;
    }

    EndModal(wxID_CANCEL);
}

void PhrozenIpConnectDialog::post_update_msg(wxString text, bool is_error)
{
    wxCommandEvent event(EVT_PHROZEN_UPDATE_CONNECT_MSG);
    event.SetEventObject(this);
    event.SetString(text);
    event.SetInt(is_error ? 1 : 0);
    wxPostEvent(this, event);
}

void PhrozenIpConnectDialog::update_msg_event(wxCommandEvent& evt)
{
    wxString text = evt.GetString();
    bool is_error = evt.GetInt() != 0;

    m_test_msg->SetLabelText(text);

    if (is_error) {
        m_test_msg->SetForegroundColour(wxColour(208, 27, 27));
    } else {
        m_test_msg->SetForegroundColour(wxColour(240, 94, 32));
    }

    Layout();
    Fit();
}

void PhrozenIpConnectDialog::workerConnectThreadFunc(std::string str_ip)
{
    if (str_ip.empty()) {
        return;
    }

    post_update_msg(_L("connecting..."), false);

    m_bSuccess = wxGetApp().InitPhrozenConnector(str_ip);

    CallAfter([this]() {
        
        closeCount = 1;

        if ( m_bSuccess )
        {
            wxGetApp().ProcessPhrozenConnector();
            post_update_msg(wxString::Format(_L("Connecting to printer success! The dialog will close later"), closeCount), false);
        }
        else
        {
            post_update_msg(wxString::Format(_L("Failed to connect to printer... The dialog will close later"), closeCount), true);
        }
        
        // Cross-platform support: use the same close flow for all platforms
        m_kSuccessCloseTimer->Start(1000);
    });
}

void PhrozenIpConnectDialog::OnTimer(wxTimerEvent& event)
{
    if (closeCount > 0) {
        closeCount--;
    }
    else {
        m_kSuccessCloseTimer->Stop();

        if ( m_bSuccess )
        {
            wxCommandEvent event(EVT_PHROZEN_IP_CONNECT_SUCCESS);
            wxPostEvent(this, event);
        }
        else
        {
            EndModal(wxID_CANCEL);
        }
    }
}

void PhrozenIpConnectDialog::on_dpi_changed(const wxRect& suggested_rect)
{
}
#pragma endregion


#pragma region PhrozenMachineObjectPanel
PhrozenMachineObjectPanel::PhrozenMachineObjectPanel(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size, long style, const wxString &name)
{
    wxPanel::Create(parent, id, pos, wxDefaultSize, style, name);

    SetSize(PHROZEN_SELECT_MACHINE_ITEM_SIZE);
    SetMinSize(PHROZEN_SELECT_MACHINE_ITEM_SIZE);
    SetMaxSize(PHROZEN_SELECT_MACHINE_ITEM_SIZE);

    Bind(wxEVT_PAINT, &PhrozenMachineObjectPanel::OnPaint, this);

    SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));

    m_unbind_img        = ScalableBitmap(this, "PhrozenImages/MachineObjectPanel_Unbind", 18);
    m_edit_name_img     = ScalableBitmap(this, "edit_button", 18);
    m_select_unbind_img = ScalableBitmap(this, "unbind_selected", 18);

    m_printer_status_offline = ScalableBitmap(this, "printer_status_offline", 12);
    m_printer_status_busy    = ScalableBitmap(this, "printer_status_busy", 12);
    m_printer_status_idle    = ScalableBitmap(this, "printer_status_idle", 12);
    m_printer_status_lock    = ScalableBitmap(this, "printer_status_lock", 16);
    m_printer_in_lan         = ScalableBitmap(this, "printer_in_lan", 16);

    this->Bind(wxEVT_ENTER_WINDOW, &PhrozenMachineObjectPanel::on_mouse_enter, this);
    this->Bind(wxEVT_LEAVE_WINDOW, &PhrozenMachineObjectPanel::on_mouse_leave, this);
    this->Bind(wxEVT_LEFT_UP, &PhrozenMachineObjectPanel::on_mouse_left_up, this);

#ifdef __APPLE__
    wxPlatformInfo platformInfo;
    auto major = platformInfo.GetOSMajorVersion();
    auto minor = platformInfo.GetOSMinorVersion();
    auto micro = platformInfo.GetOSMicroVersion();

    //macos 13.1.0
    if (major >= 13 && minor >= 1 && micro >= 0) {
        m_is_macos_special_version = true;
    }
#endif

}

PhrozenMachineObjectPanel::~PhrozenMachineObjectPanel() {}

void PhrozenMachineObjectPanel::set_printer_state(PhrozenPrinterState state)
{
    m_state = state;
    Refresh();
}

void PhrozenMachineObjectPanel::show_edit_printer_name(bool show)
{
    m_show_edit = show;
    Refresh();
}

void PhrozenMachineObjectPanel::show_printer_bind(bool show, PhrozenPrinterBindState state)
{
    m_show_bind   = show;
    m_bind_state  = state;
    Refresh();
}

void PhrozenMachineObjectPanel::OnPaint(wxPaintEvent &event)
{
    wxPaintDC dc(this);
    doRender(dc);
}

void PhrozenMachineObjectPanel::render(wxDC &dc)
{
#ifdef __WXMSW__
    wxSize     size = GetSize();
    wxMemoryDC memdc;
    wxBitmap   bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.Blit({0, 0}, size, &dc, {0, 0});

    {
        wxGCDC dc2(memdc);
        doRender(dc2);
    }

    memdc.SelectObject(wxNullBitmap);
    dc.DrawBitmap(bmp, 0, 0);
#else
    doRender(dc);
#endif
}

void PhrozenMachineObjectPanel::doRender(wxDC &dc)
{
    auto   left = 10;
    wxSize size = GetSize();
    dc.SetPen(*wxTRANSPARENT_PEN);

    auto dwbitmap = m_printer_status_offline;
    if (m_state == PhrozenPrinterState::IDLE) { dwbitmap = m_printer_status_idle; }
    if (m_state == PhrozenPrinterState::BUSY) { dwbitmap = m_printer_status_busy; }
    if (m_state == PhrozenPrinterState::OFFLINE) { dwbitmap = m_printer_status_offline; }
    if (m_state == PhrozenPrinterState::LOCK) { dwbitmap = m_printer_status_lock; }
    if (m_state == PhrozenPrinterState::IN_LAN) { dwbitmap = m_printer_in_lan; }

    // dc.DrawCircle(left, size.y / 2, 3);
    dc.DrawBitmap(dwbitmap.bmp(), wxPoint(left, (size.y - dwbitmap.GetBmpSize().y) / 2));

    left += dwbitmap.GetBmpSize().x + 8;
    dc.SetFont(Label::Body_13);
    dc.SetBackgroundMode(wxTRANSPARENT);
    dc.SetTextForeground(StateColor::darkModeColorFor(PHROZEN_SELECT_MACHINE_GREY900));
    wxString dev_name = m_strIp;

    auto        sizet        = dc.GetTextExtent(dev_name);
    auto        text_end     = 0;

    if (m_show_edit) {
        text_end = size.x - m_unbind_img.GetBmpSize().x - 30;
    }
    else {
        text_end = size.x - m_unbind_img.GetBmpSize().x;
    }

    wxString finally_name =  dev_name;
    if (sizet.x > (text_end - left)) {
        auto limit_width = text_end - left - dc.GetTextExtent("...").x - 15;
        for (auto i = 0; i < dev_name.length(); i++) {
            auto curr_width = dc.GetTextExtent(dev_name.substr(0, i));
            if (curr_width.x >= limit_width) {
                finally_name = dev_name.substr(0, i) + "...";
                break;
            }
        }
    }

    dc.DrawText(finally_name, wxPoint(left, (size.y - sizet.y) / 2));


    if (m_hover || m_is_macos_special_version) {

        if (m_hover && !m_is_macos_special_version) {
            dc.SetPen(PHROZEN_SELECT_MACHINE_BRAND);
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawRectangle(0, 0, size.x, size.y);
        }

        if (m_show_bind) {
            if (m_bind_state == PhrozenPrinterBindState::ALLOW_UNBIND) {
                left = size.x - m_unbind_img.GetBmpSize().x - 6;
                dc.DrawBitmap(m_unbind_img.bmp(), left, (size.y - m_unbind_img.GetBmpSize().y) / 2);
            }
        }

        if (m_show_edit) {
            left = size.x - m_unbind_img.GetBmpSize().x - 6 - m_edit_name_img.GetBmpSize().x - 6;
            dc.DrawBitmap(m_edit_name_img.bmp(), left, (size.y - m_edit_name_img.GetBmpSize().y) / 2);
        }
    }

}

void PhrozenMachineObjectPanel::update_machine_info(MachineObject *info, bool is_my_devices)
{
    //m_info = info;
    Refresh();
}

void PhrozenMachineObjectPanel::on_mouse_enter(wxMouseEvent &evt)
{
    m_hover = true;
    Refresh();
}

void PhrozenMachineObjectPanel::on_mouse_leave(wxMouseEvent &evt)
{
    m_hover = false;
    Refresh();
}

void PhrozenMachineObjectPanel::on_mouse_left_up(wxMouseEvent &evt)
{
    if ( m_state == PhrozenPrinterState::NOT_SELECT ) return;

    // show edit
    if (m_show_edit) {
        auto edit_left   = GetSize().x - m_unbind_img.GetBmpSize().x - 6 - m_edit_name_img.GetBmpSize().x - 6;
        auto edit_right  = edit_left + m_edit_name_img.GetBmpSize().x;
        auto edit_top    = (GetSize().y - m_edit_name_img.GetBmpSize().y) / 2;
        auto edit_bottom = (GetSize().y - m_edit_name_img.GetBmpSize().y) / 2 + m_edit_name_img.GetBmpSize().y;
        if ((evt.GetPosition().x >= edit_left && evt.GetPosition().x <= edit_right) && evt.GetPosition().y >= edit_top && evt.GetPosition().y <= edit_bottom) {
            wxCommandEvent event(EVT_EDIT_PRINT_NAME);
            event.SetEventObject(this);
            wxPostEvent(this, event);
            return;
        }
    }

    std::string strIp;
    Slic3r::GUI::wxGetApp().GetCurrentConnectedMachineIp( strIp );
    bool bIsCurrentConnected = strIp == m_strIp;

    if (m_show_bind) {
        auto left   = GetSize().x - m_unbind_img.GetBmpSize().x - 6;
        auto right  = left + m_unbind_img.GetBmpSize().x;
        auto top    = (GetSize().y - m_unbind_img.GetBmpSize().y) / 2;
        auto bottom = (GetSize().y - m_unbind_img.GetBmpSize().y) / 2 + m_unbind_img.GetBmpSize().y;
        bool bUnBindButtonAreaPressed = (evt.GetPosition().x >= left && evt.GetPosition().x <= right) && evt.GetPosition().y >= top && evt.GetPosition().y <= bottom;
        if ( bIsCurrentConnected && bUnBindButtonAreaPressed ) {
            wxCommandEvent event(EVT_PHROZEN_DISCONNECT_MACHINE, GetId());
            event.SetEventObject(this);
            GetEventHandler()->ProcessEvent(event);
        } else {
            wxCommandEvent event(EVT_PHROZEN_CONNECT_MACHINE_BY_IP);
            event.SetEventObject(this);
            event.SetString(m_strIp);
            wxPostEvent(this, event);
        }
        return;
    }




}

#pragma endregion

#pragma region PhrozenSelectMachinePopup
PhrozenSelectMachinePopup::PhrozenSelectMachinePopup(wxWindow *parent)
    : PopupWindow(parent, wxBORDER_NONE | wxPU_CONTAINS_CONTROLS), m_dismiss(false)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__


    SetSize(PHROZEN_SELECT_MACHINE_POPUP_SIZE);
    SetMinSize(PHROZEN_SELECT_MACHINE_POPUP_SIZE);
    SetMaxSize(PHROZEN_SELECT_MACHINE_POPUP_SIZE);

    Freeze();
    wxBoxSizer *m_sizer_main = new wxBoxSizer(wxVERTICAL);
    SetBackgroundColour(PHROZEN_SELECT_MACHINE_GREY400);



    m_scrolledWindow = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, PHROZEN_SELECT_MACHINE_LIST_SIZE, wxHSCROLL | wxVSCROLL);
    m_scrolledWindow->SetBackgroundColour(*wxWHITE);
    m_scrolledWindow->SetMinSize(PHROZEN_SELECT_MACHINE_LIST_SIZE);
    m_scrolledWindow->SetScrollRate(0, 5);
    auto m_sizxer_scrolledWindow = new wxBoxSizer(wxVERTICAL);
    m_scrolledWindow->SetSizer(m_sizxer_scrolledWindow);
    m_scrolledWindow->Layout();
    m_sizxer_scrolledWindow->Fit(m_scrolledWindow);

#if !BBL_RELEASE_TO_PUBLIC && defined(__WINDOWS__)
	m_sizer_search_bar = new wxBoxSizer(wxVERTICAL);
	m_search_bar = new wxSearchCtrl( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0 );
	m_search_bar->SetDescriptiveText(_L("Search"));
	m_search_bar->ShowSearchButton( true );
	m_search_bar->ShowCancelButton( false );
	m_sizer_search_bar->Add( m_search_bar, 1, wxALL| wxEXPAND, 1 );
	m_sizer_main->Add(m_sizer_search_bar, 0, wxALL | wxEXPAND, FromDIP(2));
	m_search_bar->Bind( wxEVT_COMMAND_TEXT_UPDATED, &PhrozenSelectMachinePopup::update_machine_list, this );
#endif
    auto own_title        = create_title_panel(_L("My Device"));
    m_sizer_my_devices    = new wxBoxSizer(wxVERTICAL);

    auto other_title      = create_title_panel(_L("History Device"));
    m_sizer_other_devices = new wxBoxSizer(wxVERTICAL);

    m_panel_direct_connection = new PhrozenIpKeyInButton(m_scrolledWindow, wxID_ANY, wxDefaultPosition, PHROZEN_SELECT_MACHINE_ITEM_SIZE);

    m_sizxer_scrolledWindow->Add(own_title, 0, wxEXPAND | wxLEFT, FromDIP(15));
    m_sizxer_scrolledWindow->Add(m_panel_direct_connection, 0, wxEXPAND, 0);
    m_sizxer_scrolledWindow->Add(m_sizer_my_devices, 0, wxEXPAND, 0);
    m_sizxer_scrolledWindow->Add(other_title, 0, wxEXPAND | wxLEFT, FromDIP(15));
    m_sizxer_scrolledWindow->Add(m_sizer_other_devices, 0, wxEXPAND, 0);

    m_sizer_main->Add(m_scrolledWindow, 0, wxALL | wxEXPAND, FromDIP(2));

    SetSizer(m_sizer_main);
    Layout();
    Thaw();

    #ifdef __APPLE__
    m_scrolledWindow->Bind(wxEVT_LEFT_UP, &PhrozenSelectMachinePopup::OnLeftUp, this);
    #endif // __APPLE__

    m_refresh_timer = new wxTimer();
    m_refresh_timer->SetOwner(this);
    Bind(EVT_PHROZEN_UPDATE_USER_MACHINE_LIST, &PhrozenSelectMachinePopup::update_machine_list, this);
    Bind(wxEVT_TIMER, &PhrozenSelectMachinePopup::on_timer, this);
    Bind(EVT_DISSMISS_MACHINE_LIST, &PhrozenSelectMachinePopup::on_dissmiss_win, this);
}

PhrozenSelectMachinePopup::~PhrozenSelectMachinePopup() 
{ 
    delete m_refresh_timer;

    for ( auto pObj : m_history_lan_machine_ip_panels )
    {
        release_panel_data( pObj );
    }
    m_history_lan_machine_ip_panels.clear();

    for ( auto kIter : m_lan_machine_ip_panels )
    {
        release_panel_data( kIter.second );
    }
    m_lan_machine_ip_panels.clear();
}

void PhrozenSelectMachinePopup::Popup(wxWindow *WXUNUSED(focus))
{
    BOOST_LOG_TRIVIAL(trace) << "get_print_info: start";
    PhrozenDeviceSearcher::StartSearch();

    if (m_refresh_timer) {
        m_refresh_timer->Stop();
        m_refresh_timer->Start(MACHINE_LIST_REFRESH_INTERVAL);
    }

    CallAfter([this]() {
        wxCommandEvent event(EVT_PHROZEN_UPDATE_USER_MACHINE_LIST);
        event.SetEventObject(this);
        wxPostEvent(this, event);
    });

    wxPostEvent(this, wxTimerEvent());
    PopupWindow::Popup();
}

void PhrozenSelectMachinePopup::OnDismiss()
{
    BOOST_LOG_TRIVIAL(trace) << "get_print_info: dismiss";
    m_dismiss = true;
    PhrozenDeviceSearcher::StopSearch();

    if (m_refresh_timer) {
        m_refresh_timer->Stop();
    }

    clear_all_searched_ip_panel();
}

bool PhrozenSelectMachinePopup::ProcessLeftDown(wxMouseEvent &event) {
    return PopupWindow::ProcessLeftDown(event);
}

bool PhrozenSelectMachinePopup::Show(bool show) {
    if (show) {
         for (auto& kIter : m_lan_machine_ip_panels) {
            kIter.second->update_machine_info(nullptr);
            kIter.second->Hide();
        }
    }
    return PopupWindow::Show(show);
}

wxWindow *PhrozenSelectMachinePopup::create_title_panel(wxString text)
{
    auto m_panel_title_own = new wxWindow(m_scrolledWindow, wxID_ANY, wxDefaultPosition, PHROZEN_SELECT_MACHINE_ITEM_SIZE, wxTAB_TRAVERSAL);
    m_panel_title_own->SetBackgroundColour(*wxWHITE);

    wxBoxSizer *m_sizer_title_own = new wxBoxSizer(wxHORIZONTAL);

    auto m_title_own = new wxStaticText(m_panel_title_own, wxID_ANY, text, wxDefaultPosition, wxDefaultSize, 0);
    m_title_own->Wrap(-1);
    m_sizer_title_own->Add(m_title_own, 0, wxALIGN_CENTER, 0);

    wxBoxSizer *m_sizer_line_own = new wxBoxSizer(wxHORIZONTAL);

    auto m_panel_line_own = new wxPanel(m_panel_title_own, wxID_ANY, wxDefaultPosition, wxSize(PHROZEN_SELECT_MACHINE_ITEM_SIZE.x, FromDIP(1)), wxTAB_TRAVERSAL);
    m_panel_line_own->SetBackgroundColour(PHROZEN_SELECT_MACHINE_GREY400);

    m_sizer_line_own->Add(m_panel_line_own, 0, wxALIGN_CENTER, 0);
    m_sizer_title_own->Add(0, 0, 0, wxLEFT, FromDIP(10));
    m_sizer_title_own->Add(m_sizer_line_own, 1, wxEXPAND | wxRIGHT, FromDIP(10));

    m_panel_title_own->SetSizer(m_sizer_title_own);
    m_panel_title_own->Layout();
    return m_panel_title_own;
}

void PhrozenSelectMachinePopup::on_timer(wxTimerEvent &event)
{
    BOOST_LOG_TRIVIAL(trace) << "SelectMachinePopup on_timer";
    wxGetApp().reset_to_active();
    wxCommandEvent user_event(EVT_PHROZEN_UPDATE_USER_MACHINE_LIST);
    user_event.SetEventObject(this);
    wxPostEvent(this, user_event);
}

void PhrozenSelectMachinePopup::update_history_devices()
{
    if ( m_history_lan_machine_ip_panels.empty() ) return;

    this->Freeze();
    m_scrolledWindow->Freeze();
    m_scrolledWindow->Layout();
    m_scrolledWindow->Thaw();
    Layout();
    Fit();
    this->Thaw();

}

void PhrozenSelectMachinePopup::release_panel_data( PhrozenMachineObjectPanel* pIpPanel )
{
    if ( !pIpPanel ) return;

     m_sizer_my_devices->Detach( pIpPanel );
     delete pIpPanel;
}

void PhrozenSelectMachinePopup::clear_all_searched_ip_panel()
{
    for ( auto kIter : m_lan_machine_ip_panels )
    {
        release_panel_data( kIter.second );
    }
    m_lan_machine_ip_panels.clear();
}

void PhrozenSelectMachinePopup::create_searching_pad()
{
    if ( m_pSearchingPad )
    {
        remove_searching_pad();
    }

    m_pSearchingPad = new PhrozenMachineObjectPanel( m_scrolledWindow, wxID_ANY );
    m_pSearchingPad->set_maching_ip( strSearching.ToStdString() );
    m_pSearchingPad->set_printer_state( PhrozenPrinterState::NOT_SELECT );
    m_sizer_my_devices->Add( m_pSearchingPad, 0, wxEXPAND, 0 );
}

void PhrozenSelectMachinePopup::remove_searching_pad()
{
    if ( !m_pSearchingPad ) return;
    release_panel_data( m_pSearchingPad );
    m_pSearchingPad = nullptr;
}

PhrozenMachineObjectPanel* PhrozenSelectMachinePopup::create_ip_object_panel( PhrozenPrinterState eState, std::string strIp, bool bIsConnected )
{
    auto  op = new PhrozenMachineObjectPanel( m_scrolledWindow, wxID_ANY );
    op->set_maching_ip( strIp );
    op->set_printer_state( eState );
    if ( bIsConnected ) 
    { 
        op->show_printer_bind( true, PhrozenPrinterBindState::ALLOW_UNBIND ); 
    }
    else 
    { 
        op->show_printer_bind( true, PhrozenPrinterBindState::ALLOW_BIND ); 
    }
    op->Bind(EVT_PHROZEN_CONNECT_MACHINE_BY_IP, &PhrozenSelectMachinePopup::on_ip_panel_clicked, this );
    return op;
}

void PhrozenSelectMachinePopup::on_ip_panel_clicked( wxCommandEvent& event )
{
    auto strIp = event.GetString().ToStdString();
    std::string strConnectedIp;
    Slic3r::GUI::wxGetApp().GetCurrentConnectedMachineIp( strConnectedIp );
    if ( strIp.empty() || strConnectedIp == strIp )
    {
        return;
    }

    for ( auto pObj : m_history_lan_machine_ip_panels )
    {
        if ( pObj->get_machine_ip() == strIp )
        {
            event.Skip(); // pass to upper layer, to make machine connect
            return;
        }
    }

    if ( m_history_lan_machine_ip_panels.size() == m_nMaxHistoryIp )
    {
        auto kLastIter = m_history_lan_machine_ip_panels.begin() + ( m_nMaxHistoryIp - 1 );
        release_panel_data( *kLastIter );
        m_history_lan_machine_ip_panels.erase( kLastIter );
    }

    auto  op = new PhrozenMachineObjectPanel( m_scrolledWindow, wxID_ANY );
    op->set_maching_ip( strIp );
    op->set_printer_state( PhrozenPrinterState::IDLE );
    op->show_printer_bind( true, PhrozenPrinterBindState::ALLOW_BIND );
    m_history_lan_machine_ip_panels.insert( m_history_lan_machine_ip_panels.begin(), op );
    m_sizer_other_devices->Add(op, 0, wxEXPAND, 0);

    event.Skip(); // pass to upper layer, to make machine connect
}

void PhrozenSelectMachinePopup::update_lan_devices()
{
    std::map< std::string, std::string > kSearchResult;
    if ( PhrozenDeviceSearcher::IsDataReady() )
    {
        kSearchResult = PhrozenDeviceSearcher::GetList();
    }

    this->Freeze();
    m_scrolledWindow->Freeze();

    bool bIpPadChanged = false;
    if ( kSearchResult.empty() && m_lan_machine_ip_panels.empty() )
    {
        if ( !m_pSearchingPad ) { create_searching_pad(); bIpPadChanged = true; }
        
    }
    else
    {
        remove_searching_pad();
        BOOST_LOG_TRIVIAL(trace) << "SelectMachinePopup update_machine_ip_list start";

        // always add new find.
        std::string strConnectedIp;
        Slic3r::GUI::wxGetApp().GetCurrentConnectedMachineIp( strConnectedIp );

        for ( auto kIter : kSearchResult )
        {
            auto kFounded = m_lan_machine_ip_panels.find( kIter.first );
            if ( kFounded != m_lan_machine_ip_panels.end() ) continue;

            auto strIp = kIter.first;
            auto op = create_ip_object_panel( PhrozenPrinterState::IN_LAN, strIp, strIp == strConnectedIp );
            m_lan_machine_ip_panels.insert( { strIp, op } );
            m_sizer_my_devices->Add(op, 0, wxEXPAND, 0);
            bIpPadChanged = true;
        }
    }

    if ( bIpPadChanged ) m_scrolledWindow->Fit();

    m_scrolledWindow->Layout();
    m_scrolledWindow->Thaw();
	Layout();
	Fit();
	this->Thaw();
}

bool PhrozenSelectMachinePopup::search_for_printer(MachineObject* obj)
{
	std::string search_text = std::string((m_search_bar->GetValue()).mb_str());
	if (search_text.empty()) {
		return true;
	}
	auto name = obj->dev_name;
	auto ip = obj->dev_ip;
	auto name_it = name.find(search_text);
	auto ip_it = ip.find(search_text);
	if ((name_it != std::string::npos)||(ip_it != std::string::npos)) {
		return true;
    }

    return false;
}

void PhrozenSelectMachinePopup::on_dissmiss_win(wxCommandEvent &event)
{
    Dismiss();
}

void PhrozenSelectMachinePopup::update_machine_list(wxCommandEvent &event)
{
    update_lan_devices();
    update_history_devices();
    BOOST_LOG_TRIVIAL(trace) << "SelectMachinePopup update_machine_list end";
}

void PhrozenSelectMachinePopup::OnLeftUp(wxMouseEvent &event)
{
    auto mouse_pos = ClientToScreen(event.GetPosition());
    auto wxscroll_win_pos = m_scrolledWindow->ClientToScreen(wxPoint(0, 0));

    if (mouse_pos.x > wxscroll_win_pos.x && mouse_pos.y > wxscroll_win_pos.y && mouse_pos.x < (wxscroll_win_pos.x + m_scrolledWindow->GetSize().x) &&
        mouse_pos.y < (wxscroll_win_pos.y + m_scrolledWindow->GetSize().y)) {

        for ( auto& kIter : m_lan_machine_ip_panels) {
            PhrozenMachineObjectPanel* p = kIter.second;
            auto p_rect = p->ClientToScreen(wxPoint(0, 0));
            if (mouse_pos.x > p_rect.x && mouse_pos.y > p_rect.y && mouse_pos.x < (p_rect.x + p->GetSize().x) && mouse_pos.y < (p_rect.y + p->GetSize().y)) {
                wxMouseEvent event(wxEVT_LEFT_UP);
                auto         tag_pos = p->ScreenToClient(mouse_pos);
                event.SetPosition(tag_pos);
                event.SetEventObject(p);
                wxPostEvent(p, event);
            }
        }

        //bind with access code
        auto dc_rect = m_panel_direct_connection->ClientToScreen(wxPoint(0, 0));
        if (mouse_pos.x > dc_rect.x && mouse_pos.y > dc_rect.y && mouse_pos.x < (dc_rect.x + m_panel_direct_connection->GetSize().x) && mouse_pos.y < (dc_rect.y + m_panel_direct_connection->GetSize().y)) {
            PhrozenInputIpAddressDialog dlgo;
            dlgo.ShowModal();
        }

        //hyper link
        // Commented out: The new connection close dialog flow causes crashes on macOS platform
        //auto h_rect = m_hyperlink->ClientToScreen(wxPoint(0, 0));
        //if (mouse_pos.x > h_rect.x && mouse_pos.y > h_rect.y && mouse_pos.x < (h_rect.x + m_hyperlink->GetSize().x) && mouse_pos.y < (h_rect.y + m_hyperlink->GetSize().y)) {
        //  wxLaunchDefaultBrowser(wxT("https://wiki.bambulab.com/en/software/bambu-studio/failed-to-connect-printer"));
        //}
    }
}

#pragma endregion


#pragma region PhrozenEditDevNameDialog
PhrozenEditDevNameDialog::PhrozenEditDevNameDialog(Plater *plater /*= nullptr*/)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, _L("Modifying the device name"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    SetBackgroundColour(*wxWHITE);
    wxBoxSizer *m_sizer_main = new wxBoxSizer(wxVERTICAL);
    auto        m_line_top   = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));
    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(38));
    m_textCtr = new ::TextInput(this, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(260), FromDIP(40)), wxTE_PROCESS_ENTER);
    m_textCtr->GetTextCtrl()->SetSize(wxSize(-1, FromDIP(22)));
    m_textCtr->SetMinSize(wxSize(FromDIP(260), FromDIP(40)));
    m_sizer_main->Add(m_textCtr, 0, wxALIGN_CENTER_HORIZONTAL | wxLEFT | wxRIGHT, FromDIP(40));

    m_static_valid = new wxStaticText(this, wxID_ANY, wxT(""), wxDefaultPosition, wxDefaultSize, 0);
    m_static_valid->Wrap(-1);
    m_static_valid->SetFont(::Label::Body_13);
    m_static_valid->SetForegroundColour(wxColour(255, 111, 0));
    m_sizer_main->Add(m_static_valid, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP | wxLEFT | wxRIGHT, FromDIP(10));


    m_button_confirm = new Button(this, _L("Confirm"));
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed), std::pair<wxColour, int>(wxColour(255, 124, 63), StateColor::Normal));
    m_button_confirm->SetBackgroundColor(btn_bg_green);
    m_button_confirm->SetBorderColor(wxColour(255, 124, 63));
    m_button_confirm->SetTextColor(wxColour(255, 255, 255));
    m_button_confirm->SetSize(wxSize(FromDIP(72), FromDIP(24)));
    m_button_confirm->SetMinSize(wxSize(FromDIP(72), FromDIP(24)));
    m_button_confirm->SetCornerRadius(FromDIP(12));
    m_button_confirm->Bind(wxEVT_BUTTON, &PhrozenEditDevNameDialog::on_edit_name, this);

    m_sizer_main->Add(m_button_confirm, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(10));
    m_sizer_main->Add(0, 0, 0, wxBOTTOM, FromDIP(38));

    SetSizer(m_sizer_main);
    Layout();
    Fit();
    Centre(wxBOTH);
    wxGetApp().UpdateDlgDarkUI(this);
}

PhrozenEditDevNameDialog::~PhrozenEditDevNameDialog() {}

void PhrozenEditDevNameDialog::set_machine_obj(MachineObject *obj)
{
    m_info = obj;
    if (m_info)
        m_textCtr->GetTextCtrl()->SetValue(from_u8(m_info->dev_name));
}

void PhrozenEditDevNameDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    m_button_confirm->SetSize(wxSize(FromDIP(72), FromDIP(24)));
    m_button_confirm->SetMinSize(wxSize(FromDIP(72), FromDIP(24)));
}

void PhrozenEditDevNameDialog::on_edit_name(wxCommandEvent &e)
{
    m_static_valid->SetLabel(wxEmptyString);
    auto     m_valid_type = Valid;
    wxString info_line;
    auto     new_dev_name = m_textCtr->GetTextCtrl()->GetValue();

    const char *      unusable_symbols = "<>[]:/\\|?*\"";
    const std::string unusable_suffix  = PresetCollection::get_suffix_modified();

    for (size_t i = 0; i < std::strlen(unusable_symbols); i++) {
        if (new_dev_name.find_first_of(unusable_symbols[i]) != std::string::npos) {
            info_line    = _L("Name is invalid;") + _L("illegal characters:") + " " + unusable_symbols;
            m_valid_type = NoValid;
            break;
        }
    }

    if (m_valid_type == Valid && new_dev_name.find(unusable_suffix) != std::string::npos) {
        info_line    = _L("Name is invalid;") + _L("illegal suffix:") + "\n\t" + from_u8(PresetCollection::get_suffix_modified());
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid && new_dev_name.empty()) {
        info_line    = _L("The name is not allowed to be empty.");
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid && new_dev_name.find_first_of(' ') == 0) {
        info_line    = _L("The name is not allowed to start with space character.");
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid && new_dev_name.find_last_of(' ') == new_dev_name.length() - 1) {
        info_line    = _L("The name is not allowed to end with space character.");
        m_valid_type = NoValid;
    }

    if (m_valid_type == NoValid) {
        m_static_valid->SetLabel(info_line);
        Layout();
    }

    if (m_valid_type == Valid) {
        m_static_valid->SetLabel(wxEmptyString);
        DeviceManager *dev      = Slic3r::GUI::wxGetApp().getDeviceManager();
        if (dev) {
            auto           utf8_str = new_dev_name.ToUTF8();
            auto           name     = std::string(utf8_str.data(), utf8_str.length());
            if (m_info)
                dev->modify_device_name(m_info->dev_id, name);
        }
        DPIDialog::EndModal(wxID_CLOSE);
    }
}
#pragma endregion

#pragma region PhrozenIpKeyInButton
PhrozenIpKeyInButton::PhrozenIpKeyInButton(wxWindow* parent, wxWindowID winid /*= wxID_ANY*/, const wxPoint& pos /*= wxDefaultPosition*/, const wxSize& size /*= wxDefaultSize*/)
 {
     wxPanel::Create(parent, winid, pos);
     Bind(wxEVT_PAINT, &PhrozenIpKeyInButton::OnPaint, this);
     SetSize(PHROZEN_SELECT_MACHINE_ITEM_SIZE);
     SetMaxSize(PHROZEN_SELECT_MACHINE_ITEM_SIZE);
     SetMinSize(PHROZEN_SELECT_MACHINE_ITEM_SIZE);

     m_bitmap = ScalableBitmap(this, "bind_device_ping_code",10);
     
     this->Bind(wxEVT_ENTER_WINDOW, &PhrozenIpKeyInButton::on_mouse_enter, this);
     this->Bind(wxEVT_LEAVE_WINDOW, &PhrozenIpKeyInButton::on_mouse_leave, this);
     this->Bind(wxEVT_LEFT_UP, &PhrozenIpKeyInButton::on_mouse_left_up, this);
 }

 void PhrozenIpKeyInButton::OnPaint(wxPaintEvent& event)
 {
     wxPaintDC dc(this);
     render(dc);
 }

 void PhrozenIpKeyInButton::render(wxDC& dc)
 {
#ifdef __WXMSW__
     wxSize     size = GetSize();
     wxMemoryDC memdc;
     wxBitmap   bmp(size.x, size.y);
     memdc.SelectObject(bmp);
     memdc.Blit({ 0, 0 }, size, &dc, { 0, 0 });

     {
         wxGCDC dc2(memdc);
         doRender(dc2);
     }

     memdc.SelectObject(wxNullBitmap);
     dc.DrawBitmap(bmp, 0, 0);
#else
     doRender(dc);
#endif
 }


 void PhrozenIpKeyInButton::doRender(wxDC& dc)
 {
     auto size = GetSize();
     dc.DrawBitmap(m_bitmap.bmp(), wxPoint(FromDIP(12), (size.y - m_bitmap.GetBmpSize().y) / 2));
     dc.SetFont(::Label::Head_13);
     dc.SetTextForeground(StateColor::darkModeColorFor(wxColour("#262E30"))); // ORCA fix text not visible on dark theme
     wxString txt = _L("Bind with IP");

     auto txt_size = dc.GetTextExtent(txt);
     dc.DrawText(txt, wxPoint(FromDIP(28), (size.y - txt_size.y) / 2));

     if (m_hover) {
         dc.SetPen(PHROZEN_SELECT_MACHINE_BRAND);
         dc.SetBrush(*wxTRANSPARENT_BRUSH);
         dc.DrawRectangle(0, 0, size.x, size.y);
     }
 }

 void PhrozenIpKeyInButton::on_mouse_enter(wxMouseEvent& evt)
 {
     m_hover = true;
     Refresh();
 }

 void PhrozenIpKeyInButton::on_mouse_leave(wxMouseEvent& evt)
 {
     m_hover = false;
     Refresh();
 }

 void PhrozenIpKeyInButton::on_mouse_left_up(wxMouseEvent& evt)
 {
     PhrozenInputIpAddressDialog dlgo;
     dlgo.ShowModal();
 }
 #pragma endregion

 }} // namespace Slic3r::GUI
