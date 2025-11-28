#ifndef slic3r_PhrozenDeviceManager_hpp_
#define slic3r_PhrozenDeviceManager_hpp_

#include "../DeviceManager.hpp"

namespace Slic3r {

class PhrozenMachineObject : public MachineObject
{

public:

    PhrozenMachineObject( std::string name, std::string id, std::string ip );
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

    virtual bool IsPhrozenConnected() override;
    virtual bool IsPhrozenStartReceiving() override;

    virtual std::string GetPhrozenConnectedMachineIp() override;
};

} // namespace Slic3r

#endif //  slic3r_DeviceManager_hpp_
