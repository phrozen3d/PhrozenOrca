#ifndef slic3r_PhrozenDeviceManager_hpp_
#define slic3r_PhrozenDeviceManager_hpp_

#include "../DeviceManager.hpp"
#include <atomic>

namespace Slic3r {

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

    virtual std::string GetPhrozenWebCameraStreamUrl() override;
    virtual std::string GetPhrozenWebCameraSnapshotUrl() override;
    virtual bool GetPhrozenWebCameraSnapshotImage( std::vector<unsigned char>& kWebCameraImageData ) override;
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
};

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

class PhrozenDeviceManager
{
private:
    NetworkAgent* m_agent { nullptr };

    PhrozenMachineObject* create_machine(std::string dev_id);

    static PhrozenDeviceSearchResult m_kDeviceResult;

public:
    PhrozenDeviceManager(NetworkAgent* agent = nullptr);
    ~PhrozenDeviceManager();
    void set_agent(NetworkAgent* agent);

    std::mutex listMutex;
    std::string m_strSelected_machine_ip;                               /* dev_id */
    std::map<std::string, std::shared_ptr< PhrozenMachineObject > > m_kConnectedBefore;   /* dev_id -> MachineObject*  cloudMachine of User */

    void erase_connected_before_machine(std::string dev_id);

    bool set_selected_machine(std::string dev_id,  bool do_connect = false );
    PhrozenMachineObject* get_selected_machine();

    void get_connected_before_ip_list( std::vector< std::string >& kList );

    void disconnect_all();

    boost::thread* m_thread{nullptr};

    
};

} // namespace Slic3r

#endif //  slic3r_DeviceManager_hpp_
