#ifndef slic3r_PhrozenMachineDatas_hpp_
#define slic3r_PhrozenMachineDatas_hpp_

#include <memory>
#include <atomic>
#include <mutex>
#include <type_traits>

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

#pragma region PhrozenWebServiceInfo
class PhrozenWebServiceInfo
{
public:
    std::string ip;
    std::string port = "7125";
    std::string port_device = ":8808";
    std::string payload;
    std::unique_ptr<std::string> responseData = make_unique<std::string>();
    //json jsonPrinterInfoData;
    //json jsonHistoryInfoData;
    //json jsonReturnInfoData;
    //json jsonThumbnailsInfoData;
    
    void reset() {
        ip.clear();
        payload.clear();
        responseData.reset();
    }
};
#pragma endregion

#pragma region PhrozenPrinterInfo
struct PhrozenPrinterInfo
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

    bool isSameIP = "";
    std::string pre_printerIP = "";
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
    bool first_time_to_send_query = true;
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
#pragma endregion


} // namespace Slic3r

#endif //  slic3r_PhrozenMachineDatas_hpp_
