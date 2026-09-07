#ifndef GUI_HANDLER_H
#define GUI_HANDLER_H

// Shared scene-input gate. It prevents the key edge that changes a scene from
// firing again before raylib presents a frame and polls fresh input.
class IGuiHandler
{
public:
    virtual ~IGuiHandler() = default;

    // Call before drawing; the frame must still be presented after a scene change.
    bool ProcessGuiInput(double dt);

    // Called centrally whenever a scene becomes active.
    void ResetGuiInputGate();

protected:
    // Scene-specific per-frame input handling. Only called with the gate open.
    virtual void HandleGuiInput(double dt) = 0;

    virtual void OnNavigateBack() {}

    // Polls ESC and routes it to OnNavigateBack().
    bool HandleBackNavigation();

private:
    bool guiInputArmed{false};
    int framesSinceActivation{0};
};

#endif
