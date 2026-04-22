#include "MeshyDialog.hpp"

#include <algorithm>
#include <iterator>

#include <wx/app.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/filepicker.h>
#include <wx/gauge.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/settings.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

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

namespace fs = boost::filesystem;
using json = nlohmann::json;

namespace Slic3r { namespace GUI {

static const char* MESHY_API_BASE       = "https://api.meshy.ai";
static const char* MESHY_IMG_TO_3D_PATH = "/openapi/v1/image-to-3d";
static const char* CFG_SECTION          = "meshy";
static const int   POLL_INTERVAL_MS     = 4000;

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

// --- Tiny helpers to keep the ctor readable ---------------------------------

static wxStaticText* bold_label(wxWindow* p, const wxString& text)
{
    auto* t = new wxStaticText(p, wxID_ANY, text);
    auto f = t->GetFont();
    f.MakeBold();
    t->SetFont(f);
    return t;
}

static void add_row(wxFlexGridSizer* grid, wxWindow* parent, const wxString& label, wxWindow* ctrl)
{
    grid->Add(new wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(ctrl, 1, wxEXPAND);
}

// ---------------------------------------------------------------------------

MeshyDialog::MeshyDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, _L("Generate 3D model from image (Meshy)"),
               wxDefaultPosition, wxSize(640, 640),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_poll_timer(this)
    , m_alive(std::make_shared<std::atomic<bool>>(true))
{
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    const int pad = FromDIP(10);
    const int gap = FromDIP(6);

    auto* outer = new wxBoxSizer(wxVERTICAL);

    // Title -----------------------------------------------------------------
    auto* title = new wxStaticText(this, wxID_ANY, _L("Image to 3D (Meshy)"));
    {
        auto f = title->GetFont();
        f.MakeBold();
        f.SetPointSize(f.GetPointSize() + 4);
        title->SetFont(f);
    }
    outer->Add(title, 0, wxALL, pad);
    outer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, pad);

    // Input box -------------------------------------------------------------
    auto* input_box = new wxStaticBoxSizer(wxVERTICAL, this, _L("Input"));
    auto* input_parent = input_box->GetStaticBox();
    auto* input_grid = new wxFlexGridSizer(2, gap, gap);
    input_grid->AddGrowableCol(1, 1);

    m_api_key_ctrl = new wxTextCtrl(input_parent, wxID_ANY, wxEmptyString,
                                    wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
    m_image_picker = new wxFilePickerCtrl(input_parent, wxID_ANY, wxEmptyString,
                                          _L("Select image"),
                                          "Images (*.png;*.jpg;*.jpeg)|*.png;*.jpg;*.jpeg",
                                          wxDefaultPosition, wxDefaultSize,
                                          wxFLP_OPEN | wxFLP_FILE_MUST_EXIST);

    add_row(input_grid, input_parent, _L("API key"),        m_api_key_ctrl);
    add_row(input_grid, input_parent, _L("Input image"),    m_image_picker);
    input_box->Add(input_grid, 0, wxEXPAND | wxALL, gap);
    outer->Add(input_box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);

    // Model settings --------------------------------------------------------
    auto* model_box = new wxStaticBoxSizer(wxVERTICAL, this, _L("Model settings"));
    auto* model_parent = model_box->GetStaticBox();
    auto* model_grid = new wxFlexGridSizer(2, gap, gap);
    model_grid->AddGrowableCol(1, 1);

    wxArrayString ai_models;
    ai_models.Add("latest"); ai_models.Add("meshy-6"); ai_models.Add("meshy-5");
    m_ai_model = new wxChoice(model_parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, ai_models);
    m_ai_model->SetSelection(0);

    wxArrayString mtypes;
    mtypes.Add("standard"); mtypes.Add("lowpoly");
    m_model_type = new wxChoice(model_parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, mtypes);
    m_model_type->SetSelection(0);

    wxArrayString topos;
    topos.Add("triangle"); topos.Add("quad");
    m_topology = new wxChoice(model_parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, topos);
    m_topology->SetSelection(0);

    m_polycount = new wxSpinCtrl(model_parent, wxID_ANY, "30000",
                                 wxDefaultPosition, wxDefaultSize,
                                 wxSP_ARROW_KEYS, 100, 300000, 30000);

    wxArrayString syms;
    syms.Add("auto"); syms.Add("off"); syms.Add("on");
    m_symmetry = new wxChoice(model_parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, syms);
    m_symmetry->SetSelection(0);

    wxArrayString poses;
    poses.Add(_L("(default)")); poses.Add("a-pose"); poses.Add("t-pose");
    m_pose_mode = new wxChoice(model_parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, poses);
    m_pose_mode->SetSelection(0);

    m_should_remesh = new wxCheckBox(model_parent, wxID_ANY, _L("Enable remeshing"));
    m_should_remesh->SetValue(true);

    add_row(model_grid, model_parent, _L("AI model"),        m_ai_model);
    add_row(model_grid, model_parent, _L("Mesh type"),       m_model_type);
    add_row(model_grid, model_parent, _L("Topology"),        m_topology);
    add_row(model_grid, model_parent, _L("Target polycount"),m_polycount);
    add_row(model_grid, model_parent, _L("Symmetry"),        m_symmetry);
    add_row(model_grid, model_parent, _L("Pose"),            m_pose_mode);

    model_box->Add(model_grid, 0, wxEXPAND | wxALL, gap);
    model_box->Add(m_should_remesh, 0, wxLEFT | wxBOTTOM, gap);
    outer->Add(model_box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);

    // Texture settings ------------------------------------------------------
    auto* tex_box = new wxStaticBoxSizer(wxVERTICAL, this, _L("Texture settings"));
    auto* tex_parent = tex_box->GetStaticBox();

    m_should_texture = new wxCheckBox(tex_parent, wxID_ANY, _L("Generate texture"));
    m_should_texture->SetValue(true);
    m_enable_pbr     = new wxCheckBox(tex_parent, wxID_ANY, _L("PBR maps"));
    m_hd_texture     = new wxCheckBox(tex_parent, wxID_ANY, _L("HD (4K) base color"));

    auto* tex_checks = new wxBoxSizer(wxHORIZONTAL);
    tex_checks->Add(m_should_texture, 0, wxRIGHT, pad);
    tex_checks->Add(m_enable_pbr,     0, wxRIGHT, pad);
    tex_checks->Add(m_hd_texture,     0);
    tex_box->Add(tex_checks, 0, wxALL, gap);

    auto* tex_grid = new wxFlexGridSizer(2, gap, gap);
    tex_grid->AddGrowableCol(1, 1);
    m_texture_prompt = new wxTextCtrl(tex_parent, wxID_ANY, wxEmptyString);
    add_row(tex_grid, tex_parent, _L("Texture prompt"), m_texture_prompt);
    tex_box->Add(tex_grid, 0, wxEXPAND | wxALL, gap);

    outer->Add(tex_box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);

    // Progress --------------------------------------------------------------
    auto* progress_box = new wxBoxSizer(wxVERTICAL);
    m_progress    = new wxGauge(this, wxID_ANY, 100);
    m_status_text = new wxStaticText(this, wxID_ANY, wxEmptyString);
    progress_box->Add(m_progress,    0, wxEXPAND | wxBOTTOM, gap);
    progress_box->Add(m_status_text, 0, wxEXPAND);
    outer->Add(progress_box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);

    // Buttons ---------------------------------------------------------------
    auto* btn_row = new wxBoxSizer(wxHORIZONTAL);
    m_cancel_btn   = new wxButton(this, wxID_CANCEL, _L("Close"));
    m_generate_btn = new wxButton(this, wxID_ANY,    _L("Generate"));
    btn_row->AddStretchSpacer(1);
    btn_row->Add(m_cancel_btn,   0, wxRIGHT, gap);
    btn_row->Add(m_generate_btn, 0);
    outer->Add(btn_row, 0, wxEXPAND | wxALL, pad);

    SetSizer(outer);
    outer->Fit(this);
    SetMinSize(GetSize());
    CentreOnParent();

    load_saved_settings();

    m_generate_btn->Bind(wxEVT_BUTTON, &MeshyDialog::on_generate, this);
    m_should_texture->Bind(wxEVT_CHECKBOX, &MeshyDialog::on_texture_toggle, this);
    Bind(wxEVT_TIMER, &MeshyDialog::on_poll_timer, this, m_poll_timer.GetId());

    const bool tx = m_should_texture->GetValue();
    m_enable_pbr->Enable(tx);
    m_hd_texture->Enable(tx);
    m_texture_prompt->Enable(tx);
}

MeshyDialog::~MeshyDialog()
{
    *m_alive = false;
    if (m_poll_timer.IsRunning())
        m_poll_timer.Stop();
}

void MeshyDialog::load_saved_settings()
{
    auto* cfg = wxGetApp().app_config;
    if (!cfg) return;
    auto key = cfg->get(CFG_SECTION, "api_key");
    if (!key.empty()) m_api_key_ctrl->SetValue(key);

    auto set_choice = [](wxChoice* c, const std::string& v) {
        if (v.empty()) return;
        int i = c->FindString(v);
        if (i != wxNOT_FOUND) c->SetSelection(i);
    };
    set_choice(m_ai_model,   cfg->get(CFG_SECTION, "ai_model"));
    set_choice(m_model_type, cfg->get(CFG_SECTION, "model_type"));
    set_choice(m_topology,   cfg->get(CFG_SECTION, "topology"));
    set_choice(m_symmetry,   cfg->get(CFG_SECTION, "symmetry"));

    auto pc = cfg->get(CFG_SECTION, "polycount");
    if (!pc.empty()) { try { m_polycount->SetValue(std::stoi(pc)); } catch (...) {} }

    auto remesh = cfg->get(CFG_SECTION, "should_remesh");
    if (!remesh.empty()) m_should_remesh->SetValue(remesh == "1" || remesh == "true");
    auto tex = cfg->get(CFG_SECTION, "should_texture");
    if (!tex.empty()) m_should_texture->SetValue(tex == "1" || tex == "true");
    auto pbr = cfg->get(CFG_SECTION, "enable_pbr");
    if (!pbr.empty()) m_enable_pbr->SetValue(pbr == "1" || pbr == "true");
    auto hd = cfg->get(CFG_SECTION, "hd_texture");
    if (!hd.empty()) m_hd_texture->SetValue(hd == "1" || hd == "true");
}

void MeshyDialog::save_current_settings()
{
    auto* cfg = wxGetApp().app_config;
    if (!cfg) return;
    cfg->set(CFG_SECTION, "api_key",        m_api_key_ctrl->GetValue().ToStdString());
    cfg->set(CFG_SECTION, "ai_model",       m_ai_model->GetStringSelection().ToStdString());
    cfg->set(CFG_SECTION, "model_type",     m_model_type->GetStringSelection().ToStdString());
    cfg->set(CFG_SECTION, "topology",       m_topology->GetStringSelection().ToStdString());
    cfg->set(CFG_SECTION, "symmetry",       m_symmetry->GetStringSelection().ToStdString());
    cfg->set(CFG_SECTION, "polycount",      std::to_string(m_polycount->GetValue()));
    cfg->set(CFG_SECTION, "should_remesh",  m_should_remesh->GetValue() ? "1" : "0");
    cfg->set(CFG_SECTION, "should_texture", m_should_texture->GetValue() ? "1" : "0");
    cfg->set(CFG_SECTION, "enable_pbr",     m_enable_pbr->GetValue() ? "1" : "0");
    cfg->set(CFG_SECTION, "hd_texture",     m_hd_texture->GetValue() ? "1" : "0");
    cfg->save();
}

void MeshyDialog::on_texture_toggle(wxCommandEvent&)
{
    const bool tx = m_should_texture->GetValue();
    m_enable_pbr->Enable(tx);
    m_hd_texture->Enable(tx);
    m_texture_prompt->Enable(tx);
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
    m_image_picker->Enable(!busy);
    m_ai_model->Enable(!busy);
    m_model_type->Enable(!busy);
    m_topology->Enable(!busy);
    m_polycount->Enable(!busy);
    m_symmetry->Enable(!busy);
    m_pose_mode->Enable(!busy);
    m_should_remesh->Enable(!busy);
    m_should_texture->Enable(!busy);
    const bool tx = !busy && m_should_texture->GetValue();
    m_enable_pbr->Enable(tx);
    m_hd_texture->Enable(tx);
    m_texture_prompt->Enable(tx);
}

void MeshyDialog::show_error(const wxString& msg)
{
    set_busy(false);
    if (m_poll_timer.IsRunning())
        m_poll_timer.Stop();
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
    auto api_key    = m_api_key_ctrl->GetValue().ToStdString();
    auto image_path = m_image_picker->GetPath().ToStdString();

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

    json body = {
        {"image_url",      data_uri},
        {"ai_model",       m_ai_model->GetStringSelection().ToStdString()},
        {"model_type",     m_model_type->GetStringSelection().ToStdString()},
        {"topology",       m_topology->GetStringSelection().ToStdString()},
        {"target_polycount", m_polycount->GetValue()},
        {"symmetry_mode",  m_symmetry->GetStringSelection().ToStdString()},
        {"should_remesh",  m_should_remesh->GetValue()},
        {"should_texture", m_should_texture->GetValue()},
    };
    if (m_should_texture->GetValue()) {
        body["enable_pbr"] = m_enable_pbr->GetValue();
        body["hd_texture"] = m_hd_texture->GetValue();
        auto prompt = m_texture_prompt->GetValue().ToStdString();
        if (!prompt.empty()) body["texture_prompt"] = prompt;
    }
    // pose_mode: skip default "(default)" choice
    if (m_pose_mode->GetSelection() > 0)
        body["pose_mode"] = m_pose_mode->GetStringSelection().ToStdString();

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

    auto alive = m_alive;
    const std::string url = std::string(MESHY_API_BASE) + MESHY_IMG_TO_3D_PATH + "/" + m_task_id;

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
                set_status(wxString::FromUTF8(task_status) + wxString::Format(" (%d%%)", progress));

                if (task_status == "SUCCEEDED") {
                    m_poll_timer.Stop();
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
                    if (picked_url.empty()) {
                        show_error(_L("No downloadable model URL in response."));
                        return;
                    }
                    set_status(_L("Downloading model..."));
                    download_and_load(picked_url, picked_ext);
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

void MeshyDialog::download_and_load(const std::string& url, const std::string& ext)
{
    auto alive = m_alive;
    const auto tmp_path = (fs::temp_directory_path() / ("meshy_" + m_task_id + ext)).string();

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
