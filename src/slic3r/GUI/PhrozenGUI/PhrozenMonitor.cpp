#include "libslic3r/Utils.hpp"
#include "libslic3r/Model.hpp"

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

namespace Slic3r {
namespace GUI {

#define REFRESH_INTERVAL       1000

#pragma region PhrozenMonitorPanel
 PhrozenMonitorPanel::PhrozenMonitorPanel(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style),
      m_select_machine(SelectMachinePopup(this))
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

    //m_side_tools->get_panel()->Connect(wxEVT_LEFT_DOWN, wxMouseEventHandler(MonitorPanel::on_printer_clicked), NULL, this);

    
    Bind(wxEVT_TIMER, &PhrozenMonitorPanel::on_timer, this);
    Bind(wxEVT_SIZE, &PhrozenMonitorPanel::on_size, this);
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
    m_side_tools = new SideTools(this, wxID_ANY);
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

    m_spPrintHistoryPanel = std::make_shared< wxPanel >( m_tabpanel );
    m_tabpanel->AddPage(m_spPrintHistoryPanel.get(), _L("Print History"), "", false);

    m_initialized = true;
    show_status((int) MonitorStatus::MONITOR_NO_PRINTER);
}

void PhrozenMonitorPanel::set_default()
{
    obj = nullptr;
    last_conn_type = "undefined";

    /* reset status panel*/
    m_status_info_panel->set_default();

    /* reset side tool*/
    //m_bitmap_wifi_signal->SetBitmap(wxNullBitmap);

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

void PhrozenMonitorPanel::on_size(wxSizeEvent& event)
{
    Layout();
    Refresh();
}

void PhrozenMonitorPanel::update_all()
{
    //Debug
    show_status(MONITOR_NORMAL);
    return;

    NetworkAgent* m_agent = wxGetApp().getAgent();
    Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;
    obj = dev->get_selected_machine();

    // check valid machine
    if (obj && dev->get_my_machine(obj->dev_id) == nullptr) {
        dev->set_selected_machine("");
        if (m_agent)
            m_agent->set_user_selected_machine("");
        show_status((int)MONITOR_NO_PRINTER);
        return;
    }

    //BBS check mqtt connections if user is login
    if (wxGetApp().is_user_login()) {
        dev->check_pushing();
        // check mqtt connection and reconnect if disconnected
        try {
            m_agent->refresh_connection();
        }
        catch (...) {
            ;
        }
    }

    if (obj) {
        wxGetApp().reset_to_active();
        if (obj->connection_type() != last_conn_type) {
            last_conn_type = obj->connection_type();
        }
    }

    m_status_info_panel->obj = obj;
    //m_status_info_panel->m_media_play_ctrl->SetMachineObject(obj);
    m_side_tools->update_status(obj);

    if (!obj) {
        show_status((int)MONITOR_NO_PRINTER);
        return;
    }

    if (obj->is_connecting()) {
        show_status(MONITOR_CONNECTING);
        return;
    } else if (!obj->is_connected()) {
        int server_status = 0;
        // only disconnected server in cloud mode
        if (obj->connection_type() != "lan") {
            if (m_agent) {
                server_status = m_agent->is_server_connected() ? 0 : (int)MONITOR_DISCONNECTED_SERVER;
            }
        }
        show_status((int) MONITOR_DISCONNECTED + server_status);
        return;
    }

    show_status(MONITOR_NORMAL);


    if (m_status_info_panel->IsShown()) {
        m_status_info_panel->update(obj);
    }

}

bool PhrozenMonitorPanel::Show(bool show)
{
#ifdef __APPLE__
    wxGetApp().mainframe->SetMinSize(wxGetApp().plater()->GetMinSize());
#endif

    NetworkAgent* m_agent = wxGetApp().getAgent();
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (show) {
        start_update();

        m_refresh_timer->Stop();
        m_refresh_timer->SetOwner(this);
        m_refresh_timer->Start(REFRESH_INTERVAL);
        wxPostEvent(this, wxTimerEvent());

        if (dev) {
            //set a default machine when obj is null
            obj = dev->get_selected_machine();
            if (obj == nullptr) {
                dev->load_last_machine();
                obj = dev->get_selected_machine();
                if (obj)
                    GUI::wxGetApp().sidebar().load_ams_list(obj->dev_id, obj);
            } else {
                obj->reset_update_time();
            }
        }
    } else {
        stop_update();
        m_refresh_timer->Stop();
    }
    return wxPanel::Show(show);
}

void PhrozenMonitorPanel::show_status(int status)
{
    if (!m_initialized) return;
    if (last_status == status)return;
    if ((last_status & (int)MonitorStatus::MONITOR_CONNECTING) != 0) {
        NetworkAgent* agent = wxGetApp().getAgent();
        json j;
        j["dev_id"] = obj ? obj->dev_id : "obj_nullptr";
        if ((status & (int)MonitorStatus::MONITOR_DISCONNECTED) != 0) {
            j["result"] = "failed";
        }
        else if ((status & (int)MonitorStatus::MONITOR_NORMAL) != 0) {
            j["result"] = "success";
        }
    }
    last_status = status;

    BOOST_LOG_TRIVIAL(info) << "monitor: show_status = " << status;



Freeze();
    // update panels
    m_status_info_panel->show_status(status);

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

#pragma endregion


} // GUI
} // Slic3r
