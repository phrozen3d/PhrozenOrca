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

    // set command to machine
    virtual void SetPhrozenCommand_bed_temp( int nTemp ) override;
    virtual void SetPhrozenCommand_nozzle_temp( int nTemp ) override;
    virtual void SetPhrozenCommand_cooling_auxiliary( int nPower ) override;
    virtual void SetPhrozenCommand_cooling_part( int nPower ) override;
    virtual void SetPhrozenCommand_cooling_shield( int nPower ) override;
};

} // namespace Slic3r

#endif //  slic3r_DeviceManager_hpp_
