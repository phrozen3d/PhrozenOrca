#include "PhrozenMachineDatas.hpp"


namespace Slic3r {

#pragma region PhrozenSendMessageGenerator

json PhrozenSendMessageGenerator::GenPrinterControllerPayloadMsg()
{
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
    return payload;
}

json PhrozenSendMessageGenerator::GenHistoryPayloadMsg()
{
    json payload_history;
    payload_history["jsonrpc"] = "2.0";
    payload_history["method"] = "server.history.list";
    payload_history["id"] = 5656;
    return payload_history;
}

json PhrozenSendMessageGenerator::GenAMSPayloadMsg()
{
    json payload_AMS;
    payload_AMS["jsonrpc"] = "2.0";
    payload_AMS["method"] = "printer.gcode.script";
    payload_AMS["params"]["script"] = "P114";
    payload_AMS["id"] = PhrozenPrinterID::printer_gcode_script;
    return payload_AMS;
}

json PhrozenSendMessageGenerator::GenNozzlePayloadMsg()
{
    json payload_Nozzle;
    payload_Nozzle["jsonrpc"] = "2.0";
    payload_Nozzle["method"] = "printer.gcode.script";
    payload_Nozzle["params"]["script"] = "PRZ_ADC";
    payload_Nozzle["id"] = PhrozenPrinterID::printer_gcode_script;
    return payload_Nozzle;
}

json PhrozenSendMessageGenerator::GenLEDPayloadMsg()
{
    json payload_LED;
    payload_LED["jsonrpc"] = "2.0";
    payload_LED["method"] = "printer.gcode.script";
    payload_LED["params"]["script"] = "P0 LED_GetState";
    payload_LED["id"] = PhrozenPrinterID::printer_gcode_script;
    return payload_LED;
}
    

#pragma endregion




} // namespace Slic3r
