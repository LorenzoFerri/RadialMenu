#include "render/d3d/dx12_vtable.h"

#include "core/common.h"

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

namespace radial_menu_mod::dx12_vtable {

bool DiscoverHookTargets(HookTargets& targets)
{
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"RSM_Dummy_Deferred";
    RegisterClassExW(&window_class);

    HWND dummy_window = CreateWindowExW(0, L"RSM_Dummy_Deferred", L"", WS_OVERLAPPED, 0, 0, 100, 100, nullptr,
        nullptr, window_class.hInstance, nullptr);
    if (!dummy_window) {
        Log("Deferred D3D discovery failed: CreateWindowEx failed.");
        return false;
    }

    IDXGIFactory* factory = nullptr;
    IDXGIAdapter* adapter = nullptr;
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* command_queue = nullptr;
    IDXGISwapChain* swap_chain = nullptr;
    IDXGISwapChain3* swap_chain3 = nullptr;

    auto cleanup = [&]() {
        if (swap_chain3) swap_chain3->Release();
        if (swap_chain) swap_chain->Release();
        if (command_queue) command_queue->Release();
        if (device) device->Release();
        if (adapter) adapter->Release();
        if (factory) factory->Release();
        DestroyWindow(dummy_window);
        UnregisterClassW(L"RSM_Dummy_Deferred", GetModuleHandleW(nullptr));
    };

    using CreateDXGIFactoryFn = HRESULT(WINAPI*)(REFIID, void**);
    using D3D12CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);

    const HMODULE dxgi_module = GetModuleHandleW(L"dxgi.dll");
    const HMODULE d3d12_module = GetModuleHandleW(L"d3d12.dll");
    if (!dxgi_module || !d3d12_module) {
        Log("Deferred D3D discovery failed: dxgi/d3d12 module missing.");
        cleanup();
        return false;
    }

    const auto create_factory = reinterpret_cast<CreateDXGIFactoryFn>(GetProcAddress(dxgi_module, "CreateDXGIFactory"));
    const auto create_device = reinterpret_cast<D3D12CreateDeviceFn>(GetProcAddress(d3d12_module, "D3D12CreateDevice"));
    if (!create_factory || !create_device) {
        Log("Deferred D3D discovery failed: factory/device export missing.");
        cleanup();
        return false;
    }

    if (FAILED(create_factory(IID_PPV_ARGS(&factory)))) {
        Log("Deferred D3D discovery failed: CreateDXGIFactory failed.");
        cleanup();
        return false;
    }
    if (factory->EnumAdapters(0, &adapter) == DXGI_ERROR_NOT_FOUND) {
        Log("Deferred D3D discovery failed: EnumAdapters failed.");
        cleanup();
        return false;
    }
    if (FAILED(create_device(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
        Log("Deferred D3D discovery failed: D3D12CreateDevice failed.");
        cleanup();
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue)))) {
        Log("Deferred D3D discovery failed: CreateCommandQueue failed.");
        cleanup();
        return false;
    }

    DXGI_SWAP_CHAIN_DESC swap_chain_desc{};
    swap_chain_desc.BufferDesc.Width = 100;
    swap_chain_desc.BufferDesc.Height = 100;
    swap_chain_desc.BufferDesc.RefreshRate.Numerator = 60;
    swap_chain_desc.BufferDesc.RefreshRate.Denominator = 1;
    swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap_chain_desc.SampleDesc.Count = 1;
    swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_chain_desc.BufferCount = 2;
    swap_chain_desc.OutputWindow = dummy_window;
    swap_chain_desc.Windowed = TRUE;
    swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swap_chain_desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    if (FAILED(factory->CreateSwapChain(command_queue, &swap_chain_desc, &swap_chain))) {
        Log("Deferred D3D discovery failed: CreateSwapChain failed.");
        cleanup();
        return false;
    }

    swap_chain->QueryInterface(IID_PPV_ARGS(&swap_chain3));

    void** swap_chain_vtable = *reinterpret_cast<void***>(swap_chain);
    void** queue_vtable = *reinterpret_cast<void***>(command_queue);
    targets.present = swap_chain_vtable[8];
    targets.set_fullscreen_state = swap_chain_vtable[10];
    targets.resize_buffers = swap_chain_vtable[13];
    if (swap_chain3) {
        void** swap_chain3_vtable = *reinterpret_cast<void***>(swap_chain3);
        targets.resize_buffers1 = swap_chain3_vtable[39];
    }
    targets.execute_command_lists = queue_vtable[10];

    cleanup();
    return targets.present != nullptr && targets.resize_buffers != nullptr &&
           targets.set_fullscreen_state != nullptr && targets.execute_command_lists != nullptr;
}

}  // namespace radial_menu_mod::dx12_vtable
