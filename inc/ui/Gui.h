#ifndef GUI_H
#define GUI_H

#include "core/Types.h"
#include "data/Resource.h"
#include "ui/InputManager.h"
#include "ui/RaylibResource.h"
#include "ui/UiText.h"
#include "raylib.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// raylib exposes GOLD as a color macro; GUI code also uses ResourceType::GOLD.
#ifdef GOLD
#undef GOLD
#endif

class Building;
class GameScene;

// Base rectangle widget with anchor-based layout support.
class UiWidget
{
public:
    virtual ~UiWidget() = default;

    // Draws and updates widget state for the current frame.
    virtual void Update(double dt) = 0;

    // Draws screen-space content that must sit above the ordinary widget pass.
    // The default is intentionally empty; current overlays only need two
    // explicit passes instead of a general z-index system.
    virtual void DrawOverlay(double dt) { (void)dt; }

    // Sets absolute pixel position.
    inline void ChangePosition(int x, int y)
    {
        pos.x = x;
        pos.y = y;
    }
    // Sets absolute pixel size.
    inline void ChangeSize(int sizeX, int sizeY)
    {
        size.x = sizeX;
        size.y = sizeY;
    }

    // Sets normalized position anchor relative to the window.
    inline void ChangePositionAnchor(Vec2f anchor)
    {
        posAnchor = anchor;
    }
    // Sets normalized size anchor relative to the window.
    inline void ChangeSizeAnchor(Vec2f anchor)
    {
        sizeAnchor = anchor;
    }

    // Recomputes pixel rectangle from current anchors and window size.
    virtual void UpdateSize(Vec2i windowSize)
    {
        pos = Vec2i{static_cast<int>(windowSize.x * posAnchor.x), static_cast<int>(windowSize.y * posAnchor.y)};
        size = Vec2i{static_cast<int>(windowSize.x * sizeAnchor.x), static_cast<int>(windowSize.y * sizeAnchor.y)};
    }

    // Returns true when a screen-space point is inside widget bounds.
    inline bool ContainsPoint(Vec2i point) const
    {
        return point.x >= pos.x && point.x <= pos.x + size.x &&
               point.y >= pos.y && point.y <= pos.y + size.y;
    }

    Vec2i pos{100, 100};
    Vec2i size{200, 100};

    Vec2f posAnchor{0.2f, 0.2f};
    Vec2f sizeAnchor{0.2f, 0.1f};
};

// Clickable UI button with optional normal and hover textures.
class UiButton : public UiWidget
{
public:
    UiButton();
    // Draws the button, handles hover state and dispatches clicks.
    void Update(double dt) override;
    // Replaces button label text.
    inline void ChangeText(std::string stryng)
    {
        text = stryng;
    }

    // Toggles drawing of the text label over textured buttons.
    inline void SetDrawText(bool shouldDraw)
    {
        drawText = shouldDraw;
    }

    // Executes the assigned click callback.
    virtual void OnClick()
    {
        if (func)
            func();
    }

    std::string text{"Default button text"};
    std::function<void()> func;
    bool drawText{true};
};

// Boolean toggle widget.
class CheckBox : public UiWidget
{
public:
    CheckBox()
    {
        size = Vec2i{30, 30};
        sizeAnchor = Vec2f{0.05f, 0.05f};
    }

    // Draws the checkbox and toggles it when clicked.
    void Update(double dt) override;

    // Replaces checkbox label text.
    inline void ChangeText(std::string stryng)
    {
        text = stryng;
    }

    // Returns the current checked state.
    inline bool IsActive() { return currentState; }
    // Returns true once after the checked state changes.
    inline bool HasChanged()
    {
        if (previousState != currentState)
        {
            previousState = currentState;
            return true;
        }
        return false;
    }

    bool currentState{false};
    bool previousState{false};

    std::string text{"Default checkbox text"};
};

// Horizontal scalar input backed by raygui's slider bar.
class SliderBar : public UiWidget
{
public:
    SliderBar()
    {
        size = Vec2i{30, 30};
        sizeAnchor = Vec2f{0.4f, 0.05f};
    }

    // Draws the slider and updates the current value.
    void Update(double dt) override;

    // Replaces slider label text.
    inline void ChangeText(std::string stryng)
    {
        text = stryng;
    }

    // Returns the current slider value.
    float GetValue() const
    {
        return currentValue;
    }

    // Returns true once after the slider value changes.
    inline bool HasChanged()
    {
        if (previousValue != currentValue)
        {
            previousValue = currentValue;
            return true;
        }
        return false;
    }

    float currentValue{0.0f};
    float previousValue{0.0f};
    std::string text{"Default slider bar text"};
};

// Vertical box that distributes child widgets evenly inside its bounds.
class VBox : public UiWidget
{
public:
    // Updates all child widgets.
    void Update(double dt) override;

    // Recomputes this box and its children from window anchors.
    inline void UpdateSize(Vec2i windowSize)
    {
        pos = Vec2i{static_cast<int>(windowSize.x * posAnchor.x), static_cast<int>(windowSize.y * posAnchor.y)};
        size = Vec2i{static_cast<int>(windowSize.x * sizeAnchor.x), static_cast<int>(windowSize.y * sizeAnchor.y)};

        int childrenCount = children.size();
        if(childrenCount == 0) return;

        Vec2i childrenSize{size.x, size.y / childrenCount};

        for (int i = 0; i < childrenCount; i++)
        {
            children[i]->size = childrenSize;
            children[i]->pos = Vec2i{pos.x, pos.y + (childrenSize.y + margins.y) * i};
        }
    }

    // Recomputes child rectangles using current absolute box bounds.
    void UpdateSize()
    {
        int childrenCount = children.size();
        if(childrenCount == 0) return;
        Vec2i childrenSize{size.x, size.y / childrenCount};

        for (int i = 0; i < childrenCount; i++)
        {
            children[i]->size = childrenSize;
            children[i]->pos = Vec2i{pos.x, pos.y + (childrenSize.y + margins.y) * i};
        }
    }

    // Adds one child widget and refreshes layout.
    void AddChild(std::shared_ptr<UiWidget> child)
    {
        children.push_back(child);
        UpdateSize();
    }

    // Removes all child widgets.
    void ClearChildren()
    {
        children.clear();
    }

    std::vector<std::shared_ptr<UiWidget>> children;

    Vec4i margins{0, 5, 0, 0};
};

// Horizontal box that distributes child widgets evenly inside its bounds.
class HBox : public UiWidget
{
public:
    // Updates all child widgets.
    void Update(double dt) override;

    // Recomputes this box and its children from window anchors.
    inline void UpdateSize(Vec2i windowSize)
    {
        pos = Vec2i{static_cast<int>(windowSize.x * posAnchor.x), static_cast<int>(windowSize.y * posAnchor.y)};
        size = Vec2i{static_cast<int>(windowSize.x * sizeAnchor.x), static_cast<int>(windowSize.y * sizeAnchor.y)};

        int childrenCount = children.size();
        if(childrenCount == 0) return;
        Vec2i childrenSize{size.x / childrenCount, size.y};

        for (int i = 0; i < childrenCount; i++)
        {
            children[i]->size = childrenSize;
            children[i]->pos = Vec2i{pos.x + (childrenSize.x + margins.x) * i, pos.y};
        }
    }

    // Recomputes child rectangles using current absolute box bounds.
    void UpdateSize()
    {
        int childrenCount = children.size();
        if(childrenCount == 0) return;
        Vec2i childrenSize{size.x / childrenCount, size.y};

        for (int i = 0; i < childrenCount; i++)
        {
            children[i]->size = childrenSize;
            children[i]->pos = Vec2i{pos.x + (childrenSize.x + margins.x) * i, pos.y};
        }
    }

    // Adds one child widget and refreshes layout.
    void AddChild(std::shared_ptr<UiWidget> child)
    {
        children.push_back(child);
        UpdateSize();
    }

    std::vector<std::shared_ptr<UiWidget>> children;

    Vec4i margins{0, 5, 0, 0};
};

// Fixed-size text input widget used by menu forms.
class TextBox : public UiWidget
{
    public:

    // Draws the input and stores edited text.
    void Update(double dt) override;

    // Replaces the current text value.
    inline void ChangeText(std::string stryng)
    {
        text = stryng;
    }

    // Replaces the current editable value.
    void SetValue(const std::string& value);

    // Returns true once after the text value changes.
    inline bool HasChanged()
    {
        if (text.compare(textOutput))
        {
            text = textOutput;
            return true;
        }
        return false;
    }

    // Returns the current text value.
    std::string GetText() const { return text; }

    char textOutput[256] = "\0";
    std::string text{"Default textbox text"};
};

// Static text label used by forms.
class UiLabel : public UiWidget
{
    public:
        void Update(double dt) override;
        void ChangeText(std::string value) { text = std::move(value); }

        std::string text;
        int fontSize{22};
        Color color{RAYWHITE};
};

// Reserved dropdown widget shell for future option selectors.
class DropdownBox : public UiWidget
{
    public:

    // Draws and updates the dropdown state.
    void Update(double dt) override;

    // Replaces dropdown label text.
    inline void ChangeText(std::string stryng)
    {
        text = stryng;
    }

    // Returns true once after selected text changes.
    inline bool HasChanged()
    {
        if (text.compare(textOutput))
        {
            text = textOutput;
            return true;
        }
        return false;
    }

    // Returns current selected text.
    std::string GetText() { return text; }

    char* textOutput{nullptr};
    std::string text{"Default dropdown text"};
};

// Linear progress indicator with clamped 0..1 value.
class ProgressBar : public UiWidget
{
    public:
        // Draws progress bar and optional label.
        void Update(double dt) override;

        // Replaces progress label text.
        inline void ChangeText(std::string stryng)
        {
            text = stryng;
        }

        // Sets current progress value, clamped to 0..1.
        inline void SetValue(float val)
        {
            value = std::clamp(val, 0.0f, 1.0f);
        }

        float value{0.0f};
        std::string text{"Progress"};
};

// Textured image widget that can draw contained or cover-scaled images.
class UiImage : public UiWidget
{
    public:
        ~UiImage() override;

        // Draws the loaded image when available.
        void Update(double dt) override;
        // Loads a texture from disk for this widget.
        bool LoadTextureFromFile(const std::string& path);

        tvorin::ui::TextureHandle texture{};
        bool hasTexture{false};
        bool cover{false};
};

// Decorative surface rendered behind a group of menu controls. It deliberately
// does not handle input, so the widgets drawn after it keep their normal hit
// testing and focus behaviour.
class UiPanel : public UiWidget
{
public:
    void Update(double dt) override;

    Color fill{8, 10, 13, 228};
    Color border{103, 107, 113, 235};
    float cornerRadius{0.045f};
    float borderThickness{1.5f};
};

// Layered menu background with a gently scrolling second layer. The widget
// owns the textures so scenes only need to provide their asset directory.
class UiParallaxBackground : public UiWidget
{
public:
    ~UiParallaxBackground() override;

    bool LoadFromDirectory(const std::string& directory);
    void SetScrollSpeed(float pixelsPerSecond) { scrollSpeed = pixelsPerSecond; }
    void SetScrollLayer(std::size_t layerIndex) { scrollLayerIndex = layerIndex; }
    void Update(double dt) override;

private:
    void ClearTextures();

    std::vector<tvorin::ui::TextureHandle> layers;
    float scrollSpeed{3.2f};
    std::size_t scrollLayerIndex{1};
    double scrollOffset{0.0};
};

// Small reusable frame animation for menu/UI artwork. It intentionally keeps
// motion presentation-only: a frame sequence can be added later without
// changing the owning scene, while the optional sine motion provides the
// subtle floating used by the main-menu logo.
class UiAnimation : public UiWidget
{
public:
    ~UiAnimation() override;

    bool AddFrameFromFile(const std::string& path);
    void ClearFrames();
    void SetFrameDuration(float seconds);
    void SetFloating(float amplitudePixels, float periodSeconds, float phase = 0.0f);
    void Reset();
    void Update(double dt) override;

private:
    std::vector<tvorin::ui::TextureHandle> frames;
    float frameDuration{0.18f};
    float floatAmplitude{0.0f};
    float floatPeriod{1.0f};
    float floatPhase{0.0f};
    double elapsed{0.0};
};

// Calls a user-supplied function each frame — useful for inline custom draws inside render.Draw().
class FuncWidget : public UiWidget
{
public:
    void Update(double dt) override { if (func) func(dt); }
    std::function<void(double)> func;
};

// Reusable modal popup. Opening and dismissing it notify the owner through the
// modal callback, so every popup gets the same pause/unpause semantics.
class PopupWindowWidget : public UiWidget
{
public:
    PopupWindowWidget();

    void Update(double dt) override;
    void UpdateSize(Vec2i windowSize) override;
    void Show(std::string title, std::string body, std::string actionText,
              std::function<void()> action);
    void Hide();
    void SetModalStateCallback(std::function<void(bool)> callback);
    void SetHorizontalOffset(int pixels) { horizontalOffset = pixels; }
    void SetDimBackground(bool enabled) { dimBackground = enabled; }
    bool IsVisible() const { return visible; }

private:
    UiButton actionButton;
    std::string title;
    std::string body;
    std::function<void(bool)> modalStateCallback;
    int horizontalOffset{0};
    bool dimBackground{true};
    bool visible{false};
};

// Compact objective list shown below the strategic HUD during the tutorial.
// Completed objectives remain visible and are rendered with a green marker and
// strike-through so the player can follow the whole stage at a glance.
class TutorialTaskWidget : public UiWidget
{
public:
    void Update(double dt) override;
    void UpdateSize(Vec2i windowSize) override;

    void SetTasks(std::string stageTitle, std::vector<std::pair<std::string, bool>> newTasks)
    {
        title = std::move(stageTitle);
        tasks = std::move(newTasks);
    }

    std::string title;
    std::vector<std::pair<std::string, bool>> tasks;
};

// Texture atlas used by UI panels to render resource icons.
struct ResourceIconAtlas
{
    // Loads atlas texture and records one icon cell size.
    void Load(const std::string& path, Vec2i iconSize);
    // Releases the atlas while the raylib window is still alive.
    void Unload();
    // Returns true after the atlas texture is loaded.
    bool IsLoaded() const { return loaded; }
    // Returns atlas rectangle for a resource type.
    Rectangle GetRect(ResourceType type) const;

    tvorin::ui::TextureHandle texture{};
    Vec2i size{64, 64};
    bool loaded{false};
};

// UiText / Tooltip moved to inc/ui/UiText.h so tools/ can link them without the
// rest of the GUI. Included above; nothing else had to change.

// Building information panel shown after selecting map objects.
class GuiPanel : public UiWidget
{
    public:
        GuiPanel();
        // Draws the panel and building-specific content.
        void Update(double dt) override;
        // Recomputes panel rectangle and child widget layout from window size.
        void UpdateSize(Vec2i windowSize) override;
        // Sets the building currently displayed by the panel.
        void SetBuilding(Building* ptr);
        // Returns true when the panel has a building target.
        bool HasBuilding() const { return building != nullptr; }
        // Returns the building currently displayed by the panel.
        Building* GetBuilding() const { return building; }
        // Closes the panel (clears its building target). Public so both the
        // X button and the ESC subscriber below can share one code path.
        void Close() { SetBuilding(nullptr); }
        bool ConsumeDestroyRequest()
        {
            bool requested = destroyRequested;
            destroyRequested = false;
            return requested;
        }
        // Loads shared resource icon atlas used by all panels.
        static void LoadResourceAtlas(const std::string& path, Vec2i iconSize = {64, 64});
        // Releases the shared atlas before the native window is closed.
        static void UnloadResourceAtlas();
        // Loads shared UI font used by custom panel text and raygui controls.
        static void LoadUiFont(const std::string& path);
        // Loads the dense UI font and makes it the default for controls and
        // non-heading text. Display titles continue to select EB Garamond.
        static void LoadUiPlainFont(const std::string& path, int baseSize = 32);
        // Draws one resource icon using the shared atlas or a color fallback.
        static void DrawResourceIcon(ResourceType type, Rectangle dest);
        // Scrolls generic panel content when a panel section overflows.
        void ScrollContent(float wheel);

        // Clips subsequent draws to a content rectangle (ETAP 6.1). Pair with
        // EndContentClip(); nesting is not supported (raylib scissor is a
        // single active rect, not a stack).
        void BeginContentClip(Rectangle contentArea);
        void EndContentClip();

        // Replaces panel title text.
        inline void ChangeText(std::string stryng)
        {
            text = stryng;
        }

        // Highlights worker/food/efficiency rows while a tutorial explains
        // how population supply affects a production building.
        void SetTutorialHighlight(bool enabled) { tutorialHighlight = enabled; }

    protected:
        // Draws the shared panel chrome (background, title bar, drag-to-move,
        // close button + title text) and reports the content area below the
        // title bar. Returns false when the close button was just clicked —
        // callers must stop drawing content for this frame in that case
        // (mirrors the panel's own prior behavior of returning immediately).
        bool DrawChrome(double dt, Rectangle& outContentArea);

    public:
        std::string text{"Gui Panel"};
        Building* building{nullptr};
        // TD(etap-8): set by the owning GuiSystem (mirrors selectedBuildingWidget.scene
        // etc. — see BasicMapViewSystem's constructor) so panel content that submits a
        // command (recruitment) can call scene->SubmitLocalCommand without ever
        // mutating simulation state directly.
        GameScene* scene{nullptr};
        ProgressBar progressBar;
        UiButton lockButton;
        UiButton destroyButton;
        UiButton recipeButton;
        bool destroyRequested{false};
        bool tutorialHighlight{false};
        bool dragging{false};
        Vec2i dragOffset{0, 0};
        float contentScrollOffset{0.0f};
        float maxContentScrollOffset{0.0f};
        float recruitmentQueueScrollOffset{0.0f};
        float recruitmentQueueMaxScrollOffset{0.0f};
        bool recruitmentQueueScrollbarDragging{false};
        float recruitmentQueueScrollbarDragOffset{0.0f};
        std::uint64_t selectedGarrisonTaskGroupId{0};
        bool contentScrollbarDragging{false};
        float contentScrollbarDragOffset{0.0f};
        bool roadPriorityPickerOpen{false};
        // Connectivity is expensive to query, so the selected panel keeps a
        // short-lived presentation cache instead of walking the road network
        // on every draw.
        int presentationConnectivityBuildingId{-1};
        double presentationConnectivityAge{0.0};
        bool presentationConnectivityKnown{false};
        bool presentationRoadDisconnected{false};

        // ESC closes this panel (ETAP 6.1): the wrapped subscriber
        // (de)registers itself with InputManager via RAII, so a panel that
        // goes out of scope also stops listening for free. It stays
        // registered for the panel's entire lifetime though — not just while
        // this panel happens to be the visible one — so it guards on
        // HasBuilding() to stay a no-op whenever there's nothing open. The
        // owning GuiSystem is expected to clear the building (e.g.
        // BasicMapViewSystem::OnDeactivate) whenever another system becomes
        // active, so this can't reach across systems and close a panel that
        // isn't even on screen.
        InputEventSubscriber<InputType::KeyPressed, KEY_ESCAPE> escClose{
            [this](const InputEvent&) { if (HasBuilding()) Close(); }};
};

// Standard side panel for selected building details.
class BuildingInfoPanel : public GuiPanel
{
public:
    using GuiPanel::GuiPanel;
    // Production buildings use the compact information layout; recruitment
    // and other building panels retain the taller shared layout.
    void UpdateSize(Vec2i windowSize) override;
};

// Full-screen technology tree panel used by university buildings.
class ResearchPanel : public GuiPanel
{
public:
    // Draws categorized research trees for the selected university owner.
    void Update(double dt) override;
    // Uses a larger modal-like layout than standard side panels.
    void UpdateSize(Vec2i windowSize) override;
    void AdjustTreeZoom(Vec2i point, float wheel);

    Vec2f treePanOffset{0.0f, 0.0f};
    float treeZoom{0.78f};
    bool treePanning{false};
    Vec2f lastTreePanMouse{0.0f, 0.0f};
    std::string selectedTagFilter;
    std::function<void(const std::string&, Building*)> researchRequested;
};
#endif
