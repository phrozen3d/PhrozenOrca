#include "slic3r/Utils/Bonjour.hpp"   // On Windows, boost needs to be included before wxWidgets headers

#include "PhrozenBrowserDialog.hpp"

#include <set>
#include <thread>
#include <chrono>

#include <boost/nowide/convert.hpp>
#include <boost/asio.hpp>

#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/listctrl.h>
#include <wx/stattext.h>
#include <wx/timer.h>
#include <wx/wupdlock.h>

#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/format.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {

PhrozenBrowserDialog::PhrozenBrowserDialog(wxWindow *parent, const Slic3r::PrinterTechnology& tech)
	: wxDialog(parent, wxID_ANY, _(L("Network lookup")), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER)
	, list(new wxListView(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT|wxSIMPLE_BORDER))
	//, replies(new ReplySet)
	, label(new wxStaticText(this, wxID_ANY, ""))
	, timer(new wxTimer())
	, timer_state(0)
	, tech(tech)
{
	const int em = GUI::wxGetApp().em_unit();
	list->SetMinSize(wxSize(80 * em, 30 * em));

	wxBoxSizer *vsizer = new wxBoxSizer(wxVERTICAL);

	vsizer->Add(label, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, em);

	list->SetSingleStyle(wxLC_SINGLE_SEL);
	list->SetSingleStyle(wxLC_SORT_DESCENDING);
	list->AppendColumn(_(L("Address")), wxLIST_FORMAT_LEFT, 20 * em);
	list->AppendColumn(_(L("Hostname")), wxLIST_FORMAT_LEFT, 10 * em);
	list->AppendColumn(_(L("Service name")), wxLIST_FORMAT_LEFT, 10 * em);
	if (tech == ptFFF) {
		list->AppendColumn(_(L("OctoPrint version")), wxLIST_FORMAT_LEFT, 5 * em);
	}

	vsizer->Add(list, 1, wxEXPAND | wxALL, em);

	wxBoxSizer *button_sizer = new wxBoxSizer(wxHORIZONTAL);
	button_sizer->Add(new wxButton(this, wxID_OK, _L("OK")), 0, wxALL, em);
	button_sizer->Add(new wxButton(this, wxID_CANCEL, _L("Cancel")), 0, wxALL, em);
	// ^ Note: The Ok/Cancel labels are translated by wxWidgets

	vsizer->Add(button_sizer, 0, wxALIGN_CENTER);
	SetSizerAndFit(vsizer);

	Bind(wxEVT_TIMER, &PhrozenBrowserDialog::on_timer, this);
	GUI::wxGetApp().UpdateDlgDarkUI(this);
}

PhrozenBrowserDialog::~PhrozenBrowserDialog()
{
	// Needed bacuse of forward defs
}

bool PhrozenBrowserDialog::show_and_lookup()
{
    Show();   // Because we need GetId() to work before ShowModal()

    timer->Stop();
    timer->SetOwner(this);
    timer_state = 1;
    timer->Start(1000);
    on_timer_process();
    
    try {
        using namespace boost::asio;
        using namespace boost::asio::ip;
        
        // Create io_context for asio operations
        io_context io_ctx;
        
        // Create UDP socket
        udp::socket socket(io_ctx, udp::endpoint(udp::v4(), 0));
        
        // Enable broadcast option
        socket.set_option(socket_base::broadcast(true));
        
        // Set up broadcast endpoint (address 255.255.255.255; port 8989)
        udp::endpoint broadcast_endpoint(address_v4::broadcast(), 8989);
        
        // Message to broadcast
        std::string broadcast_msg = "mkswifi";
        
        BOOST_LOG_TRIVIAL(info) << "[Phrozen] Sending UDP broadcast: " << broadcast_msg;
        
        // Send broadcast message
        boost::system::error_code send_ec;
        size_t bytes_sent = socket.send_to(buffer(broadcast_msg), broadcast_endpoint, 0, send_ec);
        
        if (send_ec) {
            BOOST_LOG_TRIVIAL(warning) << "[Phrozen] UDP broadcast send error: " << send_ec.message();
            return false;
        }
        
        BOOST_LOG_TRIVIAL(info) << "[Phrozen] UDP broadcast sent: " << bytes_sent << " bytes";
        
        // Set socket to non-blocking mode for receiving responses
        socket.non_blocking(true);
        
        // Receive responses with timeout
        auto start_time = std::chrono::steady_clock::now();
        const auto timeout_duration = std::chrono::seconds(2);
        
        char receive_buffer[1024];
        udp::endpoint sender_endpoint;
        
        while (std::chrono::steady_clock::now() - start_time < timeout_duration) {
            boost::system::error_code receive_ec;
            size_t bytes_received = socket.receive_from(
                buffer(receive_buffer), sender_endpoint, 0, receive_ec);
            
            if (!receive_ec && bytes_received > 0) {
                // Successfully received data
                receive_buffer[bytes_received] = '\0';
                std::string received_data(receive_buffer, bytes_received);
                
                BOOST_LOG_TRIVIAL(info) << "[Phrozen] Received " << bytes_received 
                                      << " bytes from " << sender_endpoint.address().to_string() 
                                      << ": " << received_data;
                
                // Parse the response
                NetworkingMachineInfo machine_info;
                
                // Extract machine name from response (assuming format similar to original)
                if (received_data.length() >= 8) {
                    size_t comma_pos = received_data.find(",");
                    if (comma_pos != std::string::npos && comma_pos > 8) {
                        machine_info.machineName = received_data.substr(8, comma_pos - 8);
                    } else {
                        machine_info.machineName = "Arco";  // Default name
                    }
                } else {
                    machine_info.machineName = "Arco";
                }
                
                machine_info.ip = sender_endpoint.address().to_string();
                machine_info.connected = false;
                machine_info.pressed = false;
                
                // Add to list if not already exists
                bool already_exists = false;
                for (const auto& existing : m_kNetworkingMachineInfoList) {
                    if (existing.ip == machine_info.ip) {
                        already_exists = true;
                        break;
                    }
                }
                
                if (!already_exists) {
                    m_kNetworkingMachineInfoList.push_back(machine_info);
                    BOOST_LOG_TRIVIAL(info) << "[Phrozen] Added machine: " << machine_info.machineName 
                                          << " at " << machine_info.ip;
                    
                    // Update UI list
                    wxWindowUpdateLocker freeze_guard(this);
                    
                    // Add new item to the list view
                    auto item_index = list->GetItemCount();
                    auto item = list->InsertItem(item_index, wxString::FromUTF8(machine_info.ip));
                    list->SetItem(item, 1, wxString::FromUTF8(machine_info.machineName));
                    list->SetItem(item, 2, wxString::FromUTF8("Phrozen Connect"));
                }
            }
            else if (receive_ec == error::would_block) {
                // No data available yet, sleep briefly and continue
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            else {
                // Other error occurred
                BOOST_LOG_TRIVIAL(warning) << "[Phrozen] UDP receive error: " << receive_ec.message();
                break;
            }
        }
        
        // Stop the search timer
        timer_state = 0;
        on_timer_process();
        
        BOOST_LOG_TRIVIAL(info) << "[Phrozen] UDP discovery completed. Found " 
                                << m_kNetworkingMachineInfoList.size() << " devices";
        
        // Show modal dialog and return result
        bool result = ShowModal() == wxID_OK && list->GetFirstSelected() >= 0;
        
        return result;
        
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[Phrozen] UDP discovery exception: " << e.what();
        timer_state = 0;
        on_timer_process();
        return false;
    }
}

wxString PhrozenBrowserDialog::get_selected() const
{
	auto sel = list->GetFirstSelected();
	return sel >= 0 ? list->GetItemText(sel) : wxString();
}

void PhrozenBrowserDialog::on_timer(wxTimerEvent &)
{
    on_timer_process();
}

// This is here so the function can be bound to wxEVT_TIMER and also called
// explicitly (wxTimerEvent should not be created by user code).
void PhrozenBrowserDialog::on_timer_process()
{
    const auto search_str = _L("Searching for devices");

    if (timer_state > 0) {
        const std::string dots(timer_state, '.');
        label->SetLabel(search_str + dots);
        timer_state = (timer_state) % 3 + 1;
    } else {
        label->SetLabel(search_str + ": " + _L("Finished") + ".");
        timer->Stop();
    }
}

}
