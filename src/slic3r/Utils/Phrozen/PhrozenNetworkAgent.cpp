#include "PhrozenNetworkAgent.hpp"
#include "libslic3r/AppConfig.hpp"
#include "PhrozenMachineDatas.hpp"

#include <boost/log/trivial.hpp>
#include <sstream>
#include <codecvt>
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

// Forward declarations for ARP resolution functions in MonitorControl namespace
// TODO: Temporary setup to allow build pass on macOS. 
//       Future adjustment needed to ensure proper compilation and function calls.
namespace MonitorControl {
    bool TriggerArpResolution(const std::string& target_ip);
    bool WaitForArpResolution(const std::string& target_ip, int max_wait_ms);
}
#endif

using namespace Slic3r;

#pragma region PhrozenFrameProcessor
// ============================================
// ReceiveResponse() Processing Modules
// ============================================
// 
// Frame processing module for WebSocket frame handling
struct PhrozenFrameProcessor {
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
#pragma endregion

#pragma region CalibrationProgressCalculator
// Calibration progress calculator module
struct CalibrationProgressCalculator {
    // Calculate calibration progress based on probe points
    static void UpdateCalibrationProgress( const std::string& params, 
                                           PhrozenCalibrationProgressInfo* info, 
                                           const PhrozenPrinterInfo& kPrintInfo ) 
{
        // Check for probe at message: "// probe at X,Y is z=Z"
        size_t probe_pos = params.find("probe at ");
        if (probe_pos != std::string::npos) {
            info->heatingCompleted = true;
            
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
                        info->calibrationProgress = std::min(progress, 100.0f);
                    }
                }
            }
        }
        
        // Update calibration progress based on heating (if not completed)
        if (info->calibrationStatus == PhrozenCalibrationState::RUNNING && !info->heatingCompleted) {
            float bed = 0, extruder = 0;
            if ( kPrintInfo.bed_temperature_target > 0)
                bed = (static_cast<float>( kPrintInfo.bed_temperature) / 
                        kPrintInfo.bed_temperature_target) * 12.5f;
            if ( kPrintInfo.extruder_temperature_target > 0) {
                extruder = (static_cast<float>( kPrintInfo.extruder_temperature) / 
                            kPrintInfo.extruder_temperature_target) * 12.5f;
                if (extruder > 12.5f)
                    extruder = 12.5f;
            }
            float progress = bed + extruder;  // Max 25%
            if (progress > 1.0f) {
                info->calibrationProgress = progress;
            }
        }
    }
    
    // Calculate resonance compensation progress
    static void UpdateResonanceCompensationProgress(const std::string& params, PhrozenCalibrationProgressInfo* info) {
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
            info->startResonanceCompensation = true;
            _progress = 10.0f;  // X axis test starts at 10% (after initial preparation phase)
            info->startTime = std::chrono::steady_clock::now();
        } else if (params == "// Testing axis y") {
            _progress = 50.0f;  // Y axis test starts at 50% (after X-axis test completes)
            info->startTime = std::chrono::steady_clock::now();
        }
        
        // Check for frequency testing: "// Testing frequency X Hz"
        // Progress calculation: baseProgress + (currentHz / maxHz) * axisProgressRange
        // Example: X-axis at 75 Hz = 10% + (75/150) * 40% = 10% + 20% = 30%
        //          Y-axis at 75 Hz = 50% + (75/150) * 40% = 50% + 20% = 70%
        int Hz = 0;
        if (sscanf(params.c_str(), "// Testing frequency %d Hz", &Hz) == 1 && Hz > 0) {
            float progress = _progress + (static_cast<float>(Hz) / 150.0f) * 40.0f;
            if (progress > 1.0f) {
                info->resonanceCompensationProgress = progress;
            }
        }
        
        // Handle initial phase (progress < 10%): Preparation phase before X-axis test starts
        // Time-based progress: 1% per 3 seconds
        if (!info->startResonanceCompensation && info->resonanceCompensationProgress < 10.0f) {
            auto nowTime = std::chrono::steady_clock::now();
            long long timeDiff = std::chrono::duration_cast<std::chrono::seconds>(nowTime - info->startTime).count();
            if (timeDiff > 3) {
                float newProgress = info->resonanceCompensationProgress + 1.0f;
                if (newProgress > 1.0f) {
                    info->resonanceCompensationProgress = newProgress;
                    info->startTime = nowTime;
                }
            }
        }
        
        // Handle final phase (89% - 99%): Completion phase after Y-axis test
        // Time-based progress: 1% per 5.2 seconds (slower than initial phase)
        if (info->resonanceCompensationProgress >= 89.0f && info->resonanceCompensationProgress <= 99.0f) {
            auto nowTime = std::chrono::steady_clock::now();
            long long timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(nowTime - info->startTime).count();
            if (timeDiff > 5200) {  // 5.2 seconds per 1%
                float newProgress = info->resonanceCompensationProgress + 1.0f;
                if (newProgress > 1.0f) {
                    info->resonanceCompensationProgress = newProgress;
                    info->startTime = nowTime;
                }
            }
        }
        
        info->resonanceCompensationProgress = std::min(info->resonanceCompensationProgress, 100.0f);
    }
    
    // Calculate temperature calibration progress
    static void UpdateTemperatureCalibrationProgress(const std::string& params, PhrozenCalibrationProgressInfo* info) {
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
                if (info->tempProgress == 0 || info->tempProgress % 2 == 0)
                    info->tempProgress++;
                
                if (t0_value1 <= 210.0f) {
                    // Formula for subsequent cycles (tempProgress > 1): Heating from 205°C to 210°C
                    // progress = completedCyclesProgress + currentCycleProgress
                    // completedCyclesProgress = 8.0f * (tempProgress - 1)
                    //   Explanation: First cycle (tempProgress=1) gives 0, second cycle (tempProgress=2) gives 8%,
                    //                third cycle (tempProgress=3) gives 16%, etc.
                    // currentCycleProgress = ((currentTemp - 205°C) / (210°C - 205°C)) * 9.0f
                    //   Explanation: Linear interpolation from 205°C to 210°C, scaled to 9% of cycle
                    //   Example: At 207.5°C (halfway), progress = 0.5 * 9% = 4.5% within cycle
                    if (info->tempProgress > 1 && t0_value1 >= 205.0f && t0_value1 <= 210.0f) {
                        float progress = (8.0f * (static_cast<float>(info->tempProgress) - 1)) + 
                                         ((t0_value1 - 205.0f) / (t0_value2 - 205.0f)) * 9.0f;
                        if (progress > 1.0f) {
                            info->temperatureCalibrationProgress = progress;
                        }
                    }
                    // Formula for first cycle (tempProgress == 1): Heating from 0°C to 210°C
                    // progress = (currentTemp / targetTemp) * 9.0f
                    //   Explanation: Linear interpolation from 0°C to 210°C, scaled to 9% total
                    //   Example: At 105°C (halfway), progress = 0.5 * 9% = 4.5%
                    else if (info->tempProgress == 1) {
                        float progress = (t0_value1 / t0_value2) * 9.0f;
                        if (progress > 1.0f) {
                            info->temperatureCalibrationProgress = progress;
                        }
                    }
                }
            }
            // 205°C phase: Cooling from 210°C to 205°C
            else if (std::abs(t0_value2 - 205.0f) < 0.1f) {
                // Increment tempProgress when entering 205°C phase (odd -> even transition)
                if (info->tempProgress % 2 != 0)
                    info->tempProgress++;
                
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
                    float progress = (8.0f * (static_cast<float>(info->tempProgress) - 1)) + 
                                     ((210.0f - t0_value1) / (210.0f - t0_value2)) * 9.0f;
                    if (progress > 1.0f) {
                        info->temperatureCalibrationProgress = progress;
                    }
                }
            }
            
            info->temperatureCalibrationProgress = std::min(info->temperatureCalibrationProgress, 100.0f);
        }
    }
};
#pragma endregion

#pragma region PhrozenParsePauseMessage
std::tuple<std::string, std::string, std::string> PhrozenParsePauseMessage(const std::string& message)
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
#pragma endregion

#pragma region PhrozenHandlePauseCode
void PhrozenHandlePauseCode( const std::string& pauseCode , PhrozenMonitorWindow* pMonitorWindow )
{
    auto it = pMonitorWindow->pauseCodeToFlag.find(pauseCode);
    if (it != pMonitorWindow->pauseCodeToFlag.end()) {
        *(it->second) = true;
    }
}
#pragma endregion

#pragma region PhrozenMessageProcessor
// Message processing module for message type detection and conversion
struct PhrozenMessageProcessor {
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
    
    /*
        check if it gcode script response message, and get params result from whole message, 
        input:
            message: whole message;
        output:
            return if has gcode_response keyword
    */
    static bool IsGcodeResponseMessage( const std::string& message ) 
    {
        std::string find_message = "{\"jsonrpc\": \"2.0\", \"method\": \"notify_gcode_response\"";
        size_t pos = message.find(find_message.c_str());
        return pos != std::string::npos ;
    }

    /*
        Get params result from json message, 
        input:
            message: whole message;
            strParamsResult: for receive result
        output:
            return if it success found
    */
    static bool GetParamsResultMessage( const std::string& message, std::string& strParamsResult )
    {
        strParamsResult.clear();
        if (json::accept(message)) {
             try {
                 json msg_json = json::parse(message);
                 if (!msg_json["params"].is_null()) 
                 {
                     strParamsResult = msg_json["params"][0].get<std::string>();
                     return !strParamsResult.empty();
                 }
             } 
             catch (const json::exception& e) {
                 BOOST_LOG_TRIVIAL(warning) << "JSON parse error in gcode_response: " << e.what();
                 return false;
             }
        }
        return false;
    }

    /*
        Check LED State from param result msg
        input:
            strParamsResult: params from json message;
            bIsLedOn: for receive result
        output:
            return if it success process
    */
    static bool ProcessGcodeResponse_LEDState( const std::string& strParamsResult, bool& bIsLedOn )
    {
        std::string strLEDKeyword = "P0 LED_State=";
        auto uLEDPos = strParamsResult.find(strLEDKeyword);
        if ( uLEDPos != std::string::npos )
        {
            std::string strLEDValue = strParamsResult.substr(uLEDPos + strLEDKeyword.length(), 1);
            bIsLedOn = std::stoi(strLEDValue);
            return true;
        }
        return false;
    }

    /*
        Check nozzle detect filament or not
        input:
            strParamsResult:            params from json message;
            bIsNozzleDetectFilament:    for receive result
        output:
            return if it success process
    */
    static bool ProcessGcodeResponse_NozzleState( const std::string& strParamsResult, bool& bIsNozzleDetectFilament )
    {
        if ( strParamsResult.find("PRZ_ADC:") != std::string::npos 
             && strParamsResult.find("fila_exist") != std::string::npos) 
        {
            if (strParamsResult.find("fila_exist:True") != std::string::npos ||
                strParamsResult.find("fila_exist:true") != std::string::npos) {
                bIsNozzleDetectFilament = true;
            } else {
                bIsNozzleDetectFilament = false;
            }
            return true;
            BOOST_LOG_TRIVIAL(info) << "*** PRZ_ADC response: fila_exist = " << ( bIsNozzleDetectFilament ? "true" : "false") << " ***";
        }
        return false;
    }


    static bool ProcessGcodeResponse( const std::string& message, 
                                      PhrozenPrinterInfo* pPrinterInfo, 
                                      std::mutex& kCalibrationProgressMutex,
                                      PhrozenCalibrationProgressInfo* pCalibInfo ) {
        if ( !IsGcodeResponseMessage( message ) ) return false;
        if (json::accept(message)) {
            try {
                json msg_json = json::parse(message);
                if (!msg_json["params"].is_null()) {
                    std::string params = msg_json["params"][0].get<std::string>();
                    
                    // Check for Unhandled exception
                    size_t pos = params.find("Unhandled exception during run");
                    if (pos != std::string::npos) {
                        pPrinterInfo->error = msg_json["params"][0].get<std::string>();
                        return false;
                    }
                    // ============================================
                    // Calibration message processing
                    // ============================================
                    {
                        // Auto-leveling (Calibration) messages
                        if (params.find("Probe samples exceed samples_tolerance") != std::string::npos) {
                            std::lock_guard<std::mutex> lock(kCalibrationProgressMutex);
                            pCalibInfo->calibrationStatus = PhrozenCalibrationState::HAS_ERROR;
                            BOOST_LOG_TRIVIAL(warning) << "Calibration error: Probe samples exceed tolerance";
                        } else if (params.find("Mesh Bed Leveling Complete") != std::string::npos) {
                            std::lock_guard<std::mutex> lock(kCalibrationProgressMutex);
                            if ( pCalibInfo->calibrationStatus == PhrozenCalibrationState::RUNNING) {
                                 pCalibInfo->calibrationStatus = PhrozenCalibrationState::COMPLETED;
                                 pCalibInfo->calibrationProgress = 100.0f;
                                BOOST_LOG_TRIVIAL(info) << "Calibration completed";
                            }
                        } else if (params.find("probe at ") != std::string::npos) {
                            std::lock_guard<std::mutex> lock(kCalibrationProgressMutex);
                            CalibrationProgressCalculator::UpdateCalibrationProgress( params, pCalibInfo, *pPrinterInfo );
                        }
                        
                        // Resonance compensation messages
                        if (params.find("// Testing axis x") != std::string::npos ||
                            params.find("// Testing axis y") != std::string::npos ||
                            params.find("// Testing frequency") != std::string::npos) {
                            std::lock_guard<std::mutex> lock(kCalibrationProgressMutex);
                            CalibrationProgressCalculator::UpdateResonanceCompensationProgress(params, pCalibInfo );
                        } else if (params.find("with these parameters and restart the printer.") != std::string::npos) {
                            std::lock_guard<std::mutex> lock(kCalibrationProgressMutex);
                            if ( pCalibInfo->resonanceCompensationStatus == PhrozenCalibrationState::RUNNING) {
                                 pCalibInfo->resonanceCompensationStatus = PhrozenCalibrationState::COMPLETED;
                                 pCalibInfo->resonanceCompensationProgress = 100.0f;
                                BOOST_LOG_TRIVIAL(info) << "Resonance compensation completed";
                            }
                        }
                        
                        // Temperature calibration messages
                        if (params.find("T0:") != std::string::npos && params.find("/") != std::string::npos) {
                            std::lock_guard<std::mutex> lock(kCalibrationProgressMutex);
                            CalibrationProgressCalculator::UpdateTemperatureCalibrationProgress(params, pCalibInfo);
                        } else if (params.find("Klippy Disconnected") != std::string::npos) {
                            std::lock_guard<std::mutex> lock(kCalibrationProgressMutex);
                            if (pCalibInfo->temperatureCalibrationStatus == PhrozenCalibrationState::RUNNING) {
                                pCalibInfo->temperatureCalibrationStatus = PhrozenCalibrationState::COMPLETED;
                                pCalibInfo->temperatureCalibrationProgress = 100.0f;
                                BOOST_LOG_TRIVIAL(info) << "Temperature calibration completed";
                            }
                        }
                        
                        // Generic completion message
                        if (params.find("Klipper state: Disconnect") != std::string::npos) {
                            std::lock_guard<std::mutex> lock(kCalibrationProgressMutex);
                            if (pCalibInfo->calibrationStatus == PhrozenCalibrationState::RUNNING) {
                                pCalibInfo->calibrationStatus = PhrozenCalibrationState::COMPLETED;
                                pCalibInfo->calibrationProgress = 100.0f;
                            }
                            if (pCalibInfo->resonanceCompensationStatus == PhrozenCalibrationState::RUNNING) {
                                pCalibInfo->resonanceCompensationStatus = PhrozenCalibrationState::COMPLETED;
                                pCalibInfo->resonanceCompensationProgress = 100.0f;
                            }
                            if (pCalibInfo->temperatureCalibrationStatus == PhrozenCalibrationState::RUNNING) {
                                pCalibInfo->temperatureCalibrationStatus = PhrozenCalibrationState::COMPLETED;
                                pCalibInfo->temperatureCalibrationProgress = 100.0f;
                            }
                        }

                        return true;
                    }
                }
                return false;
            } catch (const json::exception& e) {
                BOOST_LOG_TRIVIAL(warning) << "JSON parse error in gcode_response: " << e.what();
                return false;
            }
        }
        return false;
    }
    
    static void ProcessHistoryInfo(const std::string& message,
                                  std::string& historyBuffer,
                                  std::vector<PhrozenHistoryInfo>* pHistoryInfoList,
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
            std::vector<PhrozenHistoryInfo> _historyInfoList;
            try {
                json history_json;
                if (json::accept(historyBuffer)) {
                    history_json = json::parse(historyBuffer);
                    if (history_json["result"]["jobs"].is_array()) {
                        for (const auto& job : history_json["result"]["jobs"]) {
                            PhrozenHistoryInfo _historyInfo;
                            std::string X = job["filename"].get<std::string>();
                            std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
                            _historyInfo.gcode_name = converter.from_bytes(X);
                            _historyInfo.status = job["status"].get<std::string>();
                            _historyInfo.fliament_used = job["filament_used"];
                            _historyInfo.total_duration = job["total_duration"];
                            _historyInfoList.push_back(_historyInfo);
                        }
                        *pHistoryInfoList = std::move( _historyInfoList );
                    } else {
                        pHistoryInfoList->clear();
                        BOOST_LOG_TRIVIAL(warning) << "Invalid JSON format or missing 'jobs' array.";
                        //DebugOutput("Invalid JSON format or missing 'jobs' array.");
                    }
                }
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(warning) << "Parse error: " << e.what();
                //DebugOutput("Parse error: ", e.what());
            }
        }
    }
    
    static bool ProcessPauseMessage(const std::string& message,  PhrozenMonitorWindow* pMonitorWindow) {
        
        std::string pause_prefix = "+PAUSE:";
        size_t pause_pos = message.find(pause_prefix);
        if (pause_pos == std::string::npos) return false;
        
        try {
            std::tuple<std::string, std::string, std::string> pauseError = PhrozenParsePauseMessage(message);
            std::string code = std::get<0>(pauseError);
            std::string oldCh = std::get<1>(pauseError);
            std::string newCh = std::get<2>(pauseError);
            
            std::cout << "Code: " << code << std::endl;
            std::cout << "Old Channel: " << oldCh << std::endl;
            std::cout << "New Channel: " << newCh << std::endl;
            
            PhrozenHandlePauseCode(code, pMonitorWindow);
            
            if (code == "4") {
                pMonitorWindow->AMSselectedID = std::stoi(newCh);
                pMonitorWindow->AMS_ID = "\xC2\xA0" + std::to_string(pMonitorWindow->AMSselectedID) + "\xC2\xA0";
            } else if (code == "8") {
                pMonitorWindow->AMSselectedID = std::stoi(oldCh);
                pMonitorWindow->AMS_ID = "\xC2\xA0" + std::to_string(pMonitorWindow->AMSselectedID) + "\xC2\xA0";
            }
            
            pMonitorWindow->error_code = "[" + code + "]";
            
            //only for test
            if (pMonitorWindow->amsReturnError.empty()) {
                pMonitorWindow->amsReturnError.clear();
            }
            return true;
        } catch (const std::invalid_argument& e) {
            BOOST_LOG_TRIVIAL(warning) << "Error (input1): " << e.what();
            return false;
            //DebugOutput("Error (input1): ", e.what());
        }
    }
};
#pragma endregion

#pragma region PhrozenAMSProcessor
// AMS processing module
struct PhrozenAMSProcessor {
    static void ProcessAMSConnectionStatus(const std::string& search_text,
                                          std::string& sliding_window_buffer,
                                          PhrozenAMSPatterns* pAmsPatterns,
                                          bool& bIsConnetedToAMS ) 
{
        size_t ams_pos = search_text.find( pAmsPatterns->AMS_connected.c_str());
        if (ams_pos != std::string::npos) {
            bIsConnetedToAMS = true;
            BOOST_LOG_TRIVIAL(info) << "AMS connected status detected";
            if (ams_pos < sliding_window_buffer.size()) {
                sliding_window_buffer = sliding_window_buffer.substr(
                    ams_pos + pAmsPatterns->AMS_connected.length()
                );
            }
        }
        
        ams_pos = search_text.find(pAmsPatterns->AMS_unconnect.c_str());
        if (ams_pos != std::string::npos) {
            bIsConnetedToAMS = false;
            BOOST_LOG_TRIVIAL(info) << "AMS disconnected status detected";
            if (ams_pos < sliding_window_buffer.size()) {
                sliding_window_buffer = sliding_window_buffer.substr(
                    ams_pos + pAmsPatterns->AMS_unconnect.length()
                );
            }
        }
    }
    
    static void ProcessAMSCommandStates(const std::string& search_text,
                                       std::string& sliding_window_buffer,
                                       PhrozenAMSPatterns* pAmsPatterns,
                                       std::vector<PhrozenAMSInfo>* pAMSList ) 
{
        // Unified lambda with slot number parsing and state update
        auto parseAMSCommandState = [&](const std::string& pattern, bool is_load = false, bool is_start = false) -> bool {
            size_t pos = search_text.find(pattern);
            if (pos == std::string::npos) return false;
            
            // Handle unload all patterns - update all slots
            if (pattern == pAmsPatterns->AMS_unload_all_start || pattern == pAmsPatterns->AMS_unload_all_end) {
                PhrozenAMSCommandState new_state = (pattern == pAmsPatterns->AMS_unload_all_start) ? PhrozenAMSCommandState::START : PhrozenAMSCommandState::FINISH;
                const char* state_name = (pattern == pAmsPatterns->AMS_unload_all_start) ? "START" : "FINISH";
                
                for (size_t i = 0; i < pAMSList->size(); i++) {
                    (*pAMSList)[i].unload_state = new_state;
                    
                    if(pattern == pAmsPatterns->AMS_unload_all_start){
                        (*pAMSList)[i].unload_state = PhrozenAMSCommandState::START;
                    }
                    if(pattern == pAmsPatterns->AMS_unload_all_end){
                        (*pAMSList)[i].loading = false;
                        (*pAMSList)[i].unload_state = PhrozenAMSCommandState::NONE;
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
                const size_t max_slots = (*pAMSList).size();
                if (slot_number > 0 && slot_number <= static_cast<int>(max_slots)) {
                    size_t slot_index = static_cast<size_t>(slot_number - 1);
                    PhrozenAMSCommandState& target_state = is_load
                        ? (*pAMSList)[slot_index].load_state
                        : (*pAMSList)[slot_index].unload_state;
                    target_state = is_start ? PhrozenAMSCommandState::START : PhrozenAMSCommandState::FINISH;
                    
                    //Update related fields based on state
                    if (!is_start) {
                        if (is_load) {
                            //load_state = START -> loading = true
                            (*pAMSList)[slot_index].loading = true;
                            (*pAMSList)[slot_index].load_state = PhrozenAMSCommandState::NONE;
                        } else {
                            //unload_state = finished -> loading = true
                            (*pAMSList)[slot_index].loading = false;
                            (*pAMSList)[slot_index].unload_state = PhrozenAMSCommandState::NONE;
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
        parseAMSCommandState(pAmsPatterns->AMS_unload_all_start);
        parseAMSCommandState(pAmsPatterns->AMS_unload_all_end);
        
        // Parse load/unload single slot states: (pattern, is_load, is_start)
        parseAMSCommandState(pAmsPatterns->AMS_load_single_start, true, true);     // P1Tn:0 -> load_single = START
        parseAMSCommandState(pAmsPatterns->AMS_load_single_end, true, false);      // P1Tn:1 -> load_single = FINISH
        parseAMSCommandState(pAmsPatterns->AMS_unload_single_start, false, true);  // P1Bn:0 -> unload_single = START
        parseAMSCommandState(pAmsPatterns->AMS_unload_single_end, false, false);   // P1Bn:1 -> unload_single = FINISH
    }
    
    static void ProcessAMSEntryParkState(const std::string& message ,
                                         bool& bIsConnetedToAMS,
                                         std::vector<PhrozenAMSInfo>* pAMSList )
    {
        //AMS1连接失败 unicode
        std::string ams_disconnect_prefix = "AMS1\\u8fde\\u63a5\\u5931\\u8d25";
        size_t ams_pos = message.find(ams_disconnect_prefix);
        if (ams_pos != std::string::npos){
            bIsConnetedToAMS = false;
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
                        bIsConnetedToAMS = true;
                    } else {
                        bIsConnetedToAMS = false;
                    }
                    
                    // Initialize AMS list if empty
                    std::vector<PhrozenAMSInfo> kAMSList_temp(4);
                    
                    for (int i = 1; i <= 4; i++) {
                        kAMSList_temp[i-1].filament = "";
                        
                        // Park state logic
                        kAMSList_temp[i-1].park = false;
                        if (i == 1 && (_park_state == 1 || _park_state == 3 || _park_state == 5 ||
                                       _park_state == 9 || _park_state == 7 || _park_state == 11 ||
                                       _park_state == 13 || _park_state == 15))
                            kAMSList_temp[i-1].park = true;
                        if (i == 2 && (_park_state == 2 || _park_state == 3 || _park_state == 6 ||
                                       _park_state == 10 || _park_state == 7 || _park_state == 11 ||
                                       _park_state == 14 || _park_state == 15))
                            kAMSList_temp[i-1].park = true;
                        if (i == 3 && (_park_state == 4 || _park_state == 5 || _park_state == 6 ||
                                       _park_state == 12 || _park_state == 7 || _park_state == 13 ||
                                       _park_state == 14 || _park_state == 15))
                            kAMSList_temp[i-1].park = true;
                        if (i == 4 && (_park_state == 8 || _park_state == 9 || _park_state == 10 ||
                                       _park_state == 12 || _park_state == 11 || _park_state == 13 ||
                                       _park_state == 14 || _park_state == 15))
                            kAMSList_temp[i-1].park = true;
                        
                        // Entry state logic
                        kAMSList_temp[i-1].entry = false;
                        if (i == 1 && (_entry_state == 1 || _entry_state == 3 || _entry_state == 5 ||
                                       _entry_state == 9 || _entry_state == 7 || _entry_state == 11 ||
                                       _entry_state == 13 || _entry_state == 15))
                            kAMSList_temp[i-1].entry = true;
                        if (i == 2 && (_entry_state == 2 || _entry_state == 3 || _entry_state == 6 ||
                                       _entry_state == 10 || _entry_state == 7 || _entry_state == 11 ||
                                       _entry_state == 14 || _entry_state == 15))
                            kAMSList_temp[i-1].entry = true;
                        if (i == 3 && (_entry_state == 4 || _entry_state == 5 || _entry_state == 6 ||
                                       _entry_state == 12 || _entry_state == 7 || _entry_state == 13 ||
                                       _entry_state == 14 || _entry_state == 15))
                            kAMSList_temp[i-1].entry = true;
                        if (i == 4 && (_entry_state == 8 || _entry_state == 9 || _entry_state == 10 ||
                                       _entry_state == 12 || _entry_state == 11 || _entry_state == 13 ||
                                       _entry_state == 14 || _entry_state == 15))
                            kAMSList_temp[i-1].entry = true;
                        
                        
                        kAMSList_temp[i-1].selected = false;
                        
                        if(!(*pAMSList).empty()){
                            (*pAMSList)[i-1].park = kAMSList_temp[i-1].park;
                            (*pAMSList)[i-1].entry = kAMSList_temp[i-1].entry;
                            (*pAMSList)[i-1].selected = kAMSList_temp[i-1].selected;
                        }
                    }
                    if((*pAMSList).empty()){
                        (*pAMSList) = kAMSList_temp;
                    }
                } catch (const json::exception& e) {
                    BOOST_LOG_TRIVIAL(warning) << "JSON parse error in AMS entry_state: " << e.what();
                }
            }
        }
    }
};
#pragma endregion

#pragma region PhrozenPrinterStatusExtractor
struct PhrozenPrinterStatusExtractor {
    static void ExtractTemperatureInfo(const json& status, PhrozenPrinterInfo* kInfo) {
        if (status.contains("extruder") && status["extruder"].contains("temperature")) {
            kInfo->extruder_temperature = status["extruder"]["temperature"];
        }
        if (status.contains("extruder") && status["extruder"].contains("target")) {
            kInfo->extruder_temperature_target = status["extruder"]["target"];
        }
        if (status.contains("heater_bed") && status["heater_bed"].contains("temperature")) {
            kInfo->bed_temperature = status["heater_bed"]["temperature"];
        }
        if (status.contains("heater_bed") && status["heater_bed"].contains("target")) {
            kInfo->bed_temperature_target = status["heater_bed"]["target"];
        }
        if (status.contains("temperature_sensor Chamber_sensor") &&
            status["temperature_sensor Chamber_sensor"].contains("temperature") &&
            status["temperature_sensor Chamber_sensor"]["temperature"].is_number()) {
            kInfo->chamber_temperature = status["temperature_sensor Chamber_sensor"]["temperature"];
        }
    }
    
    static void ExtractFanSpeedInfo(const json& status, PhrozenPrinterInfo* kInfo) {
        if (status.contains("output_pin fan_assist") &&
            status["output_pin fan_assist"].contains("value") &&
            status["output_pin fan_assist"]["value"].is_number()) {
            kInfo->auxiliary_fan_speed = status["output_pin fan_assist"]["value"];
        }
        if (status.contains("fan_generic Chamber_fan") &&
            status["fan_generic Chamber_fan"].contains("speed") &&
            status["fan_generic Chamber_fan"]["speed"].is_number()) {
            kInfo->shield_fan_speed = status["fan_generic Chamber_fan"]["speed"];
        }
        if (status.contains("fan_generic cooling_fan") &&
            status["fan_generic cooling_fan"].contains("speed") &&
            status["fan_generic cooling_fan"]["speed"].is_number()) {
            kInfo->fan_speed = status["fan_generic cooling_fan"]["speed"];
        }
    }
    
    static void ExtractGcodeMoveInfo(const json& status, PhrozenPrinterInfo* kInfo) {
        if (status.contains("gcode_move")) {
            if (status["gcode_move"].contains("speed_factor")) {
                kInfo->print_speed = status["gcode_move"]["speed_factor"];
            }
            if (status["gcode_move"].contains("homing_origin") &&
                status["gcode_move"]["homing_origin"].is_array() &&
                status["gcode_move"]["homing_origin"].size() > 2) {
                kInfo->z_offsetValue = status["gcode_move"]["homing_origin"][2];
            }
        }
    }
    
    static void ExtractToolheadInfo(const json& status, PhrozenPrinterInfo* kInfo) {
        if (status.contains("toolhead")) {
            if (status["toolhead"].contains("homed_axes")) {
                kInfo->home_axes = status["toolhead"]["homed_axes"].get<std::string>();
            }
            if (status["toolhead"].contains("estimated_print_time")) {
                kInfo->estimated_print_time = status["toolhead"]["estimated_print_time"];
            }
        }
    }
    
    static void ExtractPrintStatusInfo(const json& status,  PhrozenPrinterInfo* kInfo, std::string& prev_state, bool& bThumbnailChecking  ) {
        // Static variables to track previous values for change detection
        
        if (status.contains("display_status") && status["display_status"].contains("progress")) {
            kInfo->print_progress = status["display_status"]["progress"];
        }
        if (status.contains("pause_resume") && status["pause_resume"].contains("is_paused")) {
            kInfo->is_paused = status["pause_resume"]["is_paused"];
        }
        if (status.contains("print_stats")) {
            std::string new_state;
            std::string new_print_file;
            
            if (status["print_stats"].contains("state")) {
                new_state = status["print_stats"]["state"].get<std::string>();
                kInfo->state = new_state;
            }
            if (status["print_stats"].contains("filename")) {
                kInfo->print_file = status["print_stats"]["filename"];
            }
            if (status["print_stats"].contains("print_duration")) {
                kInfo->print_time = status["print_stats"]["print_duration"];
            }
            if (status["print_stats"].contains("total_duration")) {
                kInfo->total_time = status["print_stats"]["total_duration"];
            }
            if (status["print_stats"].contains("filament_used")) {
                kInfo->print_filament = status["print_stats"]["filament_used"];
            }
            
            // Detect state change to trigger thumbnail check
            if(kInfo->isSameIP){
                if ( (prev_state == "standby" || prev_state == "offline" ||
                      prev_state == "paused" || prev_state == "cancelled" || prev_state.empty()) &&
                         (new_state == "printing" || new_state == "complete" )) {
                    bThumbnailChecking = true;
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
                    bThumbnailChecking = true;
                    BOOST_LOG_TRIVIAL(info) << "ExtractPrintStatusInfo: Print state changed from \""
                                            << prev_state << "\" to \"" << new_state << "\"";
                    // Update previous values
                    prev_state = new_state;
                    kInfo->isSameIP = true;
                }
            }
        }
    }
    
    static bool ProcessPrinterStatus(const std::string& message, 
                                     PhrozenWebServiceInfo* pWebService, 
                                     PhrozenPrinterInfo* pPrinterInfo,
                                     std::string& prev_state, 
                                     bool& bThumbnailChecking) 
    {
        bool bIsUpdate = false;
        std::string id = "\"id\": 7466";
        std::string result = "result";
        size_t pos = message.find(id.c_str());
        size_t pos_result = message.find(result.c_str());
        if (pos == std::string::npos || pos_result == std::string::npos) return bIsUpdate;
        
        if (json::accept(message)) {
            try {
                pWebService->jsonPrinterInfoData = json::parse(message);
                if (!pWebService->jsonPrinterInfoData["result"].is_null()) {
                    if (!pWebService->jsonPrinterInfoData["result"].is_object()) {
                        std::string a = pWebService->jsonPrinterInfoData["result"].get<std::string>();
                        BOOST_LOG_TRIVIAL(info) << a << endl;
                    } else if (!pWebService->jsonPrinterInfoData["result"]["status"].is_null()) {
                        json status = pWebService->jsonPrinterInfoData["result"]["status"];
                        
                        ExtractTemperatureInfo(status, pPrinterInfo);
                        ExtractFanSpeedInfo(status, pPrinterInfo);
                        ExtractGcodeMoveInfo(status, pPrinterInfo);
                        ExtractToolheadInfo(status, pPrinterInfo);
                        ExtractPrintStatusInfo(status, pPrinterInfo, prev_state, bThumbnailChecking );
                        pPrinterInfo->error = "";
                        bIsUpdate = true;
                    }
                }
            } catch (const json::exception& e) {
                BOOST_LOG_TRIVIAL(warning) << "JSON parse error in printer status: " << e.what();
            }
        } else {
            BOOST_LOG_TRIVIAL(info) << "JSON NOT ACCEPT" << endl;
        }
        return bIsUpdate;
    }

};
#pragma endregion

#pragma region PhrozenNetworkAgent

// Constructor
PhrozenNetworkAgent::PhrozenNetworkAgent(std::string log_dir)
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
}

bool PhrozenNetworkAgent::InitializeConnector( const std::string& strIp  )
{
    // reset all info
    m_spWebServiceInfo = std::make_unique< PhrozenWebServiceInfo >();
    m_spPrinterInfo = std::make_unique< PhrozenPrinterInfo >();
    m_spThreadControl = std::make_unique< PhrozenThreadControl >();
    m_spMonitorWindow = std::make_unique< PhrozenMonitorWindow >();
    m_spCalibrationProgressInfo = std::make_unique< PhrozenCalibrationProgressInfo >();

    if ( !m_spAmsPatterns ) m_spAmsPatterns = std::make_unique< PhrozenAMSPatterns >();
    
    if ( m_bIsTestMode )
    {
        uTestCounter = 0;
        return true;
    }
    


    //TODO check m_spPrinterInfo: pre_printerIP, isSameIP does it has affect?

    //trigger ams update query command after connect to speicified IP address (Printer)
    SetFirstTimeToSendQuery( true );
    CleanupWebSocketConnection();
    m_spWebServiceInfo->ip = strIp;
    m_strPrev_state.clear();
    bool bSuccess = InitializeConnectorImp( strIp );
    if ( !bSuccess )
    {
        m_spWebServiceInfo->ip = "";
        m_spMonitorWindow->connectedMachineName = "";
        m_spMonitorWindow->isShownIPConnectNotification = true;
        SetStartSending(false);
        SetStartReceiving(false);
        
    }
    return bSuccess;
}

bool PhrozenNetworkAgent::InitializeConnectorImp( const std::string& strIp )
{
    auto fnCheckCurlSuccess =[&]( CURLcode& res ) -> bool
    {
        bool bSuccess = res == CURLcode::CURLE_OK;
        if ( !bSuccess ){  m_spWebServiceInfo->ip = ""; }
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
        MonitorControl::TriggerArpResolution(m_spWebServiceInfo->ip);
        
        // Waiting for the ARP table to be updated and for confirmation that ARP resolution is complete (maximum wait 1000ms).
        if (MonitorControl::WaitForArpResolution(m_spWebServiceInfo->ip, 1000)) {
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

    m_spWebServiceInfo->reset();
}

void PhrozenNetworkAgent::SetStartSending( bool bStart )
{
    m_bStartSending.store(bStart, std::memory_order_relaxed);
}

bool PhrozenNetworkAgent::IsStartSending()
{
    return m_bStartSending.load(std::memory_order_relaxed);
}

void PhrozenNetworkAgent::SetStartReceiving( bool bStart )
{
    m_bStartReceiving.store(bStart, std::memory_order_relaxed);
}

bool PhrozenNetworkAgent::IsStartReceiving()
{
    return m_bStartReceiving.load(std::memory_order_relaxed);
}

void PhrozenNetworkAgent::SetFirstTimeToSendQuery( bool flag )
{
    m_bFirstTimeToSendQuery.store( flag, std::memory_order_relaxed);
}

bool PhrozenNetworkAgent::IsFirstTimeToSendQuery()
{
    return m_bFirstTimeToSendQuery.load(std::memory_order_relaxed);
}

void PhrozenNetworkAgent::SetThumbnailChecking( bool bCheck )
{
    m_bDoThumbnailCheck.store(bCheck, std::memory_order_relaxed);
}

bool PhrozenNetworkAgent::IsThumbnailChecking()
{
    return m_bDoThumbnailCheck.load(std::memory_order_relaxed);
}

void PhrozenNetworkAgent::SetConnectedToAms( bool connected )
{
    m_bIsConnetedToAMS.store(connected, std::memory_order_relaxed);
}

bool PhrozenNetworkAgent::IsConnetedToAMS()
{
    return m_bIsConnetedToAMS.load(std::memory_order_relaxed);
}

void PhrozenNetworkAgent::SetIsMachineLED_On( bool isOn )
{
    m_bIsMachineLED_On.store(isOn, std::memory_order_relaxed);
}

bool PhrozenNetworkAgent::IsMachineLED_On()
{
    return m_bIsMachineLED_On.load(std::memory_order_relaxed);
}

void PhrozenNetworkAgent::SetIsNozzleDetectFilament( bool isDetected )
{
    m_bIsNozzleDetectFilament.store(isDetected, std::memory_order_relaxed);
}

bool PhrozenNetworkAgent::IsNozzleDetectFilament()
{
    return m_bIsNozzleDetectFilament.load(std::memory_order_relaxed);
}


void PhrozenNetworkAgent::RunSendMessage( const std::vector< json >& kMessageList,
                                          const std::vector< bool >& kSendingList )
{

    if ( m_bIsTestMode ) return;

    try {
        size_t uCount = kMessageList.size();
        if ( kSendingList.size() != uCount ) 
        {
            DebugOutput( "Sending list not same count: " + std::to_string( uCount ) );
            return;
        }

        // CRITICAL: libcurl easy handle is NOT thread-safe
        // Cannot call curl_ws_send()/curl_ws_recv() from multiple threads simultaneously
        // Operating the same curl handle from 2 threads may cause crash risk
        std::lock_guard<std::mutex> lock( m_kCurlMutex );
        CURLcode result;
        for ( auto i = 0; i < uCount; ++i )
        {
            if ( !kSendingList[i] ) continue;
            result = send_action_Command(  kMessageList[ i ].dump()  );
            if ( result != CURLcode::CURLE_OK )
            {
                DebugOutput( "Message sending fail, id= " + std::to_string( i ) );
                BOOST_LOG_TRIVIAL(warning) << "Message sending fail, id= " << i;
            }
        }
    } catch (const std::invalid_argument& e) {
        DebugOutput( "Caught std::invalid_argument: " , e.what());
    } catch (const std::exception& e) {
        DebugOutput( "Caught std::exception: " , e.what() );
    }
 
}

void PhrozenNetworkAgent::InitializeForReceiveResponse()
{
    // local function param for RunReceiveResponse.
    // because RunReceiveResponse will process use thread from outside
    // so initialize here.
    m_strAms_message_buffer.clear();
    m_strHistoryInfo.clear();
    m_bHistoryStart = false;
    m_nAgain = 0;
    m_strSliding_window_buffer.clear();

    // other param
    m_kHistoryList.clear();
}

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
void PhrozenNetworkAgent::RunReceiveResponse()
{
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
    
    m_bIsPrinterInfoChanged = false;
    m_bIsCalibrationProgressInfoChanged = false;
    m_bIsAMSInfoListChenaged = false;
    m_bIsMonitorWindowChanged = false;

    if( !IsFirstTimeToSendQuery() ){

        // CRITICAL: libcurl easy handle is NOT thread-safe
        // Cannot call curl_ws_send()/curl_ws_recv() from multiple threads simultaneously
        // Operating the same curl handle from 2 threads may cause crash risk
        std::lock_guard<std::mutex> lock( m_kCurlMutex );

        BOOST_LOG_TRIVIAL(debug) << "ReceiveResponse: Lock acquired, Thread ID: " << thread_id;

        try {
            double connectTime = 0;
            curl_easy_getinfo( m_pCurlMainWebsocket, CURLINFO_CONNECT_TIME, &connectTime);
            
            if (connectTime > 0) {
                res = curl_ws_recv( m_pCurlMainWebsocket, buffer, sizeof(buffer), &rlen, &meta);
                
                if (res == CURLE_OK) {
                    m_nAgain = 0;
                    
                    // ============================================
                    // Frame Fragmentation Handling
                    // ============================================
                    std::string frame_data(&buffer[0], &buffer[rlen]);
                    bool is_text_frame = (meta->flags & CURLWS_TEXT) != 0;
                    bool is_binary_frame = (meta->flags & CURLWS_BINARY) != 0;
                    bool is_continuation_frame = PhrozenFrameProcessor::IsContinuationFrame(meta);
                    bool is_final_frame = PhrozenFrameProcessor::IsFinalFrame(meta);
                    
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
                        m_strAms_message_buffer += frame_data;
                        BOOST_LOG_TRIVIAL(debug) << "Accumulating continuation frame. "
                        << "Buffer size: " << m_strAms_message_buffer.size();
                        
                        // Prevent buffer from growing too large
                        if (m_strAms_message_buffer.size() > MAX_SLIDING_WINDOW_SIZE) {
                            BOOST_LOG_TRIVIAL(warning) << "AMS message buffer exceeded max size, truncating";
                            m_strAms_message_buffer = m_strAms_message_buffer.substr(m_strAms_message_buffer.size() - MAX_SLIDING_WINDOW_SIZE / 2);
                        }
                        return;  // To next loop for wait for more frames
                    }
                    
                    // ============================================
                    // Complete Message Combination
                    // ============================================
                    std::string complete_message = PhrozenFrameProcessor::CombineFrames(frame_data, m_strAms_message_buffer);
                    PhrozenFrameProcessor::UpdateSlidingWindow(m_strSliding_window_buffer, complete_message, MAX_SLIDING_WINDOW_SIZE);
                    
                    // ============================================
                    // Message Conversion
                    // ============================================
                    std::string ws = complete_message;
                    
                    // ============================================
                    // Message Type Detection
                    // ============================================
                    bool skip_proc_stat_processing = PhrozenMessageProcessor::ShouldSkipProcStat(ws);
                    if (skip_proc_stat_processing) {
                        BOOST_LOG_TRIVIAL(debug) << "Found notify_proc_stat_update, skipping proc_stat processing";
                        return;
                    }
                    
                    // ============================================
                    // G-code Response and Printer Status Processing
                    // ============================================
                    if (!skip_proc_stat_processing) {
                        BOOST_LOG_TRIVIAL(info) << "receive: " << ws << endl;

                        if ( PhrozenMessageProcessor::IsGcodeResponseMessage( ws ) )
                        {
                            std::string strParams;
                            bool bIsMachineLedOn;
                            bool bIsNozzleDetectFilament;
                            if ( PhrozenMessageProcessor::GetParamsResultMessage( ws, strParams ) )
                            {
                                if ( PhrozenMessageProcessor::ProcessGcodeResponse_LEDState( strParams, bIsMachineLedOn ) )
                                {   
                                    SetIsMachineLED_On( bIsMachineLedOn );
                                    return;
                                }
                                else if ( PhrozenMessageProcessor::ProcessGcodeResponse_NozzleState( strParams, bIsNozzleDetectFilament ) )
                                {
                                    SetIsNozzleDetectFilament( bIsNozzleDetectFilament );
                                    return;
                                }
                                else if ( PhrozenMessageProcessor::ProcessGcodeResponse(ws,
                                                                      m_spPrinterInfo.get(),
                                                                      m_kCalibrationProgressMutex,
                                                                      m_spCalibrationProgressInfo.get() )  )
                                {
                                    m_bIsCalibrationProgressInfoChanged = true;
                                    return;
                                }
                            }
                        }

                        bool bDoThumbnailCheck = IsThumbnailChecking();
                        m_bIsPrinterInfoChanged = PhrozenPrinterStatusExtractor::ProcessPrinterStatus( ws,
                                                                                                       m_spWebServiceInfo.get(),
                                                                                                       m_spPrinterInfo.get(), 
                                                                                                       m_strPrev_state,
                                                                                                       bDoThumbnailCheck );
                        SetThumbnailChecking( bDoThumbnailCheck );

                    }
                    
                    // ============================================
                    // History Info Processing (independent of skip_proc_stat)
                    // ============================================
                    //TODO history now just get value but no use
                    PhrozenMessageProcessor::ProcessHistoryInfo(ws, m_strHistoryInfo, &m_kHistoryList, m_bHistoryStart);
                    
                    // ============================================
                    // AMS Processing (independent of skip_proc_stat)
                    // ============================================
                    // Search in both the current complete message and sliding window buffer
                    // This ensures we catch patterns even if they span frame boundaries
                    std::string search_text = ws;
                    if (m_strSliding_window_buffer.size() > ws.size()) {
                        search_text = m_strSliding_window_buffer;
                    }
                    
                    bool bIsConnetedToAMS = IsConnetedToAMS();
                    PhrozenAMSProcessor::ProcessAMSConnectionStatus(search_text, m_strSliding_window_buffer, m_spAmsPatterns.get(), bIsConnetedToAMS );
                    PhrozenAMSProcessor::ProcessAMSCommandStates(search_text, m_strSliding_window_buffer,  m_spAmsPatterns.get(), &m_kAMSList );
                    PhrozenAMSProcessor::ProcessAMSEntryParkState(ws, bIsConnetedToAMS, &m_kAMSList );
                    m_bIsAMSInfoListChenaged = bIsConnetedToAMS;
                    SetConnectedToAms( bIsConnetedToAMS );
                    
                    // ============================================
                    // Pause Message Processing
                    // ============================================
                    m_bIsMonitorWindowChanged = PhrozenMessageProcessor::ProcessPauseMessage(ws, m_spMonitorWindow.get() );
                }
                else if (res == CURLE_AGAIN) {
                    m_nAgain++;
                    // Log for macOS Xcode console
                    BOOST_LOG_TRIVIAL(debug) << "ReceiveResponse: CURLE_AGAIN, Thread ID: " << thread_id << ", again count: " << m_nAgain;
                    
                    if (m_nAgain > 30) {
                        // Log for macOS Xcode console (log before resetting again)
                        BOOST_LOG_TRIVIAL(warning) << "Too many CURLE_AGAIN (count: " << m_nAgain << ")";
                        m_nAgain = 0;
                    }
                }
                else if (res == CURLE_RECV_ERROR) {
                    std::terminate();
                    BOOST_LOG_TRIVIAL(info) << "receive error: " << endl;
                    curl_easy_cleanup(m_pCurlMainWebsocket);
                    auto strIp = m_spWebServiceInfo->ip;
                    InitializeConnectorImp( strIp );
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

CURLcode PhrozenNetworkAgent::send_action_Command( std::string send_payload )
{
    //CURLcode res = curl_easy_perform(curl);
    CURLcode result = CURLE_AGAIN;
    double connectTime = 0;
    try {
        curl_easy_getinfo(m_pCurlMainWebsocket, CURLINFO_CONNECT_TIME, &connectTime);
        if (connectTime > 0)
        {
            size_t sent;
            result = curl_ws_send(m_pCurlMainWebsocket, send_payload.c_str(), strlen(send_payload.c_str()), &sent, 0, CURLWS_TEXT);
        }
        //curl_easy_cleanup(curl);
    }
    catch (const std::exception& e) {
        DebugOutput( "send error: " , e.what());
    }
    return result;
}

void PhrozenNetworkAgent::DebugOutput(const std::string& prefix, const char* message  ) {
    std::string combined = prefix + message;
#ifdef _WIN32
    OutputDebugStringA(combined.c_str());
#else
    // to do
    // need to modify/create a version for macOS to achieve the same effect as Windows.
    std::cout << combined << std::endl;
#endif
}

void PhrozenNetworkAgent::GetPrinterInfoData( PhrozenPrinterInfo& kData )
{
    if ( !m_spPrinterInfo ) return;
    kData = *m_spPrinterInfo;
}

void PhrozenNetworkAgent::GetCalibrationProgressInfoData( PhrozenCalibrationProgressInfo& kData )
{
    if ( !m_spCalibrationProgressInfo ) return;
    kData = *m_spCalibrationProgressInfo;
}
void PhrozenNetworkAgent::GetAMSInfoList( std::vector< PhrozenAMSInfo >& kData )
{
    kData = m_kAMSList;
} 
void PhrozenNetworkAgent::GetMonitorWindowData( PhrozenMonitorWindow& kData )
{
    if ( !m_spMonitorWindow ) return;
    kData = *m_spMonitorWindow;
}

#pragma endregion
