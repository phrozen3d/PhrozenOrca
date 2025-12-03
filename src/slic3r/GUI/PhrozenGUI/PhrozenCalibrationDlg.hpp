#ifndef slic3r_GUI_PhrozenCalibrationDlg_hpp_
#define slic3r_GUI_PhrozenCalibrationDlg_hpp_

#include <wx/wx.h>
#include <wx/intl.h>
#include <wx/collpane.h>
#include <wx/dataview.h>
#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/dataview.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/string.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/hyperlink.h>
#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/popupwin.h>
#include <wx/spinctrl.h>
#include <wx/artprov.h>
#include <wx/wrapsizer.h>

#include "../GUI_Utils.hpp"
#include "../wxExtensions.hpp"
#include "../DeviceManager.hpp"
#include "../Plater.hpp"
#include "../Widgets/Label.hpp"
#include "../Widgets/Button.hpp"
#include "../Widgets/StepCtrl.hpp"
#include "../Widgets/CheckBox.hpp"

namespace Slic3r { namespace GUI {


wxDECLARE_EVENT(EVT_PHROZEN_CALIBRATION_SELECTED, wxCommandEvent);

// Custom progress bar widget for calibration items
class CalibrationProgressBar : public wxWindow
{
public:
    CalibrationProgressBar(wxWindow* parent, wxString label);
    ~CalibrationProgressBar();

    void SetProgress(int percent);
    int GetProgress() const { return m_progress; }

private:
    wxString m_label;
    int m_progress{0};
    bool m_hover{false};
    bool m_pressed{false};

    void OnPaint(wxPaintEvent& event);
    void OnMouseEnter(wxMouseEvent& event);
    void OnMouseLeave(wxMouseEvent& event);
    void OnMouseDown(wxMouseEvent& event);
    void OnMouseUp(wxMouseEvent& event);

    wxDECLARE_EVENT_TABLE();
};

class PhrozenCalibrationDlg : public DPIDialog
{
private:
    enum class ECalibType : int32_t 
    {
        None,
        Auto_Leveling,
        Resonance_Compensation,
        Temperature_Calibration
    };

    CalibrationProgressBar* m_auto_leveling{nullptr};
    CalibrationProgressBar* m_resonance_compensation{nullptr};
    CalibrationProgressBar* m_temperature_calibration{nullptr};

    wxStaticText* m_description_text{nullptr};

    std::unique_ptr<boost::thread> m_spSend_command_thread{nullptr};
    std::weak_ptr<int> m_token;

    int m_nRefreshInterval{ 500 }; // 0.5 second
    std::unique_ptr<wxTimer> m_spRefresh_timer{nullptr};

    ECalibType m_eCurrentProcessingCalib{ ECalibType::None };
    void OnCalibrationSelected( wxCommandEvent& event );
    void SendCommandToMachine( const ECalibType& eType );
    void OnTimer( wxTimerEvent& event );
    void StartRefreshTimer();
    void StopRefreshTimer();

    bool m_bTestMode = false;
    void OnRefreshTest();

public:
    PhrozenCalibrationDlg(Plater *plater = nullptr);
    ~PhrozenCalibrationDlg();
    int ShowModal() wxOVERRIDE;
    void SyncAndUpdateMachineStatus();
    void on_dpi_changed(const wxRect &suggested_rect) override;
    void SetAutoLevelingProgress(int percent);
    void SetResonanceCompensationProgress(int percent);
    void SetTemperatureCalibrationProgress(int percent);

    bool Show(bool show) override;

    
};

}} // namespace Slic3r::GUI

#endif
