#ifndef slic3r_SLAPrinterSettingsDialog_hpp_
#define slic3r_SLAPrinterSettingsDialog_hpp_

#include "GUI_Utils.hpp"

class wxChoice;
class wxActivateEvent;
class wxPanel;
class ScalableButton;
class TextInput;

namespace Slic3r {
namespace GUI {

class PlaterPresetComboBox;
class Tab;

class SLAPrinterSettingsDialog : public DPIDialog
{
public:
    explicit SLAPrinterSettingsDialog(wxWindow *parent);

    /** Focus a field that exists in this dialog; used when jumping from preset search. */
    bool try_focus_printer_search_result(const std::string &opt_key);

private:
    enum class MirrorMode {
        LCD,
        DLP
    };

    PlaterPresetComboBox *m_printer_combo { nullptr };
    ScalableButton       *m_btn_save      { nullptr };
    ScalableButton       *m_btn_delete    { nullptr };
    ScalableButton       *m_btn_search    { nullptr };
    ScalableButton       *m_btn_reset_resolution_x { nullptr };
    ScalableButton       *m_btn_reset_resolution_y { nullptr };
    ScalableButton       *m_btn_reset_size_x       { nullptr };
    ScalableButton       *m_btn_reset_size_y       { nullptr };
    ScalableButton       *m_btn_reset_size_z       { nullptr };
    wxPanel              *m_search_panel  { nullptr };
    ::TextInput          *m_search_input  { nullptr };

    wxChoice   *m_mirror_choice     { nullptr };
    ::TextInput *m_resolution_x_ctrl { nullptr };
    ::TextInput *m_resolution_y_ctrl { nullptr };
    ::TextInput *m_size_x_ctrl       { nullptr };
    ::TextInput *m_size_y_ctrl       { nullptr };
    ::TextInput *m_size_z_ctrl       { nullptr };
    bool         m_is_handling_preset_selection { false };

    Tab* current_printer_tab() const;
    void build_dialog();
    void reload_from_preset();
    void sync_selected_user_printer_json_from_disk();
    void refresh_preset_buttons();
    void refresh_reset_buttons();
    void on_preset_selected(int selection);
    void on_dialog_activated(wxActivateEvent &event);
    void reset_field_to_default(::TextInput *input, const std::string &key);
    bool sync_local_to_tab(bool show_errors = true);
    void on_dpi_changed(const wxRect &suggested_rect) override;
    bool get_default_int_for_key(const std::string &key, int &out) const;
    bool get_default_float_for_key(const std::string &key, double &out) const;
    bool get_default_printable_size_xy(double &x, double &y) const;
    bool get_current_printable_size_xy(double &x, double &y) const;

    static MirrorMode mirror_mode_from_config(const DynamicPrintConfig &config);
    static void apply_mirror_mode(DynamicPrintConfig &config, MirrorMode mode);
};

} // namespace GUI
} // namespace Slic3r

#endif
