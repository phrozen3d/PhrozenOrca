#ifndef slic3r_PhrozenNetworkAgent_hpp_
#define slic3r_PhrozenNetworkAgent_hpp_

#include <curl/curl.h>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include <map>
#include "slic3r/Utils/json_diff.hpp"

namespace Slic3r {

// Forward declarations
class AppConfig;

// Callback function types for Phrozen network communication
typedef std::function<void(std::string)> OnMessageCallback;
typedef std::function<void(bool)> OnConnectionCallback;
typedef std::function<void(int, std::string)> OnErrorCallback;
typedef std::function<void(float)> OnProgressCallback;
typedef std::function<size_t(void*, size_t, size_t, void*)> OnSnapshotWriteStreamCallback;

class PhrozenNetworkAgent
{
public:
    PhrozenNetworkAgent(std::string log_dir = "");
    ~PhrozenNetworkAgent();

    // Initialization and configuration
    int init_log();
    int set_config_dir(std::string config_dir);
    int start();

    // Connection management
    int connect_printer( std::string dev_ip);
    int disconnect_printer();
    bool is_connected();

    // Callback setters
    void set_on_message_callback(OnMessageCallback callback);
    void set_on_connection_callback(OnConnectionCallback callback);
    void set_on_error_callback(OnErrorCallback callback);
    void set_on_progress_callback(OnProgressCallback callback);

    // Message sending
    int send_message(std::string dev_id, std::string message);
    int send_gcode_command(std::string dev_id, std::string gcode);

    // File operations
    int send_file(std::string dev_id, std::string file_path, OnProgressCallback progress_fn = nullptr);
    int download_file(std::string dev_id, std::string remote_path, std::string local_path);

    // Printer information queries
    int get_printer_info(std::string dev_id, std::string* info_json);
    int get_printer_status(std::string dev_id, std::string* status_json);

    // Camera operations
    int get_camera_stream_url(std::string dev_ip, std::string* url);
    CURLcode get_camera_snapshot(std::string dev_ip, std::vector<unsigned char>& image_data);

    // Utility methods
    std::string get_connected_printer_id();
    std::string get_connected_printer_ip();
    void set_timeout(int timeout_ms);
    int get_timeout() const;

private:
    // Internal state
    std::string m_log_dir;
    std::string m_config_dir;
    std::string m_connected_dev_ip;
    bool m_is_connected;
    int m_timeout_ms;

    // CURL handles
    CURL* m_curl_handle;
    CURL* m_websocket_handle;

    // Callbacks
    OnMessageCallback m_on_message_callback;
    OnConnectionCallback m_on_connection_callback;
    OnErrorCallback m_on_error_callback;
    OnProgressCallback m_on_progress_callback;
    OnSnapshotWriteStreamCallback m_fn_snapshop_write_stream_callback;

    // Thread safety
    std::mutex m_connection_mutex;
    std::mutex m_message_mutex;
    std::mutex m_curl_mutex;

    // Internal helper methods
    CURLcode initialize_curl();
    void cleanup_curl();
    CURLcode perform_http_request(const std::string& url, const std::string& method,
                                   const std::string& data, std::string* response);
    CURLcode perform_websocket_request(const std::string& url, const std::string& message);

    // CURL callbacks
    static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp);
    static size_t header_callback(char* buffer, size_t size, size_t nitems, void* userdata);
    static int progress_callback(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                                 curl_off_t ultotal, curl_off_t ulnow);

    // Error handling
    void handle_error(int error_code, const std::string& error_message);
    std::string get_error_string(CURLcode code);
};

} // namespace Slic3r

#endif // slic3r_PhrozenNetworkAgent_hpp_
