#ifndef slic3r_MeshyDialog_hpp_
#define slic3r_MeshyDialog_hpp_

#include <wx/dialog.h>
#include <wx/timer.h>

#include <atomic>
#include <memory>
#include <string>

class wxTextCtrl;
class wxFilePickerCtrl;
class wxButton;
class wxGauge;
class wxStaticText;
class wxChoice;
class wxCheckBox;
class wxSpinCtrl;

namespace Slic3r { namespace GUI {

class MeshyDialog : public wxDialog
{
public:
    MeshyDialog(wxWindow* parent);
    ~MeshyDialog() override;

private:
    // Inputs
    wxTextCtrl*        m_api_key_ctrl    {nullptr};
    wxFilePickerCtrl*  m_image_picker    {nullptr};

    // Model settings
    wxChoice*          m_ai_model        {nullptr};
    wxChoice*          m_model_type      {nullptr};
    wxChoice*          m_topology        {nullptr};
    wxSpinCtrl*        m_polycount       {nullptr};
    wxChoice*          m_symmetry        {nullptr};
    wxChoice*          m_pose_mode       {nullptr};
    wxCheckBox*        m_should_remesh   {nullptr};

    // Texture settings
    wxCheckBox*        m_should_texture  {nullptr};
    wxCheckBox*        m_enable_pbr      {nullptr};
    wxCheckBox*        m_hd_texture      {nullptr};
    wxTextCtrl*        m_texture_prompt  {nullptr};

    // Progress / buttons
    wxGauge*           m_progress        {nullptr};
    wxStaticText*      m_status_text     {nullptr};
    wxButton*          m_generate_btn    {nullptr};
    wxButton*          m_cancel_btn      {nullptr};

    wxTimer            m_poll_timer;
    std::string        m_task_id;
    std::string        m_api_key;

    std::shared_ptr<std::atomic<bool>> m_alive;

    void on_generate(wxCommandEvent& evt);
    void on_poll_timer(wxTimerEvent& evt);
    void on_texture_toggle(wxCommandEvent& evt);

    void start_task_from_image(const std::string& image_path);
    void poll_task_status();
    void download_and_load(const std::string& model_url, const std::string& ext);

    void set_status(const wxString& text);
    void set_progress(int pct);
    void set_busy(bool busy);
    void show_error(const wxString& msg);

    void load_saved_settings();
    void save_current_settings();

    static std::string image_to_data_uri(const std::string& path);
};

}} // namespace Slic3r::GUI

#endif // slic3r_MeshyDialog_hpp_
