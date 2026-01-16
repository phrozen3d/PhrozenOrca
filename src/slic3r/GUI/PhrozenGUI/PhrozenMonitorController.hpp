#ifndef slic3r_PhrozenMonitorController_hpp_
#define slic3r_PhrozenMonitorController_hpp_

#include <curl/curl.h>
#include <functional>
#include <queue>
#include <chrono>
#include "slic3r/Utils/json_diff.hpp"

typedef unsigned int GLuint;


namespace MonitorControl{

enum ID 
{
    printer_gcode_script = 7466,
    printer_firmware_restart = 64627,
    printer_restart = 22577
};

struct NetworkingMachineInfo {
    std::string mahineName;
    std::string ip;
    bool connected;
    bool pressed;
};

struct PrintingHistoryInfo {
    std::wstring fileName;
    std::string date;
    GLuint image;
};

enum AMSCommandState
{
    NONE,
    START,
    FINISH
};

struct AMSPatterns
{
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

struct NozzleInfo {
    bool fila_exist = false;
    
    bool isFilamentExisting() const {
        return fila_exist;
    }
};

struct AMSInfo {
    std::string filament = "";
    //ImColor color;
    int temperaure_max = 240, temperaure_min = 190;
    bool selected = false;
    bool entry = false;
    bool park = false;
    bool loading = false;
    //bool unloading = false;
    AMSCommandState unload_state = AMSCommandState::NONE;
    AMSCommandState load_state = AMSCommandState::NONE;
    
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
        return load_state == AMSCommandState::START;
    }
    
    bool isLoadingFinished() const {
        return load_state == AMSCommandState::FINISH;
    }
    
    bool isUnloadStart() const {
        return unload_state == AMSCommandState::START;
    }
    
    bool isUnloadFinished() const {
        return unload_state == AMSCommandState::FINISH;
    }
};

struct HistoryInfo {
    std::wstring gcode_name;
    std::string status;
    float fliament_used;
    float total_duration;
    std::string screenshotimage;
    std::string timestamp;
    std::string gcode_path;
    std::string cfg_path;
};

struct MonitorWindow {
    std::vector<AMSInfo> AMSList;
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
    bool selectMachineWindowShow = false;
    bool selectMachineWindowShow_preview = false;
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
    const std::vector<char> errorTypes = { '4', '7', '8', 'c', 'd', 'f' };
    const std::vector<std::string> errorLabels = {
    	"Error 4", "Error 7", "Error 8", "Error C", "Error D", "Error F"
    };

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

struct PhrozenMemoryStruct 
{
    char* memory;
    size_t size;
};

struct WebServiceInfo
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

struct PrinterInfo
{
    float z_offsetValue;
    int extruder_temperature;
    int extruder_temperature_target;
    int bed_temperature;
    int bed_temperature_target;
    int chamber_temperature;
    float auxiliary_fan_speed;
    float fan_speed;
    float shield_fan_speed;
    float print_speed;
    std::string home_axes;
    float estimated_print_time;
    
    std::string state;
    float print_progress = 0;
    bool is_paused = false;
    std::string print_file;
    float print_time;
    float total_time;
    float print_filament;
    std::string thumbnail_path;
    bool printing_initial = true;
    std::string error = "";
    std::string send_print_time;  // Time when print job was sent (YYYY/MM/DD HH:mm:ss)

    bool isSameIP = "";
    std::string pre_printerIP = "";
};

struct CalibrationInfo 
{
    bool bedMeshClearDone = false;
    bool calibrationDone = false;
    bool actionDone = false;
    bool resonanceCompensationDone = false;
};

// Calibration state enumeration
enum class CalibrationState {
    STOPPED = 0,    // 停止/未開始
    RUNNING = 1,    // 執行中
    COMPLETED = 2,  // 完成
    HAS_ERROR = 3       // 錯誤
};

// Calibration progress information structure
struct CalibrationProgressInfo {
    CalibrationState calibrationStatus = CalibrationState::STOPPED;
    CalibrationState resonanceCompensationStatus = CalibrationState::STOPPED;
    CalibrationState temperatureCalibrationStatus = CalibrationState::STOPPED;
    
    float calibrationProgress = 0.0f;           // 0-100
    float resonanceCompensationProgress = 0.0f;   // 0-100
    float temperatureCalibrationProgress = 0.0f;  // 0-100
    
    // Internal state tracking (for progress calculation)
    bool heatingCompleted = false;               // Auto-leveling heating completed
    bool startResonanceCompensation = false;     // Resonance compensation test started
    int tempProgress = 0;                        // Temperature calibration progress counter
    std::chrono::steady_clock::time_point startTime;
    
    // Initialize startTime
    CalibrationProgressInfo() : startTime(std::chrono::steady_clock::now()) {}
};

enum DetectionState 
{
    function_off,
    starting,
    stopping,
    idle,
    normal,
    warn,
    error
};
enum RunningState 
{
    off,
    opening,
    running
};

struct AIDetection {
    struct Result {
        int id;
        int x1, y1, x2, y2;
        double probability;
    };

    bool enabled = false;
    RunningState exeRunningState = RunningState::off;
    DetectionState detectionState = DetectionState::function_off;
    std::vector<Result> results;
    std::string lastDetectTime;

    // Windows Process Handles (Cast to void* if windows.h not included here)
    void* childProcess = nullptr;
    void* childThread = nullptr;
    void* stdinWrite = nullptr;
    void* jobHandle = nullptr;

    bool launchReceiver();
    void closeReceiver();
    bool isReceiverRunning() const;
    void poll(); // Replacement for ReadDetectionFile with optimizations
    void reset();
};

extern AIDetection AIDetector;

struct ThreadControl 
{

    mutex mutexForLoadingModels;
    mutex mutexForCommonError;
    mutex mutexExecuteModelEditor;
    mutex mutexGcodeParser;
    mutex mutexPrinterInfo;
    mutex mutexReceiveMessage;
    mutex mutexDataAndImageInitial;
    mutex mutexGetWebCameraImage;
    condition_variable cvForLoadingModels;
    condition_variable cvForCommonError;
    condition_variable cvExecuteModelEditor;
    condition_variable cvGcodeParser;
    condition_variable cvPrinterInfo;
    condition_variable cvReceiveMessage;
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
    bool first_time_to_send_query = true;
};

struct HttpErrorInfo {
    bool has_error = false;            // Whether an error occurred
    int http_status_code = 0;          // HTTP status code (200, 400, 500, etc.)
    int error_code = 0;                // Error code (from error.code)
    std::string error_type;            // Error type (from inner error.error)
    std::string error_message;         // User-friendly error message (prefer inner layer)
    std::string raw_message;           // Raw error message (from outer error.message)
    std::string traceback;             // Stack trace (optional, for debugging)
    
    // Get the best display message
    std::string GetDisplayMessage() const {
        if (!error_message.empty()) return error_message;
        if (!raw_message.empty()) return raw_message;
        return "Unknown error";
    }
    
    // Clear all fields
    void Clear() {
        has_error = false;
        http_status_code = 0;
        error_code = 0;
        error_type.clear();
        error_message.clear();
        raw_message.clear();
        traceback.clear();
    }
};

#pragma region PhrozenMonitorController
    CURLcode IsIpConnectValid( std::string strIp );
    CURLcode Initialconnect();
    void CleanupWebSocketConnection();
    void SetIp( const std::string& strIp );
    bool CheckArpEntryExists(const std::string& target_ip);
    bool WaitForArpResolution(const std::string& target_ip, int max_wait_ms = 1000);
    bool TriggerArpResolution(const std::string& target_ip);

    extern ThreadControl threadControl;
    extern bool m_bUdp_ing;
    extern bool m_bStartlistening;
    extern bool m_bDoingAction;
    extern std::atomic<bool> m_bStartReceiving;
    extern std::atomic<bool> m_bStartSending;
    void SetStartSending( bool bStart );
    bool IsStartSending();
    void SetStartReceiving( bool bStart );
    bool IsStartReceiving();

    extern CURL* m_pCurl;
    extern CURL* m_pCurl_websocket;

    extern std::vector<NetworkingMachineInfo> m_kNetworkingMachineInfoList;
    extern std::vector<HistoryInfo> m_kHistoryInfoList;
    extern std::vector<AMSInfo> m_kAMSList;
    extern std::vector<AMSInfo> m_kAMSList_temp;
    extern PrinterInfo* m_pPrinterInfo;
    extern WebServiceInfo* m_pWebServiceInfo;
    extern CalibrationInfo m_pCalibrationInfo;
    extern MonitorWindow m_kMonitorWindow;
    //AIDetection FailureDetection;
    extern bool m_bFirst;

    extern bool m_bCameraOn;
    extern std::wstring m_strVideo_path;
    extern bool m_bInitial_P28;
    extern bool m_bAMS_action_done;
    extern int m_nSendJobSuccess;
    extern double m_fProgressValue;
    extern std::wstring m_strReceiveMessage;
    extern bool m_bReceiving;
    extern int m_bIsLEDOn;

    //@vance add temporarily. It's not good coding style. Need to modify later.
    //for snapshot & stream live control
    // for 1st alternative
    extern bool m_bOpenCVStream;
    extern bool m_bVideoFinished;
    extern bool m_bVideoStart;
    extern std::string m_strVideoPath;
    extern std::string m_strVideoTempPath;
    extern float m_fRecordFPS;
    extern float m_fRecordInterval;
    // for 2nd alternative
    extern std::vector<unsigned char> m_kLatestImageData;
    extern std::mutex m_kImageMutex;
    extern bool m_bNewImageAvailable;
    extern GLuint m_nTexture;
    extern GLuint m_nHistoryTexture[50];
    extern GLuint m_nPrintingTexture;
    extern bool m_bTriggerOnce;
    extern bool m_bClose;
    extern bool m_bIsCameraOn;
    extern bool m_bConnectionInitial;
    extern std::mutex m_kCurlMutex;
    extern std::mutex m_kCommandMutex;
    extern std::mutex m_kCalibrationProgressMutex;
    extern bool m_bIsConnetedToAMS;
    extern std::atomic<bool> m_bDoThumbnailCheck;
    extern std::string prev_state;
    extern bool isReadFromGcodeFinished;
    extern HttpErrorInfo error_info;

    extern CalibrationProgressInfo m_calibrationProgressInfo;

    size_t write_data(void* ptr, size_t size, size_t nmemb, void* stream);

    /* close the connection */
    void websocket_close();

    // Return value structure: pauseCode, oldChannel, newChannel
    std::tuple<std::string, std::string, std::string> ParsePauseMessage(const std::string& message);

    void HandlePauseCode(const std::string& pauseCode);

    CURLcode ReceiveResponse();
    bool IsFatalError(CURLcode code); // 輔助函數：判斷是否為致命錯誤
    void HandleDisconnection(); // 輔助函數：處理斷線


    CURLcode CheckAMSConnection();

    void websocket_cleanup();

    //std::function<size_t(void*, size_t, size_t, void*)> fnWriteData;
    //std::function<int(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t)> fnUploadProgressCallback;
    //std::function<size_t(void* ptr, size_t size, size_t nmemb, FILE* stream)> fnWriteData_file;

    CURLcode send_action_Command(std::string send_payload);
    CURLcode CheckReceiveValue(const char* exected_payload);
    std::wstring CheckReceiveValue_new(std::wstring expected);
    CURLcode CheckReceiveValue_AMS(const char* exected_payload);
    size_t WriteBinaryData(void* buffer, size_t size, size_t nmemb, void* lpVoid);
    size_t WriteMemoryCallback(void* contents, size_t size, size_t nmemb, void* userp);
    size_t WriteCallback_test(void* contents, size_t size, size_t nmemb, std::vector<unsigned char>* data);
    size_t WriteData_test(void* buffer, size_t size, size_t nmemb, void* lpVoid);
    size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::ofstream* file);
    int DownloadProgressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);
    int CURLDebug(CURL*, curl_infotype type, char* data, size_t size, void*);
    void GetPrinterInfo_websocket();
    CURLcode printfile(std::string filename);
    HttpErrorInfo ParseHttpErrorResponse(const json& response_json, int http_status_code);
    bool doAction_http(std::string script, std::string  exected_payload, int timeout);
    CURLcode doAction(std::string method, std::string script, int id);
    
    CURLcode NozzleFilamentCheck();

    CURLcode printPause();
    
    CURLcode printResume();
    
    CURLcode printStop();

    bool NozzleFilamentCheck_Http();
    
    bool printPause_http();
    
    bool printResume_http();
    
    bool printStop_http();
    
    bool printfile_reset();
    
    bool home();
    
    CURLcode homeXY();
    
    CURLcode homeZ();
    
    bool homeXY_http();
    
    bool homeZ_http();
    
    CURLcode zoffset(float value);
    
    CURLcode SetExtruderTemperature(int value);
    
    CURLcode SetBedTemperature(int value);
    
    CURLcode SetAuxiliaryFanSpeed(int value);
    
    CURLcode SetPartFanSpeed(int value);
    
    CURLcode SetShieldFanSpeed(int value);
    
    CURLcode SetPrintSpeed(int value);    
    
    CURLcode MoveHead(std::string direction, float value);
    
    bool MoveHead_http(std::string direction, float value);

    bool MoveHead_http_zOffset(float value);
    
    CURLcode SetLED(int value);
    
    CURLcode Calibration();
    
    CURLcode FirmwareRestart();
    
    CURLcode PrinterRestart();
    
    bool load(int filamentid);
    
    bool Unload(int filamentid);
    
    bool Uninstall_filament();
    
    bool BedMeshClear_http();
    
    bool BedMeshLoadProfile_http(std::string profile);
    
    bool Calibration_http();
    
    bool ResonanceCompensation();
    
    bool TemperatureCalibration();

    CURLcode GetAMSInfo();
    CURLcode GetAMSInfo_websocket();
    void GetPrinterInfo();
    void GetAllInfo_websocket();
    void GetHistoryInfo();
    void GetHistoryInfo_websocket();
    void GetThumbnailInfo(std::string gcode);
    bool GetThumbnailImage(std::string printingfile);
    bool GetThumbnailImageInMemory(const std::string& gcodeName, std::vector<unsigned char>& thumbnail_data);
    bool GetThumbnailFromGCodeFile(const std::string& gcodeName, std::vector<unsigned char>& thumbnail_data);
    double ParseEstimatedTimeString(const std::string& line);
    bool GetEstimatedTimeFromGCodeFile(const std::string& gcodeName, double& estimated_seconds);
    int GetMachineList();
    CURLcode GetLEDState();
    void ResetAMSList();

    const std::vector<AMSInfo>& GetAMSList();
    const bool& IsConnectedToAMS();
    const NozzleInfo& GetNozzleInfo();

    void SetThumbnailChecking( bool bCheck );
    bool IsStartThumbnailChecking();
    void ResetPreviousPrintState();

    // Calibration progress and status APIs
    CalibrationProgressInfo GetCalibrationProgressInfo();
    CalibrationState GetCalibrationStatus();
    CalibrationState GetResonanceCompensationStatus();
    CalibrationState GetTemperatureCalibrationStatus();
    float GetCalibrationProgress();
    float GetResonanceCompensationProgress();
    float GetTemperatureCalibrationProgress();
    bool IsAnyCalibrationRunning();  // Check if any calibration is running

#pragma endregion //PhrozenMonitorController

} //namespace MonitorControl{


#endif /* slic3r_PhrozenMonitorController_hpp_ */
