#include "PhrozenNetworkAgent.hpp"
#include "libslic3r/AppConfig.hpp"
#include <boost/log/trivial.hpp>
#include "PhrozenMachineDatas.hpp"
#include <sstream>

using namespace Slic3r;

#pragma region PhrozenFrameProcessor
// ============================================
// ReceiveResponse() Processing Modules
// ============================================
// 
// Frame processing module for WebSocket frame handling
struct PhrozenFrameProcessor {

#ifndef __APPLE__
    static bool IsContinuationFrame(const struct curl_ws_frame* meta) {
        bool is_continuation_frame = false;
        #if defined(CURLWS_CONT) && defined(CURLWS_FIN)
        is_continuation_frame = (meta->flags & CURLWS_CONT) != 0;
        #elif defined(CURLWS_CONT)
        is_continuation_frame = (meta->flags & CURLWS_CONT) != 0;
        #else
        is_continuation_frame = ((meta->flags & 0x80) == 0);
        #endif
        return is_continuation_frame;
    }
    
    static bool IsFinalFrame(const struct curl_ws_frame* meta) {
        bool is_final_frame = true;
        #if defined(CURLWS_CONT) && defined(CURLWS_FIN)
        is_final_frame = (meta->flags & CURLWS_FIN) != 0;
        #elif defined(CURLWS_CONT)
        is_final_frame = (meta->flags & CURLWS_CONT) == 0;
        #else
        is_final_frame = ((meta->flags & 0x80) != 0);
        #endif
        return is_final_frame;
    }
#else
    static bool IsContinuationFrame(struct curl_ws_frame* meta) {
        bool is_continuation_frame = false;
        #if defined(CURLWS_CONT) && defined(CURLWS_FIN)
        is_continuation_frame = (meta->flags & CURLWS_CONT) != 0;
        #elif defined(CURLWS_CONT)
        is_continuation_frame = (meta->flags & CURLWS_CONT) != 0;
        #else
        is_continuation_frame = ((meta->flags & 0x80) == 0);
        #endif
        return is_continuation_frame;
    }
    
    static bool IsFinalFrame(struct curl_ws_frame* meta) {
        bool is_final_frame = true;
        #if defined(CURLWS_CONT) && defined(CURLWS_FIN)
        is_final_frame = (meta->flags & CURLWS_FIN) != 0;
        #elif defined(CURLWS_CONT)
        is_final_frame = (meta->flags & CURLWS_CONT) == 0;
        #else
        is_final_frame = ((meta->flags & 0x80) != 0);
        #endif
        return is_final_frame;
    }
#endif

    
    static std::string CombineFrames(const std::string& frame_data, 
                                     std::string& accumulated_buffer) {
        std::string complete_message = accumulated_buffer.empty() 
            ? frame_data 
            : (accumulated_buffer + frame_data);
        accumulated_buffer.clear();
        return complete_message;
    }
    
    static void UpdateSlidingWindow(std::string& sliding_window_buffer, 
                                    const std::string& complete_message,
                                    size_t max_size) {
        sliding_window_buffer += complete_message;
        if (sliding_window_buffer.size() > max_size) {
            sliding_window_buffer = sliding_window_buffer.substr(
                sliding_window_buffer.size() - max_size / 2
            );
        }
    }
};
#pragma endregion

#pragma region PhrozenNetworkAgent
// Constructor
PhrozenNetworkAgent::PhrozenNetworkAgent(std::string log_dir)
    : m_log_dir(log_dir)
    , m_config_dir("")
    , m_connected_dev_ip("")
    , m_is_connected(false)
    , m_timeout_ms(30000)  // 30 seconds default timeout
    , m_curl_handle(nullptr)
    , m_websocket_handle(nullptr)
    , m_on_message_callback(nullptr)
    , m_on_connection_callback(nullptr)
    , m_on_error_callback(nullptr)
    , m_on_progress_callback(nullptr)
{
    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Constructor called with log_dir: " << log_dir;

    // implement callback function
    m_fn_snapshop_write_stream_callback = [&](void* contents, size_t size, size_t nmemb, void* userp) -> size_t
    {
        size_t total_size = size * nmemb;
        std::vector<unsigned char>* buffer = (std::vector<unsigned char> *)userp;
        buffer->insert(buffer->end(), (unsigned char*)contents, (unsigned char*)contents + total_size);
        return total_size;
    };

}

// Destructor
PhrozenNetworkAgent::~PhrozenNetworkAgent()
{
    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Destructor called";
    disconnect_printer();
    cleanup_curl();
}

bool PhrozenNetworkAgent::InitializeConnector( const std::string& strIp  )
{
    if ( !m_spWebServiceInfo )
    {
        m_spWebServiceInfo = std::make_unique< PhrozenWebServiceInfo >();
    }
    if ( !m_spPrinterInfo )
    {
        m_spPrinterInfo = std::make_unique< PhrozenPrinterInfo >();
    }
    if ( !m_spThreadControl )
    {
        m_spThreadControl = std::make_unique< PhrozenThreadControl >();
    }
    if ( !m_spMonitorWindow )
    {
        m_spMonitorWindow = std::make_unique< PhrozenMonitorWindow >();
    }


    //trigger ams update query command after connect to speicified IP address (Printer)
    SetFirstTimeToSendQuery( true );
    m_spWebServiceInfo->ip = strIp;

    bool bSuccess = InitializeConnectorImp( strIp );
    if ( !bSuccess )
    {
        m_spWebServiceInfo->ip = "";
        m_spMonitorWindow->connectedMachineName = "";
        m_spMonitorWindow->isShownIPConnectNotification = true;
        SetStartReceiving(false);
        SetStartSending(false);
    }
    return bSuccess;
}

bool PhrozenNetworkAgent::InitializeConnectorImp( const std::string& strIp )
{
    auto fnCheckCurlSuccess =[&]( CURLcode& res ) -> bool
    {
        bool bSuccess = res == CURLcode::CURLE_OK;
        if ( !bSuccess ){  m_spWebServiceInfo->ip = ""; }
        return bSuccess;
    };

    CURLcode res = CURLE_FAILED_INIT;
    curl_version_info_data *ver_info;

    // Check CURL version and WebSocket support
    ver_info = curl_version_info(CURLVERSION_NOW);
    if (!ver_info) {
        printf("Failed to get CURL version info\n");
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "Failed to get CURL version info\n";
        return fnCheckCurlSuccess(res);
    }

    printf("CURL version: %s\n", ver_info->version);
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "CURL version: " << ver_info->version << "\n";

    // Check if WebSocket protocol is supported
    bool ws_supported = false;
    if (ver_info->protocols) {
        for (const char * const *proto = ver_info->protocols; *proto; ++proto) {
            if (strcmp(*proto, "ws") == 0 || strcmp(*proto, "wss") == 0) {
                ws_supported = true;
                printf("WebSocket protocol supported: %s\n", *proto);
                break;
            }
        }
    }

    if (!ws_supported) {
        printf("WebSocket protocol not supported by this CURL build\n");
        printf("Supported protocols: ");
        if (ver_info->protocols) {
            for (const char * const *proto = ver_info->protocols; *proto; ++proto) {
                printf("%s ", *proto);
            }
        }
        printf("\n");
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "WebSocket protocol not supported by this CURL build\n";
        return fnCheckCurlSuccess( res ); // Protocol not supported
    }

    CleanupWebSocketConnection();
    m_pCurlMainWebsocket = curl_easy_init();

    if (m_pCurlMainWebsocket) {
        // Validate webServiceInfo before using
        if ( m_spWebServiceInfo->ip.empty() || m_spWebServiceInfo->port.empty()) {
            printf("WebService info not properly initialized: IP=%s, Port=%s\n",
                   m_spWebServiceInfo->ip.c_str(), m_spWebServiceInfo->port.c_str());
            curl_easy_cleanup(m_pCurlMainWebsocket);
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "WebService info not properly initialized\n";
            return fnCheckCurlSuccess( res ); // Invalid configuration
        }

        std::string url = "ws://" + m_spWebServiceInfo->ip + ":" + m_spWebServiceInfo->port + "/websocket";
        printf("Attempting WebSocket connection to: %s\n", url.c_str());

#ifdef __APPLE__
        // Use the socket API to trigger an ARP request and wait for it to complete (the wait is already included internally).
        TriggerArpResolution(m_spWebServiceInfo->ip);
        
        // Waiting for the ARP table to be updated and for confirmation that ARP resolution is complete (maximum wait 1000ms).
        if (WaitForArpResolution(m_spWebServiceInfo->ip, 1000)) {
            printf("ARP resolution completed, ARP entry confirmed in table\n");
        } else {
            printf("ARP resolution may not be complete, but proceeding with connection\n");
            // The connection will continue even if the ARP table check fails (there may be a delay in ARP table updates).
        }
        printf("Proceeding with WebSocket connection\n");
#endif

        // Set CURL options
        curl_easy_setopt(m_pCurlMainWebsocket, CURLOPT_URL, url.c_str());
        curl_easy_setopt(m_pCurlMainWebsocket, CURLOPT_TIMEOUT_MS, 5000L); // Increased timeout
        curl_easy_setopt(m_pCurlMainWebsocket, CURLOPT_CONNECT_ONLY, 2L); /* websocket style */

        // Enable verbose output for debugging
        curl_easy_setopt(m_pCurlMainWebsocket, CURLOPT_VERBOSE, 1L);

        // Set user agent
        curl_easy_setopt(m_pCurlMainWebsocket, CURLOPT_USERAGENT, "PhrozenOrca WebSocket Client");

        // Disable SSL verification for testing (remove in production)
        curl_easy_setopt(m_pCurlMainWebsocket, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(m_pCurlMainWebsocket, CURLOPT_SSL_VERIFYHOST, 0L);

        /* Perform the request, res will get the return code */
        res = curl_easy_perform(m_pCurlMainWebsocket);

        /* Check for errors */
        if (res != CURLE_OK) {
            printf("WebSocket connection failed: %s (Error code: %d)\n",
                   curl_easy_strerror(res), res);
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "WebSocket connection failed\n";

            // Get more detailed error information
            long response_code;
            curl_easy_getinfo(m_pCurlMainWebsocket, CURLINFO_RESPONSE_CODE, &response_code);
            printf("HTTP response code: %ld\n", response_code);

            curl_easy_cleanup(m_pCurlMainWebsocket);
            m_pCurlMainWebsocket = nullptr;
            SetStartReceiving( false );
            SetStartSending( false );

            return fnCheckCurlSuccess( res ); // Return CURL error code
        }
        else {
            printf("WebSocket connection established successfully\n");

            // Get connection info
            long response_code;
            curl_easy_getinfo(m_pCurlMainWebsocket, CURLINFO_RESPONSE_CODE, &response_code);
            printf("HTTP response code: %ld\n", response_code);

            char *effective_url;
            curl_easy_getinfo(m_pCurlMainWebsocket, CURLINFO_EFFECTIVE_URL, &effective_url);
            if (effective_url) {
                printf("Connected to: %s\n", effective_url);
            }
        }
    }
    else {
        printf("Failed to initialize CURL handle\n");
        m_pCurlMainWebsocket = nullptr;
        SetStartReceiving( false );
        SetStartSending( false );
        return fnCheckCurlSuccess( res ); // CURL init failed
    }

    return fnCheckCurlSuccess( res );
}

void PhrozenNetworkAgent::CleanupWebSocketConnection()
{
    if ( m_pCurlMainWebsocket != nullptr) {
        //close websocket while it linking.
        size_t sent;
        curl_ws_send(m_pCurlMainWebsocket, "", 0, &sent, 0, CURLWS_CLOSE);
        
        //release memory
        curl_easy_cleanup(m_pCurlMainWebsocket);
        m_pCurlMainWebsocket = nullptr;
    }

    m_spWebServiceInfo->reset();
}

void PhrozenNetworkAgent::SetStartSending( bool bStart )
{
    m_bStartSending.store(bStart, std::memory_order_relaxed);
}

bool PhrozenNetworkAgent::IsStartSending()
{
    return m_bStartSending.load(std::memory_order_relaxed);
}

void PhrozenNetworkAgent::SetStartReceiving( bool bStart )
{
    m_bStartReceiving.store(bStart, std::memory_order_relaxed);
}

bool PhrozenNetworkAgent::IsStartReceiving()
{
    return m_bStartReceiving.load(std::memory_order_relaxed);
}

void PhrozenNetworkAgent::SetFirstTimeToSendQuery( bool flag )
{
    m_bFirstTimeToSendQuery.store( flag, std::memory_order_relaxed);
}

bool PhrozenNetworkAgent::IsFirstTimeToSendQuery()
{
    return m_bFirstTimeToSendQuery.load(std::memory_order_relaxed);
}

// Initialize logging
int PhrozenNetworkAgent::init_log()
{
    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Initializing log with directory: " << m_log_dir;
    // Log initialization logic here
    return 0;
}

// Set configuration directory
int PhrozenNetworkAgent::set_config_dir(std::string config_dir)
{
    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Setting config directory: " << config_dir;
    m_config_dir = config_dir;
    return 0;
}

// Start the agent
int PhrozenNetworkAgent::start()
{
    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Starting agent";
    CURLcode result = initialize_curl();
    if (result != CURLE_OK) {
        BOOST_LOG_TRIVIAL(error) << "PhrozenNetworkAgent: Failed to initialize CURL: " << get_error_string(result);
        return -1;
    }
    return 0;
}

// Connect to a printer
int PhrozenNetworkAgent::connect_printer( std::string dev_ip)
{
    std::lock_guard<std::mutex> lock(m_connection_mutex);

    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Connecting to printer - IP: " << dev_ip;

    if (m_is_connected) {
        BOOST_LOG_TRIVIAL(warning) << "PhrozenNetworkAgent: Already connected to a printer, disconnecting first";
        disconnect_printer();
    }

    m_connected_dev_ip = dev_ip;
    m_is_connected = true;

    // Notify via callback
    if (m_on_connection_callback) {
        m_on_connection_callback(true);
    }

    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Successfully connected to printer";
    return 0;
}

// Disconnect from printer
int PhrozenNetworkAgent::disconnect_printer()
{
    std::lock_guard<std::mutex> lock(m_connection_mutex);

    if (!m_is_connected) {
        BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: No active connection to disconnect";
        return 0;
    }

    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Disconnecting from printer: " << m_connected_dev_ip;

    m_connected_dev_ip.clear();
    m_is_connected = false;

    // Notify via callback
    if (m_on_connection_callback) {
        m_on_connection_callback(false);
    }

    return 0;
}

// Check if connected
bool PhrozenNetworkAgent::is_connected()
{
    std::lock_guard<std::mutex> lock(m_connection_mutex);
    return m_is_connected;
}

// Set message callback
void PhrozenNetworkAgent::set_on_message_callback(OnMessageCallback callback)
{
    m_on_message_callback = callback;
}

// Set connection callback
void PhrozenNetworkAgent::set_on_connection_callback(OnConnectionCallback callback)
{
    m_on_connection_callback = callback;
}

// Set error callback
void PhrozenNetworkAgent::set_on_error_callback(OnErrorCallback callback)
{
    m_on_error_callback = callback;
}

// Set progress callback
void PhrozenNetworkAgent::set_on_progress_callback(OnProgressCallback callback)
{
    m_on_progress_callback = callback;
}

// Send message
int PhrozenNetworkAgent::send_message(std::string dev_ip, std::string message)
{
    std::lock_guard<std::mutex> lock(m_message_mutex);

    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Sending message to " << dev_ip << ": " << message;

    if (!m_is_connected || m_connected_dev_ip != dev_ip) {
        BOOST_LOG_TRIVIAL(error) << "PhrozenNetworkAgent: Not connected to device " << dev_ip;
        return -1;
    }

    // Implementation needed: Send message to printer
    // This would typically use HTTP or WebSocket communication

    return 0;
}

// Send GCode command
int PhrozenNetworkAgent::send_gcode_command(std::string dev_ip, std::string gcode)
{
    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Sending GCode to " << dev_ip << ": " << gcode;

    if (!m_is_connected || m_connected_dev_ip != dev_ip) {
        BOOST_LOG_TRIVIAL(error) << "PhrozenNetworkAgent: Not connected to device " << dev_ip;
        return -1;
    }

    // Implementation needed: Send GCode command

    return 0;
}

// Send file
int PhrozenNetworkAgent::send_file(std::string dev_ip, std::string file_path, OnProgressCallback progress_fn)
{
    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Sending file to " << dev_ip << ": " << file_path;

    if (!m_is_connected || m_connected_dev_ip != dev_ip) {
        BOOST_LOG_TRIVIAL(error) << "PhrozenNetworkAgent: Not connected to device " << dev_ip;
        return -1;
    }

    // Implementation needed: File upload logic

    return 0;
}

// Download file
int PhrozenNetworkAgent::download_file(std::string dev_ip, std::string remote_path, std::string local_path)
{
    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Downloading file from " << dev_ip
                            << " - Remote: " << remote_path << ", Local: " << local_path;

    if (!m_is_connected || m_connected_dev_ip != dev_ip) {
        BOOST_LOG_TRIVIAL(error) << "PhrozenNetworkAgent: Not connected to device " << dev_ip;
        return -1;
    }

    // Implementation needed: File download logic

    return 0;
}

// Get printer info
int PhrozenNetworkAgent::get_printer_info(std::string dev_ip, std::string* info_json)
{
    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Getting printer info for " << dev_ip;

    if (!m_is_connected || m_connected_dev_ip != dev_ip) {
        BOOST_LOG_TRIVIAL(error) << "PhrozenNetworkAgent: Not connected to device " << dev_ip;
        return -1;
    }

    // Implementation needed: Query printer information

    return 0;
}

// Get printer status
int PhrozenNetworkAgent::get_printer_status(std::string dev_ip, std::string* status_json)
{
    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Getting printer status for " << dev_ip;

    if (!m_is_connected || m_connected_dev_ip != dev_ip) {
        BOOST_LOG_TRIVIAL(error) << "PhrozenNetworkAgent: Not connected to device " << dev_ip;
        return -1;
    }

    // Implementation needed: Query printer status

    return 0;
}


void PhrozenNetworkAgent::RunSendMessage( const std::vector< json >& kMessageList,
                                          const std::vector< bool >& kSendingList )
{

    try {
        size_t uCount = kMessageList.size();
        if ( kSendingList.size() != uCount ) 
        {
            DebugOutput( "Sending list not same count: " + std::to_string( uCount ) );
            return;
        }

        // CRITICAL: libcurl easy handle is NOT thread-safe
        // Cannot call curl_ws_send()/curl_ws_recv() from multiple threads simultaneously
        // Operating the same curl handle from 2 threads may cause crash risk
        std::lock_guard<std::mutex> lock( m_kCurlMutex );
        CURLcode result;
        for ( auto i = 0; i < uCount; ++i )
        {
            if ( !kSendingList[i] ) continue;
            result = send_action_Command(  kMessageList[ i ].dump()  );
            if ( result != CURLcode::CURLE_OK )
            {
                DebugOutput( "Message sending fail, id= " + std::to_string( i ) );
                BOOST_LOG_TRIVIAL(warning) << "Message sending fail, id= " << i;
            }
        }
    } catch (const std::invalid_argument& e) {
        DebugOutput( "Caught std::invalid_argument: " , e.what());
    } catch (const std::exception& e) {
        DebugOutput( "Caught std::exception: " , e.what() );
    }

#if 0
    //Initialconnect();
    size_t sent;
    json payload;
    payload["jsonrpc"] = "2.0";
    payload["method"] = "printer.objects.query";
    payload["params"] = {
        {"objects", {
            {"extruder", {"temperature", "target"}},
            {"temperature_sensor Chamber_sensor", {"temperature"}},
            {"output_pin fan_assist", {"value"}},
            {"fan_generic cooling_fan", nullptr},
            {"fan_generic Chamber_fan", {"speed"}},
            {"heater_bed", {"temperature", "target"}},
            {"gcode_move", {"speed_factor", "homing_origin"}},
            {"homing_origin", nullptr},
            {"display_status", nullptr},
            {"print_stats", nullptr},
            {"pause_resume", nullptr},
            {"error", nullptr},
            {"toolhead", {"position", "status", "homed_axes", "estimated_print_time"}}
        }}
    };
    payload["id"] = PhrozenPrinterID::printer_gcode_script;

    //History
    json payload_history;
    payload_history["jsonrpc"] = "2.0";
    payload_history["method"] = "server.history.list";
    payload_history["id"] = 5656;

    //AMS
    json payload_AMS;
    payload_AMS["jsonrpc"] = "2.0";
    payload_AMS["method"] = "printer.gcode.script";
    payload_AMS["params"]["script"] = "P114";
    payload_AMS["id"] = PhrozenPrinterID::printer_gcode_script;
    
    //Nozzle
    //to check the filament is existing in the nozzle or not
    json payload_Nozzle;
    payload_Nozzle["jsonrpc"] = "2.0";
    payload_Nozzle["method"] = "printer.gcode.script";
    payload_Nozzle["params"]["script"] = "PRZ_ADC";
    payload_Nozzle["id"] = PhrozenPrinterID::printer_gcode_script;

    //LED
    json payload_LED;
    payload_LED["jsonrpc"] = "2.0";
    payload_LED["method"] = "printer.gcode.script";
    payload_LED["params"]["script"] = "P0 LED_GetState";
    payload_LED["id"] = PhrozenPrinterID::printer_gcode_script;

    // Log thread ID for Xcode console debugging
    std::thread::id thread_id = std::this_thread::get_id();
    std::cout << "[GetAllInfo_websocket] Thread started, Thread ID: " << thread_id << std::endl;
    BOOST_LOG_TRIVIAL(info) << "GetAllInfo_websocket: Thread started, Thread ID: " << thread_id;
    
    try {
        auto nowTime = std::chrono::steady_clock::now();
        auto previousTime = std::chrono::steady_clock::now();
        while ( IsStartSending() )
        {
            {
                // CRITICAL: libcurl easy handle is NOT thread-safe
                // Cannot call curl_ws_send()/curl_ws_recv() from multiple threads simultaneously
                // Operating the same curl handle from 2 threads may cause crash risk
                std::lock_guard<std::mutex> lock(m_kCurlMutex);
                BOOST_LOG_TRIVIAL(debug) << "RunSendMessage: Lock acquired, Thread ID: " << thread_id;
                
                CURLcode result = send_action_Command(payload.dump());
                
                nowTime = std::chrono::steady_clock::now();
                long long timeDiff = std::chrono::duration_cast<std::chrono::seconds>(nowTime - previousTime).count();
                /*when re-connect after disconnect need to do one more time*/
                //if (threadControl.first_time_to_send_query) {
                    //no need to do repeat execution
                //   result = send_action_Command(payload_AMS.dump());
                //}
                // TODO: Allow duplicate execution until we implement a better mechanism
                // Temporarily allow repeated execution until better solution is found
                if ((timeDiff > 5 && m_spPrinterInfo->state != "printing") || m_spThreadControl->first_time_to_send_query)
                {
                    result = send_action_Command(payload_AMS.dump());
                    result = send_action_Command(payload_history.dump());
                    result = send_action_Command(payload_Nozzle.dump());
                    result = send_action_Command(payload_LED.dump());
                    m_spThreadControl->first_time_to_send_query = false;
                    previousTime = std::chrono::steady_clock::now();
                }
                BOOST_LOG_TRIVIAL(debug) << "GetAllInfo_websocket: Lock released, Thread ID: " << thread_id;
            }// Lock released from here before sleep to avoid blocking ReceiveResponse() thread
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

    } catch (const std::invalid_argument& e) {
        DebugOutput( "Caught std::invalid_argument: " , e.what());
    } catch (const std::exception& e) {
        DebugOutput( "Caught std::exception: " , e.what() );
    }

#endif
 
}

void PhrozenNetworkAgent::RunReceiveResponse()
{
    // Log thread ID for Xcode console debugging
    std::thread::id thread_id = std::this_thread::get_id();
    std::cout << "[ReceiveResponse] Thread started, Thread ID: " << thread_id << std::endl;
    BOOST_LOG_TRIVIAL(info) << "ReceiveResponse: Thread started, Thread ID: " << thread_id;
    
    char buffer[500000];
    size_t rlen;
    CURLcode res = CURLcode::CURLE_COULDNT_CONNECT;
    
#ifndef __APPLE__
    const struct curl_ws_frame* meta;
#else
    // Note: curl 8.x requires non-const pointer for curl_ws_recv() fifth parameter
    // Changed from: const struct curl_ws_frame* meta;
    // See: https://curl.se/docs/websockets.html - API changed in curl 8.0+
    struct curl_ws_frame* meta;
#endif
    
    // Frame accumulation buffers for handling fragmented messages
    std::string ams_message_buffer;      // Buffer for AMS-related messages
    std::string historyInfo;              // Buffer for history info (existing)
    bool historyStart = false;
    int again = 0;
    
    // Sliding window buffer for cross-frame pattern matching
    // This helps catch patterns that span across frame boundaries
    std::string sliding_window_buffer;
    const size_t MAX_SLIDING_WINDOW_SIZE = 10000;  // Maximum size to prevent memory issues
    
#if 0
    if( !IsFirstTimeToSendQuery() ){

        // CRITICAL: libcurl easy handle is NOT thread-safe
        // Cannot call curl_ws_send()/curl_ws_recv() from multiple threads simultaneously
        // Operating the same curl handle from 2 threads may cause crash risk
        std::lock_guard<std::mutex> lock( m_kCurlMutex );

        BOOST_LOG_TRIVIAL(debug) << "ReceiveResponse: Lock acquired, Thread ID: " << thread_id;

        try {
            double connectTime = 0;
            curl_easy_getinfo( m_pCurlMainWebsocket, CURLINFO_CONNECT_TIME, &connectTime);
            
            if (connectTime > 0) {
                res = curl_ws_recv( m_pCurlMainWebsocket, buffer, sizeof(buffer), &rlen, &meta);
                
                if (res == CURLE_OK) {
                    again = 0;
                    
                    // ============================================
                    // Frame Fragmentation Handling
                    // ============================================
                    std::string frame_data(&buffer[0], &buffer[rlen]);
                    bool is_text_frame = (meta->flags & CURLWS_TEXT) != 0;
                    bool is_binary_frame = (meta->flags & CURLWS_BINARY) != 0;
                    bool is_continuation_frame = PhrozenFrameProcessor::IsContinuationFrame(meta);
                    bool is_final_frame = PhrozenFrameProcessor::IsFinalFrame(meta);
                    
                    // Debug logging for frame information
                    BOOST_LOG_TRIVIAL(debug) << "WebSocket frame received: "
                    << "length=" << rlen
                    << ", flags=" << meta->flags
                    << ", text=" << is_text_frame
                    << ", continuation=" << is_continuation_frame
                    << ", final=" << is_final_frame
                    << ", content_preview=" << frame_data.substr(0, 100);
                    
                    // If this is a continuation frame, accumulate it
                    if (is_continuation_frame) {
                        ams_message_buffer += frame_data;
                        BOOST_LOG_TRIVIAL(debug) << "Accumulating continuation frame. "
                        << "Buffer size: " << ams_message_buffer.size();
                        
                        // Prevent buffer from growing too large
                        if (ams_message_buffer.size() > MAX_SLIDING_WINDOW_SIZE) {
                            BOOST_LOG_TRIVIAL(warning) << "AMS message buffer exceeded max size, truncating";
                            ams_message_buffer = ams_message_buffer.substr(ams_message_buffer.size() - MAX_SLIDING_WINDOW_SIZE / 2);
                        }
                        return;  // To next loop for wait for more frames
                    }
                    
                    // ============================================
                    // Complete Message Combination
                    // ============================================
                    std::string complete_message = PhrozenFrameProcessor::CombineFrames(frame_data, ams_message_buffer);
                    PhrozenFrameProcessor::UpdateSlidingWindow(sliding_window_buffer, complete_message, MAX_SLIDING_WINDOW_SIZE);
                    
                    // ============================================
                    // Message Conversion
                    // ============================================
                    std::string ws = complete_message;
                    
                    // ============================================
                    // Message Type Detection
                    // ============================================
                    bool skip_proc_stat_processing = MessageProcessor::ShouldSkipProcStat(ws);
                    if (skip_proc_stat_processing) {
                        BOOST_LOG_TRIVIAL(debug) << "Found notify_proc_stat_update, skipping proc_stat processing";
                        continue;
                    }
                    
                    // ============================================
                    // G-code Response and Printer Status Processing
                    // ============================================
                    if (!skip_proc_stat_processing) {
                        BOOST_LOG_TRIVIAL(info) << "receive: " << ws << endl;
                        MessageProcessor::ProcessGcodeResponse(ws);
                        PrinterStatusExtractor::ProcessPrinterStatus(ws);
                    }
                    
                    // ============================================
                    // History Info Processing (independent of skip_proc_stat)
                    // ============================================
                    MessageProcessor::ProcessHistoryInfo(ws, historyInfo, historyStart);
                    
                    // ============================================
                    // AMS Processing (independent of skip_proc_stat)
                    // ============================================
                    // Search in both the current complete message and sliding window buffer
                    // This ensures we catch patterns even if they span frame boundaries
                    std::string search_text = ws;
                    if (sliding_window_buffer.size() > ws.size()) {
                        search_text = sliding_window_buffer;
                    }
                    
                    AMSProcessor::ProcessAMSConnectionStatus(search_text, sliding_window_buffer);
                    AMSProcessor::ProcessAMSCommandStates(search_text, sliding_window_buffer);
                    AMSProcessor::ProcessAMSEntryParkState(ws);
                    
                    // ============================================
                    // Pause Message Processing
                    // ============================================
                    MessageProcessor::ProcessPauseMessage(ws);
                }
                else if (res == CURLE_AGAIN) {
                    again++;
                    // Log for macOS Xcode console
                    BOOST_LOG_TRIVIAL(debug) << "ReceiveResponse: CURLE_AGAIN, Thread ID: " << thread_id << ", again count: " << again;
                    
                    if (again > 30) {
                        // Log for macOS Xcode console (log before resetting again)
                        BOOST_LOG_TRIVIAL(warning) << "Too many CURLE_AGAIN (count: " << again << ")";
                        again = 0;
                    }
                }
                else if (res == CURLE_RECV_ERROR) {
                    std::terminate();
                    BOOST_LOG_TRIVIAL(info) << "receive error: " << endl;
                    curl_easy_cleanup(m_pCurlMainWebsocket);
                    auto strIp = m_spWebServiceInfo->ip;
                    InitializeConnectorImp( strIp );
                }
            }
            else {
                BOOST_LOG_TRIVIAL(info) << "connect failed " << endl;
                SetStartReceiving(false);
                SetStartSending(false);
            }
        }
        catch (const std::runtime_error& e) {
            DebugOutput("Error: ", e.what());
        }
        catch (const std::invalid_argument& e) {
            DebugOutput("Caught std::invalid_argument: ", e.what());
        }
        catch (const std::exception& e) {
            DebugOutput("Caught std::exception: ", e.what());
        }
        BOOST_LOG_TRIVIAL(debug) << "ReceiveResponse: Lock released, Thread ID: " << thread_id;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    m_pPrinterInfo->state = "offline";
    return res;
#endif

}

// Get camera stream URL
bool PhrozenNetworkAgent::get_camera_stream_url(std::string dev_ip, std::string* url)
{
    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Getting camera stream URL for " << dev_ip;

    //if (!m_is_connected || m_connected_dev_ip != dev_ip) {
    //    BOOST_LOG_TRIVIAL(error) << "PhrozenNetworkAgent: Not connected to device " << dev_ip;
    //    return -1;
    //}

    // Implementation needed: Get camera stream URL
    std::string port_device = ":8808";
   *url = "http://" + dev_ip + port_device + "/webcam/?action=snapshot";
    return true;
}

size_t WebcamWriteStreamCallback(void* contents, size_t size, size_t nmemb, void* userp) 
{
    size_t total_size = size * nmemb;
    std::vector<unsigned char>* buffer = (std::vector<unsigned char> *)userp;
    buffer->insert(buffer->end(), (unsigned char*)contents, (unsigned char*)contents + total_size);
    return total_size;
}

// Get camera snapshot (//LiveStreamWithMultiThread)
CURLcode PhrozenNetworkAgent::get_camera_snapshot(std::string dev_ip, std::vector<unsigned char>& image_data)
{
    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Getting camera snapshot for " << dev_ip;

    //if (!m_is_connected || m_connected_dev_ip != dev_ip) {
    //    BOOST_LOG_TRIVIAL(error) << "PhrozenNetworkAgent: Not connected to device " << dev_ip;
    //    return CURLcode::CURLE_FAILED_INIT;
    //}

    std::string str_snapshot_url;
    if ( !get_camera_stream_url( dev_ip, &str_snapshot_url ) )
    {
        return CURLcode::CURLE_FAILED_INIT;
    }

    auto lastCaptureTime = std::chrono::steady_clock::now();
    float fRecordInterval = 8.f;
    CURLcode res = CURLE_FAILED_INIT;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCaptureTime).count();
    while ( elapsed < fRecordInterval )
    {
        now = std::chrono::steady_clock::now();
        elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCaptureTime).count();
    }


    CURL* curl = curl_easy_init();
    if (!curl) {
        BOOST_LOG_TRIVIAL( error ) << "CURL initialization failed!";
        return res;
    }

    auto fn_snapshop_write_stream_callback = [&](void* contents, size_t size, size_t nmemb, void* userp) -> size_t
    {
        size_t total_size = size * nmemb;
        std::vector<unsigned char>* buffer = (std::vector<unsigned char> *)userp;
        buffer->insert(buffer->end(), (unsigned char*)contents, (unsigned char*)contents + total_size);
        return total_size;
    };

    std::vector<unsigned char> kTempWebCamImageData;
    curl_easy_setopt(curl, CURLOPT_URL, str_snapshot_url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WebcamWriteStreamCallback); //m_fn_snapshop_write_stream_callback
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &image_data);//image_data
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);

    res = curl_easy_perform(curl);
    if ( res != CURLE_OK ) {
        BOOST_LOG_TRIVIAL(error) << "get snapshop image fail. snapshop url= " << str_snapshot_url;
    }
    curl_easy_cleanup(curl);
    return res;
}

CURLcode PhrozenNetworkAgent::send_action_Command( std::string send_payload )
{
    //CURLcode res = curl_easy_perform(curl);
    CURLcode result = CURLE_AGAIN;
    double connectTime = 0;
    try {
        curl_easy_getinfo(m_pCurlMainWebsocket, CURLINFO_CONNECT_TIME, &connectTime);
        if (connectTime > 0)
        {
            size_t sent;
            result = curl_ws_send(m_pCurlMainWebsocket, send_payload.c_str(), strlen(send_payload.c_str()), &sent, 0, CURLWS_TEXT);
        }
        //curl_easy_cleanup(curl);
    }
    catch (const std::exception& e) {
        DebugOutput( "send error: " , e.what());
    }
    return result;
}

void PhrozenNetworkAgent::DebugOutput(const std::string& prefix, const char* message  ) {
    std::string combined = prefix + message;
#ifdef _WIN32
    OutputDebugStringA(combined.c_str());
#else
    // to do
    // need to modify/create a version for macOS to achieve the same effect as Windows.
    std::cout << combined << std::endl;
#endif
}

#pragma region PhrozenMessageProcessor

#if 0
// Message processing module for message type detection and conversion
bool PhrozenNetworkAgent::ShouldSkipProcStat(const std::string& message) 
{
    std::string skip_message = "{\"jsonrpc\": \"2.0\", \"method\": \"notify_proc_stat_update\"";
    return message.find(skip_message.c_str()) != std::string::npos;
}
   
void PhrozenNetworkAgent::ProcessGcodeResponse(const std::string& message, Slic3r::PhrozenPrinterInfo& kInfo ) 
{
    std::string find_message = "{\"jsonrpc\": \"2.0\", \"method\": \"notify_gcode_response\"";
    size_t pos = message.find(find_message.c_str());
    if (pos == std::string::npos) return;
    
    if (json::accept(message)) {
        try {
            json msg_json = json::parse(message);
            if (!msg_json["params"].is_null()) {
                std::string params = msg_json["params"][0].get<std::string>();
                
                // Check for Unhandled exception
                size_t pos = params.find("Unhandled exception during run");
                if (pos != std::string::npos) {
                    m_pPrinterInfo->error = msg_json["params"][0].get<std::string>();
                }

                // Check LED State
                std::string strLEDKeyword = "P0 LED_State=";
                auto uLEDPos = params.find(strLEDKeyword);
                if ( uLEDPos != std::string::npos )
                {
                    std::string strLEDValue = params.substr(uLEDPos + strLEDKeyword.length(), 1);
                    kInfo.bIsLedOn = (bool)std::stoi(strLEDValue);
                    return;
                }
                
                // Check for PRZ_ADC response with fila_exist
                if (params.find("PRZ_ADC:") != std::string::npos && params.find("fila_exist") != std::string::npos) {
                    if (params.find("fila_exist:True") != std::string::npos ||
                        params.find("fila_exist:true") != std::string::npos) {
                        kInfo.bIsNozzleDetectFilament = true;
                    } else {
                        kInfo.bIsNozzleDetectFilament = false;
                    }
                    BOOST_LOG_TRIVIAL(info) << "*** PRZ_ADC response: fila_exist = " << (kInfo.bIsNozzleDetectFilament ? "true" : "false") << " ***";
                }
                
                // ============================================
                // Calibration message processing
                // ============================================
                {
                    // Auto-leveling (Calibration) messages
                    if (params.find("Probe samples exceed samples_tolerance") != std::string::npos) {
                        std::lock_guard<std::mutex> lock(m_kCalibrationProgressMutex);
                        m_calibrationProgressInfo.calibrationStatus = CalibrationState::HAS_ERROR;
                        BOOST_LOG_TRIVIAL(warning) << "Calibration error: Probe samples exceed tolerance";
                    } else if (params.find("Mesh Bed Leveling Complete") != std::string::npos) {
                        std::lock_guard<std::mutex> lock(m_kCalibrationProgressMutex);
                        if (m_calibrationProgressInfo.calibrationStatus == CalibrationState::RUNNING) {
                            m_calibrationProgressInfo.calibrationStatus = CalibrationState::COMPLETED;
                            m_calibrationProgressInfo.calibrationProgress = 100.0f;
                            BOOST_LOG_TRIVIAL(info) << "Calibration completed";
                        }
                    } else if (params.find("probe at ") != std::string::npos) {
                        std::lock_guard<std::mutex> lock(m_kCalibrationProgressMutex);
                        CalibrationProgressCalculator::UpdateCalibrationProgress(params, m_calibrationProgressInfo);
                    }
                    
                    // Resonance compensation messages
                    if (params.find("// Testing axis x") != std::string::npos ||
                        params.find("// Testing axis y") != std::string::npos ||
                        params.find("// Testing frequency") != std::string::npos) {
                        std::lock_guard<std::mutex> lock(m_kCalibrationProgressMutex);
                        CalibrationProgressCalculator::UpdateResonanceCompensationProgress(params, m_calibrationProgressInfo);
                    } else if (params.find("with these parameters and restart the printer.") != std::string::npos) {
                        std::lock_guard<std::mutex> lock(m_kCalibrationProgressMutex);
                        if (m_calibrationProgressInfo.resonanceCompensationStatus == CalibrationState::RUNNING) {
                            m_calibrationProgressInfo.resonanceCompensationStatus = CalibrationState::COMPLETED;
                            m_calibrationProgressInfo.resonanceCompensationProgress = 100.0f;
                            BOOST_LOG_TRIVIAL(info) << "Resonance compensation completed";
                        }
                    }
                    
                    // Temperature calibration messages
                    if (params.find("T0:") != std::string::npos && params.find("/") != std::string::npos) {
                        std::lock_guard<std::mutex> lock(m_kCalibrationProgressMutex);
                        CalibrationProgressCalculator::UpdateTemperatureCalibrationProgress(params, m_calibrationProgressInfo);
                    } else if (params.find("Klippy Disconnected") != std::string::npos) {
                        std::lock_guard<std::mutex> lock(m_kCalibrationProgressMutex);
                        if (m_calibrationProgressInfo.temperatureCalibrationStatus == CalibrationState::RUNNING) {
                            m_calibrationProgressInfo.temperatureCalibrationStatus = CalibrationState::COMPLETED;
                            m_calibrationProgressInfo.temperatureCalibrationProgress = 100.0f;
                            BOOST_LOG_TRIVIAL(info) << "Temperature calibration completed";
                        }
                    }
                    
                    // Generic completion message
                    if (params.find("Klipper state: Disconnect") != std::string::npos) {
                        std::lock_guard<std::mutex> lock(m_kCalibrationProgressMutex);
                        if (m_calibrationProgressInfo.calibrationStatus == CalibrationState::RUNNING) {
                            m_calibrationProgressInfo.calibrationStatus = CalibrationState::COMPLETED;
                            m_calibrationProgressInfo.calibrationProgress = 100.0f;
                        }
                        if (m_calibrationProgressInfo.resonanceCompensationStatus == CalibrationState::RUNNING) {
                            m_calibrationProgressInfo.resonanceCompensationStatus = CalibrationState::COMPLETED;
                            m_calibrationProgressInfo.resonanceCompensationProgress = 100.0f;
                        }
                        if (m_calibrationProgressInfo.temperatureCalibrationStatus == CalibrationState::RUNNING) {
                            m_calibrationProgressInfo.temperatureCalibrationStatus = CalibrationState::COMPLETED;
                            m_calibrationProgressInfo.temperatureCalibrationProgress = 100.0f;
                        }
                    }
                }
            }
        } catch (const json::exception& e) {
            BOOST_LOG_TRIVIAL(warning) << "JSON parse error in gcode_response: " << e.what();
        }
    }
}
    
void PhrozenNetworkAgent::ProcessHistoryInfo(const std::string& message,
                                             std::string& historyBuffer,
                                             bool& historyStart) 
{
    std::string jobs_history = "\"jobs\":";
    std::string id_history = "\"id\": 5656";
    size_t pos_id_history = message.find(id_history.c_str());
    size_t pos_history = message.find(jobs_history.c_str());
    
    if (pos_history != std::string::npos && !historyStart) {
        historyBuffer = "";
        historyStart = true;
    }
    if (historyStart) {
        historyBuffer += message;
    }
    if (pos_id_history != std::string::npos) {
        historyStart = false;
    }
    if (!historyBuffer.empty() && !historyStart) {
        std::vector<HistoryInfo> _historyInfoList;
        try {
            json history_json;
            if (json::accept(historyBuffer)) {
                history_json = json::parse(historyBuffer);
                if (history_json["result"]["jobs"].is_array()) {
                    for (const auto& job : history_json["result"]["jobs"]) {
                        HistoryInfo _historyInfo;
                        std::string X = job["filename"].get<std::string>();
                        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
                        _historyInfo.gcode_name = converter.from_bytes(X);
                        _historyInfo.status = job["status"].get<std::string>();
                        _historyInfo.fliament_used = job["filament_used"];
                        _historyInfo.total_duration = job["total_duration"];
                        _historyInfoList.push_back(_historyInfo);
                    }
                    m_kHistoryInfoList = _historyInfoList;
                } else {
                    m_kHistoryInfoList.clear();
                    DebugOutput("Invalid JSON format or missing 'jobs' array.");
                }
            }
        } catch (const std::exception& e) {
            DebugOutput("Parse error: ", e.what());
        }
    }
}
    
void PhrozenNetworkAgent::ProcessPauseMessage(const std::string& message) 
{
    std::string pause_prefix = "+PAUSE:";
    size_t pause_pos = message.find(pause_prefix);
    if (pause_pos == std::string::npos) return;
    
    try {
        std::tuple<std::string, std::string, std::string> pauseError = ParsePauseMessage(message);
        std::string code = std::get<0>(pauseError);
        std::string oldCh = std::get<1>(pauseError);
        std::string newCh = std::get<2>(pauseError);
        
        std::cout << "Code: " << code << std::endl;
        std::cout << "Old Channel: " << oldCh << std::endl;
        std::cout << "New Channel: " << newCh << std::endl;
        
        HandlePauseCode(code);
        
        if (code == "4") {
            m_kMonitorWindow.AMSselectedID = std::stoi(newCh);
            m_kMonitorWindow.AMS_ID = "\xC2\xA0" + std::to_string(m_kMonitorWindow.AMSselectedID) + "\xC2\xA0";
        } else if (code == "8") {
            m_kMonitorWindow.AMSselectedID = std::stoi(oldCh);
            m_kMonitorWindow.AMS_ID = "\xC2\xA0" + std::to_string(m_kMonitorWindow.AMSselectedID) + "\xC2\xA0";
        }
        
        m_kMonitorWindow.error_code = "[" + code + "]";
        
        //only for test
        if (!m_kMonitorWindow.amsReturnError.empty()) {
            m_kMonitorWindow.amsReturnError.clear();
        }
    } catch (const std::invalid_argument& e) {
        DebugOutput("Error (input1): ", e.what());
    }
}
#endif
#pragma endregion

















// ========== in below, just for reference ============== //

// Get connected printer ID
std::string PhrozenNetworkAgent::get_connected_printer_id()
{
    std::lock_guard<std::mutex> lock(m_connection_mutex);
    return m_connected_dev_ip;
}

// Get connected printer IP
std::string PhrozenNetworkAgent::get_connected_printer_ip()
{
    std::lock_guard<std::mutex> lock(m_connection_mutex);
    return m_connected_dev_ip;
}

// Set timeout
void PhrozenNetworkAgent::set_timeout(int timeout_ms)
{
    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Setting timeout to " << timeout_ms << "ms";
    m_timeout_ms = timeout_ms;
}

// Get timeout
int PhrozenNetworkAgent::get_timeout() const
{
    return m_timeout_ms;
}

// Initialize CURL
CURLcode PhrozenNetworkAgent::initialize_curl()
{
    std::lock_guard<std::mutex> lock(m_curl_mutex);

    if (m_curl_handle == nullptr) {
        m_curl_handle = curl_easy_init();
        if (!m_curl_handle) {
            BOOST_LOG_TRIVIAL(error) << "PhrozenNetworkAgent: Failed to initialize CURL handle";
            return CURLE_FAILED_INIT;
        }

        // Set default CURL options
        curl_easy_setopt(m_curl_handle, CURLOPT_TIMEOUT_MS, m_timeout_ms);
        curl_easy_setopt(m_curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(m_curl_handle, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(m_curl_handle, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    return CURLE_OK;
}

// Cleanup CURL
void PhrozenNetworkAgent::cleanup_curl()
{
    std::lock_guard<std::mutex> lock(m_curl_mutex);

    if (m_curl_handle) {
        curl_easy_cleanup(m_curl_handle);
        m_curl_handle = nullptr;
    }

    if (m_websocket_handle) {
        curl_easy_cleanup(m_websocket_handle);
        m_websocket_handle = nullptr;
    }
}

// Perform HTTP request
CURLcode PhrozenNetworkAgent::perform_http_request(const std::string& url, const std::string& method,
                                                     const std::string& data, std::string* response)
{
    std::lock_guard<std::mutex> lock(m_curl_mutex);

    if (!m_curl_handle) {
        BOOST_LOG_TRIVIAL(error) << "PhrozenNetworkAgent: CURL not initialized";
        return CURLE_FAILED_INIT;
    }

    // Set URL
    curl_easy_setopt(m_curl_handle, CURLOPT_URL, url.c_str());

    // Set method
    if (method == "POST") {
        curl_easy_setopt(m_curl_handle, CURLOPT_POST, 1L);
        curl_easy_setopt(m_curl_handle, CURLOPT_POSTFIELDS, data.c_str());
    } else if (method == "GET") {
        curl_easy_setopt(m_curl_handle, CURLOPT_HTTPGET, 1L);
    }

    // Set write callback
    curl_easy_setopt(m_curl_handle, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(m_curl_handle, CURLOPT_WRITEDATA, response);

    // Perform request
    CURLcode res = curl_easy_perform(m_curl_handle);

    if (res != CURLE_OK) {
        BOOST_LOG_TRIVIAL(error) << "PhrozenNetworkAgent: HTTP request failed: " << get_error_string(res);
    }

    return res;
}

// Perform WebSocket request
CURLcode PhrozenNetworkAgent::perform_websocket_request(const std::string& url, const std::string& message)
{
    // Implementation needed: WebSocket communication
    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: WebSocket request to " << url;
    return CURLE_OK;
}

// CURL write callback
size_t PhrozenNetworkAgent::write_callback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t total_size = size * nmemb;
    std::string* response = static_cast<std::string*>(userp);

    if (response) {
        response->append(static_cast<char*>(contents), total_size);
    }

    return total_size;
}

// CURL header callback
size_t PhrozenNetworkAgent::header_callback(char* buffer, size_t size, size_t nitems, void* userdata)
{
    size_t total_size = size * nitems;
    // Process headers if needed
    return total_size;
}

// CURL progress callback
int PhrozenNetworkAgent::progress_callback(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                                           curl_off_t ultotal, curl_off_t ulnow)
{
    PhrozenNetworkAgent* agent = static_cast<PhrozenNetworkAgent*>(clientp);

    if (agent && agent->m_on_progress_callback) {
        float progress = 0.0f;
        if (ultotal > 0) {
            progress = static_cast<float>(ulnow) / static_cast<float>(ultotal) * 100.0f;
        } else if (dltotal > 0) {
            progress = static_cast<float>(dlnow) / static_cast<float>(dltotal) * 100.0f;
        }
        agent->m_on_progress_callback(progress);
    }

    return 0;
}

// Handle error
void PhrozenNetworkAgent::handle_error(int error_code, const std::string& error_message)
{
    BOOST_LOG_TRIVIAL(error) << "PhrozenNetworkAgent Error [" << error_code << "]: " << error_message;

    if (m_on_error_callback) {
        m_on_error_callback(error_code, error_message);
    }
}

// Get error string from CURL code
std::string PhrozenNetworkAgent::get_error_string(CURLcode code)
{
    return std::string(curl_easy_strerror(code));
}


#pragma endregion
