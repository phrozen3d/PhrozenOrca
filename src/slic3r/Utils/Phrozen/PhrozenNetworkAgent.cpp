#include "PhrozenNetworkAgent.hpp"
#include "libslic3r/AppConfig.hpp"
#include <boost/log/trivial.hpp>
#include "PhrozenMachineDatas.hpp"
#include <sstream>

namespace Slic3r {

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
    m_spThreadControl->first_time_to_send_query = true;

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
        if ( bSuccess ){  m_strIp = strIp; }
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
            
            if(m_spWebServiceInfo->ip == m_spPrinterInfo->pre_printerIP){
                m_spPrinterInfo->isSameIP = true;
            }
            else{
                m_spPrinterInfo->isSameIP = false;
                m_spPrinterInfo->pre_printerIP = m_spWebServiceInfo->ip;
            }

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
    m_strIp = "";
}

void PhrozenNetworkAgent::SetStartSending( bool bStart )
{
    if ( bStart ) { m_bStartSending.store(true, std::memory_order_relaxed); }
    else          { m_bStartSending.store(false, std::memory_order_relaxed); }
}

bool PhrozenNetworkAgent::IsStartSending()
{
    return m_bStartSending.load(std::memory_order_relaxed);
}

void PhrozenNetworkAgent::SetStartReceiving( bool bStart )
{
    if ( bStart ) { m_bStartReceiving.store(true, std::memory_order_relaxed); }
    else          { m_bStartReceiving.store(false, std::memory_order_relaxed); }
}

bool PhrozenNetworkAgent::IsStartReceiving()
{
    return m_bStartReceiving.load(std::memory_order_relaxed);
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


void PhrozenNetworkAgent::RunSendMessage( const std::vector< json >& kMessageList )
{
#if 0
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
                if ((timeDiff > 5 && m_pPrinterInfo->state != "printing") || threadControl.first_time_to_send_query)
                {
                    result = send_action_Command(payload_AMS.dump());
                    result = send_action_Command(payload_history.dump());
                    result = send_action_Command(payload_Nozzle.dump());
                    result = send_action_Command(payload_LED.dump());
                    threadControl.first_time_to_send_query = false;
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

} // namespace Slic3r
