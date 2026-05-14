#include "SLAPrinterSettingsDialog.hpp"

#include <wx/choice.h>
#include <wx/stattext.h>
#include <wx/sizer.h>
#include <wx/panel.h>
#include <algorithm>
#include <cmath>
#include <deque>

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "PresetComboBoxes.hpp"
#include "Search.hpp"
#include "Tab.hpp"
#include "Widgets/TextInput.hpp"
#include "Widgets/DialogButtons.hpp"

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"

#include <boost/filesystem.hpp>

namespace Slic3r {
namespace GUI {

namespace {

bool parse_double_ctrl(TextInput *input, const wxString &label, double min_value, double &out, bool show_errors)
{
    wxTextCtrl *ctrl = input->GetTextCtrl();
    wxString value = ctrl->GetValue();
    value.Trim(true).Trim(false);
    if (!value.ToDouble(&out) || out < min_value) {
        if (show_errors) {
            show_error(ctrl->GetParent(), wxString::Format(_L("%s must be a number greater than or equal to %g."), label, min_value));
            ctrl->SetFocus();
            ctrl->SelectAll();
        }
        return false;
    }
    return true;
}

bool parse_int_ctrl(TextInput *input, const wxString &label, int min_value, int &out, bool show_errors)
{
    wxTextCtrl *ctrl = input->GetTextCtrl();
    long parsed = 0;
    wxString value = ctrl->GetValue();
    value.Trim(true).Trim(false);
    if (!value.ToLong(&parsed) || parsed < min_value) {
        if (show_errors) {
            show_error(ctrl->GetParent(), wxString::Format(_L("%s must be an integer greater than or equal to %d."), label, min_value));
            ctrl->SetFocus();
            ctrl->SelectAll();
        }
        return false;
    }
    out = static_cast<int>(parsed);
    return true;
}

wxBoxSizer* make_single_axis_row(wxWindow *parent, const wxString &group_label, const wxString &axis, wxWindow *field, const wxString &unit, ScalableButton *reset_btn = nullptr)
{
    auto *row = new wxBoxSizer(wxHORIZONTAL);
    auto *label = new wxStaticText(parent, wxID_ANY, group_label);
    label->SetMinSize(wxSize(parent->FromDIP(90), -1));
    row->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, parent->FromDIP(10));

    auto *axis_label = new wxStaticText(parent, wxID_ANY, axis);
    axis_label->SetMinSize(wxSize(parent->FromDIP(24), -1));
    row->Add(axis_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, parent->FromDIP(6));
    row->Add(field, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, parent->FromDIP(6));
    if (!unit.empty()) {
        auto *unit_label = new wxStaticText(parent, wxID_ANY, unit);
        row->Add(unit_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, parent->FromDIP(6));
    }
    if (reset_btn)
        row->Add(reset_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, parent->FromDIP(4));
    row->AddStretchSpacer();
    return row;
}

bool text_input_equals_default(TextInput *input, const std::string &key)
{
    return false;
}

int get_int_or_default(const DynamicPrintConfig &config, const std::string &key, int fallback)
{
    if (const auto *opt = config.option<ConfigOptionInt>(key))
        return opt->value;
    return fallback;
}

double get_float_or_default(const DynamicPrintConfig &config, const std::string &key, double fallback)
{
    if (const auto *opt = config.option<ConfigOptionFloat>(key))
        return opt->value;
    return fallback;
}

bool get_bool_or_default(const DynamicPrintConfig &config, const std::string &key, bool fallback)
{
    if (const auto *opt = config.option<ConfigOptionBool>(key))
        return opt->value;
    return fallback;
}

bool printable_area_bounds(const ConfigOptionPoints *area_opt, double &min_x, double &min_y, double &size_x, double &size_y)
{
    if (!area_opt || area_opt->values.empty())
        return false;
    min_x = area_opt->values.front().x();
    min_y = area_opt->values.front().y();
    double max_x = min_x;
    double max_y = min_y;
    for (const auto &p : area_opt->values) {
        min_x = std::min(min_x, p.x());
        min_y = std::min(min_y, p.y());
        max_x = std::max(max_x, p.x());
        max_y = std::max(max_y, p.y());
    }
    size_x = max_x - min_x;
    size_y = max_y - min_y;
    return true;
}

int resolve_profile_int_chain(const PresetCollection &printers, const Preset &profile, const std::string &key, int fallback)
{
    if (const auto *o = profile.config.option<ConfigOptionInt>(key))
        return o->value;

    const Preset *walk = &profile;
    for (int depth = 0; depth < 64 && walk != nullptr; ++depth) {
        if (const auto *o = walk->config.option<ConfigOptionInt>(key))
            return o->value;
        walk = printers.get_preset_parent(*walk);
    }

    if (const Preset *base = printers.get_preset_base(profile)) {
        if (const auto *o = base->config.option<ConfigOptionInt>(key))
            return o->value;
    }

    if (const ConfigOptionDef *def = print_config_def.get(key); def && def->default_value && def->type == coInt)
        return def->default_value->getInt();

    return fallback;
}

double resolve_profile_float_chain(const PresetCollection &printers, const Preset &profile, const std::string &key, double fallback)
{
    if (const auto *o = profile.config.option<ConfigOptionFloat>(key))
        return o->value;

    const Preset *walk = &profile;
    for (int depth = 0; depth < 64 && walk != nullptr; ++depth) {
        if (const auto *o = walk->config.option<ConfigOptionFloat>(key))
            return o->value;
        walk = printers.get_preset_parent(*walk);
    }

    if (const Preset *base = printers.get_preset_base(profile)) {
        if (const auto *o = base->config.option<ConfigOptionFloat>(key))
            return o->value;
    }

    if (const ConfigOptionDef *def = print_config_def.get(key); def && def->default_value && def->type == coFloat)
        return def->default_value->getFloat();

    return fallback;
}

const ConfigOptionPoints *resolve_printable_area_chain(const PresetCollection &printers, const Preset &edited)
{
    const Preset *walk = &edited;
    for (int depth = 0; depth < 64 && walk != nullptr; ++depth) {
        if (const auto *area_opt = walk->config.option<ConfigOptionPoints>("printable_area")) {
            if (!area_opt->values.empty())
                return area_opt;
        }
        walk = printers.get_preset_parent(*walk);
    }

    if (const Preset *base = printers.get_preset_base(edited)) {
        if (const auto *area_opt = base->config.option<ConfigOptionPoints>("printable_area")) {
            if (!area_opt->values.empty())
                return area_opt;
        }
    }

    return nullptr;
}

} // namespace

SLAPrinterSettingsDialog::SLAPrinterSettingsDialog(wxWindow *parent)
    : DPIDialog(parent, wxID_ANY, _L("Printer Settings"), wxDefaultPosition,
                wxSize(45 * wxGetApp().em_unit(), -1), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    SetFont(wxGetApp().normal_font());
    SetBackgroundColour(*wxWHITE);
    build_dialog();
    // Reload user *.json only when the printer preset is clean; otherwise disk is stale vs in-memory
    // edits applied with OK (before Save), and reloading would wipe those values.
    if (wxGetApp().preset_bundle && !wxGetApp().preset_bundle->printers.current_is_dirty())
        sync_selected_user_printer_json_from_disk();
    reload_from_preset();
    refresh_preset_buttons();
    const wxSize dlg_size = FromDIP(wxSize(800, 600));
    SetMinSize(dlg_size);
    SetSize(dlg_size);
    Bind(wxEVT_ACTIVATE, &SLAPrinterSettingsDialog::on_dialog_activated, this);
    CentreOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

Tab* SLAPrinterSettingsDialog::current_printer_tab() const
{
    return wxGetApp().get_tab(Preset::TYPE_PRINTER);
}

void SLAPrinterSettingsDialog::sync_selected_user_printer_json_from_disk()
{
    PresetBundle *bundle = wxGetApp().preset_bundle;
    if (!bundle)
        return;

    PrinterPresetCollection &printers = bundle->printers;
    const size_t sel = printers.get_selected_idx();
    if (sel == static_cast<size_t>(-1) || sel >= printers().size())
        return;

    Preset &stored = printers.preset(sel, true);
    if (!stored.is_user() || stored.file.empty())
        return;

    const boost::filesystem::path fp(stored.file);
    if (!boost::filesystem::exists(fp))
        return;

    const Preset *parent = nullptr;
    if (!stored.inherits().empty())
        parent = printers.find_preset2(stored.inherits(), true);
    if (!parent)
        parent = &printers.default_preset_for(stored.config);

    stored.reload(*parent);
    Preset::normalize(stored.config);

    printers.select_preset(sel);

    if (Tab *tab = wxGetApp().get_tab(Preset::TYPE_PRINTER))
        tab->load_current_preset();
}

void SLAPrinterSettingsDialog::build_dialog()
{
    auto *top_sizer = new wxBoxSizer(wxVERTICAL);
    auto *content = new wxPanel(this, wxID_ANY);
    content->SetBackgroundColour(*wxWHITE);
    auto *content_sizer = new wxBoxSizer(wxVERTICAL);

    auto *top_row = new wxBoxSizer(wxHORIZONTAL);
    m_printer_combo = new PlaterPresetComboBox(content, Preset::TYPE_PRINTER);
    m_printer_combo->set_selection_changed_function([this](int selection) { on_preset_selected(selection); });
    m_printer_combo->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &event) {
        on_preset_selected(event.GetSelection());
        event.Skip();
    });
    top_row->Add(m_printer_combo, 1, wxALIGN_CENTER_VERTICAL);

    m_btn_save = new ScalableButton(content, wxID_ANY, "save");
    m_btn_save->SetToolTip(wxString::Format(_L("Save current %s"), _L("Printer Settings")));
    m_btn_delete = new ScalableButton(content, wxID_ANY, "cross");
    m_btn_delete->SetToolTip(_L("Delete this preset"));
    m_btn_search = new ScalableButton(content, wxID_ANY, "search");
    m_btn_search->SetToolTip(_L("Search in preset"));

    top_row->Add(m_btn_save, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, content->FromDIP(8));
    top_row->Add(m_btn_delete, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, content->FromDIP(8));
    top_row->Add(m_btn_search, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, content->FromDIP(8));
    content_sizer->Add(top_row, 0, wxEXPAND | wxALL, content->FromDIP(12));

    m_search_panel = new wxPanel(content, wxID_ANY);
    m_search_panel->SetBackgroundColour(*wxWHITE);
    auto *search_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_search_input = new TextInput(m_search_panel, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_search_input->SetIcon(*BitmapCache().load_svg("search", this->FromDIP(16), this->FromDIP(16)));
    m_search_input->GetTextCtrl()->SetHint(_L("Search in preset") + dots);
    search_sizer->Add(m_search_input, 1, wxEXPAND);
    m_search_panel->SetSizer(search_sizer);
    m_search_panel->Hide();
    content_sizer->Add(m_search_panel, 0, wxEXPAND | wxLEFT | wxRIGHT, content->FromDIP(12));

    auto *form_panel = new wxPanel(content, wxID_ANY);
    form_panel->SetBackgroundColour(*wxWHITE);
    auto *form_sizer = new wxBoxSizer(wxVERTICAL);

    auto *mirror_row = new wxBoxSizer(wxHORIZONTAL);
    auto *mirror_label = new wxStaticText(form_panel, wxID_ANY, _L("Mirror"));
    mirror_label->SetMinSize(wxSize(form_panel->FromDIP(90), -1));
    mirror_row->Add(mirror_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, form_panel->FromDIP(10));
    m_mirror_choice = new wxChoice(form_panel, wxID_ANY);
    m_mirror_choice->Append("LCD_mirror");
    m_mirror_choice->Append("DLP_mirror");
    mirror_row->Add(m_mirror_choice, 0, wxALIGN_CENTER_VERTICAL);
    mirror_row->AddStretchSpacer();
    form_sizer->Add(mirror_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, form_panel->FromDIP(12));

    m_resolution_x_ctrl = new TextInput(form_panel, "", "");
    m_resolution_y_ctrl = new TextInput(form_panel, "", "");
    m_size_x_ctrl       = new TextInput(form_panel, "", "");
    m_size_y_ctrl       = new TextInput(form_panel, "", "");
    m_size_z_ctrl       = new TextInput(form_panel, "", "");

    const wxSize input_size = wxSize(form_panel->FromDIP(90), -1);
    for (TextInput *ctrl : { m_resolution_x_ctrl, m_resolution_y_ctrl, m_size_x_ctrl, m_size_y_ctrl, m_size_z_ctrl }) {
        ctrl->SetMinSize(input_size);
        ctrl->SetCornerRadius(5.0);
    }

    m_btn_reset_resolution_x = new ScalableButton(form_panel, wxID_ANY, "undo");
    m_btn_reset_resolution_x->SetToolTip(_L("Reset to default"));
    m_btn_reset_resolution_y = new ScalableButton(form_panel, wxID_ANY, "undo");
    m_btn_reset_resolution_y->SetToolTip(_L("Reset to default"));
    m_btn_reset_size_x = new ScalableButton(form_panel, wxID_ANY, "undo");
    m_btn_reset_size_x->SetToolTip(_L("Reset to default"));
    m_btn_reset_size_y = new ScalableButton(form_panel, wxID_ANY, "undo");
    m_btn_reset_size_y->SetToolTip(_L("Reset to default"));
    m_btn_reset_size_z = new ScalableButton(form_panel, wxID_ANY, "undo");
    m_btn_reset_size_z->SetToolTip(_L("Reset to default"));

    form_sizer->Add(make_single_axis_row(form_panel, _L("Resolution"), "X", m_resolution_x_ctrl, "px", m_btn_reset_resolution_x),
                    0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, form_panel->FromDIP(12));
    form_sizer->Add(make_single_axis_row(form_panel, "", "Y", m_resolution_y_ctrl, "px", m_btn_reset_resolution_y),
                    0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, form_panel->FromDIP(12));

    form_sizer->Add(make_single_axis_row(form_panel, _L("Size"), "X", m_size_x_ctrl, "mm", m_btn_reset_size_x),
                    0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, form_panel->FromDIP(12));
    form_sizer->Add(make_single_axis_row(form_panel, "", "Y", m_size_y_ctrl, "mm", m_btn_reset_size_y),
                    0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, form_panel->FromDIP(12));
    form_sizer->Add(make_single_axis_row(form_panel, "", "Z", m_size_z_ctrl, "mm", m_btn_reset_size_z),
                    0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, form_panel->FromDIP(12));

    form_panel->SetSizer(form_sizer);
    content_sizer->Add(form_panel, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, content->FromDIP(12));

    content->SetSizer(content_sizer);
    top_sizer->Add(content, 1, wxEXPAND);

    auto *dlg_btns = new DialogButtons(this, { "ok", "cancel" }, "ok");
    if (auto *btn_ok = dlg_btns->GetOK()) {
        btn_ok->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
            if (!sync_local_to_tab(true))
                return;
            EndModal(wxID_OK);
        });
    }
    if (auto *btn_cancel = dlg_btns->GetCANCEL())
        btn_cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { EndModal(wxID_CANCEL); });

    top_sizer->Add(dlg_btns, 0, wxEXPAND);
    SetSizerAndFit(top_sizer);

    m_search_input->Bind(wxCUSTOMEVT_EXIT_SEARCH, [this](wxCommandEvent &) {
        m_search_panel->Hide();
        Layout();
    });

    m_btn_save->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        if (!sync_local_to_tab(true))
            return;
        if (auto *tab = current_printer_tab())
            tab->save_preset();
        wxGetApp().sidebar().update_presets(Preset::TYPE_PRINTER);
        reload_from_preset();
    });

    m_btn_delete->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        if (!sync_local_to_tab(true))
            return;
        if (auto *tab = current_printer_tab())
            tab->delete_preset();
        wxGetApp().sidebar().update_presets(Preset::TYPE_PRINTER);
        m_printer_combo->update_from_bundle();
        reload_from_preset();
    });

    m_btn_search->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        if (!sync_local_to_tab(true))
            return;
        m_search_panel->Show();
        Layout();
        wxGetApp().plater()->search(false, Preset::TYPE_PRINTER, this, m_search_input, m_btn_search);
    });

    m_btn_reset_resolution_x->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { reset_field_to_default(m_resolution_x_ctrl, "display_pixels_x"); });
    m_btn_reset_resolution_y->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { reset_field_to_default(m_resolution_y_ctrl, "display_pixels_y"); });
    m_btn_reset_size_x->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { reset_field_to_default(m_size_x_ctrl, "printable_area_x"); });
    m_btn_reset_size_y->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { reset_field_to_default(m_size_y_ctrl, "printable_area_y"); });
    m_btn_reset_size_z->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { reset_field_to_default(m_size_z_ctrl, "printable_height"); });

    m_resolution_x_ctrl->GetTextCtrl()->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { refresh_reset_buttons(); });
    m_resolution_y_ctrl->GetTextCtrl()->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { refresh_reset_buttons(); });
    m_size_x_ctrl->GetTextCtrl()->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { refresh_reset_buttons(); });
    m_size_y_ctrl->GetTextCtrl()->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { refresh_reset_buttons(); });
    m_size_z_ctrl->GetTextCtrl()->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { refresh_reset_buttons(); });
}

void SLAPrinterSettingsDialog::reload_from_preset()
{
    PresetBundle *bundle = wxGetApp().preset_bundle;
    if (!bundle)
        return;

    PresetCollection &printers = bundle->printers;
    // Same source as Printer tab / sidebar: edited preset holds the active config after selection.
    const Preset &edited            = printers.get_edited_preset();
    const DynamicPrintConfig &config = edited.config;

    m_mirror_choice->SetSelection(mirror_mode_from_config(config) == MirrorMode::LCD ? 0 : 1);

    const int px_x = resolve_profile_int_chain(printers, edited, "display_pixels_x", 2560);
    const int px_y = resolve_profile_int_chain(printers, edited, "display_pixels_y", 1440);
    m_resolution_x_ctrl->GetTextCtrl()->SetValue(wxString::Format("%d", px_x));
    m_resolution_y_ctrl->GetTextCtrl()->SetValue(wxString::Format("%d", px_y));

    double printable_x = 120.0, printable_y = 68.0;
    if (const auto *area_opt = config.option<ConfigOptionPoints>("printable_area")) {
        double min_x = 0.0, min_y = 0.0;
        printable_area_bounds(area_opt, min_x, min_y, printable_x, printable_y);
    } else if (const ConfigOptionPoints *chain_area = resolve_printable_area_chain(printers, edited)) {
        double min_x = 0.0, min_y = 0.0;
        printable_area_bounds(chain_area, min_x, min_y, printable_x, printable_y);
    }

    double height_z = get_float_or_default(config, "printable_height", 150.0);
    if (!config.option<ConfigOptionFloat>("printable_height")) {
        const Preset *walk = &edited;
        for (int depth = 0; depth < 64 && walk != nullptr; ++depth) {
            if (const auto *h = walk->config.option<ConfigOptionFloat>("printable_height")) {
                height_z = h->value;
                break;
            }
            walk = printers.get_preset_parent(*walk);
        }
    }

    m_size_x_ctrl->GetTextCtrl()->SetValue(wxString::Format("%g", printable_x));
    m_size_y_ctrl->GetTextCtrl()->SetValue(wxString::Format("%g", printable_y));
    m_size_z_ctrl->GetTextCtrl()->SetValue(wxString::Format("%g", height_z));

    if (m_printer_combo)
        m_printer_combo->update();

    refresh_preset_buttons();
    Layout();
}

void SLAPrinterSettingsDialog::refresh_preset_buttons()
{
    PresetBundle *bundle = wxGetApp().preset_bundle;
    if (!bundle || !m_btn_delete)
        return;
    const Preset &preset = bundle->printers.get_edited_preset();
    m_btn_delete->Show(!preset.is_default && !preset.is_system);
    refresh_reset_buttons();
    if (GetSizer())
        GetSizer()->Layout();
}

void SLAPrinterSettingsDialog::on_preset_selected(int selection)
{
    if (m_is_handling_preset_selection)
        return;
    m_is_handling_preset_selection = true;

    auto reset_guard = [this]() { m_is_handling_preset_selection = false; };

    if (!m_printer_combo || selection < 0 || selection >= static_cast<int>(m_printer_combo->GetCount()))
    {
        reset_guard();
        return;
    }

    auto selected_label = m_printer_combo->GetString(selection).ToUTF8().data();
    auto *bundle = wxGetApp().preset_bundle;
    if (!bundle)
    {
        reset_guard();
        return;
    }

    auto &printers = bundle->printers;
    std::string selected = Preset::remove_suffix_modified(printers.get_preset_name_by_alias(selected_label));
    if (selected.empty())
        selected = Preset::remove_suffix_modified(selected_label);
    if (selected.empty())
    {
        reset_guard();
        return;
    }

    // Non-strict select_preset_by_name() falls back to the first visible preset when the name is
    // missing (see Preset.cpp). Resolve by exact name + visibility instead.
    {
        const std::deque<Preset> &all = printers();
        bool                        found = false;
        size_t                      idx   = 0;
        for (size_t i = 0; i < all.size(); ++i) {
            if (all[i].name == selected && all[i].is_visible) {
                idx   = i;
                found = true;
                break;
            }
        }
        if (!found) {
            reload_from_preset();
            reset_guard();
            return;
        }
        printers.select_preset(idx);
    }

    if (auto *tab = current_printer_tab()) {
        // Force switch to ensure combobox selection always takes effect in dialog.
        if (!tab->select_preset(selected, false, "", true)) {
            reload_from_preset();
            reset_guard();
            return;
        }
    }

    sync_selected_user_printer_json_from_disk();

    if (m_printer_combo)
        m_printer_combo->update();
    wxGetApp().sidebar().update_presets(Preset::TYPE_PRINTER);
    wxGetApp().sidebar().update_all_preset_comboboxes();
    reload_from_preset();
    reset_guard();
}

void SLAPrinterSettingsDialog::on_dialog_activated(wxActivateEvent &event)
{
    if (event.GetActive()) {
        if (wxGetApp().get_ui_printer_technology() != ptSLA) {
            EndModal(wxID_CANCEL);
            return;
        }
        reload_from_preset();
    }
    event.Skip();
}

bool SLAPrinterSettingsDialog::try_focus_printer_search_result(const std::string &opt_key)
{
    ::TextInput *ti = nullptr;
    if (opt_key == "display_pixels_x")
        ti = m_resolution_x_ctrl;
    else if (opt_key == "display_pixels_y")
        ti = m_resolution_y_ctrl;
    else if (opt_key == "printable_height")
        ti = m_size_z_ctrl;
    else if (opt_key == "printable_area")
        ti = m_size_x_ctrl;
    else if (opt_key == "display_width")
        ti = m_size_x_ctrl;
    else if (opt_key == "display_height")
        ti = m_size_y_ctrl;
    else if (opt_key == "display_mirror_x" || opt_key == "display_mirror_y" || opt_key == "display_orientation") {
        if (m_mirror_choice) {
            m_mirror_choice->SetFocus();
            return true;
        }
        return false;
    }

    if (!ti)
        return false;

    wxTextCtrl *tc = ti->GetTextCtrl();
    if (!tc)
        return false;
    tc->SetFocus();
#ifdef WIN32
    tc->SetSelection(-1, -1);
#endif
    return true;
}

void SLAPrinterSettingsDialog::refresh_reset_buttons()
{
    if (m_btn_reset_resolution_x)
    {
        int def = 2560;
        get_default_int_for_key("display_pixels_x", def);
        long cur = 0;
        m_resolution_x_ctrl->GetTextCtrl()->GetValue().ToLong(&cur);
        m_btn_reset_resolution_x->Show(cur != def);
    }
    if (m_btn_reset_resolution_y)
    {
        int def = 1440;
        get_default_int_for_key("display_pixels_y", def);
        long cur = 0;
        m_resolution_y_ctrl->GetTextCtrl()->GetValue().ToLong(&cur);
        m_btn_reset_resolution_y->Show(cur != def);
    }
    if (m_btn_reset_size_x)
    {
        double def_x = 120.0, def_y = 68.0;
        get_default_printable_size_xy(def_x, def_y);
        double cur = 0.0;
        m_size_x_ctrl->GetTextCtrl()->GetValue().ToDouble(&cur);
        m_btn_reset_size_x->Show(std::abs(cur - def_x) > 1e-6);
    }
    if (m_btn_reset_size_y)
    {
        double def_x = 120.0, def_y = 68.0;
        get_default_printable_size_xy(def_x, def_y);
        double cur = 0.0;
        m_size_y_ctrl->GetTextCtrl()->GetValue().ToDouble(&cur);
        m_btn_reset_size_y->Show(std::abs(cur - def_y) > 1e-6);
    }
    if (m_btn_reset_size_z)
    {
        double def = 150.0;
        get_default_float_for_key("printable_height", def);
        double cur = 0.0;
        m_size_z_ctrl->GetTextCtrl()->GetValue().ToDouble(&cur);
        m_btn_reset_size_z->Show(std::abs(cur - def) > 1e-6);
    }
    Layout();
}

void SLAPrinterSettingsDialog::reset_field_to_default(TextInput *input, const std::string &key)
{
    if (key == "printable_area_x" || key == "printable_area_y") {
        double def_x = 120.0, def_y = 68.0;
        get_default_printable_size_xy(def_x, def_y);
        def_x = std::max(0.001, def_x);
        def_y = std::max(0.001, def_y);
        input->GetTextCtrl()->SetValue(wxString::Format("%g", key == "printable_area_x" ? def_x : def_y));
    } else {
        int i = 0;
        double f = 0.0;
        if (get_default_int_for_key(key, i))
            input->GetTextCtrl()->SetValue(wxString::Format("%d", i));
        else if (get_default_float_for_key(key, f))
            input->GetTextCtrl()->SetValue(wxString::Format("%g", f));
    }
    sync_local_to_tab(true);
    refresh_reset_buttons();
}

bool SLAPrinterSettingsDialog::sync_local_to_tab(bool show_errors)
{
    if (wxGetApp().get_ui_printer_technology() != ptSLA)
        return false;

    auto *tab = current_printer_tab();
    if (!tab || !tab->get_config())
        return false;

    int    pixels_x = 0;
    int    pixels_y = 0;
    double size_x   = 0.0;
    double size_y   = 0.0;
    double size_z   = 0.0;

    if (!parse_int_ctrl(m_resolution_x_ctrl, _L("Resolution X"), 1, pixels_x, show_errors)) return false;
    if (!parse_int_ctrl(m_resolution_y_ctrl, _L("Resolution Y"), 1, pixels_y, show_errors)) return false;
    if (!parse_double_ctrl(m_size_x_ctrl, _L("Size X"), 0.001, size_x, show_errors)) return false;
    if (!parse_double_ctrl(m_size_y_ctrl, _L("Size Y"), 0.001, size_y, show_errors)) return false;
    if (!parse_double_ctrl(m_size_z_ctrl, _L("Size Z"), 0.001, size_z, show_errors)) return false;

    DynamicPrintConfig cfg = *tab->get_config();
    cfg.set_key_value("display_pixels_x", new ConfigOptionInt(pixels_x));
    cfg.set_key_value("display_pixels_y", new ConfigOptionInt(pixels_y));
    // Size X/Y are mapped to printable_area dimensions while preserving origin.
    double min_x = 0.0, min_y = 0.0, curr_x = 0.0, curr_y = 0.0;
    if (const auto *area_opt = cfg.option<ConfigOptionPoints>("printable_area"))
        printable_area_bounds(area_opt, min_x, min_y, curr_x, curr_y);
    std::vector<Vec2d> points = {
        Vec2d(min_x,          min_y),
        Vec2d(min_x + size_x, min_y),
        Vec2d(min_x + size_x, min_y + size_y),
        Vec2d(min_x,          min_y + size_y)
    };
    cfg.set_key_value("printable_area",  new ConfigOptionPoints(points));
    cfg.set_key_value("display_width",   new ConfigOptionFloat(size_x));
    cfg.set_key_value("display_height",  new ConfigOptionFloat(size_y));
    cfg.set_key_value("printable_height", new ConfigOptionFloat(size_z));
    apply_mirror_mode(cfg, m_mirror_choice->GetSelection() == 1 ? MirrorMode::DLP : MirrorMode::LCD);

    tab->load_config(cfg);
    tab->update_tab_ui(true);
    tab->update_dirty();
    wxGetApp().sidebar().update_presets(Preset::TYPE_PRINTER);
    return true;
}

bool SLAPrinterSettingsDialog::get_default_int_for_key(const std::string &key, int &out) const
{
    // Only keys that are actually coInt in PrintConfig (e.g. not printable_height).
    const ConfigOptionDef *def = print_config_def.get(key);
    if (!def || def->type != coInt)
        return false;

    // Reset targets the saved profile (merged user JSON + inherits), not the system base preset only.
    PresetBundle *bundle = wxGetApp().preset_bundle;
    if (!bundle)
        return false;
    PresetCollection       &printers = bundle->printers;
    const Preset           &profile  = printers.get_selected_preset();
    int                     fb       = 0;
    if (def->default_value)
        fb = def->default_value->getInt();
    out = resolve_profile_int_chain(printers, profile, key, fb);
    return true;
}

bool SLAPrinterSettingsDialog::get_default_float_for_key(const std::string &key, double &out) const
{
    const ConfigOptionDef *def = print_config_def.get(key);
    if (!def || def->type != coFloat)
        return false;

    PresetBundle *bundle = wxGetApp().preset_bundle;
    if (!bundle)
        return false;
    PresetCollection       &printers = bundle->printers;
    const Preset           &profile  = printers.get_selected_preset();
    double                  fb       = 0.0;
    if (def->default_value)
        fb = def->default_value->getFloat();
    out = resolve_profile_float_chain(printers, profile, key, fb);
    // Dialog validation requires Size Z >= 0.001; bad or placeholder JSON may store 0.
    if (key == "printable_height" && out < 0.001)
        out = std::max(0.001, fb >= 0.001 ? fb : 0.001);
    return true;
}

bool SLAPrinterSettingsDialog::get_default_printable_size_xy(double &x, double &y) const
{
    PresetBundle *bundle = wxGetApp().preset_bundle;
    if (!bundle)
        return false;
    PresetCollection &printers = bundle->printers;
    const Preset     &profile  = printers.get_selected_preset();

    if (const auto *area_opt = profile.config.option<ConfigOptionPoints>("printable_area")) {
        double min_x = 0.0, min_y = 0.0;
        if (printable_area_bounds(area_opt, min_x, min_y, x, y))
            return true;
    }
    if (const ConfigOptionPoints *chain_area = resolve_printable_area_chain(printers, profile)) {
        double min_x = 0.0, min_y = 0.0;
        if (printable_area_bounds(chain_area, min_x, min_y, x, y))
            return true;
    }
    return false;
}

bool SLAPrinterSettingsDialog::get_current_printable_size_xy(double &x, double &y) const
{
    if (auto *tab = current_printer_tab()) {
        if (tab->get_config()) {
            if (const auto *area_opt = tab->get_config()->option<ConfigOptionPoints>("printable_area")) {
                double min_x = 0.0, min_y = 0.0;
                if (printable_area_bounds(area_opt, min_x, min_y, x, y))
                    return true;
            }
        }
    }
    return false;
}

void SLAPrinterSettingsDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    if (m_printer_combo)
        m_printer_combo->msw_rescale();
    if (m_btn_save)
        m_btn_save->msw_rescale();
    if (m_btn_delete)
        m_btn_delete->msw_rescale();
    if (m_btn_search)
        m_btn_search->msw_rescale();
    if (m_btn_reset_resolution_x)
        m_btn_reset_resolution_x->msw_rescale();
    if (m_btn_reset_resolution_y)
        m_btn_reset_resolution_y->msw_rescale();
    if (m_btn_reset_size_x)
        m_btn_reset_size_x->msw_rescale();
    if (m_btn_reset_size_y)
        m_btn_reset_size_y->msw_rescale();
    if (m_btn_reset_size_z)
        m_btn_reset_size_z->msw_rescale();

    const wxSize dlg_size = FromDIP(wxSize(800, 600));
    SetMinSize(dlg_size);
    SetSize(dlg_size);
    Layout();
    Refresh();
}

SLAPrinterSettingsDialog::MirrorMode SLAPrinterSettingsDialog::mirror_mode_from_config(const DynamicPrintConfig &config)
{
    const bool mirror_x = get_bool_or_default(config, "display_mirror_x", true);
    const bool mirror_y = get_bool_or_default(config, "display_mirror_y", false);
    return (!mirror_x && mirror_y) ? MirrorMode::DLP : MirrorMode::LCD;
}

void SLAPrinterSettingsDialog::apply_mirror_mode(DynamicPrintConfig &config, MirrorMode mode)
{
    const bool is_lcd = mode == MirrorMode::LCD;
    config.set_key_value("display_mirror_x", new ConfigOptionBool(is_lcd));
    config.set_key_value("display_mirror_y", new ConfigOptionBool(!is_lcd));
}

} // namespace GUI
} // namespace Slic3r
