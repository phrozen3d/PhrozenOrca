#include "libslic3r/libslic3r.h"
#include "SLASlice2DCanvas.hpp"

#include "3DScene.hpp"
#include "OpenGLManager.hpp"
#include "GUI_App.hpp"
#include "ImGuiWrapper.hpp"
#include "GLShader.hpp"
#include "GLTexture.hpp"
#include "IMSlider.hpp"
#include "GUI_Colors.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/Tesselate.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Color.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/PrintConfig.hpp"
#include <opencv2/core.hpp>

#include <GL/glew.h>

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui/imgui.h>

#include <wx/glcanvas.h>
#include <algorithm>
#include <cmath>

namespace Slic3r::GUI {

namespace {

// RenderColor is zero-initialized until GLCanvas3D seeds RenderCol_3D_Background — without a fallback the gutter clears to black.
static ImVec4 gutter_clear_color()
{
    const ImVec4& c = RenderColor::colors[RenderCol_3D_Background];
    if (c.w > 0.01f && (c.x > 0.001f || c.y > 0.001f || c.z > 0.001f))
        return c;
    return wxGetApp().dark_mode() ? ImVec4(0.329f, 0.329f, 0.353f, 1.f)  // GLCanvas3D DEFAULT_BG_LIGHT_COLOR_DARK
                                  : ImVec4(0.906f, 0.906f, 0.906f, 1.f); // GLCanvas3D default light
}

static wxColour gutter_wx_colour()
{
    const ImVec4 g = gutter_clear_color();
    return wxColour(
        (unsigned char)std::clamp(int(g.x * 255.f + 0.5f), 0, 255),
        (unsigned char)std::clamp(int(g.y * 255.f + 0.5f), 0, 255),
        (unsigned char)std::clamp(int(g.z * 255.f + 0.5f), 0, 255));
}

Transform3d ortho_2d(double left, double right, double bottom, double top)
{
    Transform3d t;
    Eigen::Matrix4d m = Eigen::Matrix4d::Zero();
    const double rl = right - left;
    const double tb = top - bottom;
    if (rl > 1e-10 && tb > 1e-10) {
        m(0, 0) = 2.0 / rl;
        m(1, 1) = 2.0 / tb;
        m(0, 3) = -(right + left) / rl;
        m(1, 3) = -(top + bottom) / tb;
        m(2, 2) = -1.0;
        m(3, 3) = 1.0;
    } else {
        m.setIdentity();
    }
    t.matrix() = m;
    return t;
}

// Match the left 3D preview: bed footprint uses printable_area (bed XY), not the LCD buffer after
// display_orientation swap. Raster layer_images are still in "panel" orientation; we rotate in
// render_texture_letterbox when sladoPortrait so the slice aligns with this aspect.
static double platform_aspect_w_over_h(const SLAPrint* print)
{
    if (print == nullptr)
        return 1.0;
    const SLAPrinterConfig& cfg = print->printer_config();
    if (!cfg.printable_area.values.empty()) {
        Points bed_pts;
        bed_pts.reserve(cfg.printable_area.values.size());
        for (const Vec2d& v : cfg.printable_area.values)
            bed_pts.emplace_back(scaled(v.x()), scaled(v.y()));
        const BoundingBox bb(bed_pts);
        const double      bw = unscale<double>(bb.size().x());
        const double      bh = unscale<double>(bb.size().y());
        if (bw < 1e-9 || bh < 1e-9)
            return 1.0;
        return bw / bh;
    }
    double w_mm = cfg.display_width.getFloat();
    double h_mm = cfg.display_height.getFloat();
    if (w_mm < 1e-6 || h_mm < 1e-6)
        return 1.0;
    return w_mm / h_mm;
}

} // namespace

void SLASlice2DCanvas::compute_slice_portal(int vp_w, int vp_h, double platform_ar, int& px, int& py, int& pw, int& ph)
{
    if (platform_ar < 1e-9 || platform_ar > 1e9)
        platform_ar = 1.0;
    const int em      = std::max(8, wxGetApp().em_unit());
    const int inner_w = std::max(1, vp_w - 2 * em);
    const int inner_h = std::max(1, vp_h - 2 * em);
    const int box_w   = std::max(1, (inner_w * 72) / 100);
    const int box_h   = std::max(1, (inner_h * 72) / 100);

    const double box_ar = double(box_w) / double(box_h);
    if (box_ar > platform_ar) {
        ph = box_h;
        pw = std::max(1, int(std::lround(double(ph) * platform_ar)));
        if (pw > box_w) {
            pw = box_w;
            ph = std::max(1, int(std::lround(double(pw) / platform_ar)));
        }
    } else {
        pw = box_w;
        ph = std::max(1, int(std::lround(double(pw) / platform_ar)));
        if (ph > box_h) {
            ph = box_h;
            pw = std::max(1, int(std::lround(double(ph) * platform_ar)));
        }
    }

    px = (vp_w - pw) / 2;
    py = (vp_h - ph) / 2;
}

// zoom=1 → base portal size from compute_slice_portal; pan is offset from centered position (after zoom).
static void slice_portal_rect(int vp_w, int vp_h, double platform_ar, float zoom, float pan_px, float pan_py, int& px, int& py, int& pw, int& ph)
{
    int pxb, pyb, pwb, phb;
    SLASlice2DCanvas::compute_slice_portal(vp_w, vp_h, platform_ar, pxb, pyb, pwb, phb);
    (void)pxb;
    (void)pyb;
    pw = std::max(1, int(std::lround(double(pwb) * double(zoom))));
    ph = std::max(1, int(std::lround(double(phb) * double(zoom))));
    const int px_c = (vp_w - pw) / 2;
    const int py_c = (vp_h - ph) / 2;
    px             = px_c + int(std::lround(pan_px));
    py             = py_c + int(std::lround(pan_py));
}

static void clamp_zoom(float& zoom, int vp_w, int vp_h, double platform_ar)
{
    int px, py, pwb, phb;
    SLASlice2DCanvas::compute_slice_portal(vp_w, vp_h, platform_ar, px, py, pwb, phb);
    (void)px;
    (void)py;
    if (pwb <= 0 || phb <= 0)
        return;
    const double zmax_fit = std::min(double(vp_w) / double(pwb), double(vp_h) / double(phb));
    const double zhi      = std::max(1e-3, std::min(8.0, zmax_fit - 1e-6));
    const double zlo      = std::min(0.25, zhi);
    zoom                  = float(std::clamp(double(zoom), zlo, zhi));
}

static void clamp_slice_pan(float& pan_px, float& pan_py, int vp_w, int vp_h, float zoom, double platform_ar)
{
    int px_c, py_c, pw, ph;
    slice_portal_rect(vp_w, vp_h, platform_ar, zoom, 0.f, 0.f, px_c, py_c, pw, ph);
    const float minx = float(-px_c);
    const float maxx = float(vp_w - pw - px_c);
    const float miny = float(-py_c);
    const float maxy = float(vp_h - ph - py_c);
    pan_px           = std::clamp(pan_px, minx, maxx);
    pan_py           = std::clamp(pan_py, miny, maxy);
}

static bool point_in_slice_portal(float mx, float my, int vp_w, int vp_h, float pan_px, float pan_py, float zoom, double platform_ar)
{
    int px, py, pw, ph;
    slice_portal_rect(vp_w, vp_h, platform_ar, zoom, pan_px, pan_py, px, py, pw, ph);
    return mx >= float(px) && my >= float(py) && mx < float(px + pw) && my < float(py + ph);
}

SLASlice2DCanvas::SLASlice2DCanvas(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL)
{
#ifdef _WIN32
    wxGetApp().UpdateDarkUI(this);
#endif
    SetBackgroundColour(gutter_wx_colour());
    m_gl = OpenGLManager::create_wxglcanvas(*this);
    m_gl->SetBackgroundColour(gutter_wx_colour());
    wxBoxSizer* sz = new wxBoxSizer(wxVERTICAL);
    sz->Add(m_gl, 1, wxEXPAND);
    SetSizer(sz);

    m_gl->Bind(wxEVT_PAINT, &SLASlice2DCanvas::on_gl_paint, this);
    m_gl->Bind(wxEVT_SIZE, &SLASlice2DCanvas::on_size, this);
    m_gl->Bind(wxEVT_MOUSEWHEEL, &SLASlice2DCanvas::on_mouse_wheel, this);
    m_gl->Bind(wxEVT_MOTION, &SLASlice2DCanvas::on_gl_mouse_motion, this);
    m_gl->Bind(wxEVT_ENTER_WINDOW, &SLASlice2DCanvas::on_gl_mouse_motion, this);
    m_gl->Bind(wxEVT_LEAVE_WINDOW, &SLASlice2DCanvas::on_gl_leave, this);
    m_gl->Bind(wxEVT_LEFT_DOWN, &SLASlice2DCanvas::on_gl_mouse_button, this);
    m_gl->Bind(wxEVT_LEFT_UP, &SLASlice2DCanvas::on_gl_mouse_button, this);
    m_gl->Bind(wxEVT_RIGHT_DOWN, &SLASlice2DCanvas::on_gl_mouse_button, this);
    m_gl->Bind(wxEVT_RIGHT_UP, &SLASlice2DCanvas::on_gl_mouse_button, this);
    m_gl->Bind(wxEVT_MIDDLE_DOWN, &SLASlice2DCanvas::on_gl_mouse_button, this);
    m_gl->Bind(wxEVT_MIDDLE_UP, &SLASlice2DCanvas::on_gl_mouse_button, this);
}

SLASlice2DCanvas::~SLASlice2DCanvas()
{
    if (m_imgui_ctx_sla != nullptr) {
        if (m_imgui_ctx_main != nullptr && ImGui::GetCurrentContext() == m_imgui_ctx_sla)
            ImGui::SetCurrentContext(m_imgui_ctx_main);
        ImGui::DestroyContext(m_imgui_ctx_sla);
        m_imgui_ctx_sla = nullptr;
    }
    if (m_gl != nullptr) {
        m_gl->SetCurrent(*wxGetApp().init_glcontext(*m_gl));
        destroy_texture();
    }
}

void SLASlice2DCanvas::ensure_sla_imgui_context(ImGuiContext* main_ctx)
{
    if (m_imgui_ctx_sla != nullptr || main_ctx == nullptr)
        return;

    ImGui::SetCurrentContext(main_ctx);
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    if (atlas == nullptr || !atlas->IsBuilt())
        return;

    const ImGuiStyle main_style = ImGui::GetStyle();
    m_imgui_ctx_sla = ImGui::CreateContext(atlas);
    ImGui::SetCurrentContext(m_imgui_ctx_sla);
    ImGui::GetStyle()   = main_style;
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::SetCurrentContext(main_ctx);
}

void SLASlice2DCanvas::render_layers_imgui_overlay(int vp_w, int vp_h)
{
    if (m_im_layers_slider == nullptr || !m_im_layers_slider->IsShown())
        return;

    ImGuiContext* main_ctx = ImGui::GetCurrentContext();
    if (main_ctx == nullptr)
        return;
    m_imgui_ctx_main = main_ctx;

    const float          main_font_scale = ImGui::GetIO().FontGlobalScale;
    const ImVec2         main_fb_scale   = ImGui::GetIO().DisplayFramebufferScale;

    ensure_sla_imgui_context(main_ctx);
    if (m_imgui_ctx_sla == nullptr)
        return;

    ImGui::SetCurrentContext(m_imgui_ctx_sla);
    ImGuiIO& io = ImGui::GetIO();

    io.DisplaySize             = ImVec2(float(vp_w), float(vp_h));
    io.DisplayFramebufferScale = main_fb_scale;
    io.FontGlobalScale         = main_font_scale;

    const auto now = std::chrono::steady_clock::now();
    if (m_imgui_have_last_frame) {
        const float dt = std::chrono::duration<float>(now - m_imgui_last_frame).count();
        io.DeltaTime = std::max(1e-4f, dt);
    } else {
        io.DeltaTime = 1.f / 60.f;
    }
    m_imgui_last_frame     = now;
    m_imgui_have_last_frame = true;

    io.MousePos = ImVec2(m_imgui_mouse_x, m_imgui_mouse_y);
    io.MouseDown[0] = m_imgui_mouse_btn[0];
    io.MouseDown[1] = m_imgui_mouse_btn[1];
    io.MouseDown[2] = m_imgui_mouse_btn[2];

    io.MouseWheel  = m_imgui_wheel_accum;
    io.MouseWheelH = 0.f;
    m_imgui_wheel_accum = 0.f;

    ImGui::NewFrame();
    if (m_pan_dragging)
        io.MouseDown[0] = false;
    m_im_layers_slider->render(vp_w, vp_h);
    ImGui::Render();
    wxGetApp().imgui()->render_imgui_draw_data(ImGui::GetDrawData());
    m_imgui_wants_capture_mouse = io.WantCaptureMouse;

    ImGui::SetCurrentContext(main_ctx);
}

void SLASlice2DCanvas::set_sla_print(const SLAPrint* print)
{
    m_print = print;
    m_cached_layer = -1;
    m_vector_cached_layer = -1;
    m_vector_model.reset();
    m_zoom     = 1.f;
    m_pan_px = m_pan_py = 0.f;
    m_pan_dragging = false;
    if (m_gl != nullptr)
        m_gl->Refresh();
}

void SLASlice2DCanvas::set_view_layer_index(int idx)
{
    m_layer_idx = std::max(0, idx);
    if (m_gl != nullptr)
        m_gl->Refresh();
}

void SLASlice2DCanvas::reset_print()
{
    detach_layers_slider();
    m_print = nullptr;
    m_layer_idx = 0;
    m_cached_layer = -1;
    m_vector_cached_layer = -1;
    m_zoom       = 1.f;
    m_pan_px = m_pan_py = 0.f;
    m_pan_dragging = false;
    if (m_gl != nullptr) {
        m_gl->SetCurrent(*wxGetApp().init_glcontext(*m_gl));
        destroy_texture();
        m_vector_model.reset();
        m_gl->Refresh();
    }
}

void SLASlice2DCanvas::attach_layers_slider(IMSlider* layers_slider)
{
    m_im_layers_slider = layers_slider;
    if (m_gl != nullptr)
        m_gl->Refresh();
}

void SLASlice2DCanvas::detach_layers_slider()
{
    m_im_layers_slider = nullptr;
    m_imgui_have_last_frame = false;
    m_imgui_wants_capture_mouse = false;
}

void SLASlice2DCanvas::on_gl_mouse_motion(wxMouseEvent& evt)
{
    m_imgui_mouse_x = float(evt.GetX());
    m_imgui_mouse_y = float(evt.GetY());
    if (m_pan_dragging && m_gl != nullptr) {
        m_pan_px += m_imgui_mouse_x - m_pan_anchor_x;
        m_pan_py += m_imgui_mouse_y - m_pan_anchor_y;
        m_pan_anchor_x = m_imgui_mouse_x;
        m_pan_anchor_y = m_imgui_mouse_y;
        {
            const wxSize cs = m_gl->GetClientSize();
            const int cw = std::max(1, cs.GetWidth());
            const int ch = std::max(1, cs.GetHeight());
            clamp_slice_pan(m_pan_px, m_pan_py, cw, ch, m_zoom, platform_aspect_w_over_h(m_print));
        }
        m_gl->Refresh();
        return;
    }
    // ImGui slider sits on the right; refreshing on every mousemove forces full GL+ImGui work even
    // when the cursor is only over the slice — limit to the slider strip (same band as hit-test).
    if (m_im_layers_slider != nullptr && m_im_layers_slider->IsShown() && m_gl != nullptr) {
        const wxSize cs = m_gl->GetClientSize();
        const int      cw = std::max(1, cs.GetWidth());
        const float      band = float(wxGetApp().em_unit()) * 14.f;
        if (m_imgui_mouse_x >= float(cw) - band)
            m_gl->Refresh();
    }
}

void SLASlice2DCanvas::on_gl_leave(wxMouseEvent& evt)
{
    (void)evt;
    if (m_pan_dragging && m_gl != nullptr && m_gl->HasCapture())
        m_gl->ReleaseMouse();
    m_pan_dragging = false;
    m_imgui_mouse_x = -1.f;
    m_imgui_mouse_y = -1.f;
    if (m_im_layers_slider != nullptr && m_im_layers_slider->IsShown())
        m_gl->Refresh();
}

void SLASlice2DCanvas::on_gl_mouse_button(wxMouseEvent& evt)
{
    const wxSize cs = m_gl ? m_gl->GetClientSize() : wxSize(1, 1);
    const int      cw = std::max(1, cs.GetWidth());
    const int      ch = std::max(1, cs.GetHeight());

    m_imgui_mouse_x = float(evt.GetX());
    m_imgui_mouse_y = float(evt.GetY());
    const float band  = float(wxGetApp().em_unit()) * 14.f;
    const bool  over_slider = m_imgui_mouse_x >= float(cw) - band;
    const double par        = platform_aspect_w_over_h(m_print);

    if (evt.LeftDown()) {
        if (!over_slider && point_in_slice_portal(m_imgui_mouse_x, m_imgui_mouse_y, cw, ch, m_pan_px, m_pan_py, m_zoom, par)) {
            m_pan_dragging   = true;
            m_pan_anchor_x   = m_imgui_mouse_x;
            m_pan_anchor_y   = m_imgui_mouse_y;
            m_imgui_mouse_btn[0] = false;
            if (m_gl != nullptr)
                m_gl->CaptureMouse();
        } else {
            m_imgui_mouse_btn[0] = true;
        }
    }
    if (evt.LeftUp()) {
        if (m_pan_dragging) {
            m_pan_dragging = false;
            if (m_gl != nullptr && m_gl->HasCapture())
                m_gl->ReleaseMouse();
        }
        m_imgui_mouse_btn[0] = false;
    }
    if (evt.RightDown())
        m_imgui_mouse_btn[1] = true;
    if (evt.RightUp())
        m_imgui_mouse_btn[1] = false;
    if (evt.MiddleDown())
        m_imgui_mouse_btn[2] = true;
    if (evt.MiddleUp())
        m_imgui_mouse_btn[2] = false;
    if (m_im_layers_slider != nullptr && m_im_layers_slider->IsShown())
        m_gl->Refresh();
}

void SLASlice2DCanvas::on_size(wxSizeEvent& evt)
{
    evt.Skip();
    if (m_gl != nullptr)
        m_gl->Refresh();
}

void SLASlice2DCanvas::on_mouse_wheel(wxMouseEvent& evt)
{
    m_imgui_mouse_x = float(evt.GetX());
    m_imgui_mouse_y = float(evt.GetY());

    const wxSize cs = m_gl ? m_gl->GetClientSize() : wxSize(0, 0);
    const int cw = std::max(1, cs.GetWidth());
    const int ch = std::max(1, cs.GetHeight());
    const float band = float(wxGetApp().em_unit()) * 14.f;
    const bool   over_slider = m_imgui_mouse_x >= float(cw) - band;
    const double par         = platform_aspect_w_over_h(m_print);
    const bool   in_portal   = point_in_slice_portal(m_imgui_mouse_x, m_imgui_mouse_y, cw, ch, m_pan_px, m_pan_py, m_zoom, par);

    if (m_im_layers_slider != nullptr && m_im_layers_slider->IsShown() && m_imgui_ctx_sla != nullptr &&
        (m_imgui_wants_capture_mouse || over_slider)) {
        m_imgui_wheel_accum += float(evt.GetWheelRotation()) / float(evt.GetWheelDelta());
        if (m_gl != nullptr)
            m_gl->Refresh();
        return;
    }

    if (in_portal) {
        const double delta = evt.GetWheelRotation() / double(evt.GetWheelDelta());
        m_zoom = float(std::clamp(double(m_zoom) * (1.0 + 0.15 * delta), 0.25, 8.0));
        clamp_zoom(m_zoom, cw, ch, par);
        clamp_slice_pan(m_pan_px, m_pan_py, cw, ch, m_zoom, par);
        if (m_gl != nullptr)
            m_gl->Refresh();
    }
}

void SLASlice2DCanvas::destroy_texture()
{
    if (m_tex_id != 0) {
        glsafe(::glDeleteTextures(1, &m_tex_id));
        m_tex_id = 0;
        m_tex_w = m_tex_h = 0;
    }
}

void SLASlice2DCanvas::upload_grayscale_texture(const unsigned char* data, int w, int h)
{
    if (w <= 0 || h <= 0 || data == nullptr)
        return;

    std::vector<unsigned char> rgba(size_t(w) * size_t(h) * 4);
    const size_t n = size_t(w) * size_t(h);
    for (size_t i = 0; i < n; ++i) {
        const unsigned char v = data[i];
        rgba[4 * i + 0] = v;
        rgba[4 * i + 1] = v;
        rgba[4 * i + 2] = v;
        rgba[4 * i + 3] = 255;
    }

    if (m_tex_id == 0)
        glsafe(::glGenTextures(1, &m_tex_id));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_tex_id));
    glsafe(::glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
    glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data()));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    glsafe(::glBindTexture(GL_TEXTURE_2D, 0));
    m_tex_w = w;
    m_tex_h = h;
}

void SLASlice2DCanvas::render_texture_letterbox(int portal_w, int portal_h)
{
    if (m_tex_id == 0 || m_tex_w <= 0 || m_tex_h <= 0 || portal_w <= 0 || portal_h <= 0)
        return;

    const bool portrait_lcd =
        m_print != nullptr && m_print->printer_config().display_orientation.value == sladoPortrait;
    // layer_images are stored like SLAPrintSteps (swapped dims in portrait). For a portal sized to
    // bed XY, the axis-aligned footprint after a 90° on-screen rotation uses h:w as aspect.
    const double aspect_img = portrait_lcd ? double(m_tex_h) / double(m_tex_w)
                                           : double(m_tex_w) / double(m_tex_h);
    const double aspect_vp  = double(portal_w) / double(portal_h);
    double draw_w = portal_w;
    double draw_h = portal_h;
    if (aspect_vp > aspect_img)
        draw_w = double(portal_h) * aspect_img;
    else
        draw_h = double(portal_w) / aspect_img;

    // Zoom scales the whole portal in render(); letterbox fills current portal only.
    const double cx = double(portal_w) * 0.5;
    const double cy = double(portal_h) * 0.5;
    const double left_px   = cx - 0.5 * draw_w;
    const double right_px  = cx + 0.5 * draw_w;
    const double bottom_px = cy - 0.5 * draw_h;
    const double top_px    = cy + 0.5 * draw_h;

    auto to_ndc_x = [portal_w](double px) {
        return float(2.0 * px / double(portal_w) - 1.0);
    };
    auto to_ndc_y = [portal_h](double py) {
        return float(1.0 - 2.0 * py / double(portal_h));
    };

    const float l = to_ndc_x(left_px);
    const float r = to_ndc_x(right_px);
    const float b = to_ndc_y(bottom_px);
    const float t = to_ndc_y(top_px);

    if (portrait_lcd) {
        // 90° CW in texture space: maps panel-tall buffer to bed-horizontal preview (see FullTextureUVs order).
        static const GLTexture::Quad_UVs uvs_rot90_cw = {
            {1.0f, 1.0f},
            {1.0f, 0.0f},
            {0.0f, 0.0f},
            {0.0f, 1.0f},
        };
        GLTexture::render_sub_texture(m_tex_id, l, r, b, t, uvs_rot90_cw);
    } else {
        GLTexture::render_texture(m_tex_id, l, r, b, t);
    }
}

void SLASlice2DCanvas::build_vector_model_from_polys(const ExPolygons& polys)
{
    m_vector_model.reset();
    if (polys.empty())
        return;

    std::vector<Vec3d> tri = triangulate_expolygons_3d(polys, 0., NORMALS_UP);
    if (tri.empty())
        return;

    GLModel::Geometry init_data;
    init_data.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3 };
    init_data.color = ColorRGBA(1.f, 1.f, 1.f, 1.f);
    init_data.reserve_vertices(tri.size());
    for (const Vec3d& v : tri)
        init_data.add_vertex((Vec3f)v.cast<float>());
    for (unsigned int i = 0; i < unsigned(tri.size()); i += 3)
        init_data.add_triangle(i, i + 1, i + 2);
    if (!init_data.is_empty())
        m_vector_model.init_from(std::move(init_data));
}

void SLASlice2DCanvas::render_vector_fallback(int portal_w, int portal_h)
{
    if (m_print == nullptr || portal_w <= 0 || portal_h <= 0)
        return;

    ExPolygons polys;

    const auto& pls = m_print->print_layers();
    if (!pls.empty() && m_layer_idx >= 0 && size_t(m_layer_idx) < pls.size())
        polys = pls[size_t(m_layer_idx)].transformed_slices();

    if (polys.empty()) {
        ExPolygons acc;
        for (const SLAPrintObject* o : m_print->objects()) {
            if (!o->is_step_done(slaposObjectSlice))
                continue;
            const auto& si = o->get_slice_index();
            if (m_layer_idx < 0 || size_t(m_layer_idx) >= si.size())
                continue;
            const SliceRecord& sr = si[size_t(m_layer_idx)];
            for (const SliceOrigin origin : { soModel, soSupport }) {
                const ExPolygons& layer = sr.get_slice(origin);
                for (const ExPolygon& p : layer)
                    acc.emplace_back(p);
            }
        }
        if (!acc.empty())
            polys = union_ex(acc);
    }

    if (m_vector_cached_layer != m_layer_idx) {
        build_vector_model_from_polys(polys);
        m_vector_cached_layer = m_layer_idx;
    }

    if (!m_vector_model.is_initialized() || polys.empty())
        return;

    const SLAPrinterConfig& cfg = m_print->printer_config();
    Transform3d             proj;
    // Match left 3D preview: ortho to printable_area bbox in bed mm (same XY as the plate outline).
    if (!cfg.printable_area.values.empty()) {
        Points bed_pts;
        bed_pts.reserve(cfg.printable_area.values.size());
        for (const Vec2d& v : cfg.printable_area.values)
            bed_pts.emplace_back(scaled(v.x()), scaled(v.y()));
        const BoundingBox plate_bb(bed_pts);
        const double      min_x = unscale<double>(plate_bb.min.x());
        const double      max_x = unscale<double>(plate_bb.max.x());
        const double      min_y = unscale<double>(plate_bb.min.y());
        const double      max_y = unscale<double>(plate_bb.max.y());
        proj                    = ortho_2d(min_x, max_x, min_y, max_y);
    } else {
        const BoundingBox bbox = get_extents(polys);
        const double      min_x = unscale<double>(bbox.min.x());
        const double      max_x = unscale<double>(bbox.max.x());
        const double      min_y = unscale<double>(bbox.min.y());
        const double      max_y = unscale<double>(bbox.max.y());
        const double      w     = max_x - min_x;
        const double      h     = max_y - min_y;
        const double      margin = 0.02 * std::max(std::max(w, h), 1e-6);
        double            cx    = 0.5 * (min_x + max_x);
        double            cy    = 0.5 * (min_y + max_y);
        double            hw    = 0.5 * w + margin;
        double            hh    = 0.5 * h + margin;
        if (hw < 1e-9)
            hw = 1e-9;
        if (hh < 1e-9)
            hh = 1e-9;
        proj = ortho_2d(cx - hw, cx + hw, cy - hh, cy + hh);
    }

    GLShaderProgram* shader = wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;
    shader->start_using();
    shader->set_uniform("view_model_matrix", Transform3d::Identity());
    shader->set_uniform("projection_matrix", proj);
    m_vector_model.render();
    shader->stop_using();
}

void SLASlice2DCanvas::render()
{
    if (m_gl == nullptr)
        return;

    m_gl->SetCurrent(*wxGetApp().init_glcontext(*m_gl));

    const wxSize sz = m_gl->GetClientSize();
    const int vp_w = std::max(1, sz.GetWidth());
    const int vp_h = std::max(1, sz.GetHeight());

    const double platform_ar = platform_aspect_w_over_h(m_print);
    clamp_zoom(m_zoom, vp_w, vp_h, platform_ar);
    clamp_slice_pan(m_pan_px, m_pan_py, vp_w, vp_h, m_zoom, platform_ar);

    int px, py, pw, ph;
    slice_portal_rect(vp_w, vp_h, platform_ar, m_zoom, m_pan_px, m_pan_py, px, py, pw, ph);
    const GLint vx = GLint(px);
    const GLint vy = GLint(vp_h - py - ph);

    glsafe(::glViewport(0, 0, vp_w, vp_h));
    // Match left 3D preview gutter; slice portal clears to black below.
    {
        const ImVec4 bg = gutter_clear_color();
        glsafe(::glClearColor(bg.x, bg.y, bg.z, bg.w));
    }
    glsafe(::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
    glsafe(::glDisable(GL_DEPTH_TEST));
    glsafe(::glDisable(GL_CULL_FACE));

    glsafe(::glEnable(GL_SCISSOR_TEST));
    glsafe(::glScissor(vx, vy, pw, ph));
    glsafe(::glClearColor(0.f, 0.f, 0.f, 1.f));
    glsafe(::glClear(GL_COLOR_BUFFER_BIT));
    glsafe(::glDisable(GL_SCISSOR_TEST));

    glsafe(::glViewport(vx, vy, pw, ph));

    // Hide raster/vector slice until slapsRasterize finishes (layer_images + consistent preview).
    const bool slice_geometry_ready =
        m_print != nullptr && m_print->is_step_done(slapsRasterize);
    if (!slice_geometry_ready) {
        if (m_tex_id != 0) {
            destroy_texture();
            m_cached_layer = -1;
        }
    } else {
        bool drew = false;
        const auto& limgs = m_print->layer_images();
        if (!limgs.empty() && m_layer_idx >= 0 && size_t(m_layer_idx) < limgs.size()) {
            const cv::Mat& m = limgs[size_t(m_layer_idx)];
            if (!m.empty() && m.type() == CV_8UC1) {
                if (m_cached_layer != m_layer_idx) {
                    destroy_texture();
                    m_cached_layer = m_layer_idx;
                    // countNonZero scans every pixel — only when the layer changes, not every wxPaint.
                    if (cv::countNonZero(m) > 0)
                        upload_grayscale_texture(m.ptr<unsigned char>(), m.cols, m.rows);
                }
                if (m_tex_id != 0) {
                    render_texture_letterbox(pw, ph);
                    drew = true;
                }
            }
        }

        if (!drew)
            render_vector_fallback(pw, ph);
    }

    glsafe(::glViewport(0, 0, vp_w, vp_h));
    render_layers_imgui_overlay(vp_w, vp_h);

    m_gl->SwapBuffers();
}

void SLASlice2DCanvas::on_gl_paint(wxPaintEvent& evt)
{
    wxPaintDC dc(m_gl);
    (void)dc;
    render();
}

} // namespace Slic3r::GUI
