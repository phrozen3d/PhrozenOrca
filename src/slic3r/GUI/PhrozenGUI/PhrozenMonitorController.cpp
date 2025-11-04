#include "PhrozenMonitorController.hpp"

#include <codecvt>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#endif

//#include <wx/app.h>
//#include <wx/button.h>
//#include <wx/scrolwin.h>
//#include <wx/sizer.h>
//
//#include <wx/bmpcbox.h>
//#include <wx/bmpbuttn.h>
//#include <wx/treectrl.h>
//#include <wx/imaglist.h>
//#include <wx/settings.h>
//#include <wx/filedlg.h>
//#include <wx/wupdlock.h>
//#include <wx/dataview.h>
//#include <wx/tglbtn.h>

//#include "../wxExtensions.hpp"
//#include "../GUI_App.hpp"
//#include "../GUI_ObjectList.hpp"
//#include "../Plater.hpp"
//#include "../MainFrame.hpp"
//#include "../Widgets/Label.hpp"
//#include "../format.hpp"
//#include "../MediaPlayCtrl.h"
//#include "../MediaFilePanel.h"
//#include "../BindDialog.hpp"

//namespace Slic3r {
//namespace GUI {
namespace MonitorControl{

    
void DebugOutput(const std::string& prefix, const char* message = ""  ) {
    std::string combined = prefix + message;
#ifdef _WIN32
    OutputDebugStringA(combined.c_str());
#else
    std::cout << combined << std::endl;
#endif
}



    // Define global variables
    ThreadControl threadControl;
    bool m_bUdp_ing = false;
    bool m_bStartlistening = false;
    bool m_bDoingAction = false;
    bool m_bStartReceiving = false;
    bool m_bStartSending = false;
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
    bool m_bIsConnetedToAMS = false;
    WebCamImageDataThreadHandler WebCamDataHandler = {};

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
            curl_global_cleanup();
            m_pCurl_websocket = nullptr;
            m_bStartReceiving = false;
            m_bStartSending = false;

            return res; // Return CURL error code
        }
        else {
            printf("WebSocket connection established successfully\n");

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
        m_bStartReceiving = false;
        m_bStartSending = false;
        return res; // CURL init failed
    }

    return res;
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

CURLcode ReceiveResponse() {

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
    
    std::string historyInfo;
    bool historyStart = false;
    int again = 0;
    while (m_bStartReceiving) {
        std::lock_guard<std::mutex> lock(m_kCurlMutex);
        try {
            //res = curl_easy_perform(curl_websocket);
            double connectTime = 0;
            curl_easy_getinfo(m_pCurl_websocket, CURLINFO_CONNECT_TIME, &connectTime);
            if (connectTime > 0)
            {
                res = curl_ws_recv(m_pCurl_websocket, buffer, sizeof(buffer), &rlen, &meta);
                //BOOST_LOG_TRIVIAL(info) << res << endl;
                if (res == CURLE_OK) {
                    again = 0;
                    std::string ws(&buffer[0], &buffer[rlen]);
                    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
                    m_strReceiveMessage = converter.from_bytes(ws);

                    std::string skip_message = "{\"jsonrpc\": \"2.0\", \"method\": \"notify_proc_stat_update\"";
                    size_t pos = ws.find(skip_message.c_str());
                    if (pos != std::string::npos)
                    {
                        continue;
                    }

                    //{"jsonrpc": "2.0", "method": "notify_gcode_response", "params": ["!! [(Phrozen)Klipper]:Unhandled exception during run"]}
                    //Printer Info
                    BOOST_LOG_TRIVIAL(info) << "receive: " << ws << endl;
                    std::string find_message = "{\"jsonrpc\": \"2.0\", \"method\": \"notify_gcode_response\"";
                    pos = ws.find(find_message.c_str());
                    if (pos != std::string::npos)
                    {
                        json msg_json;
                        if (json::accept(ws))
                        {
                            msg_json = json::parse(ws);
                            if (!msg_json["params"].is_null()) {
                                std::string params = msg_json["params"][0].get<std::string>();
                                size_t pos = params.find("Unhandled exception during run");
                                if (pos != std::string::npos) {
                                    m_pPrinterInfo->error = msg_json["params"][0].get<std::string>();
                                }
                            }
                        }
                    }

                    std::string id = "\"id\": 7466";
                    std::string result = "result";
                    pos = ws.find(id.c_str());
                    size_t pos_result = ws.find(result.c_str());
                    if (pos != std::string::npos && pos_result != std::string::npos) {

                        if (json::accept(ws))
                        {
                            m_pWebServiceInfo->jsonPrinterInfoData = json::parse(ws);
                            if (!m_pWebServiceInfo->jsonPrinterInfoData["result"].is_null())
                            {
                                if (!m_pWebServiceInfo->jsonPrinterInfoData["result"].is_object())
                                {
                                    std::string a = m_pWebServiceInfo->jsonPrinterInfoData["result"].get<std::string>();
                                    BOOST_LOG_TRIVIAL(info) << a << endl;
                                }
                                else if (!m_pWebServiceInfo->jsonPrinterInfoData["result"]["status"].is_null())
                                {
                                    json status = m_pWebServiceInfo->jsonPrinterInfoData["result"]["status"];
                                    m_pPrinterInfo->extruder_temperature = status["extruder"]["temperature"];
                                    m_pPrinterInfo->bed_temperature = status["heater_bed"]["temperature"];
                                    //printerInfo->chamber_temperature = status["temperature_sensor Chamber_sensor"]["temperature"];
                                    if (status.contains("temperature_sensor Chamber_sensor") && status["temperature_sensor Chamber_sensor"].contains("temperature") && status["temperature_sensor Chamber_sensor"]["temperature"].is_number()) {
                                        m_pPrinterInfo->chamber_temperature = status["temperature_sensor Chamber_sensor"]["temperature"];
                                    }
                                    else {
                                        m_pPrinterInfo->chamber_temperature = m_pPrinterInfo->chamber_temperature;
                                    }
                                    m_pPrinterInfo->extruder_temperature_target = status["extruder"]["target"];
                                    m_pPrinterInfo->bed_temperature_target = status["heater_bed"]["target"];
                                    //m_pPrinterInfo->auxiliary_fan_speed = status["output_pin fan_assist"]["value"];
                                    if (status.contains("output_pin fan_assist") && status["output_pin fan_assist"].contains("value") && status["output_pin fan_assist"]["value"].is_number()) {
                                        m_pPrinterInfo->auxiliary_fan_speed = status["output_pin fan_assist"]["value"];
                                    }
                                    else {
                                        m_pPrinterInfo->auxiliary_fan_speed = m_pPrinterInfo->auxiliary_fan_speed;  // initialize value
                                    }
                                    //printerInfo->shield_fan_speed = status["fan_generic Chamber_fan"]["speed"];
                                    if (status.contains("fan_generic Chamber_fan") && status["fan_generic Chamber_fan"].contains("speed") && status["fan_generic Chamber_fan"]["speed"].is_number()) {
                                        m_pPrinterInfo->shield_fan_speed = status["fan_generic Chamber_fan"]["speed"];
                                    }
                                    else {
                                        m_pPrinterInfo->shield_fan_speed = m_pPrinterInfo->shield_fan_speed;  // initialize value
                                    }
                                    //m_pPrinterInfo->fan_speed = status["fan"]["speed"];
                                    if (status.contains("fan_generic cooling_fan") && status["fan_generic cooling_fan"].contains("speed") && status["fan_generic cooling_fan"]["speed"].is_number()) {
                                        m_pPrinterInfo->fan_speed = status["fan_generic cooling_fan"]["speed"];
                                    }
                                    else {
                                        m_pPrinterInfo->fan_speed = m_pPrinterInfo->fan_speed;  // initialize value
                                    }
                                    m_pPrinterInfo->print_speed = status["gcode_move"]["speed_factor"];
                                    m_pPrinterInfo->home_axes = status["toolhead"]["homed_axes"].get<std::string>();
                                    m_pPrinterInfo->estimated_print_time = status["toolhead"]["estimated_print_time"];
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
                        else
                        {
                            BOOST_LOG_TRIVIAL(info) << "JSON NOT ACCEPT" << endl;
                        }
                    }

                    //History Info
                    std::string jobs_history = "\"jobs\":";
                    std::string id_history = "\"id\": 5656";
                    size_t pos_id_history = ws.find(id_history.c_str());
                    size_t pos_history = ws.find(jobs_history.c_str());
                    if (pos_history != std::string::npos && !historyStart)
                    {
                        historyInfo = "";
                        historyStart = true;
                    }
                    if (historyStart)
                    {
                        historyInfo += ws;
                    }
                    if (pos_id_history != std::string::npos)
                        historyStart = false;
                    if (!historyInfo.empty() && !historyStart)
                    {
                        std::vector<HistoryInfo> _historyInfoList;
                        try {
                            json history_json;
                            if (json::accept(historyInfo))
                            {
                                history_json = json::parse(historyInfo);
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
                                }
                                else {
                                    m_kHistoryInfoList.clear();
                                    DebugOutput( "Invalid JSON format or missing 'jobs' array." );
                                }
                            }
                        }
                        catch (const std::exception& e) {
                            DebugOutput( "Parse error: ", e.what() );
                        }
                    }

                    //AMS Info
                    bool isConnected = false;
                    //std::cout << "GOT Data of CURLcode CheckAMSConnection" << std::endl;
                    std::string AMS_connected = "\\u6709\\u51e0\\u53f0AMS\\u5df2\\u7ecf\\u6253\\u5f00\\u4e32\\u53e3='1'";
                    std::string AMS_unconnect = "!! \\u6ca1\\u6709\\u8fde\\u63a5\\u4efb\\u4f55AMS\\u591a\\u8272\\uff0c\\u8fde\\u63a5AMS\\u5931\\u8d25";
                    size_t ams_pos = ws.find(AMS_connected.c_str());
                    if (ams_pos != std::string::npos) {
                        m_bIsConnetedToAMS = true;
                    }
                    ams_pos = ws.find(AMS_unconnect.c_str());
                    if (ams_pos != std::string::npos) {
                        m_bIsConnetedToAMS = false;
                    }

                    std::string ams_info = "entry_state";
                    ams_pos = ws.find(ams_info.c_str());
                    if (ams_pos != std::string::npos)
                    {
                        json ams_json;
                        if (json::accept(ws)) {
                            ams_json = json::parse(ws);

                            std::string entry_state = ams_json["params"][0].get<std::string>();
                            entry_state = entry_state.substr(entry_state.find("{"));
                            int _entry_state = 0;
                            int _park_state = 0;
                            if (json::accept(entry_state))
                            {
                                json info_json = json::parse(entry_state);
                                _entry_state = info_json["entry_state"];
                                _park_state = info_json["park_state"];
                            }

                            m_kAMSList_temp.clear();
                            for (int i = 1; i <= 4; i++)
                            {
                                AMSInfo _AMSInfo;
                                //_AMSInfo.color = ImColor(47, 53, 50, 255);
                                _AMSInfo.filament = "";
                                if (i == 1 && _park_state == 1)
                                    _AMSInfo.loading = true;
                                else if (i == 2 && _park_state == 2)
                                    _AMSInfo.loading = true;
                                else if (i == 3 && _park_state == 4)
                                    _AMSInfo.loading = true;
                                else if (i == 4 && _park_state == 8)
                                    _AMSInfo.loading = true;

                                //AMS 1-1 2-2: 3-4 4-8
                                if (i == 1 && (_entry_state == 1 || _entry_state == 3 || _entry_state == 5 || _entry_state == 9 || _entry_state == 7 || _entry_state == 11 || _entry_state == 13 || _entry_state == 15))
                                    _AMSInfo.install = true;
                                if (i == 2 && (_entry_state == 2 || _entry_state == 3 || _entry_state == 6 || _entry_state == 10 || _entry_state == 7 || _entry_state == 11 || _entry_state == 14 || _entry_state == 15))
                                    _AMSInfo.install = true;
                                if (i == 3 && (_entry_state == 4 || _entry_state == 5 || _entry_state == 6 || _entry_state == 12 || _entry_state == 7 || _entry_state == 13 || _entry_state == 14 || _entry_state == 15))
                                    _AMSInfo.install = true;
                                if (i == 4 && (_entry_state == 8 || _entry_state == 9 || _entry_state == 10 || _entry_state == 12 || _entry_state == 11 || _entry_state == 13 || _entry_state == 14 || _entry_state == 15))
                                    _AMSInfo.install = true;

                                _AMSInfo.selected = false;
                                m_kAMSList_temp.push_back(_AMSInfo);
                            }
                            m_kAMSList = m_kAMSList_temp;
                        }
                    }

                    //only for test
                    if (!m_kMonitorWindow.amsReturnError.empty()) {
                        ws = m_kMonitorWindow.amsReturnError;
                    }

                    std::string pause_prefix = "+PAUSE:";
                    size_t pause_pos = ws.find(pause_prefix);
                    if (pause_pos != std::string::npos) {
                        try {
                            std::tuple<std::string, std::string, std::string> pauseError = ParsePauseMessage(ws);
                            std::string code = std::get<0>(pauseError);
                            std::string oldCh = std::get<1>(pauseError);
                            std::string newCh = std::get<2>(pauseError);

                            std::cout << "Code: " << code << std::endl;
                            std::cout << "Old Channel: " << oldCh << std::endl;
                            std::cout << "New Channel: " << newCh << std::endl;

                            //to trigger notification
                            HandlePauseCode(code);

                            //{ "4", &isShownLoadFilamentErrorNotification },
                            //{ "8", &isShownUnloadFilamentErrorNotification },
                            if (code == "4") {
                                m_kMonitorWindow.AMSselectedID = std::stoi(newCh);
                                m_kMonitorWindow.AMS_ID = "\xC2\xA0" + std::to_string(m_kMonitorWindow.AMSselectedID) + "\xC2\xA0";
                            }
                            else if (code == "8") {
                                m_kMonitorWindow.AMSselectedID = std::stoi(oldCh);
                                m_kMonitorWindow.AMS_ID = "\xC2\xA0" + std::to_string(m_kMonitorWindow.AMSselectedID) + "\xC2\xA0";
                            }
                            
                            m_kMonitorWindow.error_code = "[" + code +  "]";

                            //only for test
                            if (!m_kMonitorWindow.amsReturnError.empty()) {
                                m_kMonitorWindow.amsReturnError.clear();
                            }
                        }
                        catch (const std::invalid_argument& e) {
                            DebugOutput( "Error (input1): ", e.what() );
                        }
                    }
                    
                }
                else if (res == CURLE_AGAIN)
                {
                    again++;
                    if (again > 30)
                    {
                        again = 0;
                        curl_easy_cleanup(m_pCurl_websocket);
                        curl_global_cleanup();
                        Initialconnect();
                    }
                }
                else if (res == CURLE_RECV_ERROR)
                {
                    BOOST_LOG_TRIVIAL(info) << "receive error: " << endl;
                    curl_easy_cleanup(m_pCurl_websocket);
                    curl_global_cleanup();

                    Initialconnect();
                }
            }
            else
            {
                BOOST_LOG_TRIVIAL(info) << "connect failed " << endl;
                m_bStartReceiving = false;
                m_bStartSending = false;
            }
        }
        catch (const std::runtime_error& e) {
            DebugOutput( "Error: " , e.what() );
        } catch (const std::invalid_argument& e) {
            DebugOutput( "Caught std::invalid_argument: " , e.what() );
        } catch (const std::exception& e) {
            DebugOutput( "Caught std::exception: " , e.what() );
        }

        //curl_easy_cleanup(curl);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
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
    while (!MonitorControl::m_bClose) {

        
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
                    _AMSInfo.install = true;
                if (i == 2 && (_entry_state == 2 || _entry_state == 3 || _entry_state == 6 || _entry_state == 10 || _entry_state == 7 || _entry_state == 11 || _entry_state == 14 || _entry_state == 15))
                    _AMSInfo.install = true;
                if (i == 3 && (_entry_state == 4 || _entry_state == 5 || _entry_state == 6 || _entry_state == 12 || _entry_state == 7 || _entry_state == 13 || _entry_state == 14 || _entry_state == 15))
                    _AMSInfo.install = true;
                if (i == 4 && (_entry_state == 8 || _entry_state == 9 || _entry_state == 10 || _entry_state == 12 || _entry_state == 11 || _entry_state == 13 || _entry_state == 14 || _entry_state == 15))
                    _AMSInfo.install = true;

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

        if (result == CURLE_OK)
        {
            m_pWebServiceInfo->jsonReturnInfoData = json::parse(*m_pWebServiceInfo->responseData.get());
            if (m_pWebServiceInfo->jsonReturnInfoData["result"] == exected_payload)
            {
                _result = true;
                BOOST_LOG_TRIVIAL(error) << "HTTP OK" << endl;
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

    try {
        auto nowTime = std::chrono::steady_clock::now();
        auto previousTime = std::chrono::steady_clock::now();
        int sendcnt = 0;
        while (m_bStartSending) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::lock_guard<std::mutex> lock(m_kCurlMutex);
            CURLcode result = send_action_Command(payload.dump());

            nowTime = std::chrono::steady_clock::now();
            long long timeDiff = std::chrono::duration_cast<std::chrono::seconds>(nowTime - previousTime).count();

            if (timeDiff > 5 && m_pPrinterInfo->state != "printing" || sendcnt < 3)
            {
                sendcnt++;
                result = send_action_Command(payload_history.dump());
                result = send_action_Command(payload_AMS.dump());
                previousTime = std::chrono::steady_clock::now();
            }
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
        std::string url = "http://" + m_pWebServiceInfo->ip + m_pWebServiceInfo->port_device + "/server/files/thumbnails?filename=" + gcode;
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

        if (result == CURLE_OK)
        {
            m_pWebServiceInfo->jsonThumbnailsInfoData = json::parse(*m_pWebServiceInfo->responseData.get());
            json j = m_pWebServiceInfo->jsonThumbnailsInfoData;

            if (j["result"].is_array()) {
                for (const auto& result : j["result"]) {

                    std::string image = result["thumbnail_path"].get<std::string>();
                    m_pPrinterInfo->thumbnail_path = result["thumbnail_path"].get<std::string>();
                }
            }
            else {
                DebugOutput( "Invalid JSON format or missing 'jobs' array."  );
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
        std::string url = "http://" + m_pWebServiceInfo->ip + m_pWebServiceInfo->port_device + "/server/files/gcodes/" + m_pPrinterInfo->thumbnail_path;
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

CURLcode zoffset(int value)
{
    std::string script = "SET_GCODE_OFFSET Z_ADJUST=" + std::to_string(value);
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

CURLcode SetFanSpeed(int value)
{
    std::string script = "M106 S" + std::to_string(value);
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

} // namespace MonitorControl

//} // namespace MonitorControl 

//} // namespace Slic3r


