#include "MeshyDialog.hpp"

#include <algorithm>
#include <iterator>

#include <wx/app.h>
#include <wx/bitmap.h>
#include <wx/filedlg.h>
#include <wx/image.h>
#include <wx/mstream.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/settings.h>
#include <wx/statbmp.h>
#include <wx/statbox.h>
#include <wx/statline.h>
#include <wx/stattext.h>

#include <boost/beast/core/detail/base64.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/log/trivial.hpp>

#include <nlohmann/json.hpp>

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "Plater.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/BuildVolume.hpp"
#include "libslic3r/Model.hpp"
#include "slic3r/Utils/Http.hpp"

#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/ProgressBar.hpp"
#include "Widgets/SpinInput.hpp"
#include "Widgets/TextInput.hpp"

namespace fs = boost::filesystem;
using json = nlohmann::json;

namespace Slic3r { namespace GUI {

static const char* MESHY_API_BASE       = "https://api.meshy.ai";
static const char* MESHY_IMG_TO_3D_PATH = "/openapi/v1/image-to-3d";
static const char* MESHY_MC_PATH        = "/openapi/v1/print/multi-color";
static const char* CFG_SECTION          = "meshy";
static const int   POLL_INTERVAL_MS     = 4000;

static const wxColour COLOR_BG          (255, 255, 255);
static const wxColour COLOR_LABEL       ( 38,  46,  48);
static const wxColour COLOR_ERROR       (255, 111,   0);
static const wxColour COLOR_STATUS      ( 80,  80,  80);

static void fit_loaded_objects_to_half_bed(Plater& plater, const std::vector<size_t>& obj_idxs)
{
    if (obj_idxs.empty()) return;

    auto bed_size = plater.build_volume().bounding_volume2d().size();
    const double target_max = std::min(bed_size.x(), bed_size.y()) / 2.0;
    if (target_max <= 0.0) return;

    Model& model = wxGetApp().model();
    BoundingBoxf3 bbox;
    for (size_t idx : obj_idxs) {
        if (idx < model.objects.size())
            bbox.merge(model.objects[idx]->bounding_box_exact());
    }
    auto size = bbox.size();
    const double current_max = std::max(size.x(), size.y());
    if (current_max <= 1e-6) return;

    const double factor = target_max / current_max;
    for (size_t idx : obj_idxs) {
        if (idx >= model.objects.size()) continue;
        ModelObject* mo = model.objects[idx];
        for (ModelInstance* inst : mo->instances)
            inst->set_scaling_factor(inst->get_scaling_factor() * factor);
        mo->invalidate_bounding_box();
    }

    BoundingBoxf3 scaled_bbox;
    for (size_t idx : obj_idxs) {
        if (idx < model.objects.size())
            scaled_bbox.merge(model.objects[idx]->bounding_box_exact());
    }
    auto bed_center = plater.build_volume().bounding_volume2d().center();
    Vec3d delta(
        bed_center.x() - scaled_bbox.center().x(),
        bed_center.y() - scaled_bbox.center().y(),
        -scaled_bbox.min.z()
    );
    for (size_t idx : obj_idxs) {
        if (idx >= model.objects.size()) continue;
        ModelObject* mo = model.objects[idx];
        for (ModelInstance* inst : mo->instances)
            inst->set_offset(inst->get_offset() + delta);
        mo->invalidate_bounding_box();
    }

    plater.update();
}

// ---- construction helpers --------------------------------------------------

static wxStaticText* form_label(wxWindow* parent, const wxString& text)
{
    auto* t = new wxStaticText(parent, wxID_ANY, text);
    t->SetFont(Label::Body_14);
    t->SetForegroundColour(COLOR_LABEL);
    return t;
}

static wxStaticText* section_title(wxWindow* parent, const wxString& text)
{
    auto* t = new wxStaticText(parent, wxID_ANY, text);
    t->SetFont(Label::Head_14);
    t->SetForegroundColour(COLOR_LABEL);
    return t;
}

static void add_form_row(wxFlexGridSizer* grid, wxWindow* parent,
                         const wxString& label_text, wxWindow* ctrl)
{
    grid->Add(form_label(parent, label_text), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(ctrl, 1, wxEXPAND);
}

static wxSizer* make_check_row(wxWindow* parent, ::CheckBox** out_cb,
                               const wxString& label_text, wxStaticText** out_label = nullptr)
{
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    *out_cb = new ::CheckBox(parent);
    auto* lbl = form_label(parent, label_text);
    if (out_label) *out_label = lbl;
    row->Add(*out_cb, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, parent->FromDIP(6));
    row->Add(lbl,     0, wxALIGN_CENTER_VERTICAL);
    return row;
}

// ----------------------------------------------------------------------------

MeshyDialog::MeshyDialog(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, _L("Generate 3D model from image (Meshy)"),
                wxDefaultPosition, wxDefaultSize,
                wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER)
    , m_poll_timer(this)
    , m_alive(std::make_shared<std::atomic<bool>>(true))
{
    SetBackgroundColour(COLOR_BG);
    SetFont(wxGetApp().normal_font());

    const int pad = FromDIP(10);
    const int gap = FromDIP(6);
    const wxSize input_sz   = wxSize(FromDIP(360), FromDIP(28));
    const wxSize combo_sz   = wxSize(FromDIP(360), FromDIP(28));
    const wxSize spin_sz    = wxSize(FromDIP(360), FromDIP(28));
    const wxSize small_spin = wxSize(FromDIP(120), FromDIP(28));
    const wxSize btn_sz     = wxSize(FromDIP(90),  FromDIP(28));
    const wxSize browse_sz  = wxSize(FromDIP(84),  FromDIP(28));

    auto* outer = new wxBoxSizer(wxVERTICAL);

    auto* header = new wxStaticText(this, wxID_ANY, _L("Image to 3D (Meshy)"));
    header->SetFont(Label::Head_16);
    header->SetForegroundColour(COLOR_LABEL);
    outer->Add(header, 0, wxLEFT | wxRIGHT | wxTOP, pad);
    outer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);

    // ---- Input --------------------------------------------------------------
    outer->Add(section_title(this, _L("Input")), 0, wxLEFT | wxRIGHT | wxTOP, pad);
    auto* input_grid = new wxFlexGridSizer(2, gap, gap);
    input_grid->AddGrowableCol(1, 1);

    m_api_key_ctrl = new ::TextInput(this, wxEmptyString, wxEmptyString, wxEmptyString,
                                     wxDefaultPosition, input_sz, wxTE_PASSWORD);

    auto* file_row = new wxBoxSizer(wxHORIZONTAL);
    m_image_path_ctrl = new ::TextInput(this, wxEmptyString, wxEmptyString, wxEmptyString,
                                        wxDefaultPosition,
                                        wxSize(FromDIP(260), FromDIP(28)), wxTE_READONLY);
    m_browse_btn = new ::Button(this, _L("Browse..."));
    m_browse_btn->SetMinSize(browse_sz);
    file_row->Add(m_image_path_ctrl, 1, wxRIGHT | wxALIGN_CENTER_VERTICAL, gap);
    file_row->Add(m_browse_btn,      0, wxALIGN_CENTER_VERTICAL);

    add_form_row(input_grid, this, _L("API key"), m_api_key_ctrl);
    input_grid->Add(form_label(this, _L("Input image")), 0, wxALIGN_CENTER_VERTICAL);
    input_grid->Add(file_row, 1, wxEXPAND);
    outer->Add(input_grid, 0, wxEXPAND | wxALL, pad);

    // ---- Model settings -----------------------------------------------------
    outer->Add(section_title(this, _L("Model settings")), 0, wxLEFT | wxRIGHT | wxTOP, pad);
    auto* model_grid = new wxFlexGridSizer(2, gap, gap);
    model_grid->AddGrowableCol(1, 1);

    m_ai_model = new ::ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, combo_sz,
                                0, nullptr, wxCB_READONLY);
    m_ai_model->Append("latest");
    m_ai_model->Append("meshy-6");
    m_ai_model->Append("meshy-5");
    m_ai_model->SetSelection(0);

    m_model_type = new ::ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, combo_sz,
                                  0, nullptr, wxCB_READONLY);
    m_model_type->Append("standard");
    m_model_type->Append("lowpoly");
    m_model_type->SetSelection(0);

    m_topology = new ::ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, combo_sz,
                                0, nullptr, wxCB_READONLY);
    m_topology->Append("triangle");
    m_topology->Append("quad");
    m_topology->SetSelection(0);

    m_polycount = new ::SpinInput(this, "30000", wxEmptyString,
                                  wxDefaultPosition, spin_sz, 0,
                                  100, 300000, 30000, 1000);

    m_symmetry = new ::ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, combo_sz,
                                0, nullptr, wxCB_READONLY);
    m_symmetry->Append("auto");
    m_symmetry->Append("off");
    m_symmetry->Append("on");
    m_symmetry->SetSelection(0);

    m_pose_mode = new ::ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, combo_sz,
                                 0, nullptr, wxCB_READONLY);
    m_pose_mode->Append(_L("(default)"));
    m_pose_mode->Append("a-pose");
    m_pose_mode->Append("t-pose");
    m_pose_mode->SetSelection(0);

    add_form_row(model_grid, this, _L("AI model"),         m_ai_model);
    add_form_row(model_grid, this, _L("Mesh type"),        m_model_type);
    add_form_row(model_grid, this, _L("Topology"),         m_topology);
    add_form_row(model_grid, this, _L("Target polycount"), m_polycount);
    add_form_row(model_grid, this, _L("Symmetry"),         m_symmetry);
    add_form_row(model_grid, this, _L("Pose"),             m_pose_mode);
    outer->Add(model_grid, 0, wxEXPAND | wxALL, pad);

    auto* remesh_row = make_check_row(this, &m_should_remesh, _L("Enable remeshing"));
    m_should_remesh->SetValue(true);
    outer->Add(remesh_row, 0, wxLEFT | wxRIGHT | wxBOTTOM, pad);

    // ---- Output -------------------------------------------------------------
    outer->Add(section_title(this, _L("Output")), 0, wxLEFT | wxRIGHT | wxTOP, pad);

    auto* mc_row = make_check_row(this, &m_multi_color,
                                  _L("Multi-color 3MF (for multi-material printing)"));
    outer->Add(mc_row, 0, wxLEFT | wxRIGHT | wxTOP, pad);

    auto* mc_grid = new wxFlexGridSizer(2, gap, gap);
    mc_grid->AddGrowableCol(1, 1);
    m_max_colors = new ::SpinInput(this, "4", wxEmptyString,
                                   wxDefaultPosition, small_spin, 0, 1, 16, 4, 1);
    m_max_depth  = new ::SpinInput(this, "4", wxEmptyString,
                                   wxDefaultPosition, small_spin, 0, 3, 6, 4, 1);
    m_max_colors_label = form_label(this, _L("Max colors (1-16)"));
    m_max_depth_label  = form_label(this, _L("Quadtree depth (3-6)"));
    mc_grid->Add(m_max_colors_label, 0, wxALIGN_CENTER_VERTICAL);
    mc_grid->Add(m_max_colors,       0);
    mc_grid->Add(m_max_depth_label,  0, wxALIGN_CENTER_VERTICAL);
    mc_grid->Add(m_max_depth,        0);
    outer->Add(mc_grid, 0, wxEXPAND | wxALL, pad);

    // ---- Progress -----------------------------------------------------------
    m_progress = new ::ProgressBar(this, wxID_ANY, 100, wxDefaultPosition,
                                   wxSize(-1, FromDIP(8)));
    outer->Add(m_progress, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);

    m_status_text = new wxStaticText(this, wxID_ANY, wxEmptyString);
    m_status_text->SetFont(Label::Body_12);
    m_status_text->SetForegroundColour(COLOR_STATUS);
    outer->Add(m_status_text, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);

    // ---- Preview (hidden until SUCCEEDED) -----------------------------------
    m_preview_label = section_title(this, _L("Preview"));
    m_preview_label->Hide();
    outer->Add(m_preview_label, 0, wxLEFT | wxRIGHT | wxTOP, pad);

    m_preview_image = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap,
                                         wxDefaultPosition, wxSize(FromDIP(240), FromDIP(240)));
    m_preview_image->Hide();
    auto* preview_row = new wxBoxSizer(wxHORIZONTAL);
    preview_row->AddStretchSpacer(1);
    preview_row->Add(m_preview_image, 0, wxALIGN_CENTER);
    preview_row->AddStretchSpacer(1);
    outer->Add(preview_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);

    // ---- Action buttons -----------------------------------------------------
    auto* btn_row = new wxBoxSizer(wxHORIZONTAL);
    m_cancel_btn   = new ::Button(this, _L("Close"));
    m_generate_btn = new ::Button(this, _L("Generate"));
    m_discard_btn  = new ::Button(this, _L("Discard"));
    m_load_btn     = new ::Button(this, _L("Load to plate"));
    m_cancel_btn  ->SetMinSize(btn_sz);
    m_generate_btn->SetMinSize(btn_sz);
    m_discard_btn ->SetMinSize(btn_sz);
    m_load_btn    ->SetMinSize(wxSize(FromDIP(120), FromDIP(28)));
    m_discard_btn->Hide();
    m_load_btn->Hide();
    btn_row->AddStretchSpacer(1);
    btn_row->Add(m_cancel_btn,   0, wxRIGHT, gap);
    btn_row->Add(m_discard_btn,  0, wxRIGHT, gap);
    btn_row->Add(m_generate_btn, 0);
    btn_row->Add(m_load_btn,     0);
    outer->Add(btn_row, 0, wxEXPAND | wxALL, pad);

    SetSizer(outer);
    outer->Fit(this);
    SetMinSize(GetSize());
    CentreOnParent();

    load_saved_settings();

    m_generate_btn ->Bind(wxEVT_BUTTON, &MeshyDialog::on_generate, this);
    m_cancel_btn   ->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); });
    m_browse_btn   ->Bind(wxEVT_BUTTON, &MeshyDialog::on_browse, this);
    m_load_btn     ->Bind(wxEVT_BUTTON, &MeshyDialog::on_load_to_plate, this);
    m_discard_btn  ->Bind(wxEVT_BUTTON, &MeshyDialog::on_discard, this);
    m_multi_color  ->Bind(wxEVT_TOGGLEBUTTON, &MeshyDialog::on_multi_color_toggle, this);
    Bind(wxEVT_TIMER, &MeshyDialog::on_poll_timer, this, m_poll_timer.GetId());

    sync_multi_color_visibility();
    wxGetApp().UpdateDlgDarkUI(this);
}

MeshyDialog::~MeshyDialog()
{
    *m_alive = false;
    if (m_poll_timer.IsRunning())
        m_poll_timer.Stop();
}

void MeshyDialog::on_dpi_changed(const wxRect& /*suggested_rect*/)
{
    Layout();
    Refresh();
}

void MeshyDialog::on_browse(wxCommandEvent&)
{
    wxFileDialog dlg(this, _L("Select image"), "", "",
                     "Images (*.png;*.jpg;*.jpeg)|*.png;*.jpg;*.jpeg",
                     wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() == wxID_OK)
        m_image_path_ctrl->GetTextCtrl()->SetValue(dlg.GetPath());
}

void MeshyDialog::on_multi_color_toggle(wxCommandEvent& evt)
{
    sync_multi_color_visibility();
    evt.Skip();
}

void MeshyDialog::sync_multi_color_visibility()
{
    const bool mc = m_multi_color->GetValue();
    m_max_colors->Enable(mc);
    m_max_depth->Enable(mc);
    m_max_colors_label->Enable(mc);
    m_max_depth_label->Enable(mc);
}

void MeshyDialog::load_saved_settings()
{
    auto* cfg = wxGetApp().app_config;
    if (!cfg) return;
    auto key = cfg->get(CFG_SECTION, "api_key");
    if (!key.empty()) m_api_key_ctrl->GetTextCtrl()->SetValue(key);

    auto set_combo = [](::ComboBox* c, const std::string& v) {
        if (v.empty()) return;
        for (unsigned int i = 0; i < c->GetCount(); ++i) {
            if (c->GetString(i).ToStdString() == v) { c->SetSelection(i); return; }
        }
    };
    set_combo(m_ai_model,   cfg->get(CFG_SECTION, "ai_model"));
    set_combo(m_model_type, cfg->get(CFG_SECTION, "model_type"));
    set_combo(m_topology,   cfg->get(CFG_SECTION, "topology"));
    set_combo(m_symmetry,   cfg->get(CFG_SECTION, "symmetry"));

    auto pc = cfg->get(CFG_SECTION, "polycount");
    if (!pc.empty()) { try { m_polycount->SetValue(std::stoi(pc)); } catch (...) {} }
    auto mxc = cfg->get(CFG_SECTION, "max_colors");
    if (!mxc.empty()) { try { m_max_colors->SetValue(std::stoi(mxc)); } catch (...) {} }
    auto mxd = cfg->get(CFG_SECTION, "max_depth");
    if (!mxd.empty()) { try { m_max_depth->SetValue(std::stoi(mxd)); } catch (...) {} }

    auto remesh = cfg->get(CFG_SECTION, "should_remesh");
    if (!remesh.empty()) m_should_remesh->SetValue(remesh == "1" || remesh == "true");
    auto mc = cfg->get(CFG_SECTION, "multi_color");
    if (!mc.empty()) m_multi_color->SetValue(mc == "1" || mc == "true");
}

void MeshyDialog::save_current_settings()
{
    auto* cfg = wxGetApp().app_config;
    if (!cfg) return;
    cfg->set(CFG_SECTION, "api_key",        m_api_key_ctrl->GetTextCtrl()->GetValue().ToStdString());
    cfg->set(CFG_SECTION, "ai_model",       m_ai_model->GetValue().ToStdString());
    cfg->set(CFG_SECTION, "model_type",     m_model_type->GetValue().ToStdString());
    cfg->set(CFG_SECTION, "topology",       m_topology->GetValue().ToStdString());
    cfg->set(CFG_SECTION, "symmetry",       m_symmetry->GetValue().ToStdString());
    cfg->set(CFG_SECTION, "polycount",      std::to_string(m_polycount->GetValue()));
    cfg->set(CFG_SECTION, "should_remesh",  m_should_remesh->GetValue() ? "1" : "0");
    cfg->set(CFG_SECTION, "multi_color",    m_multi_color->GetValue() ? "1" : "0");
    cfg->set(CFG_SECTION, "max_colors",     std::to_string(m_max_colors->GetValue()));
    cfg->set(CFG_SECTION, "max_depth",      std::to_string(m_max_depth->GetValue()));
    cfg->save();
}

void MeshyDialog::set_status(const wxString& text)
{
    m_status_text->SetLabel(text);
    Layout();
}

void MeshyDialog::set_progress(int pct)
{
    m_progress->SetValue(std::max(0, std::min(100, pct)));
}

void MeshyDialog::set_busy(bool busy)
{
    m_generate_btn->Enable(!busy);
    m_api_key_ctrl->Enable(!busy);
    m_image_path_ctrl->Enable(!busy);
    m_browse_btn->Enable(!busy);
    m_ai_model->Enable(!busy);
    m_model_type->Enable(!busy);
    m_topology->Enable(!busy);
    m_polycount->Enable(!busy);
    m_symmetry->Enable(!busy);
    m_pose_mode->Enable(!busy);
    m_should_remesh->Enable(!busy);
    m_multi_color->Enable(!busy);
    if (busy) {
        m_max_colors->Enable(false);
        m_max_depth->Enable(false);
    } else {
        sync_multi_color_visibility();
    }
}

void MeshyDialog::show_error(const wxString& msg)
{
    set_busy(false);
    if (m_poll_timer.IsRunning())
        m_poll_timer.Stop();
    m_status_text->SetForegroundColour(COLOR_ERROR);
    set_status(_L("Error:") + " " + msg);
    BOOST_LOG_TRIVIAL(error) << "Meshy: " << msg.ToStdString();
}

std::string MeshyDialog::image_to_data_uri(const std::string& path)
{
    fs::ifstream ifs(fs::path(path), std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(ifs)),
                      std::istreambuf_iterator<char>());

    std::string b64;
    b64.resize(boost::beast::detail::base64::encoded_size(bytes.size()));
    const auto n = boost::beast::detail::base64::encode(&b64[0], bytes.data(), bytes.size());
    b64.resize(n);

    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    std::string mime = "image/png";
    if (ext == ".jpg" || ext == ".jpeg")
        mime = "image/jpeg";
    return "data:" + mime + ";base64," + b64;
}

void MeshyDialog::on_generate(wxCommandEvent&)
{
    auto api_key    = m_api_key_ctrl->GetTextCtrl()->GetValue().ToStdString();
    auto image_path = m_image_path_ctrl->GetTextCtrl()->GetValue().ToStdString();

    if (api_key.empty()) {
        show_error(_L("API key is required."));
        return;
    }
    if (image_path.empty() || !fs::exists(image_path)) {
        show_error(_L("Please choose an image file."));
        return;
    }

    m_api_key = api_key;
    save_current_settings();
    m_phase = Phase::Image;
    m_image_task_id.clear();
    m_task_id.clear();

    m_status_text->SetForegroundColour(COLOR_STATUS);
    set_busy(true);
    set_progress(0);
    set_status(_L("Uploading image..."));
    start_task_from_image(image_path);
}

void MeshyDialog::start_task_from_image(const std::string& image_path)
{
    std::string data_uri;
    try {
        data_uri = image_to_data_uri(image_path);
    } catch (const std::exception& e) {
        show_error(wxString::Format("%s %s", _L("Failed to read image:"), e.what()));
        return;
    }

    // Multi-color mode requires textures (Meshy extracts the palette from them).
    const bool need_texture = m_multi_color->GetValue();

    json body = {
        {"image_url",        data_uri},
        {"ai_model",         m_ai_model->GetValue().ToStdString()},
        {"model_type",       m_model_type->GetValue().ToStdString()},
        {"topology",         m_topology->GetValue().ToStdString()},
        {"target_polycount", m_polycount->GetValue()},
        {"symmetry_mode",    m_symmetry->GetValue().ToStdString()},
        {"should_remesh",    m_should_remesh->GetValue()},
        {"should_texture",   need_texture},
    };
    if (m_pose_mode->GetSelection() > 0)
        body["pose_mode"] = m_pose_mode->GetValue().ToStdString();

    const std::string body_str = body.dump();
    auto alive = m_alive;
    const std::string url = std::string(MESHY_API_BASE) + MESHY_IMG_TO_3D_PATH;

    auto http = Http::post(url);
    http.header("Content-Type", "application/json")
        .header("Authorization", "Bearer " + m_api_key)
        .set_post_body(body_str)
        .on_complete([alive, this](std::string body, unsigned status) {
            wxGetApp().CallAfter([alive, this, body, status]() {
                if (!*alive) return;
                if (status != 200 && status != 201 && status != 202) {
                    show_error(wxString::Format("HTTP %u: %s", status, wxString::FromUTF8(body)));
                    return;
                }
                auto j = json::parse(body, nullptr, false);
                if (j.is_discarded()) {
                    show_error(_L("Invalid JSON response from Meshy."));
                    return;
                }
                std::string tid;
                if (j.contains("result") && j["result"].is_string())
                    tid = j["result"].get<std::string>();
                else if (j.contains("id") && j["id"].is_string())
                    tid = j["id"].get<std::string>();
                if (tid.empty()) {
                    show_error(wxString::Format("%s %s", _L("Unexpected response:"), wxString::FromUTF8(body)));
                    return;
                }
                m_task_id = tid;
                set_status(_L("Task submitted, generating...") + " (" + wxString::FromUTF8(tid) + ")");
                m_poll_timer.Start(POLL_INTERVAL_MS);
            });
        })
        .on_error([alive, this](std::string body, std::string error, unsigned status) {
            wxGetApp().CallAfter([alive, this, body, error, status]() {
                if (!*alive) return;
                show_error(wxString::Format("HTTP %u: %s %s",
                    status, wxString::FromUTF8(error), wxString::FromUTF8(body)));
            });
        })
        .perform();
}

void MeshyDialog::on_poll_timer(wxTimerEvent&)
{
    poll_task_status();
}

void MeshyDialog::poll_task_status()
{
    if (m_task_id.empty())
        return;

    const char* base_path = (m_phase == Phase::MultiColor) ? MESHY_MC_PATH : MESHY_IMG_TO_3D_PATH;
    const std::string url = std::string(MESHY_API_BASE) + base_path + "/" + m_task_id;

    auto alive = m_alive;
    auto http = Http::get(url);
    http.header("Authorization", "Bearer " + m_api_key)
        .on_complete([alive, this](std::string body, unsigned status) {
            wxGetApp().CallAfter([alive, this, body, status]() {
                if (!*alive) return;
                if (status != 200) {
                    BOOST_LOG_TRIVIAL(warning) << "Meshy poll HTTP " << status << ": " << body;
                    return;
                }
                auto j = json::parse(body, nullptr, false);
                if (j.is_discarded()) return;

                std::string task_status = j.value("status", "");
                int         progress    = j.value("progress", 0);
                set_progress(progress);

                wxString stage_tag = (m_phase == Phase::MultiColor)
                                     ? _L("Multi-color:") : _L("Generating:");
                set_status(stage_tag + " " + wxString::FromUTF8(task_status)
                           + wxString::Format(" (%d%%)", progress));

                if (task_status == "SUCCEEDED") {
                    m_poll_timer.Stop();
                    if (m_phase == Phase::Image) {
                        // Image-to-3d done. Cache model URLs + thumbnail, show preview.
                        std::string picked_url;
                        std::string picked_ext = ".stl";
                        if (j.contains("model_urls") && j["model_urls"].is_object()) {
                            const auto& mu = j["model_urls"];
                            for (const auto& kv : std::vector<std::pair<std::string,std::string>>{
                                    {"stl", ".stl"}, {"obj", ".obj"}, {"glb", ".glb"}}) {
                                if (mu.contains(kv.first) && mu[kv.first].is_string()) {
                                    std::string s = mu[kv.first].get<std::string>();
                                    if (!s.empty()) { picked_url = s; picked_ext = kv.second; break; }
                                }
                            }
                        }
                        m_image_task_id    = m_task_id;
                        m_picked_model_url = picked_url;
                        m_picked_ext       = picked_ext;
                        std::string thumb = j.value("thumbnail_url", "");
                        enter_preview_phase(thumb);
                    } else {
                        // Multi-color done. Pull 3mf URL and load.
                        std::string three_mf;
                        if (j.contains("model_urls") && j["model_urls"].is_object()) {
                            const auto& mu = j["model_urls"];
                            if (mu.contains("3mf") && mu["3mf"].is_string())
                                three_mf = mu["3mf"].get<std::string>();
                        }
                        if (three_mf.empty()) {
                            show_error(_L("Multi-color task returned no 3MF URL."));
                            return;
                        }
                        set_status(_L("Downloading multi-color 3MF..."));
                        download_and_load(three_mf, ".3mf");
                    }
                } else if (task_status == "FAILED" || task_status == "CANCELED") {
                    m_poll_timer.Stop();
                    std::string err_msg = j.value("task_error", json::object()).value("message", task_status);
                    show_error(_L("Task failed:") + " " + wxString::FromUTF8(err_msg));
                }
            });
        })
        .on_error([alive, this](std::string body, std::string error, unsigned status) {
            wxGetApp().CallAfter([alive, this, body, error, status]() {
                if (!*alive) return;
                BOOST_LOG_TRIVIAL(warning) << "Meshy poll error " << status << ": " << error << " " << body;
            });
        })
        .perform();
}

void MeshyDialog::enter_preview_phase(const std::string& thumbnail_url)
{
    m_generate_btn->Hide();
    m_discard_btn->Show();
    m_load_btn->Show();

    m_preview_label->Show();
    m_preview_image->Show();
    set_progress(100);
    set_status(_L("Generation complete. Review preview and choose Load or Discard."));

    set_busy(true);
    m_discard_btn->Enable(true);
    m_load_btn->Enable(true);

    Layout();
    Fit();

    if (thumbnail_url.empty()) {
        BOOST_LOG_TRIVIAL(info) << "Meshy: no thumbnail_url in response";
        return;
    }

    auto alive = m_alive;
    auto http = Http::get(thumbnail_url);
    http.on_complete([alive, this](std::string body, unsigned status) {
        wxGetApp().CallAfter([alive, this, body, status]() {
            if (!*alive) return;
            if (status != 200) {
                BOOST_LOG_TRIVIAL(warning) << "Meshy thumbnail HTTP " << status;
                return;
            }
            wxMemoryInputStream mis(body.data(), body.size());
            wxImage img;
            if (!img.LoadFile(mis, wxBITMAP_TYPE_ANY) || !img.IsOk()) {
                BOOST_LOG_TRIVIAL(warning) << "Meshy thumbnail: invalid image";
                return;
            }
            const int target = FromDIP(240);
            double ratio = std::min((double) target / img.GetWidth(),
                                    (double) target / img.GetHeight());
            if (ratio > 0 && ratio != 1.0) {
                img = img.Scale((int)(img.GetWidth() * ratio),
                                (int)(img.GetHeight() * ratio),
                                wxIMAGE_QUALITY_HIGH);
            }
            m_preview_image->SetBitmap(wxBitmap(img));
            Layout();
            Fit();
        });
    })
    .on_error([alive](std::string body, std::string error, unsigned status) {
        BOOST_LOG_TRIVIAL(warning) << "Meshy thumbnail error " << status << ": " << error;
    })
    .perform();
}

void MeshyDialog::on_load_to_plate(wxCommandEvent&)
{
    m_load_btn->Enable(false);
    m_discard_btn->Enable(false);

    if (m_multi_color->GetValue()) {
        // Two-stage: spin up a multi-color-print task using the image task id,
        // then poll it, then download the 3mf.
        set_status(_L("Starting multi-color 3MF conversion..."));
        set_progress(0);
        start_multi_color_task();
    } else {
        if (m_picked_model_url.empty()) return;
        set_status(_L("Downloading model..."));
        download_and_load(m_picked_model_url, m_picked_ext);
    }
}

void MeshyDialog::start_multi_color_task()
{
    json body = {
        {"input_task_id", m_image_task_id},
        {"max_colors",    m_max_colors->GetValue()},
        {"max_depth",     m_max_depth->GetValue()},
    };
    const std::string body_str = body.dump();
    auto alive = m_alive;
    const std::string url = std::string(MESHY_API_BASE) + MESHY_MC_PATH;

    m_phase = Phase::MultiColor;

    auto http = Http::post(url);
    http.header("Content-Type", "application/json")
        .header("Authorization", "Bearer " + m_api_key)
        .set_post_body(body_str)
        .on_complete([alive, this](std::string body, unsigned status) {
            wxGetApp().CallAfter([alive, this, body, status]() {
                if (!*alive) return;
                if (status != 200 && status != 201 && status != 202) {
                    show_error(wxString::Format("HTTP %u: %s", status, wxString::FromUTF8(body)));
                    return;
                }
                auto j = json::parse(body, nullptr, false);
                if (j.is_discarded()) {
                    show_error(_L("Invalid JSON response from Meshy."));
                    return;
                }
                std::string tid;
                if (j.contains("result") && j["result"].is_string())
                    tid = j["result"].get<std::string>();
                else if (j.contains("id") && j["id"].is_string())
                    tid = j["id"].get<std::string>();
                if (tid.empty()) {
                    show_error(wxString::Format("%s %s", _L("Unexpected response:"), wxString::FromUTF8(body)));
                    return;
                }
                m_task_id = tid;
                set_status(_L("Multi-color task submitted...") + " (" + wxString::FromUTF8(tid) + ")");
                m_poll_timer.Start(POLL_INTERVAL_MS);
            });
        })
        .on_error([alive, this](std::string body, std::string error, unsigned status) {
            wxGetApp().CallAfter([alive, this, body, error, status]() {
                if (!*alive) return;
                show_error(wxString::Format("HTTP %u: %s %s",
                    status, wxString::FromUTF8(error), wxString::FromUTF8(body)));
            });
        })
        .perform();
}

void MeshyDialog::on_discard(wxCommandEvent&)
{
    m_picked_model_url.clear();
    m_picked_ext.clear();
    m_image_task_id.clear();
    m_task_id.clear();
    m_phase = Phase::Image;
    reset_to_idle();
}

void MeshyDialog::reset_to_idle()
{
    m_discard_btn->Hide();
    m_load_btn->Hide();
    m_generate_btn->Show();

    m_preview_image->SetBitmap(wxNullBitmap);
    m_preview_image->Hide();
    m_preview_label->Hide();

    set_progress(0);
    m_status_text->SetForegroundColour(COLOR_STATUS);
    set_status(wxEmptyString);
    set_busy(false);

    Layout();
    Fit();
}

void MeshyDialog::download_and_load(const std::string& url, const std::string& ext)
{
    auto alive = m_alive;
    const std::string id_for_name = m_image_task_id.empty() ? m_task_id : m_image_task_id;
    const auto tmp_path = (fs::temp_directory_path() / ("meshy_" + id_for_name + ext)).string();

    auto http = Http::get(url);
    http.on_complete([alive, this, tmp_path](std::string body, unsigned status) {
        wxGetApp().CallAfter([alive, this, body, tmp_path, status]() {
            if (!*alive) return;
            if (status != 200) {
                show_error(wxString::Format("%s HTTP %u", _L("Model download failed:"), status));
                return;
            }
            try {
                fs::ofstream ofs(fs::path(tmp_path), std::ios::binary);
                ofs.write(body.data(), body.size());
                ofs.close();
            } catch (const std::exception& e) {
                show_error(wxString::Format("%s %s", _L("Failed to save model:"), e.what()));
                return;
            }
            set_progress(100);
            set_status(_L("Loading model into plater..."));
            if (auto* plater = wxGetApp().plater()) {
                std::vector<std::string> files{ tmp_path };
                auto obj_idxs = plater->load_files(files, LoadStrategy::LoadModel, false);
                fit_loaded_objects_to_half_bed(*plater, obj_idxs);
            }
            EndModal(wxID_OK);
        });
    })
    .on_error([alive, this](std::string body, std::string error, unsigned status) {
        wxGetApp().CallAfter([alive, this, body, error, status]() {
            if (!*alive) return;
            show_error(wxString::Format("%s HTTP %u: %s",
                _L("Model download failed:"), status, wxString::FromUTF8(error)));
        });
    })
    .perform();
}

}} // namespace Slic3r::GUI
