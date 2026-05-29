#pragma once

namespace radial_menu_mod::radial_config {

struct Color {
    int r = 255;
    int g = 255;
    int b = 255;
    int a = 255;
};

struct RadialConfig {
    float scale = 1.0f;
    float center_x = 0.5f;
    float center_y = 0.5f;
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    float wheel_inner_radius = 120.0f;
    float wheel_outer_radius = 250.0f;
    float icon_size = 80.0f;
    float gap_size = 3.0f;
    float ring_padding = 4.0f;
    float opacity = 1.0f;
    float screen_dim_opacity = 0.3f;
    bool show_center_panel = true;
    bool show_controls = true;
    int editor_toggle_key = 0x76;
    bool editor_toggle_shift = true;
    bool editor_toggle_ctrl = false;
    bool editor_toggle_alt = false;

    Color screen_dim_color = {0, 0, 0, 255};
    Color background_color = {24, 22, 19, 224};
    Color selected_color = {40, 36, 30, 232};
    Color border_color = {110, 95, 65, 180};
    Color accent_color = {171, 148, 102, 190};
    Color text_color = {244, 238, 223, 255};
    Color icon_color = {255, 255, 255, 255};
    Color sorcery_color = {155, 240, 255, 255};
    Color incantation_color = {255, 219, 170, 255};
    Color spell_color = {220, 230, 235, 255};
};

void Load();
void ReloadIfChanged();
const RadialConfig& Get();
RadialConfig& Edit();
bool Save();

}  // namespace radial_menu_mod::radial_config
