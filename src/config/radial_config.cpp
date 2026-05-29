#include "config/radial_config.h"

#include "core/common.h"

#include "mini/ini.h"

#include <windows.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cwchar>
#include <cstdio>
#include <string>

namespace radial_menu_mod::radial_config {
namespace {

constexpr wchar_t kConfigFileName[] = L"RadialMenu.ini";
constexpr char kSection[] = "radial_menu";
constexpr char kColorsSection[] = "colors";

RadialConfig g_config = {};
std::wstring g_config_path;
FILETIME g_last_write_time = {};
bool g_config_file_present = false;
bool g_loaded = false;

std::string FormatFloat(float value, int precision)
{
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
    return buffer;
}

std::string FormatBool(bool value)
{
    return value ? "true" : "false";
}

std::string FormatColor(Color color)
{
    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x%02x", color.r, color.g, color.b, color.a);
    return buffer;
}

std::wstring ResolveConfigPath()
{
    HMODULE module = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&Load),
        &module);

    wchar_t module_path[MAX_PATH] = {};
    if (!GetModuleFileNameW(module, module_path, MAX_PATH)) return kConfigFileName;

    wchar_t* sep = std::wcsrchr(module_path, L'\\');
    if (!sep) return kConfigFileName;

    *(sep + 1) = L'\0';
    std::wstring config_path(module_path);
    config_path += kConfigFileName;
    return config_path;
}

bool GetLastWriteTime(const std::wstring& path, FILETIME& write_time)
{
    WIN32_FILE_ATTRIBUTE_DATA attributes = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) return false;
    write_time = attributes.ftLastWriteTime;
    return true;
}

bool SameFileTime(const FILETIME& a, const FILETIME& b)
{
    return a.dwLowDateTime == b.dwLowDateTime && a.dwHighDateTime == b.dwHighDateTime;
}

float ParseFloat(const std::string& value, float fallback)
{
    char* end = nullptr;
    errno = 0;
    const float parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || errno == ERANGE || !std::isfinite(parsed)) return fallback;
    return parsed;
}

float ClampFloat(float value, float min_value, float max_value)
{
    return std::clamp(value, min_value, max_value);
}

float ReadFloat(const mINI::INIMap<std::string>& section, const char* key, float fallback, float min_value,
    float max_value)
{
    if (!section.has(key)) return fallback;
    return ClampFloat(ParseFloat(section.get(key), fallback), min_value, max_value);
}

bool ParseBool(std::string value, bool fallback)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (value == "true" || value == "1" || value == "yes" || value == "on") return true;
    if (value == "false" || value == "0" || value == "no" || value == "off") return false;
    return fallback;
}

bool ReadBool(const mINI::INIMap<std::string>& section, const char* key, bool fallback)
{
    if (!section.has(key)) return fallback;
    return ParseBool(section.get(key), fallback);
}

int ParseKey(std::string value, int fallback)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    if (value.size() == 1 && value[0] >= 'A' && value[0] <= 'Z') return value[0];
    if (value.size() == 1 && value[0] >= '0' && value[0] <= '9') return value[0];
    if (value.size() >= 2 && value[0] == 'F') {
        const int index = std::atoi(value.c_str() + 1);
        if (index >= 1 && index <= 24) return VK_F1 + index - 1;
    }
    if (value == "INSERT") return VK_INSERT;
    if (value == "HOME") return VK_HOME;
    if (value == "END") return VK_END;
    if (value == "PAGEUP") return VK_PRIOR;
    if (value == "PAGEDOWN") return VK_NEXT;
    return fallback;
}

std::string FormatKey(int key)
{
    if (key >= 'A' && key <= 'Z') return std::string(1, static_cast<char>(key));
    if (key >= '0' && key <= '9') return std::string(1, static_cast<char>(key));
    if (key >= VK_F1 && key <= VK_F24) return "F" + std::to_string(key - VK_F1 + 1);
    if (key == VK_INSERT) return "Insert";
    if (key == VK_HOME) return "Home";
    if (key == VK_END) return "End";
    if (key == VK_PRIOR) return "PageUp";
    if (key == VK_NEXT) return "PageDown";
    return "F7";
}

int ReadKey(const mINI::INIMap<std::string>& section, const char* key, int fallback)
{
    if (!section.has(key)) return fallback;
    return ParseKey(section.get(key), fallback);
}

int HexDigitValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

bool ParseHexByte(const std::string& value, std::size_t offset, int& byte)
{
    const int high = HexDigitValue(value[offset]);
    const int low = HexDigitValue(value[offset + 1]);
    if (high < 0 || low < 0) return false;
    byte = (high << 4) | low;
    return true;
}

Color ParseColor(std::string value, Color fallback)
{
    if (!value.empty() && value[0] == '#') value.erase(0, 1);
    if (value.size() >= 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) value.erase(0, 2);
    if (value.size() != 6 && value.size() != 8) return fallback;

    Color parsed = fallback;
    if (!ParseHexByte(value, 0, parsed.r)) return fallback;
    if (!ParseHexByte(value, 2, parsed.g)) return fallback;
    if (!ParseHexByte(value, 4, parsed.b)) return fallback;
    parsed.a = 255;
    if (value.size() == 8 && !ParseHexByte(value, 6, parsed.a)) return fallback;
    return parsed;
}

Color ReadColor(const mINI::INIMap<std::string>& section, const char* key, Color fallback)
{
    if (!section.has(key)) return fallback;
    return ParseColor(section.get(key), fallback);
}

void ReadColors(const mINI::INIStructure& ini, RadialConfig& config)
{
    if (!ini.has(kColorsSection)) return;

    const mINI::INIMap<std::string> colors = ini.get(kColorsSection);
    config.screen_dim_color = ReadColor(colors, "screen_dim_color", config.screen_dim_color);
    config.background_color = ReadColor(colors, "background_color", config.background_color);
    config.selected_color = ReadColor(colors, "selected_color", config.selected_color);
    config.border_color = ReadColor(colors, "border_color", config.border_color);
    config.accent_color = ReadColor(colors, "accent_color", config.accent_color);
    config.text_color = ReadColor(colors, "text_color", config.text_color);
    config.icon_color = ReadColor(colors, "icon_color", config.icon_color);
    config.sorcery_color = ReadColor(colors, "sorcery_color", config.sorcery_color);
    config.incantation_color = ReadColor(colors, "incantation_color", config.incantation_color);
    config.spell_color = ReadColor(colors, "spell_color", config.spell_color);
}

RadialConfig ReadConfig()
{
    RadialConfig config = {};

    mINI::INIFile file(g_config_path);
    mINI::INIStructure ini;
    if (!file.read(ini)) {
        Log("Radial config: RadialMenu.ini not found or unreadable; using built-in defaults.");
        return config;
    }
    if (!ini.has(kSection)) {
        Log("Radial config: [radial_menu] section not found; using built-in defaults.");
        return config;
    }

    const mINI::INIMap<std::string> section = ini.get(kSection);
    config.scale = ReadFloat(section, "scale", config.scale, 0.25f, 4.0f);
    config.center_x = ReadFloat(section, "center_x", config.center_x, -1.0f, 2.0f);
    config.center_y = ReadFloat(section, "center_y", config.center_y, -1.0f, 2.0f);
    config.offset_x = ReadFloat(section, "offset_x", config.offset_x, -4000.0f, 4000.0f);
    config.offset_y = ReadFloat(section, "offset_y", config.offset_y, -4000.0f, 4000.0f);
    config.wheel_inner_radius = ReadFloat(section, "wheel_inner_radius", config.wheel_inner_radius, 0.0f, 2000.0f);
    config.wheel_outer_radius = ReadFloat(section, "wheel_outer_radius", config.wheel_outer_radius, 1.0f, 2500.0f);
    config.icon_size = ReadFloat(section, "icon_size", config.icon_size, 0.0f, 512.0f);
    config.gap_size = ReadFloat(section, "gap_size", config.gap_size, 0.0f, 30.0f);
    config.ring_padding = ReadFloat(section, "ring_padding", config.ring_padding, 0.0f, 80.0f);
    config.opacity = ReadFloat(section, "opacity", config.opacity, 0.0f, 1.0f);
    config.screen_dim_opacity = ReadFloat(section, "screen_dim_opacity", config.screen_dim_opacity, 0.0f, 1.0f);
    config.show_center_panel = ReadBool(section, "show_center_panel", config.show_center_panel);
    config.show_controls = ReadBool(section, "show_controls", config.show_controls);
    config.editor_toggle_key = ReadKey(section, "editor_toggle_key", config.editor_toggle_key);
    config.editor_toggle_shift = ReadBool(section, "editor_toggle_shift", config.editor_toggle_shift);
    config.editor_toggle_ctrl = ReadBool(section, "editor_toggle_ctrl", config.editor_toggle_ctrl);
    config.editor_toggle_alt = ReadBool(section, "editor_toggle_alt", config.editor_toggle_alt);

    if (config.wheel_inner_radius >= config.wheel_outer_radius) {
        config.wheel_inner_radius = std::max(0.0f, config.wheel_outer_radius - 1.0f);
    }
    ReadColors(ini, config);
    return config;
}

void LogConfigSummary()
{
    Log("Radial config loaded: scale=%.2f center=(%.2f, %.2f) offset=(%.0f, %.0f) wheel_radii=(%.0f, %.0f) icon_size=%.0f gap_size=%.1f ring_padding=%.0f opacity=%.2f screen_dim=%.2f center_panel=%d controls=%d.",
        g_config.scale,
        g_config.center_x,
        g_config.center_y,
        g_config.offset_x,
        g_config.offset_y,
        g_config.wheel_inner_radius,
        g_config.wheel_outer_radius,
        g_config.icon_size,
        g_config.gap_size,
        g_config.ring_padding,
        g_config.opacity,
        g_config.screen_dim_opacity,
        static_cast<int>(g_config.show_center_panel),
        static_cast<int>(g_config.show_controls));
}

void LoadFromDisk()
{
    if (g_config_path.empty()) g_config_path = ResolveConfigPath();
    g_config = ReadConfig();
    g_config_file_present = GetLastWriteTime(g_config_path, g_last_write_time);
    if (!g_config_file_present) g_last_write_time = {};
    g_loaded = true;
    LogConfigSummary();
}

void WriteConfigToIni(mINI::INIStructure& ini, const RadialConfig& config)
{
    auto& section = ini[kSection];
    section["scale"] = FormatFloat(config.scale, 2);
    section["center_x"] = FormatFloat(config.center_x, 2);
    section["center_y"] = FormatFloat(config.center_y, 2);
    section["offset_x"] = FormatFloat(config.offset_x, 0);
    section["offset_y"] = FormatFloat(config.offset_y, 0);
    section["wheel_inner_radius"] = FormatFloat(config.wheel_inner_radius, 0);
    section["wheel_outer_radius"] = FormatFloat(config.wheel_outer_radius, 0);
    section["icon_size"] = FormatFloat(config.icon_size, 0);
    section["gap_size"] = FormatFloat(config.gap_size, 1);
    section["ring_padding"] = FormatFloat(config.ring_padding, 0);
    section["opacity"] = FormatFloat(config.opacity, 2);
    section["screen_dim_opacity"] = FormatFloat(config.screen_dim_opacity, 2);
    section["show_center_panel"] = FormatBool(config.show_center_panel);
    section["show_controls"] = FormatBool(config.show_controls);
    section["editor_toggle_key"] = FormatKey(config.editor_toggle_key);
    section["editor_toggle_shift"] = FormatBool(config.editor_toggle_shift);
    section["editor_toggle_ctrl"] = FormatBool(config.editor_toggle_ctrl);
    section["editor_toggle_alt"] = FormatBool(config.editor_toggle_alt);

    auto& colors = ini[kColorsSection];
    colors["screen_dim_color"] = FormatColor(config.screen_dim_color);
    colors["background_color"] = FormatColor(config.background_color);
    colors["selected_color"] = FormatColor(config.selected_color);
    colors["border_color"] = FormatColor(config.border_color);
    colors["accent_color"] = FormatColor(config.accent_color);
    colors["text_color"] = FormatColor(config.text_color);
    colors["icon_color"] = FormatColor(config.icon_color);
    colors["sorcery_color"] = FormatColor(config.sorcery_color);
    colors["incantation_color"] = FormatColor(config.incantation_color);
    colors["spell_color"] = FormatColor(config.spell_color);
}

}  // namespace

void Load()
{
    LoadFromDisk();
}

void ReloadIfChanged()
{
    if (!g_loaded) {
        LoadFromDisk();
        return;
    }

    FILETIME write_time = {};
    const bool config_file_present = GetLastWriteTime(g_config_path, write_time);
    if (!config_file_present && !g_config_file_present) return;
    if (config_file_present && g_config_file_present && SameFileTime(write_time, g_last_write_time)) return;

    Log("Radial config changed; reloading RadialMenu.ini.");
    LoadFromDisk();
}

const RadialConfig& Get()
{
    if (!g_loaded) LoadFromDisk();
    return g_config;
}

RadialConfig& Edit()
{
    if (!g_loaded) LoadFromDisk();
    return g_config;
}

bool Save()
{
    if (g_config_path.empty()) g_config_path = ResolveConfigPath();

    mINI::INIFile file(g_config_path);
    mINI::INIStructure ini;
    (void)file.read(ini);
    WriteConfigToIni(ini, g_config);
    const bool saved = file.write(ini, true);
    if (!saved) {
        Log("Radial config: failed to save RadialMenu.ini.");
        return false;
    }
    g_config_file_present = GetLastWriteTime(g_config_path, g_last_write_time);
    if (!g_config_file_present) g_last_write_time = {};
    Log("Radial config saved to RadialMenu.ini.");
    return true;
}

}  // namespace radial_menu_mod::radial_config
