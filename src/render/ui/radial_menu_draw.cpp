#include "render/ui/radial_menu_draw.h"

#include "config/radial_config.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace radial_menu_mod::radial_menu {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kBaseViewportHeight = 1080.0f;

struct RadialLayout {
    const radial_config::RadialConfig* config = nullptr;
    ImGuiViewport* viewport = nullptr;
    ImVec2 center{};
    ImVec2 bottom_right{};
    float ui_scale = 1.0f;
    float wheel_inner_radius = 0.0f;
    float wheel_outer_radius = 0.0f;
    float central_panel_outer_radius = 0.0f;
    float slot_inner_radius = 0.0f;
    float slot_outer_radius = 0.0f;
    float icon_size = 0.0f;
    float segment_gap_radians = 0.0f;
    float opacity = 1.0f;
    float screen_dim_opacity = 0.28f;
    bool wheel_hidden = false;
    bool central_panel_hidden = false;
    bool slot_details = true;
    bool show_controls = true;
};

struct SelectedDetailsCache {
    const ImFont* font = nullptr;
    float font_size = 0.0f;
    float wrap_width = 0.0f;
    std::uint32_t id = 0;
    bool occupied = false;
    bool is_item = false;
    std::string name;
    std::vector<std::string> label_lines;
    bool valid = false;
};

SelectedDetailsCache g_selected_details_cache = {};

std::string FormatSlotLabel(const RadialSlot& slot)
{
    if (!slot.occupied) return "Empty";
    if (!slot.name.empty()) return slot.name;

    char buffer[64] = {};
    std::snprintf(buffer, sizeof(buffer), "%s %u", slot.is_item ? "Item" : "Spell", slot.id);
    return buffer;
}

const char* GetCategoryLabel(const RadialSlot& slot)
{
    if (slot.is_item) return "ITEM";

    switch (slot.category) {
    case SpellCategory::sorcery:
        return "SORCERY";
    case SpellCategory::incantation:
        return "INCANTATION";
    case SpellCategory::unknown:
    default:
        return "SPELL";
    }
}

const radial_config::Color& GetSelectedSlotBorderColor(const RadialSlot& slot, const radial_config::RadialConfig& config)
{
    switch (slot.category) {
    case SpellCategory::sorcery:
        return config.slot_selected_sorcery_border_color;
    case SpellCategory::incantation:
        return config.slot_selected_incantation_border_color;
    case SpellCategory::unknown:
    default:
        return config.slot_border_color;
    }
}

ImU32 ColorWithOpacity(const radial_config::Color& color, float opacity, float alpha_scale = 1.0f)
{
    const int alpha = std::clamp(
        static_cast<int>(std::round(static_cast<float>(color.a) * opacity * alpha_scale)), 0, 255);
    return IM_COL32(color.r, color.g, color.b, alpha);
}

ImVec2 PolarPoint(const ImVec2& center, float angle, float radius)
{
    return {center.x + (std::cos(angle) * radius), center.y + (std::sin(angle) * radius)};
}

void AddRingSegment(ImDrawList* draw_list, const ImVec2& center, float inner_radius, float outer_radius,
    float start_angle, float end_angle, ImU32 fill, ImU32 border, float border_thickness)
{
    const int segments = std::max(12, static_cast<int>((end_angle - start_angle) * 18.0f));
    draw_list->PathClear();
    draw_list->PathArcTo(center, outer_radius, start_angle, end_angle, segments);
    draw_list->PathArcTo(center, inner_radius, end_angle, start_angle, segments);
    draw_list->PathFillConcave(fill);

    draw_list->PathClear();
    draw_list->PathArcTo(center, outer_radius, start_angle, end_angle, segments);
    draw_list->PathArcTo(center, inner_radius, end_angle, start_angle, segments);
    draw_list->PathStroke(border, ImDrawFlags_Closed, border_thickness);
}

void AddCircleRing(ImDrawList* draw_list, const ImVec2& center, float inner_radius, float outer_radius,
    ImU32 color)
{
    const float thickness = std::max(0.0f, outer_radius - inner_radius);
    if (thickness <= 0.0f) return;
    draw_list->AddCircle(center, inner_radius + (thickness * 0.5f), color, 72, thickness);
}

void AddSegmentSeparator(ImDrawList* draw_list, const ImVec2& center, float angle, float inner_radius,
    float outer_radius, ImU32 color, float thickness)
{
    draw_list->AddLine(PolarPoint(center, angle, inner_radius), PolarPoint(center, angle, outer_radius), color, thickness);
}

void AddArcStroke(ImDrawList* draw_list, const ImVec2& center, float radius, float start_angle, float end_angle,
    ImU32 color, float thickness)
{
    const int segments = std::max(8, static_cast<int>((end_angle - start_angle) * 14.0f));
    draw_list->PathClear();
    draw_list->PathArcTo(center, radius, start_angle, end_angle, segments);
    draw_list->PathStroke(color, 0, thickness);
}

void AddCenteredText(ImDrawList* draw_list, ImFont* font, float font_size, const ImVec2& center, float y,
    ImU32 color, const char* text)
{
    const ImVec2 text_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, text);
    draw_list->AddText(font, font_size, {center.x - (text_size.x * 0.5f), y}, color, text);
}

std::vector<std::string> WrapTextLines(ImFont* font, float font_size, const std::string& text, float wrap_width,
    std::size_t max_lines)
{
    std::vector<std::string> lines;
    if (text.empty() || max_lines == 0) return lines;

    auto measure = [&](const std::string& value) {
        return font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, value.c_str()).x;
    };
    auto fit_with_ellipsis = [&](std::string value) {
        while (!value.empty() && measure(value + "...") > wrap_width) value.pop_back();
        return value.empty() ? std::string("...") : (value + "...");
    };

    std::vector<std::string> words;
    for (std::size_t cursor = 0; cursor < text.size();) {
        while (cursor < text.size() && text[cursor] == ' ') ++cursor;
        if (cursor >= text.size()) break;
        std::size_t next_space = text.find(' ', cursor);
        if (next_space == std::string::npos) next_space = text.size();
        words.push_back(text.substr(cursor, next_space - cursor));
        cursor = next_space + 1;
    }

    for (std::size_t word_index = 0; word_index < words.size() && lines.size() < max_lines;) {
        std::string line = words[word_index];
        if (measure(line) > wrap_width) {
            lines.push_back(fit_with_ellipsis(line));
            ++word_index;
            continue;
        }

        std::size_t next_index = word_index + 1;
        while (next_index < words.size()) {
            const std::string candidate = line + " " + words[next_index];
            if (measure(candidate) > wrap_width) break;
            line = candidate;
            ++next_index;
        }

        if ((lines.size() + 1) == max_lines && next_index < words.size()) {
            lines.push_back(fit_with_ellipsis(line));
            break;
        }

        lines.push_back(line);
        word_index = next_index;
    }

    return lines;
}

RadialLayout BuildLayout()
{
    const radial_config::RadialConfig& config = radial_config::Get();
    RadialLayout layout{};
    layout.config = &config;
    layout.viewport = ImGui::GetMainViewport();
    layout.center = {
        layout.viewport->Pos.x + (layout.viewport->Size.x * config.center_x) + config.offset_x,
        layout.viewport->Pos.y + (layout.viewport->Size.y * config.center_y) + config.offset_y,
    };
    layout.bottom_right = {
        layout.viewport->Pos.x + layout.viewport->Size.x,
        layout.viewport->Pos.y + layout.viewport->Size.y,
    };
    const float viewport_min = std::min(layout.viewport->Size.x, layout.viewport->Size.y);
    layout.ui_scale = std::clamp(viewport_min / kBaseViewportHeight, 0.9f, 1.85f) * config.scale;
    layout.wheel_inner_radius = config.wheel_inner_radius * layout.ui_scale;
    layout.wheel_outer_radius = config.wheel_outer_radius * layout.ui_scale;
    layout.central_panel_outer_radius = config.central_panel_outer_radius * layout.ui_scale;
    layout.slot_inner_radius = config.slot_inner_radius * layout.ui_scale;
    layout.slot_outer_radius = config.slot_outer_radius * layout.ui_scale;
    layout.icon_size = config.icon_size * layout.ui_scale;
    layout.segment_gap_radians = (config.slot_gap_degrees * kPi / 180.0f) * 0.5f;
    layout.opacity = config.opacity;
    layout.screen_dim_opacity = config.screen_dim_opacity;
    layout.wheel_hidden = config.wheel_hidden;
    layout.central_panel_hidden = config.central_panel_hidden;
    layout.slot_details = config.slot_details;
    layout.show_controls = config.show_controls;
    return layout;
}

float IconRadiusForSegment(const RadialLayout& layout, float segment_angle)
{
    const float inner = layout.slot_inner_radius;
    const float outer = layout.slot_outer_radius;
    if (outer <= inner) return inner;

    const float denominator = outer * outer - inner * inner;
    if (denominator <= 0.0f || segment_angle <= 0.0f) return (inner + outer) * 0.5f;

    const float radial_centroid = (2.0f / 3.0f) *
        ((outer * outer * outer) - (inner * inner * inner)) / denominator;
    const float half_angle = segment_angle * 0.5f;
    float radius = radial_centroid * (std::sin(half_angle) / half_angle);

    const float margin = 2.0f * layout.ui_scale;
    const float min_radius = inner + (layout.icon_size * 0.5f) + margin;
    const float max_radius = outer - (layout.icon_size * 0.5f) - margin;
    if (min_radius <= max_radius) radius = std::clamp(radius, min_radius, max_radius);
    return radius;
}

void BeginOverlayWindow(const RadialLayout& layout)
{
    ImGui::SetNextWindowPos(layout.viewport->Pos);
    ImGui::SetNextWindowSize(layout.viewport->Size);

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground;
    ImGui::Begin("RadialMenuOverlay", nullptr, flags);
}

void DrawSlotIcon(ImDrawList* draw_list, const ImVec2& center, const IconTextureInfo& icon,
    const radial_config::Color& icon_color, float icon_size, float ui_scale, float opacity)
{
    if (icon.texture == ImTextureID{}) return;

    const float half_extent = icon_size * 0.5f;
    const ImVec2 image_min = {center.x - half_extent, center.y - half_extent};
    const ImVec2 image_max = {center.x + half_extent, center.y + half_extent};
    draw_list->AddImageRounded(icon.texture, image_min, image_max, icon.uv_min, icon.uv_max,
        ColorWithOpacity(icon_color, opacity), 10.0f * ui_scale);
}

void DrawBackdrop(ImDrawList* draw_list, const RadialLayout& layout)
{
    const radial_config::RadialConfig& config = *layout.config;

    draw_list->AddRectFilled(layout.viewport->Pos, layout.bottom_right,
        ColorWithOpacity(config.screen_dim_color, layout.screen_dim_opacity));
    if (!layout.wheel_hidden) {
        AddCircleRing(draw_list, layout.center, layout.wheel_inner_radius, layout.wheel_outer_radius,
            ColorWithOpacity(config.wheel_background_color, layout.opacity, 0.94f));
    }
}

void DrawWheelCenterRims(ImDrawList* draw_list, const RadialLayout& layout)
{
    const radial_config::RadialConfig& config = *layout.config;
    if (layout.wheel_hidden) return;
    draw_list->AddCircle(layout.center, layout.wheel_inner_radius,
        ColorWithOpacity(config.wheel_border_color, layout.opacity), 72, 2.0f * layout.ui_scale);
    draw_list->AddCircle(layout.center, layout.wheel_outer_radius,
        ColorWithOpacity(config.wheel_border_color, layout.opacity), 72, 2.0f * layout.ui_scale);
}

void DrawWheel(ImDrawList* draw_list, const RadialLayout& layout, const std::vector<RadialSlot>& slots,
    int selected_slot)
{
    const radial_config::RadialConfig& config = *layout.config;
    const float slice_inner_radius = layout.slot_inner_radius;
    const float slice_outer_radius = std::max(slice_inner_radius + (1.0f * layout.ui_scale), layout.slot_outer_radius);
    const float trim_inset = std::min(4.0f * layout.ui_scale, (slice_outer_radius - slice_inner_radius) * 0.25f);
    const std::size_t slot_count = std::max<std::size_t>(slots.size(), 1);
    const float step = (2.0f * kPi) / static_cast<float>(slot_count);
    const float segment_gap_radians = std::min(layout.segment_gap_radians, step * 0.45f);

    for (std::size_t i = 0; i < slots.size(); ++i) {
        const bool is_selected = static_cast<int>(i) == selected_slot;
        const float start_angle = (-kPi * 0.5f) + (step * static_cast<float>(i)) + segment_gap_radians;
        const float end_angle = (-kPi * 0.5f) + (step * static_cast<float>(i + 1)) - segment_gap_radians;

        const ImU32 fill = ColorWithOpacity(is_selected ? config.slot_selected_background_color :
            config.slot_background_color, layout.opacity);
        const ImU32 border = is_selected ?
            ColorWithOpacity(GetSelectedSlotBorderColor(slots[i], config), layout.opacity) :
            ColorWithOpacity(config.slot_border_color, layout.opacity, 0.95f);
        const float border_thickness = (is_selected ? 3.5f : 1.25f) * layout.ui_scale;
        AddRingSegment(draw_list, layout.center, slice_inner_radius, slice_outer_radius, start_angle,
            end_angle, fill, border, border_thickness);

        if (layout.slot_details) {
            const ImU32 inner_trim = is_selected ?
                ColorWithOpacity(GetSelectedSlotBorderColor(slots[i], config), layout.opacity, 0.75f) :
                ColorWithOpacity(config.slot_border_color, layout.opacity, 0.65f);
            const ImU32 outer_trim = is_selected ?
                ColorWithOpacity(GetSelectedSlotBorderColor(slots[i], config), layout.opacity, 0.75f) :
                ColorWithOpacity(config.slot_border_color, layout.opacity, 0.65f);
            AddArcStroke(draw_list, layout.center, slice_inner_radius + trim_inset, start_angle + 0.03f,
                end_angle - 0.03f, inner_trim, 1.0f * layout.ui_scale);
            AddArcStroke(draw_list, layout.center, slice_outer_radius - trim_inset, start_angle + 0.05f,
                end_angle - 0.05f, outer_trim, 1.0f * layout.ui_scale);
        }
    }

    if (layout.slot_details) {
        for (std::size_t i = 0; i < slot_count; ++i) {
            const float separator_angle = (-kPi * 0.5f) + (step * static_cast<float>(i));
            AddSegmentSeparator(draw_list, layout.center, separator_angle,
                slice_inner_radius + (2.0f * layout.ui_scale), slice_outer_radius - (2.0f * layout.ui_scale),
                ColorWithOpacity(config.slot_border_color, layout.opacity, 0.9f), 1.0f * layout.ui_scale);
        }
    }
}

void DrawWheelIcons(ImDrawList* draw_list, const RadialLayout& layout, std::size_t slot_count,
    const std::vector<IconTextureInfo>& icon_textures)
{
    if (slot_count == 0) return;

    const float step = (2.0f * kPi) / static_cast<float>(slot_count);
    const float segment_gap_radians = std::min(layout.segment_gap_radians, step * 0.45f);
    const float segment_angle = std::max(0.0f, step - (segment_gap_radians * 2.0f));
    const float icon_radius = IconRadiusForSegment(layout, segment_angle);
    const IconTextureInfo empty_icon = {};

    for (std::size_t i = 0; i < slot_count; ++i) {
        const float start_angle = (-kPi * 0.5f) + (step * static_cast<float>(i)) + segment_gap_radians;
        const float end_angle = (-kPi * 0.5f) + (step * static_cast<float>(i + 1)) - segment_gap_radians;
        const float mid_angle = (start_angle + end_angle) * 0.5f;
        const ImVec2 icon_center = PolarPoint(layout.center, mid_angle, icon_radius);
        DrawSlotIcon(draw_list, icon_center, i < icon_textures.size() ? icon_textures[i] : empty_icon,
            layout.config->icon_color,
            layout.icon_size, layout.ui_scale, layout.opacity);
    }
}

void DrawCenterPanel(ImDrawList* draw_list, const RadialLayout& layout)
{
    const radial_config::RadialConfig& config = *layout.config;
    draw_list->AddCircleFilled(layout.center, layout.central_panel_outer_radius,
        ColorWithOpacity(config.central_panel_background_color, layout.opacity), 56);
    draw_list->AddCircle(layout.center, layout.central_panel_outer_radius,
        ColorWithOpacity(config.central_panel_border_color, layout.opacity), 56, 1.0f * layout.ui_scale);
}

void DrawSelectedDetails(ImDrawList* draw_list, ImFont* font, float base_font_size, const RadialLayout& layout,
    const std::vector<RadialSlot>& slots, const char* title, int selected_slot)
{
    const radial_config::RadialConfig& config = *layout.config;
    AddCenteredText(draw_list, font, base_font_size * 0.94f * layout.ui_scale, layout.center,
        layout.center.y - (48.0f * layout.ui_scale), ColorWithOpacity(config.text_color, layout.opacity, 0.86f), title);
    if (selected_slot < 0 || selected_slot >= static_cast<int>(slots.size())) return;

    const RadialSlot& slot = slots[static_cast<std::size_t>(selected_slot)];
    AddCenteredText(draw_list, font, base_font_size * 0.84f * layout.ui_scale, layout.center,
        layout.center.y - (28.0f * layout.ui_scale),
        ColorWithOpacity(GetSelectedSlotBorderColor(slot, config), layout.opacity),
        GetCategoryLabel(slot));

    const float font_size = base_font_size * 0.96f * layout.ui_scale;
    const float wrap_width = layout.central_panel_outer_radius * 1.52f;
    if (!g_selected_details_cache.valid || g_selected_details_cache.font != font ||
        g_selected_details_cache.font_size != font_size || g_selected_details_cache.wrap_width != wrap_width ||
        g_selected_details_cache.id != slot.id || g_selected_details_cache.occupied != slot.occupied ||
        g_selected_details_cache.is_item != slot.is_item || g_selected_details_cache.name != slot.name) {
        g_selected_details_cache.font = font;
        g_selected_details_cache.font_size = font_size;
        g_selected_details_cache.wrap_width = wrap_width;
        g_selected_details_cache.id = slot.id;
        g_selected_details_cache.occupied = slot.occupied;
        g_selected_details_cache.is_item = slot.is_item;
        g_selected_details_cache.name = slot.name;
        g_selected_details_cache.label_lines = WrapTextLines(font, font_size, FormatSlotLabel(slot), wrap_width, 2);
        g_selected_details_cache.valid = true;
    }

    const std::vector<std::string>& label_lines = g_selected_details_cache.label_lines;
    const float line_height = font_size + (2.0f * layout.ui_scale);
    float line_y = layout.center.y + (14.0f * layout.ui_scale);
    if (label_lines.size() > 1) line_y -= (line_height * 0.35f);
    for (const std::string& line : label_lines) {
        AddCenteredText(draw_list, font, font_size, layout.center, line_y,
            ColorWithOpacity(config.text_color, layout.opacity), line.c_str());
        line_y += line_height;
    }
}

}  // namespace

void DrawMenuContents(const std::vector<RadialSlot>& slots, const char* title, const char* controls,
    int selected_slot, const std::vector<IconTextureInfo>& icon_textures)
{
    const RadialLayout layout = BuildLayout();
    BeginOverlayWindow(layout);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    const float base_font_size = ImGui::GetFontSize();

    DrawBackdrop(draw_list, layout);
    DrawWheel(draw_list, layout, slots, selected_slot);
    if (!layout.central_panel_hidden) DrawCenterPanel(draw_list, layout);
    DrawWheelCenterRims(draw_list, layout);
    DrawWheelIcons(draw_list, layout, slots.size(), icon_textures);
    if (!layout.central_panel_hidden) {
        DrawSelectedDetails(draw_list, font, base_font_size, layout, slots, title, selected_slot);
    }
    if (layout.show_controls) {
        AddCenteredText(draw_list, font, base_font_size * 0.84f * layout.ui_scale, layout.center,
            layout.center.y + layout.wheel_outer_radius + (28.0f * layout.ui_scale),
            ColorWithOpacity(layout.config->text_color, layout.opacity, 0.86f), controls);
    }

    ImGui::End();
}

void InvalidateMenuDrawCache()
{
    g_selected_details_cache = {};
}

}  // namespace radial_menu_mod::radial_menu
