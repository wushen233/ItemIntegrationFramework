#include "GUIEditorKeyHandler.h"
#include "pch.h"
#include <vector>

GUIEditorKeyHandler* GUIEditorKeyHandler::GetSingleton()
{
    static GUIEditorKeyHandler instance;
    return &instance;
}

void GUIEditorKeyHandler::RegisterSink()
{
    auto* menuControls = RE::MenuControls::GetSingleton();
    if (menuControls) {
        menuControls->handlers.push_back(GetSingleton());
        REX::INFO("GUIEditor key handler registered.");
    } else {
        REX::ERROR("GUIEditor key handler register failed: MenuControls is null.");
    }
}

KeyHandlerEvent GUIEditorKeyHandler::Register(uint32_t bsButtonCode, KeyEventType eventType, KeyCallback callback)
{
    std::unique_lock lock(_mutex);
    KeyHandlerEvent handle = _nextHandle++;
    _handleMap[handle] = { bsButtonCode, eventType };

    auto& cbs = _registeredCallbacks[bsButtonCode];
    if (eventType == KeyEventType::KEY_DOWN) {
        cbs.down[handle] = std::move(callback);
    } else {
        cbs.up[handle] = std::move(callback);
    }

    return handle;
}

void GUIEditorKeyHandler::Unregister(KeyHandlerEvent handle)
{
    std::unique_lock lock(_mutex);
    auto it = _handleMap.find(handle);
    if (it == _handleMap.end()) return;

    auto& cbs = _registeredCallbacks[it->second.key];
    if (it->second.type == KeyEventType::KEY_DOWN) {
        cbs.down.erase(handle);
    } else {
        cbs.up.erase(handle);
    }
    _handleMap.erase(it);
}

bool GUIEditorKeyHandler::ShouldHandleEvent(const RE::InputEvent* a_event)
{
    if (!a_event) return false;
    return a_event->eventType == RE::INPUT_EVENT_TYPE::kButton &&
        a_event->device == RE::INPUT_DEVICE::kKeyboard;
}

void GUIEditorKeyHandler::OnButtonEvent(const RE::ButtonEvent* a_event)
{
    if (!a_event) return;
    const uint32_t key = static_cast<uint32_t>(a_event->GetBSButtonCode());
    const bool isDown = a_event->QJustPressed();
    const bool isUp = (a_event->QAnalogValue() == 0.0F);
    if (!isDown && !isUp) return;

    std::vector<KeyCallback> callbacks;
    {
        std::shared_lock lock(_mutex);
        auto it = _registeredCallbacks.find(key);
        if (it == _registeredCallbacks.end()) return;

        if (isDown) {
            callbacks.reserve(it->second.down.size());
            for (auto& [handle, cb] : it->second.down) {
                callbacks.push_back(cb);
            }
        } else {
            callbacks.reserve(it->second.up.size());
            for (auto& [handle, cb] : it->second.up) {
                callbacks.push_back(cb);
            }
        }
    }

    if (isDown) {
        for (auto& cb : callbacks) cb();
    } else if (isUp) {
        for (auto& cb : callbacks) {
            cb();
        }
    }
}
