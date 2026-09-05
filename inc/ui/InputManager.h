#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include <functional>
#include <vector>

// Input event types recognized by the observer-pattern input system.
enum class InputType
{
    KeyPressed,
    KeyReleased,
    KeyDown,
    MouseButtonPressed,
    MouseButtonReleased,
    MouseButtonDown,
    MouseScroll,
    MouseMove
};

// Snapshot of one input occurrence delivered to matching subscribers.
struct InputEvent
{
    InputType type;
    int key = 0;
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    float scrollDelta = 0.0f;
    bool consumed = false;  // Set true by a handler to stop further dispatch this frame.
};

// Non-owning registration interface implemented by InputEventSubscriber<T,K>.
class IInputSubscriber
{
public:
    virtual ~IInputSubscriber() = default;
    virtual InputType GetType() const = 0;
    virtual int GetKey() const = 0;
    virtual void Trigger(const InputEvent& event) = 0;
};

// Process-wide dispatcher: the only place allowed to call raylib's
// IsKeyPressed/IsKeyDown/IsMouseButton*/GetMouseWheelMove (see Poll()).
// Input is a hardware-level concept (one keyboard/mouse), so a single
// instance is appropriate here; simulation-specific state does not belong in
// this dispatcher.
//
// Besides the subscriber/event system above (for "do X when key K is
// pressed" bindings), GUI code frequently needs a synchronous, inline answer
// ("is the mouse over this button AND was it just clicked") that doesn't fit
// a callback fired elsewhere. The static Is*/Get* query methods below cover
// that case: they forward 1:1 to raylib's own per-frame edge-detected
// functions (raylib itself is the single source of truth, sampled once per
// real frame regardless of who asks), so calling them mid-Update() is exactly
// as correct as calling raylib directly — the difference is that every other
// file in ui/ and scenes/ goes through here instead of naming raylib's
// IsKeyPressed/IsMouseButton*/GetMouseWheelMove directly, so this class stays
// the one place that would need to change if the input backend ever did.
class InputManager
{
public:
    static InputManager& Instance();

    // Polls raylib input state once per frame and dispatches to subscribers.
    void Poll();

    // Registers/removes a subscriber. Called by InputEventSubscriber's RAII lifecycle.
    void AddSubscriber(IInputSubscriber* subscriber);
    void RemoveSubscriber(IInputSubscriber* subscriber);

    float GetMouseX() const { return mouseX; }
    float GetMouseY() const { return mouseY; }

    // Synchronous input queries, for call sites that need an inline answer
    // rather than a subscriber callback. See class comment above.
    static bool IsKeyPressed(int key);
    static bool IsKeyPressedRepeat(int key);
    static bool IsKeyDown(int key);
    static bool IsKeyReleased(int key);
    static bool IsMouseButtonPressed(int button);
    static bool IsMouseButtonDown(int button);
    static bool IsMouseButtonReleased(int button);
    static float GetMouseWheelMove();

    // Suppresses action queries while a modal popup renders the frozen game
    // behind it. Mouse coordinates remain available for drawing.
    static void SetInputEnabled(bool enabled);
    static bool IsInputEnabled();

private:
    InputManager() = default;

    void PollKeyboardInput();
    void PollMouseInput();
    void DispatchEvent(const InputEvent& event);

    std::vector<IInputSubscriber*> subscribers;
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    bool inputEnabled = true;
};

// RAII observer for one (InputType, key) combination. Registers itself with
// the global InputManager at construction and unregisters at destruction, so
// aggregating one as a plain member is enough to manage the subscription's
// lifetime automatically:
//
//   class GuiPanel {
//       InputEventSubscriber<InputType::KeyPressed, KEY_ESCAPE> escClose{
//           [this](const InputEvent&) { Close(); }};
//   };
//
// Not copyable: copying would register the same callback under two
// addresses, and the destructor of one copy would unregister a pointer the
// other copy still expects to be live.
template<InputType T, int K>
class InputEventSubscriber : public IInputSubscriber
{
public:
    using Callback = std::function<void(const InputEvent&)>;

    InputEventSubscriber() { InputManager::Instance().AddSubscriber(this); }
    explicit InputEventSubscriber(Callback cb) : callback(std::move(cb))
    {
        InputManager::Instance().AddSubscriber(this);
    }
    ~InputEventSubscriber() override { InputManager::Instance().RemoveSubscriber(this); }

    InputEventSubscriber(const InputEventSubscriber&) = delete;
    InputEventSubscriber& operator=(const InputEventSubscriber&) = delete;

    void SetCallback(Callback cb) { callback = std::move(cb); }

    InputType GetType() const override { return T; }
    int GetKey() const override { return K; }

    void Trigger(const InputEvent& event) override
    {
        if (callback)
            callback(event);
    }

private:
    Callback callback;
};

#endif
