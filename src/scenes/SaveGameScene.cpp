#include "scenes/Scenes.h"
#include "scenes/SceneUtils.h"

SaveGameScene::SaveGameScene()
{
    backButton.ChangeText("Back");
    backButton.ChangePositionAnchor(Vec2f{0.7f, 0.7f});
    backButton.func = std::bind(&SaveGameScene::OnBackPressed, this);

    saveName.ChangeText("");
    saveName.ChangeSizeAnchor(Vec2f{0.32f, 0.07f});
    saveName.ChangePositionAnchor(Vec2f{0.1f, 0.1f});

    newSaveButton.ChangeText("Create Save");
    newSaveButton.ChangeSizeAnchor(Vec2f{0.18f, 0.07f});
    newSaveButton.ChangePositionAnchor(Vec2f{0.44f, 0.1f});
    newSaveButton.func = std::bind(&SaveGameScene::OnNewSavePressed, this);

    saveButtons.ChangeSizeAnchor(Vec2f{0.36f, 0.36f});
    saveButtons.ChangePositionAnchor(Vec2f{0.1f, 0.22f});

    confirmOverwriteButton.ChangeText("Confirm overwrite");
    confirmOverwriteButton.ChangeSizeAnchor(Vec2f{0.24f, 0.07f});
    confirmOverwriteButton.ChangePositionAnchor(Vec2f{0.5f, 0.32f});
    confirmOverwriteButton.func = std::bind(&SaveGameScene::OnConfirmOverwrite, this);

    cancelOverwriteButton.ChangeText("Cancel");
    cancelOverwriteButton.ChangeSizeAnchor(Vec2f{0.16f, 0.07f});
    cancelOverwriteButton.ChangePositionAnchor(Vec2f{0.5f, 0.41f});
    cancelOverwriteButton.func = std::bind(&SaveGameScene::OnCancelOverwrite, this);

    LoadSaves();
    backButton.UpdateSize({GetScreenWidth(), GetScreenHeight()});
    saveName.UpdateSize({GetScreenWidth(), GetScreenHeight()});
    newSaveButton.UpdateSize({GetScreenWidth(), GetScreenHeight()});
    saveButtons.UpdateSize({GetScreenWidth(), GetScreenHeight()});
    confirmOverwriteButton.UpdateSize({GetScreenWidth(), GetScreenHeight()});
    cancelOverwriteButton.UpdateSize({GetScreenWidth(), GetScreenHeight()});
}

void SaveGameScene::Update(double dt)
{
    ProcessGuiInput(dt);
    if (overwriteConfirmationVisible)
        render.Draw({&backButton, &saveName, &newSaveButton, &saveButtons, &confirmOverwriteButton, &cancelOverwriteButton}, dt);
    else
        render.Draw({&backButton, &saveName, &newSaveButton, &saveButtons}, dt);
}

void SaveGameScene::OnBackPressed()
{
    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = previousSceneName;
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

void SaveGameScene::OnSavePressed(std::string save)
{
    pendingOverwriteSave = save;
    overwriteConfirmationVisible = true;
    confirmOverwriteButton.ChangeText("Overwrite " + save);
}

void SaveGameScene::OnNewSavePressed()
{
    std::string requestedName = SanitizeSaveName(saveName.GetText());
    if (SaveExists(requestedName))
    {
        OnSavePressed(requestedName);
        return;
    }

    auto msg = std::make_shared<SaveGameEvent>();
    msg->sender = this;
    msg->name = requestedName;
    broker->Broadcast(msg);
    overwriteConfirmationVisible = false;
}

void SaveGameScene::OnConfirmOverwrite()
{
    if (pendingOverwriteSave.empty())
        return;

    auto msg = std::make_shared<SaveGameEvent>();
    msg->sender = this;
    msg->name = pendingOverwriteSave;
    broker->Broadcast(msg);
    pendingOverwriteSave.clear();
    overwriteConfirmationVisible = false;
}

void SaveGameScene::OnCancelOverwrite()
{
    pendingOverwriteSave.clear();
    overwriteConfirmationVisible = false;
}

void SaveGameScene::LoadSaves()
{
    PopulateSaveButtons(saveButtons, [this](std::string saveName)
    {
        OnSavePressed(saveName);
    });
}

void SaveGameScene::HandleEvent(std::shared_ptr<Event> e)
{
    auto ptr = std::dynamic_pointer_cast<WindowSizeChangedEvent>(e);
    if (ptr != nullptr)
    {
        backButton.UpdateSize(ptr->windowSize);
        saveName.UpdateSize(ptr->windowSize);
        newSaveButton.UpdateSize(ptr->windowSize);
        saveButtons.UpdateSize(ptr->windowSize);
        confirmOverwriteButton.UpdateSize(ptr->windowSize);
        cancelOverwriteButton.UpdateSize(ptr->windowSize);
    }

    auto sceneChange = std::dynamic_pointer_cast<ChangeSceneEvent>(e);
    if (sceneChange != nullptr && sceneChange->sceneName == name)
        LoadSaves();

    auto saveListChanged = std::dynamic_pointer_cast<SaveListChangedEvent>(e);
    if (saveListChanged != nullptr)
        LoadSaves();
}
