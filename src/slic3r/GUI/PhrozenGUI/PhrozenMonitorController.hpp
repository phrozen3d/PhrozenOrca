#ifndef slic3r_PhrozenMonitorController_hpp_
#define slic3r_PhrozenMonitorController_hpp_

//#include <wx/notebook.h>
//#include <wx/scrolwin.h>
//#include <wx/sizer.h>
//#include <wx/bmpcbox.h>
//#include <wx/bmpbuttn.h>
//#include <wx/treectrl.h>
//#include <wx/imaglist.h>
//#include <wx/artprov.h>
//#include <wx/xrc/xmlres.h>
//#include <wx/string.h>
//#include <wx/stattext.h>
//#include <wx/gdicmn.h>
//#include <wx/font.h>
//#include <wx/colour.h>
//#include <wx/settings.h>
//#include <wx/sizer.h>
//#include <wx/grid.h>
//#include <wx/dataview.h>
//#include <wx/panel.h>
//#include <wx/statline.h>
//#include <wx/bitmap.h>
//#include <wx/image.h>
//#include <wx/icon.h>
//#include <wx/bmpbuttn.h>
//#include <wx/button.h>
//#include <wx/gbsizer.h>
//#include <wx/statbox.h>
//#include <wx/tglbtn.h>
//#include <wx/popupwin.h>
//#include <wx/spinctrl.h>
//#include <wx/artprov.h>
//#include <wx/webrequest.h>
//#include <map>
//#include <vector>
//#include <memory>
//#include <curl/curl.h>
//#include "../Event.hpp"
//#include "libslic3r/ProjectTask.hpp"
//#include "../wxExtensions.hpp"
//#include "slic3r/GUI/MsgDialog.hpp"
//#include "slic3r/GUI/DeviceManager.hpp"
//#include "PhrozenStatusPanel.hpp"
#include <curl/curl.h>
#include <functional>
#include "slic3r/Utils/json_diff.hpp"

typedef unsigned int GLuint;

//namespace Slic3r {

//namespace GUI {

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

struct AMSInfo {
    std::string filament = "";
    //ImColor color;
    int temperaure_max = 240, temperaure_min = 190;
    bool selected = false;
    bool install = false;
    bool loading = false;
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
    std::vector<NetworkingMachineInfo> natworkingMachineList;
    std::vector<PrintingHistoryInfo> printhistoryList;
    std::vector<AMSInfo> AMSList;
    std::vector<bool> AMSselected = { false,false, false, false };
    int AMSselectedID = 1;
    std::wstring expected = L"";
    std::string connectedMachineName = "";
    std::string printfile = "";
    std::string printfileForRestore = "";
    std::string moveHead = "";
    int print_speed;
    int auxiliary_fan_speed;
    int fan_speed;
    int shield_fan_speed;
    int nozzle_temperature;
    int bed_temperature;
    int chamber_temperature;
    float offset_value = 1;
    float z_offset_value = 0.005;
    int Led_value;
    int selectMachineTab;
    bool initial = true;
    bool selectMachineWindowShow = false;
    bool selectMachineWindowShow_preview = false;
    bool beforePrintWindowShow = false;
    bool beforeExportWindowShow = false;
    bool isShownAMSsetting = false;
    bool isShownAMSeditor = false;
    bool isShownPrinterBusyWarning = false;
    bool isShownPrintCompleteAndCancelWarning = false;
    bool isShownPrinterFailedWarning = false;
    bool isShownMonitorErrorWarning = false;
    int AMSeditorIndex = 0;
    
    bool isShownCalibrationNotification = false;
    bool isShownCalibration = false;
    int CalibrationStatus = 0; // 0:stop 1:start 2:done 3:error
    int TemperatureCalibrationStatus = 0;
    int ResonanceCompensationStatus = 0;
    bool isCalibrationStart = false;
    int calibration_progress = 2;
    bool calibration_did = false;
    std::string calibration_profile = "default";
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
    
    bool resonanceCompensation_did = false;
    bool home_did = false;
    bool AMSaction_did = false;
    std::string pringfile;
    bool getThumbnailsuccess = false;
    bool ActionDone = false;
    std::wstring receiveMessage = L"";
    bool isShowAMSmenu = false;
    bool beforePritingStart = false;
    bool beforePritingHeating = false;
    bool beforePritingDone = false;
    bool beforePrintingWindowInitial = true;
    bool sendjob = false;
    bool isShownAMStutorial = false;
    bool isShownAMSuninstallfilament = false;
    bool isClickAMS_EnterStandbyArea = false;
    bool isClickAMS_EnetrNozzleHead = false;
    bool isClickAMS_RetractToStandbyArea = false;
    bool isClickAMS_UninstallFilament = false;
    int EnterStandbyArea_page = 0;
    int EnetrNozzleHead_page = 0;
    int RetractToStandbyArea_page = 0;
    int UninstallFilament_page = 0;
    bool historyDetailWindowShow = false;
    bool history_info = false;
    std::wstring history_detail_path;
    std::string history_name;
    std::string history_print_date;
    std::string history_total_layers;
    std::string history_filament_volume;
    std::string history_total_height;
    std::string history_print_time = "";
    std::string history_print_time_hrs = "0";
    std::string history_print_time_mins = "0";
    std::string history_print_time_secs = "0";
    int history_rate;
    std::string history_print_comment;
    std::vector<int> star_ratings;
    std::vector<std::string> i_time;
    std::vector<std::string> i_filament;
    int projectId = 0;
    std::vector<bool> historySelection;
    bool selectAllPrintHistory = false;
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
    float z_offsetValure;
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
};

struct CalibrationInfo 
{
    bool bedMeshClearDone = false;
    bool calibrationDone = false;
    bool actionDone = false;
    bool resonanceCompensationDone = false;
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




#if 0 //AIDetection
struct AIDetection {
    bool enabled = false;
    RunningState exeRunningState = RunningState::off;
    DetectionState detectionState = DetectionState::function_off;
    struct ErrorMessage {
        cv::Point _box[2];
        int id;
        double probability;
    };
    std::vector<ErrorMessage> error_messages;

    std::string lastDetectTime;

    HANDLE childProcess = NULL;
    HANDLE childThread = NULL;
    HANDLE stdinWrite = NULL;
    HANDLE jobHandle = NULL;

    bool launchReceiver() {
        exeRunningState = RunningState::opening;
        SECURITY_ATTRIBUTES saAttr{};
        saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
        saAttr.bInheritHandle = TRUE;
        saAttr.lpSecurityDescriptor = NULL;

        // construct stdin pipe
        HANDLE hChildStdinRead = NULL;
        HANDLE hChildStdinWrite = NULL;
        if (!CreatePipe(&hChildStdinRead, &hChildStdinWrite, &saAttr, 0)) {
            std::cerr << "CreatePipe failed\n";
            return false;
        }
        SetHandleInformation(hChildStdinWrite, HANDLE_FLAG_INHERIT, 0);

        // Set startup information
        STARTUPINFOA si{};
        si.cb = sizeof(STARTUPINFOA);
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdInput = hChildStdinRead;
        si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

        PROCESS_INFORMATION pi{};

        // Create a sub-process
        BOOL success = CreateProcessA(
            NULL,
            "AnomalyDetection_for_ARCO.exe",
            NULL, NULL, TRUE, // Inheritance of handle must be allowed
            CREATE_NO_WINDOW, // Use CREATE_NO_WINDOW
            NULL,
            NULL,
            &si, &pi
        );

        if (!success) {
            std::cerr << "CreateProcessA failed: " << GetLastError() << "\n";
            CloseHandle(hChildStdinRead);
            CloseHandle(hChildStdinWrite);
            return false;
        }

        // Establish and specify Kill-on-close
        jobHandle = CreateJobObject(NULL, NULL);
        if (jobHandle == NULL) {
            std::cerr << "CreateJobObject failed: " << GetLastError() << "\n";
            // No automatic cleanup
        }
        else {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInfo{};
            jobInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(jobHandle, JobObjectExtendedLimitInformation, &jobInfo, sizeof(jobInfo))) {
                std::cerr << "SetInformationJobObject failed: " << GetLastError() << "\n";
                CloseHandle(jobHandle);
                jobHandle = NULL;
            }
            else {
                AssignProcessToJobObject(jobHandle, pi.hProcess);
            }
        }

        // Turn off unnecessary read ports
        CloseHandle(hChildStdinRead);

        // Save handle
        childProcess = pi.hProcess;
        childThread = pi.hThread;
        stdinWrite = hChildStdinWrite;

        return true;
    }

    void closeReceiver() {
        if (stdinWrite) {
            CloseHandle(stdinWrite);
            stdinWrite = NULL;
        }
        if (childThread) {
            CloseHandle(childThread);
            childThread = NULL;
        }
        if (childProcess) {
            TerminateProcess(childProcess, 0); // If need to force close
            CloseHandle(childProcess);
            childProcess = NULL;
        }
        if (jobHandle) {
            CloseHandle(jobHandle); // When closed, the subprocess will be terminated
            jobHandle = NULL;
        }
        exeRunningState = RunningState::off;
    }

    bool isReceiverRunning() const {
        if (!childProcess) return false;

        DWORD exitCode;
        if (GetExitCodeProcess(childProcess, &exitCode)) {
            return exitCode == STILL_ACTIVE;
        }
        return false;
    }


    float TimeDiffSeconds(const std::string& timeStr1, const std::string& timeStr2) {
        auto parseTime = [](const std::string& timeStr) -> std::tm {
            std::tm tm{};
            std::istringstream ss(timeStr);
            ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
            return tm;
            };

        std::tm tm1 = parseTime(timeStr1);
        std::tm tm2 = parseTime(timeStr2);

        std::time_t t1 = std::mktime(&tm1);
        std::time_t t2 = std::mktime(&tm2);

        if (t1 == -1 || t2 == -1) {
            return 0.0f;
        }
        return static_cast<float>(std::difftime(t1, t2));
    }

    bool NotStarted() {
        if (exeRunningState == RunningState::off)
            return true;
        else
            return false;
    }
    bool IsRunning() {
        if (exeRunningState == RunningState::running)
            return true;
        else
            return false;
    }

    void ResetDetectionState() {

        std::ofstream file_to_model;
        error_messages.clear();
        file_to_model.open("detection_results.txt");
        file_to_model << "";
        file_to_model.close();
    }
    string getCurrentTimeString() {
        auto now = chrono::system_clock::now();
        time_t now_c = chrono::system_clock::to_time_t(now);
        tm local_tm;
#if defined(_WIN32) || defined(_WIN64)
        localtime_s(&local_tm, &now_c);  // Windows safe mode
#else
        localtime_r(&now_c, &local_tm);  // Linux / macOS safe mode
#endif
        stringstream ss;
        ss << put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }
    void AddReadTimestamp(string filename) {
        ifstream infile(filename);
        if (!infile.is_open()) {
            cerr << "Can't open file: " << filename << endl;
            return;
        }
        string firstLine;
        getline(infile, firstLine);

        bool hasReadTimestamp = (firstLine.rfind("read at", 0) == 0);
        if (hasReadTimestamp) {
            infile.close();
            return;
        }

        stringstream buffer;
        buffer << infile.rdbuf();
        infile.close();

        ofstream outfile(filename, ios::trunc);
        if (!outfile.is_open()) {
            cerr << "Can't open file: " << filename << endl;
            return;
        }
        outfile << "read at " << getCurrentTimeString() << endl;
        outfile << firstLine << endl;
        outfile << buffer.str();
        outfile.close();
    }
    void setEnable(bool value) {
        enabled = value;
    }
    std::string GetDetectionState() {
        std::string detection_state = "Off";    // state 0
        if(exeRunningState == RunningState::off)
            return detection_state;

        switch (detectionState) {
        case DetectionState::starting:
            detection_state = "Starting";
            break;
        case DetectionState::stopping:
            detection_state = "Stopping";
            break;
        case DetectionState::normal:
            detection_state = "Normal";
            break;
        case DetectionState::warn:
            detection_state = "Warn";
            break;
        case DetectionState::error:
            detection_state = "Error";
            break;
        }
        return detection_state;
    }
    void updateDetectionState() {
        bool need_stop_print = false;
        bool need_warn = false;
        for (auto i = 0; i < error_messages.size(); i++) {
            switch (error_messages[i].id) {
            case 0:    // BLOB
                if (error_messages[i].probability >= 0.5)
                    need_warn = true;
                break;
            case 1:    // CRACKS
                if (error_messages[i].probability >= 0.8)
                    need_stop_print = true;
                else if (error_messages[i].probability >= 0.3)
                    need_warn = true;
                break;
            case 2:    // SPAGHETTI
                if (error_messages[i].probability >= 0.8)
                    need_stop_print = true;
                else if (error_messages[i].probability >= 0.3)
                    need_warn = true;
                break;
            case 3:    // STRINGGING
                if (error_messages[i].probability >= 0.5)
                    need_warn = true;
                break;
            case 4:    // UNDEREXTRUSION
                if (error_messages[i].probability >= 0.8)
                    need_stop_print = true;
                else if (error_messages[i].probability >= 0.3)
                    need_warn = true;
                break;
            }
        }
        if (need_stop_print)
            detectionState = DetectionState::error;
        else if (need_warn)
            detectionState = DetectionState::warn;
        else
            detectionState = DetectionState::normal;
    }
    void ReadDetectionFile() {
        std::ifstream error_message;
        error_message.open("detection_results.txt");
        std::string this_line;
        error_messages.clear();
        bool has_message = false;
        bool already_read = false;
        while (std::getline(error_message, this_line)) {
            if (this_line.find("read at") != std::string::npos) {
                already_read = true;
                continue;
            }
            if (this_line.find("normal") != std::string::npos) {
                has_message = true;
                break;
            }
            std::stringstream ss(this_line);
            std::vector<double> numbs;
            std::string token = "";
            // Detection screen size: 640 x 480
            while (std::getline(ss, token, ',')) {
                if (!token.empty()) {
                    numbs.push_back(std::stod(token));
                    has_message = true;
                }
            }
            if (numbs.size() >= 6) {
                // Flip image
                numbs[1] = (640 - numbs[1]);
                numbs[2] = (480 - numbs[2]);
                numbs[3] = (640 - numbs[3]);
                numbs[4] = (480 - numbs[4]);

                ErrorMessage temp;
                temp.id = (int)numbs[0];
                temp._box[0] = cv::Point(numbs[1], numbs[2]);
                temp._box[1] = cv::Point(numbs[3], numbs[4]);
                temp.probability = numbs[5];
                if (temp.id < 5)
                    error_messages.push_back(temp);
            }
        }
        error_message.close();
        updateDetectionState();
        if (has_message && exeRunningState == RunningState::opening) {
            exeRunningState = RunningState::running;
        }
        if (!has_message) {
            if (enabled)
                detectionState = DetectionState::starting;
            else
                detectionState = DetectionState::stopping;
        }
        if (!already_read)
            AddReadTimestamp("detection_results.txt");
    }
    std::string GetErrorMessage() {
        if (!IsRunning())
            return "";
        std::string error_message = "";
        int error_prob[5] = { 0, 0, 0, 0, 0 };
        for (auto i = 0; i < error_messages.size(); i++) {
            if (error_messages[i].id < 5 && error_messages[i].id >= 0) {
                error_prob[error_messages[i].id] = int(error_messages[i].probability * 100);
            }
        }
        if (error_prob[0] > 0)
            error_message += "BLOB                 " + std::to_string(error_prob[0]) + "%%\n";
        if (error_prob[1] > 0)
            error_message += "CRACKS               " + std::to_string(error_prob[1]) + "%%\n";
        if (error_prob[2] > 0)
            error_message += "SPAGHETTI            " + std::to_string(error_prob[2]) + "%%\n";
        if (error_prob[3] > 0)
            error_message += "STRINGGING           " + std::to_string(error_prob[3]) + "%%\n";
        if (error_prob[4] > 0)
            error_message += "UNDEREXTRUSION       " + std::to_string(error_prob[4]) + "%%\n";
        return error_message;
    }
};
#endif //AIDetection

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
};

struct WebCamImageDataThreadHandler
{
    std::mutex buffer_mutex;
    std::vector<unsigned char> bufferA;
    std::vector<unsigned char> bufferB;
    std::vector<unsigned char>* pWriteBuffer = &bufferA;
    std::vector<unsigned char>* pReadBuffer = &bufferB;
    bool bNewImageAvailable = false;
};


#pragma region PhrozenMonitorController
    CURLcode Initialconnect();
    void SetIp( const std::string& strIp );

    extern ThreadControl threadControl;
    extern bool m_bUdp_ing;
    extern bool m_bStartlistening;
    extern bool m_bDoingAction;
    extern bool m_bStartReceiving;
    extern bool m_bStartSending;
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
    extern bool m_bIsConnetedToAMS;
    extern WebCamImageDataThreadHandler WebCamDataHandler;

    size_t write_data(void* ptr, size_t size, size_t nmemb, void* stream);

    /* close the connection */
    void websocket_close();

    // Return value structure: pauseCode, oldChannel, newChannel
    std::tuple<std::string, std::string, std::string> ParsePauseMessage(const std::string& message);

    void HandlePauseCode(const std::string& pauseCode);

    CURLcode ReceiveResponse();
    
    CURLcode ReceiveWebCameraView( const std::string & url ); //LiveStreamWithMultiThread

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
    bool doAction_http(std::string script, std::string  exected_payload, int timeout);
    CURLcode doAction(std::string method, std::string script, int id);
    CURLcode printPause();
    
    CURLcode printResume();
    
    CURLcode printStop();
    
    bool printPause_http();
    
    bool printResume_http();
    
    bool printStop_http();
    
    bool printfile_reset();
    
    bool home();
    
    CURLcode homeXY();
    
    CURLcode homeZ();
    
    bool homeXY_http();
    
    bool homeZ_http();
    
    CURLcode zoffset(int value);
    
    CURLcode SetExtruderTemperature(int value);
    
    CURLcode SetBedTemperature(int value);
    
    CURLcode SetAuxiliaryFanSpeed(int value);
    
    CURLcode SetFanSpeed(int value);
    
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
    int GetMachineList();
    CURLcode GetLEDState();



#pragma endregion //PhrozenMonitorController

} //namespace MonitorControl{

//} // namespace MonitorControl

//} // namespace Slic3r


#endif /* slic3r_PhrozenMonitorController_hpp_ */
