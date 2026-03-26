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
#include "PhrozenMachineDatas.hpp"


namespace Slic3r {

// Forward declarations
class AppConfig;
class PhrozenWebServiceInfo;
class PhrozenPrinterInfo;
class PhrozenThreadControl;
class PhrozenMonitorWindow;
class PhrozenCalibrationProgressInfo;
class PhrozenHistoryInfo;
class PhrozenAMSPatterns;
class PhrozenAMSInfo;

template <typename T>
class DoubleBufferSP;


#pragma region WorkerFuncSafe
class WorkerFuncSafe {
public:
    explicit WorkerFuncSafe(std::function<void()> runFunc,
                            std::chrono::milliseconds loopSleep = std::chrono::milliseconds{1})
        : m_runFunc(std::move(runFunc)),
          m_loopSleep(loopSleep),
          m_running(false),
          m_stopping(false),
          m_processing(false){}

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
            m_processing.store(true, std::memory_order_release);

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
            m_processing.store(false, std::memory_order_release);
            // end loop, start do clear( stop() ), do not do other thing here.
        });

        return true;
    }

    // Cooperative stopping: Notify the loop to end, join without holding locks, set to nullptr
    void Stop() {
        if ( m_stopping.load(std::memory_order_acquire) ) return;

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
        if ( localThread && localThread->joinable() && m_processing.load(std::memory_order_acquire)  ) {
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
        if ( m_thread ) 
        {
            Stop(); // Ensure resource recycling
        }
    }

private:
    std::function<void()>         m_runFunc;
    std::chrono::milliseconds     m_loopSleep;

    std::unique_ptr<std::thread>  m_thread;
    std::atomic<bool>             m_running;  // While loop continues
    std::atomic<bool>             m_stopping; // Is stop() cleaning up
    std::atomic<bool>             m_processing; 

    std::mutex                    m_mutex;
    std::condition_variable       m_cv;
};
#pragma endregion

// Callback function types for Phrozen network communication
typedef std::function<size_t(void*, size_t, size_t, void*)> OnSnapshotWriteStreamCallback;

#pragma region PhrozenNetworkAgent

class SafeString {
    std::string value;
    std::mutex m;
public:
    void set(const std::string& s) {
        std::lock_guard<std::mutex> lk(m);
        value = s;
    }
       std::string get() {
        std::lock_guard<std::mutex> lk(m);
        return value;
    }
};

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
    
    void SetFirstTimeToSendQuery( bool flag );
    bool IsFirstTimeToSendQuery();

    void SetThumbnailChecking( bool bCheck );
    bool IsThumbnailChecking();

    void SetConnectedToAms( bool connected );
    bool IsConnetedToAMS();

    void SetIsMachineLED_On( bool connected );
    bool IsMachineLED_On();

    void SetIsNozzleDetectFilament( bool connected );
    bool IsNozzleDetectFilament();

    // ==== function to interactive with machine === //
    void RunSendMessage( const std::vector< json >& kMessageList,
                         const std::vector< bool >& kSendingList );
    
    void InitializeForReceiveResponse();
    //void RunReceiveResponse();

    bool get_camera_stream_url(std::string dev_ip, std::string* url);
    CURLcode get_camera_snapshot(std::string dev_ip, std::vector<unsigned char>& image_data);

    /// GET /server/webcams/list → 解析第一個 webcam 的 flip/rotation 並寫入 out
    bool get_webcam_display_config(const std::string& dev_ip, PhrozenWebcamDisplayConfig& out);
    /// POST /server/webcams/item 將 cfg 的 flip/rotation 寫回 Moonraker
    bool set_webcam_display_config(const std::string& dev_ip, const PhrozenWebcamDisplayConfig& cfg);


    bool IsPrinterInfoChanged() { return m_bIsPrinterInfoChanged; }
    bool IsCalibrationProgressInfoChanged() { return  m_bIsCalibrationProgressInfoChanged; }
    bool IsAMSInfoListChenaged() { return m_bIsAMSInfoListChenaged; }
    bool IsMonitorWindowChanged() { return m_bIsMonitorWindowChanged; }
    
    void GetPrinterInfoData( PhrozenPrinterInfo& kData );  
    void GetCalibrationProgressInfoData( PhrozenCalibrationProgressInfo& kData );  
    void GetAMSInfoList( std::vector< PhrozenAMSInfo >& kData );  
    void GetMonitorWindowData( PhrozenMonitorWindow& kData );  

private:
    
    bool InitializeConnectorImp( const std::string& strIp );
    CURLcode send_action_Command( std::string send_payload );
    void DebugOutput(const std::string& prefix, const char* message = ""  );

    CURL* m_pCurlMainWebsocket = nullptr;
    std::mutex m_kCurlMutex;
    std::mutex m_kCalibrationProgressMutex;

    std::unique_ptr< PhrozenWebServiceInfo > m_spWebServiceInfo{ nullptr };
    std::unique_ptr< PhrozenPrinterInfo > m_spPrinterInfo{ nullptr };
    std::unique_ptr< PhrozenThreadControl > m_spThreadControl{ nullptr };
    std::unique_ptr< PhrozenMonitorWindow > m_spMonitorWindow{ nullptr };
    std::unique_ptr< PhrozenCalibrationProgressInfo > m_spCalibrationProgressInfo{ nullptr };

    std::unique_ptr< PhrozenAMSPatterns > m_spAmsPatterns{ nullptr };
    
    std::vector< PhrozenHistoryInfo > m_kHistoryList;
    std::vector< PhrozenAMSInfo > m_kAMSList;
    
    std::atomic<bool> m_bStartSending{false};
    std::atomic<bool> m_bStartReceiving{false};
    std::atomic<bool> m_bFirstTimeToSendQuery{true};
    std::atomic<bool> m_bDoThumbnailCheck{true};
    std::atomic<bool> m_bIsConnetedToAMS{true};
    std::atomic<bool> m_bIsMachineLED_On{true};
    std::atomic<bool> m_bIsNozzleDetectFilament{true};

    //SafeString m_kPrev_state;
    std::string m_strPrev_state;

    // flag to check if data chenged
    bool m_bIsPrinterInfoChanged{false};
    bool m_bIsCalibrationProgressInfoChanged{false};
    bool m_bIsAMSInfoListChenaged{false};
    bool m_bIsMonitorWindowChanged{false};
    

    // some data to large, need receive multiple times
    // Frame accumulation buffers for handling fragmented messages
    std::string m_strAms_message_buffer;      // Buffer for AMS-related messages
    std::string m_strHistoryInfo;              // Buffer for history info (existing)
    bool m_bHistoryStart = false;
    int m_nAgain = 0;
    
    // Sliding window buffer for cross-frame pattern matching
    // This helps catch patterns that span across frame boundaries
    std::string m_strSliding_window_buffer;
    const size_t MAX_SLIDING_WINDOW_SIZE = 10000;  // Maximum size to prevent memory issues
    
    OnSnapshotWriteStreamCallback m_fn_snapshop_write_stream_callback;

    bool m_bIsTestMode = false;
    size_t uTestCounter = 0;

};
#pragma endregion


} // namespace Slic3r

#endif // slic3r_PhrozenNetworkAgent_hpp_
