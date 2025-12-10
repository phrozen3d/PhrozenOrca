#include "PhrozenNetworkAgent.hpp"
#include "libslic3r/AppConfig.hpp"
#include <boost/log/trivial.hpp>
#include <sstream>

namespace Slic3r {

// Constructor
PhrozenNetworkAgent::PhrozenNetworkAgent(std::string log_dir)
    : m_log_dir(log_dir)
    , m_config_dir("")
    , m_connected_dev_id("")
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
}

// Destructor
PhrozenNetworkAgent::~PhrozenNetworkAgent()
{
    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Destructor called";
    disconnect_printer();
    cleanup_curl();
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
int PhrozenNetworkAgent::connect_printer(std::string dev_id, std::string dev_ip)
{
    std::lock_guard<std::mutex> lock(m_connection_mutex);

    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Connecting to printer - ID: " << dev_id << ", IP: " << dev_ip;

    if (m_is_connected) {
        BOOST_LOG_TRIVIAL(warning) << "PhrozenNetworkAgent: Already connected to a printer, disconnecting first";
        disconnect_printer();
    }

    m_connected_dev_id = dev_id;
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

    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Disconnecting from printer: " << m_connected_dev_id;

    m_connected_dev_id.clear();
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
int PhrozenNetworkAgent::send_message(std::string dev_id, std::string message)
{
    std::lock_guard<std::mutex> lock(m_message_mutex);

    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Sending message to " << dev_id << ": " << message;

    if (!m_is_connected || m_connected_dev_id != dev_id) {
        BOOST_LOG_TRIVIAL(error) << "PhrozenNetworkAgent: Not connected to device " << dev_id;
        return -1;
    }

    // Implementation needed: Send message to printer
    // This would typically use HTTP or WebSocket communication

    return 0;
}

// Send GCode command
int PhrozenNetworkAgent::send_gcode_command(std::string dev_id, std::string gcode)
{
    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Sending GCode to " << dev_id << ": " << gcode;

    if (!m_is_connected || m_connected_dev_id != dev_id) {
        BOOST_LOG_TRIVIAL(error) << "PhrozenNetworkAgent: Not connected to device " << dev_id;
        return -1;
    }

    // Implementation needed: Send GCode command

    return 0;
}

// Send file
int PhrozenNetworkAgent::send_file(std::string dev_id, std::string file_path, OnProgressCallback progress_fn)
{
    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Sending file to " << dev_id << ": " << file_path;

    if (!m_is_connected || m_connected_dev_id != dev_id) {
        BOOST_LOG_TRIVIAL(error) << "PhrozenNetworkAgent: Not connected to device " << dev_id;
        return -1;
    }

    // Implementation needed: File upload logic

    return 0;
}

// Download file
int PhrozenNetworkAgent::download_file(std::string dev_id, std::string remote_path, std::string local_path)
{
    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Downloading file from " << dev_id
                            << " - Remote: " << remote_path << ", Local: " << local_path;

    if (!m_is_connected || m_connected_dev_id != dev_id) {
        BOOST_LOG_TRIVIAL(error) << "PhrozenNetworkAgent: Not connected to device " << dev_id;
        return -1;
    }

    // Implementation needed: File download logic

    return 0;
}

// Get printer info
int PhrozenNetworkAgent::get_printer_info(std::string dev_id, std::string* info_json)
{
    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Getting printer info for " << dev_id;

    if (!m_is_connected || m_connected_dev_id != dev_id) {
        BOOST_LOG_TRIVIAL(error) << "PhrozenNetworkAgent: Not connected to device " << dev_id;
        return -1;
    }

    // Implementation needed: Query printer information

    return 0;
}

// Get printer status
int PhrozenNetworkAgent::get_printer_status(std::string dev_id, std::string* status_json)
{
    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Getting printer status for " << dev_id;

    if (!m_is_connected || m_connected_dev_id != dev_id) {
        BOOST_LOG_TRIVIAL(error) << "PhrozenNetworkAgent: Not connected to device " << dev_id;
        return -1;
    }

    // Implementation needed: Query printer status

    return 0;
}

// Get camera stream URL
int PhrozenNetworkAgent::get_camera_stream_url(std::string dev_id, std::string* url)
{
    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Getting camera stream URL for " << dev_id;

    if (!m_is_connected || m_connected_dev_id != dev_id) {
        BOOST_LOG_TRIVIAL(error) << "PhrozenNetworkAgent: Not connected to device " << dev_id;
        return -1;
    }

    // Implementation needed: Get camera stream URL

    return 0;
}

// Get camera snapshot
int PhrozenNetworkAgent::get_camera_snapshot(std::string dev_id, std::vector<unsigned char>& image_data)
{
    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Getting camera snapshot for " << dev_id;

    if (!m_is_connected || m_connected_dev_id != dev_id) {
        BOOST_LOG_TRIVIAL(error) << "PhrozenNetworkAgent: Not connected to device " << dev_id;
        return -1;
    }

    // Implementation needed: Get camera snapshot

    return 0;
}

// Get connected printer ID
std::string PhrozenNetworkAgent::get_connected_printer_id()
{
    std::lock_guard<std::mutex> lock(m_connection_mutex);
    return m_connected_dev_id;
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
