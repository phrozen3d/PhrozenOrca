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
    static void UpdateCalibrationProgress(const std::string& params, CalibrationProgressInfo* info, const PhrozenPrinterInfo& kPrintInfo ) {
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
        if (info->calibrationStatus == CalibrationState::RUNNING && !info->heatingCompleted) {
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
    static void UpdateResonanceCompensationProgress(const std::string& params, CalibrationProgressInfo* info) {
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
    static void UpdateTemperatureCalibrationProgress(const std::string& params, CalibrationProgressInfo* info) {
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
    
    static void ProcessGcodeResponse( const std::string& message, 
                                      PhrozenPrinterInfo* pPrinterInfo, 
                                      std::mutex& kCalibrationProgressMutex,
                                      CalibrationProgressInfo* pCalibInfo ) {
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
                        pPrinterInfo->error = msg_json["params"][0].get<std::string>();
                    }

                    // Check LED State
                    std::string strLEDKeyword = "P0 LED_State=";
                    auto uLEDPos = params.find(strLEDKeyword);
                    if ( uLEDPos != std::string::npos )
                    {
                        std::string strLEDValue = params.substr(uLEDPos + strLEDKeyword.length(), 1);
                        pPrinterInfo->bIsLedOn = std::stoi(strLEDValue);
                        return;
                    }
                    
                    // Check for PRZ_ADC response with fila_exist
                    if (params.find("PRZ_ADC:") != std::string::npos && params.find("fila_exist") != std::string::npos) {
                        if (params.find("fila_exist:True") != std::string::npos ||
                            params.find("fila_exist:true") != std::string::npos) {
                            pPrinterInfo->bIsNozzleDetectFilament = true;
                        } else {
                            pPrinterInfo->bIsNozzleDetectFilament = false;
                        }
                        BOOST_LOG_TRIVIAL(info) << "*** PRZ_ADC response: fila_exist = " << ( pPrinterInfo->bIsNozzleDetectFilament ? "true" : "false") << " ***";
                    }
                    
                    // ============================================
                    // Calibration message processing
                    // ============================================
                    {
                        // Auto-leveling (Calibration) messages
                        if (params.find("Probe samples exceed samples_tolerance") != std::string::npos) {
                            std::lock_guard<std::mutex> lock(kCalibrationProgressMutex);
                            pCalibInfo->calibrationStatus = CalibrationState::HAS_ERROR;
                            BOOST_LOG_TRIVIAL(warning) << "Calibration error: Probe samples exceed tolerance";
                        } else if (params.find("Mesh Bed Leveling Complete") != std::string::npos) {
                            std::lock_guard<std::mutex> lock(kCalibrationProgressMutex);
                            if ( pCalibInfo->calibrationStatus == CalibrationState::RUNNING) {
                                 pCalibInfo->calibrationStatus = CalibrationState::COMPLETED;
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
                            if ( pCalibInfo->resonanceCompensationStatus == CalibrationState::RUNNING) {
                                 pCalibInfo->resonanceCompensationStatus = CalibrationState::COMPLETED;
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
                            if (pCalibInfo->temperatureCalibrationStatus == CalibrationState::RUNNING) {
                                pCalibInfo->temperatureCalibrationStatus = CalibrationState::COMPLETED;
                                pCalibInfo->temperatureCalibrationProgress = 100.0f;
                                BOOST_LOG_TRIVIAL(info) << "Temperature calibration completed";
                            }
                        }
                        
                        // Generic completion message
                        if (params.find("Klipper state: Disconnect") != std::string::npos) {
                            std::lock_guard<std::mutex> lock(kCalibrationProgressMutex);
                            if (pCalibInfo->calibrationStatus == CalibrationState::RUNNING) {
                                pCalibInfo->calibrationStatus = CalibrationState::COMPLETED;
                                pCalibInfo->calibrationProgress = 100.0f;
                            }
                            if (pCalibInfo->resonanceCompensationStatus == CalibrationState::RUNNING) {
                                pCalibInfo->resonanceCompensationStatus = CalibrationState::COMPLETED;
                                pCalibInfo->resonanceCompensationProgress = 100.0f;
                            }
                            if (pCalibInfo->temperatureCalibrationStatus == CalibrationState::RUNNING) {
                                pCalibInfo->temperatureCalibrationStatus = CalibrationState::COMPLETED;
                                pCalibInfo->temperatureCalibrationProgress = 100.0f;
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
    
    static void ProcessPauseMessage(const std::string& message,  PhrozenMonitorWindow* pMonitorWindow) {
        
        std::string pause_prefix = "+PAUSE:";
        size_t pause_pos = message.find(pause_prefix);
        if (pause_pos == std::string::npos) return;
        
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
        } catch (const std::invalid_argument& e) {
            BOOST_LOG_TRIVIAL(warning) << "Error (input1): " << e.what();
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
                kInfo->z_offsetValure = status["gcode_move"]["homing_origin"][2];
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
    
    static void ProcessPrinterStatus(const std::string& message, 
                                     PhrozenWebServiceInfo* pWebService, 
                                     PhrozenPrinterInfo* pPrinterInfo,
                                     std::string& prev_state, 
                                     bool& bThumbnailChecking) 
{
        std::string id = "\"id\": 7466";
        std::string result = "result";
        size_t pos = message.find(id.c_str());
        size_t pos_result = message.find(result.c_str());
        if (pos == std::string::npos || pos_result == std::string::npos) return;
        
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
#pragma endregion

#pragma region PhrozenNetworkAgent
// Constructor
PhrozenNetworkAgent::PhrozenNetworkAgent(std::string log_dir)
    : m_log_dir(log_dir)
    , m_config_dir("")
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
    disconnect_printer();
    cleanup_curl();
}

bool PhrozenNetworkAgent::InitializeConnector( const std::string& strIp  )
{
    // reset all info
    m_spWebServiceInfo = std::make_unique< PhrozenWebServiceInfo >();
    m_spPrinterInfo = std::make_unique< PhrozenPrinterInfo >();
    m_spThreadControl = std::make_unique< PhrozenThreadControl >();
    m_spMonitorWindow = std::make_unique< PhrozenMonitorWindow >();

    if ( !m_spAmsPatterns ) m_spAmsPatterns = std::make_unique< PhrozenAMSPatterns >();
    


    //TODO check m_spPrinterInfo: pre_printerIP, isSameIP does it has affect?

    //trigger ams update query command after connect to speicified IP address (Printer)
    SetFirstTimeToSendQuery( true );
    m_spWebServiceInfo->ip = strIp;

    bool bSuccess = InitializeConnectorImp( strIp );
    if ( !bSuccess )
    {
        m_spWebServiceInfo->ip = "";
        m_spMonitorWindow->connectedMachineName = "";
        m_spMonitorWindow->isShownIPConnectNotification = true;
        SetStartReceiving(false);
        SetStartSending(false);
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

    CleanupWebSocketConnection();
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
        TriggerArpResolution(m_spWebServiceInfo->ip);
        
        // Waiting for the ARP table to be updated and for confirmation that ARP resolution is complete (maximum wait 1000ms).
        if (WaitForArpResolution(m_spWebServiceInfo->ip, 1000)) {
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
int PhrozenNetworkAgent::connect_printer( std::string dev_ip)
{
    std::lock_guard<std::mutex> lock(m_connection_mutex);

    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Connecting to printer - IP: " << dev_ip;

    if (m_is_connected) {
        BOOST_LOG_TRIVIAL(warning) << "PhrozenNetworkAgent: Already connected to a printer, disconnecting first";
        disconnect_printer();
    }

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

    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Disconnecting from printer: " << m_connected_dev_ip;

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
int PhrozenNetworkAgent::send_message(std::string dev_ip, std::string message)
{
    std::lock_guard<std::mutex> lock(m_message_mutex);

    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Sending message to " << dev_ip << ": " << message;

    if (!m_is_connected || m_connected_dev_ip != dev_ip) {
        BOOST_LOG_TRIVIAL(error) << "PhrozenNetworkAgent: Not connected to device " << dev_ip;
        return -1;
    }

    // Implementation needed: Send message to printer
    // This would typically use HTTP or WebSocket communication

    return 0;
}

// Send GCode command
int PhrozenNetworkAgent::send_gcode_command(std::string dev_ip, std::string gcode)
{
    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Sending GCode to " << dev_ip << ": " << gcode;

    if (!m_is_connected || m_connected_dev_ip != dev_ip) {
        BOOST_LOG_TRIVIAL(error) << "PhrozenNetworkAgent: Not connected to device " << dev_ip;
        return -1;
    }

    // Implementation needed: Send GCode command

    return 0;
}

// Send file
int PhrozenNetworkAgent::send_file(std::string dev_ip, std::string file_path, OnProgressCallback progress_fn)
{
    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Sending file to " << dev_ip << ": " << file_path;

    if (!m_is_connected || m_connected_dev_ip != dev_ip) {
        BOOST_LOG_TRIVIAL(error) << "PhrozenNetworkAgent: Not connected to device " << dev_ip;
        return -1;
    }

    // Implementation needed: File upload logic

    return 0;
}

// Download file
int PhrozenNetworkAgent::download_file(std::string dev_ip, std::string remote_path, std::string local_path)
{
    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Downloading file from " << dev_ip
                            << " - Remote: " << remote_path << ", Local: " << local_path;

    if (!m_is_connected || m_connected_dev_ip != dev_ip) {
        BOOST_LOG_TRIVIAL(error) << "PhrozenNetworkAgent: Not connected to device " << dev_ip;
        return -1;
    }

    // Implementation needed: File download logic

    return 0;
}

// Get printer info
int PhrozenNetworkAgent::get_printer_info(std::string dev_ip, std::string* info_json)
{
    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Getting printer info for " << dev_ip;

    if (!m_is_connected || m_connected_dev_ip != dev_ip) {
        BOOST_LOG_TRIVIAL(error) << "PhrozenNetworkAgent: Not connected to device " << dev_ip;
        return -1;
    }

    // Implementation needed: Query printer information

    return 0;
}

// Get printer status
int PhrozenNetworkAgent::get_printer_status(std::string dev_ip, std::string* status_json)
{
    BOOST_LOG_TRIVIAL(info) << "PhrozenNetworkAgent: Getting printer status for " << dev_ip;

    if (!m_is_connected || m_connected_dev_ip != dev_ip) {
        BOOST_LOG_TRIVIAL(error) << "PhrozenNetworkAgent: Not connected to device " << dev_ip;
        return -1;
    }

    // Implementation needed: Query printer status

    return 0;
}


void PhrozenNetworkAgent::RunSendMessage( const std::vector< json >& kMessageList,
                                          const std::vector< bool >& kSendingList )
{

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

#if 0
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
    payload["id"] = PhrozenPrinterID::printer_gcode_script;

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
    payload_AMS["id"] = PhrozenPrinterID::printer_gcode_script;
    
    //Nozzle
    //to check the filament is existing in the nozzle or not
    json payload_Nozzle;
    payload_Nozzle["jsonrpc"] = "2.0";
    payload_Nozzle["method"] = "printer.gcode.script";
    payload_Nozzle["params"]["script"] = "PRZ_ADC";
    payload_Nozzle["id"] = PhrozenPrinterID::printer_gcode_script;

    //LED
    json payload_LED;
    payload_LED["jsonrpc"] = "2.0";
    payload_LED["method"] = "printer.gcode.script";
    payload_LED["params"]["script"] = "P0 LED_GetState";
    payload_LED["id"] = PhrozenPrinterID::printer_gcode_script;

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
                // CRITICAL: libcurl easy handle is NOT thread-safe
                // Cannot call curl_ws_send()/curl_ws_recv() from multiple threads simultaneously
                // Operating the same curl handle from 2 threads may cause crash risk
                std::lock_guard<std::mutex> lock(m_kCurlMutex);
                BOOST_LOG_TRIVIAL(debug) << "RunSendMessage: Lock acquired, Thread ID: " << thread_id;
                
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
                if ((timeDiff > 5 && m_spPrinterInfo->state != "printing") || m_spThreadControl->first_time_to_send_query)
                {
                    result = send_action_Command(payload_AMS.dump());
                    result = send_action_Command(payload_history.dump());
                    result = send_action_Command(payload_Nozzle.dump());
                    result = send_action_Command(payload_LED.dump());
                    m_spThreadControl->first_time_to_send_query = false;
                    previousTime = std::chrono::steady_clock::now();
                }
                BOOST_LOG_TRIVIAL(debug) << "GetAllInfo_websocket: Lock released, Thread ID: " << thread_id;
            }// Lock released from here before sleep to avoid blocking ReceiveResponse() thread
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

    } catch (const std::invalid_argument& e) {
        DebugOutput( "Caught std::invalid_argument: " , e.what());
    } catch (const std::exception& e) {
        DebugOutput( "Caught std::exception: " , e.what() );
    }

#endif
 
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
    
    // Frame accumulation buffers for handling fragmented messages
    std::string ams_message_buffer;      // Buffer for AMS-related messages
    std::string historyInfo;              // Buffer for history info (existing)
    bool historyStart = false;
    int again = 0;
    
    // Sliding window buffer for cross-frame pattern matching
    // This helps catch patterns that span across frame boundaries
    std::string sliding_window_buffer;
    const size_t MAX_SLIDING_WINDOW_SIZE = 10000;  // Maximum size to prevent memory issues
    

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
                    again = 0;
                    
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
                        ams_message_buffer += frame_data;
                        BOOST_LOG_TRIVIAL(debug) << "Accumulating continuation frame. "
                        << "Buffer size: " << ams_message_buffer.size();
                        
                        // Prevent buffer from growing too large
                        if (ams_message_buffer.size() > MAX_SLIDING_WINDOW_SIZE) {
                            BOOST_LOG_TRIVIAL(warning) << "AMS message buffer exceeded max size, truncating";
                            ams_message_buffer = ams_message_buffer.substr(ams_message_buffer.size() - MAX_SLIDING_WINDOW_SIZE / 2);
                        }
                        return;  // To next loop for wait for more frames
                    }
                    
                    // ============================================
                    // Complete Message Combination
                    // ============================================
                    std::string complete_message = PhrozenFrameProcessor::CombineFrames(frame_data, ams_message_buffer);
                    PhrozenFrameProcessor::UpdateSlidingWindow(sliding_window_buffer, complete_message, MAX_SLIDING_WINDOW_SIZE);
                    
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
                        std::string prev_state = m_kPrev_state.get();
                        bool bThumbnailChecking = IsThumbnailChecking();
                        PhrozenPrinterStatusExtractor::ProcessPrinterStatus( ws,
                                                                             m_spWebServiceInfo.get(),
                                                                             m_spPrinterInfo.get(), 
                                                                             prev_state,
                                                                             bThumbnailChecking );
                        m_kPrev_state.set( prev_state );
                        SetThumbnailChecking( bThumbnailChecking );
                    }
                    
                    // ============================================
                    // History Info Processing (independent of skip_proc_stat)
                    // ============================================
                    PhrozenMessageProcessor::ProcessHistoryInfo(ws, historyInfo, &m_kHistoryList, historyStart);
                    
                    // ============================================
                    // AMS Processing (independent of skip_proc_stat)
                    // ============================================
                    // Search in both the current complete message and sliding window buffer
                    // This ensures we catch patterns even if they span frame boundaries
                    std::string search_text = ws;
                    if (sliding_window_buffer.size() > ws.size()) {
                        search_text = sliding_window_buffer;
                    }
                    
                    bool bIsConnetedToAMS = IsConnetedToAMS();
                    PhrozenAMSProcessor::ProcessAMSConnectionStatus(search_text, sliding_window_buffer, m_spAmsPatterns.get(), bIsConnetedToAMS );
                    PhrozenAMSProcessor::ProcessAMSCommandStates(search_text, sliding_window_buffer,  m_spAmsPatterns.get(), &m_kAMSList );
                    PhrozenAMSProcessor::ProcessAMSEntryParkState(ws, bIsConnetedToAMS, &m_kAMSList );
                    SetConnectedToAms( bIsConnetedToAMS );
                    
                    // ============================================
                    // Pause Message Processing
                    // ============================================
                    PhrozenMessageProcessor::ProcessPauseMessage(ws, m_spMonitorWindow.get() );
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
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    m_spPrinterInfo->state = "offline";
    //return res;


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


// ========== in below, just for reference ============== //

// Get connected printer ID
std::string PhrozenNetworkAgent::get_connected_printer_id()
{
    std::lock_guard<std::mutex> lock(m_connection_mutex);
    return m_connected_dev_ip;
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


#pragma endregion
