#ifndef slic3r_GUI_LayerPrintTimeCompensationDialog_hpp_
#define slic3r_GUI_LayerPrintTimeCompensationDialog_hpp_

#include <wx/dialog.h>

class wxCommandEvent;
class wxStaticText;
class SpinInput;
class TextInput;

namespace Slic3r { namespace GUI {

// Calculator: (actual - predicted) / layer_count -> per-layer seconds, written to process preset on Apply.
class LayerPrintTimeCompensationDialog : public wxDialog
{
public:
    LayerPrintTimeCompensationDialog(wxWindow *parent, double initial_layer_compensation_s);
    double layer_compensation_result_s() const { return m_result_layer_s; }

private:
    void recalc();
    void on_apply();
    void on_spin(wxCommandEvent &evt);
    void save_ui_state();

    struct PersistedState {
        int  pred_h{ 0 }, pred_m{ 0 }, pred_s{ 0 };
        int  act_h{ 0 }, act_m{ 0 }, act_s{ 0 };
        int  layers{ 0 };
        bool initialized{ false };
    };
    static PersistedState s_state;

    SpinInput *   m_pred_h{ nullptr };
    SpinInput *   m_pred_m{ nullptr };
    SpinInput *   m_pred_s{ nullptr };
    SpinInput *   m_act_h{ nullptr };
    SpinInput *   m_act_m{ nullptr };
    SpinInput *   m_act_s{ nullptr };
    SpinInput *   m_layers{ nullptr };
    TextInput *   m_readout{ nullptr };
    wxStaticText *m_readout_title{ nullptr };
    double        m_result_layer_s{ 0. };
};

}} // namespace Slic3r::GUI

#endif
