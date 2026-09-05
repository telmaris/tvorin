#include "ui/InputManager.h"
#include "raylib.h"

#include <algorithm>
#include <set>
#include <utility>

InputManager& InputManager::Instance()
{
    static InputManager instance;
    return instance;
}

bool InputManager::IsKeyPressed(int key) { return Instance().inputEnabled && ::IsKeyPressed(key); }
bool InputManager::IsKeyPressedRepeat(int key) { return Instance().inputEnabled && ::IsKeyPressedRepeat(key); }
bool InputManager::IsKeyDown(int key) { return Instance().inputEnabled && ::IsKeyDown(key); }
bool InputManager::IsKeyReleased(int key) { return Instance().inputEnabled && ::IsKeyReleased(key); }
bool InputManager::IsMouseButtonPressed(int button) { return Instance().inputEnabled && ::IsMouseButtonPressed(button); }
bool InputManager::IsMouseButtonDown(int button) { return Instance().inputEnabled && ::IsMouseButtonDown(button); }
bool InputManager::IsMouseButtonReleased(int button) { return Instance().inputEnabled && ::IsMouseButtonReleased(button); }
float InputManager::GetMouseWheelMove() { return Instance().inputEnabled ? ::GetMouseWheelMove() : 0.0f; }

void InputManager::SetInputEnabled(bool enabled)
{
    Instance().inputEnabled = enabled;
}

bool InputManager::IsInputEnabled()
{
    return Instance().inputEnabled;
}

void InputManager::AddSubscriber(IInputSubscriber* subscriber)
{
    if (subscriber != nullptr)
        subscribers.push_back(subscriber);
}

void InputManager::RemoveSubscriber(IInputSubscriber* subscriber)
{
    subscribers.erase(std::remove(subscribers.begin(), subscribers.end(), subscriber), subscribers.end());
}

void InputManager::Poll()
{
    mouseX = static_cast<float>(GetMouseX());
    mouseY = static_cast<float>(GetMouseY());

    if (!inputEnabled)
    {
        // Drain queued text/key events while a transition, popup or focus
        // recovery frame suppresses input. They must not fire on the first
        // enabled frame after the overlay disappears.
        while (GetKeyPressed() != 0) {}
        while (GetCharPressed() != 0) {}
        return;
    }

    PollKeyboardInput();
    PollMouseInput();
}

void InputManager::DispatchEvent(const InputEvent& event)
{
    // Snapshot first: a triggered callback may add/remove subscribers (e.g. a
    // panel destroying its own subscribers when Close() runs mid-dispatch).
    auto snapshot = subscribers;
    for (auto* sub : snapshot)
    {
        if (sub == nullptr || sub->GetType() != event.type || sub->GetKey() != event.key)
            continue;

        InputEvent copy = event;
        sub->Trigger(copy);
        if (copy.consumed)
            break;
    }
}

void InputManager::PollKeyboardInput()
{
    // raylib queues discrete key-press events; draining it is exact and cheap
    // (no need to know in advance which keys subscribers care about).
    int key;
    while ((key = GetKeyPressed()) != 0)
        DispatchEvent(InputEvent{InputType::KeyPressed, key, mouseX, mouseY, 0.0f, false});

    // KeyDown/KeyReleased have no raylib event queue; poll only keys that
    // currently have a subscriber, deduplicated so shared bindings fire once.
    std::set<std::pair<InputType, int>> polled;
    for (auto* sub : subscribers)
    {
        if (sub == nullptr)
            continue;

        InputType type = sub->GetType();
        if (type != InputType::KeyDown && type != InputType::KeyReleased)
            continue;

        auto entry = std::make_pair(type, sub->GetKey());
        if (!polled.insert(entry).second)
            continue;

        bool active = (type == InputType::KeyDown) ? IsKeyDown(entry.second) : IsKeyReleased(entry.second);
        if (active)
            DispatchEvent(InputEvent{type, entry.second, mouseX, mouseY, 0.0f, false});
    }
}

void InputManager::PollMouseInput()
{
    // Mouse buttons are also queue-free in raylib; same dedup strategy as keys.
    std::set<std::pair<InputType, int>> polled;
    for (auto* sub : subscribers)
    {
        if (sub == nullptr)
            continue;

        InputType type = sub->GetType();
        if (type != InputType::MouseButtonPressed && type != InputType::MouseButtonReleased &&
            type != InputType::MouseButtonDown)
            continue;

        auto entry = std::make_pair(type, sub->GetKey());
        if (!polled.insert(entry).second)
            continue;

        bool active = false;
        switch (type)
        {
            case InputType::MouseButtonPressed:  active = IsMouseButtonPressed(entry.second); break;
            case InputType::MouseButtonReleased: active = IsMouseButtonReleased(entry.second); break;
            case InputType::MouseButtonDown:      active = IsMouseButtonDown(entry.second); break;
            default: break;
        }
        if (active)
            DispatchEvent(InputEvent{type, entry.second, mouseX, mouseY, 0.0f, false});
    }

    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f)
        DispatchEvent(InputEvent{InputType::MouseScroll, 0, mouseX, mouseY, wheel, false});

    Vector2 delta = GetMouseDelta();
    if (delta.x != 0.0f || delta.y != 0.0f)
        DispatchEvent(InputEvent{InputType::MouseMove, 0, mouseX, mouseY, 0.0f, false});
}
