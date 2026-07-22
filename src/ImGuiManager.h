#pragma once
#include "pch.h"
#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>

namespace IIF::UI
{
    class ImGuiManager
    {
    public:
        static ImGuiManager& GetSingleton() {
            static ImGuiManager instance;
            return instance;
        }

        using WndProc_t = LRESULT(WINAPI*)(HWND, UINT, WPARAM, LPARAM);

        bool Install();
        void ToggleDisplay();
        bool IsVisible() const { return m_isVisible; }

    private:
        ImGuiManager() = default;
        ~ImGuiManager() = default;

        static void RenderCore(IDXGISwapChain* pSwapChain);

        static HRESULT WINAPI Present_Hook(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
        static LRESULT WINAPI WndProc_Hook(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

        static void* m_originalPresentVtEntry;
        static WndProc_t m_originalWndProc;

        static HWND m_windowHandle;
        static ID3D11Device* m_pDevice;
        static ID3D11DeviceContext* m_pContext;

        static bool m_isInitialized;
        static bool m_isVisible;
    };
}