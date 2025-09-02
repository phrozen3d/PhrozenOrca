#ifndef slic3r_PhrozenBrowserDialog_hpp_
#define slic3r_PhrozenBrowserDialog_hpp_

#include <cstddef>
#include <memory>

#include <wx/dialog.h>
#include <wx/string.h>

class wxListView;
class wxStaticText;
class wxTimer;
class wxTimerEvent;
class address;

namespace Slic3r {
enum PrinterTechnology : unsigned char;

class PhrozenBrowserDialog: public wxDialog
{
public:
	PhrozenBrowserDialog(wxWindow *parent, const Slic3r::PrinterTechnology& );
	PhrozenBrowserDialog(PhrozenBrowserDialog &&) = delete;
	PhrozenBrowserDialog(const PhrozenBrowserDialog &) = delete;
	PhrozenBrowserDialog &operator=(PhrozenBrowserDialog &&) = delete;
	PhrozenBrowserDialog &operator=(const PhrozenBrowserDialog &) = delete;
	~PhrozenBrowserDialog();

	bool show_and_lookup();
	wxString get_selected() const;
private:
	wxListView *list;
	wxStaticText *label;
	std::unique_ptr<wxTimer> timer;
	unsigned timer_state;
	Slic3r::PrinterTechnology tech;

	void on_timer(wxTimerEvent &);
    void on_timer_process();

    struct NetworkingMachineInfo {
        std::string machineName;
        std::string ip;
        bool connected;
        bool pressed;
    };

    std::vector<NetworkingMachineInfo> m_kNetworkingMachineInfoList;
};

}

#endif //slic3r_PhrozenBrowserDialog_hpp_
