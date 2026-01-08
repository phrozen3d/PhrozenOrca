#include "libslic3r/Utils.hpp"
#include "libslic3r/Model.hpp"
#include "slic3r/GUI/I18N.hpp"

#include <wx/app.h>
#include <wx/button.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>

#include <wx/bmpcbox.h>
#include <wx/bmpbuttn.h>
#include <wx/treectrl.h>
#include <wx/imaglist.h>
#include <wx/settings.h>
#include <wx/filedlg.h>
#include <wx/wupdlock.h>
#include <wx/dataview.h>
#include <wx/tglbtn.h>

#include "../wxExtensions.hpp"
#include "../GUI_App.hpp"
#include "../GUI_ObjectList.hpp"
#include "../Plater.hpp"
#include "../MainFrame.hpp"
#include "../Widgets/Label.hpp"
#include "../format.hpp"
#include "../MediaPlayCtrl.h"
#include "../MediaFilePanel.h"
#include "../BindDialog.hpp"
#include "PhrozenMonitorController.hpp"
#include "PhrozenDeviceManager.hpp"
#include "PhrozenSideTools.hpp"
#include "PhrozenSelectMachinePopup.hpp"

namespace Slic3r {
namespace GUI {

#define REFRESH_INTERVAL       1000

#pragma region PhrozenMonitorPanel
 PhrozenMonitorPanel::PhrozenMonitorPanel(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style),
      m_select_machine(new PhrozenSelectMachinePopup(this))
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__

    init_bitmap();

    init_tabpanel();

    m_main_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_main_sizer->Add(m_tabpanel, 1, wxEXPAND | wxLEFT, 0);
    SetSizerAndFit(m_main_sizer);
    m_status_info_panel->show_ams_group(true);

    init_timer();

    m_side_tools->get_panel()->Connect(wxEVT_LEFT_DOWN, wxMouseEventHandler(PhrozenMonitorPanel::on_printer_clicked), NULL, this);

    
    Bind(wxEVT_TIMER, &PhrozenMonitorPanel::on_timer, this);
    Bind(wxEVT_SIZE, &PhrozenMonitorPanel::on_size, this);
    m_select_machine->Bind(EVT_PHROZEN_CONNECT_MACHINE_BY_IP, &PhrozenMonitorPanel::OnConnectMachineByIp, this );
    m_select_machine->Bind(EVT_PHROZEN_DISCONNECT_MACHINE, &PhrozenMonitorPanel::OnDisconnectMachine, this );
    
    //Bind(wxEVT_COMMAND_CHOICE_SELECTED, &MonitorPanel::on_select_printer, this);

 }

PhrozenMonitorPanel::~PhrozenMonitorPanel()
{
    if (m_refresh_timer)
        m_refresh_timer->Stop();
    delete m_refresh_timer;
}

 void PhrozenMonitorPanel::init_bitmap()
{
    m_signal_strong_img = create_scaled_bitmap("monitor_signal_strong", nullptr, 24);
    m_signal_middle_img = create_scaled_bitmap("monitor_signal_middle", nullptr, 24);
    m_signal_weak_img = create_scaled_bitmap("monitor_signal_weak", nullptr, 24);
    m_signal_no_img   = create_scaled_bitmap("monitor_signal_no", nullptr, 24);
    m_printer_img = create_scaled_bitmap("monitor_printer", nullptr, 26);
    m_arrow_img = create_scaled_bitmap("monitor_arrow",nullptr, 14);
}

 void PhrozenMonitorPanel::init_timer()
{
    m_refresh_timer = new wxTimer();
    m_refresh_timer->SetOwner(this);
    m_refresh_timer->Start(REFRESH_INTERVAL);
    wxPostEvent(this, wxTimerEvent());

    Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;
    MachineObject *obj_ = dev->get_selected_machine();
    if (obj_)
        GUI::wxGetApp().sidebar().load_ams_list(obj_->dev_id, obj_);
}

  void PhrozenMonitorPanel::init_tabpanel()
{
    m_side_tools = new PhrozenSideTools(this, wxID_ANY);
    wxBoxSizer* sizer_side_tools = new wxBoxSizer(wxVERTICAL);
    sizer_side_tools->Add(m_side_tools, 1, wxEXPAND, 0);
    m_tabpanel = new Tabbook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, sizer_side_tools, wxNB_LEFT | wxTAB_TRAVERSAL | wxNB_NOPAGETHEME);
    m_side_tools->set_table_panel(m_tabpanel);
    m_tabpanel->SetBackgroundColour(wxColour("#FEFFFF"));
    m_tabpanel->Bind(wxEVT_BOOKCTRL_PAGE_CHANGED, [this](wxBookCtrlEvent& e) {
        auto page = m_tabpanel->GetCurrentPage();
        page->SetFocus();
    }, m_tabpanel->GetId());

    m_status_info_panel = new PhrozenStatusPanel(m_tabpanel);
    m_tabpanel->AddPage(m_status_info_panel, _L("Status"), "", true);

    //m_spPrintHistoryPanel = std::make_shared< wxPanel >( m_tabpanel );
    //m_tabpanel->AddPage(m_spPrintHistoryPanel.get(), _L("Print History"), "", false);

    m_initialized = true;
    show_status((int) MonitorStatus::MONITOR_NO_PRINTER);
}

void PhrozenMonitorPanel::set_default()
{
    /* reset status panel*/
    m_status_info_panel->set_default();

    wxGetApp().sidebar().load_ams_list({}, {});
}

void PhrozenMonitorPanel::on_sys_color_changed()
{
    m_status_info_panel->on_sys_color_changed();
}

void PhrozenMonitorPanel::msw_rescale()
{
    init_bitmap();

    /* side_tool rescale */
    m_side_tools->msw_rescale();
    m_tabpanel->Rescale();
    /* Panel rescale */
    m_status_info_panel->msw_rescale();
    //m_spPrintHistoryPanel->msw_rescale();

    Layout();
    Refresh();
}

void PhrozenMonitorPanel::on_update_all(wxMouseEvent& event)
{
    if (update_flag) {
        update_all();
        Layout();
        Refresh();
    }
}

 void PhrozenMonitorPanel::on_timer(wxTimerEvent& event)
{
     if (update_flag) {
         update_all();
         Layout();
         Refresh();
     }
}

void PhrozenMonitorPanel::on_printer_clicked(wxMouseEvent &event)
{
    auto mouse_pos = ClientToScreen(event.GetPosition());
    wxPoint rect = m_side_tools->ClientToScreen(wxPoint(0, 0));

    if (!m_side_tools->is_in_interval()) {
        wxPoint pos = m_side_tools->ClientToScreen(wxPoint(0, 0));
        pos.y += m_side_tools->GetRect().height;
        //pos.x = pos.x < 0? 0:pos.x;
        m_select_machine->Move(pos);

#ifdef __linux__
        m_select_machine->SetSize(wxSize(m_side_tools->GetSize().x, -1));
        m_select_machine->SetMaxSize(wxSize(m_side_tools->GetSize().x, -1));
        m_select_machine->SetMinSize(wxSize(m_side_tools->GetSize().x, -1));
#endif

        m_select_machine->Popup();
    }
}

void PhrozenMonitorPanel::on_size(wxSizeEvent& event)
{
    Layout();
    Refresh();
}

void PhrozenMonitorPanel::update_all()
{
    if ( !m_status_info_panel->IsShown() ) return;

    if (m_status_info_panel->IsShown() && MonitorControl::IsStartReceiving() ) 
    {
        show_status(MONITOR_NORMAL);
        auto pManager = wxGetApp().GetPhrozenDeviceManager();
        if ( pManager )
        {
            auto pMachineObj = pManager->GetConnectingMachine();
            if ( pMachineObj )
            {
                // new flow for recieve webcam
                m_side_tools->set_current_printer_name( pMachineObj->GetMachineIp() );
                m_status_info_panel->SetPhrozenMachineObject( pMachineObj );

                // origin flow for other panel result
                auto pPhrozenMachineObj = wxGetApp().GetPhrozenMachineObject();
                m_status_info_panel->SetMachineObject( pPhrozenMachineObj );
                m_status_info_panel->update( pPhrozenMachineObj );
            }
            else
            {
                //TODO reset and disable ui
            }
        }

    }
    else
    {
        auto pPhrozenMachineObj = wxGetApp().GetPhrozenMachineObject();
        if ( pPhrozenMachineObj && m_last_status == MONITOR_NORMAL )
        {   
            //call disconnect from ui side, to prevent machine object killed when ui updating
            OnDisconnectMachine( wxCommandEvent() );
        }
        m_side_tools->set_none_printer_mode();
        show_status(MONITOR_UNKNOWN);
    }
    return;

#if 0
    obj = wxGetApp().GetPhrozenMachineObject();

    // check valid machine
    if (obj && !obj->IsPhrozenConnected() ) {
        obj->dev_ip = "";
        show_status((int)MONITOR_NO_PRINTER);
        return;
    }

    m_status_info_panel->SetMachineObject( obj );
    //m_status_info_panel->m_media_play_ctrl->SetMachineObject(obj);
    //m_side_tools->update_status(obj);

    if (!obj) {
        show_status((int)MONITOR_NO_PRINTER);
        //m_tabpanel->GetBtnsListCtrl()->showNewTag(3, false);
        return;
    }

    if (obj->IsPhrozenConnected() && obj->IsPhrozenStartReceiving() ) {
        show_status(MONITOR_CONNECTING);
        return;
    } else if (!obj->is_connected()) {
        int server_status = 0;
        show_status((int) MONITOR_DISCONNECTED + server_status);
        return;
    }

    show_status(MONITOR_NORMAL);

    if (m_status_info_panel->IsShown()) {
        m_status_info_panel->update(obj);
    }

    //update_hms_tag();
#endif
}

bool PhrozenMonitorPanel::Show(bool show)
{
#ifdef __APPLE__
    wxGetApp().mainframe->SetMinSize(wxGetApp().plater()->GetMinSize());
#endif

    if (show) {
        start_update();

        m_refresh_timer->Stop();
        m_refresh_timer->SetOwner(this);
        m_refresh_timer->Start(REFRESH_INTERVAL);
        wxPostEvent(this, wxTimerEvent());
    } else {
        stop_update();
        m_refresh_timer->Stop();
    }
    return wxPanel::Show(show);
}

void PhrozenMonitorPanel::show_status(int status)
{
    if (!m_initialized) return;
    if ( m_last_status == status ) return;
    m_last_status = status;
Freeze();
    // update panels
    m_status_info_panel->show_status(status);
    if ( m_side_tools ) { m_side_tools->show_status(status); }

    if ((status & (int)MonitorStatus::MONITOR_NO_PRINTER) != 0) 
    {
        set_default();
        m_tabpanel->Layout();
    } else if (((status & (int)MonitorStatus::MONITOR_NORMAL) != 0)
        || ((status & (int)MonitorStatus::MONITOR_DISCONNECTED) != 0)
        || ((status & (int) MonitorStatus::MONITOR_DISCONNECTED_SERVER) != 0)
        || ((status & (int)MonitorStatus::MONITOR_CONNECTING) != 0) )
    {

        if (((status & (int) MonitorStatus::MONITOR_DISCONNECTED) != 0)
            || ((status & (int) MonitorStatus::MONITOR_DISCONNECTED_SERVER) != 0)
            || ((status & (int)MonitorStatus::MONITOR_CONNECTING) != 0))
        {
            set_default();
        }
        m_tabpanel->Layout();
    }
    Layout();
Thaw();
}

void PhrozenMonitorPanel::OnConnectMachineByIp( wxCommandEvent& event )
{
    std::string strConnectedIp;
    Slic3r::GUI::wxGetApp().GetCurrentConnectedMachineIp( strConnectedIp );

    if ( event.GetString().IsEmpty() ) return;

    if ( strConnectedIp == event.GetString() ) { return; }
    else
    {
#ifdef __WINDOWS__
        OnDisconnectMachine( wxCommandEvent() );
#else
        wxCommandEvent disconnectEvent;
        OnDisconnectMachine( disconnectEvent );
#endif
    }

    
    PhrozenIpConnectDialog kConnect( this );
    kConnect.set_ip_address( event.GetString().ToStdString() );
    kConnect.ShowModal();

}

void PhrozenMonitorPanel::OnDisconnectMachine( wxCommandEvent& event )
{
    stop_update();

    //Debug
    m_status_info_panel->SetMachineObject( nullptr );
    m_status_info_panel->SetPhrozenMachineObject( nullptr );
    m_side_tools->set_none_printer_mode();

    wxGetApp().ProcessPhrozenDisconnect();

    start_update();
}

#pragma endregion


} // GUI
} // Slic3r
