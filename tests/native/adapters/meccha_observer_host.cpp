#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <chrono>
#include <iostream>
#include <thread>

using Microsoft::WRL::ComPtr;

namespace {

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  return DefWindowProcW(window, message, wparam, lparam);
}

template <typename T>
bool ok(HRESULT result, const T& operation) {
  if (SUCCEEDED(result)) {
    return true;
  }
  std::cerr << operation << " failed: 0x" << std::hex
            << static_cast<unsigned long>(result) << "\n";
  return false;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  const bool continuous =
      wcsstr(GetCommandLineW(), L"--continuous") != nullptr;
  const wchar_t class_name[] = L"VrClientMecchaObserverHost";
  WNDCLASSW window_class{};
  window_class.lpfnWndProc = windowProc;
  window_class.hInstance = instance;
  window_class.lpszClassName = class_name;
  if (!RegisterClassW(&window_class)) {
    return 10;
  }

  HWND window = CreateWindowExW(
      0,
      class_name,
      L"VRClient Meccha Observer Fixture",
      WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      640,
      360,
      nullptr,
      nullptr,
      instance,
      nullptr);
  if (window == nullptr) {
    return 11;
  }

  ComPtr<IDXGIFactory4> factory;
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12CommandQueue> queue;
  if (!ok(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2") ||
      !ok(D3D12CreateDevice(
              nullptr,
              D3D_FEATURE_LEVEL_11_0,
              IID_PPV_ARGS(&device)),
          "D3D12CreateDevice")) {
    DestroyWindow(window);
    return 12;
  }

  D3D12_COMMAND_QUEUE_DESC queue_desc{};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (!ok(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)),
          "CreateCommandQueue")) {
    DestroyWindow(window);
    return 13;
  }

  DXGI_SWAP_CHAIN_DESC1 swapchain_desc{};
  swapchain_desc.Width = 640;
  swapchain_desc.Height = 360;
  swapchain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swapchain_desc.SampleDesc.Count = 1;
  swapchain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapchain_desc.BufferCount = 2;
  swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

  ComPtr<IDXGISwapChain1> swapchain1;
  ComPtr<IDXGISwapChain3> swapchain;
  if (!ok(factory->CreateSwapChainForHwnd(
              queue.Get(),
              window,
              &swapchain_desc,
              nullptr,
              nullptr,
              &swapchain1),
          "CreateSwapChainForHwnd") ||
      !ok(swapchain1.As(&swapchain), "IDXGISwapChain3")) {
    DestroyWindow(window);
    return 14;
  }

  for (int i = 0; i < 3; ++i) {
    if (!ok(swapchain->Present(0, 0), "Present")) {
      DestroyWindow(window);
      return 15;
    }
  }
  const HRESULT resize_result = swapchain->ResizeBuffers(
      2, 800, 450, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
  if (!ok(resize_result, "ResizeBuffers")) {
    std::fprintf(
        stderr,
        "Device removed reason after ResizeBuffers: 0x%08lx\n",
        static_cast<unsigned long>(device->GetDeviceRemovedReason()));
    DestroyWindow(window);
    return 16;
  }
  const HRESULT present_after_resize = swapchain->Present(0, 0);
  if (!ok(present_after_resize, "Present after resize")) {
    std::fprintf(
        stderr,
        "Device removed reason after Present: 0x%08lx\n",
        static_cast<unsigned long>(device->GetDeviceRemovedReason()));
    DestroyWindow(window);
    return 16;
  }

  // The injected payload starts its hook-priming worker asynchronously. Keep a
  // short second observation cycle in the bounded fixture so the test does not
  // depend on winning a process-start scheduling race on a busy host.
  if (!continuous) {
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    for (int i = 0; i < 4; ++i) {
      if (!ok(swapchain->Present(0, 0), "Settled Present")) {
        DestroyWindow(window);
        return 17;
      }
    }
    if (!ok(
            swapchain->ResizeBuffers(
                2, 800, 450, DXGI_FORMAT_R8G8B8A8_UNORM, 0),
            "Settled ResizeBuffers") ||
        !ok(swapchain->Present(0, 0), "Settled Present after resize")) {
      DestroyWindow(window);
      return 18;
    }
  }

  if (continuous) {
    for (int i = 0; i < 360; ++i) {
      swapchain->Present(0, 0);
      std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
  } else {
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
  }
  DestroyWindow(window);
  return 0;
}
