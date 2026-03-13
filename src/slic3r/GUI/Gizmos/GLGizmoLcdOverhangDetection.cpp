#pragma region Phrozen LCD Parameter
#include "GLGizmoLcdOverhangDetection.hpp"

#include "libslic3r/Model.hpp"
//BBS
#include "libslic3r/Layer.hpp"
#include "libslic3r/Thread.hpp"

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

#include <boost/log/trivial.hpp>

namespace Slic3r::GUI {

GLGizmoLcdOverhangDetection::GLGizmoLcdOverhangDetection(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoPainterBase(parent, icon_filename, sprite_id), m_current_tool(ImGui::CircleButtonIcon)
{
    m_tool_type = ToolType::BRUSH;
    m_cursor_type = TriangleSelector::CursorType::CIRCLE;
}

bool GLGizmoLcdOverhangDetection::on_is_selectable() const
{
    // Only show when IsPhrozenLCDEditMode is true
    //return wxGetApp().IsPhrozenLCDEditMode();
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
    m_detection_accuracy = Accuracy_Low;
    m_current_model_index = 0;
    m_current_overhang_area_index = 0;
    m_total_overhang_areas = 8;
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
    render_cursor();

    glsafe(::glDisable(GL_BLEND));
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
    }
    else if (get_state() == Off) {
        ModelObject* mo = m_c->selection_info()->model_object();
        if (mo) Slic3r::save_object_mesh(*mo);
    }
}

void GLGizmoLcdOverhangDetection::on_render_input_window(float x, float y, float bottom_limit)
{
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
    const float accuracy_button_width = 118.67; // m_imgui->scaled(8.f);
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
    m_imgui->text(m_desc.at("detection_accuracy"));
    
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

    // Row 3: Model label
    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc.at("model"));
    
    // Row 4: < Model text >
    ImGui::AlignTextToFramePadding();
    
    // Left arrow button
    if (ImGui::Button("<", ImVec2(bracket_button_width, 0))) {
        if (m_current_model_index > 0) {
            m_current_model_index--;
        }
    }
    
    ImGui::SameLine();
    
    // Model text (centered) - 使用實際模型名稱
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
    
    // Right arrow button
    float right_button_x = ImGui::GetCursorPosX();
    ImGui::SetCursorPosX(354.f);
    if (ImGui::Button(">", ImVec2(bracket_button_width, 0))) {
        if (m_current_model_index < static_cast<int>(m_model_names.size()) - 1) {
            m_current_model_index++;
        }
    }
    
    // Row 5: Overhang Area label
    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc.at("overhang_area"));
    
    // Row 6: < 0/12 >
    ImGui::AlignTextToFramePadding();
    
    // Left arrow button
    if (ImGui::Button("<##overhang", ImVec2(bracket_button_width, 0))) {
        if (m_current_overhang_area_index > 0) {
            m_current_overhang_area_index--;
        }
    }
    
    ImGui::SameLine();
    
    // Overhang area text (centered)
    wxString overhang_text = wxString::Format("%d/%d", m_current_overhang_area_index, m_total_overhang_areas);
    float overhang_text_width = m_imgui->calc_text_size(overhang_text).x;
    available_width = ImGui::GetContentRegionAvail().x - button_width * 2 - ImGui::GetStyle().ItemSpacing.x * 2;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available_width - overhang_text_width) * 0.5f);
    m_imgui->text(overhang_text);
    
    ImGui::SameLine();
    
    // Right arrow button
    ImGui::SetCursorPosX(354.f);
    if (ImGui::Button(">##overhang", ImVec2(bracket_button_width, 0))) {
        if (m_current_overhang_area_index < m_total_overhang_areas - 1) {
            m_current_overhang_area_index++;
        }
    }
    
    // Row 7: Separator
    ImGui::Separator();
    
    // Row 8: Tooltip icon and buttons
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
    
    // Tooltip icon
    float get_cur_y = ImGui::GetContentRegionMax().y + ImGui::GetFrameHeight() + y;
    float caption_max = 0.f;
    show_tooltip_information(caption_max, x, get_cur_y + 25);
    
    ImGui::SetCursorPos(ImVec2(x + 30, get_cur_y));
    
    // Detect all button
    if (m_imgui->button(m_desc.at("detect_all"))) {
        // TODO: Implement detect all functionality
    }
    
    ImGui::SameLine();
    
    // Detect selected button
    if (m_imgui->button(m_desc.at("detect_selected"))) {
        // TODO: Implement detect selected functionality
    }
    
    ImGui::SetCursorPosX(x + 30);
    // Add overhang supports button
    if (m_imgui->button(m_desc.at("add_overhang_supports"))) {
        // TODO: Implement add overhang supports functionality
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

    caption_max += m_imgui->calc_text_size(std::string_view{": "}).x + 15.f;

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

    // Phrozen LCD: 初始化模型名稱陣列
    m_model_names.clear();
    int volume_id = -1;
    std::vector<ColorRGBA> ebt_colors;
    ebt_colors.push_back(GLVolume::NEUTRAL_COLOR);
    ebt_colors.push_back(TriangleSelectorGUI::enforcers_color);
    ebt_colors.push_back(TriangleSelectorGUI::blockers_color);
    for (const ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;

        ++volume_id;

        // Phrozen LCD: 儲存模型名稱
        m_model_names.push_back(mv->name);

        // This mesh does not account for the possible Z up SLA offset.
        const TriangleMesh* mesh = &mv->mesh();
        m_triangle_selectors.emplace_back(std::make_unique<TriangleSelectorPatch>(*mesh, ebt_colors));
        // Reset of TriangleSelector is done inside TriangleSelectorGUI's constructor, so we don't need it to perform it again in deserialize().
        m_triangle_selectors.back()->deserialize(mv->supported_facets.get_data(), false);
        m_triangle_selectors.back()->request_update_render_data();

        //BBS: add timestamp logic
        m_volume_timestamps.emplace_back(mv->supported_facets.timestamp());
    }

    // Phrozen LCD: 確保索引不超出範圍
    if (m_current_model_index >= static_cast<int>(m_model_names.size())) {
        m_current_model_index = m_model_names.empty() ? 0 : static_cast<int>(m_model_names.size()) - 1;
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

} // namespace Slic3r::GUI
#pragma endregion