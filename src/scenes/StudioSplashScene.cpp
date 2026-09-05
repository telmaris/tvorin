#include "scenes/Scenes.h"
#include "ui/UiText.h"

#include "raylib.h"

#include <algorithm>

namespace
{
    void DrawOutlinedText(const char* text, int x, int y, int fontSize)
    {
        constexpr int outlineSize = 2;
        const Color outline{0, 0, 0, 255};

        for (int offsetX = -outlineSize; offsetX <= outlineSize; ++offsetX)
            for (int offsetY = -outlineSize; offsetY <= outlineSize; ++offsetY)
                if (offsetX != 0 || offsetY != 0)
                    UiText::Draw(text, static_cast<float>(x + offsetX),
                                 static_cast<float>(y + offsetY), fontSize, outline);

        UiText::Draw(text, static_cast<float>(x), static_cast<float>(y), fontSize, WHITE);
    }
}

StudioSplashScene::StudioSplashScene()
{
    studioLogo = tvorin::ui::TextureHandle{LoadTexture("assets/studio_logo.png")};
}

StudioSplashScene::~StudioSplashScene()
{
    studioLogo.Reset();
}

void StudioSplashScene::OnActivated()
{
    Renderer::SetCustomCursorVisible(false);
}

void StudioSplashScene::OnDeactivated()
{
    Renderer::SetCustomCursorVisible(true);
    if (studioLogo.IsValid())
    {
        studioLogo.Reset();
    }
}

void StudioSplashScene::Update(double dt)
{
    elapsed += std::max(0.0, dt);
    if (!transitionRequested && elapsed >= 3.0)
    {
        transitionRequested = true;
        auto event = std::make_shared<ChangeSceneEvent>();
        event->sender = this;
        event->sceneName = "MainScene";
        event->previousSceneName = name;
        broker->Broadcast(event);
    }

    BeginDrawing();
    ClearBackground(BLACK);

    constexpr float logoWidth = 116.0f;
    constexpr float logoHeight = 119.0f;
    constexpr float logoTextGap = 12.0f;
    constexpr int titleFontSize = 42;
    constexpr int subtitleFontSize = 28;
    constexpr int lineGap = -10;

    const float textWidth = static_cast<float>(std::max(
        UiText::Measure("BARCZYK", titleFontSize),
        UiText::Measure("Games", subtitleFontSize)));
    const float compositionWidth = logoWidth + logoTextGap + textWidth;
    const float compositionLeft = (GetScreenWidth() - compositionWidth) * 0.5f;
    const float centerY = GetScreenHeight() * 0.5f;
    const float logoTop = centerY - logoHeight * 0.5f;

    if (studioLogo.IsValid())
    {
        DrawTexturePro(studioLogo.Get(),
                       {0.0f, 0.0f, static_cast<float>(studioLogo.Get().width),
                        static_cast<float>(studioLogo.Get().height)},
                       {compositionLeft, logoTop, logoWidth, logoHeight},
                       {0.0f, 0.0f}, 0.0f, WHITE);
    }

    const float textLeft = compositionLeft + logoWidth + logoTextGap;
    const float textBlockHeight = titleFontSize + lineGap + subtitleFontSize;
    const float textTop = centerY - textBlockHeight * 0.5f;
    DrawOutlinedText("BARCZYK", static_cast<int>(textLeft),
                     static_cast<int>(textTop), titleFontSize);
    DrawOutlinedText("Games", static_cast<int>(textLeft),
                     static_cast<int>(textTop + titleFontSize + lineGap), subtitleFontSize);

    // The renderer-owned present applies the global scene transition overlay
    // after the splash has drawn, so this logo also fades in from black.
    render.PresentFrame();
}
