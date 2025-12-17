#include "PhrozenDeviceManager.hpp"
//#include "libslic3r/libslic3r.h"
//#include "DeviceManager.hpp"
//#include "libslic3r/Time.hpp"
//#include "libslic3r/Thread.hpp"
//#include "slic3r/Utils/ColorSpaceConvert.hpp"
#include "PhrozenMonitorController.hpp"
#include <algorithm>
#include <cmath>
#include <wx/image.h>
#include <wx/mstream.h>
#include <boost/log/trivial.hpp>
#include "../GUI_App.hpp"
#include "../../Utils/Phrozen/PhrozenNetworkAgent.hpp"
//#include "MsgDialog.hpp"
//#include "Plater.hpp"
//#include "ReleaseNote.hpp"
//#include <thread>
//#include <mutex>
//#include <codecvt>
//#include <boost/foreach.hpp>
//#include <boost/typeof/typeof.hpp>
//#include <boost/uuid/uuid.hpp>
//#include <boost/uuid/uuid_generators.hpp>
//#include <boost/uuid/uuid_io.hpp>
//#include "fast_float/fast_float.h"
//#include <wx/dir.h>

namespace Slic3r {

#pragma region PhrozenMachineObject
PhrozenMachineObject::PhrozenMachineObject( std::string name, std::string id, std::string ip )
    : MachineObject( nullptr, name, id, ip )
{
}

PhrozenMachineObject::PhrozenMachineObject( std::string ip )
    : MachineObject( nullptr, "", "", ip )
{

}


PhrozenMachineObject::~PhrozenMachineObject(){}

float PhrozenMachineObject::GetPhrozenBedTemperature()
{
    //std::lock_guard<std::mutex> lock( MonitorControl::m_kCurlMutex);
    return MonitorControl::m_pPrinterInfo->bed_temperature;
}

float PhrozenMachineObject::GetPhrozenNozzleTemperature() 
{
    return MonitorControl::m_pPrinterInfo->extruder_temperature;
}

float PhrozenMachineObject::GetPhrozenPrintSpeed()
{
    return MonitorControl::m_pPrinterInfo->print_speed;
}

float PhrozenMachineObject::GetPhrozenAuxiliaryCoolingSpeed()
{
    return MonitorControl::m_pPrinterInfo->auxiliary_fan_speed;
}

float PhrozenMachineObject::GetPhrozenPartCoolingSpeed()
{
    return MonitorControl::m_pPrinterInfo->fan_speed;
}

float PhrozenMachineObject::GetPhrozenShieldCoolingSpeed()
{
    return MonitorControl::m_pPrinterInfo->shield_fan_speed;
}

float PhrozenMachineObject::GetPhrozenBedTargetTemperature() 
{
    return MonitorControl::m_pPrinterInfo->bed_temperature_target;
}

float PhrozenMachineObject::GetPhrozenNozzleTargetTemperature() 
{
    return MonitorControl::m_pPrinterInfo->extruder_temperature_target;
}

int PhrozenMachineObject::GetPhrozenBedTemperature_limit()
{
    //todo get from machine?
    return 300;
}

int PhrozenMachineObject::GetPhrozenNozzleTemperature_limit() 
{
    //todo get from machine?
    return 300;
}

std::string PhrozenMachineObject::GetPhrozenPrintStatus()
{
    return MonitorControl::m_pPrinterInfo->state;
}

std::string PhrozenMachineObject::GetPhrozenPrintFile()
{
    return MonitorControl::m_pPrinterInfo->print_file;
}

std::string PhrozenMachineObject::GetPhrozenThumbnailPath()
{
    return MonitorControl::m_pPrinterInfo->thumbnail_path;
}

void PhrozenMachineObject::GetPhrozenThumbnailInfo(std::string gcodeName)
{
    //get gcode path of printer storage by gcode name
    MonitorControl::GetThumbnailInfo(gcodeName);
}

void PhrozenMachineObject::GetPhrozenThumbnailImage(std::string thumbnailPath)
{
    //generate thumbnail image of local folder by gcode path
    MonitorControl::GetThumbnailImage(thumbnailPath);
}

bool PhrozenMachineObject::GetPhrozenThumbnailAsBitmap(const std::string& gcodeName, wxBitmap& thumbnailBitmap)
{
    // ============================================
    // Step 1: Try to get thumbnail from GCode file first
    // ============================================
    std::vector<unsigned char> thumbnail_data;
    bool download_success = false;
    {
        std::lock_guard<std::mutex> lock(MonitorControl::m_kCommandMutex);
        download_success = MonitorControl::GetThumbnailFromGCodeFile(gcodeName, thumbnail_data);
    }
    
    // ============================================
    // Step 2: If failed, try to get thumbnail image via HTTP API (fallback)
    // ============================================
    // 因為先採用直接解析Gcode內容的縮圖資料來生成UI想要顯示的縮圖圖片檔案，所以暫時先不多執行這一個動作，但先保留，之後如果有需要可以
    //if (!download_success || thumbnail_data.empty()) {
    //    std::cout << "[GetPhrozenThumbnailAsBitmap] Failed to get thumbnail from GCode, trying HTTP API fallback..." << std::endl;
        //download_success = MonitorControl::GetThumbnailImageInMemory(gcodeName, thumbnail_data);
    //}
    
    if (!download_success || thumbnail_data.empty()) {
        std::cout << "[GetPhrozenThumbnailAsBitmap] ERROR - Failed to get thumbnail for gcode: " << gcodeName << std::endl;
        BOOST_LOG_TRIVIAL(error) << "GetPhrozenThumbnailAsBitmap: "
                                 << "Failed to download thumbnail image (both GCode extraction and HTTP API failed) for gcode: " << gcodeName;
        return false;
    }
    
    // ============================================
    // Step 2: Convert memory data to wxBitmap with highest quality
    // ============================================
    try {
        // Create memory input stream from vector data
        wxMemoryInputStream mem_stream(thumbnail_data.data(), thumbnail_data.size());
        
        std::cout << "[GetPhrozenThumbnailAsBitmap] Loading image from memory, data size: " 
                  << thumbnail_data.size() << " bytes" << std::endl;
        
        // Load image from memory stream (try PNG first as it's usually highest quality)
        wxImage thumbnail_image;
        bool load_success = false;
        
        // Try PNG first (usually highest quality, lossless)
        mem_stream.SeekI(0);
        load_success = thumbnail_image.LoadFile(mem_stream, wxBITMAP_TYPE_PNG);
        
        if (!load_success) {
            // Try JPEG
            mem_stream.SeekI(0);
            load_success = thumbnail_image.LoadFile(mem_stream, wxBITMAP_TYPE_JPEG);
        }
        
        if (!load_success) {
            // Try auto-detect
            mem_stream.SeekI(0);
            load_success = thumbnail_image.LoadFile(mem_stream);
        }
        
        if (!load_success) {
            // Try BMP as last resort
            mem_stream.SeekI(0);
            load_success = thumbnail_image.LoadFile(mem_stream, wxBITMAP_TYPE_BMP);
        }
        
        if (load_success && thumbnail_image.IsOk()) {
            // Ensure image has alpha channel if original had it (preserve transparency)
            if (!thumbnail_image.HasAlpha() && thumbnail_image.HasMask()) {
                thumbnail_image.InitAlpha();
            }
            
            // Log image properties for debugging
            std::cout << "[GetPhrozenThumbnailAsBitmap] Image loaded successfully: " 
                      << thumbnail_image.GetWidth() << "x" << thumbnail_image.GetHeight()
                      << ", HasAlpha: " << (thumbnail_image.HasAlpha() ? "yes" : "no")
                      << ", HasMask: " << (thumbnail_image.HasMask() ? "yes" : "no") << std::endl;
            
            // Convert wxImage to wxBitmap with highest quality
            // On macOS, use scale factor to preserve quality for Retina displays
            #ifdef __APPLE__
            thumbnailBitmap = wxBitmap(thumbnail_image, -1, 1.0);  // Use scale factor 1.0 to preserve original quality
            #else
            thumbnailBitmap = wxBitmap(thumbnail_image);
            #endif
            
            std::cout << "[GetPhrozenThumbnailAsBitmap] Successfully converted to wxBitmap: " 
                      << thumbnail_image.GetWidth() << "x" << thumbnail_image.GetHeight() 
                      << " (data size: " << thumbnail_data.size() << " bytes)" << std::endl;
            BOOST_LOG_TRIVIAL(info) << "GetPhrozenThumbnailAsBitmap: "
                                    << "Successfully converted to wxBitmap: " 
                                    << thumbnail_image.GetWidth() << "x" 
                                    << thumbnail_image.GetHeight();
            return true;
        } else {
            BOOST_LOG_TRIVIAL(error) << "GetPhrozenThumbnailAsBitmap: "
                                     << "Failed to load image from memory data";
            return false;
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "GetPhrozenThumbnailAsBitmap: "
                                 << "Exception during image conversion: " << e.what();
        return false;
    }
}

float PhrozenMachineObject::GetPhrozenPrintProgress()
{
    return MonitorControl::m_pPrinterInfo->print_progress;
}

float PhrozenMachineObject::GetPhrozenPrintTime()
{
    return MonitorControl::m_pPrinterInfo->print_time;
}

float PhrozenMachineObject::GetPhrozenTotalTime()
{
    return MonitorControl::m_pPrinterInfo->total_time;
}

float PhrozenMachineObject::GetPhrozenPrintFilamentAmount()
{
    return MonitorControl::m_pPrinterInfo->print_filament;
}

bool PhrozenMachineObject::IsPrintPaused()
{
    return MonitorControl::m_pPrinterInfo->is_paused;
}

bool PhrozenMachineObject::GetPhrozenCommand_lighting_enabled() 
{
    return MonitorControl::m_bIsLEDOn;
}

double PhrozenMachineObject::GetPhrozenSendFileProgress()
{
    // Return value range: 0.0 ~ 100.0
    return MonitorControl::m_fProgressValue;
}

void PhrozenMachineObject::SetPhrozenCommand_bed_temp( int nTemp ) 
{
    //完整的程式碼註解，請參閱void PhrozenMachineObject::SetPhrozenCommand_nozzle_temp( int nTemp )
    if (!IsPhrozenConnected() || !IsPhrozenStartReceiving()) {
        BOOST_LOG_TRIVIAL(warning) << "SetPhrozenCommand_bed_temp: connection or receiver not ready, command ignored - !IsPhrozenConnected()=" << IsPhrozenConnected()
        << ", !IsPhrozenStartReceiving()=" << IsPhrozenStartReceiving();
        return;
    }

    int tempLimit = GetPhrozenBedTemperature_limit();

    const int originalTemp = nTemp;
    nTemp = std::clamp(nTemp, 0, tempLimit);
    BOOST_LOG_TRIVIAL(info) << "SetPhrozenCommand_bed_temp: Temperature (" << originalTemp
    << ") exceeds limit (" << tempLimit << "), clamped to " << tempLimit << ", nTemp=" << nTemp;

    try {
        std::thread threadForSetBedTemp([nTemp](){
            
            std::lock_guard<std::mutex> lock(MonitorControl::m_kCommandMutex);
            
            MonitorControl::SetBedTemperature(nTemp);
        });
        
        threadForSetBedTemp.detach();
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "SetPhrozenCommand_bed_temp: Failed to create thread: " << e.what();
    }
}

void PhrozenMachineObject::SetPhrozenCommand_nozzle_temp( int nTemp ) 
{
    // ============================================================================
    // PHASE 1: Connection Status Validation
    // ============================================================================
    // Check if WebSocket connection is established and receiver is ready
    if (!IsPhrozenConnected() || !IsPhrozenStartReceiving()) {
        BOOST_LOG_TRIVIAL(warning) << "SetPhrozenCommand_nozzle_temp: connection or receiver not ready, command ignored - !IsPhrozenConnected()=" << IsPhrozenConnected() 
        << ", !IsPhrozenStartReceiving()=" << IsPhrozenStartReceiving();
        return;
    }

    // ============================================================================
    // PHASE 2: Parameter Validation and Clamping
    // ============================================================================
    // Get maximum allowed temperature limit (typically 300°C)
    int tempLimit = GetPhrozenNozzleTemperature_limit();

    // std::clamp (C++17)
    // 限制溫度值在 [0, tempLimit] 範圍內
    const int originalTemp = nTemp;
    nTemp = std::clamp(nTemp, 0, tempLimit);
    BOOST_LOG_TRIVIAL(info) << "SetPhrozenCommand_nozzle_temp: Temperature (" << originalTemp
    << ") exceeds limit (" << tempLimit << "), clamped to " << tempLimit << ", nTemp=" << nTemp;

    // ============================================================================
    // PHASE 3: Asynchronous Command Execution
    // ============================================================================
    // 異常處理 (try-catch)
    try {
        // Lambda 表達式 (C++11)
        std::thread threadForSetNozzleTemp([nTemp](){
            
            // RAII(Resource acquisition is initialization) - std::lock_guard<std::mutex> (C++11)
            // 自動管理互斥鎖生命週期，確保執行緒安全
            std::lock_guard<std::mutex> lock(MonitorControl::m_kCommandMutex);
            
            // Send command to printer firmware (firmware queue handles execution order)
            MonitorControl::SetExtruderTemperature(nTemp);
        });
        
        // std::thread::detach() - Fire-and-forget 異步執行模式
        threadForSetNozzleTemp.detach();
    }
    // 異常處理 - catch (const std::exception& e) 按常量引用捕獲
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "SetPhrozenCommand_nozzle_temp: Failed to create thread: " << e.what();
    }
}

void PhrozenMachineObject::SetPhrozenCommand_cooling_auxiliary( int nPower ) 
{
    //完整的程式碼註解，請參閱void PhrozenMachineObject::SetPhrozenCommand_nozzle_temp( int nTemp )
    if (!IsPhrozenConnected() || !IsPhrozenStartReceiving()) {
        BOOST_LOG_TRIVIAL(warning) << "SetPhrozenCommand_cooling_auxiliary: connection or receiver not ready, command ignored - !IsPhrozenConnected()=" << IsPhrozenConnected()
        << ", !IsPhrozenStartReceiving()=" << IsPhrozenStartReceiving();
        return;
    }

    int powerLimit = GetPhrozenCoolingPower_limit();

    const int originalPower = nPower;
    nPower = std::clamp(nPower, 0, powerLimit);
    // 將 UI 百分比值轉換為機器命令所需的數值範圍
    // 原因：
    //   - UI 輸入範圍：0-100（百分比值，更容易用戶理解）
    //   - 機器命令範圍：0-255（M106 標準風扇速度範圍，符合 Klipper 規範）
    //   - 必須進行數值轉換才能正確發送命令給機器
    // 方式：
    //   - 使用轉換係數 2.55 (255/100 = 2.55)
    //   - 將百分比值乘以 2.55 得到對應的機器值
    //   - 使用 std::ceil 進行無條件進位（向上取整），確保用戶輸入的數值與實際調整的數值一致
    //   - 這樣可以讓用戶在 UI 輸入的值對應到機器實際設定的值，避免用戶輸入 50% 但機器只設定到 49.x% 的情況
    // 效果：
    //   - UI 輸入 0   → 機器收到 0   (ceil(0 * 2.55) = ceil(0) = 0)
    //   - UI 輸入 50  → 機器收到 128 (ceil(50 * 2.55) = ceil(127.5) = 128)
    //   - UI 輸入 100 → 機器收到 255 (ceil(100 * 2.55) = ceil(255) = 255)
    // 範例：
    //   用戶在 UI 輸入 50%（表示 50% 風扇速度）
    //   → nPower = 50 (clamp 後)
    //   → machinePower = ceil(50 * 2.55) = ceil(127.5) = 128
    //   → 發送給機器：M106 P2 S128
    int machinePower = std::ceil(nPower * 2.55);
    
    BOOST_LOG_TRIVIAL(info) << "SetPhrozenCommand_cooling_auxiliary: Power (" << originalPower
    << ") exceeds limit (" << powerLimit << "), clamped to " << nPower << ", converted to machine value: " << machinePower;

    try {
        std::thread threadForSetCoolingAuxiliary([machinePower](){
            
            std::lock_guard<std::mutex> lock(MonitorControl::m_kCommandMutex);
            
            MonitorControl::SetAuxiliaryFanSpeed(machinePower);
        });
        
        threadForSetCoolingAuxiliary.detach();
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "SetPhrozenCommand_cooling_auxiliary: Failed to create thread: " << e.what();
    }
}

void PhrozenMachineObject::SetPhrozenCommand_cooling_part( int nPower ) 
{
    //完整的程式碼註解，請參閱void PhrozenMachineObject::SetPhrozenCommand_nozzle_temp( int nTemp )
    if (!IsPhrozenConnected() || !IsPhrozenStartReceiving()) {
        BOOST_LOG_TRIVIAL(warning) << "SetPhrozenCommand_cooling_part: connection or receiver not ready, command ignored - !IsPhrozenConnected()=" << IsPhrozenConnected()
        << ", !IsPhrozenStartReceiving()=" << IsPhrozenStartReceiving();
        return;
    }

    int powerLimit = GetPhrozenCoolingPower_limit();

    const int originalPower = nPower;
    nPower = std::clamp(nPower, 0, powerLimit);
    //需要執行換算的原因，細節請參閱void PhrozenMachineObject::SetPhrozenCommand_cooling_auxiliary( int nPower )
    int machinePower = std::ceil(nPower * 2.55);
    
    BOOST_LOG_TRIVIAL(info) << "SetPhrozenCommand_cooling_part: Power (" << originalPower
    << ") exceeds limit (" << powerLimit << "), clamped to " << nPower << ", converted to machine value: " << machinePower;

    try {
        std::thread threadForSetCoolingPart([machinePower](){
            
            std::lock_guard<std::mutex> lock(MonitorControl::m_kCommandMutex);
            
            MonitorControl::SetPartFanSpeed(machinePower);
        });
        
        threadForSetCoolingPart.detach();
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "SetPhrozenCommand_cooling_part: Failed to create thread: " << e.what();
    }
}

void PhrozenMachineObject::SetPhrozenCommand_cooling_shield( int nPower ) 
{
    //完整的程式碼註解，請參閱void PhrozenMachineObject::SetPhrozenCommand_nozzle_temp( int nTemp )
    if (!IsPhrozenConnected() || !IsPhrozenStartReceiving()) {
        BOOST_LOG_TRIVIAL(warning) << "SetPhrozenCommand_cooling_shield: connection or receiver not ready, command ignored - !IsPhrozenConnected()=" << IsPhrozenConnected()
        << ", !IsPhrozenStartReceiving()=" << IsPhrozenStartReceiving();
        return;
    }

    int powerLimit = GetPhrozenCoolingPower_limit();

    const int originalPower = nPower;
    nPower = std::clamp(nPower, 0, powerLimit);
    //需要執行換算的原因，細節請參閱void PhrozenMachineObject::SetPhrozenCommand_cooling_auxiliary( int nPower )
    int machinePower = std::ceil(nPower * 2.55);
    
    BOOST_LOG_TRIVIAL(info) << "SetPhrozenCommand_cooling_shield: Power (" << originalPower
    << ") exceeds limit (" << powerLimit << "), clamped to " << nPower << ", converted to machine value: " << machinePower;

    try {
        std::thread threadForSetCoolingShiled([machinePower](){
            
            std::lock_guard<std::mutex> lock(MonitorControl::m_kCommandMutex);
            
            MonitorControl::SetShieldFanSpeed(machinePower);
        });
        
        threadForSetCoolingShiled.detach();
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "SetPhrozenCommand_cooling_shield: Failed to create thread: " << e.what();
    }
}

void PhrozenMachineObject::SetPhrozenCommand_print_speed( float fValue )
{
    //完整的程式碼註解，請參閱void PhrozenMachineObject::SetPhrozenCommand_nozzle_temp( int nTemp )
    if (!IsPhrozenConnected() || !IsPhrozenStartReceiving()) {
        BOOST_LOG_TRIVIAL(warning) << "SetPhrozenCommand_print_speed: connection or receiver not ready, command ignored - !IsPhrozenConnected()=" << IsPhrozenConnected()
        << ", !IsPhrozenStartReceiving()=" << IsPhrozenStartReceiving();
        return;
    }

    // 將 float 速度倍數值轉換為 int 百分比值
    // 原因：
    //   - SetPrintSpeed() 函數接收 int 類型參數
    //   - M220 S 命令需要整數百分比值（例如 S80 表示 80%）
    //   - print_speed_enum_to_percent() 將 UI 的五種速度模式轉換為 float 倍數值：
    //     * Silent: 0.5 (50%)
    //     * Quite: 0.8 (80%)
    //     * Standard: 1.0 (100%)
    //     * Fast: 1.2 (120%)
    //     * Turbo: 1.5 (150%)
    // 方式：
    //   - 直接將 float 倍數值乘以 100 後使用 std::round 進行四捨五入，轉換為整數百分比值
    //   - 使用標準的四捨五入轉換方法，簡單且高效
    // 效果：
    //   - 0.5 → 50 (Silent 模式 → 50%)
    //   - 0.8 → 80 (Quite 模式 → 80%)
    //   - 1.0 → 100 (Standard 模式 → 100%)
    //   - 1.2 → 120 (Fast 模式 → 120%)
    //   - 1.5 → 150 (Turbo 模式 → 150%)
    // 將速度倍數值轉換為百分比整數值
    int speedPercent = std::round(fValue * 100.0f);
    BOOST_LOG_TRIVIAL(info) << "SetPhrozenCommand_print_speed: Speed (" << fValue << ")" 
                            << ", converted to percent: " << speedPercent;

    try {
        std::thread threadForSetPrintSpeed([speedPercent](){
            
            std::lock_guard<std::mutex> lock(MonitorControl::m_kCommandMutex);
            
            MonitorControl::SetPrintSpeed(speedPercent);
        });
        
        threadForSetPrintSpeed.detach();
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "SetPhrozenCommand_print_speed: Failed to create thread: " << e.what();
    }
}

void PhrozenMachineObject::SetPhrozenCommand_nozzle_movement( std::string direction, float fValue )
{
    //完整的程式碼註解，請參閱void PhrozenMachineObject::SetPhrozenCommand_nozzle_temp( int nTemp )
    if (!IsPhrozenConnected() || !IsPhrozenStartReceiving()) {
        BOOST_LOG_TRIVIAL(warning) << "SetPhrozenCommand_nozzle_movement: connection or receiver not ready, command ignored - !IsPhrozenConnected()=" << IsPhrozenConnected()
        << ", !IsPhrozenStartReceiving()=" << IsPhrozenStartReceiving();
        return;
    }
    
    BOOST_LOG_TRIVIAL(info) << "SetPhrozenCommand_nozzle_movement: direction (" << direction << "), movement (" << fValue << ")";

    try {
        std::thread threadForSetNozzleMovement([direction, fValue](){
            
            std::lock_guard<std::mutex> lock(MonitorControl::m_kCommandMutex);
            
            if(direction == "home_xy"){
                MonitorControl::homeXY();
            }
            else{
                MonitorControl::MoveHead(direction, fValue);
            }
        });
        
        threadForSetNozzleMovement.detach();
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "SetPhrozenCommand_nozzle_movement: Failed to create thread: " << e.what();
    }
}

void PhrozenMachineObject::SetPhrozenCommand_nozzle_offset( float fValue )
{
    //完整的程式碼註解，請參閱void PhrozenMachineObject::SetPhrozenCommand_nozzle_temp( int nTemp )
    if (!IsPhrozenConnected() || !IsPhrozenStartReceiving()) {
        BOOST_LOG_TRIVIAL(warning) << "SetPhrozenCommand_nozzle_offset: connection or receiver not ready, command ignored - !IsPhrozenConnected()=" << IsPhrozenConnected()
        << ", !IsPhrozenStartReceiving()=" << IsPhrozenStartReceiving();
        return;
    }
    
    BOOST_LOG_TRIVIAL(info) << "SetPhrozenCommand_nozzle_offset: movement (" << fValue << ")";

    try {
        std::thread threadForSetNozzleMovement([fValue](){
            
            std::lock_guard<std::mutex> lock(MonitorControl::m_kCommandMutex);
            
            MonitorControl::zoffset(fValue);
        });
        
        threadForSetNozzleMovement.detach();
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "SetPhrozenCommand_nozzle_offset: Failed to create thread: " << e.what();
    }
}

void PhrozenMachineObject::SetPhrozenCommand_load(int filament_id)
{
    //完整的程式碼註解，請參閱void PhrozenMachineObject::SetPhrozenCommand_nozzle_temp( int nTemp )
    if (!IsPhrozenConnected() || !IsPhrozenStartReceiving()) {
        BOOST_LOG_TRIVIAL(warning) << "SetPhrozenCommand_load: connection or receiver not ready, command ignored - !IsPhrozenConnected()=" << IsPhrozenConnected()
        << ", !IsPhrozenStartReceiving()=" << IsPhrozenStartReceiving();
        return;
    }
    
    BOOST_LOG_TRIVIAL(info) << "SetPhrozenCommand_load: filament id (" << filament_id << ")";
    
    try {
        std::thread threadForload([filament_id](){
            
            std::lock_guard<std::mutex> lock(MonitorControl::m_kCommandMutex);
            
            MonitorControl::load(filament_id);
        });
        
        threadForload.detach();
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "SetPhrozenCommand_load: Failed to create thread: " << e.what();
    }
}

void PhrozenMachineObject::SetPhrozenCommand_unload(int filament_id)
{
    //完整的程式碼註解，請參閱void PhrozenMachineObject::SetPhrozenCommand_nozzle_temp( int nTemp )
    if (!IsPhrozenConnected() || !IsPhrozenStartReceiving()) {
        BOOST_LOG_TRIVIAL(warning) << "SetPhrozenCommand_unload: connection or receiver not ready, command ignored - !IsPhrozenConnected()=" << IsPhrozenConnected()
        << ", !IsPhrozenStartReceiving()=" << IsPhrozenStartReceiving();
        return;
    }
    
    BOOST_LOG_TRIVIAL(info) << "SetPhrozenCommand_load: filament id (" << filament_id << ")";

    try {
        std::thread threadForUnload([filament_id](){
            
            std::lock_guard<std::mutex> lock(MonitorControl::m_kCommandMutex);
            
            MonitorControl::Unload(filament_id);
        });
        
        threadForUnload.detach();
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "SetPhrozenCommand_unload: Failed to create thread: " << e.what();
    }
}

void PhrozenMachineObject::SetPhrozenCommand_unload_all_slots()
{
    //完整的程式碼註解，請參閱void PhrozenMachineObject::SetPhrozenCommand_nozzle_temp( int nTemp )
    if (!IsPhrozenConnected() || !IsPhrozenStartReceiving()) {
        BOOST_LOG_TRIVIAL(warning) << "SetPhrozenCommand_unload_all_slots: connection or receiver not ready, command ignored - !IsPhrozenConnected()=" << IsPhrozenConnected()
        << ", !IsPhrozenStartReceiving()=" << IsPhrozenStartReceiving();
        return;
    }
    
    try {
        std::thread threadForUnloadAllSlots([](){
            
            std::lock_guard<std::mutex> lock(MonitorControl::m_kCommandMutex);
            
            MonitorControl::Uninstall_filament();
        });
        
        threadForUnloadAllSlots.detach();
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "SetPhrozenCommand_unload_all_slots: Failed to create thread: " << e.what();
    }
}

void PhrozenMachineObject::SetPhrozenCommand_nozzle_filament_check()
{
    //完整的程式碼註解，請參閱void PhrozenMachineObject::SetPhrozenCommand_nozzle_temp( int nTemp )
    if (!IsPhrozenConnected() || !IsPhrozenStartReceiving()) {
        BOOST_LOG_TRIVIAL(warning) << "SetPhrozenCommand_nozzle_filament_check: connection or receiver not ready, command ignored - !IsPhrozenConnected()=" << IsPhrozenConnected()
        << ", !IsPhrozenStartReceiving()=" << IsPhrozenStartReceiving();
        return;
    }
    
    try {
        std::thread threadForNozzleFilamentCheck([](){
            
            std::lock_guard<std::mutex> lock(MonitorControl::m_kCommandMutex);
            
            MonitorControl::NozzleFilamentCheck();
        });
        
        threadForNozzleFilamentCheck.detach();
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "SetPhrozenCommand_nozzle_filament_check: Failed to create thread: " << e.what();
    }
}

bool PhrozenMachineObject::SetPhrozenCommand_pause()
{
    // Check if WebSocket connection is established and receiver is ready
    if (!IsPhrozenConnected() || !IsPhrozenStartReceiving()) {
        BOOST_LOG_TRIVIAL(warning) << "SetPhrozenCommand_pause: connection or receiver not ready, command ignored - !IsPhrozenConnected()=" << IsPhrozenConnected()
        << ", !IsPhrozenStartReceiving()=" << IsPhrozenStartReceiving();
        return false;
    }
    
    BOOST_LOG_TRIVIAL(info) << "SetPhrozenCommand_pause: Pausing print task";
    
    try {
        std::thread threadForPause([](){
            std::lock_guard<std::mutex> lock(MonitorControl::m_kCommandMutex);
            MonitorControl::printPause_http();
        });
        
        threadForPause.detach();
        return true;
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "SetPhrozenCommand_pause: Failed to create thread: " << e.what();
        return false;
    }
}

bool PhrozenMachineObject::SetPhrozenCommand_resume()
{
    // Check if WebSocket connection is established and receiver is ready
    if (!IsPhrozenConnected() || !IsPhrozenStartReceiving()) {
        BOOST_LOG_TRIVIAL(warning) << "SetPhrozenCommand_resume: connection or receiver not ready, command ignored - !IsPhrozenConnected()=" << IsPhrozenConnected()
        << ", !IsPhrozenStartReceiving()=" << IsPhrozenStartReceiving();
        return false;
    }
    
    BOOST_LOG_TRIVIAL(info) << "SetPhrozenCommand_resume: Resuming print task";
    
    try {
        std::thread threadForResume([](){
            std::lock_guard<std::mutex> lock(MonitorControl::m_kCommandMutex);
            MonitorControl::printResume_http();
        });
        
        threadForResume.detach();
        return true;
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "SetPhrozenCommand_resume: Failed to create thread: " << e.what();
        return false;
    }
}

bool PhrozenMachineObject::SetPhrozenCommand_abort()
{
    // Check if WebSocket connection is established and receiver is ready
    if (!IsPhrozenConnected() || !IsPhrozenStartReceiving()) {
        BOOST_LOG_TRIVIAL(warning) << "SetPhrozenCommand_abort: connection or receiver not ready, command ignored - !IsPhrozenConnected()=" << IsPhrozenConnected()
        << ", !IsPhrozenStartReceiving()=" << IsPhrozenStartReceiving();
        return false;
    }
    
    BOOST_LOG_TRIVIAL(info) << "SetPhrozenCommand_abort: Aborting print task";
    
    try {
        std::thread threadForAbort([](){
            std::lock_guard<std::mutex> lock(MonitorControl::m_kCommandMutex);
            MonitorControl::printStop_http();
        });
        
        threadForAbort.detach();
        return true;
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "SetPhrozenCommand_abort: Failed to create thread: " << e.what();
        return false;
    }
}

bool PhrozenMachineObject::SetPhrozenCommand_sendandprint(std::string filePath)
{
    // Example file path: 
    // Absolute path - Windows: "C:/Users/phrozenmac/Desktop/SendFileAndPrint_TEST.gcode"
    // Absolute path - macOS: "/Users/phrozenmac/Desktop/SendFileAndPrint_TEST.gcode"
    // Relative path: "./SendFileAndPrint_TEST.gcode"
    // Check if WebSocket connection is established and receiver is ready
    if (!IsPhrozenConnected() || !IsPhrozenStartReceiving()) {
        BOOST_LOG_TRIVIAL(warning) << "SetPhrozenCommand_sendandprint: connection or receiver not ready, command ignored - !IsPhrozenConnected()=" << IsPhrozenConnected()
        << ", !IsPhrozenStartReceiving()=" << IsPhrozenStartReceiving();
        return false;
    }
    
    BOOST_LOG_TRIVIAL(info) << "SetPhrozenCommand_sendandprint: Sending and printing file: " << filePath;
    
    try {
        std::thread threadForSendAndPrint([filePath](){
            std::lock_guard<std::mutex> lock(MonitorControl::m_kCommandMutex);
            MonitorControl::printfile(filePath);
        });
        
        threadForSendAndPrint.detach();
        return true;
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "SetPhrozenCommand_sendandprint: Failed to create thread: " << e.what();
        return false;
    }
}

void PhrozenMachineObject::SetPhrozenCommand_lighting_enabled( bool bEnabled )
{
    if (!IsPhrozenConnected() || !IsPhrozenStartReceiving()) {
        BOOST_LOG_TRIVIAL(warning) << "SetPhrozenCommand_lighting_enabled: connection or receiver not ready, command ignored - !IsPhrozenConnected()=" << IsPhrozenConnected()
        << ", !IsPhrozenStartReceiving()=" << IsPhrozenStartReceiving();
        return;
    }
    
    BOOST_LOG_TRIVIAL(info) << "SetPhrozenCommand_lighting_enabled: (" << bEnabled << ")";

    try {
        std::thread threadForSetNozzleMovement([bEnabled](){
            
            std::lock_guard<std::mutex> lock(MonitorControl::m_kCommandMutex);
    
            MonitorControl::SetLED( bEnabled );
        });
        
        threadForSetNozzleMovement.detach();
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "SetPhrozenCommand_nozzle_offset: Failed to create thread: " << e.what();
    }

    
}

bool PhrozenMachineObject::IsPhrozenConnected() 
{
    return MonitorControl::m_pCurl_websocket != nullptr;
}

bool PhrozenMachineObject::IsPhrozenStartReceiving() 
{
    return MonitorControl::IsStartReceiving();
}

std::string PhrozenMachineObject::GetPhrozenConnectedMachineIp()
{
    return dev_ip;
}

// Calibration functions implementation
bool PhrozenMachineObject::StartCalibration()
{
    // Check if WebSocket connection is established and receiver is ready
    if (!IsPhrozenConnected() || !IsPhrozenStartReceiving()) {
        BOOST_LOG_TRIVIAL(warning) << "StartCalibration: connection or receiver not ready, command ignored";
        return false;
    }
    
    // Check if any calibration is already running
    if (MonitorControl::IsAnyCalibrationRunning()) {
        BOOST_LOG_TRIVIAL(warning) << "StartCalibration: Another calibration is already running";
        return false;
    }
    
    BOOST_LOG_TRIVIAL(info) << "StartCalibration: Starting calibration (async)";
    
    try {
        // Initialize calibration status
        {
            std::lock_guard<std::mutex> lock(MonitorControl::m_kCalibrationProgressMutex);
            MonitorControl::m_calibrationProgressInfo.calibrationStatus = MonitorControl::CalibrationState::RUNNING;
            MonitorControl::m_calibrationProgressInfo.calibrationProgress = 0.0f;
            MonitorControl::m_calibrationProgressInfo.heatingCompleted = false;
            MonitorControl::m_calibrationProgressInfo.startTime = std::chrono::steady_clock::now();
        }
        
        // Start calibration in background thread
        std::thread threadForCalibration([](){
            std::lock_guard<std::mutex> lock(MonitorControl::m_kCommandMutex);
            MonitorControl::Calibration_http();
        });
        
        threadForCalibration.detach();
        return true;
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "StartCalibration: Failed to create thread: " << e.what();
        {
            std::lock_guard<std::mutex> lock(MonitorControl::m_kCalibrationProgressMutex);
            MonitorControl::m_calibrationProgressInfo.calibrationStatus = MonitorControl::CalibrationState::STOPPED;
        }
        return false;
    }
}

bool PhrozenMachineObject::StartResonanceCompensation()
{
    // Check if WebSocket connection is established and receiver is ready
    if (!IsPhrozenConnected() || !IsPhrozenStartReceiving()) {
        BOOST_LOG_TRIVIAL(warning) << "StartResonanceCompensation: connection or receiver not ready, command ignored";
        return false;
    }
    
    // Check if any calibration is already running
    if (MonitorControl::IsAnyCalibrationRunning()) {
        BOOST_LOG_TRIVIAL(warning) << "StartResonanceCompensation: Another calibration is already running";
        return false;
    }
    
    BOOST_LOG_TRIVIAL(info) << "StartResonanceCompensation: Starting resonance compensation (async)";
    
    try {
        // Initialize resonance compensation status
        {
            std::lock_guard<std::mutex> lock(MonitorControl::m_kCalibrationProgressMutex);
            MonitorControl::m_calibrationProgressInfo.resonanceCompensationStatus = MonitorControl::CalibrationState::RUNNING;
            MonitorControl::m_calibrationProgressInfo.resonanceCompensationProgress = 0.0f;
            MonitorControl::m_calibrationProgressInfo.startResonanceCompensation = false;
            MonitorControl::m_calibrationProgressInfo.startTime = std::chrono::steady_clock::now();
        }
        
        // Start resonance compensation in background thread
        std::thread threadForResonanceCompensation([](){
            std::lock_guard<std::mutex> lock(MonitorControl::m_kCommandMutex);
            MonitorControl::ResonanceCompensation();
        });
        
        threadForResonanceCompensation.detach();
        return true;
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "StartResonanceCompensation: Failed to create thread: " << e.what();
        {
            std::lock_guard<std::mutex> lock(MonitorControl::m_kCalibrationProgressMutex);
            MonitorControl::m_calibrationProgressInfo.resonanceCompensationStatus = MonitorControl::CalibrationState::STOPPED;
        }
        return false;
    }
}

bool PhrozenMachineObject::StartTemperatureCalibration()
{
    // Check if WebSocket connection is established and receiver is ready
    if (!IsPhrozenConnected() || !IsPhrozenStartReceiving()) {
        BOOST_LOG_TRIVIAL(warning) << "StartTemperatureCalibration: connection or receiver not ready, command ignored";
        return false;
    }
    
    // Check if any calibration is already running
    if (MonitorControl::IsAnyCalibrationRunning()) {
        BOOST_LOG_TRIVIAL(warning) << "StartTemperatureCalibration: Another calibration is already running";
        return false;
    }
    
    BOOST_LOG_TRIVIAL(info) << "StartTemperatureCalibration: Starting temperature calibration (async)";
    
    try {
        // Initialize temperature calibration status
        {
            std::lock_guard<std::mutex> lock(MonitorControl::m_kCalibrationProgressMutex);
            MonitorControl::m_calibrationProgressInfo.temperatureCalibrationStatus = MonitorControl::CalibrationState::RUNNING;
            MonitorControl::m_calibrationProgressInfo.temperatureCalibrationProgress = 0.0f;
            MonitorControl::m_calibrationProgressInfo.tempProgress = 0;
            MonitorControl::m_calibrationProgressInfo.startTime = std::chrono::steady_clock::now();
        }
        
        // Start temperature calibration in background thread
        std::thread threadForTemperatureCalibration([](){
            std::lock_guard<std::mutex> lock(MonitorControl::m_kCommandMutex);
            MonitorControl::TemperatureCalibration();
        });
        
        threadForTemperatureCalibration.detach();
        return true;
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "StartTemperatureCalibration: Failed to create thread: " << e.what();
        {
            std::lock_guard<std::mutex> lock(MonitorControl::m_kCalibrationProgressMutex);
            MonitorControl::m_calibrationProgressInfo.temperatureCalibrationStatus = MonitorControl::CalibrationState::STOPPED;
        }
        return false;
    }
}

int PhrozenMachineObject::GetCalibrationStatus()
{
    return static_cast<int>(MonitorControl::GetCalibrationStatus());
}

int PhrozenMachineObject::GetResonanceCompensationStatus()
{
    return static_cast<int>(MonitorControl::GetResonanceCompensationStatus());
}

int PhrozenMachineObject::GetTemperatureCalibrationStatus()
{
    return static_cast<int>(MonitorControl::GetTemperatureCalibrationStatus());
}

float PhrozenMachineObject::GetCalibrationProgress()
{
    return MonitorControl::GetCalibrationProgress();
}

float PhrozenMachineObject::GetResonanceCompensationProgress()
{
    return MonitorControl::GetResonanceCompensationProgress();
}

float PhrozenMachineObject::GetTemperatureCalibrationProgress()
{
    return MonitorControl::GetTemperatureCalibrationProgress();
}

bool PhrozenMachineObject::IsAnyCalibrationRunning()
{
    return MonitorControl::IsAnyCalibrationRunning();
}
#pragma endregion 


#pragma region PhrozenDeviceSearcher
//std::unique_ptr< boost::thread > PhrozenDeviceSearcher::m_spSearchThread{nullptr};
PhrozenDeviceSearchResult PhrozenDeviceSearcher::m_kSearchResult;

std::unique_ptr<boost::thread> PhrozenDeviceSearcher::t_{nullptr};
std::atomic<bool> PhrozenDeviceSearcher::stop_{false};
std::exception_ptr PhrozenDeviceSearcher::eptr_{nullptr};


void PhrozenDeviceSearcher::StartSearch()
{
    if (t_) return;
    stop_.store(false, std::memory_order_relaxed);
    t_ = std::make_unique< boost::thread > ( boost::thread(boost::bind(&PhrozenDeviceSearcher::Run)) );
}

void PhrozenDeviceSearcher::StopSearch()
{
    stop_.store(true, std::memory_order_relaxed);

    if (t_ && t_->joinable()) {
        // before join, need do interrup, makesure wail/sleep can pause
        t_->interrupt();
        t_->join();
    }
    t_.reset();
    t_ = nullptr;

    // if thread in background has exception, throw it because now is main thread
    if (eptr_) {
        auto e = eptr_;
        eptr_ = nullptr;
        std::rethrow_exception(e);
    }

    m_kSearchResult.SetDataReady(false);
    m_kSearchResult.ClearAll();
}

bool PhrozenDeviceSearcher::IsDataReady()
{
    return m_kSearchResult.IsDataReady();
}

std::map< std::string, std::string > PhrozenDeviceSearcher::GetList()
{
    return m_kSearchResult.GetFounded();
}

void PhrozenDeviceSearcher::Run() noexcept
{
    try {
        while (!stop_.load(std::memory_order_relaxed)) {
            // pause befor start loop
            boost::this_thread::interruption_point();
        
            // ---- main work ----
            std::map< std::string, std::string > kResult;
            ProcessSearchMachine( kResult );
            m_kSearchResult.WriteDataAndSwap( kResult );
            
        }
    } catch (const boost::thread_interrupted&) {
        // get interruption, normal return
    } catch (...) {
        // get exception, remain to main thread to handle
        eptr_ = std::current_exception();
    }
}

void PhrozenDeviceSearcher::ProcessSearchMachine( std::map< std::string, std::string >& kResult )
{
    try {

        using namespace boost::asio;
        using namespace boost::asio::ip;
        
        // Create io_context for asio operations
        io_context io_ctx;
        
        // Create UDP socket
        udp::socket socket(io_ctx, udp::endpoint(udp::v4(), 0));
        
        // Enable broadcast option
        socket.set_option(socket_base::broadcast(true));
        
        // Set up broadcast endpoint (address 255.255.255.255; port 8989)
        udp::endpoint broadcast_endpoint(address_v4::broadcast(), 8989);
        
        // Message to broadcast
        std::string broadcast_msg = "mkswifi";
        
        BOOST_LOG_TRIVIAL(info) << "[Phrozen] Sending UDP broadcast: " << broadcast_msg;
        
        boost::this_thread::interruption_point();

        // Send broadcast message
        boost::system::error_code send_ec;
        size_t bytes_sent = socket.send_to(buffer(broadcast_msg), broadcast_endpoint, 0, send_ec);
        
        if (send_ec) {
            BOOST_LOG_TRIVIAL(warning) << "[Phrozen] UDP broadcast send error: " << send_ec.message();
            return;
        }
        
        BOOST_LOG_TRIVIAL(info) << "[Phrozen] UDP broadcast sent: " << bytes_sent << " bytes";
        
        // Set socket to non-blocking mode for receiving responses
        socket.non_blocking(true);
        
        // Receive responses with timeout
        auto start_time = std::chrono::steady_clock::now();
        const auto timeout_duration = std::chrono::seconds(1);
        
        char receive_buffer[1024];
        udp::endpoint sender_endpoint;
        
        while (std::chrono::steady_clock::now() - start_time < timeout_duration) {
            boost::this_thread::interruption_point();

            boost::system::error_code receive_ec;
            size_t bytes_received = socket.receive_from(
                buffer(receive_buffer), sender_endpoint, 0, receive_ec);
            
            boost::this_thread::interruption_point();

            if (!receive_ec && bytes_received > 0) {
                // Successfully received data
                receive_buffer[bytes_received] = '\0';
                std::string received_data(receive_buffer, bytes_received);
                
                BOOST_LOG_TRIVIAL(info) << "[Phrozen] Received " << bytes_received 
                                      << " bytes from " << sender_endpoint.address().to_string() 
                                      << ": " << received_data;
                
                // Parse the response
                std::string strMachineIp;
                std::string strMachineName;
                
                strMachineIp = sender_endpoint.address().to_string();

                // Extract machine name from response (assuming format similar to original)
                if (received_data.length() >= 8) {
                    size_t comma_pos = received_data.find(",");
                    if (comma_pos != std::string::npos && comma_pos > 8) {
                        strMachineName = received_data.substr(8, comma_pos - 8);
                    } else {
                        strMachineName = "Arco";  // Default name
                    }
                } else {
                    strMachineName = "Arco";
                }
                
                boost::this_thread::interruption_point();

                // Add to list if not already exists
                if ( kResult.find( strMachineIp ) == kResult.end() )
                {
                    kResult.insert( { strMachineIp, strMachineName } );
                    BOOST_LOG_TRIVIAL(info) << "[Phrozen] Added machine: " << strMachineName 
                                            << " at " << strMachineIp;
                }

            }
            else if (receive_ec == error::would_block) {
                // No data available yet, sleep briefly and continue
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            else {
                // Other error occurred
                BOOST_LOG_TRIVIAL(warning) << "[Phrozen] UDP receive error: " << receive_ec.message();
                return;
            }
        }

    } catch (...) {
        throw;
    }
}
#pragma endregion

#pragma region PhrozenMachineObject_Dev
PhrozenMachineObject_Dev::PhrozenMachineObject_Dev( std::string ip, PhrozenNetworkAgent* pAgent ) 
    :m_strIp( ip ),
    m_pNetworkAgent( pAgent )
{
    
}

PhrozenMachineObject_Dev::~PhrozenMachineObject_Dev()
{
}


bool PhrozenMachineObject_Dev::MoveDataToWebcamSnapshot( std::vector<unsigned char>& data )
{
    auto pWebcam = GetWebcameSnapshotPtr();
    if ( pWebcam->move_and_write( data ) )
    {
        pWebcam->flip();
    }
    return true;
}

bool PhrozenMachineObject_Dev::ReadDataFromWebcamSnapshot( std::vector<unsigned char>& data )
{
    auto pWebcam = GetWebcameSnapshotPtr();
    return pWebcam->try_read( data );
}

#pragma endregion



// PhrozenDeviceManager implementation
#pragma region PhrozenDeviceManager
PhrozenDeviceManager::PhrozenDeviceManager( PhrozenNetworkAgent* agent)
{
    m_pNetworkAgent = agent;

    if (agent) {
        AppConfig*  config         = GUI::wxGetApp().app_config; // not sure if need it 
    }
}

PhrozenDeviceManager::~PhrozenDeviceManager()
{
    if ( m_spConnectedMachine )
    {   
        DisconnectMachine();
    }
}

void PhrozenDeviceManager::set_agent(PhrozenNetworkAgent* agent)
{
    m_pNetworkAgent = agent;
}

bool PhrozenDeviceManager::CreateAndConnectMachine(std::string dev_id ) 
{
    if ( !m_pNetworkAgent ) return false;
    if ( m_spConnectedMachine )
    {
        DisconnectMachine();
    }

    bool bSucced = m_pNetworkAgent->InitializeConnector( dev_id );
    return bSucced ? CreateMachine( dev_id , m_spConnectedMachine ) : false;

}

PhrozenMachineObject_Dev* PhrozenDeviceManager::GetConnectingMachine()
{
    return m_spConnectedMachine ? m_spConnectedMachine.get() : nullptr;
}

void PhrozenDeviceManager::DisconnectMachine() 
{
    if ( !m_spConnectedMachine ) return;

    StopReceiveWebcam();
    m_spConnectedMachine = nullptr;

}

bool PhrozenDeviceManager::CreateMachine( std::string dev_id , std::shared_ptr< PhrozenMachineObject_Dev >& spObject ) 
{
    spObject = nullptr;
    if ( dev_id.empty() || !m_pNetworkAgent ) { return false; }

    spObject = std::shared_ptr< PhrozenMachineObject_Dev >( new PhrozenMachineObject_Dev( dev_id, m_pNetworkAgent ) );
    return spObject != nullptr;
}

bool PhrozenDeviceManager::StartReceiveWebcam()
{
    if ( !m_spConnectedMachine || !m_pNetworkAgent ) return false;

    auto strIp = m_spConnectedMachine->GetMachineIp();
    if ( strIp.empty() ) return false;
    if ( m_spRecieveWebcam ) return true;

    auto agent = m_pNetworkAgent;
    auto machineObj = m_spConnectedMachine;

    m_spRecieveWebcam = std::make_unique< WorkerFuncSafe >( 
    [ agent, machineObj, strIp] {

        std::vector<unsigned char> image_data;
        auto kResult = agent->get_camera_snapshot(strIp, image_data );
        if ( kResult == CURLcode::CURLE_OK )
        {
            machineObj->MoveDataToWebcamSnapshot( image_data );
        }
    }, 
    std::chrono::milliseconds{10} );

    m_spRecieveWebcam->Process();
    return true;
}

void PhrozenDeviceManager::StopReceiveWebcam()
{
    m_spRecieveWebcam->Stop();
    m_spRecieveWebcam = nullptr;
}

bool PhrozenDeviceManager::StartSendMessage()
{
    return true;
}

void PhrozenDeviceManager::StopSendMessage()
{

}


#pragma endregion

} // namespace Slic3r
