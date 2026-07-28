///|/ Copyright (c) Prusa Research 2019 - 2023 Enrico Turri @enricoturri1966, Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv, Tomáš Mészáros @tamasmeszaros, Filip Sykala @Jony01, Lukáš Hejl @hejllukas, Oleksandra Iushchenko @YuSanka, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2019 BeldrothTheGold @BeldrothTheGold
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
// Step 4.3: GLGizmoHollow refactored to inherit GLGizmoSlaBase.
// Key changes from PhrozenOrca's old GLGizmoBase version:
// - Base class: GLGizmoBase → GLGizmoSlaBase (slaposAssembly)
// - set_sla_support_data() → data_changed() + set_hide_full_scene(true)
// - GLModel m_cylinder → PickingModel m_cylinder (adds mesh_raycaster)
// - unproject_on_mesh() removed (now in GLGizmoSlaBase)
// - hollow_mesh() removed (use reslice_until_step(slaposDrillHoles))
// - on_get_requirements() removed (inherited from GLGizmoSlaBase)
// - HollowedMesh requirement removed (not needed after refactor)
// - PhrozenOrca: model_instance() replaced with model_object()+get_active_instance()
// - PhrozenOrca: no set_use_shift() API → omitted
// - PhrozenOrca: m_imgui->xxx() ImGui style preserved (not ImGuiPureWrap::)

#include "GLGizmoHollow.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosCommon.hpp"

#include <GL/glew.h>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectSettings.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/SLAPrint.hpp"

#include "libslic3r/Model.hpp"


namespace Slic3r {
namespace GUI {

// Step 4.7: Use no-step constructor (m_min_sla_print_object_step = -1).
// PrusaSlicer uses slaposAssembly — an always-complete early init step — so m_input_enabled
// is true as soon as the object is loaded, and the gizmo is immediately interactive.
// PhrozenOrca has no equivalent "always-done" early step. slaposSliceSupports (the previous choice)
// is the LAST step in the pipeline, so it was incorrectly keeping the gizmo disabled (gray model)
// until the full SLA pipeline completed. Using no-step (-1) makes m_input_enabled = true whenever
// a mesh is available, matching PrusaSlicer's behavior: model displays in normal color immediately.
GLGizmoHollow::GLGizmoHollow(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoSlaBase(parent, icon_filename, sprite_id)  // no minimum step — always enable input when mesh exists
{
}


bool GLGizmoHollow::on_init()
{
    m_shortcut_key = WXK_CONTROL_H;
    // offset / quality / closing_distance labels are set from config defs inside render_hollow_panel
    m_desc["hp_hollow"] = _(L("Hollow"));
    m_desc["hp_remove"] = _(L("Remove"));

    return true;
}

// Step 4.3: Replaces set_sla_support_data(). Called by framework when selection changes.
// Key addition: set_hide_full_scene(true) hides the model so render_volumes() can render instead.
// Step 4.7: Guarded reslice — only trigger when required_step >= 0. With no-step constructor
// (required_step = -1), automatic reslicing is skipped; update_volumes() uses get_mesh_to_print()
// (hollowed mesh if slaposHollowing is done) or the Selection fallback path (raw model mesh).
// The user explicitly triggers hollowing preview via the "Preview hollowed model" button.
void GLGizmoHollow::data_changed(bool is_serializing)
{
    if (!m_c->selection_info())
        return;

    const ModelObject* mo = m_c->selection_info()->model_object();
    if (m_state == On && !mo) {
        // resin-mode-structural-mutation-safety: the focused ModelObject vanished (e.g. deleted
        // by a structural mutation that was not routed through the containment choke point).
        // Self-close rather than silently stalling with a dangling reference; leave_mode_undo_stack()
        // (invoked via on_set_state(Off)) is safe here since it only inspects undo-stack state.
        m_parent.get_gizmos_manager().reset_all_states();
        return;
    }
    if (m_state == On && mo) {
        // PhrozenOrca: required_step < 0 means no minimum step (no-step constructor was used).
        const int required_step = get_min_sla_print_object_step();
        const SLAPrintObject* po = m_c->selection_info()->print_object();
        if (required_step >= 0 && po != nullptr && po->get_mesh_to_print().empty())
            reslice_until_step((SLAPrintObjectStep)required_step);

        update_volumes();

        // Step 4.4 dependency: hide all model objects so this gizmo renders its own volumes.
        m_c->instances_hider()->set_hide_full_scene(true);
    }
}



void GLGizmoHollow::on_render()
{
    // Safety check: if selected print object doesn't exist on active bed, close gizmo.
    if (!selected_print_object_exists(m_parent, wxEmptyString)) {
        wxGetApp().CallAfter([this]() {
            m_parent.get_gizmos_manager().open_gizmo(m_parent.get_gizmos_manager().get_current_type());
        });
    }
    const Selection& selection = m_parent.get_selection();
    const CommonGizmosDataObjects::SelectionInfo* sel_info = m_c->selection_info();
    if (!sel_info)
        return;  // m_c not yet refreshed (e.g. intermediate render during undo/redo)

    // resin-mode-structural-mutation-safety: bounds-check before indexing. An empty or
    // out-of-range selection (e.g. right after the focused object was deleted) must not index
    // objects[] with an invalid index (get_object_idx() returns -1 when nothing is selected).
    const int obj_idx = selection.get_object_idx();
    // If current m_c->m_model_object does not match selection, ask GLCanvas3D to turn us off
    if (m_state == On
     && (obj_idx < 0 || !selection.get_model() || obj_idx >= (int)selection.get_model()->objects.size()
      || sel_info->model_object() != selection.get_model()->objects[obj_idx]
      || sel_info->get_active_instance() != selection.get_instance_idx())) {
        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_RESETGIZMOS));
        return;
    }

    glsafe(::glEnable(GL_BLEND));
    glsafe(::glEnable(GL_DEPTH_TEST));

    // Step 4.3: render_volumes() is now from GLGizmoSlaBase.
    render_volumes();
    m_c->object_clipper()->render_cut();
    if (are_sla_supports_shown())
        m_c->supports_clipper()->render_cut();

    glsafe(::glDisable(GL_BLEND));
}

void GLGizmoHollow::on_register_raycasters_for_picking()
{
    // Only the mesh volume is registered for picking.
    // Hole raycasters are registered by GLGizmoDrill.
    register_volume_raycasters_for_picking();
}

void GLGizmoHollow::on_unregister_raycasters_for_picking()
{
    // Mirrors on_register_raycasters_for_picking: only volume raycasters are managed here.
    unregister_volume_raycasters_for_picking();
}

// Note: unproject_on_mesh() has been REMOVED — it is now provided by GLGizmoSlaBase.


// Following function is called from GLCanvas3D to inform the gizmo about a mouse/keyboard event.
// The gizmo has an opportunity to react - if it does, it should return true so that the Canvas3D is
// aware that the event was reacted to and stops trying to make different sense of it. If the gizmo
// concludes that the event was not intended for it, it should return false.
bool GLGizmoHollow::gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down)
{
    // All hole interaction (add/select/move/delete/rectangle-select) is handled by GLGizmoDrill.
    // Hollow only handles clipping plane shortcuts.

    if (action == SLAGizmoEventType::MouseWheelUp && control_down) {
        double pos = m_c->object_clipper()->get_position();
        pos = std::min(1., pos + 0.01);
        m_c->object_clipper()->set_position_by_ratio(pos, true);
        return true;
    }

    if (action == SLAGizmoEventType::MouseWheelDown && control_down) {
        double pos = m_c->object_clipper()->get_position();
        pos = std::max(0., pos - 0.01);
        m_c->object_clipper()->set_position_by_ratio(pos, true);
        return true;
    }

    if (action == SLAGizmoEventType::ResetClippingPlane) {
        m_c->object_clipper()->set_position_by_ratio(-1., false);
        return true;
    }

    return false;
}

bool GLGizmoHollow::on_mouse(const wxMouseEvent &mouse_event)
{
    // Hollow does not consume any mouse events; all mouse input is passed back to the canvas.
    // Hole editing is handled by GLGizmoDrill. Clipping plane adjustment (Ctrl+scroll) is
    // handled via gizmo_event(), which receives pre-translated SLAGizmoEventType values.
    if (mouse_event.Moving()) return false;

    if (mouse_event.Dragging() && mouse_event.CmdDown())
        return false;

    return false;
}

std::vector<std::pair<const ConfigOption*, const ConfigOptionDef*>>
GLGizmoHollow::get_config_options(const std::vector<std::string>& keys) const
{
    std::vector<std::pair<const ConfigOption*, const ConfigOptionDef*>> out;
    if (!m_c->selection_info())
        return out;
    const ModelObject* mo = m_c->selection_info()->model_object();
    if (!mo)
        return out;
    // Stale detection: if SelectionInfo references a different object than the current
    // selection, the pointer may be dangling (undo replaced the model). Skip this frame.
    {
        const Selection& sel = m_parent.get_selection();
        const int obj_idx = sel.get_object_idx();
        if (obj_idx < 0 || !sel.get_model() || obj_idx >= (int)sel.get_model()->objects.size()
            || mo != sel.get_model()->objects[obj_idx])
            return out;
    }

    const DynamicPrintConfig& object_cfg = mo->config.get();
    const DynamicPrintConfig& print_cfg = wxGetApp().preset_bundle->sla_prints.get_edited_preset().config;
    std::unique_ptr<DynamicPrintConfig> default_cfg = nullptr;

    for (const std::string& key : keys) {
        // PhrozenOrca: DynamicPrintConfig has no option_def(). Use def()->get(key) instead.
        if (object_cfg.has(key))
            out.emplace_back(object_cfg.option(key), object_cfg.def()->get(key));
        else
            if (print_cfg.has(key))
                out.emplace_back(print_cfg.option(key), print_cfg.def()->get(key));
            else { // we must get it from defaults
                if (default_cfg == nullptr)
                    default_cfg.reset(DynamicPrintConfig::new_from_defaults_keys(keys));
                out.emplace_back(default_cfg->option(key), default_cfg->def()->get(key));
            }
    }

    return out;
}


void GLGizmoHollow::on_render_input_window(float x, float y, float bottom_limit)
{
    if (!m_c->selection_info())
        return;
    ModelObject* mo = m_c->selection_info()->model_object();
    if (!mo) {
        // resin-mode-structural-mutation-safety: focused ModelObject vanished; self-close.
        m_parent.get_gizmos_manager().reset_all_states();
        return;
    }
    // Stale detection: SelectionInfo may hold a dangling pointer after undo replaces
    // the model. Compare against the current selection before dereferencing mo->config.
    {
        const Selection& sel = m_parent.get_selection();
        const int obj_idx = sel.get_object_idx();
        if (obj_idx < 0 || !sel.get_model() || obj_idx >= (int)sel.get_model()->objects.size()
            || mo != sel.get_model()->objects[obj_idx]) {
            // resin-mode-structural-mutation-safety: stale/vanished object reference; self-close.
            m_parent.get_gizmos_manager().reset_all_states();
            return;
        }
    }

    // Clamp y so the panel stays on screen
    y = std::min(y, bottom_limit - m_imgui->scaled(20.0f));

    // Refresh hollow-enable state from config each frame
    {
        auto opts = get_config_options({"hollowing_enable"});
        m_enable_hollowing = static_cast<const ConfigOptionBool*>(opts[0].first)->value;
    }

    // Reset pending params when object changes (discards unapplied adjustments)
    if (mo != m_pending_owner) {
        auto p_opts = get_config_options({"hollowing_min_thickness", "hollowing_quality", "hollowing_closing_distance"});
        m_pending_offset    = static_cast<const ConfigOptionFloat*>(p_opts[0].first)->value;
        m_pending_quality   = static_cast<const ConfigOptionFloat*>(p_opts[1].first)->value;
        m_pending_closing_d = static_cast<const ConfigOptionFloat*>(p_opts[2].first)->value;
        m_pending_owner     = mo;
    }

    ConfigOptionMode current_mode = wxGetApp().get_mode();
    bool config_changed = false;
    render_hollow_panel(x, y, mo, current_mode, config_changed);

    if (config_changed)
        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_FORCE_UPDATE));
}


// Custom horizontal slider for the hollow panel.
// Identical in behavior to draw_custom_slider in GLGizmoDrill.cpp.
// s_min/s_max: slider bar drag range and handle visual mapping.
// v_min/v_max: final value clamp range (typically wider, allows off-scale keyboard input).
// Returns true if v was changed this frame by slider interaction.
static bool hp_draw_custom_slider(const char* id, float& v,
                                   float s_min, float s_max,
                                   float v_min, float v_max,
                                   float bar_w, bool enabled = true)
{
    ImDrawList*  dl  = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float  fh  = ImGui::GetFrameHeight();

    constexpr float kBarH     = 3.f;
    constexpr float kGap      = 2.f;
    constexpr float kTriH     = 7.f;
    constexpr float kTriW     = 10.f;
    constexpr float kRounding = 1.5f;

    const float block_h = kBarH + kGap + kTriH;
    const float v_off   = std::floor((fh - block_h) * 0.5f);
    const float bar_y0  = pos.y + v_off;
    const float bar_y1  = bar_y0 + kBarH;
    const float tri_ay  = bar_y1 + kGap;
    const float tri_by  = tri_ay + kTriH;

    ImGui::InvisibleButton(id, ImVec2(bar_w, fh));
    const bool active  = ImGui::IsItemActive();
    bool       changed = false;

    if (active && enabled) {
        const float t  = std::clamp((ImGui::GetIO().MousePos.x - pos.x) / bar_w, 0.f, 1.f);
        float       nv = s_min + t * (s_max - s_min);
        nv = std::clamp(nv, v_min, v_max);
        if (nv != v) { v = nv; changed = true; }
    }

    // Handle draws at clamped visual position; value outside slider range saturates to endpoint.
    const float t_draw = (s_max > s_min)
        ? std::clamp((v - s_min) / (s_max - s_min), 0.f, 1.f) : 0.f;
    const float hx = pos.x + t_draw * bar_w;

    constexpr ImU32 kOrange = IM_COL32(232, 107, 32, 255);
    constexpr ImU32 kGray   = IM_COL32(190, 190, 190, 255);

    if (hx > pos.x)
        dl->AddRectFilled(ImVec2(pos.x, bar_y0), ImVec2(hx,          bar_y1), kOrange, kRounding);
    if (hx < pos.x + bar_w)
        dl->AddRectFilled(ImVec2(hx,    bar_y0), ImVec2(pos.x+bar_w, bar_y1), kGray,   kRounding);

    dl->AddTriangleFilled(
        ImVec2(hx,               tri_ay),
        ImVec2(hx - kTriW * .5f, tri_by),
        ImVec2(hx + kTriW * .5f, tri_by),
        kOrange);

    return changed;
}


void GLGizmoHollow::render_hollow_panel(float x, float y,
                                         ModelObject* mo, ConfigOptionMode current_mode,
                                         bool& config_changed)
{
    const float scale = m_parent.get_scale();

    // Fetch config defs for labels, ranges, and mode thresholds.
    // Values are kept in m_pending_* (initialized from config on object change, written to
    // config only when the Hollow button is pressed — not on every slider interaction).
    std::vector<std::string> opts_keys = {"hollowing_min_thickness", "hollowing_quality", "hollowing_closing_distance"};
    auto opts = get_config_options(opts_keys);

    // Set translated labels from config defs (locale-safe, updates on language change)
    m_desc["offset"]           = _(opts[0].second->label) + ":";
    m_desc["quality"]          = _(opts[1].second->label) + ":";
    m_desc["closing_distance"] = _(opts[2].second->label) + ":";

    float& offset    = m_pending_offset;
    float& quality   = m_pending_quality;
    float& closing_d = m_pending_closing_d;
    const float offset_min    = (float)opts[0].second->min;
    const float offset_max    = (float)opts[0].second->max;
    const float quality_min   = (float)opts[1].second->min;
    const float quality_max   = (float)opts[1].second->max;
    const float closing_d_min = (float)opts[2].second->min;
    const float closing_d_max = (float)opts[2].second->max;
    const ConfigOptionMode quality_mode   = opts[1].second->mode;
    const ConfigOptionMode closing_d_mode = opts[2].second->mode;

    // Column widths matching drill panel conventions
    const float value_box_w = m_imgui->scaled(4.f);
    const float slider_w    = m_imgui->scaled(8.f);
    const float spacing_x   = m_imgui->get_item_spacing().x;

    // Label column: widest of all slider-row labels + extra margin
    const float label_col_w = std::max({
        m_imgui->calc_text_size(m_desc.at("offset")).x,
        m_imgui->calc_text_size(m_desc.at("quality")).x,
        m_imgui->calc_text_size(m_desc.at("closing_distance")).x
    }) + m_imgui->scaled(1.5f);

    const float fp    = ImGui::GetStyle().FramePadding.x * 2.f;
    const float btn_w = std::max(
        m_imgui->calc_text_size(m_desc.at("hp_hollow")).x,
        m_imgui->calc_text_size(m_desc.at("hp_remove")).x
    ) + fp + m_imgui->scaled(1.f);

    ImGuiWrapper::push_toolbar_style(scale);

    static float panel_w = 0.f;
    GizmoImguiSetNextWIndowPos(x, y, panel_w, 0, ImGuiCond_Always);
    m_imgui->begin(wxString("HollowPanel"),
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    // Rows 2–4: hollowing params — always enabled when input is available
    const bool hollow_active = is_input_enabled();
    m_imgui->disabled_begin(!hollow_active);

    // Row 2: Wall thickness — slider range == config range; InputFloat allows same range
    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc.at("offset"));
    ImGui::SameLine(label_col_w);
    hp_draw_custom_slider("##hp_sl_off", offset, offset_min, offset_max, offset_min, offset_max, slider_w, hollow_active);
    ImGui::SameLine();
    ImGui::PushItemWidth(value_box_w);
    ImGui::InputFloat("##hp_v_off", &offset, 0.f, 0.f, "%.1f");
    if (ImGui::IsItemDeactivatedAfterEdit() && hollow_active)
        offset = std::clamp(offset, offset_min, offset_max);
    ImGui::PopItemWidth();

    // Row 3: Accuracy — mode-conditional
    if (current_mode >= quality_mode) {
        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("quality"));
        ImGui::SameLine(label_col_w);
        hp_draw_custom_slider("##hp_sl_qua", quality, quality_min, quality_max, quality_min, quality_max, slider_w, hollow_active);
        ImGui::SameLine();
        ImGui::PushItemWidth(value_box_w);
        ImGui::InputFloat("##hp_v_qua", &quality, 0.f, 0.f, "%.2f");
        if (ImGui::IsItemDeactivatedAfterEdit() && hollow_active)
            quality = std::clamp(quality, quality_min, quality_max);
        ImGui::PopItemWidth();
    }

    // Row 4: Closing distance — mode-conditional
    if (current_mode >= closing_d_mode) {
        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("closing_distance"));
        ImGui::SameLine(label_col_w);
        hp_draw_custom_slider("##hp_sl_cld", closing_d, closing_d_min, closing_d_max, closing_d_min, closing_d_max, slider_w, hollow_active);
        ImGui::SameLine();
        ImGui::PushItemWidth(value_box_w);
        ImGui::InputFloat("##hp_v_cld", &closing_d, 0.f, 0.f, "%.1f");
        if (ImGui::IsItemDeactivatedAfterEdit() && hollow_active)
            closing_d = std::clamp(closing_d, closing_d_min, closing_d_max);
        ImGui::PopItemWidth();
    }

    m_imgui->disabled_end();

    // Button row: [?] | Hollow | Remove — tighter item spacing
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
    {
        ImTextureID normal_id = m_parent.get_gizmos_manager().get_icon_texture_id(
            GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_TOOLTIP);
        ImTextureID hover_id  = m_parent.get_gizmos_manager().get_icon_texture_id(
            GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_TOOLTIP_HOVER);
        const float icon_sz = 25.f * scale;

        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,    {0, 0});
        ImGui::ImageButton3(normal_id, hover_id, ImVec2(icon_sz, icon_sz));
        if (ImGui::IsItemHovered()) {
            // Position tooltip below the hollow panel (not overlapping it)
            const float hollow_panel_h = ImGui::GetWindowSize().y;
            ImGui::BeginTooltip2(ImVec2(x, y + hollow_panel_h));
            ImGui::EndTooltip();
        }
        ImGui::PopStyleVar(2);

        m_imgui->disabled_begin(!is_input_enabled());
        ImGui::SameLine();
        if (ImGui::Button((m_desc.at("hp_hollow") + "##hp_on").ToUTF8().data(), ImVec2(btn_w, 0.f))) {
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Hollow");
            mo->config.set("hollowing_min_thickness",    m_pending_offset);
            mo->config.set("hollowing_quality",          m_pending_quality);
            mo->config.set("hollowing_closing_distance", m_pending_closing_d);
            mo->config.set("hollowing_enable", true);
            m_enable_hollowing = true;
            wxGetApp().obj_list()->update_and_show_object_settings_item();
            config_changed = true;
            reslice_until_step(slaposDrillHoles);
        }
        m_imgui->disabled_end();
        m_imgui->disabled_begin(!is_input_enabled() || !m_enable_hollowing);
        ImGui::SameLine();
        if (ImGui::Button((m_desc.at("hp_remove") + "##hp_off").ToUTF8().data(), ImVec2(btn_w, 0.f))) {
            mo->config.set("hollowing_enable", false);
            m_enable_hollowing = false;
            wxGetApp().obj_list()->update_and_show_object_settings_item();
            config_changed = true;
            reslice_until_step(slaposDrillHoles);
        }
        m_imgui->disabled_end();
    }
    ImGui::PopStyleVar(1);

    panel_w = ImGui::GetWindowWidth();
    m_imgui->end();
    ImGuiWrapper::pop_toolbar_style();
}


bool GLGizmoHollow::on_is_activable() const
{
    const Selection& selection = m_parent.get_selection();

    if (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() != ptSLA
        || !selection.is_single_full_instance())
        return false;

    // Check that none of the selected volumes is outside. Only SLA auxiliaries (supports) are allowed outside.
    const Selection::IndicesList& list = selection.get_volume_idxs();
    for (const auto& idx : list)
        if (selection.get_volume(idx)->is_outside && selection.get_volume(idx)->composite_id.volume_id >= 0)
            return false;

    // Check that none of the selected volumes is marked as non-printable.
    for (const auto& idx : list) {
        if (!selection.get_volume(idx)->printable)
            return false;
    }

    return true;
}

bool GLGizmoHollow::on_is_selectable() const
{
    return (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptSLA);
}

std::string GLGizmoHollow::on_get_name() const
{
    return _u8L("Hollow");
}

void GLGizmoHollow::on_set_state()
{
    if (m_state == m_old_state)
        return;

    if (m_state == On) {
        // Make sure that current object is on current bed. Refuse to turn on otherwise.
        if (!selected_print_object_exists(m_parent, _(L("Selected object has to be on the active bed.")))) {
            m_state = Off;
            return;
        }
    }

    if (m_state == On && m_old_state != On) {
        // resin-mode-scoped-undo-stack: opening Hollow anchors a scoped undo/redo baseline.
        enter_mode_undo_stack();
    }

    if (m_state == Off && m_old_state != Off) {
        // the gizmo was just turned Off
        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_FORCE_UPDATE));
        // Step 4.3: restore model visibility when gizmo closes.
        m_c->instances_hider()->set_hide_full_scene(false);
        // Note: set_use_shift() not available in PhrozenOrca's SelectionInfo.
        m_pending_owner = nullptr;  // discard pending params so re-entry re-reads from config
        // resin-mode-scoped-undo-stack: collapse the session into at most one main-stack
        // snapshot (or none, if no Apply produced a net change since the baseline).
        leave_mode_undo_stack();
    }

    m_old_state = m_state;
}



// Hollow has no draggable objects; hole dragging is handled by GLGizmoDrill.
void GLGizmoHollow::on_start_dragging() {}
void GLGizmoHollow::on_stop_dragging()  {}
void GLGizmoHollow::on_dragging(const UpdateData &) {}


void GLGizmoHollow::on_load(cereal::BinaryInputArchive& ar)
{
    ar(m_pending_offset, m_pending_quality, m_pending_closing_d, m_enable_hollowing);
    // Force data_changed() to re-initialize from the restored ModelObject on next call,
    // since the previous m_pending_owner pointer is invalid after undo restores the model.
    m_pending_owner = nullptr;
}

void GLGizmoHollow::on_save(cereal::BinaryOutputArchive& ar) const
{
    ar(m_pending_offset, m_pending_quality, m_pending_closing_d, m_enable_hollowing);
}



// Hollow has no interactive objects that track hover state; hole hover is handled by GLGizmoDrill.
void GLGizmoHollow::on_set_hover_id() {}



} // namespace GUI
} // namespace Slic3r
