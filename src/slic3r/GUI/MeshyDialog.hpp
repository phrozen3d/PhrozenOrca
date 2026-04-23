#ifndef slic3r_MeshyDialog_hpp_
#define slic3r_MeshyDialog_hpp_

#include "GUI_Utils.hpp"
#include <wx/timer.h>

#include <atomic>
#include <memory>
#include <string>

class Button;
class TextInput;
class ComboBox;
class CheckBox;
class SpinInput;
class ProgressBar;
class wxStaticText;
class wxStaticBitmap;

namespace Slic3r { namespace GUI {

class MeshyDialog : public DPIDialog
{
public:
    MeshyDialog(wxWindow* parent);
    ~MeshyDialog() override;

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    // Two-stage task flow:
    //   Image  — image-to-3d is running/just completed.
    //   MultiColor — multi-color-print is running (entered from Preview via Load).
    enum class Phase { Image, MultiColor };
    Phase m_phase {Phase::Image};

    // Input
    ::TextInput*   m_api_key_ctrl   {nullptr};
    ::TextInput*   m_image_path_ctrl{nullptr};
    ::Button*      m_browse_btn     {nullptr};

    // Model settings
    ::ComboBox*    m_ai_model       {nullptr};
    ::ComboBox*    m_model_type     {nullptr};
    ::ComboBox*    m_topology       {nullptr};
    ::SpinInput*   m_polycount      {nullptr};
    ::ComboBox*    m_symmetry       {nullptr};
    ::ComboBox*    m_pose_mode      {nullptr};
    ::CheckBox*    m_should_remesh  {nullptr};

    // Output
    ::CheckBox*    m_multi_color    {nullptr};
    wxStaticText*  m_max_colors_label{nullptr};
    ::SpinInput*   m_max_colors     {nullptr};
    wxStaticText*  m_max_depth_label{nullptr};
    ::SpinInput*   m_max_depth      {nullptr};

    // Progress / actions
    ::ProgressBar* m_progress       {nullptr};
    wxStaticText*  m_status_text    {nullptr};
    ::Button*      m_generate_btn   {nullptr};
    ::Button*      m_cancel_btn     {nullptr};

    // Preview phase
    wxStaticText*   m_preview_label  {nullptr};
    wxStaticBitmap* m_preview_image  {nullptr};
    ::Button*       m_load_btn       {nullptr};
    ::Button*       m_discard_btn    {nullptr};

    std::string     m_picked_model_url;
    std::string     m_picked_ext;

    wxTimer        m_poll_timer;
    std::string    m_image_task_id;  // image-to-3d task id (cached for multi-color stage)
    std::string    m_task_id;        // task currently being polled
    std::string    m_api_key;

    std::shared_ptr<std::atomic<bool>> m_alive;

    void on_generate(wxCommandEvent& evt);
    void on_browse(wxCommandEvent& evt);
    void on_poll_timer(wxTimerEvent& evt);
    void on_multi_color_toggle(wxCommandEvent& evt);
    void on_load_to_plate(wxCommandEvent& evt);
    void on_discard(wxCommandEvent& evt);

    void start_task_from_image(const std::string& image_path);
    void poll_task_status();
    void enter_preview_phase(const std::string& thumbnail_url);
    void start_multi_color_task();
    void reset_to_idle();
    void download_and_load(const std::string& model_url, const std::string& ext);

    void set_status(const wxString& text);
    void set_progress(int pct);
    void set_busy(bool busy);
    void show_error(const wxString& msg);
    void sync_multi_color_visibility();

    void load_saved_settings();
    void save_current_settings();

    static std::string image_to_data_uri(const std::string& path);
};

}} // namespace Slic3r::GUI

#endif // slic3r_MeshyDialog_hpp_
