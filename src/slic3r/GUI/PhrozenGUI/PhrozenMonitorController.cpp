#include "PhrozenMonitorController.hpp"

#include <codecvt>
#include <cctype>
#include <iostream>
#include <sstream>
#include <atomic>
#include <regex>
#include <boost/beast/core/detail/base64.hpp>
#include <boost/algorithm/string/predicate.hpp>
#ifdef __APPLE__
#include <thread>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/sysctl.h>
#include <net/route.h>
#include <net/if_dl.h>
#include <net/if_arp.h>
#include <netinet/if_ether.h>
#endif
#ifdef _WIN32
#include <windows.h>
#endif


//namespace Slic3r {
//namespace GUI {
namespace MonitorControl{

    
void DebugOutput(const std::string& prefix, const char* message = ""  ) {
    std::string combined = prefix + message;
#ifdef _WIN32
    OutputDebugStringA(combined.c_str());
#else
    // to do
    // need to modify/create a version for macOS to achieve the same effect as Windows.
    std::cout << combined << std::endl;
#endif
}



    // Define global variables
    ThreadControl threadControl;
    bool m_bUdp_ing = false;
    bool m_bStartlistening = false;
    bool m_bDoingAction = false;
    //bool m_bStartReceiving = false;
    //bool m_bStartSending = false;
    std::atomic<bool> m_bStartSending{false};
    std::atomic<bool> m_bStartReceiving{false};
    

    void SetStartSending( bool bStart )
    {
        if ( bStart ) { m_bStartSending.store(true, std::memory_order_relaxed); }
        else          { m_bStartSending.store(false, std::memory_order_relaxed); }
    }
    bool IsStartSending()
    {
        return m_bStartSending.load(std::memory_order_relaxed);
    }

    void SetStartReceiving( bool bStart )
    {
        if ( bStart ) { m_bStartReceiving.store(true, std::memory_order_relaxed); }
        else          { m_bStartReceiving.store(false, std::memory_order_relaxed); }
    }
    bool IsStartReceiving()
    {
        return m_bStartReceiving.load(std::memory_order_relaxed);
    }


    

    CURL* m_pCurl = nullptr;
    CURL* m_pCurl_websocket = nullptr;

    std::vector<NetworkingMachineInfo> m_kNetworkingMachineInfoList;
    std::vector<HistoryInfo> m_kHistoryInfoList;
    std::vector<AMSInfo> m_kAMSList;
    std::vector<AMSInfo> m_kAMSList_temp;
    PrinterInfo* m_pPrinterInfo = new PrinterInfo();
    WebServiceInfo* m_pWebServiceInfo = new WebServiceInfo();
    CalibrationInfo m_pCalibrationInfo;
    MonitorWindow m_kMonitorWindow;
    bool m_bFirst = true;

    bool m_bCameraOn = false;
    std::wstring m_strVideo_path = L"";
    bool m_bInitial_P28 = true;
    bool m_bAMS_action_done = false;
    int m_nSendJobSuccess = 0;
    double m_fProgressValue = 0.0f;
    std::wstring m_strReceiveMessage = L"";
    bool m_bReceiving = true;
    int m_bIsLEDOn = 0;

    bool m_bOpenCVStream = false;
    bool m_bVideoFinished = false;
    bool m_bVideoStart = false;
    std::string m_strVideoPath = "";
    std::string m_strVideoTempPath = "";
    float m_fRecordFPS = 60.f;
    float m_fRecordInterval = 8.f;

    std::vector<unsigned char> m_kLatestImageData;
    std::mutex m_kImageMutex;
    bool m_bNewImageAvailable = false;
    GLuint m_nTexture = 0;
    GLuint m_nHistoryTexture[50] = { 0 };
    GLuint m_nPrintingTexture;
    bool m_bTriggerOnce = true;
    bool m_bClose = false;
    bool m_bIsCameraOn = false;
    bool m_bConnectionInitial = false;
    std::mutex m_kCurlMutex;
    std::mutex m_kCommandMutex;
    std::mutex m_kCalibrationProgressMutex;
    bool m_bIsConnetedToAMS = false;
    std::atomic<bool> m_bDoThumbnailCheck{false};
    std::string prev_state = "";
    bool isReadFromGcodeFinished = true;
    
    // Calibration progress tracking
    CalibrationProgressInfo m_calibrationProgressInfo = {};
    WebCamImageDataThreadHandler WebCamDataHandler = {};
    HttpErrorInfo error_info = {};
    AMSPatterns amsPatterns = {};
    NozzleInfo nozzleInfo = {};

    size_t fnWriteData(void* buffer, size_t size, size_t nmemb, void* lpVoid)
    {
        std::string* str = dynamic_cast<std::string*>((std::string*)lpVoid);
        if (NULL == str || NULL == buffer)
        {
            return -1;
        }
    
        char* pData = (char*)buffer;
        str->append(pData, size * nmemb);
        return nmemb;
    };

    int fnUploadProgressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
    {
        // Calculate the download progress percentage
        if (ultotal > 0) {
    
            m_fProgressValue = ulnow * 100.0f / ultotal;
            std::cout << "ulnow: " << ulnow << std::endl;
            std::cout << "ultotal: " << ultotal << std::endl;
            std::cout << "Upload Progress: " << m_fProgressValue << "%" << std::endl;
        }
        return 0;
    };

    size_t fnWriteData_file(void* ptr, size_t size, size_t nmemb, FILE* stream)
    {
        size_t written;
        //@vance add to avoid crash when stream is null pointer
        if (stream != nullptr) {
            written = fwrite(ptr, size, nmemb, stream);
        }
        else {
            written = 0;
        }
        return written;
    };

#ifdef __APPLE__
// macOS 特定：檢查 ARP 表中是否存在目標 IP
bool CheckArpEntryExists(const std::string& target_ip) {
    struct in_addr target;
    if (inet_aton(target_ip.c_str(), &target) == 0) {
        return false;
    }
    
    // 查詢 ARP 表
    size_t needed = 0;
    int mib[6] = {CTL_NET, PF_ROUTE, 0, AF_INET, NET_RT_FLAGS, RTF_LLINFO};
    
    if (sysctl(mib, 6, NULL, &needed, NULL, 0) < 0) {
        return false;
    }
    
    if (needed == 0) {
        return false;
    }
    
    char* buf = (char*)malloc(needed);
    if (sysctl(mib, 6, buf, &needed, NULL, 0) < 0) {
        free(buf);
        return false;
    }
    
    // 解析 ARP 表，查找目標 IP
    char* lim = buf + needed;
    char* next = buf;
    
    while (next < lim) {
        struct rt_msghdr* rtm = (struct rt_msghdr*)next;
        next += rtm->rtm_msglen;
        
        if (rtm->rtm_type != RTM_GET) {
            continue;
        }
        
        // 正確解析路由表消息中的 sockaddr 結構體
        // 路由表消息格式：rt_msghdr 後面跟著多個 sockaddr 結構體
        // 需要正確計算偏移量並遍歷所有 sockaddr
        char* sa_ptr = (char*)(rtm + 1);
        char* sa_end = (char*)rtm + rtm->rtm_msglen;
        
        // 遍歷所有 sockaddr 結構體
        while (sa_ptr < sa_end) {
            struct sockaddr* sa = (struct sockaddr*)sa_ptr;
            
            // 檢查 sockaddr 長度是否有效
            if (sa->sa_len == 0 || sa->sa_len > (sa_end - sa_ptr)) {
                break; // 無效的 sockaddr，停止解析
            }
            
            // 檢查是否為 IPv4 地址
            if (sa->sa_family == AF_INET) {
                struct sockaddr_in* sin = (struct sockaddr_in*)sa;
                if (sin->sin_addr.s_addr == target.s_addr) {
                    // 找到 ARP 條目
                    free(buf);
                    return true;
                }
            }
            
            // 移動到下一個 sockaddr（需要對齊到 4 字節邊界）
            sa_ptr += sa->sa_len;
            // macOS 要求 sockaddr 對齊到 4 字節邊界
            sa_ptr = (char*)(((uintptr_t)sa_ptr + 3) & ~3);
            
            // 額外檢查：確保對齊後不會超出邊界
            if (sa_ptr >= sa_end) {
                break;
            }
        }
    }
    
    free(buf);
    return false;
}

// macOS 特定：等待 ARP 表更新，直到目標 IP 出現或超時
bool WaitForArpResolution(const std::string& target_ip, int max_wait_ms) {
    auto start_time = std::chrono::steady_clock::now();
    
    while (true) {
        // 檢查 ARP 表中是否存在目標 IP
        if (CheckArpEntryExists(target_ip)) {
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
            printf("ARP entry found in table after %lld ms\n", elapsed_ms);
            return true;
        }
        
        // 檢查超時
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > max_wait_ms) {
            printf("ARP entry not found after %d ms timeout\n", max_wait_ms);
            return false;
        }
        
        // 等待一小段時間後重試
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

// macOS 特定：使用 socket 直接觸發 ARP 解析並等待完成
bool TriggerArpResolution(const std::string& target_ip) {
    if (target_ip.empty()) {
        return false;
    }
    
    // 使用 socket API 直接觸發 ARP，讓系統自動完成解析
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        printf("Failed to create socket for ARP resolution\n");
        return false;
    }
    
    // 設置較長的超時時間，等待 ARP 完成
    struct timeval timeout;
    timeout.tv_sec = 2;  // 2 秒超時
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    // 構造目標地址（使用一個不存在的端口，觸發 ARP 但不會真正連接）
    struct sockaddr_in target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(1);  // 端口 1 通常不會有服務
    inet_aton(target_ip.c_str(), &target_addr.sin_addr);
    
    // 嘗試連接（會觸發 ARP 解析，系統會自動等待 ARP 完成）
    // 即使連接失敗（端口不存在），ARP 解析也會完成
    int result = connect(sock, (struct sockaddr*)&target_addr, sizeof(target_addr));
    
    close(sock);
    
    // 無論連接成功與否，ARP 解析都會被觸發
    // 給系統一點時間確保 ARP 表已更新
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    printf("ARP resolution triggered via socket (connect result: %d)\n", result);
    BOOST_LOG_TRIVIAL(info) << "ARP resolution triggered for " << target_ip
                            << " (socket connect result: " << result << ")";
    
    return true;
}
#endif

CURLcode Initialconnect()
{
    CURLcode res = CURLE_FAILED_INIT;
    curl_version_info_data *ver_info;

    // Check CURL version and WebSocket support
    ver_info = curl_version_info(CURLVERSION_NOW);
    if (!ver_info) {
        printf("Failed to get CURL version info\n");
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "Failed to get CURL version info\n";
        return res;
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
        return res; // Protocol not supported
    }

    CleanupWebSocketConnection();
    m_pCurl_websocket = curl_easy_init();

    if (m_pCurl_websocket) {
        // Validate webServiceInfo before using
        if (m_pWebServiceInfo->ip.empty() || m_pWebServiceInfo->port.empty()) {
            printf("WebService info not properly initialized: IP=%s, Port=%s\n",
                   m_pWebServiceInfo->ip.c_str(), m_pWebServiceInfo->port.c_str());
            curl_easy_cleanup(m_pCurl_websocket);
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "WebService info not properly initialized\n";
            return res; // Invalid configuration
        }

        std::string url = "ws://" + m_pWebServiceInfo->ip + ":" + m_pWebServiceInfo->port + "/websocket";
        printf("Attempting WebSocket connection to: %s\n", url.c_str());

#ifdef __APPLE__
        // 使用 socket API 觸發 ARP 並等待完成（內部已包含等待）
        TriggerArpResolution(m_pWebServiceInfo->ip);
        
        // 等待 ARP 表更新，確認 ARP 解析完成（最多等待 1000ms）
        if (WaitForArpResolution(m_pWebServiceInfo->ip, 1000)) {
            printf("ARP resolution completed, ARP entry confirmed in table\n");
        } else {
            printf("ARP resolution may not be complete, but proceeding with connection\n");
            // 即使 ARP 表檢查失敗，也繼續連接（可能 ARP 表更新有延遲）
        }
        printf("Proceeding with WebSocket connection\n");
#endif

        // Set CURL options
        curl_easy_setopt(m_pCurl_websocket, CURLOPT_URL, url.c_str());
        curl_easy_setopt(m_pCurl_websocket, CURLOPT_TIMEOUT_MS, 5000L); // Increased timeout
        curl_easy_setopt(m_pCurl_websocket, CURLOPT_CONNECT_ONLY, 2L); /* websocket style */

        // Enable verbose output for debugging
        curl_easy_setopt(m_pCurl_websocket, CURLOPT_VERBOSE, 1L);

        // Set user agent
        curl_easy_setopt(m_pCurl_websocket, CURLOPT_USERAGENT, "PhrozenOrca WebSocket Client");

        // Disable SSL verification for testing (remove in production)
        curl_easy_setopt(m_pCurl_websocket, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(m_pCurl_websocket, CURLOPT_SSL_VERIFYHOST, 0L);

        /* Perform the request, res will get the return code */
        res = curl_easy_perform(m_pCurl_websocket);

        /* Check for errors */
        if (res != CURLE_OK) {
            printf("WebSocket connection failed: %s (Error code: %d)\n",
                   curl_easy_strerror(res), res);
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "WebSocket connection failed\n";

            // Get more detailed error information
            long response_code;
            curl_easy_getinfo(m_pCurl_websocket, CURLINFO_RESPONSE_CODE, &response_code);
            printf("HTTP response code: %ld\n", response_code);

            curl_easy_cleanup(m_pCurl_websocket);
            m_pCurl_websocket = nullptr;
            SetStartReceiving( false );
            SetStartSending( false );

            return res; // Return CURL error code
        }
        else {
            printf("WebSocket connection established successfully\n");
            
            if(m_pWebServiceInfo->ip == m_pPrinterInfo->pre_printerIP){
                m_pPrinterInfo->isSameIP = true;
            }
            else{
                m_pPrinterInfo->isSameIP = false;
                m_pPrinterInfo->pre_printerIP = m_pWebServiceInfo->ip;
            }

            // Get connection info
            long response_code;
            curl_easy_getinfo(m_pCurl_websocket, CURLINFO_RESPONSE_CODE, &response_code);
            printf("HTTP response code: %ld\n", response_code);

            char *effective_url;
            curl_easy_getinfo(m_pCurl_websocket, CURLINFO_EFFECTIVE_URL, &effective_url);
            if (effective_url) {
                printf("Connected to: %s\n", effective_url);
            }
        }
    }
    else {
        printf("Failed to initialize CURL handle\n");
        m_pCurl_websocket = nullptr;
        SetStartReceiving( false );
        SetStartSending( false );
        return res; // CURL init failed
    }

    return res;
}

void CleanupWebSocketConnection()
{
    if (m_pCurl_websocket != nullptr) {
        //close websocket while it linking.
        size_t sent;
        curl_ws_send(m_pCurl_websocket, "", 0, &sent, 0, CURLWS_CLOSE);
        
        //release memory
        curl_easy_cleanup(m_pCurl_websocket);
        m_pCurl_websocket = nullptr;
    }
}

void SetIp( const std::string& strIp ) { m_pWebServiceInfo->ip = strIp; }

size_t write_data(void* ptr, size_t size, size_t nmemb, void* stream)
{
    size_t written = fwrite(ptr, size, nmemb, (FILE*)stream);
    return written;
}

void websocket_close()
{
    size_t sent;
    (void)curl_ws_send( m_pCurl, "", 0, &sent, 0, CURLWS_CLOSE);
}

std::tuple<std::string, std::string, std::string> ParsePauseMessage(const std::string& message)
{
    const std::string prefix = "+PAUSE:";

    // Check prefix
    size_t start = message.find(prefix); 
    if (start == std::string::npos) {
        throw std::invalid_argument("Invalid format: does not contain +PAUSE:");
    }

    // Extract the content after the colon
    std::string content = message.substr(start + prefix.length());

    // Split into three parts: pauseCode, oldChannel, newChannel
    size_t firstComma = content.find(',');
    if (firstComma == std::string::npos)
        throw std::invalid_argument("Format error: missing first comma");

    size_t secondComma = content.find(',', firstComma + 1);
    if (secondComma == std::string::npos)
        throw std::invalid_argument("Format error: missing second comma");

    std::string pauseCode = content.substr(0, firstComma);
    std::string oldChannel = content.substr(firstComma + 1, secondComma - firstComma - 1);
    std::string newChannel = content.substr(secondComma + 1);

    return { pauseCode, oldChannel, newChannel };
}

void HandlePauseCode(const std::string& pauseCode)
{
    auto it = m_kMonitorWindow.pauseCodeToFlag.find(pauseCode);
    if (it != m_kMonitorWindow.pauseCodeToFlag.end()) {
        *(it->second) = true;
    }
}

// ============================================
// ReceiveResponse() Processing Modules
// ============================================

// Frame processing module for WebSocket frame handling
struct FrameProcessor {

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

// Calibration progress calculator module
struct CalibrationProgressCalculator {
    // Calculate calibration progress based on probe points
    static void UpdateCalibrationProgress(const std::string& params, CalibrationProgressInfo& info) {
        // Check for probe at message: "// probe at X,Y is z=Z"
        size_t probe_pos = params.find("probe at ");
        if (probe_pos != std::string::npos) {
            info.heatingCompleted = true;
            
            double x = 0, y = 0, z = 0;
            if (sscanf(params.c_str(), "// probe at %lf,%lf is z=%lf", &x, &y, &z) == 3) {
                // Auto-leveling Progress Calculation
                // Background: According to machine operation and Dongguan team feedback
                // Auto-leveling probe grid configuration
                // Note: The following values should be obtained from the printer's printer.cfg configuration:
                // - Grid coordinates (coords[]): Start and end positions come from bed_mesh_min/bed_mesh_max in printer.cfg
                // - Grid size (gridSize): X/Y probe point counts come from bed_mesh_probe_count in printer.cfg
                // Currently using hardcoded values for a 6x6 grid with coordinates [10, 66, 122, 178, 234, 290]
                // Grid layout: 6x6 = 36 probe points
                // Start position: (10, 10), End position: (290, 290)
                // Probe point spacing: 56mm
                // Calculation formula: spacing = (end.x - start.x) / (gridSize - 1) = (290 - 10) / (6 - 1) = 280 / 5 = 56mm
                // Or from adjacent coordinates: 66 - 10 = 56, 122 - 66 = 56, etc.
                const double coords[] = {10.0, 66.0, 122.0, 178.0, 234.0, 290.0};
                const int gridSize = 6;  // X/Y probe point count from printer.cfg bed_mesh_probe_count
                const float add = 1.944f;  // Progress increment per probe point: (95 - 25) / 36 ≈ 1.944
                const float baseProgress = 25.0f;  // Base progress after heating phase (0-25%)
                
                // Find indices for x and y coordinates in the grid
                int xIdx = -1, yIdx = -1;
                for (int i = 0; i < gridSize; ++i) {
                    if (std::abs(x - coords[i]) <= 0.01) { xIdx = i; break; }
                }
                for (int i = 0; i < gridSize; ++i) {
                    if (std::abs(y - coords[i]) <= 0.01) { yIdx = i; break; }
                }
                
                // Calculate progress with zigzag scanning pattern
                // The probe uses zigzag (snake) pattern: odd rows (1st, 3rd, 5th) scan left-to-right,
                // even rows (2nd, 4th, 6th) scan right-to-left (X reversed)
                // Point index calculation:
                // - Odd rows (yIdx even: 0, 2, 4): pointIndex = yIdx * gridSize + xIdx + 1
                // - Even rows (yIdx odd: 1, 3, 5): pointIndex = yIdx * gridSize + (gridSize - 1 - xIdx) + 1
                // Progress ranges from 25% (first point) to ~95% (last point, 36th)
                if (xIdx >= 0 && yIdx >= 0) {
                    int pointIndex;
                    if (yIdx % 2 == 0) {
                        // Odd row (1st, 3rd, 5th): scan left-to-right
                        pointIndex = yIdx * gridSize + xIdx + 1;
                    } else {
                        // Even row (2nd, 4th, 6th): scan right-to-left (X reversed)
                        pointIndex = yIdx * gridSize + (gridSize - 1 - xIdx) + 1;
                    }
                    float progress = baseProgress + add * pointIndex;
                    if (progress > 1.0f) {
                        info.calibrationProgress = std::min(progress, 100.0f);
                    }
                }
            }
        }
        
        // Update calibration progress based on heating (if not completed)
        if (info.calibrationStatus == CalibrationState::RUNNING && !info.heatingCompleted) {
            float bed = 0, extruder = 0;
            if (m_pPrinterInfo->bed_temperature_target > 0)
                bed = (static_cast<float>(m_pPrinterInfo->bed_temperature) / 
                       m_pPrinterInfo->bed_temperature_target) * 12.5f;
            if (m_pPrinterInfo->extruder_temperature_target > 0) {
                extruder = (static_cast<float>(m_pPrinterInfo->extruder_temperature) / 
                           m_pPrinterInfo->extruder_temperature_target) * 12.5f;
                if (extruder > 12.5f)
                    extruder = 12.5f;
            }
            float progress = bed + extruder;  // Max 25%
            if (progress > 1.0f) {
                info.calibrationProgress = progress;
            }
        }
    }
    
    // Calculate resonance compensation progress
    static void UpdateResonanceCompensationProgress(const std::string& params, CalibrationProgressInfo& info) {
        // Resonance Compensation Progress Calculation
        // Background: According to machine operation and Dongguan team feedback, resonance compensation has a fixed duration.
        // The process consists of X-axis and Y-axis resonance compensation tests, which can be observed from the web console logs.
        //
        // Progress Distribution:
        // - Initial phase (preparation): 0% - 10% (10% progress)
        // - X-axis test: 10% - 50% (40% progress, 120 seconds fixed duration)
        // - Y-axis test: 50% - 90% (40% progress, 120 seconds fixed duration)
        // - Final phase (completion): 90% - 100% (10% progress)
        //
        // Calculation Formula:
        // - For X/Y axis tests: 40% progress = 120 seconds
        // - Progress rate: 40% / 120 seconds = 1% per 3 seconds
        // - Frequency-based progress: progress = baseProgress + (Hz / 150) * 40
        //   where baseProgress is 10% for X-axis and 50% for Y-axis
        //   and 150 Hz is the maximum test frequency
        //
        // Time-based progress (when frequency info unavailable):
        // - Initial phase: 1% per 3 seconds (0% - 10%)
        // - Final phase: 1% per 5.2 seconds (89% - 99%)
        
        static float _progress = 0;
        
        // Check for axis testing
        if (params == "// Testing axis x") {
            info.startResonanceCompensation = true;
            _progress = 10.0f;  // X axis test starts at 10% (after initial preparation phase)
            info.startTime = std::chrono::steady_clock::now();
        } else if (params == "// Testing axis y") {
            _progress = 50.0f;  // Y axis test starts at 50% (after X-axis test completes)
            info.startTime = std::chrono::steady_clock::now();
        }
        
        // Check for frequency testing: "// Testing frequency X Hz"
        // Progress calculation: baseProgress + (currentHz / maxHz) * axisProgressRange
        // Example: X-axis at 75 Hz = 10% + (75/150) * 40% = 10% + 20% = 30%
        //          Y-axis at 75 Hz = 50% + (75/150) * 40% = 50% + 20% = 70%
        int Hz = 0;
        if (sscanf(params.c_str(), "// Testing frequency %d Hz", &Hz) == 1 && Hz > 0) {
            float progress = _progress + (static_cast<float>(Hz) / 150.0f) * 40.0f;
            if (progress > 1.0f) {
                info.resonanceCompensationProgress = progress;
            }
        }
        
        // Handle initial phase (progress < 10%): Preparation phase before X-axis test starts
        // Time-based progress: 1% per 3 seconds
        if (!info.startResonanceCompensation && info.resonanceCompensationProgress < 10.0f) {
            auto nowTime = std::chrono::steady_clock::now();
            long long timeDiff = std::chrono::duration_cast<std::chrono::seconds>(nowTime - info.startTime).count();
            if (timeDiff > 3) {
                float newProgress = info.resonanceCompensationProgress + 1.0f;
                if (newProgress > 1.0f) {
                    info.resonanceCompensationProgress = newProgress;
                    info.startTime = nowTime;
                }
            }
        }
        
        // Handle final phase (89% - 99%): Completion phase after Y-axis test
        // Time-based progress: 1% per 5.2 seconds (slower than initial phase)
        if (info.resonanceCompensationProgress >= 89.0f && info.resonanceCompensationProgress <= 99.0f) {
            auto nowTime = std::chrono::steady_clock::now();
            long long timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(nowTime - info.startTime).count();
            if (timeDiff > 5200) {  // 5.2 seconds per 1%
                float newProgress = info.resonanceCompensationProgress + 1.0f;
                if (newProgress > 1.0f) {
                    info.resonanceCompensationProgress = newProgress;
                    info.startTime = nowTime;
                }
            }
        }
        
        info.resonanceCompensationProgress = std::min(info.resonanceCompensationProgress, 100.0f);
    }
    
    // Calculate temperature calibration progress
    static void UpdateTemperatureCalibrationProgress(const std::string& params, CalibrationProgressInfo& info) {
        // Temperature Progress Calculation
        // Background: According to machine operation and Dongguan team feedback.
        // Match pattern: T0:XXX/YYY (e.g., "T0:210.0/210.0" or "T0:205.5/205.0")
        // According to CalibrationWindow_Analysis.md line 224, the original pattern was:
        // R"(T0:(\d+\.\d+)\s*/\s*(\d+\.\d+))" which requires decimal point
        // This strict pattern ensures:
        // - \d+ : one or more digits before decimal point (required)
        // - \. : decimal point (required)
        // - \d+ : one or more digits after decimal point (required)
        // This matches only formats like "T0:210.0/210.0" with decimal point
        std::regex t0_pattern(R"(T0:(\d+\.\d+)\s*/\s*(\d+\.\d+))");
        std::smatch match;
        if (std::regex_search(params, match, t0_pattern)) {
            float t0_value1 = std::stof(match[1]);  // Current temperature
            float t0_value2 = std::stof(match[2]);  // Target temperature
            
            // Temperature Calibration Progress Formula Explanation
            // Background: Temperature calibration cycles between 210°C and 205°C repeatedly.
            // Each complete cycle (210°C -> 205°C -> 210°C) represents approximately 9% progress.
            // The first cycle (0°C -> 210°C) is special and also counts as 9% (0% - 9%).
            // Subsequent cycles each contribute 8% progress increment.
            //
            // Progress Distribution:
            // - Cycle 1 (0°C -> 210°C): 0% - 9% (9% total)
            // - Cycle 2 (210°C -> 205°C -> 210°C): 9% - 17% (8% increment)
            // - Cycle 3 (210°C -> 205°C -> 210°C): 17% - 25% (8% increment)
            // - ... and so on until 100%
            //
            // tempProgress counter tracks cycle number:
            // - Even values (0, 2, 4, ...): 210°C phase
            // - Odd values (1, 3, 5, ...): 205°C phase
            // - Increments when entering each phase
            
            // 210°C phase: Heating from 205°C (or 0°C for first cycle) to 210°C
            if (std::abs(t0_value2 - 210.0f) < 0.1f) {
                // Increment tempProgress when entering 210°C phase (even -> odd transition)
                if (info.tempProgress == 0 || info.tempProgress % 2 == 0)
                    info.tempProgress++;
                
                if (t0_value1 <= 210.0f) {
                    // Formula for subsequent cycles (tempProgress > 1): Heating from 205°C to 210°C
                    // progress = completedCyclesProgress + currentCycleProgress
                    // completedCyclesProgress = 8.0f * (tempProgress - 1)
                    //   Explanation: First cycle (tempProgress=1) gives 0, second cycle (tempProgress=2) gives 8%,
                    //                third cycle (tempProgress=3) gives 16%, etc.
                    // currentCycleProgress = ((currentTemp - 205°C) / (210°C - 205°C)) * 9.0f
                    //   Explanation: Linear interpolation from 205°C to 210°C, scaled to 9% of cycle
                    //   Example: At 207.5°C (halfway), progress = 0.5 * 9% = 4.5% within cycle
                    if (info.tempProgress > 1 && t0_value1 >= 205.0f && t0_value1 <= 210.0f) {
                        float progress = (8.0f * (static_cast<float>(info.tempProgress) - 1)) + 
                                         ((t0_value1 - 205.0f) / (t0_value2 - 205.0f)) * 9.0f;
                        if (progress > 1.0f) {
                            info.temperatureCalibrationProgress = progress;
                        }
                    }
                    // Formula for first cycle (tempProgress == 1): Heating from 0°C to 210°C
                    // progress = (currentTemp / targetTemp) * 9.0f
                    //   Explanation: Linear interpolation from 0°C to 210°C, scaled to 9% total
                    //   Example: At 105°C (halfway), progress = 0.5 * 9% = 4.5%
                    else if (info.tempProgress == 1) {
                        float progress = (t0_value1 / t0_value2) * 9.0f;
                        if (progress > 1.0f) {
                            info.temperatureCalibrationProgress = progress;
                        }
                    }
                }
            }
            // 205°C phase: Cooling from 210°C to 205°C
            else if (std::abs(t0_value2 - 205.0f) < 0.1f) {
                // Increment tempProgress when entering 205°C phase (odd -> even transition)
                if (info.tempProgress % 2 != 0)
                    info.tempProgress++;
                
                if (t0_value1 <= 210.0f && t0_value1 >= 205.0f) {
                    // Formula: Cooling from 210°C to 205°C
                    // progress = completedCyclesProgress + currentCycleProgress
                    // completedCyclesProgress = 8.0f * (tempProgress - 1)
                    //   Explanation: Same as 210°C phase, tracks completed cycles
                    // currentCycleProgress = ((210°C - currentTemp) / (210°C - 205°C)) * 9.0f
                    //   Explanation: Linear interpolation from 210°C to 205°C (inverse direction),
                    //                scaled to 9% of cycle
                    //   Example: At 207.5°C (halfway cooling), progress = 0.5 * 9% = 4.5% within cycle
                    //   Note: (210°C - currentTemp) gives distance from start (210°C), 
                    //         divided by total range (5°C) gives progress ratio
                    float progress = (8.0f * (static_cast<float>(info.tempProgress) - 1)) + 
                                     ((210.0f - t0_value1) / (210.0f - t0_value2)) * 9.0f;
                    if (progress > 1.0f) {
                        info.temperatureCalibrationProgress = progress;
                    }
                }
            }
            
            info.temperatureCalibrationProgress = std::min(info.temperatureCalibrationProgress, 100.0f);
        }
    }
};

// Message processing module for message type detection and conversion
struct MessageProcessor {
    static bool ShouldSkipProcStat(const std::string& message) {
        std::string skip_message = "{\"jsonrpc\": \"2.0\", \"method\": \"notify_proc_stat_update\"";
        return message.find(skip_message.c_str()) != std::string::npos;
    }
    
    static std::wstring ConvertToWideString(const std::string& utf8_str) {
        try {
            std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
            return converter.from_bytes(utf8_str);
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(warning) << "UTF-8 to UTF-16 conversion failed: " << e.what();
            return std::wstring(utf8_str.begin(), utf8_str.end());
        }
    }
    
    static void ProcessGcodeResponse(const std::string& message) {
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
                    
                    // Check for PRZ_ADC response with fila_exist
                    if (params.find("PRZ_ADC:") != std::string::npos && params.find("fila_exist") != std::string::npos) {
                        if (params.find("fila_exist:True") != std::string::npos ||
                            params.find("fila_exist:true") != std::string::npos) {
                            nozzleInfo.fila_exist = true;
                        } else {
                            nozzleInfo.fila_exist = false;
                        }
                        BOOST_LOG_TRIVIAL(info) << "*** PRZ_ADC response: fila_exist = " << (nozzleInfo.fila_exist ? "true" : "false") << " ***";
                    }
                    
                    // ============================================
                    // Calibration message processing
                    // ============================================
                    {
                        // Auto-leveling (Calibration) messages
                        if (params.find("Probe samples exceed samples_tolerance") != std::string::npos) {
                            std::lock_guard<std::mutex> lock(m_kCalibrationProgressMutex);
                            m_calibrationProgressInfo.calibrationStatus = CalibrationState::ERROR;
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
    
    static void ProcessHistoryInfo(const std::string& message,
                                  std::string& historyBuffer,
                                  bool& historyStart) {
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
    
    static void ProcessPauseMessage(const std::string& message) {
        
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
};

// Printer status extraction module
struct PrinterStatusExtractor {
    static void ExtractTemperatureInfo(const json& status) {
        if (status.contains("extruder") && status["extruder"].contains("temperature")) {
            m_pPrinterInfo->extruder_temperature = status["extruder"]["temperature"];
        }
        if (status.contains("extruder") && status["extruder"].contains("target")) {
            m_pPrinterInfo->extruder_temperature_target = status["extruder"]["target"];
        }
        if (status.contains("heater_bed") && status["heater_bed"].contains("temperature")) {
            m_pPrinterInfo->bed_temperature = status["heater_bed"]["temperature"];
        }
        if (status.contains("heater_bed") && status["heater_bed"].contains("target")) {
            m_pPrinterInfo->bed_temperature_target = status["heater_bed"]["target"];
        }
        if (status.contains("temperature_sensor Chamber_sensor") &&
            status["temperature_sensor Chamber_sensor"].contains("temperature") &&
            status["temperature_sensor Chamber_sensor"]["temperature"].is_number()) {
            m_pPrinterInfo->chamber_temperature = status["temperature_sensor Chamber_sensor"]["temperature"];
        }
    }
    
    static void ExtractFanSpeedInfo(const json& status) {
        if (status.contains("output_pin fan_assist") &&
            status["output_pin fan_assist"].contains("value") &&
            status["output_pin fan_assist"]["value"].is_number()) {
            m_pPrinterInfo->auxiliary_fan_speed = status["output_pin fan_assist"]["value"];
        }
        if (status.contains("fan_generic Chamber_fan") &&
            status["fan_generic Chamber_fan"].contains("speed") &&
            status["fan_generic Chamber_fan"]["speed"].is_number()) {
            m_pPrinterInfo->shield_fan_speed = status["fan_generic Chamber_fan"]["speed"];
        }
        if (status.contains("fan_generic cooling_fan") &&
            status["fan_generic cooling_fan"].contains("speed") &&
            status["fan_generic cooling_fan"]["speed"].is_number()) {
            m_pPrinterInfo->fan_speed = status["fan_generic cooling_fan"]["speed"];
        }
    }
    
    static void ExtractGcodeMoveInfo(const json& status) {
        if (status.contains("gcode_move")) {
            if (status["gcode_move"].contains("speed_factor")) {
                m_pPrinterInfo->print_speed = status["gcode_move"]["speed_factor"];
            }
            if (status["gcode_move"].contains("homing_origin") &&
                status["gcode_move"]["homing_origin"].is_array() &&
                status["gcode_move"]["homing_origin"].size() > 2) {
                m_pPrinterInfo->z_offsetValure = status["gcode_move"]["homing_origin"][2];
            }
        }
    }
    
    static void ExtractToolheadInfo(const json& status) {
        if (status.contains("toolhead")) {
            if (status["toolhead"].contains("homed_axes")) {
                m_pPrinterInfo->home_axes = status["toolhead"]["homed_axes"].get<std::string>();
            }
            if (status["toolhead"].contains("estimated_print_time")) {
                m_pPrinterInfo->estimated_print_time = status["toolhead"]["estimated_print_time"];
            }
        }
    }
    
    static void ExtractPrintStatusInfo(const json& status) {
        // Static variables to track previous values for change detection
        
        if (status.contains("display_status") && status["display_status"].contains("progress")) {
            m_pPrinterInfo->print_progress = status["display_status"]["progress"];
        }
        if (status.contains("pause_resume") && status["pause_resume"].contains("is_paused")) {
            m_pPrinterInfo->is_paused = status["pause_resume"]["is_paused"];
        }
        if (status.contains("print_stats")) {
            std::string new_state;
            std::string new_print_file;
            
            if (status["print_stats"].contains("state")) {
                new_state = status["print_stats"]["state"].get<std::string>();
                m_pPrinterInfo->state = new_state;
            }
            if (status["print_stats"].contains("filename")) {
                m_pPrinterInfo->print_file = status["print_stats"]["filename"];
            }
            if (status["print_stats"].contains("print_duration")) {
                m_pPrinterInfo->print_time = status["print_stats"]["print_duration"];
            }
            if (status["print_stats"].contains("total_duration")) {
                m_pPrinterInfo->total_time = status["print_stats"]["total_duration"];
            }
            if (status["print_stats"].contains("filament_used")) {
                m_pPrinterInfo->print_filament = status["print_stats"]["filament_used"];
            }
            
            // Detect state change to trigger thumbnail check
            if(m_pPrinterInfo->isSameIP){
                if ( (prev_state == "standby" || prev_state == "offline" ||
                      prev_state == "paused" || prev_state == "cancelled" || prev_state.empty()) &&
                         (new_state == "printing" || new_state == "complete" )) {
                    SetThumbnailChecking(true);
                    BOOST_LOG_TRIVIAL(info) << "ExtractPrintStatusInfo: Print state changed from \""
                                            << prev_state << "\" to \"" << new_state << "\"";
                    // Update previous values
                    prev_state = new_state;
                }else{
                    prev_state = new_state;
                }
            }
            else{
                if ( (new_state == "paused" || new_state == "complete" ||
                      new_state == "cancelled" || new_state == "printing" ||
                      new_state == "complete" )) {
                    SetThumbnailChecking(true);
                    BOOST_LOG_TRIVIAL(info) << "ExtractPrintStatusInfo: Print state changed from \""
                                            << prev_state << "\" to \"" << new_state << "\"";
                    // Update previous values
                    prev_state = new_state;
                    m_pPrinterInfo->isSameIP = true;
                }
            }
        }
    }
    
    static void ProcessPrinterStatus(const std::string& message) {
        std::string id = "\"id\": 7466";
        std::string result = "result";
        size_t pos = message.find(id.c_str());
        size_t pos_result = message.find(result.c_str());
        if (pos == std::string::npos || pos_result == std::string::npos) return;
        
        if (json::accept(message)) {
            try {
                m_pWebServiceInfo->jsonPrinterInfoData = json::parse(message);
                if (!m_pWebServiceInfo->jsonPrinterInfoData["result"].is_null()) {
                    if (!m_pWebServiceInfo->jsonPrinterInfoData["result"].is_object()) {
                        std::string a = m_pWebServiceInfo->jsonPrinterInfoData["result"].get<std::string>();
                        BOOST_LOG_TRIVIAL(info) << a << endl;
                    } else if (!m_pWebServiceInfo->jsonPrinterInfoData["result"]["status"].is_null()) {
                        json status = m_pWebServiceInfo->jsonPrinterInfoData["result"]["status"];
                        
                        ExtractTemperatureInfo(status);
                        ExtractFanSpeedInfo(status);
                        ExtractGcodeMoveInfo(status);
                        ExtractToolheadInfo(status);
                        ExtractPrintStatusInfo(status);
                        
                        m_pPrinterInfo->error = "";
                    }
                }
            } catch (const json::exception& e) {
                BOOST_LOG_TRIVIAL(warning) << "JSON parse error in printer status: " << e.what();
            }
        } else {
            BOOST_LOG_TRIVIAL(info) << "JSON NOT ACCEPT" << endl;
        }
    }
};

// AMS processing module
struct AMSProcessor {
    static void ProcessAMSConnectionStatus(const std::string& search_text,
                                          std::string& sliding_window_buffer) {
        size_t ams_pos = search_text.find(amsPatterns.AMS_connected.c_str());
        if (ams_pos != std::string::npos) {
            m_bIsConnetedToAMS = true;
            BOOST_LOG_TRIVIAL(info) << "AMS connected status detected";
            if (ams_pos < sliding_window_buffer.size()) {
                sliding_window_buffer = sliding_window_buffer.substr(
                    ams_pos + amsPatterns.AMS_connected.length()
                );
            }
        }
        
        ams_pos = search_text.find(amsPatterns.AMS_unconnect.c_str());
        if (ams_pos != std::string::npos) {
            m_bIsConnetedToAMS = false;
            BOOST_LOG_TRIVIAL(info) << "AMS disconnected status detected";
            if (ams_pos < sliding_window_buffer.size()) {
                sliding_window_buffer = sliding_window_buffer.substr(
                    ams_pos + amsPatterns.AMS_unconnect.length()
                );
            }
        }
    }
    
    static void ProcessAMSCommandStates(const std::string& search_text,
                                       std::string& sliding_window_buffer) {
        // Unified lambda with slot number parsing and state update
        auto parseAMSCommandState = [&](const std::string& pattern, bool is_load = false, bool is_start = false) -> bool {
            size_t pos = search_text.find(pattern);
            if (pos == std::string::npos) return false;
            
            // Handle unload all patterns - update all slots
            if (pattern == amsPatterns.AMS_unload_all_start || pattern == amsPatterns.AMS_unload_all_end) {
                AMSCommandState new_state = (pattern == amsPatterns.AMS_unload_all_start) ? AMSCommandState::START : AMSCommandState::FINISH;
                const char* state_name = (pattern == amsPatterns.AMS_unload_all_start) ? "START" : "FINISH";
                
                for (size_t i = 0; i < m_kAMSList.size(); i++) {
                    m_kAMSList[i].unload_state = new_state;
                    
                    if(pattern == amsPatterns.AMS_unload_all_start){
                        m_kAMSList[i].unload_state = AMSCommandState::START;
                    }
                    if(pattern == amsPatterns.AMS_unload_all_end){
                        m_kAMSList[i].loading = false;
                        m_kAMSList[i].unload_state = AMSCommandState::NONE;
                    }
                }
                
                BOOST_LOG_TRIVIAL(info) << "*** All AMS slots unload_state = " << state_name << " ***";
                
                // Clean up sliding window buffer
                if (pos < sliding_window_buffer.size()) {
                    sliding_window_buffer.erase(0, pos + pattern.length());
                }
                return true;
            } else {
                // Parse slot number after comma (e.g., P1Bn:0,2 -> extract 2)
                int slot_number = -1;
                size_t comma_pos = pos + pattern.length();
                if (comma_pos < search_text.length() && search_text[comma_pos] == ',') {
                    char* end_ptr;
                    slot_number = static_cast<int>(std::strtol(search_text.c_str() + comma_pos + 1, &end_ptr, 10));
                    if (end_ptr == search_text.c_str() + comma_pos + 1) slot_number = -1;
                }
                
                // Update AMSInfo state based on slot number
                const size_t max_slots = m_kAMSList.size();
                if (slot_number > 0 && slot_number <= static_cast<int>(max_slots)) {
                    size_t slot_index = static_cast<size_t>(slot_number - 1);
                    AMSCommandState& target_state = is_load
                        ? m_kAMSList[slot_index].load_state
                        : m_kAMSList[slot_index].unload_state;
                    target_state = is_start ? AMSCommandState::START : AMSCommandState::FINISH;
                    
                    //Update related fields based on state
                    if (!is_start) {
                        if (is_load) {
                            //load_state = START -> loading = true
                            m_kAMSList[slot_index].loading = true;
                            m_kAMSList[slot_index].load_state = AMSCommandState::NONE;
                        } else {
                            //unload_state = finished -> loading = true
                            m_kAMSList[slot_index].loading = false;
                            m_kAMSList[slot_index].unload_state = AMSCommandState::NONE;
                        }
                    }
                    
                    const char* state_name = is_start ? "START" : "FINISH";
                    const char* cmd_type = is_load ? "load_single" : "unload_single";
                    BOOST_LOG_TRIVIAL(info) << "*** Slot " << slot_number << " " << cmd_type << " = " << state_name << " ***";
                } else if (slot_number > 0) {
                    BOOST_LOG_TRIVIAL(warning) << "*** Slot number " << slot_number << " out of range (max: " << max_slots << ") ***";
                } else {
                    BOOST_LOG_TRIVIAL(error) << "*** Pattern " << pattern << " found but no valid slot number ***";
                }
            }
            
            // Clean up sliding window buffer
            if (pos < sliding_window_buffer.size()) {
                sliding_window_buffer.erase(0, pos + pattern.length());
            }
            return true;
        };
        
        // Parse unload all states (simple patterns, no slot number)
        parseAMSCommandState(amsPatterns.AMS_unload_all_start);
        parseAMSCommandState(amsPatterns.AMS_unload_all_end);
        
        // Parse load/unload single slot states: (pattern, is_load, is_start)
        parseAMSCommandState(amsPatterns.AMS_load_single_start, true, true);     // P1Tn:0 -> load_single = START
        parseAMSCommandState(amsPatterns.AMS_load_single_end, true, false);      // P1Tn:1 -> load_single = FINISH
        parseAMSCommandState(amsPatterns.AMS_unload_single_start, false, true);  // P1Bn:0 -> unload_single = START
        parseAMSCommandState(amsPatterns.AMS_unload_single_end, false, false);   // P1Bn:1 -> unload_single = FINISH
    }
    
    static void ProcessAMSEntryParkState(const std::string& message)
    {
        //AMS1连接失败 unicode
        std::string ams_disconnect_prefix = "AMS1\\u8fde\\u63a5\\u5931\\u8d25";
        size_t ams_pos = message.find(ams_disconnect_prefix);
        if (ams_pos != std::string::npos){
            m_bIsConnetedToAMS = false;
        }
        else{
            std::string ams_info = "entry_state";
            size_t ams_pos = message.find(ams_info.c_str());
            if (ams_pos == std::string::npos) return;
            
            json ams_json;
            if (json::accept(message)) {
                try {
                    ams_json = json::parse(message);
                    std::string entry_state = ams_json["params"][0].get<std::string>();
                    entry_state = entry_state.substr(entry_state.find("{"));
                    int _entry_state = 0;
                    int _park_state = 0;
                    
                    if (json::accept(entry_state)) {
                        json info_json = json::parse(entry_state);
                        _entry_state = info_json["entry_state"];
                        _park_state = info_json["park_state"];
                    }
                    
                    if (_entry_state > -1) {
                        m_bIsConnetedToAMS = true;
                    } else {
                        m_bIsConnetedToAMS = false;
                    }
                    
                    // Initialize AMS list if empty
                    if (m_kAMSList_temp.empty()) {
                        for (int i = 1; i <= 4; i++) {
                            AMSInfo _AMSInfo;
                            m_kAMSList_temp.push_back(_AMSInfo);
                        }
                    }
                    
                    for (int i = 1; i <= 4; i++) {
                        m_kAMSList_temp[i-1].filament = "";
                        
                        // Park state logic
                        m_kAMSList_temp[i-1].park = false;
                        if (i == 1 && (_park_state == 1 || _park_state == 3 || _park_state == 5 ||
                                       _park_state == 9 || _park_state == 7 || _park_state == 11 ||
                                       _park_state == 13 || _park_state == 15))
                            m_kAMSList_temp[i-1].park = true;
                        if (i == 2 && (_park_state == 2 || _park_state == 3 || _park_state == 6 ||
                                       _park_state == 10 || _park_state == 7 || _park_state == 11 ||
                                       _park_state == 14 || _park_state == 15))
                            m_kAMSList_temp[i-1].park = true;
                        if (i == 3 && (_park_state == 4 || _park_state == 5 || _park_state == 6 ||
                                       _park_state == 12 || _park_state == 7 || _park_state == 13 ||
                                       _park_state == 14 || _park_state == 15))
                            m_kAMSList_temp[i-1].park = true;
                        if (i == 4 && (_park_state == 8 || _park_state == 9 || _park_state == 10 ||
                                       _park_state == 12 || _park_state == 11 || _park_state == 13 ||
                                       _park_state == 14 || _park_state == 15))
                            m_kAMSList_temp[i-1].park = true;
                        
                        // Entry state logic
                        m_kAMSList_temp[i-1].entry = false;
                        if (i == 1 && (_entry_state == 1 || _entry_state == 3 || _entry_state == 5 ||
                                       _entry_state == 9 || _entry_state == 7 || _entry_state == 11 ||
                                       _entry_state == 13 || _entry_state == 15))
                            m_kAMSList_temp[i-1].entry = true;
                        if (i == 2 && (_entry_state == 2 || _entry_state == 3 || _entry_state == 6 ||
                                       _entry_state == 10 || _entry_state == 7 || _entry_state == 11 ||
                                       _entry_state == 14 || _entry_state == 15))
                            m_kAMSList_temp[i-1].entry = true;
                        if (i == 3 && (_entry_state == 4 || _entry_state == 5 || _entry_state == 6 ||
                                       _entry_state == 12 || _entry_state == 7 || _entry_state == 13 ||
                                       _entry_state == 14 || _entry_state == 15))
                            m_kAMSList_temp[i-1].entry = true;
                        if (i == 4 && (_entry_state == 8 || _entry_state == 9 || _entry_state == 10 ||
                                       _entry_state == 12 || _entry_state == 11 || _entry_state == 13 ||
                                       _entry_state == 14 || _entry_state == 15))
                            m_kAMSList_temp[i-1].entry = true;
                        
                        
                        m_kAMSList_temp[i-1].selected = false;
                        
                        if(!m_kAMSList.empty()){
                            m_kAMSList[i-1].park = m_kAMSList_temp[i-1].park;
                            m_kAMSList[i-1].entry = m_kAMSList_temp[i-1].entry;
                            m_kAMSList[i-1].selected = m_kAMSList_temp[i-1].selected;
                        }
                    }
                    if(m_kAMSList.empty()){
                        m_kAMSList = m_kAMSList_temp;
                    }
                } catch (const json::exception& e) {
                    BOOST_LOG_TRIVIAL(warning) << "JSON parse error in AMS entry_state: " << e.what();
                }
            }
        }
    }
};

/**
 * Optimized ReceiveResponse() Function
 *
 * This optimized version addresses WebSocket frame fragmentation issues
 * and improves message reliability by:
 * 1.   Checking WebSocket frame flags to handle fragmented messages
 * 2.   Accumulating frames until complete messages are received
 * 3.   Using sliding window search for cross-frame pattern matching
 * 4.   Removing early continue to ensure all messages are processed
 * 5.   Adding comprehensive debug logging
 *
 * Refactored into modular structure for better readability and maintainability
 */

CURLcode ReceiveResponse() {
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
    
    while (IsStartReceiving()) {
        // ⚠️ CRITICAL: libcurl easy handle is NOT thread-safe
        // Cannot call curl_ws_send()/curl_ws_recv() from multiple threads simultaneously
        // Operating the same curl handle from 2 threads may cause crash risk
        if(!threadControl.first_time_to_send_query){
            std::lock_guard<std::mutex> lock(m_kCurlMutex);
            BOOST_LOG_TRIVIAL(debug) << "ReceiveResponse: Lock acquired, Thread ID: " << thread_id;
            try {
                double connectTime = 0;
                curl_easy_getinfo(m_pCurl_websocket, CURLINFO_CONNECT_TIME, &connectTime);
                
                if (connectTime > 0) {
                    res = curl_ws_recv(m_pCurl_websocket, buffer, sizeof(buffer), &rlen, &meta);
                    
                    if (res == CURLE_OK) {
                        again = 0;
                        
                        // ============================================
                        // Frame Fragmentation Handling
                        // ============================================
                        std::string frame_data(&buffer[0], &buffer[rlen]);
                        bool is_text_frame = (meta->flags & CURLWS_TEXT) != 0;
                        bool is_binary_frame = (meta->flags & CURLWS_BINARY) != 0;
                        bool is_continuation_frame = FrameProcessor::IsContinuationFrame(meta);
                        bool is_final_frame = FrameProcessor::IsFinalFrame(meta);
                        
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
                            continue;  // Wait for more frames
                        }
                        
                        // ============================================
                        // Complete Message Combination
                        // ============================================
                        std::string complete_message = FrameProcessor::CombineFrames(frame_data, ams_message_buffer);
                        FrameProcessor::UpdateSlidingWindow(sliding_window_buffer, complete_message, MAX_SLIDING_WINDOW_SIZE);
                        
                        // ============================================
                        // Message Conversion
                        // ============================================
                        std::string ws = complete_message;
                        m_strReceiveMessage = MessageProcessor::ConvertToWideString(ws);
                        
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
                        BOOST_LOG_TRIVIAL(info) << "receive error: " << endl;
                        curl_easy_cleanup(m_pCurl_websocket);
                        curl_global_cleanup();
                        Initialconnect();
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
    }
    
    m_pPrinterInfo->state = "offline";
    return res;
}

size_t WriteStreamCallback(void* contents, size_t size, size_t nmemb, void* userp) 
{
    size_t total_size = size * nmemb;
    std::vector<unsigned char>* buffer = (std::vector<unsigned char> *)userp;
    buffer->insert(buffer->end(), (unsigned char*)contents, (unsigned char*)contents + total_size);
    return total_size;
}

CURLcode ReceiveWebCameraView( const std::string & url )
{
    auto lastCaptureTime = std::chrono::steady_clock::now();
    auto lastPipeTime = std::chrono::steady_clock::now();
    CURLcode res = CURLE_FAILED_INIT;
    while ( !MonitorControl::m_bClose && IsStartReceiving() ) {

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCaptureTime).count();

        if (elapsed >= MonitorControl::m_fRecordInterval) {
            lastCaptureTime = now;

            CURL* curl = curl_easy_init();
            if (!curl) {
                DebugOutput( "cURL initialization failed!");
                return res;
            }

            std::vector<unsigned char> kTempWebCamImageData;
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteStreamCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &kTempWebCamImageData); //&image_data
            curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);

            CURLcode res = curl_easy_perform(curl);
            if (res == CURLE_OK) {
                std::lock_guard<std::mutex> lock( WebCamDataHandler.buffer_mutex );
                *WebCamDataHandler.pWriteBuffer = std::move( kTempWebCamImageData );
                std::swap( WebCamDataHandler.pWriteBuffer, WebCamDataHandler.pReadBuffer );
                WebCamDataHandler.bNewImageAvailable = true;
            }
            curl_easy_cleanup(curl);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return res;

}

CURLcode CheckAMSConnection() {

    bool isConnected = false;
    CURLcode res = CURLE_COULDNT_CONNECT;
    if (m_pCurl) {

        // JSON payload
        json payload;
        payload["jsonrpc"] = "2.0";
        payload["method"] = "printer.gcode.script";
        payload["params"]["script"] = "P28";
        payload["id"] = printer_gcode_script;
        std::string payloadString = payload.dump();
        size_t sent;

        // Connection established, now send the payload
        res = curl_ws_send(m_pCurl, payload.dump().c_str(), strlen(payload.dump().c_str()), &sent, 0, CURLWS_TEXT);

        if (res == CURLE_OK) {

            // Connection established, now set up a loop to wait for responses
            // get response until no data
            auto nowTime = std::chrono::steady_clock::now();
            auto previousTime_printinfo = std::chrono::steady_clock::now();
            long long timeDiff = std::chrono::duration_cast<std::chrono::seconds>(nowTime - previousTime_printinfo).count();

            while (true && (timeDiff < 1.5)) {

                // must add a very samll sleep time for response data
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                //printf("nowTime %d\n", nowTime);
                //printf("previousTime_printinfo %d\n", previousTime_printinfo);
                //printf("timeDiff %d\n", timeDiff);
                // 
                // Set up a buffer to store received data
                // Use curl_ws_recv or similar WebSocket function to receive data

#ifndef __APPLE__
                const struct curl_ws_frame* meta;
#else
                    // Note: curl 8.x requires non-const pointer for curl_ws_recv() fifth parameter
                    // Changed from: const struct curl_ws_frame* meta;
                    // See: https://curl.se/docs/websockets.html - API changed in curl 8.0+
                    struct curl_ws_frame* meta;
#endif
                char buffer[2048];
                size_t rlen;
                res = curl_ws_recv(m_pCurl, buffer, sizeof(buffer), &rlen, &meta);

                if (res == CURLE_OK) {
                    // Data has been received, process the content in 'buffer'
                    //std::cout << "Received data: " << std::string(buffer, rlen) << std::endl;
                    //BOOST_LOG_TRIVIAL(info) << "AMS Received data" << endl;
                    std::wstring ws(&buffer[0], &buffer[2048]);
                    if (true) {

                        // @vance add to store ams connection state for slice (gcode) identification
                        // and to fix the misjudging the state of ams connection with printer
                        // Cmds_CmdP28]AMS\u591a\u8272\u8fde\u63a5\u6210\u529f
                        // Cmds_CmdP28]\\u4e32\\u53e3\\u5df2\\u7ecf\\u6253\\u5f00\\uff0c\\u8fd4\\u56de
                        // \u591a\u8272\u8fde\u63a5\u6210\u529f are unicode characters
                        // text equals to "AMS multi-color connection successful" in simplified chinese
                        // or "\u4e32\u53e3\u5df2\u7ecf\u6253\u5f00\uff0c\u8fd4\u56de"
                        // text equals to "The serial port has been opened, return" in simplified chinese
                        // Several AMS have opened serial ports='1'
                        // \u6709\u51e0\u53f0AMS\u5df2\u7ecf\u6253\u5f00\u4e32\u53e3='1'
                        std::cout << "GOT Data of CURLcode CheckAMSConnection" << std::endl;
                        std::wstring firstTimeToOpen = L"\\u6709\\u51e0\\u53f0AMS\\u5df2\\u7ecf\\u6253\\u5f00\\u4e32\\u53e3='1'";//L"Cmds_CmdP28]AMS\\u591a\\u8272\\u8fde\\u63a5\\u6210\\u529f";
                        std::wstring alreadyOpened = L"Cmds_CmdP28]\\u4e32\\u53e3\\u5df2\\u7ecf\\u6253\\u5f00\\uff0c\\u8fd4\\u56de";
                        size_t id = ws.find(firstTimeToOpen.c_str());
                        if (id != std::string::npos) {
                            isConnected = true;
                            break;
                        }
                        else {
                            id = ws.find(alreadyOpened.c_str());
                            if (id != std::string::npos) {
                                isConnected = true;
                                break;
                            }
                        }
                    }

                    // Break out of the loop or continue processing
                }
                else if (res == CURLE_AGAIN) {
                    ///std::cout << "No Data of CURLcode CheckAMSConnection"<< std::endl;
                    // No data yet, continue the loop
                }
                else {
                    // Handle error or connection closure
                    BOOST_LOG_TRIVIAL(info) << "Handle error of CURLcode CheckAMSConnection" << endl;
                    break;
                }

                nowTime = std::chrono::steady_clock::now();
                timeDiff = std::chrono::duration_cast<std::chrono::seconds>(nowTime - previousTime_printinfo).count();
            }
        }
        else {
            BOOST_LOG_TRIVIAL(info) << "WebSocket connection failed: " << curl_easy_strerror(res) << endl;
            DebugOutput( "WebSocket connection failed: " , curl_easy_strerror(res));
        }

        // Cleanup
        curl_easy_cleanup(m_pCurl);
    }

    if (isConnected) {
        m_bIsConnetedToAMS = true;
    }
    else {
        m_bIsConnetedToAMS = false;
    }

    // @vance add
    // must close the url to prevent curremt response data to interrupt next websocket command and response data
    curl_easy_cleanup(m_pCurl);
    curl_global_cleanup();

    return res;
}

/* close the connection */
void websocket_cleanup()
{
    curl_easy_cleanup(m_pCurl);
}

CURLcode send_action_Command(std::string send_payload)
{
    //CURLcode res = curl_easy_perform(curl);
    CURLcode result = CURLE_AGAIN;
    double connectTime = 0;
    try {
        curl_easy_getinfo(m_pCurl_websocket, CURLINFO_CONNECT_TIME, &connectTime);
        if (connectTime > 0)
        {
            size_t sent;
            result = curl_ws_send(m_pCurl_websocket, send_payload.c_str(), strlen(send_payload.c_str()), &sent, 0, CURLWS_TEXT);
        }
        //curl_easy_cleanup(curl);
    }
    catch (const std::exception& e) {
        DebugOutput( "send error: " , e.what());
    }
    return result;
}

CURLcode CheckReceiveValue(const char* exected_payload)
{
    size_t rlen;

#ifndef __APPLE__
    const struct curl_ws_frame* meta;
#else
    // Note: curl 8.x requires non-const pointer - API breaking change
    struct curl_ws_frame* meta;
#endif
    char buffer[256];
    CURLcode result = curl_ws_recv(m_pCurl, buffer, sizeof(buffer), &rlen, &meta);
    if (result == CURLE_OK) {
        if (meta->flags & CURLWS_PONG) {
            int same = 0;
            fprintf(stderr, "ws: got PONG back\n");
            if (rlen == strlen(exected_payload)) {
                if (!memcmp(exected_payload, buffer, rlen)) {
                    fprintf(stderr, "ws: got the same payload back\n");
                    same = 1;
                }
            }
            if (!same)
                fprintf(stderr, "ws: did NOT get the same payload back\n");
        }
        else {
            fprintf(stderr, "recv_pong: got %u bytes rflags %x\n", (int)rlen,
                meta->flags);
        }
    }
    fprintf(stderr, "ws: curl_ws_recv returned %u, received %u\n",
        (unsigned int)result, (unsigned int)rlen);
    return result;
}

std::wstring CheckReceiveValue_new(std::wstring expected)
{
    CURLcode res = Initialconnect();
    std::wstring message;
    if (res == CURLE_OK)
    {

        auto nowTime = std::chrono::steady_clock::now();
        auto previousTime_printinfo = std::chrono::steady_clock::now();
        long long timeDiff = std::chrono::duration_cast<std::chrono::seconds>(nowTime - previousTime_printinfo).count();

        size_t rlen;
#ifndef __APPLE__
        const struct curl_ws_frame* meta;
#else
        // Note: curl 8.x requires non-const pointer - API breaking change
        struct curl_ws_frame* meta;
#endif
        char buffer[2048];
        CURLcode result = curl_ws_recv(m_pCurl, buffer, sizeof(buffer), &rlen, &meta);

        int messageIsEmpty = 0;
        while (true && timeDiff < 600 && m_bReceiving /*&& times < 5*/)
        {
            try {
                memset(buffer, 0, sizeof(buffer));

                result = curl_ws_recv(m_pCurl, buffer, sizeof(buffer), &rlen, &meta);

                //std::wstring ws(&buffer[0], &buffer[2048]);
                std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
                std::wstring ws = converter.from_bytes(buffer, buffer + rlen);

                //std::wstring ws(buffer, buffer + rlen / sizeof(wchar_t));
                message = ws;
                m_kMonitorWindow.receiveMessage = ws;
                BOOST_LOG_TRIVIAL(info) << "CheckReceiveValue_new" << ws.c_str() << endl;

                if (message.empty())
                {
                    messageIsEmpty++;
                    if (messageIsEmpty > 10)
                    {
                        m_pPrinterInfo->state = "offline";
                        break;
                    }
                }
                else
                {
                    messageIsEmpty = 0;
                }
                if (m_pPrinterInfo->state == "offline")
                    break;

                std::wstring _expected1 = L"Probe samples exceed samples_tolerance";
                size_t id = ws.find(_expected1.c_str());
                if (id != std::string::npos) {
                    break;
                }
                std::wstring _expected2 = L"Mesh Bed Leveling Complete";
                size_t id2 = ws.find(_expected2.c_str());
                if (id2 != std::string::npos) {
                    break;
                }
                std::wstring _expected3 = L"Klipper state: Disconnect";
                size_t id3 = ws.find(_expected3.c_str());
                if (id3 != std::string::npos) {
                    break;
                }
            }
            catch (...) {

                BOOST_LOG_TRIVIAL(info) << "receive error" << endl;
            }

            nowTime = std::chrono::steady_clock::now();
            timeDiff = std::chrono::duration_cast<std::chrono::seconds>(nowTime - previousTime_printinfo).count();
            // must add a very samll sleep time for response data
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    //finish and close curl         
    curl_easy_cleanup(m_pCurl);
    curl_global_cleanup();
    return message;
}

CURLcode CheckReceiveValue_AMS(const char* exected_payload)
{
    size_t rlen;
#ifndef __APPLE__
    const struct curl_ws_frame* meta;
#else
    // Note: curl 8.x requires non-const pointer - API breaking change
    struct curl_ws_frame* meta;
#endif
    char buffer[2048];
    CURLcode result = curl_ws_recv(m_pCurl, buffer, sizeof(buffer), &rlen, &meta);
    std::wstring ws(&buffer[0], &buffer[2048]);
    if (result == CURLE_OK) {
        char* pch;
        pch = strstr(buffer, exected_payload);
        if (pch != NULL)
        {
            std::string entry_state = pch;
            entry_state = entry_state.substr(entry_state.find_first_of(":") + 2);
            std::string park_state = entry_state;
            entry_state = entry_state.erase(entry_state.find_first_of(","), entry_state.length());
            park_state = park_state.substr(park_state.find_first_of(":") + 2, 1);

            int _entry_state = std::stoi(entry_state);
            int _park_state = std::stoi(park_state);
            m_kAMSList_temp.clear();
            for (int i = 1; i <= 4; i++)
            {
                AMSInfo _AMSInfo;
                //_AMSInfo.color = ImColor(47, 53, 50, 255);
                _AMSInfo.filament = "";
                //if (i == 1 && _park_state == 1)
                //    _AMSInfo.loading = true;
                //else if (i == 2 && _park_state == 2)
                //    _AMSInfo.loading = true;
                //else if (i == 3 && _park_state == 4)
                //    _AMSInfo.loading = true;
                //else if (i == 4 && _park_state == 8)
                //    _AMSInfo.loading = true;

                for (int j = 0; j < 4; ++j) {
                    if (_park_state & (1 << j)) {
                        if (j + 1 == i)
                        {
                            _AMSInfo.loading = true;
                        }
                        std::cout << "gangway " << (j + 1) << " Buffer wire\n";
                    }
                }


                //AMS 1-1 2-2: 3-4 4-8
                if (i == 1 && (_entry_state == 1 || _entry_state == 3 || _entry_state == 5 || _entry_state == 9 || _entry_state == 7 || _entry_state == 11 || _entry_state == 13 || _entry_state == 15))
                    _AMSInfo.entry = true;
                if (i == 2 && (_entry_state == 2 || _entry_state == 3 || _entry_state == 6 || _entry_state == 10 || _entry_state == 7 || _entry_state == 11 || _entry_state == 14 || _entry_state == 15))
                    _AMSInfo.entry = true;
                if (i == 3 && (_entry_state == 4 || _entry_state == 5 || _entry_state == 6 || _entry_state == 12 || _entry_state == 7 || _entry_state == 13 || _entry_state == 14 || _entry_state == 15))
                    _AMSInfo.entry = true;
                if (i == 4 && (_entry_state == 8 || _entry_state == 9 || _entry_state == 10 || _entry_state == 12 || _entry_state == 11 || _entry_state == 13 || _entry_state == 14 || _entry_state == 15))
                    _AMSInfo.entry = true;

                _AMSInfo.selected = false;
                m_kAMSList_temp.push_back(_AMSInfo);
            }
            m_kAMSList = m_kAMSList_temp;
        }
        else
        {
            return CURLE_AGAIN;
            fprintf(stderr, "ws: did NOT get the same payload back\n");
        }
    }
    //fprintf(stderr, "ws: curl_ws_recv returned %u, received %u\n",
    //    (unsigned int)result, (unsigned int)rlen);
    return result;
}

size_t WriteBinaryData(void* buffer, size_t size, size_t nmemb, void* lpVoid)
{
    size_t no;
    no = fwrite(buffer, size, nmemb, static_cast<FILE*>(lpVoid));


    return no;
}
size_t WriteMemoryCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t realsize = size * nmemb;
    struct PhrozenMemoryStruct* mem = (struct PhrozenMemoryStruct*)userp;

    void* ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (ptr == NULL) {
        /* out of memory! */
        printf("not enough memory (realloc returned NULL)\n");
        return 0;
    }
    mem->memory = (char*)ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}
size_t WriteCallback_test(void* contents, size_t size, size_t nmemb, std::vector<unsigned char>* data) {
    size_t numBytes = size * nmemb;

    std::vector<unsigned char>& imgData = *data;
    imgData.insert(imgData.end(), static_cast<unsigned char*>(contents), static_cast<unsigned char*>(contents) + numBytes);

    m_bFirst = false;
    return numBytes;
}
size_t WriteData_test(void* buffer, size_t size, size_t nmemb, void* lpVoid)
{
    std::string* str = dynamic_cast<std::string*>((std::string*)lpVoid);
    if (NULL == str || NULL == buffer)
    {
        return -1;
    }

    char* pData = (char*)buffer;
    str->append(pData, size * nmemb);

    return nmemb;
}
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::ofstream* file) {

    size_t numBytes = size * nmemb;
    file->write(static_cast<char*>(contents), numBytes);

    //std::vector<uchar> imgData;
    //imgData.insert(imgData.end(), static_cast<uchar*>(contents), static_cast<uchar*>(contents) + numBytes);
    //cv::Mat frame = cv::imdecode(imgData, cv::IMREAD_COLOR);

    // Display the frame
    //cv::imshow("MJPEG Stream", frame);
    return numBytes;
}

// Callback function to track download progress
int DownloadProgressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    // Calculate the download progress percentage
    if (dltotal > 0) {

        m_fProgressValue = dlnow * 100.0f / dltotal;
        std::cout << "dlnow: " << dlnow << std::endl;
        std::cout << "dltotal: " << dltotal << std::endl;
        std::cout << "Download Progress: " << m_fProgressValue << "%" << std::endl;
    }
    return 0;
}


// Output detailed logs and communication content
int CURLDebug(CURL*, curl_infotype type, char* data, size_t size, void*) {
    switch (type) {
    case CURLINFO_TEXT:
        DebugOutput( "== Info: " , data);
        break;
    case CURLINFO_HEADER_OUT:
        DebugOutput( "=> Send header: " , data);
        break;
    case CURLINFO_DATA_OUT:
        DebugOutput( "=> Send data: " , data);
        break;
    case CURLINFO_HEADER_IN:
        DebugOutput( "<= Recv header: " , data);
        break;
    case CURLINFO_DATA_IN:
        DebugOutput( "<= Recv data: " , data);
        break;
    default: // other information
        return 0;
    }
    return 0;
}

void GetPrinterInfo_websocket()
{
    json payload;
    payload["jsonrpc"] = "2.0";
    payload["method"] = "printer.objects.query";
    payload["params"] = {
        {"objects", {
            {"extruder", {"temperature", "target"}},
            {"fan", {"speed"}},
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
    payload["id"] = printer_gcode_script;
    CURLcode result = send_action_Command(payload.dump());
}

CURLcode printfile(std::string filename)
{
    bool _result = false;
    CURL* curl;
    CURLcode res;
    m_nSendJobSuccess = 0;
    m_pWebServiceInfo->responseData = make_unique<std::string>();
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (curl) {

        //1. Set up communication protocols, routing, RESTful APIs, and CRUD tasks
        std::string url = "http://" + m_pWebServiceInfo->ip + m_pWebServiceInfo->port_device + "/api/files/local";
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_DEFAULT_PROTOCOL, "https");
        struct curl_slist* headers = NULL;
        //headers = curl_slist_append(headers, "X-Api-Key: 4f10ca9726ce4cb083108e17317ec0db");//807830FADD7249A29FEBF94371436AA1");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        //2. Set file location and machine execution command
        curl_mime* mime;
        curl_mimepart* part;
        mime = curl_mime_init(curl);
        //2.1. set upload file info
        part = curl_mime_addpart(mime);
        curl_mime_name(part, "file");
        curl_mime_filedata(part, filename.c_str());
        //2.2. set print command
        part = curl_mime_addpart(mime);
        curl_mime_name(part, "print");
        curl_mime_data(part, "true", CURL_ZERO_TERMINATED);
        curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

        //3. set url response info
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fnWriteData);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, m_pWebServiceInfo->responseData.get());
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
        //curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3);
        //curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20);

        //4. set the progress callback function
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);


        // Callback function to track upload progress
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, fnUploadProgressCallback);
        

        //5. set upload min/max rate (bytes/second)
        // max - 16MB/second
        // min - 8MB/second
        //curl_easy_setopt(curl, CURLOPT_MAX_SEND_SPEED_LARGE, 16777216L);
        //curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 8388608L);

        //@vance add to trace and debug via internet commuication info
        //6.1. Enable the log function to output basic communication information
        //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

        //6.2. Set the callback function to output detailed logs and communication content
        //curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, CURLDebug);

        res = curl_easy_perform(curl);

        if (res == CURLE_OK)
        {
            m_pWebServiceInfo->jsonReturnInfoData = json::parse(*m_pWebServiceInfo->responseData.get());
            BOOST_LOG_TRIVIAL(info) << m_pWebServiceInfo->jsonReturnInfoData["action"].get<std::string>() << endl;
            if (!m_pWebServiceInfo->jsonReturnInfoData["action"].is_null())
            {
                if (m_pWebServiceInfo->jsonReturnInfoData["action"] == "create_file")
                {
                    _result = true;
                    BOOST_LOG_TRIVIAL(info) << "HTTP PRINT OK" << endl;
                    m_nSendJobSuccess = 1;
                }
            }
        }
        else
        {
            BOOST_LOG_TRIVIAL(error) << "HTTP PRINT FAILED" << endl;
            //@vance add to get details about curl execution
            BOOST_LOG_TRIVIAL(error) << "CURL Error: " << curl_easy_strerror(res) << std::endl;
            _result = false;
            m_nSendJobSuccess = 2;
        }
        curl_mime_free(mime);
    }
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    if (m_nSendJobSuccess)
    {

    }
    return res;
}

HttpErrorInfo ParseHttpErrorResponse(const json& response_json, int http_status_code) {
    HttpErrorInfo error_info;
    error_info.http_status_code = http_status_code;
    
    // Check if HTTP status indicates error
    if (http_status_code >= 400) {
        error_info.has_error = true;
    }
    
    // Safely check for "error" field in JSON
    auto error_iter = response_json.find("error");
    if (error_iter == response_json.end() || error_iter->is_null()) {
        return error_info; // No error field found
    }
    
    error_info.has_error = true;
    const json& error_obj = *error_iter;
    
    // Extract outer error fields
    try {
        if (error_obj.contains("code") && !error_obj["code"].is_null()) {
            error_info.error_code = error_obj["code"].get<int>();
        }
        
        if (error_obj.contains("message") && !error_obj["message"].is_null()) {
            error_info.raw_message = error_obj["message"].get<std::string>();
        }
        
        if (error_obj.contains("traceback") && !error_obj["traceback"].is_null()) {
            error_info.traceback = error_obj["traceback"].get<std::string>();
        }
    } catch (const json::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << "Failed to extract outer error fields: " << e.what();
    }
    
    // Try to parse inner JSON from error.message
    if (!error_info.raw_message.empty()) {
        try {
            json inner_json = json::parse(error_info.raw_message);
            
            // Extract inner error type
            if (inner_json.contains("error") && !inner_json["error"].is_null()) {
                error_info.error_type = inner_json["error"].get<std::string>();
            }
            
            // Extract inner error message (preferred)
            if (inner_json.contains("message") && !inner_json["message"].is_null()) {
                error_info.error_message = inner_json["message"].get<std::string>();
                // Replace escaped newlines with actual newlines
                size_t pos = 0;
                while ((pos = error_info.error_message.find("\\n", pos)) != std::string::npos) {
                    error_info.error_message.replace(pos, 2, "\n");
                    pos += 1;
                }
            }
        } catch (const json::exception& e) {
            // Inner JSON parsing failed, use outer message
            BOOST_LOG_TRIVIAL(debug) << "Inner JSON parse failed, using outer message: " << e.what();
            error_info.error_message = error_info.raw_message;
        }
    }
    
    return error_info;
}

bool doAction_http(std::string script, std::string  exected_payload, int timeout)
{
    m_bStartlistening = true;
    bool _result = false;
    CURL* curl;
    CURLcode result;
    m_pWebServiceInfo->responseData = make_unique<std::string>();
    //1. curl initialization
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (curl) {

        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
        std::string url = "http://" + m_pWebServiceInfo->ip + m_pWebServiceInfo->port_device + "/printer/gcode/script?script=" + script;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_DEFAULT_PROTOCOL, "https");


        //4. set url response info
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fnWriteData);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, m_pWebServiceInfo->responseData.get());
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);

        BOOST_LOG_TRIVIAL(error) << exected_payload << endl;
        result = curl_easy_perform(curl);
        
        int http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        if (result == CURLE_OK)
        {
            m_pWebServiceInfo->jsonReturnInfoData = json::parse(*m_pWebServiceInfo->responseData.get());
            
            // Debug: Print entire JSON response
            std::string json_debug = m_pWebServiceInfo->jsonReturnInfoData.dump(4);
            BOOST_LOG_TRIVIAL(debug) << "=== JSON Response Debug ===" << endl;
            BOOST_LOG_TRIVIAL(debug) << "Full JSON content: " << json_debug << endl;
            BOOST_LOG_TRIVIAL(debug) << "===========================" << endl;
            
            // Also output to Xcode console
            std::cout << "=== JSON Response Debug ===" << std::endl;
            std::cout << "Full JSON content: " << json_debug << std::endl;
            std::cout << "===========================" << std::endl;
            
            if (m_pWebServiceInfo->jsonReturnInfoData["result"] == exected_payload)
            {
                _result = true;
                BOOST_LOG_TRIVIAL(info) << "HTTP OK" << endl;
            }
            else{
                error_info = ParseHttpErrorResponse(m_pWebServiceInfo->jsonReturnInfoData, http_code);
            }
        }
        else
        {
            _result = false;
        }
    }
    else
        _result = false;

    //6. finish and close curl         
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    m_bStartlistening = false;
    return _result;
}

CURLcode doAction(std::string method, std::string script, int id)
{
    m_bStartlistening = true;
    //Initialconnect();

    json payload;
    payload["jsonrpc"] = "2.0";
    payload["method"] = method;
    if (!script.empty())
        payload["params"]["script"] = script;
    payload["id"] = id;

    CURLcode result = send_action_Command(payload.dump());

    //if (result == CURLE_OK)
    //{
    //    result = CheckReceiveValue("ok");
    //}
    m_bStartlistening = false;
    return result;
}

CURLcode GetAMSInfo()
{
    m_bStartlistening = true;
    Initialconnect();

    json payload;
    payload["jsonrpc"] = "2.0";
    payload["method"] = "printer.gcode.script";
    payload["params"]["script"] = "P28";
    payload["id"] = printer_gcode_script;

    CURLcode result;
    if (m_bInitial_P28)
    {
        // @vance add to check ams is pluged into 3D printer or not
        result = CheckAMSConnection();
        //result = send_action_Command(payload.dump());
        //initial_P28 = false;
    }
    else
        result = CURLE_OK;

    if (result == CURLE_OK && m_bIsConnetedToAMS)
    {
        Initialconnect();
        payload["params"]["script"] = "P114";
        CURLcode result = send_action_Command(payload.dump());

        // @vance add 
        // use real time-out to make sure we get the real response data from the websocket command
        // wait until result is CURLE_OK or over 1.2 seconds
        auto nowTime = std::chrono::steady_clock::now();
        auto previousTime_printinfo = std::chrono::steady_clock::now();
        long long timeDiff = std::chrono::duration_cast<std::chrono::seconds>(nowTime - previousTime_printinfo).count();
        int times = 0;
        if (result == CURLE_OK)
        {
            result = CheckReceiveValue_AMS("entry_state");
            while (result == CURLE_AGAIN && timeDiff < 1.2 /*&& times < 5*/)
            {
                result = CheckReceiveValue_AMS("entry_state");
                times++;
                nowTime = std::chrono::steady_clock::now();
                timeDiff = std::chrono::duration_cast<std::chrono::seconds>(nowTime - previousTime_printinfo).count();
                // must add a very samll sleep time for response data
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    //finish and close curl         
    curl_easy_cleanup(m_pCurl);
    curl_global_cleanup();
    m_bStartlistening = false;

    return result;
}

CURLcode GetAMSInfo_websocket()
{
    //Initialconnect();

    json payload;
    payload["jsonrpc"] = "2.0";
    payload["method"] = "printer.gcode.script";
    payload["params"]["script"] = "P28";
    payload["id"] = printer_gcode_script;
    CURLcode result = send_action_Command(payload.dump());

    //Initialconnect();
    payload["params"]["script"] = "P114";
    result = send_action_Command(payload.dump());

    return result;
}

void GetPrinterInfo()
{
    CURL* _curl;
    CURLcode result;
    m_pWebServiceInfo->responseData = make_unique<std::string>();
    //1. curl initialization
    curl_global_init(CURL_GLOBAL_DEFAULT);
    _curl = curl_easy_init();
    if (_curl) {

        //std::ifstream inputFile("C:/Users/heidi.hsieh/Desktop/info.json");  // Replace with the actual JSON file name
        //
        //if (inputFile.is_open()) {
        //    inputFile >> webServiceInfo->jsonPrinterInfoData;
        //}
        // 
        //2. set url basic info, must use c_str()
        curl_easy_setopt(_curl, CURLOPT_CUSTOMREQUEST, "GET");
        std::string url = "http://" + m_pWebServiceInfo->ip + m_pWebServiceInfo->port_device + "/printer/objects/query?extruder=temperature,target&fan=speed&heater_bed=temperature,target&gcode_move=speed_factor,homing_origin&display_status&print_stats&pause_resume&toolhead=homed_axes&temperature_sensor Chamber_sensor=temperature&output_pin fan_assist=value&fan_generic Chamber_fan=speed";
        curl_easy_setopt(_curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(_curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(_curl, CURLOPT_DEFAULT_PROTOCOL, "https");


        //4. set url response info
        curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, fnWriteData);
        curl_easy_setopt(_curl, CURLOPT_WRITEDATA, m_pWebServiceInfo->responseData.get());
        curl_easy_setopt(_curl, CURLOPT_NOSIGNAL, 1);
        curl_easy_setopt(_curl, CURLOPT_CONNECTTIMEOUT, 3);
        curl_easy_setopt(_curl, CURLOPT_TIMEOUT, 10);

        result = curl_easy_perform(_curl);

        //if (true)
        if (result == CURLE_OK)
        {
            if (!m_pWebServiceInfo->responseData.get()->empty())
            {
                if (m_pWebServiceInfo->responseData.get()->find("<html>") == -1)
                    m_pWebServiceInfo->jsonPrinterInfoData = json::parse(*m_pWebServiceInfo->responseData.get());

                // @vance add
                // check "error" object content of json existing or not by find function of json class 
                // to avoid crash when checking by using json object directly  
                json::iterator iter = m_pWebServiceInfo->jsonPrinterInfoData.find("error");
                if (iter != m_pWebServiceInfo->jsonPrinterInfoData.end()) {
                    if (!m_pWebServiceInfo->jsonPrinterInfoData["error"].is_null())
                    {
                        BOOST_LOG_TRIVIAL(error) << "error" << m_pWebServiceInfo->jsonPrinterInfoData["error"]["message"].get<std::string>() << endl;
                        m_pPrinterInfo->error = "error";

                    }
                }
                else if (m_pWebServiceInfo->jsonPrinterInfoData.is_object() && !m_pWebServiceInfo->jsonPrinterInfoData.empty())
                {
                    if (!m_pWebServiceInfo->jsonPrinterInfoData["result"].is_null())
                    {
                        m_pPrinterInfo->extruder_temperature = m_pWebServiceInfo->jsonPrinterInfoData["result"]["status"]["extruder"]["temperature"];
                        m_pPrinterInfo->bed_temperature = m_pWebServiceInfo->jsonPrinterInfoData["result"]["status"]["heater_bed"]["temperature"];
                        m_pPrinterInfo->extruder_temperature_target = m_pWebServiceInfo->jsonPrinterInfoData["result"]["status"]["extruder"]["target"];
                        m_pPrinterInfo->bed_temperature_target = m_pWebServiceInfo->jsonPrinterInfoData["result"]["status"]["heater_bed"]["target"];
                        if (!m_pWebServiceInfo->jsonPrinterInfoData["result"]["status"]["fan"]["speed"].is_null())
                            m_pPrinterInfo->fan_speed = m_pWebServiceInfo->jsonPrinterInfoData["result"]["status"]["fan"]["speed"];
                        m_pPrinterInfo->print_speed = m_pWebServiceInfo->jsonPrinterInfoData["result"]["status"]["gcode_move"]["speed_factor"];
                        m_pPrinterInfo->home_axes = m_pWebServiceInfo->jsonPrinterInfoData["result"]["status"]["toolhead"]["homed_axes"].get<std::string>();

                        json status = m_pWebServiceInfo->jsonPrinterInfoData["result"]["status"];
                        m_pPrinterInfo->print_progress = status["display_status"]["progress"];
                        m_pPrinterInfo->is_paused = status["pause_resume"]["is_paused"];
                        m_pPrinterInfo->state = status["print_stats"]["state"].get<std::string>();
                        m_pPrinterInfo->print_file = status["print_stats"]["filename"].get<std::string>();
                        m_pPrinterInfo->print_time = status["print_stats"]["print_duration"];
                        m_pPrinterInfo->total_time = status["print_stats"]["total_duration"];
                        m_pPrinterInfo->print_filament = status["print_stats"]["filament_used"];
                        m_pPrinterInfo->error = "";
                        m_pPrinterInfo->z_offsetValure = status["gcode_move"]["homing_origin"][2];

                    }
                }
            }
            //inputFile.close();
        }
        else if (result == CURLE_OPERATION_TIMEDOUT)
        {
            m_pPrinterInfo->state = "offline";
        }
    }
    else {
        //return CURLE_FAILED_INIT;
    }

    //6. finish and close curl         
    curl_easy_cleanup(_curl);
    curl_global_cleanup();
    //return result;
}

void GetAllInfo_websocket()
{
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
    payload["id"] = printer_gcode_script;

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
    payload_AMS["id"] = printer_gcode_script;
    
    //Nozzle
    //to check the filament is existing in the nozzle or not
    json payload_Nozzle;
    payload_Nozzle["jsonrpc"] = "2.0";
    payload_Nozzle["method"] = "printer.gcode.script";
    payload_Nozzle["params"]["script"] = "PRZ_ADC";
    payload_Nozzle["id"] = printer_gcode_script;

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
                // ⚠️ CRITICAL: libcurl easy handle is NOT thread-safe
                // Cannot call curl_ws_send()/curl_ws_recv() from multiple threads simultaneously
                // Operating the same curl handle from 2 threads may cause crash risk
                std::lock_guard<std::mutex> lock(m_kCurlMutex);
                BOOST_LOG_TRIVIAL(debug) << "GetAllInfo_websocket: Lock acquired, Thread ID: " << thread_id;
                
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
                    threadControl.first_time_to_send_query = false;
                    previousTime = std::chrono::steady_clock::now();
                }
                BOOST_LOG_TRIVIAL(debug) << "GetAllInfo_websocket: Lock released, Thread ID: " << thread_id;
            }// ✅ Lock released from here before sleep to avoid blocking ReceiveResponse() thread
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

    } catch (const std::invalid_argument& e) {
        DebugOutput( "Caught std::invalid_argument: " , e.what());
    } catch (const std::exception& e) {
        DebugOutput( "Caught std::exception: " , e.what() );
    }

}

void GetHistoryInfo()
{
    bool offline_test = false;
    if (!offline_test)
    {
        if (m_pWebServiceInfo->ip == "")
            return;
    }

    CURL* curl;
    CURLcode result;
    m_pWebServiceInfo->responseData = make_unique<std::string>();
    //1. curl initialization
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (curl) {

        if (offline_test)
        {
            std::ifstream inputFile("C:/Users/heidi.hsieh/Desktop/history.json");  // Replace with the actual JSON file name
            if (inputFile.is_open()) {
                inputFile >> m_pWebServiceInfo->jsonHistoryInfoData;
            }
        }

        //m_pWebServiceInfo->ip = "192.168.1.100";
        //2. set url basic info, must use c_str()
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
        std::string url = "http://" + m_pWebServiceInfo->ip + m_pWebServiceInfo->port_device + "/server/history/list";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_DEFAULT_PROTOCOL, "https");

        //4. set url response info
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fnWriteData);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, m_pWebServiceInfo->responseData.get());
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5);

        result = curl_easy_perform(curl);


        if (result == CURLE_OK || offline_test)
        {
            std::vector<HistoryInfo> _historyInfoList;
            if (!offline_test)
                m_pWebServiceInfo->jsonHistoryInfoData = json::parse(*m_pWebServiceInfo->responseData.get());
            json j = m_pWebServiceInfo->jsonHistoryInfoData;

            if (j["result"]["jobs"].is_array()) {
                for (const auto& job : j["result"]["jobs"]) {
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
            }
            else {
                m_kHistoryInfoList.clear();
                DebugOutput( "Invalid JSON format or missing 'jobs' array." );
            }
            //inputFile.close();
        }
    }
    else {
        //return CURLE_FAILED_INIT;
    }

    //6. finish and close curl         
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    //return result;
}

void GetHistoryInfo_websocket()
{
    //Initialconnect();
    json payload;
    payload["jsonrpc"] = "2.0";
    payload["method"] = "server.history.list";
    payload["id"] = 5656;

    CURLcode result = send_action_Command(payload.dump());

}

void GetThumbnailInfo(std::string gcode)
{
    CURL* curl;
    CURLcode result;
    m_pWebServiceInfo->responseData = make_unique<std::string>();
    //1. curl initialization
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (curl) {

        //2. set url basic info, must use c_str()
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
        
        // URL encode the filename for query parameter
        char* encoded_gcode = curl_easy_escape(curl, gcode.c_str(), gcode.length());
        if (!encoded_gcode) {
            BOOST_LOG_TRIVIAL(error) << "GetThumbnailInfo: Failed to URL encode filename: " << gcode;
            curl_easy_cleanup(curl);
            curl_global_cleanup();
            return;
        }
        
        std::string url = "http://" + m_pWebServiceInfo->ip + m_pWebServiceInfo->port_device + "/server/files/thumbnails?filename=" + std::string(encoded_gcode);
        curl_free(encoded_gcode);  // Free the memory allocated by curl_easy_escape
        
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_DEFAULT_PROTOCOL, "https");

        //4. set url response info
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fnWriteData);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, m_pWebServiceInfo->responseData.get());
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5);

        BOOST_LOG_TRIVIAL(info) << "GetThumbnailInfo: Requesting thumbnail info from URL: " << url;
        result = curl_easy_perform(curl);

        if (result == CURLE_OK)
        {
            // Log the raw response for debugging
            std::string raw_response = *m_pWebServiceInfo->responseData.get();
            std::cout << "[GetThumbnailInfo] Raw JSON response (full): " << raw_response << std::endl;
            BOOST_LOG_TRIVIAL(debug) << "GetThumbnailInfo: Raw response: " << raw_response;
            
            m_pWebServiceInfo->jsonThumbnailsInfoData = json::parse(raw_response);
            json j = m_pWebServiceInfo->jsonThumbnailsInfoData;

            if (j["result"].is_array() && !j["result"].empty()) {
                // Find the largest thumbnail (prefer larger sizes for better quality)
                std::string largest_thumbnail_path;
                int largest_size = 0;
                
                for (const auto& result_item : j["result"]) {
                    if (result_item.contains("thumbnail_path")) {
                        std::string thumbnail_path = result_item["thumbnail_path"].get<std::string>();
                        
                        // Try to extract size from path (e.g., "thumbnails/TEST.gcode.32x30.png")
                        int size = 0;
                        if (result_item.contains("width") && result_item.contains("height")) {
                            int width = result_item["width"].get<int>();
                            int height = result_item["height"].get<int>();
                            size = width * height;  // Use area as size metric
                        } else {
                            // If no size info, estimate from filename
                            size_t pos = thumbnail_path.find_last_of('.');
                            if (pos != std::string::npos) {
                                std::string base = thumbnail_path.substr(0, pos);
                                size_t x_pos = base.find_last_of('x');
                                if (x_pos != std::string::npos) {
                                    try {
                                        int w = std::stoi(base.substr(base.find_last_not_of("0123456789x", x_pos) + 1, x_pos));
                                        int h = std::stoi(base.substr(x_pos + 1));
                                        size = w * h;
                                    } catch (...) {
                                        size = 0;
                                    }
                                }
                            }
                        }
                        
                        std::cout << "[GetThumbnailInfo] Found thumbnail_path: \"" << thumbnail_path 
                                  << "\" (estimated size: " << size << ")" << std::endl;
                        
                        if (size > largest_size) {
                            largest_size = size;
                            largest_thumbnail_path = thumbnail_path;
                        }
                    }
                }
                
                if (!largest_thumbnail_path.empty()) {
                    m_pPrinterInfo->thumbnail_path = largest_thumbnail_path;
                    std::cout << "[GetThumbnailInfo] Selected largest thumbnail: \"" << largest_thumbnail_path 
                              << "\" (size: " << largest_size << ")" << std::endl;
                    BOOST_LOG_TRIVIAL(info) << "GetThumbnailInfo: Selected largest thumbnail_path: \"" << largest_thumbnail_path << "\"";
                } else {
                    // Fallback: use first thumbnail if no size info available
                    for (const auto& result_item : j["result"]) {
                        if (result_item.contains("thumbnail_path")) {
                            m_pPrinterInfo->thumbnail_path = result_item["thumbnail_path"].get<std::string>();
                            std::cout << "[GetThumbnailInfo] Using first thumbnail (no size info): \"" 
                                      << m_pPrinterInfo->thumbnail_path << "\"" << std::endl;
                            BOOST_LOG_TRIVIAL(info) << "GetThumbnailInfo: Found thumbnail_path: \"" << m_pPrinterInfo->thumbnail_path << "\"";
                            break;
                        }
                    }
                }
                
                // If no thumbnail_path found in array, log warning
                if (m_pPrinterInfo->thumbnail_path.empty()) {
                    BOOST_LOG_TRIVIAL(warning) << "GetThumbnailInfo: No thumbnail_path found in result array for gcode: \"" << gcode << "\"";
                    BOOST_LOG_TRIVIAL(debug) << "GetThumbnailInfo: Full JSON response: " << j.dump(2);
                }
            }
            else {
                BOOST_LOG_TRIVIAL(warning) << "GetThumbnailInfo: Invalid JSON format or empty result array for gcode: \"" << gcode << "\"";
                BOOST_LOG_TRIVIAL(debug) << "GetThumbnailInfo: Full JSON response: " << j.dump(2);
                DebugOutput("Invalid JSON format or missing 'result' array.");
            }
        }
        else {
            BOOST_LOG_TRIVIAL(error) << "GetThumbnailInfo: CURL request failed: " << curl_easy_strerror(result);
        }
    }
    else {
        BOOST_LOG_TRIVIAL(error) << "GetThumbnailInfo: Failed to initialize CURL";
    }

    //6. finish and close curl         
    curl_easy_cleanup(curl);
    curl_global_cleanup();
}

bool GetThumbnailImage(std::string printingfile)
{
    CURL* curl;
    FILE* fp;
    CURLcode result;
    bool res = false;
    m_pWebServiceInfo->responseData = make_unique<std::string>();
    //1. curl initialization
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (curl) {
        //2. set url basic info, must use c_str()
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
        
        // URL encode the thumbnail path to handle special characters
        char* encoded_path = curl_easy_escape(curl, m_pPrinterInfo->thumbnail_path.c_str(), m_pPrinterInfo->thumbnail_path.length());
        if (!encoded_path) {
            BOOST_LOG_TRIVIAL(error) << "GetThumbnailImage: Failed to URL encode thumbnail path: " << m_pPrinterInfo->thumbnail_path;
            curl_easy_cleanup(curl);
            curl_global_cleanup();
            return false;
        }
        
        std::string url = "http://" + m_pWebServiceInfo->ip + m_pWebServiceInfo->port_device + "/server/files/gcodes/" + std::string(encoded_path);
        curl_free(encoded_path);  // Free the memory allocated by curl_easy_escape
        
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_DEFAULT_PROTOCOL, "https");


        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        std::wstring w_printingfile = converter.from_bytes(printingfile);
        //4. set url response info
        //fp = fopen(printingfile.c_str(), "wb");
        //fp = _wfopen(w_printingfile.c_str(), L"wb");
#if defined(_WIN32) || defined(_WIN64)
            fp = _wfopen(w_printingfile.c_str(), L"wb");
#elif defined(__APPLE__)
            fp = fopen(printingfile.c_str(), "wb");
#elif defined(__linux__)
#else
#endif
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fnWriteData_file);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);

        BOOST_LOG_TRIVIAL(info) << "GetThumbnailImage: " << url << endl;
        result = curl_easy_perform(curl);

        if (result == CURLE_OK)
        {
            BOOST_LOG_TRIVIAL(info) << "GetThumbnailImage OK" << endl;
            res = true;
        }
    }
    else {
        //return CURLE_FAILED_INIT;
        res = false;
    }

    //6. finish and close curl         
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    //@vance add to avoid crash when fp is null pointer
    if (fp) {
        fclose(fp);
    }
    return res;
}

// Memory write callback for thumbnail download
static size_t WriteThumbnailMemoryCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t realsize = size * nmemb;
    std::vector<unsigned char>* mem = static_cast<std::vector<unsigned char>*>(userp);
    
    if (mem == nullptr) {
        return 0;
    }
    
    // Append data to vector
    const unsigned char* data = static_cast<const unsigned char*>(contents);
    mem->insert(mem->end(), data, data + realsize);
    
    return realsize;
}

bool GetThumbnailImageInMemory(const std::string& gcodeName, std::vector<unsigned char>& thumbnail_data)
{
    thumbnail_data.clear();
    
    // ============================================
    // Step 1: Get thumbnail path information
    // ============================================
    try {
        GetThumbnailInfo(gcodeName);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "GetThumbnailImageInMemory: "
                                 << "Failed to get thumbnail info for gcode: " 
                                 << gcodeName << ", error: " << e.what();
        return false;
    }
    
    // Check if thumbnail path was successfully retrieved
    std::string thumbnail_path = m_pPrinterInfo->thumbnail_path;
    if (thumbnail_path.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "GetThumbnailImageInMemory: "
                                    << "Thumbnail path is empty for gcode: " << gcodeName;
        return false;
    }
    
    // ============================================
    // Step 2: Download thumbnail image to memory
    // ============================================
    CURL* curl = nullptr;
    CURLcode result = CURLE_FAILED_INIT;
    
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    
    if (!curl) {
        BOOST_LOG_TRIVIAL(error) << "GetThumbnailImageInMemory: Failed to initialize CURL";
        curl_global_cleanup();
        return false;
    }
    
    // Build download URL with URL-encoded thumbnail path
    // URL encode the thumbnail path to handle special characters like spaces, parentheses, etc.
    char* encoded_path = curl_easy_escape(curl, thumbnail_path.c_str(), thumbnail_path.length());
    if (!encoded_path) {
        std::cout << "[GetThumbnailImageInMemory] ERROR - Failed to URL encode thumbnail path: " << thumbnail_path << std::endl;
        BOOST_LOG_TRIVIAL(error) << "GetThumbnailImageInMemory: Failed to URL encode thumbnail path: " << thumbnail_path;
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        return false;
    }
    
    std::string url = "http://" + m_pWebServiceInfo->ip 
                     + m_pWebServiceInfo->port_device 
                     + "/server/files/gcodes/" + std::string(encoded_path);
    curl_free(encoded_path);  // Free the memory allocated by curl_easy_escape
    
    // Set CURL options
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_DEFAULT_PROTOCOL, "https");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteThumbnailMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &thumbnail_data);  // Write to memory
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);
    
    std::cout << "[GetThumbnailImageInMemory] Downloading thumbnail from: " << url << std::endl;
    BOOST_LOG_TRIVIAL(info) << "GetThumbnailImageInMemory: Downloading from " << url;
    
    result = curl_easy_perform(curl);
    
    bool download_success = false;
    if (result == CURLE_OK) {
        if (!thumbnail_data.empty()) {
            download_success = true;
            std::cout << "[GetThumbnailImageInMemory] Successfully downloaded " << thumbnail_data.size() 
                      << " bytes for gcode: " << gcodeName << std::endl;
            BOOST_LOG_TRIVIAL(info) << "GetThumbnailImageInMemory: "
                                    << "Successfully downloaded " << thumbnail_data.size() 
                                    << " bytes for gcode: " << gcodeName;
        } else {
            BOOST_LOG_TRIVIAL(warning) << "GetThumbnailImageInMemory: "
                                       << "Downloaded data is empty";
        }
    } else {
        BOOST_LOG_TRIVIAL(error) << "GetThumbnailImageInMemory: "
                                 << "CURL error: " << curl_easy_strerror(result);
    }
    
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    
    return download_success;
}

// Extract thumbnail from GCode file (higher resolution)
bool GetThumbnailFromGCodeFile(const std::string& gcodeName, std::vector<unsigned char>& thumbnail_data)
{
    thumbnail_data.clear();
    
    std::cout << "[GetThumbnailFromGCodeFile] Attempting to extract thumbnail from GCode file: \"" << gcodeName << "\"" << std::endl;
    
    // ============================================
    // Step 1: Download first portion of GCode file to memory (using HTTP Range request)
    // Thumbnails are typically located at the beginning of GCode files, so we only
    // need to download the first portion instead of the entire file.
    // This significantly reduces download time and prevents timeout issues.
    // 
    // Typical thumbnail sizes:
    // - Multiple thumbnails + GCode header comments: usually < 10KB total
    // 
    // Based on actual GCode file analysis, the first 95 lines (containing header,
    // thumbnail, and initial config) are only about 5.7KB. We use 16KB (16384 bytes)
    // as initial range, which provides about 2.8x safety margin while being much
    // faster than larger ranges. This should cover 99% of cases.
    // ============================================
    std::vector<unsigned char> gcode_data;
    CURL* curl = nullptr;
    CURLcode result = CURLE_FAILED_INIT;
    
    // Define the range to download: first 16KB (0 to 16383 bytes)
    // This is sufficient for thumbnail extraction in most cases
    // Thumbnails are always at the start of GCode files, typically within first 10KB
    const size_t INITIAL_RANGE_SIZE = 16 * 1024;  // 16KB
    const size_t INITIAL_RANGE_END = INITIAL_RANGE_SIZE - 1;  // 0 to 16383
    char range_header_buf[64];
    snprintf(range_header_buf, sizeof(range_header_buf), "Range: bytes=0-%zu", INITIAL_RANGE_END);
    const char* RANGE_HEADER = range_header_buf;
    
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    
    if (!curl) {
        std::cout << "[GetThumbnailFromGCodeFile] ERROR - Failed to initialize CURL" << std::endl;
        BOOST_LOG_TRIVIAL(error) << "GetThumbnailFromGCodeFile: Failed to initialize CURL";
        curl_global_cleanup();
        return false;
    }
    
    // Build GCode file download URL with URL-encoded filename
    // URL encode the filename to handle special characters like spaces, parentheses, etc.
    char* encoded_gcodeName = curl_easy_escape(curl, gcodeName.c_str(), gcodeName.length());
    if (!encoded_gcodeName) {
        std::cout << "[GetThumbnailFromGCodeFile] ERROR - Failed to URL encode filename: " << gcodeName << std::endl;
        BOOST_LOG_TRIVIAL(error) << "GetThumbnailFromGCodeFile: Failed to URL encode filename: " << gcodeName;
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        return false;
    }
    
    std::string url = "http://" + m_pWebServiceInfo->ip 
                     + m_pWebServiceInfo->port_device 
                     + "/server/files/gcodes/" + std::string(encoded_gcodeName);
    curl_free(encoded_gcodeName);  // Free the memory allocated by curl_easy_escape
    
    // Build HTTP header list with Range request
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, RANGE_HEADER);
    
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_DEFAULT_PROTOCOL, "https");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteThumbnailMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &gcode_data);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30);  // Reduced timeout since we're only downloading 16KB
    
    std::cout << "[GetThumbnailFromGCodeFile] Downloading first " << (INITIAL_RANGE_SIZE / 1024) 
              << "KB of GCode file from: " << url << std::endl;
    result = curl_easy_perform(curl);
    
    // Clean up header list
    curl_slist_free_all(headers);
    
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    
    if (result != CURLE_OK || gcode_data.empty()) {
        std::cout << "[GetThumbnailFromGCodeFile] ERROR - Failed to download GCode file: " 
                  << (result != CURLE_OK ? curl_easy_strerror(result) : "empty data") << std::endl;
        BOOST_LOG_TRIVIAL(error) << "GetThumbnailFromGCodeFile: Failed to download GCode file";
        return false;
    }
    
    std::cout << "[GetThumbnailFromGCodeFile] Downloaded " << gcode_data.size() 
              << " bytes of GCode data (requested " << (INITIAL_RANGE_SIZE / 1024) << "KB range)" << std::endl;
    
    // In generally, 16KB should be sufficient for thumbnail extraction
    
    // ============================================
    // Step 2: Parse downloaded GCode data and extract thumbnail
    // Note: We only parse the downloaded portion, which should contain all thumbnails
    // Thumbnails are always at the start of GCode files, typically within first 16KB
    // ============================================
    const std::string BEGIN_MASK = "; thumbnail begin";
    const std::string END_MASK = "; thumbnail end";
    
    // Convert vector to string for parsing
    std::string gcode_content(reinterpret_cast<const char*>(gcode_data.data()), gcode_data.size());
    std::istringstream gcode_stream(gcode_content);
    std::string line;
    
    bool reading_thumbnail = false;
    std::string thumbnail_base64;
    unsigned int thumbnail_width = 0;
    unsigned int thumbnail_height = 0;
    int largest_size = 0;
    std::string largest_thumbnail_base64;
    unsigned int largest_width = 0;
    unsigned int largest_height = 0;
    
    while (std::getline(gcode_stream, line)) {
        if (boost::starts_with(line, BEGIN_MASK)) {
            // Extract dimensions from line like "; thumbnail begin 240x224 12880"
            std::string dims_line = line.substr(BEGIN_MASK.length() + 1);
            std::istringstream dims_stream(dims_line);
            std::string dims_str;
            dims_stream >> dims_str;  // Get "240x224"
            
            size_t x_pos = dims_str.find('x');
            if (x_pos != std::string::npos) {
                try {
                    unsigned int width = std::stoi(dims_str.substr(0, x_pos));
                    unsigned int height = std::stoi(dims_str.substr(x_pos + 1));
                    int size = width * height;
                    
                    std::cout << "[GetThumbnailFromGCodeFile] Found thumbnail in GCode: " 
                              << width << "x" << height << " (size: " << size << ")" << std::endl;
                    
                    // Keep track of largest thumbnail
                    if (size > largest_size) {
                        largest_size = size;
                        largest_width = width;
                        largest_height = height;
                        largest_thumbnail_base64.clear();
                        reading_thumbnail = true;
                    } else {
                        reading_thumbnail = false;
                    }
                } catch (...) {
                    reading_thumbnail = false;
                }
            }
        } else if (reading_thumbnail && boost::starts_with(line, END_MASK)) {
            reading_thumbnail = false;
        } else if (reading_thumbnail && line.length() > 2 && line[0] == ';' && line[1] == ' ') {
            // Append base64 data (skip "; " prefix)
            largest_thumbnail_base64 += line.substr(2);
        }
    }
    
    if (largest_thumbnail_base64.empty()) {
        std::cout << "[GetThumbnailFromGCodeFile] WARNING - No thumbnail found in GCode file" << std::endl;
        BOOST_LOG_TRIVIAL(warning) << "GetThumbnailFromGCodeFile: No thumbnail found in GCode file";
        return false;
    }
    
    std::cout << "[GetThumbnailFromGCodeFile] Extracted largest thumbnail: " 
              << largest_width << "x" << largest_height 
              << " (base64 length: " << largest_thumbnail_base64.length() << ")" << std::endl;
    
    // ============================================
    // Step 3: Decode base64 to binary
    // ============================================
    try {
        thumbnail_data.resize(boost::beast::detail::base64::decoded_size(largest_thumbnail_base64.size()));
        auto decode_result = boost::beast::detail::base64::decode(
            thumbnail_data.data(), 
            largest_thumbnail_base64.data(), 
            largest_thumbnail_base64.size()
        );
        thumbnail_data.resize(decode_result.first);
        
        std::cout << "[GetThumbnailFromGCodeFile] Successfully decoded thumbnail: " 
                  << largest_width << "x" << largest_height 
                  << " (" << thumbnail_data.size() << " bytes)" << std::endl;
        BOOST_LOG_TRIVIAL(info) << "GetThumbnailFromGCodeFile: Successfully extracted thumbnail " 
                                 << largest_width << "x" << largest_height 
                                 << " from GCode file";
        return true;
    } catch (const std::exception& e) {
        std::cout << "[GetThumbnailFromGCodeFile] ERROR - Failed to decode base64: " << e.what() << std::endl;
        BOOST_LOG_TRIVIAL(error) << "GetThumbnailFromGCodeFile: Failed to decode base64: " << e.what();
        return false;
    }
}

int GetMachineList()
{
#if defined(_WIN32) || defined(_WIN64)
    WSADATA ws;
    if (WSAStartup(MAKEWORD(2, 2), &ws) != 0)
    {
        printf("winsock initialize fail!\n");
        return 1;
    }
    //startScheduledDiscovery(0);
    std::string hostname{ "192.168.0.255" };
    uint16_t port = 8989;

    // Get the local hostname
    char szHostName[255];
    gethostname(szHostName, 255);
    struct hostent* host_entry;
    host_entry = gethostbyname(szHostName);
    LPSTR IpAddr = host_entry->h_addr_list[0];

    switch (host_entry->h_addrtype)
    {
    case AF_INET:
        char** pptr = host_entry->h_addr_list;
        for (; *pptr != NULL; pptr++)
        {
            // Init WinSock
            WSADATA wsa_Data;
            int wsa_ReturnCode = WSAStartup(0x101, &wsa_Data);

            int sock = ::socket(AF_INET, SOCK_DGRAM, 0);

            BOOST_LOG_TRIVIAL(info) << "UDP Local IP" << inet_ntoa(*(struct in_addr*)*pptr) << endl;


            /*
            struct in_addr inAddr;
            memmove(&inAddr, IpAddr, 4);
            BOOST_LOG_TRIVIAL(info) << "UDP Local IP1" << inet_ntoa(inAddr) << endl;
            memmove(&inAddr, IpAddr + 4, 4);
            BOOST_LOG_TRIVIAL(info) << "UDP Local IP2" << inet_ntoa(inAddr) << endl;
            memmove(&inAddr, IpAddr + 8, 4);
            BOOST_LOG_TRIVIAL(info) << "UDP Local IP3" << inet_ntoa(inAddr) << endl;
            memmove(&inAddr, IpAddr + 12, 4);
            BOOST_LOG_TRIVIAL(info) << "UDP Local IP4" << inet_ntoa(inAddr) << endl;
            memmove(&inAddr, IpAddr + 16, 4);
            BOOST_LOG_TRIVIAL(info) << "UDP Local IP5" << inet_ntoa(inAddr) << endl;

            */
            char* szLocalIP;
            szLocalIP = inet_ntoa(*(struct in_addr*)*pptr);//inet_ntoa(*(struct in_addr*)*host_entry->h_addr_list);

            //if(i == 0)
            //    szLocalIP = "168.8.1.1";

            std::string _loaclIP = szLocalIP;
            _loaclIP = _loaclIP.substr(0, _loaclIP.find_last_of(".")) + ".255";
            _loaclIP = "255.255.255.255";

            sockaddr_in destination;
            destination.sin_family = AF_INET;
            destination.sin_port = htons(port);
            destination.sin_addr.s_addr = inet_addr(_loaclIP.c_str());

            std::string msg = "mkswifi";
            const char broadcastEnable = 1;
            setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));
            int n_bytes = ::sendto(sock, msg.c_str(), msg.length(), 0, reinterpret_cast<sockaddr*>(&destination), sizeof(destination));
            std::cout << n_bytes << " bytes sent" << std::endl;

            BOOST_LOG_TRIVIAL(info) << "UDP Local IP" << szLocalIP << endl;
            BOOST_LOG_TRIVIAL(info) << "UDP Seach IP" << _loaclIP << endl;
            BOOST_LOG_TRIVIAL(info) << "UDP bytes sent" << n_bytes << endl;

            //if (::bind(sock, (struct sockaddr*)&destination, sizeof(destination)) < 0) {
            //    DebugOutput( "Error binding socket" );
            //    close(sock);
            //    return 1;
            //}

            int cnt = 0;
            int ready = 1;
            while (ready > 0)
            {
                char buffer[1024];
                struct sockaddr_in clientAddr;
                int clientAddrLen = sizeof(clientAddr);

                // Set the socket to non-blocking
                //int flags = fcntl(sockfd, F_GETFL, 0);
                //fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

                // Set up a timeout of 5 seconds
                struct timeval timeout;
                timeout.tv_sec = 2;
                timeout.tv_usec = 0;

                // Wait for data to arrive or timeout
                fd_set readSet;
                FD_ZERO(&readSet);
                FD_SET(sock, &readSet);

                ready = select(sock + 1, &readSet, NULL, NULL, &timeout);
                if (ready < 0) {
                    DebugOutput( "Error in select()"  );
                }
                else if (ready == 0) {
                    std::cout << "No data received within the timeout." << std::endl;
                    BOOST_LOG_TRIVIAL(info) << "UDP No data received within the timeout." << endl;
                }
                else {
                    if (FD_ISSET(sock, &readSet)) {
                        // Receive data from clients
                        size_t bytesRead = ::recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&clientAddr, &clientAddrLen);
                        if (bytesRead < 0) {
                            DebugOutput( "Error receiving data"  );
                            BOOST_LOG_TRIVIAL(info) << "UDP Error receiving data" << endl;
                        }
                        else {
                            buffer[bytesRead] = '\0';
                            std::cout << "Received " << bytesRead << " bytes from " << inet_ntoa(clientAddr.sin_addr) << ": " << buffer << std::endl;
                            std::string reveive = buffer;
                            NetworkingMachineInfo _networkingMachineInfo;
                            std::string machine_name = reveive.substr(8, reveive.find(",") - 8);
                            json info = reveive;

                            _networkingMachineInfo.mahineName = "Arco";//machine_name;
                            _networkingMachineInfo.ip = inet_ntoa(clientAddr.sin_addr);
                            _networkingMachineInfo.connected = false;
                            _networkingMachineInfo.pressed = false;

                            BOOST_LOG_TRIVIAL(info) << "UDP Machine IP" << _networkingMachineInfo.ip << endl;
                            m_kNetworkingMachineInfoList.push_back(_networkingMachineInfo);
                        }
                    }
                }
            }
            WSACleanup();
        }
    }
    return 0;
#elif defined(__APPLE__)
        // need to modify for macOS
        return 0;
#elif defined(__linux__)
        return 0;
#else
        return 0;
#endif
}

CURLcode GetLEDState() {

    //!!!must clear buffer for previous other command, to make sure get right buffer data for LED state
    curl_easy_cleanup(m_pCurl);
    curl_global_cleanup();

    //!!!must initial
    Initialconnect();

    CURLcode res = CURLE_COULDNT_CONNECT;
    if (m_pCurl) {

        // JSON payload
        json payload;
        payload["jsonrpc"] = "2.0";
        payload["method"] = "printer.gcode.script";
        payload["params"]["script"] = "P0 LED_GetState";
        payload["id"] = printer_gcode_script;
        std::string payloadString = payload.dump();
        size_t sent;

        // Connection established, now send the payload
        res = curl_ws_send(m_pCurl, payload.dump().c_str(), strlen(payload.dump().c_str()), &sent, 0, CURLWS_TEXT);

        if (res == CURLE_OK) {

            // Connection established, now set up a loop to wait for responses
            // get response until no data
            auto nowTime = std::chrono::steady_clock::now();
            auto previousTime = std::chrono::steady_clock::now();
            long long timeDiff = std::chrono::duration_cast<std::chrono::seconds>(nowTime - previousTime).count();

            while (true && (timeDiff < 3)) {

                // must add a very samll sleep time for response data
                std::this_thread::sleep_for(std::chrono::milliseconds(160));

                // Set up a buffer to store received data
                // Use curl_ws_recv or similar WebSocket function to receive data
                // first response
#ifndef __APPLE__
                const struct curl_ws_frame* meta;
#else
                // Note: curl 8.x requires non-const pointer - API breaking change
                struct curl_ws_frame* meta;
#endif
                char buffer[2048] = { 0 };;
                size_t rlen;
                res = curl_ws_recv(m_pCurl, buffer, sizeof(buffer), &rlen, &meta);
                // second response
#ifndef __APPLE__
                const struct curl_ws_frame* meta2;
#else
                // Note: curl 8.x requires non-const pointer - API breaking change
                struct curl_ws_frame* meta2;
#endif
                char buffer2[2048] = { 0 };;
                size_t rlen2;
                res = curl_ws_recv(m_pCurl, buffer2, sizeof(buffer2), &rlen2, &meta2);

                if (res == CURLE_OK) {

                    // Data has been received, process the content in 'buffer'
                    std::wstring ws(&buffer[0], &buffer[2048]);
                    std::wstring ws2(&buffer2[0], &buffer2[2048]);
                    //json response = json::parse(ws);

                    //std::cout << "GOT Data of CURLcode GetLEDState" << std::endl;
                    std::wstring keyword = L"P0 LED_State=";
                    //std::wstring secondKeyword = L"P0 LED_State=";
                    size_t id = ws.find(keyword.c_str());
                    if (id == std::string::npos) {
                        size_t id = ws2.find(keyword.c_str());
                        if (id != std::string::npos) {
                            std::wstring value = ws2.substr(id + keyword.length(), 1);
                            m_bIsLEDOn = std::stoi(value);//StrToIntW(value.c_str());
                            break;
                        }
                        else {
                            BOOST_LOG_TRIVIAL(info) << "GOT Data of CURLcode GetLEDState failed: " << endl;
                            DebugOutput( "GOT Data of CURLcode GetLEDState failed: "  );
                            break;
                        }
                    }
                    else {
                        std::wstring value = ws.substr(id + keyword.length(), 1);
                        m_bIsLEDOn = std::stoi(value);//StrToIntW(value.c_str());
                        break;
                    }

                    // Break out of the loop or continue processing
                }
                else if (res == CURLE_AGAIN) {
                    ///std::cout << "No Data of CURLcode GetLEDState"<< std::endl;
                    // No data yet, continue the loop
                }
                else {
                    // Handle error or connection closure
                    BOOST_LOG_TRIVIAL(info) << "Handle error of CURLcode GetLEDState" << endl;
                    break;
                }

                nowTime = std::chrono::steady_clock::now();
                timeDiff = std::chrono::duration_cast<std::chrono::seconds>(nowTime - previousTime).count();
            }
        }
        else {
            BOOST_LOG_TRIVIAL(info) << "WebSocket connection failed: " << curl_easy_strerror(res) << endl;
            DebugOutput( "WebSocket connection failed: " , curl_easy_strerror(res)  );
        }

        // Cleanup
        curl_easy_cleanup(m_pCurl);
    }

    // @vance add
    // must close the url to prevent curremt response data to interrupt next websocket command and response data
    curl_easy_cleanup(m_pCurl);
    curl_global_cleanup();

    return res;
}

CURLcode NozzleFilamentCheck()
{
    CURLcode result = doAction("printer.gcode.script", "PRZ_ADC", printer_gcode_script);
    return result;
}

CURLcode printPause()
{
    CURLcode result = doAction("printer.gcode.script", "PRZ_PAUSE", printer_gcode_script);
    return result;
}

CURLcode printResume()
{
    CURLcode result = doAction("printer.gcode.script", "PRZ_RESUME", printer_gcode_script);
    return result;
}

CURLcode printStop()
{
    CURLcode result = doAction("printer.gcode.script", "PRZ_CANCEL", printer_gcode_script);
    return result;
}

bool NozzleFilamentCheck_Http()
{
    return doAction_http("PRZ_ADC", "ok", 10);
}

bool printPause_http()
{
    return doAction_http("PRZ_PAUSE", "ok", 10);
}

bool printResume_http()
{
    return doAction_http("PRZ_RESUME", "ok", 10);
}

bool printStop_http()
{
    return doAction_http("PRZ_CANCEL", "ok", 10);
}

bool printfile_reset()
{
    m_pCalibrationInfo.actionDone = doAction_http("SDCARD_RESET_FILE", "ok", 10);
    return m_pCalibrationInfo.actionDone;
}

bool home()
{
    m_pCalibrationInfo.actionDone = doAction_http("G28", "ok", 10);
    return m_pCalibrationInfo.actionDone;
    //CURLcode result = doAction("printer.gcode.script", "G28", printer_gcode_script);
    //return result;
}

CURLcode homeXY()
{
    CURLcode result = doAction("printer.gcode.script", "PG28_X_Y", printer_gcode_script);
    return result;
}

CURLcode homeZ()
{
    CURLcode result = doAction("printer.gcode.script", "G28 Z", printer_gcode_script);
    return result;
}

bool homeXY_http()
{
    return doAction_http("G28%20X%20Y", "ok", 10);
}

bool homeZ_http()
{
    return doAction_http("G28 Z", "ok", 10);
}

CURLcode zoffset(float value)
{
    std::string script = "SET_GCODE_OFFSET Z_ADJUST=" + std::to_string(value) + " MOVE=1";
    CURLcode result = doAction("printer.gcode.script", script, printer_gcode_script);
    return result;
}

CURLcode SetExtruderTemperature(int value)
{
    std::string script = "SET_HEATER_TEMPERATURE HEATER=extruder TARGET=" + std::to_string(value);
    CURLcode result = doAction("printer.gcode.script", script, printer_gcode_script);
    return result;
}

CURLcode SetBedTemperature(int value)
{
    std::string script = "SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=" + std::to_string(value);
    CURLcode result = doAction("printer.gcode.script", script, printer_gcode_script);
    return result;
}

CURLcode SetAuxiliaryFanSpeed(int value)
{
    std::string script = "M106 P2 S" + std::to_string(value);
    CURLcode result = doAction("printer.gcode.script", script, printer_gcode_script);
    return result;
}

CURLcode SetPartFanSpeed(int value)
{
    std::string script = "M106 P1 S" + std::to_string(value);
    CURLcode result = doAction("printer.gcode.script", script, printer_gcode_script);
    return result;
}

CURLcode SetShieldFanSpeed(int value)
{
    std::string script = "M106 P3 S" + std::to_string(value);
    CURLcode result = doAction("printer.gcode.script", script, printer_gcode_script);
    return result;
}

CURLcode SetPrintSpeed(int value)
{
    std::string script = "M220 S" + std::to_string(value);
    CURLcode result = doAction("printer.gcode.script", script, printer_gcode_script);
    return result;
}

CURLcode MoveHead(std::string direction, float value)
{
    std::string script = "G91\r\nG1 " + direction + std::to_string(value) + " F7800\r\nG90";
    CURLcode result = doAction("printer.gcode.script", script, printer_gcode_script);
    return result;
}

bool MoveHead_http(std::string direction, float value)
{
    std::string script = "G91%0aG1%20" + direction + std::to_string(value) + "%20F7800%0aG90";
    return doAction_http(script, "ok", 10);
}

bool MoveHead_http_zOffset(float value)
{
    std::string sign = (value > 0 ? "\"+\"" : "\"-\"");
    if (value < 0) {
        value = -value;
    }
    std::string valueStr = std::to_string(value);

    std::string script = "SET_GCODE_OFFSET%20Z_ADJUST=" + sign + valueStr + "%20MOVE=1";
    
    bool result =  doAction_http(script, "ok", 10);
    GetPrinterInfo_websocket();
    return result;
}

CURLcode SetLED(int value)
{
    std::string script = "P0 LED_SetState=" + std::to_string(value);
    CURLcode result = doAction("printer.gcode.script", script, printer_gcode_script);
    return result;
}

CURLcode Calibration()
{
    CURLcode result = doAction("printer.gcode.script", "G29", printer_gcode_script);
    return result;
}

CURLcode FirmwareRestart()
{
    CURLcode result = doAction("printer.gcode.script", "FIRMWARE_RESTART", printer_firmware_restart);
    //CURLcode result = doAction("printer.firmware.restart", "", printer_firmware_restart);
    return result;
}

CURLcode PrinterRestart()
{
    CURLcode result = doAction("printer.gcode.script", "", printer_restart);
    return result;
}

bool load(int filamentid)
{
    std::string script = "P1%20T" + std::to_string(filamentid);
    m_bAMS_action_done = doAction_http(script, "ok", 200);

    return m_bAMS_action_done;
}

bool Unload(int filamentid)
{
    std::string script = "P1%20B" + std::to_string(filamentid);
    m_bAMS_action_done = doAction_http(script, "ok", 200);

    return m_bAMS_action_done;
}

bool Uninstall_filament()
{
    std::string script = "P2%20A2";
    m_bAMS_action_done = doAction_http(script, "ok", 200);

    return m_bAMS_action_done;
}

bool BedMeshClear_http()
{
    m_pCalibrationInfo.bedMeshClearDone = doAction_http("BED_MESH_CLEAR", "ok", 3);
    return m_pCalibrationInfo.bedMeshClearDone;
}

bool BedMeshLoadProfile_http(std::string profile)
{
    m_pCalibrationInfo.bedMeshClearDone = doAction_http("BED_MESH_PROFILE LOAD=" + profile, "ok", 3);
    return m_pCalibrationInfo.bedMeshClearDone;
}

bool Calibration_http()
{
    m_pCalibrationInfo.calibrationDone = doAction_http("G29", "ok", 5); //G29 BED_MESH_CALIBRATE
    return m_pCalibrationInfo.calibrationDone;
}

bool ResonanceCompensation()
{
    m_pCalibrationInfo.resonanceCompensationDone = doAction_http("G40", "ok", 5);
    return m_pCalibrationInfo.resonanceCompensationDone;
}

bool TemperatureCalibration()
{
    m_pCalibrationInfo.resonanceCompensationDone = doAction_http("M303", "ok", 5);
    return m_pCalibrationInfo.resonanceCompensationDone;
}

const std::vector<AMSInfo>& GetAMSList()
{
    return m_kAMSList;
}

const NozzleInfo& GetNozzleInfo()
{
    return nozzleInfo;
}

const bool& IsConnectedToAMS()
{
    return m_bIsConnetedToAMS;
}

void SetThumbnailChecking( bool bCheck )
{
    if ( bCheck ) { m_bDoThumbnailCheck.store(true, std::memory_order_relaxed); }
    else          { m_bDoThumbnailCheck.store(false, std::memory_order_relaxed); }
}

bool IsStartThumbnailChecking()
{
    return m_bDoThumbnailCheck.load(std::memory_order_relaxed);
}

void ResetPreviousPrintState(  )
{
    prev_state.clear();
}

// Calibration progress and status query APIs
CalibrationProgressInfo GetCalibrationProgressInfo()
{
    std::lock_guard<std::mutex> lock(m_kCalibrationProgressMutex);
    return m_calibrationProgressInfo;
}

CalibrationState GetCalibrationStatus()
{
    std::lock_guard<std::mutex> lock(m_kCalibrationProgressMutex);
    return m_calibrationProgressInfo.calibrationStatus;
}

CalibrationState GetResonanceCompensationStatus()
{
    std::lock_guard<std::mutex> lock(m_kCalibrationProgressMutex);
    return m_calibrationProgressInfo.resonanceCompensationStatus;
}

CalibrationState GetTemperatureCalibrationStatus()
{
    std::lock_guard<std::mutex> lock(m_kCalibrationProgressMutex);
    return m_calibrationProgressInfo.temperatureCalibrationStatus;
}

float GetCalibrationProgress()
{
    std::lock_guard<std::mutex> lock(m_kCalibrationProgressMutex);
    return m_calibrationProgressInfo.calibrationProgress;
}

float GetResonanceCompensationProgress()
{
    std::lock_guard<std::mutex> lock(m_kCalibrationProgressMutex);
    return m_calibrationProgressInfo.resonanceCompensationProgress;
}

float GetTemperatureCalibrationProgress()
{
    std::lock_guard<std::mutex> lock(m_kCalibrationProgressMutex);
    return m_calibrationProgressInfo.temperatureCalibrationProgress;
}

bool IsAnyCalibrationRunning()
{
    std::lock_guard<std::mutex> lock(m_kCalibrationProgressMutex);
    return m_calibrationProgressInfo.calibrationStatus == CalibrationState::RUNNING ||
           m_calibrationProgressInfo.resonanceCompensationStatus == CalibrationState::RUNNING ||
           m_calibrationProgressInfo.temperatureCalibrationStatus == CalibrationState::RUNNING;
}

} // namespace MonitorControl

//} // namespace MonitorControl 

//} // namespace Slic3r


