#ifndef slic3r_GUI_PhrozenSelectMachinePopup_hpp_
#define slic3r_GUI_PhrozenSelectMachinePopup_hpp_

#include <wx/wx.h>
#include <wx/intl.h>
#include <wx/collpane.h>
#include <wx/dataview.h>
#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/dataview.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/string.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/hyperlink.h>
#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/popupwin.h>
#include <wx/spinctrl.h>
#include <wx/artprov.h>
#include <wx/wrapsizer.h>
#include <wx/srchctrl.h>

//#include "../ReleaseNote.hpp"
#include "../GUI_Utils.hpp"
#include "../wxExtensions.hpp"
#include "../DeviceManager.hpp"
#include "../Plater.hpp"
#include "../BBLStatusBar.hpp"
#include "../BBLStatusBarSend.hpp"
#include "../Widgets/Label.hpp"
#include "../Widgets/Button.hpp"
#include "../Widgets/CheckBox.hpp"
#include "../Widgets/ComboBox.hpp"
#include "../Widgets/ScrolledWindow.hpp"
#include "../Widgets/PopupWindow.hpp"
#include <wx/simplebook.h>
#include <wx/hashmap.h>

namespace Slic3r { namespace GUI {

enum class PhrozenPrinterState : int32_t {
    OFFLINE,
    IDLE,
    BUSY,
    LOCK,
    IN_LAN,
    NOT_SELECT
};

enum class PhrozenPrinterBindState : int32_t {
    NONE,
    ALLOW_BIND,
    ALLOW_UNBIND
};


wxDECLARE_EVENT(EVT_CHECKBOX_CHANGE, wxCommandEvent);
wxDECLARE_EVENT(EVT_PHROZEN_CONNECT_MACHINE_BY_IP, wxCommandEvent);
wxDECLARE_EVENT(EVT_PHROZEN_DISCONNECT_MACHINE, wxCommandEvent);
wxDECLARE_EVENT(EVT_PHROZEN_UPDATE_MACHINE_TITLE, wxCommandEvent);

//PhrozenIpConnectDialog
wxDECLARE_EVENT(EVT_PHROZEN_UPDATE_CONNECT_MSG, wxCommandEvent);
wxDECLARE_EVENT(EVT_PHROZEN_IP_CONNECT_SUCCESS, wxCommandEvent);

#define PHROZEN_SELECT_MACHINE_POPUP_SIZE wxSize(FromDIP(216), FromDIP(364))
#define PHROZEN_SELECT_MACHINE_LIST_SIZE wxSize(FromDIP(212), FromDIP(360))
#define PHROZEN_SELECT_MACHINE_ITEM_SIZE wxSize(FromDIP(190), FromDIP(35))
#define PHROZEN_SELECT_MACHINE_GREY900 wxColour(38, 46, 48)
#define PHROZEN_SELECT_MACHINE_GREY600 wxColour(144, 144, 144)
#define PHROZEN_SELECT_MACHINE_GREY400 wxColour(206, 206, 206)
#define PHROZEN_SELECT_MACHINE_BRAND wxColour(255, 124, 63)
#define PHROZEN_SELECT_MACHINE_REMIND wxColour(255, 111, 0)
#define PHROZEN_SELECT_MACHINE_LIGHT_GREEN wxColour(219, 253, 231)

#pragma region PhrozenInputIpAddressDialog
class PhrozenInputIpAddressDialog : public DPIDialog
{
public:
    wxString comfirm_before_enter_text;
    wxString comfirm_after_enter_text;
    wxString comfirm_last_enter_text;

    boost::thread* m_thread{nullptr};

    std::string m_ip;
    Label* m_tip1{ nullptr };
    Label* m_tip2{ nullptr };
    Label* m_tip4{ nullptr };
    PhrozenInputIpAddressDialog(wxWindow* parent = nullptr);
    ~PhrozenInputIpAddressDialog();

    wxPanel * ip_input_top_panel{ nullptr };
    Button* m_button_ok{ nullptr };
    Label* m_tips_ip{ nullptr };
    Label* m_test_right_msg{ nullptr };
    Label* m_test_wrong_msg{ nullptr };
    TextInput* m_input_ip{ nullptr };
    wxStaticBitmap* m_img_help{ nullptr };
    wxStaticBitmap* m_img_step1{ nullptr };
    wxStaticBitmap* m_img_step2{ nullptr };
    wxHyperlinkCtrl* m_trouble_shoot{ nullptr };
    int    m_result;

    void on_cancel();
    void update_title(wxString title);
    void update_test_msg(wxString msg, bool connected);
    bool isIp(std::string ipstr);
    void on_ok(wxMouseEvent& evt);
    void on_text(wxCommandEvent& evt);
    void on_dpi_changed(const wxRect& suggested_rect) override;
};
#pragma endregion

#pragma region  PhrozenIpConnectDialog

class PhrozenIpConnectDialog : public DPIDialog
{
public:
    PhrozenIpConnectDialog(wxWindow* parent = nullptr);
    ~PhrozenIpConnectDialog();

    void set_ip_address(const std::string& ip);
    int ShowModal() wxOVERRIDE;
    bool IsConnectSuccess() { return m_bSuccess; }

private:
    void start_connect();
    boost::thread* m_thread{nullptr};
    std::string m_ip_address;

    Label* m_test_msg{ nullptr };
    wxTimer* m_kSuccessCloseTimer{ nullptr };
    int closeCount{3};
    std::shared_ptr<BBLStatusBarSend> m_status_bar;
    bool m_bSuccess = false;

    void on_cancel();
    void post_update_msg(wxString text, bool is_error);
    void update_msg_event(wxCommandEvent& evt);
    void workerConnectThreadFunc(std::string str_ip);
    void OnTimer(wxTimerEvent& event);
    void on_dpi_changed(const wxRect& suggested_rect) override;
};
#pragma endregion 

class PhrozenMachineObjectPanel : public wxPanel
{
private:
    bool        m_show_edit{false};
    bool        m_show_bind{false};
    bool        m_hover {false};
    bool        m_is_macos_special_version{false};


    PhrozenPrinterBindState   m_bind_state{PhrozenPrinterBindState::NONE};
    PhrozenPrinterState       m_state;

    ScalableBitmap m_unbind_img;
    ScalableBitmap m_edit_name_img;
    ScalableBitmap m_select_unbind_img;

    ScalableBitmap m_printer_status_offline;
    ScalableBitmap m_printer_status_busy;
    ScalableBitmap m_printer_status_idle;
    ScalableBitmap m_printer_status_lock;
    ScalableBitmap m_printer_in_lan;
    std::string m_strIp;
    //MachineObject *m_info;

protected:
    wxStaticBitmap *m_bitmap_info;
    wxStaticBitmap *m_bitmap_bind;

public:
    PhrozenMachineObjectPanel(wxWindow *      parent,
                       wxWindowID      id    = wxID_ANY,
                       const wxPoint & pos   = wxDefaultPosition,
                       const wxSize &  size  = wxDefaultSize,
                       long            style = wxTAB_TRAVERSAL,
                       const wxString &name  = wxEmptyString);

    ~PhrozenMachineObjectPanel();

    void set_printer_state(PhrozenPrinterState state);
    void show_printer_bind(bool show, PhrozenPrinterBindState state);
    void show_edit_printer_name(bool show);
    void update_machine_info(MachineObject *info, bool is_my_devices = false);
    void set_maching_ip( std::string strIp ){ m_strIp = strIp; }
    std::string get_machine_ip(){ return m_strIp; }
protected:
    void OnPaint(wxPaintEvent &event);
    void render(wxDC &dc);
    void doRender(wxDC &dc);
    void on_mouse_enter(wxMouseEvent &evt);
    void on_mouse_leave(wxMouseEvent &evt);
    void on_mouse_left_up(wxMouseEvent &evt);
};

class PhrozenIpKeyInButton : public wxPanel
{
public:
    PhrozenIpKeyInButton(wxWindow* parent,
        wxWindowID      winid = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize);
    ~PhrozenIpKeyInButton() {};

    ScalableBitmap       m_bitmap;
    bool           m_hover{false};

    void OnPaint(wxPaintEvent& event);
    void render(wxDC& dc);
    void doRender(wxDC& dc);

    void on_mouse_enter(wxMouseEvent& evt);
    void on_mouse_leave(wxMouseEvent& evt);
    void on_mouse_left_up(wxMouseEvent& evt);
};

class PhrozenSelectMachinePopup : public PopupWindow
{
public:
    PhrozenSelectMachinePopup(wxWindow *parent);
    ~PhrozenSelectMachinePopup();

    // PopupWindow virtual methods are all overridden to log them
    virtual void Popup(wxWindow *focus = NULL) wxOVERRIDE;
    virtual void OnDismiss() wxOVERRIDE;
    virtual bool ProcessLeftDown(wxMouseEvent &event) wxOVERRIDE;
    virtual bool Show(bool show = true) wxOVERRIDE;

    void update_machine_list(wxCommandEvent &event);
    void start_ssdp(bool on_off);
    bool was_dismiss() { return m_dismiss; }

private:
    int                               m_other_devices_count{0};
    PhrozenIpKeyInButton*             m_panel_direct_connection{nullptr};
    wxWindow*                         m_placeholder_panel{nullptr};
    wxHyperlinkCtrl*                  m_hyperlink{nullptr};
    Label*                            m_ping_code_text{nullptr};
    wxStaticBitmap*                   m_img_ping_code{nullptr};
    wxBoxSizer *                      m_sizer_body{nullptr};
    wxBoxSizer *                      m_sizer_my_devices{nullptr};
    wxBoxSizer *                      m_sizer_other_devices{nullptr};
    wxBoxSizer *                      m_sizer_search_bar{nullptr};
    wxSearchCtrl*                     m_search_bar{nullptr};
    wxScrolledWindow *                m_scrolledWindow{nullptr};
    wxWindow *                        m_panel_body{nullptr};
    wxTimer *                         m_refresh_timer{nullptr};
    boost::thread*                    get_print_info_thread{ nullptr };
    std::shared_ptr<int>              m_token = std::make_shared<int>(0);
    bool                              m_dismiss { false };
    bool                              m_bFirstUpdating{ false };


    std::vector< PhrozenMachineObjectPanel* > m_history_lan_machine_ip_panels;
    size_t m_nMaxHistoryIp{ 5 };
    std::map< std::string, PhrozenMachineObjectPanel* > m_lan_machine_ip_panels;

private:
    void OnLeftUp(wxMouseEvent &event);
    void on_timer(wxTimerEvent &event);

	void update_history_devices();
    void update_lan_devices();
    bool search_for_printer(MachineObject* obj);
    void on_dissmiss_win(wxCommandEvent &event);
    void release_panel_data( PhrozenMachineObjectPanel* pIpPanel );
    void clear_all_searched_ip_panel();
    wxWindow *create_title_panel(wxString text);
    PhrozenMachineObjectPanel* m_pSearchingPad{nullptr};
    void create_searching_pad();
    void remove_searching_pad();
    PhrozenMachineObjectPanel* create_ip_object_panel( PhrozenPrinterState eState, std::string strIp, bool bIsConnected = false );

    void on_ip_panel_clicked( wxCommandEvent& event );
    //m_select_machine->Bind(EVT_PHROZEN_CONNECT_MACHINE_BY_IP, &PhrozenMonitorPanel::OnConnectMachineByIp, this );
};

class PhrozenEditDevNameDialog : public DPIDialog
{
public:
    PhrozenEditDevNameDialog(Plater *plater = nullptr);
    ~PhrozenEditDevNameDialog();

    void set_machine_obj(MachineObject *obj);
    void on_dpi_changed(const wxRect &suggested_rect) override;
    void on_edit_name(wxCommandEvent &e);

    Button*             m_button_confirm{nullptr};
    TextInput*          m_textCtr{nullptr};
    wxStaticText*       m_static_valid{nullptr};
    MachineObject*      m_info{nullptr};
};

}} // namespace Slic3r::GUI

#endif
