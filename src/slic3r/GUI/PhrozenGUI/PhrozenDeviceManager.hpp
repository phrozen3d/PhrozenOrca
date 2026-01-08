#ifndef slic3r_PhrozenDeviceManager_hpp_
#define slic3r_PhrozenDeviceManager_hpp_

#include "../DeviceManager.hpp"
#include "../../Utils/Phrozen/PhrozenMachineDatas.hpp"
#include <atomic>

namespace Slic3r {

class PhrozenNetworkAgent;
class WorkerFuncSafe;

#pragma region PhrozenMachineObject
class PhrozenMachineObject : public MachineObject
{

public:
    PhrozenMachineObject( std::string name, std::string id, std::string ip );
    PhrozenMachineObject( std::string ip );
    ~PhrozenMachineObject();

    virtual float GetPhrozenBedTemperature() override;
    virtual float GetPhrozenNozzleTemperature() override;
    virtual float GetPhrozenPrintSpeed() override;
    virtual float GetPhrozenAuxiliaryCoolingSpeed() override;
    virtual float GetPhrozenPartCoolingSpeed() override;
    virtual float GetPhrozenShieldCoolingSpeed() override;

    virtual float GetPhrozenBedTargetTemperature() override;
    virtual float GetPhrozenNozzleTargetTemperature() override;
    virtual float GetPhrozenZOffset() override;

    virtual int GetPhrozenBedTemperature_limit() override;
    virtual int GetPhrozenNozzleTemperature_limit() override;
    // print states
    virtual std::string GetPhrozenPrintStatus() override;
    virtual std::string GetPhrozenPrintFile() override;
    virtual std::string GetPhrozenThumbnailPath() override;
    virtual void GetPhrozenThumbnailInfo(std::string) override;
    virtual void GetPhrozenThumbnailImage(std::string) override;
    virtual bool GetPhrozenThumbnailAsBitmap(const std::string& gcodeName, wxBitmap& thumbnailBitmap) override;
    virtual float GetPhrozenPrintProgress() override;
    virtual float GetPhrozenPrintTime() override;
    virtual float GetPhrozenTotalTime() override;
    virtual float GetPhrozenPrintFilamentAmount() override;
    virtual std::string GetPhrozenSendPrintTime() override;
    virtual bool IsPrintPaused() override;

    virtual bool GetPhrozenCommand_lighting_enabled() override;

    virtual double GetPhrozenSendFileProgress() override;

    // set command to machine
    //control
    virtual void SetPhrozenCommand_bed_temp( int nTemp ) override;
    virtual void SetPhrozenCommand_nozzle_temp( int nTemp ) override;
    virtual void SetPhrozenCommand_cooling_auxiliary( int nPower ) override;
    virtual void SetPhrozenCommand_cooling_part( int nPower ) override;
    virtual void SetPhrozenCommand_cooling_shield( int nPower ) override;
    virtual void SetPhrozenCommand_print_speed( float fValue ) override;
    virtual void SetPhrozenCommand_nozzle_movement( std::string ,float fValue ) override;
    virtual void SetPhrozenCommand_nozzle_offset(float fValue ) override;
    //ams
    virtual void SetPhrozenCommand_load(int filament_id) override;
    virtual void SetPhrozenCommand_unload(int filament_id) override;
    virtual void SetPhrozenCommand_unload_all_slots() override;
    virtual void SetPhrozenCommand_nozzle_filament_check() override;
    //print control pause, resume,abort
    virtual bool SetPhrozenCommand_pause() override;
    virtual bool SetPhrozenCommand_resume() override;
    virtual bool SetPhrozenCommand_abort() override;
    virtual bool SetPhrozenCommand_sendandprint(std::string) override;

    virtual void SetPhrozenCommand_lighting_enabled(  bool bEnabled ) override;

    virtual bool IsPhrozenConnected() override;
    virtual bool IsPhrozenStartReceiving() override;

    virtual std::string GetPhrozenConnectedMachineIp() override;
    
    // Calibration functions
    // Start calibration (async)
    virtual bool StartCalibration() override;
    
    // Start resonance compensation (async)
    virtual bool StartResonanceCompensation() override;
    
    // Start temperature calibration (async)
    virtual bool StartTemperatureCalibration() override;
    
    // Get calibration status (returns int: 0=STOPPED, 1=RUNNING, 2=COMPLETED, 3=ERROR)
    virtual int GetCalibrationStatus() override;
    virtual int GetResonanceCompensationStatus() override;
    virtual int GetTemperatureCalibrationStatus() override;
    
    // Get calibration progress (0-100)
    virtual float GetCalibrationProgress() override;
    virtual float GetResonanceCompensationProgress() override;
    virtual float GetTemperatureCalibrationProgress() override;
    
    // Check if any calibration is running
    virtual bool IsAnyCalibrationRunning() override;

    // link to console page by local webside
    virtual std::string GetConsolePageHyperlink() override;
};
#pragma endregion

#pragma region PhrozenMachineObject_Dev
class PhrozenMachineObject_Dev 
{
public:
    PhrozenMachineObject_Dev( std::string ip, PhrozenNetworkAgent* pAgent );
    ~PhrozenMachineObject_Dev();

    std::string GetMachineIp() { return m_strIp; }

    // Read from ui
    bool ReadDataFromWebcamSnapshot( std::vector<unsigned char>& data );
    bool ReadDataFromAMSInfoList( std::vector< PhrozenAMSInfo >& data );
    bool ReadDataFromCalibrationProgressInfo( PhrozenCalibrationProgressInfo& data );
    bool IsConnectedToAMS();
    bool IsMachineLED_On();
    bool IsNozzleDetectFilament();

    // Recieve from machine
    void MoveDataToWebcamSnapshot( std::vector<unsigned char>& data );
    void MoveDataToPrinterInfo( PhrozenPrinterInfo& data );
    void MoveDataToAMSInfoList( std::vector< PhrozenAMSInfo >& data );
    void MoveDataToCalibrationProgressInfo( PhrozenCalibrationProgressInfo& kData );
    void SetIsConnectedToAMS( const bool& bConnected );
    void SetIsMachineLED_On( const bool& bOn );
    void SetIsNozzleDetectFilament( const bool& bDetected );


    //PrinterInfo
    float GetPhrozenBedTemperature();
    float GetPhrozenNozzleTemperature();
    float GetPhrozenPrintSpeed();
    float GetPhrozenAuxiliaryCoolingSpeed();
    float GetPhrozenPartCoolingSpeed();
    float GetPhrozenShieldCoolingSpeed();
    float GetPhrozenBedTargetTemperature();
    float GetPhrozenNozzleTargetTemperature();
    std::string GetPhrozenPrintStatus();
    std::string GetPhrozenPrintFile();
    std::string GetPhrozenThumbnailPath();
    float GetPhrozenPrintProgress();
    float GetPhrozenPrintTime();
    float GetPhrozenTotalTime();
    float GetPhrozenPrintFilamentAmount();
    std::string GetPhrozenSendPrintTime();
    bool IsPrintPaused();

    int GetPhrozenBedTemperature_limit() { return 300; }
    int GetPhrozenNozzleTemperature_limit() { return 300; }




private:
    



private:
    PhrozenNetworkAgent* m_pNetworkAgent { nullptr };
    std::string m_strIp;

    DoubleBufferSP< std::vector<unsigned char> >* GetWebcameSnapshotPtr() { return &m_webcame_snapshot; }
    DoubleBufferSP< std::vector<unsigned char> > m_webcame_snapshot;

    DoubleBufferSP< PhrozenPrinterInfo >* PrinterInfoPtr() { return &m_printerInfo; }
    DoubleBufferSP< PhrozenPrinterInfo > m_printerInfo;

    DoubleBufferSP< PhrozenCalibrationProgressInfo > m_calibrationProgressInfo;

    DoubleBufferSP< bool > m_connectedToAMS;
    DoubleBufferSP< bool > m_machineLED_On;
    DoubleBufferSP< bool > m_nozzleDetectFilament;
    DoubleBufferSP< std::vector< PhrozenAMSInfo > > m_AMSInfoList;
     



};
#pragma endregion

#pragma region PhrozenDeviceSearchResult
class PhrozenDeviceSearchResult
{
public:
    PhrozenDeviceSearchResult(){}

    void ClearAll() { m_kFoundedListA.clear(); m_kFoundedListB.clear(); }
    std::map< std::string, std::string > GetFounded() { return *m_pReadBuffer; }

    void SetDataReady( bool bReady )
    {
        if ( bReady ) { m_bDataReady.store(true, std::memory_order_relaxed); }
        else          { m_bDataReady.store(false, std::memory_order_relaxed); }
    }
    bool IsDataReady()
    {
        return m_bDataReady.load(std::memory_order_relaxed);
    }

    void WriteDataAndSwap( std::map< std::string, std::string >& kData )
    {
        SetDataReady(false);
        m_pWriteBuffer->clear();
        ( *m_pWriteBuffer ) = std::move( kData );

        //swap buffer pointer
        auto tempBuffer = m_pReadBuffer;
        m_pReadBuffer = m_pWriteBuffer;
        m_pWriteBuffer = tempBuffer;

        SetDataReady(true);
    }

private:

    std::atomic<bool> m_bDataReady{false};

    std::map< std::string, std::string > m_kFoundedListA;
    std::map< std::string, std::string > m_kFoundedListB;
    std::map< std::string, std::string >* m_pWriteBuffer = &m_kFoundedListA;
    std::map< std::string, std::string >* m_pReadBuffer = &m_kFoundedListB;
};
#pragma endregion 

#pragma region PhrozenDeviceSearcher
class PhrozenDeviceSearcher
{
public:

static void StartSearch();
static void StopSearch();
static bool IsDataReady();
static std::map< std::string, std::string > GetList();

private:
static void Run() noexcept;
static void ProcessSearchMachine( std::map< std::string, std::string >& kResult );


static std::unique_ptr<boost::thread> t_;
static std::atomic<bool> stop_;



static std::exception_ptr eptr_;

static PhrozenDeviceSearchResult m_kSearchResult;
};
#pragma endregion 

#pragma region PhrozenDeviceManager
class PhrozenDeviceManager
{

public:
    PhrozenDeviceManager( PhrozenNetworkAgent* agent = nullptr);
    ~PhrozenDeviceManager();
    void set_agent( PhrozenNetworkAgent* agent);
    
    void DisconnectMachine();
    bool CreateAndConnectMachine( std::string strIp );
    PhrozenMachineObject_Dev* GetConnectingMachine();

    bool StartReceiveWebcam();
    void StopReceiveWebcam();

    bool StartSendMessage();
    void StopSendMessage();

    bool StartReceiveMessage();
    void StopReceiveMessage();

    bool IsMachineConnecting() { return m_spConnectedMachine != nullptr; }

private:
    bool CreateMachine( std::string dev_id , std::shared_ptr< PhrozenMachineObject_Dev >& spObject );
    
    std::shared_ptr< PhrozenMachineObject_Dev > m_spConnectedMachine{ nullptr };
    PhrozenNetworkAgent* m_pNetworkAgent { nullptr };

    std::vector<bool> m_kSendingList;
private:
    std::unique_ptr< WorkerFuncSafe > m_spRecieveWebcam{ nullptr };
    std::unique_ptr< WorkerFuncSafe > m_spSendMessage{ nullptr };
    std::unique_ptr< WorkerFuncSafe > m_spReceiveMessage{ nullptr };
};
#pragma endregion 

} // namespace Slic3r

#endif //  slic3r_DeviceManager_hpp_
