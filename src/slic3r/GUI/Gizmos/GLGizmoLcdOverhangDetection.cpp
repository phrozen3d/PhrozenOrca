#pragma region Phrozen LCD Parameter
#include "GLGizmoLcdOverhangDetection.hpp"

#include "libslic3r/Model.hpp"
//BBS
#include "libslic3r/Layer.hpp"
#include "libslic3r/Thread.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/SLA/SupportIslands/UniformSupportIsland.hpp"
#include "libslic3r/SLA/SupportIslands/SampleConfigFactory.hpp"

#include "slic3r/GUI/Gizmos/GLGizmoSlaSupports.hpp"
#include "slic3r/GUI/I18N.hpp"

#include <boost/log/trivial.hpp>

//#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/Utils/UndoRedo.hpp"

#include <GL/glew.h>

namespace Slic3r::GUI {

GLGizmoLcdOverhangDetection::GLGizmoLcdOverhangDetection(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoPainterBase(parent, icon_filename, sprite_id), m_current_tool(ImGui::CircleButtonIcon)
{
    m_tool_type = ToolType::BRUSH;
    m_cursor_type = TriangleSelector::CursorType::CIRCLE;
}

bool GLGizmoLcdOverhangDetection::on_is_selectable() const
{
    // Align behavior with "SLA Support Points": show only for SLA printers.
    return (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptSLA);
}

bool GLGizmoLcdOverhangDetection::on_is_activable() const
{
    const Selection& selection = m_parent.get_selection();

    if (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() != ptSLA
        || !selection.is_from_single_instance())
        return false;

    // Keep consistent with GLGizmoSlaSupports: disallow activating when a real model volume is outside the build area.
    // Only SLA auxiliaries (supports) are allowed outside.
    const Selection::IndicesList& list = selection.get_volume_idxs();
    for (const auto& idx : list)
        if (selection.get_volume(idx)->is_outside && selection.get_volume(idx)->composite_id.volume_id >= 0)
            return false;

    return true;
}

void GLGizmoLcdOverhangDetection::on_shutdown()
{
    //BBS
    //wait the thread
    if (m_thread.joinable()) {
        Print *print = m_print_instance.print_object->print();
        if (print) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "cancel the print";
            print->cancel();
        }
        //join the thread
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "try to join thread for 2000 ms";
        auto ret = m_thread.try_join_for(boost::chrono::milliseconds(2000));
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "join thread returns "<<ret;
    }

    m_print_instance.print_object = NULL;
    m_print_instance.model_instance = NULL;

    m_highlight_by_angle_threshold_deg = 0.f;
    m_parent.use_slope(false);
    m_parent.toggle_model_objects_visibility(true);
}

//BBS: add on_open
void GLGizmoLcdOverhangDetection::on_opening()
{
    m_angle_threshold_deg = 40;
    m_parent.set_slope_normal_angle(90.f - m_angle_threshold_deg);
    if (! m_parent.is_using_slope()) {
        m_parent.use_slope(true);
        m_parent.set_as_dirty();
    }
    m_print_instance.print_object = NULL;
    m_print_instance.model_instance = NULL;
    m_edit_state = state_idle;

    m_volume_ready = false;
    m_volume_valid = false;

    // Phrozen LCD: Set default values
    m_current_tool = ImGui::SphereButtonIcon;
    m_paint_on_overhangs_only = true;
    m_detection_accuracy = Accuracy_Middle;
    m_current_model_index = 0;
    m_current_overhang_area_index = 0;
    m_total_overhang_areas = 0;
    m_model_names.clear();
}

std::string GLGizmoLcdOverhangDetection::on_get_name() const
{
    return _u8L("Overhang Detection");
}

bool GLGizmoLcdOverhangDetection::on_init()
{
    // BBS
    m_shortcut_key = WXK_CONTROL_L;

    m_desc["clipping_of_view_caption"] = _L("Alt + Mouse wheel");
    m_desc["clipping_of_view"]      = _L("Section view");
    m_desc["reset_direction"]       = _L("Reset direction");
    m_desc["cursor_size_caption"]   = _L("Ctrl + Mouse wheel");
    m_desc["cursor_size"]           = _L("Pen size");
    m_desc["enforce_caption"]       = _L("Left mouse button");
    m_desc["enforce"]               = _L("Enforce supports");
    m_desc["block_caption"]         = _L("Right mouse button");
    m_desc["block"]                 = _L("Block supports");
    m_desc["remove_caption"]        = _L("Shift + Left mouse button");
    m_desc["remove"]                = _L("Erase");
    m_desc["remove_all"]            = _L("Erase all painting");
    m_desc["highlight_by_angle"]    = _L("Highlight overhang areas");
    m_desc["gap_fill"]              = _L("Gap fill");
    m_desc["perform"]               = _L("Perform");
    m_desc["gap_area_caption"]      = _L("Ctrl + Mouse wheel");
    m_desc["gap_area"]              = _L("Gap area");
    m_desc["tool_type"]             = _L("Tool type");
    m_desc["smart_fill_angle_caption"] = _L("Ctrl + Mouse wheel");
    m_desc["smart_fill_angle"]      = _L("Smart fill angle");
    m_desc["on_overhangs_only"] = _L("On overhangs only");
    
    // Phrozen LCD Overhang Detection UI
    m_desc["detection_accuracy"] = _L("Detection Accuracy");
    m_desc["low"] = _L("Low");
    m_desc["middle"] = _L("Middle");
    m_desc["high"] = _L("High");
    m_desc["model"] = _L("Model");
    m_desc["overhang_area"] = _L("Overhang Area");
    m_desc["detect_all"] = _L("Detect all");
    m_desc["detect_selected"] = _L("Detect selected");
    m_desc["add_overhang_supports"] = _L("Add overhang supports");

    memset(&m_print_instance, 0, sizeof(m_print_instance));
    return true;
}

void GLGizmoLcdOverhangDetection::render_painter_gizmo()
{
    const Selection& selection = m_parent.get_selection();

    glsafe(::glEnable(GL_BLEND));
    glsafe(::glEnable(GL_DEPTH_TEST));

    render_triangles(selection);
    //BBS: draw support volumes
    if (m_volume_ready && m_support_volume && (m_edit_state != state_generating))
    {
        // TODO: FIXME
        m_support_volume->set_render_color({0.f, 0.7f, 0.f, 0.7f});
        m_support_volume->render();
    }

    m_c->object_clipper()->render_cut();
    m_c->instances_hider()->render_cut();
    // No sphere cursor rendered. Update raycast cache so m_rr stays current for
    // on_mouse() orbit-center computation.
    const ModelObject* mo_rc = m_c->selection_info()->model_object();
    if (mo_rc) {
        const ModelInstance* mi_rc = mo_rc->instances[m_parent.get_selection().get_instance_idx()];
        std::vector<Transform3d> trafos;
        for (const ModelVolume* mv : mo_rc->volumes)
            if (mv->is_model_part())
                trafos.emplace_back(mi_rc->get_transformation().get_matrix() * mv->get_matrix());
        if (!trafos.empty())
            update_raycast_cache(m_parent.get_local_mouse_position(),
                                 wxGetApp().plater()->get_camera(), trafos);
    }

    glsafe(::glDisable(GL_BLEND));
}

void GLGizmoLcdOverhangDetection::on_render()
{
    render_island_contours();
}

Vec3d GLGizmoLcdOverhangDetection::compute_orbit_center() const
{
    // If m_rr has a valid surface hit, convert from mesh local space to world space.
    if (m_rr.mesh_id >= 0) {
        const ModelObject* mo = m_c->selection_info()->model_object();
        if (mo) {
            const ModelInstance* mi = mo->instances[m_parent.get_selection().get_instance_idx()];
            int vol_idx = 0;
            for (const ModelVolume* mv : mo->volumes) {
                if (mv->is_model_part()) {
                    if (vol_idx == m_rr.mesh_id) {
                        const Transform3d trafo = mi->get_transformation().get_matrix() * mv->get_matrix();
                        return trafo * m_rr.hit.cast<double>();
                    }
                    ++vol_idx;
                }
            }
        }
    }
    // Fallback: selection bounding box center.
    return m_parent.get_selection().get_bounding_box().center();
}

bool GLGizmoLcdOverhangDetection::on_mouse(const wxMouseEvent& mouse_event)
{
    // Island Detection gizmo has no painting. Left-drag orbits around the surface
    // hit point; right-drag and middle-drag are handled by the canvas (pan).

    if (mouse_event.Moving())
        return false;

    if (mouse_event.LeftDown()) {
        m_nav_orbit_center = compute_orbit_center();
        m_nav_drag_start   = Vec2d(mouse_event.GetX(), mouse_event.GetY());
        m_nav_dragging     = true;
        return true;  // consume so canvas does not select/move objects
    }

    if (mouse_event.Dragging() && m_nav_dragging && mouse_event.LeftIsDown()) {
        const Vec2d cur   = Vec2d(mouse_event.GetX(), mouse_event.GetY());
        const Vec2d delta = cur - m_nav_drag_start;
        m_nav_drag_start  = cur;

        Camera& camera = wxGetApp().plater()->get_camera();
        const std::string mult_str = wxGetApp().app_config->get("camera_orbit_mult");
        const double      mult     = mult_str.empty() ? 1.0 : std::stod(mult_str);
        // Same formula as GLCanvas3D orbit: pixel delta → radians
        // rot.x = horizontal delta → azimuth; rot.y = vertical delta → zenith
        const Vec3d rot = Vec3d(delta.x(), delta.y(), 0.) * (M_PI * 0.8 / 180.) * mult;
        camera.rotate_on_sphere_with_target(rot.x(), rot.y(), false, m_nav_orbit_center);
        m_parent.set_as_dirty();
        return true;
    }

    if (mouse_event.LeftUp()) {
        m_nav_dragging = false;
        return true;
    }

    // Right / middle button: return false so canvas handles pan normally.
    return false;
}

// BBS
bool GLGizmoLcdOverhangDetection::on_key_down_select_tool_type(int keyCode) {
    switch (keyCode)
    {
    case 'F':
        m_current_tool = ImGui::FillButtonIcon;
        break;
    case 'S':
        m_current_tool = ImGui::SphereButtonIcon;
        break;
    case 'C':
        m_current_tool = ImGui::CircleButtonIcon;
        break;
    case 'G':
        m_current_tool = ImGui::GapFillIcon;
        break;
    default:
        return false;
        break;
    }
    return true;
}

void GLGizmoLcdOverhangDetection::on_set_state()
{
    GLGizmoPainterBase::on_set_state();

    if (get_state() == On) {
        m_support_threshold_angle = -1;

        // Task 3.1: 初始化 model 清單，定位至進入時已選取的物件
        sync_all_objects_names();
        int sel_idx = m_parent.get_selection().get_object_idx();
        if (sel_idx >= 0 && sel_idx < (int)m_model_names.size())
            m_current_model_index = sel_idx;
        else
            m_current_model_index = 0;

        // 若已有 island 資料，立即建立索引
        auto it = m_island_data_per_object.find(m_current_model_index);
        if (it != m_island_data_per_object.end() && it->second.valid && !it->second.islands.empty())
            rebuild_overhang_area_index_map(false);
    }
    else if (get_state() == Off) {
        // 清除所有 island GL 資源與資料
        m_island_original_model.reset();
        m_island_overlay_model.reset();
        m_island_highlight_model.reset();
        m_island_data_per_object.clear();
        m_overhang_area_index_map.clear();
        m_current_overhang_area_index = 0;
        m_total_overhang_areas        = 0;
        m_island_data_dirty           = false;
        m_slice_pending_for_detect    = false;

        ModelObject* mo = m_c->selection_info()->model_object();
        if (mo) Slic3r::save_object_mesh(*mo);
    }
}

void GLGizmoLcdOverhangDetection::on_render_input_window(float x, float y, float bottom_limit)
{
    // Check if a pending slice triggered by detect_selected has now completed.
    if (m_slice_pending_for_detect) {
        const SLAPrint* sla_print = m_parent.sla_print();
        if (sla_print && m_current_model_index < (int)sla_print->objects().size()) {
            if (sla_print->objects()[m_current_model_index]->is_step_done(slaposObjectSlice)) {
                m_slice_pending_for_detect = false;
                sync_island_data_for_object(m_current_model_index);
                rebuild_overhang_area_index_map(false);
                if (!m_overhang_area_index_map.empty()) {
                    rebuild_island_overlay_mesh();
                    rebuild_island_highlight_mesh(0);
                }
                m_parent.set_as_dirty();
            }
        }
    }

    init_print_instance();
    if (! m_c->selection_info()->model_object())
        return;

    int support_threshold_angle = get_selection_support_threshold_angle();
    // when support painting tool is on, reset highlight threshold angle
    if (m_support_threshold_angle == -1) {
        m_highlight_by_angle_threshold_deg = support_threshold_angle;
        m_parent.set_slope_normal_angle(90.f - m_highlight_by_angle_threshold_deg);
    }
    m_support_threshold_angle = 45;
    m_highlight_by_angle_threshold_deg = 45;

    m_parent.set_slope_normal_angle(90.f - m_highlight_by_angle_threshold_deg);
    if (!m_parent.is_using_slope()) {
        m_parent.use_slope(true);
        m_parent.set_as_dirty();
    }

    const float approx_height = m_imgui->scaled(23.f);
    y = std::min(y, bottom_limit - approx_height);

    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 0.0f, 0.0f);

    //BBS
    ImGuiWrapper::push_toolbar_style(m_parent.get_scale());
    GizmoImguiBegin(get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    // Phrozen LCD: New UI layout
    const float accuracy_button_width = m_imgui->scaled(6.f); //118.67
    const float bracket_button_width  = 24.f;
    const float button_width = 24.f;
    const float button_height = 24.f;
    const float max_tooltip_width = ImGui::GetFontSize() * 20.0f;
    
    // Ensure tool is Sphere and On Overhang Only is enabled
    m_current_tool = ImGui::SphereButtonIcon;
    m_cursor_type = TriangleSelector::CursorType::SPHERE;
    m_tool_type = ToolType::BRUSH;
    m_paint_on_overhangs_only = true;
    
    // Row 1: Detection Accuracy label
    ImGui::AlignTextToFramePadding();
    //m_imgui->text(m_desc.at("detection_accuracy"));
    m_imgui->text(_L("Detection Accuracy"));
    
    // Row 2: Low, Middle, High buttons
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 4.0f));
    

    DetectionAccuracy next_accuracy  = m_detection_accuracy;

    // Low button
    bool low_selected = (next_accuracy == Accuracy_Low);
    if (low_selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(255 / 255.f, 235 / 255.f, 226 / 255.f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(255 / 255.f, 124 / 255.f, 63 / 255.f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(255 / 255.f, 235 / 255.f, 226 / 255.f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImGuiWrapper::COL_PHROZEN);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    }

    if (ImGui::Button(into_u8(m_desc.at("low")).c_str(), ImVec2(accuracy_button_width, 0))) {
        next_accuracy = Accuracy_Low;
    }

    if (low_selected) {
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(1);
    }
    
    ImGui::SameLine();
    
    // Middle button
    bool middle_selected = (next_accuracy == Accuracy_Middle);
    if (middle_selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(255 / 255.f, 235 / 255.f, 226 / 255.f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(255 / 255.f, 124 / 255.f, 63 / 255.f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(255 / 255.f, 235 / 255.f, 226 / 255.f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImGuiWrapper::COL_PHROZEN);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    }

    if (ImGui::Button(into_u8(m_desc.at("middle")).c_str(), ImVec2(accuracy_button_width, 0))) {
        next_accuracy = Accuracy_Middle;
    }

    if (middle_selected) {
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(1);
    }
    
    ImGui::SameLine();
    
    // High button
    bool high_selected = (next_accuracy == Accuracy_High);
    if (high_selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(255 / 255.f, 235 / 255.f, 226 / 255.f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(255 / 255.f, 124 / 255.f, 63 / 255.f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(255 / 255.f, 235 / 255.f, 226 / 255.f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImGuiWrapper::COL_PHROZEN);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    }

    if (ImGui::Button(into_u8(m_desc.at("high")).c_str(), ImVec2(accuracy_button_width, 0))) {
        next_accuracy = Accuracy_High;
    }

    if (high_selected) {
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(1);
    }

    m_detection_accuracy = next_accuracy;

    ImGui::PopStyleVar(1);

    // Layer height hint — updates immediately when accuracy level changes
    if ( m_bDebugMode )
    {
        const float lh = accuracy_to_layer_height(m_detection_accuracy);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Layer Height: %.2fmm", lh);
        ImGui::AlignTextToFramePadding();
        m_imgui->text(buf);
    }
    #if 0
    // Row 3: Model label
    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc.at("model"));
    
    // Row 4: < Model text >
    // Set to true to show model navigation arrows; false to hide (layout preserved).
    static constexpr bool show_model_nav_arrows = false;

    ImGui::AlignTextToFramePadding();

    if (show_model_nav_arrows) {
        if (ImGui::Button("<", ImVec2(bracket_button_width, 0))) {
            if (!m_model_names.empty() && m_current_model_index > 0) {
                m_current_model_index--;
                rebuild_overhang_area_index_map(false);
            }
        }
    } else {
        ImGui::Dummy(ImVec2(bracket_button_width, ImGui::GetFrameHeight()));
    }

    ImGui::SameLine();

    // Model text (centered)
    wxString model_text;
    if (m_current_model_index >= 0 && m_current_model_index < static_cast<int>(m_model_names.size())) {
        model_text = wxString::FromUTF8(m_model_names[m_current_model_index].c_str());
    } else {
        model_text = wxString::Format("Model %d", m_current_model_index + 1);
    }
    float model_text_width = m_imgui->calc_text_size(model_text).x;
    float available_width = ImGui::GetContentRegionAvail().x - button_width * 2 - ImGui::GetStyle().ItemSpacing.x * 2;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available_width - model_text_width) * 0.5f);
    m_imgui->text(model_text);

    ImGui::SameLine();

    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - bracket_button_width);
    if (show_model_nav_arrows) {
        if (ImGui::Button(">", ImVec2(bracket_button_width, 0))) {
            if (!m_model_names.empty() && m_current_model_index < static_cast<int>(m_model_names.size()) - 1) {
                m_current_model_index++;
                rebuild_overhang_area_index_map(false);
            }
        }
    } else {
        ImGui::Dummy(ImVec2(bracket_button_width, ImGui::GetFrameHeight()));
    }
    #endif

    // Row 5: Overhang Area label
    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc.at("overhang_area"));
    
    // Row 6: < 0/12 >
    ImGui::AlignTextToFramePadding();
    
    // Left arrow button
    if (ImGui::Button("<##overhang", ImVec2(bracket_button_width, 0))) {
        if (!m_overhang_area_index_map.empty() && m_current_overhang_area_index > 1) {
            m_current_overhang_area_index--;
            rebuild_island_overlay_mesh();
            rebuild_island_highlight_mesh(m_current_overhang_area_index);
            focus_camera_on_island(m_current_overhang_area_index);
        }
    }
    
    ImGui::SameLine();
    
    // Overhang area text (centered)
    wxString overhang_text = wxString::Format("%d/%d", m_current_overhang_area_index, m_total_overhang_areas);
    float overhang_text_width       = m_imgui->calc_text_size(overhang_text).x;
    float overhang_available_width  = ImGui::GetContentRegionAvail().x - button_width * 2 - ImGui::GetStyle().ItemSpacing.x * 2;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (overhang_available_width - overhang_text_width) * 0.5f);
    m_imgui->text(overhang_text);
    
    ImGui::SameLine();
    
    // Right arrow button
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - bracket_button_width);
    if (ImGui::Button(">##overhang", ImVec2(bracket_button_width, 0))) {
        if (!m_overhang_area_index_map.empty() && m_current_overhang_area_index < m_total_overhang_areas ) {
            m_current_overhang_area_index++;
            rebuild_island_overlay_mesh();
            rebuild_island_highlight_mesh(m_current_overhang_area_index);
            focus_camera_on_island(m_current_overhang_area_index);
        }
    }
    
    // Row 7: Separator
    ImGui::Separator();
    
    // Row 8: Tooltip icon + buttons（勿用螢幕座標 y/x 呼叫 SetCursorPos，否則視窗會被撐滿整個畫布高度並造成位置每幀跳動）
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));

    const float tooltip_anchor_y = ImGui::GetContentRegionMax().y + ImGui::GetFrameHeight() + y;
    float       caption_max      = 0.f;
    show_tooltip_information(caption_max, x, tooltip_anchor_y + 25.f);

    ImGui::SameLine();

    // Each button independently scales its own text width × 1.3.
    const float padding_x    = 2.f * ImGui::GetStyle().FramePadding.x;
    const float detect_btn_w = (m_imgui->calc_text_size(m_desc.at("detect_selected")).x + padding_x) * 1.3f;

    if (m_imgui->button(m_desc.at("detect_selected"), detect_btn_w, 0.f)) {
        const SLAPrint* sla_print = m_parent.sla_print();
        if (sla_print && m_current_model_index < (int)sla_print->objects().size()) {
            const SLAPrintObject* po = sla_print->objects()[m_current_model_index];
            if (!po->is_step_done(slaposObjectSlice)) {
                // Slice not done — trigger it; detection will auto-run when complete.
                m_slice_pending_for_detect = true;
                const ModelObject* mo = po->model_object();
                if (mo)
                    wxGetApp().CallAfter([mo]() {
                        wxGetApp().plater()->reslice_SLA_until_step(slaposObjectSlice, *mo, false);
                    });
            } else {
                sync_island_data_for_object(m_current_model_index);
                rebuild_overhang_area_index_map(false);
                if (!m_overhang_area_index_map.empty()) {
                    rebuild_island_overlay_mesh();
                    rebuild_island_highlight_mesh(0);
                }
                m_parent.set_as_dirty();
            }
        }
    }

    // Next row: tooltip icon gap + add_overhang_supports scaled independently.
    const float scale            = m_parent.get_scale();
    const float tooltip_icon_w   = 25.f * scale;
    const float add_supports_btn_w = (m_imgui->calc_text_size(m_desc.at("add_overhang_supports")).x + padding_x) * 1.3f;

    ImGui::Dummy(ImVec2(tooltip_icon_w, 0.f));
    ImGui::SameLine();
    const bool can_add = !m_overhang_area_index_map.empty();
    if (m_imgui->button(m_desc.at("add_overhang_supports"), ImVec2(add_supports_btn_w, 0.f), can_add)) {
        generate_island_support_points();
    }

    ImGui::PopStyleVar(1);

    GizmoImguiEnd();

    // BBS
    ImGuiWrapper::pop_toolbar_style();
}

void GLGizmoLcdOverhangDetection::tool_changed(wchar_t old_tool, wchar_t new_tool)
{
    if ((old_tool == ImGui::GapFillIcon && new_tool == ImGui::GapFillIcon) ||
        (old_tool != ImGui::GapFillIcon && new_tool != ImGui::GapFillIcon))
        return;

    for (auto& selector_ptr : m_triangle_selectors) {
        TriangleSelectorPatch* tsp = dynamic_cast<TriangleSelectorPatch*>(selector_ptr.get());
        tsp->set_filter_state(new_tool == ImGui::GapFillIcon);
    }
}

void GLGizmoLcdOverhangDetection::show_tooltip_information(float caption_max, float x, float y)
{
    ImTextureID normal_id = m_parent.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_TOOLTIP);
    ImTextureID hover_id  = m_parent.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_TOOLTIP_HOVER);

    // The second column uses SameLine(caption_max). If caption_max is too small, the "text" column overlaps the "caption".
    // Unlike some other gizmos, our caller may pass caption_max as 0, so compute it here based on current tooltip items.
    std::vector<std::string> tip_items;
    switch (m_tool_type) {
        case ToolType::BRUSH:
            tip_items = {"enforce", "block", "remove", "cursor_size", "clipping_of_view"};
            break;
        case ToolType::BUCKET_FILL:
            break;
        case ToolType::SMART_FILL:
            tip_items = {"enforce", "block", "remove", "smart_fill_angle", "clipping_of_view"};
            break;
        case ToolType::GAP_FILL:
            tip_items = {"gap_area"};
            break;
        default:
            break;
    }

    caption_max = 0.f;
    for (const auto &t : tip_items) {
        const wxString caption = m_desc.at(t + "_caption") + ": ";
        caption_max = std::max(caption_max, m_imgui->calc_text_size(caption).x);
    }
    caption_max += ImGui::GetStyle().WindowPadding.x + m_imgui->calc_text_size(std::string_view{": "}).x + 15.f;

    float  scale       = m_parent.get_scale();
    ImVec2 button_size = ImVec2(25 * scale, 25 * scale); // ORCA: Use exact resolution will prevent blur on icon
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, 0}); // ORCA: Dont add padding
    ImGui::ImageButton3(normal_id, hover_id, button_size);

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip2(ImVec2(x, y));
        auto draw_text_with_caption = [this, &caption_max](const wxString &caption, const wxString &text) {
            // BBS
            m_imgui->text_colored(ImGuiWrapper::COL_ACTIVE, caption);
            ImGui::SameLine(caption_max);
            m_imgui->text_colored(ImGuiWrapper::COL_WINDOW_BG, text);
        };
        for (const auto &t : tip_items) draw_text_with_caption(m_desc.at(t + "_caption") + ": ", m_desc.at(t));

        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar(2);
}

// BBS
int GLGizmoLcdOverhangDetection::get_selection_support_threshold_angle()
{
    auto sel_info = m_c->selection_info();
    if (sel_info == nullptr)
        return -1;

    const DynamicPrintConfig& obj_cfg = sel_info->model_object()->config.get();
    const DynamicPrintConfig& glb_cfg = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    bool enable_support = obj_cfg.option("enable_support") ? obj_cfg.opt_bool("enable_support") : glb_cfg.opt_bool("enable_support");
    SupportType support_type = obj_cfg.option("support_type") ? obj_cfg.opt_enum<SupportType>("support_type") : glb_cfg.opt_enum<SupportType>("support_type");
    int support_threshold_angle = obj_cfg.option("support_threshold_angle") ? obj_cfg.opt_int("support_threshold_angle") : glb_cfg.opt_int("support_threshold_angle");

    bool auto_support = enable_support && is_auto(support_type);
    return auto_support ? support_threshold_angle : 0;
}

void GLGizmoLcdOverhangDetection::select_facets_by_angle(float threshold_deg, bool block)
{
    float threshold = (float(M_PI)/180.f)*threshold_deg;
    const Selection& selection = m_parent.get_selection();
    const ModelObject* mo = m_c->selection_info()->model_object();
    const ModelInstance* mi = mo->instances[selection.get_instance_idx()];

    int mesh_id = -1;
    for (const ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;

        ++mesh_id;

        const Transform3d trafo_matrix = mi->get_matrix_no_offset() * mv->get_matrix_no_offset();
        Vec3f down  = (trafo_matrix.inverse() * (-Vec3d::UnitZ())).cast<float>().normalized();
        Vec3f limit = (trafo_matrix.inverse() * Vec3d(std::sin(threshold), 0, -std::cos(threshold))).cast<float>().normalized();

        float dot_limit = limit.dot(down);

        // Now calculate dot product of vert_direction and facets' normals.
        int idx = 0;
        const indexed_triangle_set &its = mv->mesh().its;
        for (const stl_triangle_vertex_indices &face : its.indices) {
            if (its_face_normal(its, face).dot(down) > dot_limit) {
                m_triangle_selectors[mesh_id]->set_facet(idx, block ? EnforcerBlockerType::BLOCKER : EnforcerBlockerType::ENFORCER);
                m_triangle_selectors.back()->request_update_render_data();
            }
            ++ idx;
        }
    }

    Plater::TakeSnapshot snapshot(wxGetApp().plater(), block ? "Block supports by angle"
                                                    : "Add supports by angle");
    update_model_object();
    m_parent.set_as_dirty();
}

//BBS: remove const
void GLGizmoLcdOverhangDetection::update_model_object()
{
    bool updated = false;
    ModelObject* mo = m_c->selection_info()->model_object();
    int idx = -1;
    for (ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;
        ++idx;
        updated |= mv->supported_facets.set(*m_triangle_selectors[idx].get());
    }

    if (updated) {
        const ModelObjectPtrs& mos = wxGetApp().model().objects;
        wxGetApp().obj_list()->update_info_items(std::find(mos.begin(), mos.end(), mo) - mos.begin());

        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
    }

    //BBS: invalid volume_support status
    invalid_support_volumes(true);
}

//BBS
void GLGizmoLcdOverhangDetection::update_from_model_object(bool first_update)
{
    wxBusyCursor wait;

    const ModelObject* mo = m_c->selection_info()->model_object();
    m_triangle_selectors.clear();
    //BBS: add timestamp logic
    m_volume_timestamps.clear();

    // 從場景 SLAPrintObject 重建 model names（Task 2.2）
    sync_all_objects_names();
    if (m_current_model_index >= static_cast<int>(m_model_names.size()))
        m_current_model_index = m_model_names.empty() ? 0 : static_cast<int>(m_model_names.size()) - 1;

    int volume_id = -1;
    std::vector<ColorRGBA> ebt_colors;
    ebt_colors.push_back(GLVolume::NEUTRAL_COLOR);
    ebt_colors.push_back(TriangleSelectorGUI::enforcers_color);
    ebt_colors.push_back(TriangleSelectorGUI::blockers_color);
    for (const ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;

        ++volume_id;

        // This mesh does not account for the possible Z up SLA offset.
        const TriangleMesh* mesh = &mv->mesh();
        m_triangle_selectors.emplace_back(std::make_unique<TriangleSelectorPatch>(*mesh, ebt_colors));
        m_triangle_selectors.back()->deserialize(mv->supported_facets.get_data(), false);
        m_triangle_selectors.back()->request_update_render_data();

        //BBS: add timestamp logic
        m_volume_timestamps.emplace_back(mv->supported_facets.timestamp());
    }

    //BBS: invalid volume_support status
    invalid_support_volumes(true);
}

PainterGizmoType GLGizmoLcdOverhangDetection::get_painter_type() const
{
    return PainterGizmoType::FDM_SUPPORTS;
}

wxString GLGizmoLcdOverhangDetection::handle_snapshot_action_name(bool shift_down, GLGizmoPainterBase::Button button_down) const
{
    // BBS remove _L()
    wxString action_name;
    if (shift_down)
        action_name = ("Unselect all");
    else {
        if (button_down == Button::Left)
            action_name = ("Enforce supports");
        else
            action_name = ("Block supports");
    }
    return action_name;
}

//BBS
void GLGizmoLcdOverhangDetection::init_print_instance()
{
    const PrintObject* print_object = NULL;
    PrintInstance print_instance = { 0 };
    const Print *print = m_parent.fff_print();

    if (!m_c->selection_info() || (m_print_instance.print_object))
    {
        //no selection or already got a print instance before
        return;
    }
    const ModelObject* model_object = m_c->selection_info()->model_object();
    int instance_index = m_c->selection_info()->get_active_instance();
    const ModelInstance* model_instance = model_object->instances[instance_index];

    //check the print
    if (!print)
    {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",print invalid\n";
        return;
    }

    for (const PrintObject* object : print->objects())
    {
        if (object->model_object()->id() == model_object->id())
        {
            BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ",found a PrintObject, id is" << model_object->id().id;
            print_object = object;
            break;
        }
    }

    //check the pring object
    if (!print_object)
    {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",can not find a PrintObject\n";
        return;
    }

    //find the print instance
    for (const PrintInstance &instance : print_object->instances())
    {
        if (instance.model_instance->id() == model_instance->id())
        {
            BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ",found a PrintInstance, id is" << model_instance->id().id;
            m_print_instance = instance;
            break;
        }
    }

    //check the pring object
    if (!m_print_instance.print_object)
    {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",can not find a PrintInstance\n";
        return;
    }

    const PrintObjectConfig& config = m_print_instance.print_object->config();
    m_angle_threshold_deg = config.support_angle;
    m_is_tree_support = config.enable_support.value && is_tree(config.support_type.value);
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",get support_angle "<< m_angle_threshold_deg<<", is_tree "<<m_is_tree_support;

    return;
}

void GLGizmoLcdOverhangDetection::invalid_support_volumes(bool invalid_step)
{
    std::unique_lock<std::mutex> lck(m_mutex);
    m_volume_valid = false;

    if ((invalid_step) && (m_edit_state == state_generating) && m_print_instance.print_object)
    {
        Print *print = m_print_instance.print_object->print();
        if (print) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "cancel the print";
            print->cancel();
        }
    }
    m_edit_state = state_idle;
    lck.unlock();

    return;
}

bool GLGizmoLcdOverhangDetection::need_regenerate_support_volumes()
{
    if (!m_support_volume)
        return true;

    const ModelObject* mo = m_c->selection_info()->model_object();

    if (m_object_id != m_print_instance.print_object->id().id)
    {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",object_id changed from " << m_object_id << " to " << m_print_instance.print_object->id().id << ", need to regenerate";
        return true;
    }

    int volume_id = -1;
    for (const ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;

        ++volume_id;
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",volume_id "<<volume_id<<", record_timestamp "<< m_volume_timestamps[volume_id]
                <<", current_timestamp "<<mv->supported_facets.timestamp();
        if (m_volume_timestamps[volume_id] != mv->supported_facets.timestamp())
        {
            return true;
        }
    }

    return false;
}

void GLGizmoLcdOverhangDetection::update_support_volumes()
{
    //PrintInstance m_print_instance = get_current_print_instance();

    if ((!m_print_instance.print_object))
    {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",invalid param, m_volume_ready="<< m_volume_ready;
        return;
    }

    if (m_volume_valid || !need_regenerate_support_volumes())
    {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",no need to regenerate support volume, return directly";

        std::unique_lock<std::mutex> lck(m_mutex);
        m_volume_ready = true;
        m_volume_valid = true;
        m_edit_state = state_ready;
        lck.unlock();
        return;
    }

    //generate_support_preview in async mode
    std::unique_lock<std::mutex> lck(m_mutex);
    m_volume_ready = false;
    //destroy previous support volume
    if (m_support_volume)
    {
        delete m_support_volume;
        m_support_volume = NULL;
    }
    lck.unlock();

    if (m_thread.joinable()) {
        //join the thread in ui thread
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "try to join thread for 100 ms";
        auto ret = m_thread.try_join_for(boost::chrono::milliseconds(100));
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "join thread returns "<<ret;
    }
    m_cancel = false;
    m_thread = create_thread([this]{this->run_thread();});
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",created thread to generate support volumes";
    return;
}

void GLGizmoLcdOverhangDetection::run_thread()
{
    try {
        Print *print = m_print_instance.print_object->print();

        print->restart();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",before generate_support_preview";
        m_print_instance.print_object->generate_support_preview();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",after generate_support_preview";

        if (m_cancel)
        {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", cancelled";
            goto _finished;
        }

        std::unique_lock<std::mutex> lck(m_mutex);
        m_support_volume = new GLVolume(0.5f, 0.5f, 0.5f, 0.5f);
        //m_support_volume->is_support_part = true;
        m_support_volume->force_native_color = true;
        m_support_volume->set_render_color();
        lck.unlock();

        auto record_timestamp = [this]()
        {
            const ModelObject* mo = m_c->selection_info()->model_object();

            int volume_id = -1;
            for (const ModelVolume* mv : mo->volumes) {
                if (!mv->is_model_part())
                    continue;

                ++volume_id;
                m_volume_timestamps[volume_id] = mv->supported_facets.timestamp();
            }
        };

        if (m_cancel)
        {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", cancelled";
            goto _finished;
        }

        if (!m_print_instance.print_object->support_layers().size())
        {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",no support layer found, update status to 100%\n";
            print->set_status(100, L("Support Generated"));
            goto _finished;
        }
        GLModel::Geometry init_data;
        init_data.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3 };
        for (const SupportLayer *support_layer : m_print_instance.print_object->support_layers())
        {
            for (const ExtrusionEntity *extrusion_entity : support_layer->support_fills.entities)
            {
                _3DScene::extrusionentity_to_verts(extrusion_entity, float(support_layer->print_z), m_print_instance.shift, init_data);
            }
        }
        m_support_volume->model.init_from(std::move(init_data));
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", finished extrusionentity_to_verts, update status to 100%";
        print->set_status(100, L("Support Generated"));
        
        record_timestamp();
    }
    catch (...) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ",exception catched, mostly cancelling from gui!";
        //wxTheApp->OnUnhandledException();
    }

_finished:
    std::unique_lock<std::mutex> lck(m_mutex);
    if (m_edit_state == state_generating)
        m_edit_state = state_ready;

    lck.unlock();
    m_parent.set_as_dirty();
    m_parent.post_event(SimpleEvent(wxEVT_PAINT));
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", finished all";
    return;
}

void GLGizmoLcdOverhangDetection::generate_support_volume()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",before finalize_geometry";

    std::unique_lock<std::mutex> lck(m_mutex);
    m_volume_ready = true;
    m_volume_valid = true;
    m_object_id = m_print_instance.print_object->id().id;
    lck.unlock();

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",finished finalize_geometry";
}

// ── Island data layer ──────────────────────────────────────────────────────

float GLGizmoLcdOverhangDetection::accuracy_to_layer_height(DetectionAccuracy acc) const
{
    switch (acc) {
    case Accuracy_High:   return 0.05f;
    case Accuracy_Middle: return 0.10f;
    case Accuracy_Low:    return 0.50f;
    default:              return 0.10f;
    }
}

void GLGizmoLcdOverhangDetection::sync_all_objects_names()
{
    m_model_names.clear();
    const SLAPrint* sla_print = m_parent.sla_print();
    if (!sla_print)
        return;
    for (const SLAPrintObject* po : sla_print->objects()) {
        if (po && po->model_object())
            m_model_names.push_back(po->model_object()->name);
    }
}

void GLGizmoLcdOverhangDetection::sync_island_data_for_object(int obj_idx)
{
    const SLAPrint* sla_print = m_parent.sla_print();
    if (!sla_print)
        return;
    const auto& objs = sla_print->objects();
    if (obj_idx < 0 || obj_idx >= (int)objs.size())
        return;

    const ModelObject* mo = objs[obj_idx]->model_object();
    if (!mo)
        return;

    // Trigger on-demand re-slice at the layer height matching the current accuracy level.
    const float detect_lh = accuracy_to_layer_height(m_detection_accuracy);
    wxGetApp().plater()->sla_print().redetect_islands(mo->id(), detect_lh);

    // Read back the updated island contours.
    m_island_data_per_object[obj_idx] = objs[obj_idx]->island_contours();
}

void GLGizmoLcdOverhangDetection::sync_island_data_for_all()
{
    const SLAPrint* sla_print = m_parent.sla_print();
    if (!sla_print)
        return;
    const auto& objs = sla_print->objects();
    for (int i = 0; i < (int)objs.size(); ++i)
        sync_island_data_for_object(i);
}

void GLGizmoLcdOverhangDetection::rebuild_overhang_area_index_map(bool all_objects)
{
    m_overhang_area_index_map.clear();

    if (all_objects) {
        for (const auto& kv : m_island_data_per_object) {
            int obj_idx = kv.first;
            for (int i = 0; i < (int)kv.second.islands.size(); ++i)
                m_overhang_area_index_map.emplace_back(obj_idx, i);
        }
    } else {
        auto it = m_island_data_per_object.find(m_current_model_index);
        if (it != m_island_data_per_object.end()) {
            for (int i = 0; i < (int)it->second.islands.size(); ++i)
                m_overhang_area_index_map.emplace_back(m_current_model_index, i);
        }
    }

    m_total_overhang_areas       = (int)m_overhang_area_index_map.size();
    m_current_overhang_area_index = 0;
    m_island_data_dirty           = true;
}

// ── Island GL visualization ────────────────────────────────────────────────

// Build extruded-solid geometry (P3N3) for one island into `geo`.
// top_verts / bot_verts: world-space perimeter points at z_top / z_bot.
// Normals: top cap +(0,0,1), bottom cap -(0,0,1), side walls = per-edge outward horizontal.
static void build_island_extrude_p3n3(
    GLModel::Geometry&          geo,
    const Vec3f&                center_top,
    const Vec3f&                center_bot,
    const std::vector<Vec3f>&   top_verts,
    const std::vector<Vec3f>&   bot_verts)
{
    const auto    n   = (unsigned int)top_verts.size();
    const Vec3f   nup { 0.f,  0.f,  1.f };
    const Vec3f   ndn { 0.f,  0.f, -1.f };

    // Top face fan (CCW from above, normal +Z)
    const unsigned int ct = (unsigned int)geo.vertices_count();
    geo.add_vertex(center_top, nup);
    for (const auto& v : top_verts) geo.add_vertex(v, nup);
    for (unsigned int j = 0; j < n; ++j) {
        geo.add_index(ct);
        geo.add_index(ct + 1 + j);
        geo.add_index(ct + 1 + (j + 1) % n);
    }

    // Bottom face fan (reversed winding — CCW from below, normal -Z)
    const unsigned int cb = (unsigned int)geo.vertices_count();
    geo.add_vertex(center_bot, ndn);
    for (const auto& v : bot_verts) geo.add_vertex(v, ndn);
    for (unsigned int j = 0; j < n; ++j) {
        geo.add_index(cb);
        geo.add_index(cb + 1 + (j + 1) % n);
        geo.add_index(cb + 1 + j);
    }

    // Side walls — unshared vertices per edge for flat outward normals.
    // CCW polygon: outward normal of edge j = normalize(dy, -dx, 0) where d = next - curr.
    for (unsigned int j = 0; j < n; ++j) {
        const unsigned int j1  = (j + 1) % n;
        const float        dx  = top_verts[j1].x() - top_verts[j].x();
        const float        dy  = top_verts[j1].y() - top_verts[j].y();
        const float        len = std::sqrt(dx * dx + dy * dy);
        const Vec3f        nrm = (len > 1e-6f) ? Vec3f(dy / len, -dx / len, 0.f)
                                                : Vec3f(0.f, 0.f, 1.f);
        const unsigned int sw  = (unsigned int)geo.vertices_count();
        geo.add_vertex(top_verts[j],  nrm);   // sw+0  TL
        geo.add_vertex(top_verts[j1], nrm);   // sw+1  TR
        geo.add_vertex(bot_verts[j1], nrm);   // sw+2  BR
        geo.add_vertex(bot_verts[j],  nrm);   // sw+3  BL
        // CCW from outside: (TL, BR, TR) then (TL, BL, BR)
        geo.add_index(sw + 0); geo.add_index(sw + 2); geo.add_index(sw + 1);
        geo.add_index(sw + 0); geo.add_index(sw + 3); geo.add_index(sw + 2);
    }
}

void GLGizmoLcdOverhangDetection::rebuild_island_overlay_mesh()
{
    m_island_overlay_model.reset();
    if (m_overhang_area_index_map.empty()) {
        rebuild_island_original_mesh();
        m_island_data_dirty = false;
        return;
    }

    const SLAPrint* sla_print = m_parent.sla_print();
    if (!sla_print) {
        rebuild_island_original_mesh();
        m_island_data_dirty = false;
        return;
    }

    GLModel::Geometry geo;
    geo.format = { GLModel::Geometry::EPrimitiveType::Triangles,
                   GLModel::Geometry::EVertexLayout::P3N3 };

    const auto& objs = sla_print->objects();
    int flat_idx = 0;
    for (const auto& [obj_idx, isl_idx] : m_overhang_area_index_map) {
        const bool is_selected = (flat_idx++ == m_current_overhang_area_index);
        if (is_selected) continue;  // selected island is drawn by highlight model (gray)
        auto it = m_island_data_per_object.find(obj_idx);
        if (it == m_island_data_per_object.end())
            continue;
        const sla::IslandContourSet& cs = it->second;
        if (isl_idx >= (int)cs.islands.size())
            continue;
        const sla::IslandContour& ic = cs.islands[isl_idx];
        const Polygon& poly = ic.contour.contour;
        const size_t n = poly.points.size();
        if (n < 3 || obj_idx >= (int)objs.size())
            continue;

        const SLAPrintObject* po = objs[obj_idx];
        const ModelObject* mo = po->model_object();
        if (!mo || mo->instances.empty())
            continue;
        const Transform3d world_trafo = mo->instances[0]->get_transformation().get_matrix();
        const float z = ic.print_z;

        double cx = 0.0, cy = 0.0;
        for (const Point& p : poly.points) {
            cx += unscale<double>(p.x());
            cy += unscale<double>(p.y());
        }
        cx /= (double)n;
        cy /= (double)n;

        const float z_top = z + m_island_overlay_z_offset;
        const float z_bot = z_top - m_island_overlay_thickness;

        std::vector<Vec3f> top_verts, bot_verts;
        top_verts.reserve(n);
        bot_verts.reserve(n);
        for (const Point& p : poly.points) {
            const double sx = cx + (unscale<double>(p.x()) - cx) * m_island_overlay_scale;
            const double sy = cy + (unscale<double>(p.y()) - cy) * m_island_overlay_scale;
            Vec3d w = world_trafo * Vec3d(sx, sy, 0.0);
            top_verts.emplace_back(float(w.x()), float(w.y()), z_top);
            bot_verts.emplace_back(float(w.x()), float(w.y()), z_bot);
        }

        Vec3d wc = world_trafo * Vec3d(cx, cy, 0.0);
        build_island_extrude_p3n3(geo,
            Vec3f(float(wc.x()), float(wc.y()), z_top),
            Vec3f(float(wc.x()), float(wc.y()), z_bot),
            top_verts, bot_verts);
    }

    if (geo.vertices_count() > 0) {
        m_island_overlay_model.init_from(std::move(geo));
        m_island_overlay_model.set_color(island_overlay_color());
    }
    rebuild_island_original_mesh();
    m_island_data_dirty = false;
}

void GLGizmoLcdOverhangDetection::rebuild_island_original_mesh()
{
    m_island_original_model.reset();
    if (m_overhang_area_index_map.empty())
        return;
    const SLAPrint* sla_print = m_parent.sla_print();
    if (!sla_print)
        return;

    GLModel::Geometry geo;
    geo.format = { GLModel::Geometry::EPrimitiveType::Triangles,
                   GLModel::Geometry::EVertexLayout::P3 };

    const auto& objs = sla_print->objects();
    for (const auto& [obj_idx, isl_idx] : m_overhang_area_index_map) {
        auto it = m_island_data_per_object.find(obj_idx);
        if (it == m_island_data_per_object.end())
            continue;
        const sla::IslandContourSet& cs = it->second;
        if (isl_idx >= (int)cs.islands.size())
            continue;
        const sla::IslandContour& ic = cs.islands[isl_idx];
        const Polygon& poly = ic.contour.contour;
        const size_t n = poly.points.size();
        if (n < 3 || obj_idx >= (int)objs.size())
            continue;

        const SLAPrintObject* po = objs[obj_idx];
        const ModelObject* mo = po->model_object();
        if (!mo || mo->instances.empty())
            continue;
        const Transform3d world_trafo = mo->instances[0]->get_transformation().get_matrix();
        const float z_orig = ic.print_z + m_island_overlay_z_offset;

        double cx = 0.0, cy = 0.0;
        for (const Point& p : poly.points) {
            cx += unscale<double>(p.x());
            cy += unscale<double>(p.y());
        }
        cx /= (double)n;
        cy /= (double)n;

        const unsigned int base = (unsigned int)geo.vertices_count();
        Vec3d wc = world_trafo * Vec3d(cx, cy, 0.0);
        geo.add_vertex(Vec3f(float(wc.x()), float(wc.y()), z_orig));
        for (const Point& p : poly.points) {
            Vec3d w = world_trafo * Vec3d(unscale<double>(p.x()), unscale<double>(p.y()), 0.0);
            geo.add_vertex(Vec3f(float(w.x()), float(w.y()), z_orig));
        }
        for (unsigned int j = 0; j < (unsigned int)n; ++j) {
            geo.add_index(base);
            geo.add_index(base + 1 + j);
            geo.add_index(base + 1 + (j + 1) % (unsigned int)n);
        }
    }

    if (geo.vertices_count() > 0) {
        m_island_original_model.init_from(std::move(geo));
        m_island_original_model.set_color(island_contour_color());
    }
}

void GLGizmoLcdOverhangDetection::rebuild_island_highlight_mesh(int flat_idx)
{
    m_island_highlight_model.reset();
    if (flat_idx < 0 || flat_idx >= (int)m_overhang_area_index_map.size())
        return;

    auto [obj_idx, isl_idx] = m_overhang_area_index_map[flat_idx];
    auto it = m_island_data_per_object.find(obj_idx);
    if (it == m_island_data_per_object.end())
        return;
    const sla::IslandContourSet& cs = it->second;
    if (isl_idx >= (int)cs.islands.size())
        return;
    const sla::IslandContour& ic = cs.islands[isl_idx];
    const Polygon& poly = ic.contour.contour;
    const size_t n = poly.points.size();
    if (n < 3)
        return;
    const SLAPrint* sla_print = m_parent.sla_print();
    if (!sla_print || obj_idx >= (int)sla_print->objects().size())
        return;
    const SLAPrintObject* po_h = sla_print->objects()[obj_idx];
    const ModelObject* mo_h = po_h->model_object();
    if (!mo_h || mo_h->instances.empty())
        return;
    const Transform3d world_trafo_h = mo_h->instances[0]->get_transformation().get_matrix();
    const float z = ic.print_z;

    double cx = 0.0, cy = 0.0;
    for (const Point& p : poly.points) {
        cx += unscale<double>(p.x());
        cy += unscale<double>(p.y());
    }
    cx /= (double)n;
    cy /= (double)n;

    // Extruded gray solid — same geometry as overlay but for the selected island only.
    const float z_top = z + m_island_overlay_z_offset;
    const float z_bot = z_top - m_island_overlay_thickness;

    std::vector<Vec3f> top_verts, bot_verts;
    top_verts.reserve(n);
    bot_verts.reserve(n);
    for (const Point& p : poly.points) {
        const double sx = cx + (unscale<double>(p.x()) - cx) * m_island_overlay_scale;
        const double sy = cy + (unscale<double>(p.y()) - cy) * m_island_overlay_scale;
        Vec3d w = world_trafo_h * Vec3d(sx, sy, 0.0);
        top_verts.emplace_back(float(w.x()), float(w.y()), z_top);
        bot_verts.emplace_back(float(w.x()), float(w.y()), z_bot);
    }

    Vec3d wc = world_trafo_h * Vec3d(cx, cy, 0.0);

    GLModel::Geometry geo;
    geo.format = { GLModel::Geometry::EPrimitiveType::Triangles,
                   GLModel::Geometry::EVertexLayout::P3N3 };

    build_island_extrude_p3n3(geo,
        Vec3f(float(wc.x()), float(wc.y()), z_top),
        Vec3f(float(wc.x()), float(wc.y()), z_bot),
        top_verts, bot_verts);

    m_island_highlight_model.init_from(std::move(geo));
    m_island_highlight_model.set_color(island_selected_color());
}

void GLGizmoLcdOverhangDetection::render_island_contours()
{
    if (m_overhang_area_index_map.empty() && !m_island_data_dirty)
        return;

    if (m_island_data_dirty)
        rebuild_island_overlay_mesh();

    if (!m_island_overlay_model.is_initialized() &&
        !m_island_highlight_model.is_initialized() &&
        !m_island_original_model.is_initialized())
        return;

    const Camera&     camera      = wxGetApp().plater()->get_camera();
    const Transform3d view_matrix = camera.get_view_matrix();

    glsafe(::glEnable(GL_BLEND));
    glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    // Pass 1: extruded solids with gouraud_light (depth test ON, back-face culling ON).
    // Geometry is already in world space → model_matrix = Identity →
    // view_normal_matrix = upper-left 3×3 of view_matrix.
    GLShaderProgram* gouraud = wxGetApp().get_shader("gouraud_light");
    if (gouraud) {
        const Matrix3d view_normal_matrix = view_matrix.matrix().block(0, 0, 3, 3);
        gouraud->start_using();
        gouraud->set_uniform("view_model_matrix",  view_matrix);
        gouraud->set_uniform("projection_matrix",  camera.get_projection_matrix());
        gouraud->set_uniform("view_normal_matrix", view_normal_matrix);
        gouraud->set_uniform("emission_factor",    0.1f);

        if (m_island_overlay_model.is_initialized())
            m_island_overlay_model.render();

        if (m_island_highlight_model.is_initialized() &&
            !m_overhang_area_index_map.empty() &&
            m_current_overhang_area_index >= 0 &&
            m_current_overhang_area_index < (int)m_overhang_area_index_map.size())
            m_island_highlight_model.render();

        gouraud->stop_using();
    }

    // Pass 2: orange original flat contour (P3, flat shader) — depth test OFF so it is
    // always visible regardless of model geometry above the island overhang plane.
    GLShaderProgram* flat_sh = wxGetApp().get_shader("flat");
    if (flat_sh && m_island_original_model.is_initialized()) {
        flat_sh->start_using();
        flat_sh->set_uniform("view_model_matrix", view_matrix);
        flat_sh->set_uniform("projection_matrix", camera.get_projection_matrix());
        glsafe(::glDisable(GL_DEPTH_TEST));
        m_island_original_model.render();
        glsafe(::glEnable(GL_DEPTH_TEST));
        flat_sh->stop_using();
    }

    glsafe(::glDisable(GL_BLEND));
}

void GLGizmoLcdOverhangDetection::generate_island_support_points()
{
    if (m_overhang_area_index_map.empty())
        return;

    const SLAPrint* sla_print = m_parent.sla_print();
    if (!sla_print)
        return;

    // Build SampleConfig from current SLA print settings
    const DynamicPrintConfig& cfg =
        wxGetApp().preset_bundle->sla_prints.get_edited_preset().config;
    const float head_diameter =
        float(cfg.opt<ConfigOptionFloat>("support_head_front_diameter")->value);
    const sla::SampleConfig sample_cfg =
        sla::SampleConfigFactory::create(head_diameter);

    // Group islands by object so we process each object once
    std::map<int, std::vector<int>> by_obj;
    for (int fi = 0; fi < (int)m_overhang_area_index_map.size(); ++fi)
        by_obj[m_overhang_area_index_map[fi].first].push_back(fi);

    const auto& objs = sla_print->objects();
    for (auto& [obj_idx, flat_indices] : by_obj) {
        if (obj_idx >= (int)objs.size())
            continue;
        const SLAPrintObject* po = objs[obj_idx];
        const ModelObject* mo_const = po->model_object();
        if (!mo_const)
            continue;
        ModelObject* mo = wxGetApp().plater()->model().objects[obj_idx];
        if (!mo)
            continue;

        auto it = m_island_data_per_object.find(obj_idx);
        if (it == m_island_data_per_object.end())
            continue;
        const sla::IslandContourSet& cs = it->second;

        // Remove previously injected island-type support points
        sla::SupportPoints& pts = mo->sla_support_points;
        pts.erase(std::remove_if(pts.begin(), pts.end(),
            [](const sla::SupportPoint& p) {
                return p.type == sla::SupportPointType::island;
            }),
            pts.end());

        // island contour XY is in sla_trafo (print) space; ic.print_z is also
        // print-space Z.  GLGizmoSlaSupports::render_points() applies
        // vol->get_instance_transformation() (world trafo), so the stored
        // positions must be in LOCAL MODEL space: apply sla_trafo.inverse().
        const Transform3d trafo_inv = po->trafo().inverse();

        // Generate and inject new island support points
        for (int fi : flat_indices) {
            int isl_idx = m_overhang_area_index_map[fi].second;
            if (isl_idx >= (int)cs.islands.size())
                continue;
            const sla::IslandContour& ic = cs.islands[isl_idx];

            sla::SupportIslandPoints samples =
                sla::uniform_support_island(ic.contour, Points{}, sample_cfg);

            for (const sla::SupportIslandPointPtr& s : samples) {
                // Convert from print space to local model space
                const Vec3d local = trafo_inv * Vec3d(
                    unscale<double>(s->point.x()),
                    unscale<double>(s->point.y()),
                    double(ic.print_z));
                sla::SupportPoint sp(
                    Vec3f(float(local.x()), float(local.y()), float(local.z())),
                    head_diameter / 2.f,
                    sla::SupportPointType::island);
                pts.push_back(sp);
            }
        }

        // Mark as user-modified: pipeline will use our points only (no auto-gen)
        mo->sla_points_status = sla::PointsStatus::UserModified;
    }

    // Switch to SLA support gizmo and force Structure view + support tree build.
    // open_gizmo() is synchronous — data_changed() runs inside it and populates
    // m_normal_cache from mo->sla_support_points before activate_structure_view()
    // is called.
    wxGetApp().CallAfter([this]() {
        GLCanvas3D* canvas = wxGetApp().plater()->canvas3D();
        if (!canvas)
            return;
        canvas->get_gizmos_manager().open_gizmo(GLGizmosManager::EType::SlaSupports);
        GLGizmoBase* base = canvas->get_gizmos_manager().get_gizmo(GLGizmosManager::EType::SlaSupports);
        if (auto* sla = dynamic_cast<GLGizmoSlaSupports*>(base))
            sla->activate_structure_view();
    });
}

void GLGizmoLcdOverhangDetection::focus_camera_on_island(int flat_idx)
{
    if (flat_idx < 0 || flat_idx >= (int)m_overhang_area_index_map.size())
        return;

    auto [obj_idx, isl_idx] = m_overhang_area_index_map[flat_idx];
    auto it = m_island_data_per_object.find(obj_idx);
    if (it == m_island_data_per_object.end())
        return;
    const sla::IslandContourSet& cs = it->second;
    if (isl_idx >= (int)cs.islands.size())
        return;
    const sla::IslandContour& ic = cs.islands[isl_idx];
    const Polygon& poly = ic.contour.contour;
    if (poly.points.empty())
        return;

    // Get world transform for XY (same as rebuild_island_overlay_mesh)
    const SLAPrint* sla_print = m_parent.sla_print();
    if (!sla_print || obj_idx >= (int)sla_print->objects().size())
        return;
    const ModelObject* mo = sla_print->objects()[obj_idx]->model_object();
    if (!mo || mo->instances.empty())
        return;
    const Transform3d world_trafo = mo->instances[0]->get_transformation().get_matrix();

    // Compute 2D bounding box in local coords then transform to world XY
    BoundingBoxf bb2d;
    for (const Point& p : poly.points)
        bb2d.merge(Vec2d(unscale<double>(p.x()), unscale<double>(p.y())));

    // Build 3D AABB: transform corners to world space, Z range = print_z ± 2mm
    const float z = ic.print_z;
    BoundingBoxf3 bb3d;
    for (double xi : {bb2d.min.x(), bb2d.max.x()})
        for (double yi : {bb2d.min.y(), bb2d.max.y()})
            for (float zi : {z - 2.0f, z + 2.0f}) {
                Vec3d w = world_trafo * Vec3d(xi, yi, 0.0);
                bb3d.merge(Vec3d(w.x(), w.y(), (double)zi));
            }

    wxGetApp().plater()->get_camera().zoom_to_box(bb3d);
    m_parent.set_as_dirty();
}

} // namespace Slic3r::GUI
#pragma endregion