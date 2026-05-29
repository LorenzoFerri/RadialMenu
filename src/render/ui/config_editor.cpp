#include "render/ui/config_editor.h"

#include "config/radial_config.h"

#include <windows.h>

#include <imgui.h>

#include <algorithm>

namespace radial_menu_mod::config_editor {
namespace {

bool g_open = false;
bool g_saved_message = false;

float Clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

void ColorToFloat4(const radial_config::Color& color, float out[4])
{
    out[0] = Clamp01(static_cast<float>(color.r) / 255.0f);
    out[1] = Clamp01(static_cast<float>(color.g) / 255.0f);
    out[2] = Clamp01(static_cast<float>(color.b) / 255.0f);
    out[3] = Clamp01(static_cast<float>(color.a) / 255.0f);
}

void Float4ToColor(const float value[4], radial_config::Color& color)
{
    color.r = std::clamp(static_cast<int>(value[0] * 255.0f + 0.5f), 0, 255);
    color.g = std::clamp(static_cast<int>(value[1] * 255.0f + 0.5f), 0, 255);
    color.b = std::clamp(static_cast<int>(value[2] * 255.0f + 0.5f), 0, 255);
    color.a = std::clamp(static_cast<int>(value[3] * 255.0f + 0.5f), 0, 255);
}

bool ColorEdit(const char* label, radial_config::Color& color)
{
    float value[4] = {};
    ColorToFloat4(color, value);
    if (!ImGui::ColorEdit4(label, value, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)) {
        return false;
    }
    Float4ToColor(value, color);
    g_saved_message = false;
    return true;
}

void MarkEdited()
{
    if (ImGui::IsItemEdited()) g_saved_message = false;
}

const char* KeyName(int key)
{
    switch (key) {
    case VK_F1: return "F1";
    case VK_F2: return "F2";
    case VK_F3: return "F3";
    case VK_F4: return "F4";
    case VK_F5: return "F5";
    case VK_F6: return "F6";
    case VK_F7: return "F7";
    case VK_F8: return "F8";
    case VK_F9: return "F9";
    case VK_F10: return "F10";
    case VK_F11: return "F11";
    case VK_F12: return "F12";
    case VK_INSERT: return "Insert";
    case VK_HOME: return "Home";
    case VK_END: return "End";
    case VK_PRIOR: return "PageUp";
    case VK_NEXT: return "PageDown";
    default: return "F7";
    }
}

void DrawHotkey(radial_config::RadialConfig& config)
{
    constexpr int keys[] = {
        VK_F1, VK_F2, VK_F3, VK_F4, VK_F5, VK_F6, VK_F7, VK_F8, VK_F9, VK_F10, VK_F11, VK_F12,
        VK_INSERT, VK_HOME, VK_END, VK_PRIOR, VK_NEXT,
    };

    if (ImGui::BeginCombo("Toggle key", KeyName(config.editor_toggle_key))) {
        for (int key : keys) {
            const bool selected = config.editor_toggle_key == key;
            if (ImGui::Selectable(KeyName(key), selected)) {
                config.editor_toggle_key = key;
                g_saved_message = false;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (ImGui::Checkbox("Require Shift", &config.editor_toggle_shift)) g_saved_message = false;
    ImGui::SameLine();
    if (ImGui::Checkbox("Require Ctrl", &config.editor_toggle_ctrl)) g_saved_message = false;
    ImGui::SameLine();
    if (ImGui::Checkbox("Require Alt", &config.editor_toggle_alt)) g_saved_message = false;
}

}  // namespace

void Toggle()
{
    g_open = !g_open;
    g_saved_message = false;
}

void Close()
{
    g_open = false;
}

bool IsOpen()
{
    return g_open;
}

void Draw()
{
    if (!g_open) return;

    radial_config::RadialConfig& config = radial_config::Edit();

    ImGui::SetNextWindowSize({520.0f, 680.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Radial Menu Config", &g_open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("Changes apply live. Use Save to persist them to RadialMenu.ini.");
    if (ImGui::Button("Save to RadialMenu.ini")) {
        g_saved_message = radial_config::Save();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload from disk")) {
        radial_config::Load();
        g_saved_message = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to defaults")) {
        ImGui::OpenPopup("Reset config?");
    }
    if (ImGui::BeginPopupModal("Reset config?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Reset all live settings to built-in defaults?");
        ImGui::TextUnformatted("Use Save afterwards to overwrite RadialMenu.ini.");
        if (ImGui::Button("Reset", {120.0f, 0.0f})) {
            radial_config::Edit() = radial_config::RadialConfig{};
            g_saved_message = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {120.0f, 0.0f})) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (g_saved_message) ImGui::TextUnformatted("Saved.");

    if (ImGui::CollapsingHeader("Layout", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Scale", &config.scale, 0.25f, 4.0f, "%.2f"); MarkEdited();
        ImGui::SliderFloat("Center X", &config.center_x, -1.0f, 2.0f, "%.2f"); MarkEdited();
        ImGui::SliderFloat("Center Y", &config.center_y, -1.0f, 2.0f, "%.2f"); MarkEdited();
        ImGui::SliderFloat("Offset X", &config.offset_x, -4000.0f, 4000.0f, "%.0f"); MarkEdited();
        ImGui::SliderFloat("Offset Y", &config.offset_y, -4000.0f, 4000.0f, "%.0f"); MarkEdited();
    }

    if (ImGui::CollapsingHeader("Wheel", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Checkbox("Hidden##wheel", &config.wheel_hidden)) g_saved_message = false;
        ImGui::SliderFloat("Inner size##wheel", &config.wheel_inner_radius, 0.0f, 2000.0f, "%.0f"); MarkEdited();
        ImGui::SliderFloat("Outer size##wheel", &config.wheel_outer_radius, 1.0f, 2500.0f, "%.0f"); MarkEdited();
        if (config.wheel_inner_radius >= config.wheel_outer_radius) {
            config.wheel_inner_radius = std::max(0.0f, config.wheel_outer_radius - 1.0f);
        }
        ColorEdit("Background color##wheel", config.wheel_background_color);
        ColorEdit("Border color##wheel", config.wheel_border_color);
    }

    if (ImGui::CollapsingHeader("Central Panel", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Checkbox("Hidden##central_panel", &config.central_panel_hidden)) g_saved_message = false;
        ImGui::SliderFloat("Outer size##central_panel", &config.central_panel_outer_radius, 1.0f, 2500.0f,
            "%.0f"); MarkEdited();
        ColorEdit("Background color##central_panel", config.central_panel_background_color);
        ColorEdit("Border color##central_panel", config.central_panel_border_color);
    }

    if (ImGui::CollapsingHeader("Item/Spell Slot", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Inner size##slot", &config.slot_inner_radius, 0.0f, 2000.0f, "%.0f"); MarkEdited();
        ImGui::SliderFloat("Outer size##slot", &config.slot_outer_radius, 1.0f, 2500.0f, "%.0f"); MarkEdited();
        if (config.slot_inner_radius >= config.slot_outer_radius) {
            config.slot_inner_radius = std::max(0.0f, config.slot_outer_radius - 1.0f);
        }
        ColorEdit("Background color##slot", config.slot_background_color);
        ColorEdit("Selected background color##slot", config.slot_selected_background_color);
        ColorEdit("Border color##slot", config.slot_border_color);
        ColorEdit("Selected sorcery border color##slot", config.slot_selected_sorcery_border_color);
        ColorEdit("Selected incantation border color##slot", config.slot_selected_incantation_border_color);
        if (ImGui::Checkbox("Details arcs/lines##slot", &config.slot_details)) g_saved_message = false;
        ImGui::SliderFloat("Gap##slot", &config.slot_gap_degrees, 0.0f, 30.0f, "%.1f degrees"); MarkEdited();
    }

    if (ImGui::CollapsingHeader("Editor Hotkey")) {
        DrawHotkey(config);
    }

    ImGui::End();
}

}  // namespace radial_menu_mod::config_editor
