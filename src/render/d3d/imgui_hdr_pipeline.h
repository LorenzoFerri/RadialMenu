#pragma once

#include <d3d12.h>
#include <dxgiformat.h>
#include <imgui.h>

namespace radial_menu_mod::imgui_hdr_pipeline {

void Initialize(ID3D12Device* device, DXGI_FORMAT rtv_format);
void Shutdown();
void BeginDrawList(ImDrawList* draw_list, bool hdr_enabled, float ui_brightness_nits, float ui_saturation);
void EndDrawList(ImDrawList* draw_list, bool hdr_enabled);

}  // namespace radial_menu_mod::imgui_hdr_pipeline
