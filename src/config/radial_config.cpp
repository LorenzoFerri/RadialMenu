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
constexpr char kLayoutSection[] = "layout";
constexpr char kWheelSection[] = "wheel";
constexpr char kCentralPanelSection[] = "central_panel";
constexpr char kSlotSection[] = "item_spell_slot";
constexpr char kEditorSection[] = "editor";

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

RadialConfig ReadConfig()
{
    RadialConfig config = {};

    mINI::INIFile file(g_config_path);
    mINI::INIStructure ini;
    if (!file.read(ini)) {
        Log("Radial config: RadialMenu.ini not found or unreadable; using built-in defaults.");
        return config;
    }
    if (!ini.has(kLayoutSection) && !ini.has(kWheelSection) && !ini.has(kCentralPanelSection) &&
        !ini.has(kSlotSection) && !ini.has(kEditorSection)) {
        Log("Radial config: no known sections found in RadialMenu.ini; using built-in defaults.");
    }

    if (ini.has(kLayoutSection)) {
        const mINI::INIMap<std::string> layout = ini.get(kLayoutSection);
        config.scale = ReadFloat(layout, "scale", config.scale, 0.25f, 4.0f);
        config.center_x = ReadFloat(layout, "center_x", config.center_x, -1.0f, 2.0f);
        config.center_y = ReadFloat(layout, "center_y", config.center_y, -1.0f, 2.0f);
        config.offset_x = ReadFloat(layout, "offset_x", config.offset_x, -4000.0f, 4000.0f);
        config.offset_y = ReadFloat(layout, "offset_y", config.offset_y, -4000.0f, 4000.0f);
    }

    if (ini.has(kWheelSection)) {
        const mINI::INIMap<std::string> wheel = ini.get(kWheelSection);
        config.wheel_inner_radius = ReadFloat(wheel, "inner_size", config.wheel_inner_radius, 0.0f, 2000.0f);
        config.wheel_outer_radius = ReadFloat(wheel, "outer_size", config.wheel_outer_radius, 1.0f, 2500.0f);
        config.wheel_background_color = ReadColor(wheel, "background_color", config.wheel_background_color);
        config.wheel_border_color = ReadColor(wheel, "border_color", config.wheel_border_color);
        config.wheel_hidden = ReadBool(wheel, "hidden", config.wheel_hidden);
    }

    if (ini.has(kCentralPanelSection)) {
        const mINI::INIMap<std::string> central_panel = ini.get(kCentralPanelSection);
        config.central_panel_hidden = ReadBool(central_panel, "hidden", config.central_panel_hidden);
        config.central_panel_outer_radius = ReadFloat(central_panel, "outer_size",
            config.central_panel_outer_radius, 1.0f, 2500.0f);
        config.central_panel_background_color = ReadColor(central_panel, "background_color",
            config.central_panel_background_color);
        config.central_panel_border_color = ReadColor(central_panel, "border_color", config.central_panel_border_color);
    }

    if (ini.has(kSlotSection)) {
        const mINI::INIMap<std::string> slot = ini.get(kSlotSection);
        config.slot_inner_radius = ReadFloat(slot, "inner_size", config.slot_inner_radius, 0.0f, 2000.0f);
        config.slot_outer_radius = ReadFloat(slot, "outer_size", config.slot_outer_radius, 1.0f, 2500.0f);
        config.slot_background_color = ReadColor(slot, "background_color", config.slot_background_color);
        config.slot_selected_background_color = ReadColor(slot, "selected_background_color",
            config.slot_selected_background_color);
        config.slot_border_color = ReadColor(slot, "border_color", config.slot_border_color);
        config.slot_selected_sorcery_border_color = ReadColor(slot, "selected_sorcery_border_color",
            config.slot_selected_sorcery_border_color);
        config.slot_selected_incantation_border_color = ReadColor(slot, "selected_incantation_border_color",
            config.slot_selected_incantation_border_color);
        config.slot_details = ReadBool(slot, "details", config.slot_details);
        config.slot_gap_degrees = ReadFloat(slot, "gap_degrees", config.slot_gap_degrees, 0.0f, 30.0f);
    }

    if (ini.has(kEditorSection)) {
        const mINI::INIMap<std::string> editor = ini.get(kEditorSection);
        config.editor_toggle_key = ReadKey(editor, "toggle_key", config.editor_toggle_key);
        config.editor_toggle_shift = ReadBool(editor, "toggle_shift", config.editor_toggle_shift);
        config.editor_toggle_ctrl = ReadBool(editor, "toggle_ctrl", config.editor_toggle_ctrl);
        config.editor_toggle_alt = ReadBool(editor, "toggle_alt", config.editor_toggle_alt);
    }

    if (config.wheel_inner_radius >= config.wheel_outer_radius) {
        config.wheel_inner_radius = std::max(0.0f, config.wheel_outer_radius - 1.0f);
    }
    if (config.slot_inner_radius >= config.slot_outer_radius) {
        config.slot_inner_radius = std::max(0.0f, config.slot_outer_radius - 1.0f);
    }
    return config;
}

void LogConfigSummary()
{
    Log("Radial config loaded: scale=%.2f center=(%.2f, %.2f) offset=(%.0f, %.0f) wheel=(%.0f, %.0f hidden=%d) center=(%.0f hidden=%d) slot=(%.0f, %.0f gap=%.1f details=%d).",
        g_config.scale,
        g_config.center_x,
        g_config.center_y,
        g_config.offset_x,
        g_config.offset_y,
        g_config.wheel_inner_radius,
        g_config.wheel_outer_radius,
        static_cast<int>(g_config.wheel_hidden),
        g_config.central_panel_outer_radius,
        static_cast<int>(g_config.central_panel_hidden),
        g_config.slot_inner_radius,
        g_config.slot_outer_radius,
        g_config.slot_gap_degrees,
        static_cast<int>(g_config.slot_details));
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
    auto& layout = ini[kLayoutSection];
    layout["scale"] = FormatFloat(config.scale, 2);
    layout["center_x"] = FormatFloat(config.center_x, 2);
    layout["center_y"] = FormatFloat(config.center_y, 2);
    layout["offset_x"] = FormatFloat(config.offset_x, 0);
    layout["offset_y"] = FormatFloat(config.offset_y, 0);

    auto& wheel = ini[kWheelSection];
    wheel["inner_size"] = FormatFloat(config.wheel_inner_radius, 0);
    wheel["outer_size"] = FormatFloat(config.wheel_outer_radius, 0);
    wheel["background_color"] = FormatColor(config.wheel_background_color);
    wheel["border_color"] = FormatColor(config.wheel_border_color);
    wheel["hidden"] = FormatBool(config.wheel_hidden);

    auto& central_panel = ini[kCentralPanelSection];
    central_panel["hidden"] = FormatBool(config.central_panel_hidden);
    central_panel["outer_size"] = FormatFloat(config.central_panel_outer_radius, 0);
    central_panel["background_color"] = FormatColor(config.central_panel_background_color);
    central_panel["border_color"] = FormatColor(config.central_panel_border_color);

    auto& slot = ini[kSlotSection];
    slot["inner_size"] = FormatFloat(config.slot_inner_radius, 0);
    slot["outer_size"] = FormatFloat(config.slot_outer_radius, 0);
    slot["background_color"] = FormatColor(config.slot_background_color);
    slot["selected_background_color"] = FormatColor(config.slot_selected_background_color);
    slot["border_color"] = FormatColor(config.slot_border_color);
    slot["selected_sorcery_border_color"] = FormatColor(config.slot_selected_sorcery_border_color);
    slot["selected_incantation_border_color"] = FormatColor(config.slot_selected_incantation_border_color);
    slot["details"] = FormatBool(config.slot_details);
    slot["gap_degrees"] = FormatFloat(config.slot_gap_degrees, 1);

    auto& editor = ini[kEditorSection];
    editor["toggle_key"] = FormatKey(config.editor_toggle_key);
    editor["toggle_shift"] = FormatBool(config.editor_toggle_shift);
    editor["toggle_ctrl"] = FormatBool(config.editor_toggle_ctrl);
    editor["toggle_alt"] = FormatBool(config.editor_toggle_alt);
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
