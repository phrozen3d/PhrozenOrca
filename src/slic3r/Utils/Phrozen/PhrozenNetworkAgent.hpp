#ifndef slic3r_PhrozenNetworkAgent_hpp_
#define slic3r_PhrozenNetworkAgent_hpp_

#include <curl/curl.h>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <atomic>
#include "slic3r/Utils/json_diff.hpp"


namespace Slic3r {

// Forward declarations
class AppConfig;
class PhrozenWebServiceInfo;
class PhrozenPrinterInfo;
class PhrozenThreadControl;
class PhrozenMonitorWindow;

#pragma region WorkerFuncSafe
class WorkerFuncSafe {
public:
    explicit WorkerFuncSafe(std::function<void()> runFunc,
                            std::chrono::milliseconds loopSleep = std::chrono::milliseconds{1})
        : m_runFunc(std::move(runFunc)),
          m_loopSleep(loopSleep),
          m_running(false),
          m_stopping(false) {}

    bool Process( bool bNonBlockingIfStopping = false ) {
        std::unique_lock<std::mutex> lk(m_mutex);

        if (m_thread && m_running.load(std::memory_order_acquire)) return false; 

        if (m_thread && m_stopping.load(std::memory_order_acquire)) {
            if (bNonBlockingIfStopping) return false; // if not totally stop, return directly

            // otherwise, wait until totally stop, then reprocess.
            m_cv.wait(lk, [this] { return !m_thread; });
        }

        // thread totally stop, start work
        m_running.store(true, std::memory_order_release);
        m_stopping.store(false, std::memory_order_release);

        // create new thread
        m_thread = std::make_unique<std::thread>([this]{

            while (m_running.load(std::memory_order_acquire)) {
                try {
                    m_runFunc();
                } catch (const std::exception& ex) {
                    // prevent exception let thread silent dead
                    std::cerr << "Worker runFunc() exception: " << ex.what() << std::endl;
                } catch (...) {
                    std::cerr << "Worker runFunc() unknown exception" << std::endl;
                }

                // Cut costs according to demand and avoid a vicious cycle.
                if (m_loopSleep.count() > 0) {
                    std::this_thread::sleep_for(m_loopSleep);
                }
            }

            // end loop, start do clear( stop() ), do not do other thing here.
        });

        return true;
    }

    // Cooperative stopping: Notify the loop to end, join without holding locks, set to nullptr
    void Stop() {
        std::unique_ptr<std::thread> localThread;

        {
            std::lock_guard<std::mutex> lk(m_mutex);

            if (!m_thread) {
                m_running.store(false, std::memory_order_release);
                m_stopping.store(false, std::memory_order_release);
                return;
            }

            // set stop flag to cut while loop
            m_running.store(false, std::memory_order_release);
            m_stopping.store(true, std::memory_order_release);

            // Move the thread out of the scope of the variable to reduce the lock range 
            // (to avoid deadlock caused by holding locks during joins).
            localThread = std::move(m_thread); // m_thread change to nullptr
        }

        // Join without holding a lock to avoid blocking other calls (such as processes).
        if (localThread && localThread->joinable()) {
            localThread->join();
        }

        // Cleanup complete; reset the stopping flag to notify potentially waiting processes()
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_stopping.store(false, std::memory_order_release);
        }
        m_cv.notify_all();
    }

    ~WorkerFuncSafe() {
        Stop(); // Ensure resource recycling
    }

private:
    std::function<void()>         m_runFunc;
    std::chrono::milliseconds     m_loopSleep;

    std::unique_ptr<std::thread>  m_thread;
    std::atomic<bool>             m_running;  // While loop continues
    std::atomic<bool>             m_stopping; // Is stop() cleaning up

    std::mutex                    m_mutex;
    std::condition_variable       m_cv;
};
#pragma endregion

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

    bool InitializeConnector( const std::string& strIp  );
    void CleanupWebSocketConnection();

    void SetStartSending( bool bStart );
    bool IsStartSending();

    void SetStartReceiving( bool bStart );
    bool IsStartReceiving();


    // ==== function to interactive with machine === //
    void RunSendMessage( const std::vector< json >& kMessageList );

    bool get_camera_stream_url(std::string dev_ip, std::string* url);
    CURLcode get_camera_snapshot(std::string dev_ip, std::vector<unsigned char>& image_data);




    //====== below just for reference ========== //
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

    // Utility methods
    std::string get_connected_printer_id();
    std::string get_connected_printer_ip();
    void set_timeout(int timeout_ms);
    int get_timeout() const;

private:
    
    bool InitializeConnectorImp( const std::string& strIp );


    std::string m_strIp;
    CURL* m_pCurlMainWebsocket = nullptr;
    std::mutex m_kCurlMutex;

    std::unique_ptr< PhrozenWebServiceInfo > m_spWebServiceInfo{ nullptr };
    std::unique_ptr< PhrozenPrinterInfo > m_spPrinterInfo{ nullptr };
    std::unique_ptr< PhrozenThreadControl > m_spThreadControl{ nullptr };
    std::unique_ptr< PhrozenMonitorWindow > m_spMonitorWindow{ nullptr };
    
    std::atomic<bool> m_bStartSending{false};
    std::atomic<bool> m_bStartReceiving{false};

     //====== below just for reference ========== //
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
