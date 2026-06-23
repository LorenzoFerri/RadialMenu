#include "render/d3d/imgui_hdr_pipeline.h"

#include "core/common.h"

#include <backends/imgui_impl_dx12.h>
#include <d3dcompiler.h>
#include <cstring>
#include <cstdio>
#include <string>

namespace radial_menu_mod::imgui_hdr_pipeline {
namespace {

ID3D12Device* g_device = nullptr;
DXGI_FORMAT g_rtv_format = DXGI_FORMAT_UNKNOWN;
ID3D12RootSignature* g_root_signature = nullptr;
ID3D12PipelineState* g_pipeline_state = nullptr;
int g_pipeline_brightness = -1;
int g_pipeline_saturation = -1;
bool g_logged_compile_failure = false;
bool g_logged_pipeline_failure = false;

template<typename T>
void SafeRelease(T*& resource)
{
    if (resource) resource->Release();
    resource = nullptr;
}

int ClampBrightness(float value)
{
    if (value < 50.0f) return 50;
    if (value > 2000.0f) return 2000;
    return static_cast<int>(value + 0.5f);
}

int ClampSaturation(float value)
{
    if (value < 0.0f) return 0;
    if (value > 2.0f) return 200;
    return static_cast<int>((value * 100.0f) + 0.5f);
}

bool CreateRootSignature()
{
    if (g_root_signature) return true;
    if (!g_device) return false;

    D3D12_DESCRIPTOR_RANGE srv_range = {};
    srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srv_range.NumDescriptors = 1;
    srv_range.BaseShaderRegister = 0;
    srv_range.RegisterSpace = 0;
    srv_range.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER params[2] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.RegisterSpace = 0;
    params[0].Constants.Num32BitValues = 16;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &srv_range;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 2;
    desc.pParameters = params;
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers = &sampler;
    desc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    ID3DBlob* blob = nullptr;
    ID3DBlob* error_blob = nullptr;
    if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error_blob))) {
        if (error_blob && !g_logged_pipeline_failure) {
            Log("HDR ImGui root signature serialization failed: %s",
                static_cast<const char*>(error_blob->GetBufferPointer()));
            g_logged_pipeline_failure = true;
        }
        if (error_blob) error_blob->Release();
        return false;
    }

    const HRESULT hr = g_device->CreateRootSignature(
        0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&g_root_signature));
    blob->Release();
    if (FAILED(hr)) {
        if (!g_logged_pipeline_failure) {
            Log("HDR ImGui root signature creation failed (hr=0x%08lx).", static_cast<unsigned long>(hr));
            g_logged_pipeline_failure = true;
        }
        return false;
    }
    return true;
}

const char* VertexShaderSource()
{
    return
        "cbuffer vertexBuffer : register(b0) \
        {\
          float4x4 ProjectionMatrix; \
        };\
        struct VS_INPUT\
        {\
          float2 pos : POSITION;\
          float4 col : COLOR0;\
          float2 uv  : TEXCOORD0;\
        };\
        struct PS_INPUT\
        {\
          float4 pos : SV_POSITION;\
          float4 col : COLOR0;\
          float2 uv  : TEXCOORD0;\
        };\
        PS_INPUT main(VS_INPUT input)\
        {\
          PS_INPUT output;\
          output.pos = mul(ProjectionMatrix, float4(input.pos.xy, 0.0f, 1.0f));\
          output.col = input.col;\
          output.uv = input.uv;\
          return output;\
        }";
}

std::string PixelShaderSource(int brightness_nits, int saturation_percent)
{
    char prefix[96] = {};
    std::snprintf(prefix, sizeof(prefix),
        "#define UI_NITS %.1f\n#define UI_SATURATION %.3f\n",
        static_cast<float>(brightness_nits),
        static_cast<float>(saturation_percent) / 100.0f);

    return std::string(prefix) +
        "struct PS_INPUT\
        {\
          float4 pos : SV_POSITION;\
          float4 col : COLOR0;\
          float2 uv  : TEXCOORD0;\
        };\
        SamplerState sampler0 : register(s0);\
        Texture2D texture0 : register(t0);\
        float SrgbToLinear1(float c)\
        {\
          return (c <= 0.04045f) ? (c / 12.92f) : pow(abs(c + 0.055f) / 1.055f, 2.4f);\
        }\
        float3 SrgbToLinear(float3 color)\
        {\
          return float3(SrgbToLinear1(color.r), SrgbToLinear1(color.g), SrgbToLinear1(color.b));\
        }\
        float3 Rec709ToRec2020(float3 color)\
        {\
          return float3(\
            dot(color, float3(0.627402f, 0.329292f, 0.043306f)),\
            dot(color, float3(0.069095f, 0.919544f, 0.011360f)),\
            dot(color, float3(0.016394f, 0.088028f, 0.895578f)));\
        }\
        float3 LinearToST2084(float3 color)\
        {\
          const float m1 = 2610.0f / 4096.0f / 4.0f;\
          const float m2 = 2523.0f / 4096.0f * 128.0f;\
          const float c1 = 3424.0f / 4096.0f;\
          const float c2 = 2413.0f / 4096.0f * 32.0f;\
          const float c3 = 2392.0f / 4096.0f * 32.0f;\
          float3 cp = pow(abs(color), m1);\
          return pow((c1 + c2 * cp) / (1.0f + c3 * cp), m2);\
        }\
        float3 ApplySaturation(float3 color)\
        {\
          float luma = dot(color, float3(0.2627f, 0.6780f, 0.0593f));\
          return max(0.0f, luma + (color - luma) * UI_SATURATION);\
        }\
        float4 main(PS_INPUT input) : SV_Target\
        {\
          float4 out_col = input.col * texture0.Sample(sampler0, input.uv);\
          float3 linear_color = SrgbToLinear(saturate(out_col.rgb));\
          float3 rec2020 = Rec709ToRec2020(linear_color);\
          rec2020 = ApplySaturation(rec2020);\
          out_col.rgb = LinearToST2084(rec2020 * (UI_NITS / 10000.0f));\
          return out_col;\
        }";
}

bool CreatePipelineState(int brightness_nits, int saturation_percent)
{
    if (!g_device || g_rtv_format == DXGI_FORMAT_UNKNOWN) return false;
    if (g_pipeline_state && g_pipeline_brightness == brightness_nits &&
        g_pipeline_saturation == saturation_percent) {
        return true;
    }
    if (!CreateRootSignature()) return false;

    SafeRelease(g_pipeline_state);

    ID3DBlob* vertex_shader = nullptr;
    ID3DBlob* pixel_shader = nullptr;
    ID3DBlob* error_blob = nullptr;

    if (FAILED(D3DCompile(VertexShaderSource(), std::strlen(VertexShaderSource()), nullptr, nullptr, nullptr,
            "main", "vs_5_0", 0, 0, &vertex_shader, &error_blob))) {
        if (error_blob && !g_logged_compile_failure) {
            Log("HDR ImGui vertex shader compile failed: %s", static_cast<const char*>(error_blob->GetBufferPointer()));
            g_logged_compile_failure = true;
        }
        if (error_blob) error_blob->Release();
        return false;
    }

    std::string pixel_source = PixelShaderSource(brightness_nits, saturation_percent);
    if (FAILED(D3DCompile(pixel_source.c_str(), pixel_source.size(), nullptr, nullptr, nullptr,
            "main", "ps_5_0", 0, 0, &pixel_shader, &error_blob))) {
        if (error_blob && !g_logged_compile_failure) {
            Log("HDR ImGui pixel shader compile failed: %s", static_cast<const char*>(error_blob->GetBufferPointer()));
            g_logged_compile_failure = true;
        }
        if (error_blob) error_blob->Release();
        vertex_shader->Release();
        return false;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.NodeMask = 1;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.pRootSignature = g_root_signature;
    desc.VS = {vertex_shader->GetBufferPointer(), vertex_shader->GetBufferSize()};
    desc.PS = {pixel_shader->GetBufferPointer(), pixel_shader->GetBufferSize()};
    desc.SampleMask = UINT_MAX;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = g_rtv_format;
    desc.SampleDesc.Count = 1;

    static D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(ImDrawVert, pos), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(ImDrawVert, uv), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(ImDrawVert, col), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    desc.InputLayout = {layout, 3};

    D3D12_BLEND_DESC& blend = desc.BlendState;
    blend.RenderTarget[0].BlendEnable = true;
    blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_RASTERIZER_DESC& rasterizer = desc.RasterizerState;
    rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
    rasterizer.CullMode = D3D12_CULL_MODE_NONE;
    rasterizer.DepthClipEnable = true;

    D3D12_DEPTH_STENCIL_DESC& depth = desc.DepthStencilState;
    depth.DepthEnable = false;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depth.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;

    const HRESULT hr = g_device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&g_pipeline_state));
    vertex_shader->Release();
    pixel_shader->Release();
    if (FAILED(hr)) {
        if (!g_logged_pipeline_failure) {
            Log("HDR ImGui pipeline creation failed (hr=0x%08lx).", static_cast<unsigned long>(hr));
            g_logged_pipeline_failure = true;
        }
        return false;
    }

    g_pipeline_brightness = brightness_nits;
    g_pipeline_saturation = saturation_percent;
    g_logged_compile_failure = false;
    g_logged_pipeline_failure = false;
    return true;
}

void HdrPipelineCallback(const ImDrawList*, const ImDrawCmd*)
{
    if (!g_pipeline_state) return;
    auto* render_state = static_cast<ImGui_ImplDX12_RenderState*>(ImGui::GetPlatformIO().Renderer_RenderState);
    if (!render_state || !render_state->CommandList) return;
    render_state->CommandList->SetPipelineState(g_pipeline_state);
}

}  // namespace

void Initialize(ID3D12Device* device, DXGI_FORMAT rtv_format)
{
    g_device = device;
    g_rtv_format = rtv_format;
}

void Shutdown()
{
    SafeRelease(g_pipeline_state);
    SafeRelease(g_root_signature);
    g_pipeline_brightness = -1;
    g_pipeline_saturation = -1;
    g_logged_compile_failure = false;
    g_logged_pipeline_failure = false;
    g_device = nullptr;
    g_rtv_format = DXGI_FORMAT_UNKNOWN;
}

void BeginDrawList(ImDrawList* draw_list, bool hdr_enabled, float ui_brightness_nits, float ui_saturation)
{
    if (!draw_list || !hdr_enabled) return;
    const int brightness = ClampBrightness(ui_brightness_nits);
    const int saturation = ClampSaturation(ui_saturation);
    if (!CreatePipelineState(brightness, saturation)) return;
    draw_list->AddCallback(&HdrPipelineCallback, nullptr);
}

void EndDrawList(ImDrawList* draw_list, bool hdr_enabled)
{
    if (!draw_list || !hdr_enabled) return;
    ImDrawCallback reset_callback = ImGui::GetPlatformIO().DrawCallback_ResetRenderState;
    if (reset_callback) draw_list->AddCallback(reset_callback, nullptr);
}

}  // namespace radial_menu_mod::imgui_hdr_pipeline
