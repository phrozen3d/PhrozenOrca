#ifndef slic3r_PhrozenMachineDatas_hpp_
#define slic3r_PhrozenMachineDatas_hpp_

#include <memory>
#include <atomic>
#include <mutex>
#include <type_traits>
#include "../json_diff.hpp"

typedef unsigned int GLuint;

namespace Slic3r {

#pragma region DoubleBuffer
template <typename T>
class DoubleBufferSP {
    static_assert(std::is_copy_assignable_v<T> || std::is_move_assignable_v<T>,
                  "T must be copy-assignable or move-assignable.");

private:
    std::shared_ptr<T> bufferA = std::make_shared<T>();
    std::shared_ptr<T> bufferB = std::make_shared<T>();

    std::shared_ptr<T> writeBuffer = bufferB; // initialize: A for reader, B for writer

    //An immutable snapshot made public to readers.
    std::shared_ptr<const T> readSnapshot;

    // Write-side mutual exclusion (protects writeBuffer content updates and flip switching)
    mutable std::mutex m_write;

public:
    DoubleBufferSP() {
        // Initialization: Showing the reader the content of A
        std::shared_ptr<const T> initSnap(bufferA, bufferA.get()); // aliasing: const view
        std::atomic_store(&readSnapshot, std::move(initSnap));
    }

    explicit DoubleBufferSP(const T& init) : DoubleBufferSP() {
        *bufferA = init;
        *bufferB = init;
    }

    // ======== Read (lock-free, atomic snapshot loading) ========

    std::shared_ptr<const T> read() const {
        return std::atomic_load(&readSnapshot);
    }

    bool try_read(std::shared_ptr<const T>& out) const {
        out = std::atomic_load(&readSnapshot);
        return (out != nullptr);
    }

    bool try_read(T& out) const {
        auto p = std::atomic_load(&readSnapshot);
        if (!p) return false;                    
        out = *p;                               
        return true;
    }


    // ======== Write (lock only the write end) ========

    void write(const T& src) {
        std::lock_guard<std::mutex> guard(m_write);
        *writeBuffer = src;
    }

    void write(T&& src) {
        std::lock_guard<std::mutex> guard(m_write);
        *writeBuffer = std::move(src);
    }

    bool move_and_write(T& src) {
        return try_write( std::move( src ) );
    }

    bool try_write(const T& src) {
        if (m_write.try_lock()) {
            *writeBuffer = src;
            m_write.unlock();
            return true;
        }
        return false;
    }

    bool try_write(T&& src) {
        if (m_write.try_lock()) {
            *writeBuffer = std::move(src);
            m_write.unlock();
            return true;
        }
        return false;
    }

    // ======== flip (Atomic release of new snapshot + switching to the next write buffer) ========

    void flip() {
        std::lock_guard<std::mutex> guard(m_write);

        // Publish a new snapshot (const view)
        std::shared_ptr<const T> newSnap(writeBuffer, writeBuffer.get());
        std::atomic_store(&readSnapshot, std::move(newSnap));

        // Switch write target
        writeBuffer = (writeBuffer == bufferA) ? bufferB : bufferA;
    }

    bool try_flip() {
        if (!m_write.try_lock()) return false;

        std::shared_ptr<const T> newSnap(writeBuffer, writeBuffer.get());
        std::atomic_store(&readSnapshot, std::move(newSnap));

        writeBuffer = (writeBuffer == bufferA) ? bufferB : bufferA;

        m_write.unlock();
        return true;
    }
};
#pragma endregion

#pragma region PhrozenWebcamDisplayConfig
/// Phrozen 監控分頁「即時鏡頭預覽」用的畫面端幾何設定（僅影響顯示，不改變主機回傳的 JPEG 位元組）。
///
/// 欄位命名與語意對齊 Moonraker 的 webcam 項目（API 文件中的 flip_horizontal、flip_vertical、rotation），
/// 因此日後可將 GET /server/webcams/list 回傳值經正規化後寫入此結構。
///
/// 執行緒：由 PhrozenMachineObject_Dev 以 DoubleBufferSP 保存，與 snapshot 緩衝相同模式，
/// 以便 UI 與網路／背景工作緒安全讀寫。
struct PhrozenWebcamDisplayConfig {
    /// 為 true 時在旋轉之後對影像做水平鏡像（左右對調）。
    bool flip_horizontal = false;
    /// 為 true 時在旋轉之後對影像做垂直鏡像（上下對調）。
    bool flip_vertical   = false;
    /// 順時針旋轉角度（度）。Moonraker 僅使用 0、90、180、270。
    /// 其他角度在 SetWebcamDisplayConfig 內會被正規化；非象限角度會視為 0。
    int rotation_deg     = 0;
};
#pragma endregion

#pragma region PhrozenWebServiceInfo

enum class PhrozenPrinterID : int32_t 
{
    printer_gcode_script = 7466,
    printer_firmware_restart = 64627,
    printer_restart = 22577
};


class PhrozenWebServiceInfo
{
public:
    std::string ip;
    std::string port = "7125";
    std::string port_device = ":8808";
    std::string payload;
    std::unique_ptr<std::string> responseData = make_unique<std::string>();
    json jsonPrinterInfoData;
    json jsonHistoryInfoData;
    json jsonReturnInfoData;
    json jsonThumbnailsInfoData;

    void reset() {
        ip.clear();
        payload.clear();
        responseData.reset();
    }
};
#pragma endregion

#pragma region PhrozenPrinterInfo
class PhrozenPrinterInfo
{
public:

    // --- 位置/校正 ---
    float z_offsetValue = 0.0f;

    // --- 溫度資訊 ---
    int extruder_temperature = 0;
    int extruder_temperature_target = 0;
    int bed_temperature = 0;
    int bed_temperature_target = 0;
    int chamber_temperature = 0;

    // --- 風扇/速度 ---
    float auxiliary_fan_speed = 0.0f;
    float fan_speed = 0.0f;
    float shield_fan_speed = 0.0f;
    float print_speed = 0.0f;

    // --- 其他狀態 ---
    std::string home_axes;               // 預設空字串即可
    float estimated_print_time = 0.0f;

    std::string state;                   // 如 "idle"/"printing"/"paused"
    float print_progress = 0.0f;
    bool is_paused = false;
    std::string print_file;
    float print_time = 0.0f;
    float total_time = 0.0f;
    float print_filament = 0.0f;
    std::string thumbnail_path;
    bool printing_initial = true;
    std::string error = "";
    std::string send_print_time;  // Time when print job was sent (YYYY/MM/DD HH:mm:ss)

    bool bIsLedOn = false;
    bool bIsNozzleDetectFilament = false;

    // --- 網路相關 ---
    bool isSameIP = false;               // 以布林表示「是否相同 IP」
    std::string pre_printerIP = "";      // 上一次 IP

    // （可選）提供明確的預設建構子，確保所有成員已初始化
    PhrozenPrinterInfo() = default;

    // （可選）提供部分初始化的建構子，方便快速設定
    PhrozenPrinterInfo(const std::string& initialState,
                       const std::string& ip = "")
               : state(initialState),
          pre_printerIP(ip) {}

};
#pragma endregion

#pragma region PhrozenThreadControl
class PhrozenThreadControl 
{
public:
    std::mutex mutexForLoadingModels;
    std::mutex mutexForCommonError;
    std::mutex mutexExecuteModelEditor;
    std::mutex mutexGcodeParser;
    std::mutex mutexPrinterInfo;
    std::mutex mutexReceiveMessage;
    std::mutex mutexDataAndImageInitial;
    std::mutex mutexGetWebCameraImage;
    std::condition_variable cvForLoadingModels;
    std::condition_variable cvForCommonError;
    std::condition_variable cvExecuteModelEditor;
    std::condition_variable cvGcodeParser;
    std::condition_variable cvPrinterInfo;
    std::condition_variable cvReceiveMessage;
    bool waitForLoadingModels = false;
    bool waitForCommonError = false;
    bool waitForGizmoMove = false;
    bool readyToDrawStructure = true;
    bool readyToExecuteModelEditor = false; // mirror, ... others?
    bool readyToConvertGcode = false;
    bool waitForPrinterInfo = false;
    bool waitForReceiveMessage = false;
    bool isGcodeParserFinish = false;
    bool isGcodeSliceFinish = false;
    bool isGcodeImportFinish = false;
    bool isGcodeLoadFinish = false;
    bool singlelayer = false;
    int isWide = false;
};
#pragma endregion

#pragma region PhrozenAMSInfo
enum class PhrozenAMSCommandState : int32_t
{
    NONE,
    START,
    FINISH
};

class PhrozenAMSInfo {
public:
    std::string filament = "";
    //ImColor color;
    int temperaure_max = 240, temperaure_min = 190;
    bool selected = false;
    bool entry = false;
    bool park = false;
    bool loading = false;
    //bool unloading = false;
    PhrozenAMSCommandState unload_state = PhrozenAMSCommandState::NONE;
    PhrozenAMSCommandState load_state = PhrozenAMSCommandState::NONE;
    
    bool getEntryState() const {
        return entry;
    }
    
    bool getParkState() const {
        return park;
    }
    
    bool getLoadingState() const {
        return loading;
    }
    
    //bool getUnloadingState() const {
    //    return unloading;
    //}
    
    bool isLoadingStart() const {
        return load_state == PhrozenAMSCommandState::START;
    }
    
    bool isLoadingFinished() const {
        return load_state == PhrozenAMSCommandState::FINISH;
    }
    
    bool isUnloadStart() const {
        return unload_state == PhrozenAMSCommandState::START;
    }
    
    bool isUnloadFinished() const {
        return unload_state == PhrozenAMSCommandState::FINISH;
    }
};
#pragma endregion

#pragma region PhrozenMonitorWindow
class PhrozenMonitorWindow {
public:
    std::vector< PhrozenAMSInfo > AMSList;
    std::vector<bool> AMSselected = { false,false, false, false };
    int AMSselectedID = 1;
    std::string connectedMachineName = "";
    std::string printfile = "";
    int print_speed;
    int auxiliary_fan_speed;
    int fan_speed;
    int shield_fan_speed;
    int nozzle_temperature;
    int bed_temperature;
    int chamber_temperature;
    int Led_value;
    bool isShownIPConnectNotification = false;

    //AMS error
    int amsNum = 0;
    std::string Chroma_Kit = "Chroma\xC2\xA0Kit";
    std::string AMS_ID = "\xC2\xA0" + std::to_string(AMSselectedID) + "\xC2\xA0";
    std::string error_code = "[XXXXXXXXX]";
    std::string test_AMS_Response = "";

    bool isShownChromaConnectionErrorNotification = false; // f
    bool isShownUnloadFilamentErrorNotification = false; // 8
    bool isShownLoadFilamentErrorNotification = false; // 4
    bool isShownSendMaterialFailedNotification = false; //
    bool isShownExtrusionErrorNotification = false; // c, 7
    bool isShownFilamentFlushTimeoutNotification = false; // d

    bool isShownStopPrintNotification = false;
    bool stopPrinting_click = false;

    //PauseCode and Warning Flag Map
    std::unordered_map<std::string, bool*> pauseCodeToFlag = {
    	{ "4", &isShownLoadFilamentErrorNotification },
    	{ "7", &isShownExtrusionErrorNotification },
    	{ "8", &isShownUnloadFilamentErrorNotification },
    	{ "c", &isShownExtrusionErrorNotification },
    	{ "d", &isShownFilamentFlushTimeoutNotification },
    	{ "f", &isShownChromaConnectionErrorNotification }
    };

    // only for test
    // Optional: Set the label corresponding to the error type
    static const std::vector<char> errorTypes;
    static const std::vector<std::string> errorLabels;


    std::string amsReturnError = "";
    std::wstring receiveMessage = L"";

    // New AMS Note
    // lancaigang231202:+PAUSE:1,oldchannel,newchannel;1-New AMS does not need
    // lancaigang231202:+PAUSE:2,oldchannel,newchannel;2-Pause ACK
    // lancaigang231204:+PAUSE:3,oldchannel,newchannel;3-Motor stall error (possible cause: motor stall)
    // lancaigang231205:+PAUSE:4,oldchannel,newchannel;4-The new channel feeds material for 60 seconds and pauses (possible reasons for the error: the old channel has run out of material and cannot return the material, or the material plate has too little material and cannot return the material, or the new channel feeds the stranded wire abnormally and the feed times out)
    // lancaigang231205:+PAUSE:5,oldchannel,newchannel;5-New AMS does not need
    // lancaigang231205:+PAUSE:6,oldchannel,newchannel;6-The time from the entrance to the parking position is 10 seconds, pause
    // lancaigang231205:+PAUSE:7,oldchannel,newchannel;7-The buffer is full for 60 seconds and paused (possible reasons for the error: abnormal fullness of the incoming line, fullness of the nozzle due to material jamming, or fullness of the hot end due to material blockage)
    // lancaigang231205:+PAUSE:8,oldchannel,newchannel;8-The printhead cutter or sensor is abnormal and paused (possible reasons for the error: the old channel has run out of material and cannot be returned, or the material tray has too little material and cannot be returned, or the printhead cutter is abnormal)
    // lancaigang231205:+PAUSE:9,oldchannel,newchannel;9-
    // lancaigang231202:+PAUSE:a,oldchannel,newchannel;a-
    // lancaigang231202:+PAUSE:b,oldchannel,newchannel;b-Single color broken wire detection
    // lancaigang231202:+PAUSE:c,oldchannel,newchannel;c-The nozzle is clogged during printing
    // lancaigang231202:+PAUSE:d,oldchannel,newchannel;d-AMS cannot feed the material during discharge, and the wire has bite holes
    // lancaigang231202:+PAUSE:e,oldchannel,newchannel;e-When printing is started, if the AMS is not drying and the AMS air outlet temperature exceeds 50 degrees, printing will be suspended.
    // lancaigang231202:+PAUSE:f,oldchannel,newchannel;f-When printing is started, if the AMS air outlet temperature exceeds 50 degrees in the AMS drying state, printing will be suspended and the AMS drying function will be stopped.
    // lancaigang231202:+PAUSE:f,oldchannel,newchannel;g-AMS multi-color USB cable abnormality, pause // lancaigang231202:+PAUSE:h,oldchannel,newchannel;h-
    // lancaigang231202:+PAUSE:i,oldchannel,newchannel;i-
    // lancaigang231202:+PAUSE:j,oldchannel,newchannel;j-
    // lancaigang231202:+PAUSE:10,oldchannel,newchannel;10-The touch screen or fluidd web page automatically pauses
};
#pragma endregion

#pragma region PhrozenSendMessageGenerator
class PhrozenSendMessageGenerator
{
 public:
    // printer status, for control panel
    static json GenPrinterControllerPayloadMsg();

    static json GenHistoryPayloadMsg();

    // check ams status
    static json GenAMSPayloadMsg();

    //t o check the filament is existing in the nozzle or not
    static json GenNozzlePayloadMsg();

    // to check if led turn On/Off
    static json GenLEDPayloadMsg();
    
};
#pragma endregion

#pragma region PhrozenCalibrationProgressInfo
// Calibration state enumeration
enum class PhrozenCalibrationState : int32_t {
    STOPPED = 0,    // 停止/未開始
    RUNNING = 1,    // 執行中
    COMPLETED = 2,  // 完成
    HAS_ERROR = 3       // 錯誤
};


class PhrozenCalibrationProgressInfo 
{
public:
    PhrozenCalibrationState calibrationStatus = PhrozenCalibrationState::STOPPED;
    PhrozenCalibrationState resonanceCompensationStatus = PhrozenCalibrationState::STOPPED;
    PhrozenCalibrationState temperatureCalibrationStatus = PhrozenCalibrationState::STOPPED;
    
    float calibrationProgress = 0.0f;           // 0-100
    float resonanceCompensationProgress = 0.0f;   // 0-100
    float temperatureCalibrationProgress = 0.0f;  // 0-100
    
    // Internal state tracking (for progress calculation)
    bool heatingCompleted = false;               // Auto-leveling heating completed
    bool startResonanceCompensation = false;     // Resonance compensation test started
    int tempProgress = 0;                        // Temperature calibration progress counter
    std::chrono::steady_clock::time_point startTime;
    
    // Initialize startTime
    PhrozenCalibrationProgressInfo() : startTime(std::chrono::steady_clock::now()) {}
};

#pragma endregion

#pragma region PhrozenHistoryInfo
class PhrozenHistoryInfo 
{
public:
    std::wstring gcode_name;
    std::string status;
    float fliament_used;
    float total_duration;
    std::string screenshotimage;
    std::string timestamp;
    std::string gcode_path;
    std::string cfg_path;
};
#pragma endregion

#pragma region PhrozenAMSPatterns
class PhrozenAMSPatterns
{
public:
    // AMS pattern strings
    const std::string AMS_connected = "\\u6709\\u51e0\\u53f0AMS\\u5df2\\u7ecf\\u6253\\u5f00\\u4e32\\u53e3='1'";
    const std::string AMS_unconnect = "!! \\u6ca1\\u6709\\u8fde\\u63a5\\u4efb\\u4f55AMS\\u591a\\u8272\\uff0c\\u8fde\\u63a5AMS\\u5931\\u8d25";
    const std::string AMS_load_single_start = "P1Tn:0";
    const std::string AMS_load_single_end = "P1Tn:1";
    const std::string AMS_unload_single_start = "P1Bn:0";
    const std::string AMS_unload_single_end = "P1Bn:1";
    const std::string AMS_unload_all_start = "P2A2:0";
    const std::string AMS_unload_all_end = "P2A2:1";
};
#pragma endregion

} // namespace Slic3r

#endif //  slic3r_PhrozenMachineDatas_hpp_
