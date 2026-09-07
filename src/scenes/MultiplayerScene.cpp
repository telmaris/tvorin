#include "scenes/Scenes.h"
#include "scenes/MultiplayerLobbyProtocol.h"
#include "scenes/SceneUtils.h"
#include "core/Log.h"
#include "multiplayer/TcpGameTransport.h"
#include "ui/ControlIcons.h"

#include <fstream>

namespace
{
    struct MultiplayerConfig
    {
        std::string nickname{"Player"};
        std::string sessionName{"lan_test"};
        std::string hostIp{"127.0.0.1"};
        unsigned short port{27015};
        MapSizePreset sizePreset{MapSizePreset::S};
        float resourceDensity{0.65f};
        float resourceFieldSize{0.45f};
        float resourceRichness{0.5f};
        bool debugMode{false};
        int globalLayoutPreset{1};
        int globalTypeWeightsPreset{0};
        int globalValueScalesPreset{0};
    };

    unsigned short ParsePort(const std::string& text)
    {
        try
        {
            int parsed = std::stoi(text);
            return static_cast<unsigned short>(std::clamp(parsed, 1, 65535));
        }
        catch (...)
        {
            return 27015;
        }
    }

    int ParseIntOrDefault(const std::string& text, int fallback)
    {
        try
        {
            return std::stoi(text);
        }
        catch (...)
        {
            return fallback;
        }
    }

    float ParseFloatOrDefault(const std::string& text, float fallback)
    {
        try
        {
            return std::stof(text);
        }
        catch (...)
        {
            return fallback;
        }
    }

    bool ParseBoolOrDefault(const std::string& text, bool fallback)
    {
        if (text == "1" || text == "true")
            return true;
        if (text == "0" || text == "false")
            return false;
        return fallback;
    }

    MultiplayerConfig LoadMultiplayerConfig()
    {
        MultiplayerConfig config;
        std::ifstream file("config/multiplayer.cfg");
        if (!file.is_open())
            return config;

        std::string line;
        while (std::getline(file, line))
        {
            auto split = line.find('=');
            if (split == std::string::npos)
                continue;

            std::string key = line.substr(0, split);
            std::string value = line.substr(split + 1);
            if (key == "nickname" && !value.empty())
                config.nickname = value;
            else if (key == "session" && !value.empty())
                config.sessionName = value;
            else if (key == "host_ip" && !value.empty())
                config.hostIp = value;
            else if (key == "port")
                config.port = ParsePort(value);
            else if (key == "size")
                config.sizePreset = static_cast<MapSizePreset>(std::clamp(ParseIntOrDefault(value, 0), 0, 3));
            else if (key == "resource_density")
                config.resourceDensity = std::clamp(ParseFloatOrDefault(value, config.resourceDensity), 0.0f, 1.0f);
            else if (key == "resource_field_size")
                config.resourceFieldSize = std::clamp(ParseFloatOrDefault(value, config.resourceFieldSize), 0.0f, 1.0f);
            else if (key == "resource_richness")
                config.resourceRichness = std::clamp(ParseFloatOrDefault(value, config.resourceRichness), 0.0f, 1.0f);
            else if (key == "debug_mode")
                config.debugMode = ParseBoolOrDefault(value, false);
            else if (key == "global_layout")
                config.globalLayoutPreset = std::clamp(ParseIntOrDefault(value, 1), 0, 2);
            else if (key == "global_type_weights")
                config.globalTypeWeightsPreset = std::clamp(ParseIntOrDefault(value, 0), 0, 2);
            else if (key == "global_value_scales")
                config.globalValueScalesPreset = std::clamp(ParseIntOrDefault(value, 0), 0, 2);
        }
        return config;
    }

    void SaveMultiplayerConfig(const MultiplayerConfig& config)
    {
        std::error_code error;
        std::filesystem::create_directories("config", error);
        std::ofstream file("config/multiplayer.cfg", std::ios::trunc);
        if (!file.is_open())
            return;

        file << "nickname=" << config.nickname << '\n';
        file << "session=" << config.sessionName << '\n';
        file << "host_ip=" << config.hostIp << '\n';
        file << "port=" << config.port << '\n';
        file << "size=" << static_cast<int>(config.sizePreset) << '\n';
        file << "resource_density=" << config.resourceDensity << '\n';
        file << "resource_field_size=" << config.resourceFieldSize << '\n';
        file << "resource_richness=" << config.resourceRichness << '\n';
        file << "debug_mode=" << (config.debugMode ? 1 : 0) << '\n';
        file << "global_layout=" << config.globalLayoutPreset << '\n';
        file << "global_type_weights=" << config.globalTypeWeightsPreset << '\n';
        file << "global_value_scales=" << config.globalValueScalesPreset << '\n';
    }

    CampaignGenerationParameters MakeDefaultMultiplayerParams(MapSizePreset sizePreset = MapSizePreset::S,
        float resourceDensity = 0.65f, float resourceFieldSize = 0.45f,
        float resourceRichnessSlider = 0.5f, bool debugMode = false)
    {
        MapParameters local;
        local.sizePreset = sizePreset;
        local.sizeX = MapGenerator::SizeFromPreset(local.sizePreset);
        local.sizeY = local.sizeX;
        local.seed = 27015;
        local.resourceDensity = resourceDensity;
        local.resourceFieldSize = resourceFieldSize;
        local.resourceRichness = SliderToInt(resourceRichnessSlider, 40, 160);
        local.aiOpponentCount = 0;
        local.aiDifficulty = 0;
        local.debugMode = debugMode;
        if (local.debugMode)
            ApplyDebugLocalMapPreset(local);
        CampaignGenerationParameters campaign{std::move(local)};
        campaign.globalMap.seed = campaign.localMap.seed;
        return campaign;
    }
    Color LocalPlayerChatColor()
    {
        return Color{66, 154, 255, 255};
    }

    Color RemotePlayerChatColor()
    {
        return Color{220, 72, 72, 255};
    }

}

MultiplayerScene::MultiplayerScene()
{
    MultiplayerConfig config = LoadMultiplayerConfig();

    backButton.ChangeText("Back");
    backButton.ChangeSizeAnchor(Vec2f{0.18f, 0.07f});
    backButton.ChangePositionAnchor(Vec2f{0.04f, 0.90f});
    backButton.func = std::bind(&MultiplayerScene::OnBackPressed, this);

    nicknameLabel.ChangeText("Nickname");
    nicknameLabel.ChangeSizeAnchor(Vec2f{0.18f, 0.045f});
    nicknameLabel.ChangePositionAnchor(Vec2f{0.16f, 0.172f});

    nickname.SetValue(config.nickname);
    nickname.ChangeSizeAnchor(Vec2f{0.32f, 0.07f});
    nickname.ChangePositionAnchor(Vec2f{0.34f, 0.16f});

    sessionNameLabel.ChangeText("Session");
    sessionNameLabel.ChangeSizeAnchor(Vec2f{0.18f, 0.045f});
    sessionNameLabel.ChangePositionAnchor(Vec2f{0.16f, 0.252f});

    sessionName.SetValue(config.sessionName);
    sessionName.ChangeSizeAnchor(Vec2f{0.32f, 0.07f});
    sessionName.ChangePositionAnchor(Vec2f{0.34f, 0.24f});

    addressLabel.ChangeText("Host IP");
    addressLabel.ChangeSizeAnchor(Vec2f{0.18f, 0.045f});
    addressLabel.ChangePositionAnchor(Vec2f{0.16f, 0.362f});

    address.SetValue(config.hostIp);
    address.ChangeSizeAnchor(Vec2f{0.32f, 0.07f});
    address.ChangePositionAnchor(Vec2f{0.34f, 0.35f});

    portLabel.ChangeText("Port");
    portLabel.ChangeSizeAnchor(Vec2f{0.18f, 0.045f});
    portLabel.ChangePositionAnchor(Vec2f{0.16f, 0.472f});

    port.SetValue(std::to_string(config.port));
    port.ChangeSizeAnchor(Vec2f{0.18f, 0.07f});
    port.ChangePositionAnchor(Vec2f{0.41f, 0.46f});

    lobbySizePreset = config.sizePreset;
    lobbyGlobalLayoutPreset = config.globalLayoutPreset;
    lobbyGlobalTypeWeightsPreset = config.globalTypeWeightsPreset;
    lobbyGlobalValueScalesPreset = config.globalValueScalesPreset;

    hostButton.ChangeText("Host");
    hostButton.ChangeSizeAnchor(Vec2f{0.24f, 0.08f});
    hostButton.ChangePositionAnchor(Vec2f{0.25f, 0.68f});
    hostButton.func = std::bind(&MultiplayerScene::OnHostPressed, this);

    joinButton.ChangeText("Join");
    joinButton.ChangeSizeAnchor(Vec2f{0.24f, 0.08f});
    joinButton.ChangePositionAnchor(Vec2f{0.51f, 0.68f});
    joinButton.func = std::bind(&MultiplayerScene::OnJoinPressed, this);

    startButton.ChangeText("Start");
    startButton.ChangeSizeAnchor(Vec2f{0.18f, 0.07f});
    startButton.ChangePositionAnchor(Vec2f{0.68f, 0.88f});
    startButton.func = std::bind(&MultiplayerScene::OnStartPressed, this);

    gameSettingsButton.ChangeText("Game Settings");
    gameSettingsButton.ChangeSizeAnchor(Vec2f{0.22f, 0.07f});
    gameSettingsButton.ChangePositionAnchor(Vec2f{0.44f, 0.88f});
    gameSettingsButton.func = std::bind(&MultiplayerScene::OnGameSettingsPressed, this);

    closeSettingsButton.ChangeText("Close");
    closeSettingsButton.ChangeSizeAnchor(Vec2f{0.20f, 0.065f});
    closeSettingsButton.ChangePositionAnchor(Vec2f{0.30f, 0.84f});
    closeSettingsButton.func = std::bind(&MultiplayerScene::OnCloseGameSettingsPressed, this);

    multiplayerSizeButton.ChangeSizeAnchor(Vec2f{0.40f, 0.055f});
    multiplayerSizeButton.ChangePositionAnchor(Vec2f{0.30f, 0.22f});
    multiplayerSizeButton.func = std::bind(&MultiplayerScene::OnMultiplayerSizePressed, this);

    multiplayerGlobalLayoutButton.ChangeSizeAnchor(Vec2f{0.40f, 0.045f});
    multiplayerGlobalLayoutButton.ChangePositionAnchor(Vec2f{0.08f, 0.65f});
    multiplayerGlobalLayoutButton.func = std::bind(&MultiplayerScene::OnMultiplayerGlobalLayoutPressed, this);
    multiplayerGlobalTypeWeightsButton.ChangeSizeAnchor(Vec2f{0.40f, 0.045f});
    multiplayerGlobalTypeWeightsButton.ChangePositionAnchor(Vec2f{0.52f, 0.65f});
    multiplayerGlobalTypeWeightsButton.func = std::bind(&MultiplayerScene::OnMultiplayerGlobalTypeWeightsPressed, this);
    multiplayerGlobalValueScalesButton.ChangeSizeAnchor(Vec2f{0.84f, 0.045f});
    multiplayerGlobalValueScalesButton.ChangePositionAnchor(Vec2f{0.08f, 0.71f});
    multiplayerGlobalValueScalesButton.func = std::bind(&MultiplayerScene::OnMultiplayerGlobalValueScalesPressed, this);

    multiplayerResourceDensity.ChangePositionAnchor(Vec2f{0.30f, 0.32f});
    multiplayerResourceDensity.ChangeSizeAnchor(Vec2f{0.40f, 0.045f});
    multiplayerResourceDensity.currentValue = config.resourceDensity;
    multiplayerResourceFieldSize.ChangePositionAnchor(Vec2f{0.30f, 0.41f});
    multiplayerResourceFieldSize.ChangeSizeAnchor(Vec2f{0.40f, 0.045f});
    multiplayerResourceFieldSize.currentValue = config.resourceFieldSize;
    multiplayerResourceRichness.ChangePositionAnchor(Vec2f{0.30f, 0.50f});
    multiplayerResourceRichness.ChangeSizeAnchor(Vec2f{0.40f, 0.045f});
    multiplayerResourceRichness.currentValue = config.resourceRichness;
    multiplayerDebugMode.ChangeText("Debug mode");
    multiplayerDebugMode.ChangePositionAnchor(Vec2f{0.30f, 0.59f});
    multiplayerDebugMode.ChangeSizeAnchor(Vec2f{0.20f, 0.045f});
    multiplayerDebugMode.currentState = config.debugMode;

    chatInput.SetValue("");
    chatInput.ChangeSizeAnchor(Vec2f{0.48f, 0.055f});
    chatInput.ChangePositionAnchor(Vec2f{0.34f, 0.80f});

    sendChatButton.ChangeText("Send");
    sendChatButton.ChangeSizeAnchor(Vec2f{0.08f, 0.055f});
    sendChatButton.ChangePositionAnchor(Vec2f{0.83f, 0.80f});
    sendChatButton.func = std::bind(&MultiplayerScene::OnSendChatPressed, this);

    Vec2i size{GetScreenWidth(), GetScreenHeight()};
    backButton.UpdateSize(size);
    nicknameLabel.UpdateSize(size);
    nickname.UpdateSize(size);
    sessionNameLabel.UpdateSize(size);
    sessionName.UpdateSize(size);
    addressLabel.UpdateSize(size);
    address.UpdateSize(size);
    portLabel.UpdateSize(size);
    port.UpdateSize(size);
    hostButton.UpdateSize(size);
    joinButton.UpdateSize(size);
    startButton.UpdateSize(size);
    gameSettingsButton.UpdateSize(size);
    closeSettingsButton.UpdateSize(size);
    multiplayerSizeButton.UpdateSize(size);
    multiplayerGlobalLayoutButton.UpdateSize(size);
    multiplayerGlobalTypeWeightsButton.UpdateSize(size);
    multiplayerGlobalValueScalesButton.UpdateSize(size);
    multiplayerResourceDensity.UpdateSize(size);
    multiplayerResourceFieldSize.UpdateSize(size);
    multiplayerResourceRichness.UpdateSize(size);
    multiplayerDebugMode.UpdateSize(size);
    chatInput.UpdateSize(size);
    sendChatButton.UpdateSize(size);
    RefreshMultiplayerLabels();
}

void MultiplayerScene::Update(double dt)
{
    ProcessGuiInput(dt);
    UpdateLobbyMessages(dt);
    if (connectionMessageTimer > 0.0)
        connectionMessageTimer = std::max(0.0, connectionMessageTimer - dt);
    RefreshMultiplayerLabels();

    std::vector<UiWidget*> widgets;
    if (lobbyActive)
    {
        if (showGameSettings && isLobbyHost)
        {
            widgets = {
                &multiplayerSizeButton,
                &multiplayerResourceDensity,
                &multiplayerResourceFieldSize,
                &multiplayerResourceRichness,
                &multiplayerDebugMode,
                &multiplayerGlobalLayoutButton,
                &multiplayerGlobalTypeWeightsButton,
                &multiplayerGlobalValueScalesButton,
                &closeSettingsButton,
                &backButton};
        }
        else
        {
            if (isLobbyHost)
            {
                widgets.push_back(&gameSettingsButton);
                widgets.push_back(&startButton);
            }
            widgets.push_back(&chatInput);
            widgets.push_back(&sendChatButton);
            widgets.push_back(&backButton);
        }
    }
    else
    {
        widgets = {
            &nicknameLabel,
            &nickname,
            &sessionNameLabel,
            &sessionName,
            &addressLabel,
            &address,
            &portLabel,
            &port,
            &hostButton,
            &joinButton,
            &backButton};
    }

    BeginDrawing();
    ClearBackground(BLACK);
    if (lobbyActive && showGameSettings && isLobbyHost)
        DrawGameSettingsPanel();
    else if (lobbyActive)
    {
        Rectangle chatBounds{
            GetScreenWidth() * 0.34f,
            GetScreenHeight() * 0.20f,
            GetScreenWidth() * 0.58f,
            GetScreenHeight() * 0.58f};
        if (CheckCollisionPointRec(GetMousePosition(), chatBounds))
        {
            int maxScroll = std::max(0, static_cast<int>(lobbyLines.size()) - 10);
            lobbyChatScroll = std::clamp(lobbyChatScroll + static_cast<int>(InputManager::GetMouseWheelMove()), 0, maxScroll);
        }
        DrawLobbyLog();
    }
    for (auto* widget : widgets)
        if (widget != nullptr)
            widget->Update(dt);
    if (connectingToLobby || connectionMessageTimer > 0.0)
        DrawConnectionDialog();
    render.PresentFrame();

    if (lobbyActive && !showGameSettings && InputManager::IsKeyPressed(KEY_ENTER) && !chatInput.GetText().empty())
        OnSendChatPressed();
    if (lobbyActive && showGameSettings && isLobbyHost && InputManager::IsMouseButtonReleased(MOUSE_LEFT_BUTTON))
        MaybeBroadcastSettingsChange("Game settings updated.");
}

void MultiplayerScene::HandleEvent(std::shared_ptr<Event> e)
{
    auto ptr = std::dynamic_pointer_cast<WindowSizeChangedEvent>(e);
    if (ptr != nullptr)
    {
        backButton.UpdateSize(ptr->windowSize);
        nicknameLabel.UpdateSize(ptr->windowSize);
        nickname.UpdateSize(ptr->windowSize);
        sessionNameLabel.UpdateSize(ptr->windowSize);
        sessionName.UpdateSize(ptr->windowSize);
        addressLabel.UpdateSize(ptr->windowSize);
        address.UpdateSize(ptr->windowSize);
        portLabel.UpdateSize(ptr->windowSize);
        port.UpdateSize(ptr->windowSize);
        hostButton.UpdateSize(ptr->windowSize);
        joinButton.UpdateSize(ptr->windowSize);
        startButton.UpdateSize(ptr->windowSize);
        gameSettingsButton.UpdateSize(ptr->windowSize);
        closeSettingsButton.UpdateSize(ptr->windowSize);
        multiplayerSizeButton.UpdateSize(ptr->windowSize);
        multiplayerGlobalLayoutButton.UpdateSize(ptr->windowSize);
        multiplayerGlobalTypeWeightsButton.UpdateSize(ptr->windowSize);
        multiplayerGlobalValueScalesButton.UpdateSize(ptr->windowSize);
        multiplayerResourceDensity.UpdateSize(ptr->windowSize);
        multiplayerResourceFieldSize.UpdateSize(ptr->windowSize);
        multiplayerResourceRichness.UpdateSize(ptr->windowSize);
        multiplayerDebugMode.UpdateSize(ptr->windowSize);
        chatInput.UpdateSize(ptr->windowSize);
        sendChatButton.UpdateSize(ptr->windowSize);
    }
}

void MultiplayerScene::OnBackPressed()
{
    if (connectingToLobby)
    {
        ResetLobby();
        return;
    }

    if (showGameSettings)
    {
        showGameSettings = false;
        SaveMultiplayerSettings();
        return;
    }

    if (lobbyActive)
    {
        ResetLobby();
        return;
    }

    auto msg = std::make_shared<ChangeSceneEvent>();
    msg->sender = this;
    msg->sceneName = "MainScene";
    msg->previousSceneName = name;
    broker->Broadcast(msg);
}

void MultiplayerScene::OnHostPressed()
{
    ResetLobby();
    SaveMultiplayerSettings();
    lobbyNickname = SanitizeSaveName(nickname.GetText());
    lobbySessionName = SanitizeSaveName(sessionName.GetText());
    lobbyPort = ParsePort(port.GetText());
    lobbyTransport = TcpGameTransport::CreateHost(lobbyPort);
    isLobbyHost = true;
    lobbyActive = true;
    AddLobbyLine(lobbyNickname + " is hosting '" + lobbySessionName + "' on port " + std::to_string(lobbyPort));
    AddLobbyLine("Waiting for players...");
    lastBroadcastLobbyState.clear();
    Log::Msg("[Lobby]", "Host lobby opened: ", lobbySessionName, " port=", lobbyPort);
}

void MultiplayerScene::OnJoinPressed()
{
    ResetLobby();
    SaveMultiplayerSettings();
    lobbyNickname = SanitizeSaveName(nickname.GetText());
    lobbySessionName = SanitizeSaveName(sessionName.GetText());
    lobbyAddress = address.GetText();
    lobbyPort = ParsePort(port.GetText());
    lobbyTransport = TcpGameTransport::CreateClient(lobbyAddress, lobbyPort);
    isLobbyHost = false;
    lobbyActive = false;
    connectingToLobby = true;
    connectionWaitTimer = 0.0;
    connectionMessageTimer = 0.0;
    connectionMessage = "Connecting to " + lobbyAddress + ":" + std::to_string(lobbyPort);
    lastBroadcastLobbyState.clear();
    Log::Msg("[Lobby]", "Client lobby join requested: ", lobbyAddress, ":", lobbyPort, " session=", lobbySessionName);
}

void MultiplayerScene::OnStartPressed()
{
    if (!lobbyActive || !isLobbyHost || lobbyTransport == nullptr)
    {
        AddLobbyLine("Only the host can start an active lobby.");
        return;
    }

    SaveMultiplayerSettings();
    MaybeBroadcastSettingsChange();
    CampaignGenerationParameters params = BuildLobbyMapParameters();
    lobbyTransport->SendLobbyMessage(MultiplayerLobbyProtocol::SerializeStart(lobbySessionName, params));
    AddLobbyLine("Starting game...");
    Log::Msg("[Lobby]", "Host starting multiplayer game: ", lobbySessionName);

    auto msg = std::make_shared<HostMultiplayerGameEvent>();
    msg->sender = this;
    msg->name = lobbySessionName;
    msg->params = params;
    msg->port = lobbyPort;
    msg->transport = lobbyTransport;
    lobbyTransport = nullptr;
    broker->Broadcast(msg);
}

void MultiplayerScene::OnGameSettingsPressed()
{
    if (isLobbyHost)
        showGameSettings = true;
}

void MultiplayerScene::OnCloseGameSettingsPressed()
{
    showGameSettings = false;
    SaveMultiplayerSettings();
    MaybeBroadcastSettingsChange("Game settings updated.");
}

void MultiplayerScene::OnMultiplayerSizePressed()
{
    int next = (static_cast<int>(lobbySizePreset) + 1) % 4;
    lobbySizePreset = static_cast<MapSizePreset>(next);
    RefreshMultiplayerLabels();
    MaybeBroadcastSettingsChange("Map size set to " + MapSizeName(lobbySizePreset) + ".");
}

void MultiplayerScene::OnMultiplayerGlobalLayoutPressed()
{
    if (!isLobbyHost)
        return;
    lobbyGlobalLayoutPreset = (lobbyGlobalLayoutPreset + 1) % 3;
    RefreshMultiplayerLabels();
    MaybeBroadcastSettingsChange("Global campaign layout updated.");
}

void MultiplayerScene::OnMultiplayerGlobalTypeWeightsPressed()
{
    if (!isLobbyHost)
        return;
    lobbyGlobalTypeWeightsPreset = (lobbyGlobalTypeWeightsPreset + 1) % 3;
    RefreshMultiplayerLabels();
    MaybeBroadcastSettingsChange("Global province type weights updated.");
}

void MultiplayerScene::OnMultiplayerGlobalValueScalesPressed()
{
    if (!isLobbyHost)
        return;
    lobbyGlobalValueScalesPreset = (lobbyGlobalValueScalesPreset + 1) % 3;
    RefreshMultiplayerLabels();
    MaybeBroadcastSettingsChange("Global province value scales updated.");
}

void MultiplayerScene::OnSendChatPressed()
{
    if (!lobbyActive || lobbyTransport == nullptr)
    {
        AddLobbyLine("Open or join a lobby first.");
        return;
    }

    std::string text = chatInput.GetText();
    if (text.empty())
        return;

    AddLobbyLine(lobbyNickname + ": " + text, LocalPlayerChatColor());
    lobbyTransport->SendLobbyMessage("CHAT " + lobbyNickname + ": " + text);
    chatInput.SetValue("");
}

// Adds one visible lobby/chat line.
void MultiplayerScene::AddLobbyLine(const std::string& line, Color color)
{
    lobbyLines.push_back({line, color});
    if (lobbyLines.size() > 100)
        lobbyLines.erase(lobbyLines.begin());
    lobbyChatScroll = std::clamp(lobbyChatScroll, 0, std::max(0, static_cast<int>(lobbyLines.size()) - 10));
}

// Clears current lobby connection state.
void MultiplayerScene::ResetLobby()
{
    lobbyTransport.reset();
    lobbyLines.clear();
    remoteLobbyNickname.clear();
    lobbyActive = false;
    connectingToLobby = false;
    isLobbyHost = false;
    announcedConnection = false;
    showGameSettings = false;
    hasRemoteLobbyPlayer = false;
    lobbyChatScroll = 0;
    connectionWaitTimer = 0.0;
    connectionMessageTimer = 0.0;
    connectionMessage.clear();
    lastBroadcastLobbyState.clear();
}

// Polls socket state and lobby messages.
void MultiplayerScene::UpdateLobbyMessages(double dt)
{
    if ((!lobbyActive && !connectingToLobby) || lobbyTransport == nullptr)
        return;

    std::string status = lobbyTransport->GetStatus();
    if (connectingToLobby)
    {
        connectionWaitTimer += dt;
        connectionMessage = "Connecting to " + lobbyAddress + ":" + std::to_string(lobbyPort) + "...";
        if (lobbyTransport->IsConnected())
        {
            connectingToLobby = false;
            lobbyActive = true;
            announcedConnection = true;
            connectionMessage.clear();
            AddLobbyLine("Connected to host.");
            Log::Msg("[Lobby]", status);
            lobbyTransport->SendLobbyMessage("JOIN " + lobbyNickname);
        }
        else if (lobbyTransport->HasFailed() || connectionWaitTimer >= 10.0)
        {
            connectionMessage = "Connection failed: " + status;
            connectionMessageTimer = 3.0;
            Log::Msg("[Lobby]", "Connection failed or timed out: ", status);
            lobbyTransport.reset();
            connectingToLobby = false;
            lobbyActive = false;
            announcedConnection = false;
            return;
        }
        else
        {
            return;
        }
    }
    if (!announcedConnection && lobbyTransport->IsConnected())
    {
        announcedConnection = true;
        AddLobbyLine(isLobbyHost ? "Client connected." : "Connected to host.");
        Log::Msg("[Lobby]", status);
        if (!isLobbyHost)
            lobbyTransport->SendLobbyMessage("JOIN " + lobbyNickname);
        else
            BroadcastLobbyState();
    }
    if (lobbyTransport->HasFailed())
        AddLobbyLine("Connection failed: " + status);

    for (const auto& payload : lobbyTransport->ReceiveLobbyMessages())
    {
        if (payload.rfind("JOIN ", 0) == 0)
        {
            std::string playerName = payload.substr(5);
            if (isLobbyHost)
            {
                for (const auto& [line, color] : lobbyLines)
                    lobbyTransport->SendLobbyMessage("HISTORY " + line);
            }
            remoteLobbyNickname = playerName;
            hasRemoteLobbyPlayer = true;
            AddLobbyLine(playerName + " joined.");
            Log::Msg("[Lobby]", "Player joined: ", playerName);
            if (isLobbyHost)
            {
                lobbyTransport->SendLobbyMessage("INFO " + playerName + " joined.");
                BroadcastLobbyState();
            }
        }
        else if (payload.rfind("INFO ", 0) == 0)
        {
            AddLobbyLine(payload.substr(5));
        }
        else if (payload.rfind("HISTORY ", 0) == 0)
        {
            AddLobbyLine(payload.substr(8));
        }
        else if (payload.rfind("STATE ", 0) == 0)
        {
            if (!ApplyLobbyState(payload.substr(6)))
                AddLobbyLine("Failed to parse lobby state from host.", Color{240, 120, 120, 255});
        }
        else if (payload.rfind("START ", 0) == 0)
        {
            CampaignGenerationParameters params;
            if (!MultiplayerLobbyProtocol::TryDeserializeStart(payload.substr(6), lobbySessionName, params))
            {
                AddLobbyLine("Failed to parse game settings from host.", Color{240, 120, 120, 255});
                continue;
            }
            AddLobbyLine("Host started the game.");
            Log::Msg("[Lobby]", "Client received start for session ", lobbySessionName);

            auto msg = std::make_shared<JoinMultiplayerGameEvent>();
            msg->sender = this;
            msg->name = lobbySessionName;
            msg->params = params;
            msg->address = lobbyAddress;
            msg->port = lobbyPort;
            msg->transport = lobbyTransport;
            lobbyTransport = nullptr;
            broker->Broadcast(msg);
        }
        else if (payload.rfind("CHAT ", 0) == 0)
        {
            AddLobbyLine(payload.substr(5), RemotePlayerChatColor());
        }
        else
        {
            AddLobbyLine(payload);
        }
    }
}

// Refreshes dynamic labels in the multiplayer setup view.
void MultiplayerScene::RefreshMultiplayerLabels()
{
    multiplayerSizeButton.ChangeText("Map size " + MapSizeName(lobbySizePreset));
    multiplayerResourceDensity.ChangeText("Resource density " + std::to_string(SliderToInt(multiplayerResourceDensity.GetValue(), 50, 225)) + "%");
    multiplayerResourceFieldSize.ChangeText("Resource field size " + std::to_string(SliderToInt(multiplayerResourceFieldSize.GetValue(), 50, 225)) + "%");
    multiplayerResourceRichness.ChangeText("Resource richness " + std::to_string(SliderToInt(multiplayerResourceRichness.GetValue(), 40, 160)));
    static const std::array<const char*, 3> layouts{
        "Global layout: 16 provinces / 105 spacing",
        "Global layout: 32 provinces / 120 spacing",
        "Global layout: 500 provinces / 145 spacing"};
    static const std::array<const char*, 3> weights{
        "Types: frontier 55 / city 20 / bandit 15 / event 10",
        "Types: frontier 40 / city 30 / bandit 20 / event 10",
        "Types: frontier 65 / city 15 / bandit 10 / event 10"};
    static const std::array<const char*, 3> values{
        "Values: standard resources / city / bandit",
        "Values: rich resources / wealthy city / strong bandit",
        "Values: sparse resources / modest city / weak bandit"};
    multiplayerGlobalLayoutButton.ChangeText(
        layouts[static_cast<std::size_t>(lobbyGlobalLayoutPreset)]);
    multiplayerGlobalTypeWeightsButton.ChangeText(
        weights[static_cast<std::size_t>(lobbyGlobalTypeWeightsPreset)]);
    multiplayerGlobalValueScalesButton.ChangeText(
        values[static_cast<std::size_t>(lobbyGlobalValueScalesPreset)]);
}

// Stores the multiplayer setup for future sessions.
void MultiplayerScene::SaveMultiplayerSettings() const
{
    MultiplayerConfig config;
    config.nickname = SanitizeSaveName(nickname.GetText());
    config.sessionName = SanitizeSaveName(sessionName.GetText());
    config.hostIp = address.GetText();
    config.port = ParsePort(port.GetText());
    config.sizePreset = lobbySizePreset;
    config.resourceDensity = multiplayerResourceDensity.GetValue();
    config.resourceFieldSize = multiplayerResourceFieldSize.GetValue();
    config.resourceRichness = multiplayerResourceRichness.GetValue();
    config.debugMode = multiplayerDebugMode.currentState;
    config.globalLayoutPreset = lobbyGlobalLayoutPreset;
    config.globalTypeWeightsPreset = lobbyGlobalTypeWeightsPreset;
    config.globalValueScalesPreset = lobbyGlobalValueScalesPreset;
    SaveMultiplayerConfig(config);
}

// Builds the authoritative hosted game generation parameters.
CampaignGenerationParameters MultiplayerScene::BuildLobbyMapParameters() const
{
    CampaignGenerationParameters campaign = MakeDefaultMultiplayerParams(
        lobbySizePreset,
        multiplayerResourceDensity.GetValue(),
        multiplayerResourceFieldSize.GetValue(),
        multiplayerResourceRichness.GetValue(),
        multiplayerDebugMode.currentState);
    switch (lobbyGlobalLayoutPreset)
    {
        case 0:
            campaign.globalMap.provinceCount = 16;
            campaign.globalMap.extraEdgeCount = 7;
            campaign.globalMap.layoutRadius = 800;
            campaign.globalMap.minimumLayoutSpacing = 105;
            break;
        case 1:
            campaign.globalMap.provinceCount = 32;
            campaign.globalMap.extraEdgeCount = 12;
            campaign.globalMap.layoutRadius = 1000;
            campaign.globalMap.minimumLayoutSpacing = 120;
            break;
        case 2:
            campaign.globalMap.provinceCount = 500;
            campaign.globalMap.extraEdgeCount = 188;
            campaign.globalMap.layoutRadius = 4472;
            campaign.globalMap.minimumLayoutSpacing = 145;
            break;
    }
    switch (lobbyGlobalTypeWeightsPreset)
    {
        case 0:
            campaign.globalMap.buildableWeight = 55;
            campaign.globalMap.neutralCityWeight = 20;
            campaign.globalMap.banditCampWeight = 15;
            campaign.globalMap.eventSiteWeight = 10;
            break;
        case 1:
            campaign.globalMap.buildableWeight = 40;
            campaign.globalMap.neutralCityWeight = 30;
            campaign.globalMap.banditCampWeight = 20;
            campaign.globalMap.eventSiteWeight = 10;
            break;
        case 2:
            campaign.globalMap.buildableWeight = 65;
            campaign.globalMap.neutralCityWeight = 15;
            campaign.globalMap.banditCampWeight = 10;
            campaign.globalMap.eventSiteWeight = 10;
            break;
    }
    switch (lobbyGlobalValueScalesPreset)
    {
        case 0:
            campaign.globalMap.buildableWealthScale = 1.0;
            campaign.globalMap.cityWealthScale = 1.0;
            campaign.globalMap.banditStrengthScale = 1.0;
            break;
        case 1:
            campaign.globalMap.buildableWealthScale = 1.35;
            campaign.globalMap.cityWealthScale = 1.35;
            campaign.globalMap.banditStrengthScale = 1.25;
            break;
        case 2:
            campaign.globalMap.buildableWealthScale = 0.80;
            campaign.globalMap.cityWealthScale = 0.85;
            campaign.globalMap.banditStrengthScale = 0.75;
            break;
    }
    return campaign;
}

void MultiplayerScene::BroadcastLobbyState(const std::string& infoMessage)
{
    if (!lobbyActive || !isLobbyHost || lobbyTransport == nullptr)
        return;

    CampaignGenerationParameters params = BuildLobbyMapParameters();
    std::string state = MultiplayerLobbyProtocol::SerializeState(lobbySessionName, lobbyNickname, remoteLobbyNickname, params);
    lobbyTransport->SendLobbyMessage(state);
    lastBroadcastLobbyState = state;
    if (!infoMessage.empty())
    {
        AddLobbyLine(infoMessage);
        lobbyTransport->SendLobbyMessage("INFO " + infoMessage);
    }
}

bool MultiplayerScene::ApplyLobbyState(const std::string& payload)
{
    if (isLobbyHost)
        return true;

    std::string session;
    std::string hostName;
    std::string remoteName;
    CampaignGenerationParameters params;
    if (!MultiplayerLobbyProtocol::TryDeserializeState(payload, session, hostName, remoteName, params))
        return false;

    lobbySessionName = session;
    remoteLobbyNickname = hostName.empty() ? "Host" : hostName;
    hasRemoteLobbyPlayer = true;
    lobbySizePreset = params.localMap.sizePreset;
    const auto& global = params.globalMap;
    if (global.provinceCount <= 16)
        lobbyGlobalLayoutPreset = 0;
    else if (global.provinceCount >= 266)
        lobbyGlobalLayoutPreset = 2;
    else
        lobbyGlobalLayoutPreset = 1;
    if (global.neutralCityWeight >= 25)
        lobbyGlobalTypeWeightsPreset = 1;
    else if (global.buildableWeight >= 60)
        lobbyGlobalTypeWeightsPreset = 2;
    else
        lobbyGlobalTypeWeightsPreset = 0;
    if (global.buildableWealthScale > 1.1)
        lobbyGlobalValueScalesPreset = 1;
    else if (global.buildableWealthScale < 0.95)
        lobbyGlobalValueScalesPreset = 2;
    else
        lobbyGlobalValueScalesPreset = 0;
    multiplayerResourceDensity.currentValue = params.localMap.resourceDensity;
    multiplayerResourceFieldSize.currentValue = params.localMap.resourceFieldSize;
    multiplayerResourceRichness.currentValue = std::clamp((params.localMap.resourceRichness - 40) / 120.0f, 0.0f, 1.0f);
    multiplayerDebugMode.currentState = params.localMap.debugMode;
    RefreshMultiplayerLabels();
    return true;
}

void MultiplayerScene::MaybeBroadcastSettingsChange(const std::string& infoMessage)
{
    if (!lobbyActive || !isLobbyHost || lobbyTransport == nullptr)
        return;

    CampaignGenerationParameters params = BuildLobbyMapParameters();
    std::string state = MultiplayerLobbyProtocol::SerializeState(lobbySessionName, lobbyNickname, remoteLobbyNickname, params);
    if (state == lastBroadcastLobbyState)
        return;

    BroadcastLobbyState(infoMessage);
}

void MultiplayerScene::DrawConnectionDialog() const
{
    int width = static_cast<int>(GetScreenWidth() * 0.46f);
    int height = static_cast<int>(GetScreenHeight() * 0.22f);
    int x = (GetScreenWidth() - width) / 2;
    int y = (GetScreenHeight() - height) / 2;
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{0, 0, 0, 120});
    Rectangle dialog{static_cast<float>(x), static_cast<float>(y),
                     static_cast<float>(width), static_cast<float>(height)};
    if (!UiControlIcons::DrawPixelHudPanelFrame(dialog))
    {
        DrawRectangle(x, y, width, height, Color{20, 24, 31, 245});
        DrawRectangleLines(x, y, width, height, Color{96, 116, 142, 255});
    }

    std::string title = connectingToLobby ? "Connecting" : "Connection failed";
    UiText::DrawFit(title,
        Rectangle{static_cast<float>(x + 22), static_cast<float>(y + 18), static_cast<float>(width - 44), 32.0f},
        28,
        RAYWHITE);

    std::string body = connectionMessage;
    if (connectingToLobby)
    {
        int remaining = std::max(0, 10 - static_cast<int>(std::floor(connectionWaitTimer)));
        body += "  Timeout in " + std::to_string(remaining) + "s";
    }

    UiText::DrawFit(body,
        Rectangle{static_cast<float>(x + 24), static_cast<float>(y + 68), static_cast<float>(width - 48), static_cast<float>(height - 88)},
        21,
        Color{202, 216, 236, 255});
}

// Draws the lobby/chat log under the form.
void MultiplayerScene::DrawLobbyLog() const
{
    int x = static_cast<int>(GetScreenWidth() * 0.04f);
    int y = static_cast<int>(GetScreenHeight() * 0.045f);
    int width = static_cast<int>(GetScreenWidth() * 0.92f);
    int height = static_cast<int>(GetScreenHeight() * 0.80f);
    Rectangle lobbyPanel{static_cast<float>(x), static_cast<float>(y),
                         static_cast<float>(width), static_cast<float>(height)};
    if (!UiControlIcons::DrawPixelHudPanelFrame(lobbyPanel))
    {
        DrawRectangle(x, y, width, height, Color{20, 24, 31, 225});
        DrawRectangleLines(x, y, width, height, Color{86, 100, 120, 255});
    }

    UiText::DrawFit("Multiplayer Lobby", Rectangle{static_cast<float>(x + 18), static_cast<float>(y + 14), static_cast<float>(width - 36), 34.0f}, 30, RAYWHITE);
    std::string status = isLobbyHost
        ? "Host: " + lobbySessionName + "  Port: " + std::to_string(lobbyPort)
        : "Client: " + lobbySessionName + "  Host: " + lobbyAddress + ":" + std::to_string(lobbyPort);
    UiText::DrawFit(status, Rectangle{static_cast<float>(x + 18), static_cast<float>(y + 54), static_cast<float>(width - 36), 28.0f}, 22, Color{190, 205, 224, 255});

    DrawLobbyPlayerPanels();

    Rectangle chatPanel{
        static_cast<float>(x + static_cast<int>(GetScreenWidth() * 0.30f)),
        static_cast<float>(y + 96),
        static_cast<float>(width - static_cast<int>(GetScreenWidth() * 0.33f)),
        static_cast<float>(height - 124)};
    if (!UiControlIcons::DrawPixelHudPanelFrame(chatPanel))
    {
        DrawRectangleRec(chatPanel, Color{15, 18, 24, 210});
        DrawRectangleLinesEx(chatPanel, 1.0f, Color{70, 84, 104, 255});
    }
    UiText::Draw("Chat", chatPanel.x + 12.0f, chatPanel.y + 10.0f, 22, Color{220, 230, 244, 255});

    int visibleLines = std::max(1, static_cast<int>((chatPanel.height - 50.0f) / 24.0f));
    int maxScroll = std::max(0, static_cast<int>(lobbyLines.size()) - visibleLines);
    int scroll = std::clamp(lobbyChatScroll, 0, maxScroll);
    int firstLine = std::max(0, static_cast<int>(lobbyLines.size()) - visibleLines - scroll);
    int lastLine = std::min(static_cast<int>(lobbyLines.size()), firstLine + visibleLines);
    int lineY = static_cast<int>(chatPanel.y + 42.0f);
    BeginScissorMode(static_cast<int>(chatPanel.x), static_cast<int>(chatPanel.y + 36.0f), static_cast<int>(chatPanel.width - 16.0f), static_cast<int>(chatPanel.height - 42.0f));
    for (int i = firstLine; i < lastLine; i++)
    {
        const auto& line = lobbyLines[static_cast<size_t>(i)];
        UiText::Draw(line.first, chatPanel.x + 12.0f, static_cast<float>(lineY), 20, line.second);
        lineY += 24;
    }
    EndScissorMode();

    if (maxScroll > 0)
    {
        Rectangle track{chatPanel.x + chatPanel.width - 10.0f, chatPanel.y + 38.0f, 4.0f, chatPanel.height - 48.0f};
        float thumbHeight = std::max(24.0f, track.height * (visibleLines / static_cast<float>(lobbyLines.size())));
        float thumbRange = std::max(1.0f, track.height - thumbHeight);
        float thumbY = track.y + thumbRange * (scroll / static_cast<float>(maxScroll));
        DrawRectangleRec(track, Color{52, 62, 78, 220});
        DrawRectangleRounded({track.x - 2.0f, thumbY, 8.0f, thumbHeight}, 0.5f, 4, Color{118, 148, 188, 255});
    }
}

// Draws lobby player cards above chat.
void MultiplayerScene::DrawLobbyPlayerPanels() const
{
    int x = static_cast<int>(GetScreenWidth() * 0.06f);
    int y = static_cast<int>(GetScreenHeight() * 0.18f);
    int width = static_cast<int>(GetScreenWidth() * 0.26f);
    int cardHeight = static_cast<int>(GetScreenHeight() * 0.085f);
    int gap = 10;

    Rectangle panel{
        static_cast<float>(x),
        static_cast<float>(y - 40),
        static_cast<float>(width),
        static_cast<float>(GetScreenHeight() * 0.66f)};
    if (!UiControlIcons::DrawPixelHudPanelFrame(panel))
    {
        DrawRectangleRec(panel, Color{15, 18, 24, 210});
        DrawRectangleLinesEx(panel, 1.0f, Color{70, 84, 104, 255});
    }
    UiText::Draw("Players", panel.x + 12.0f, panel.y + 10.0f, 22, Color{220, 230, 244, 255});

    auto drawCard = [&](int index, const std::string& label, const std::string& role, Color color)
    {
        Rectangle card{
            static_cast<float>(x + 12),
            static_cast<float>(y + index * (cardHeight + gap)),
            static_cast<float>(width - 24),
            static_cast<float>(cardHeight)};
        if (!UiControlIcons::DrawPixelHudPanelFrame(card))
        {
            DrawRectangleRec(card, Color{25, 31, 40, 230});
            DrawRectangleLinesEx(card, 1.0f, Color{78, 94, 116, 255});
        }
        Rectangle swatch{card.x + 10.0f, card.y + 9.0f, 22.0f, card.height - 18.0f};
        DrawRectangleRec(swatch, color);
        DrawRectangleLinesEx(swatch, 1.0f, Color{220, 230, 245, 180});
        UiText::DrawFit(label, Rectangle{card.x + 44.0f, card.y + 9.0f, card.width - 58.0f, 24.0f}, 20, RAYWHITE);
        UiText::DrawFit(role, Rectangle{card.x + 44.0f, card.y + 38.0f, card.width - 58.0f, 22.0f}, 18, Color{184, 198, 218, 255});
    };

    drawCard(0, lobbyNickname, isLobbyHost ? "Host" : "You", LocalPlayerChatColor());
    if (hasRemoteLobbyPlayer || !isLobbyHost)
        drawCard(1, hasRemoteLobbyPlayer ? remoteLobbyNickname : "Host", isLobbyHost ? "Client" : "Host", RemotePlayerChatColor());

}

// Draws the hosted game settings panel behind controls.
void MultiplayerScene::DrawGameSettingsPanel() const
{
    int x = static_cast<int>(GetScreenWidth() * 0.18f);
    int y = static_cast<int>(GetScreenHeight() * 0.08f);
    int width = static_cast<int>(GetScreenWidth() * 0.64f);
    int height = static_cast<int>(GetScreenHeight() * 0.84f);
    Rectangle settingsPanel{static_cast<float>(x), static_cast<float>(y),
                            static_cast<float>(width), static_cast<float>(height)};
    if (!UiControlIcons::DrawPixelHudPanelFrame(settingsPanel))
    {
        DrawRectangle(x, y, width, height, Color{20, 24, 31, 225});
        DrawRectangleLines(x, y, width, height, Color{86, 100, 120, 255});
    }
    UiText::DrawFit("Game Settings", Rectangle{static_cast<float>(x + 18), static_cast<float>(y + 14), static_cast<float>(width - 36), 34.0f}, 30, RAYWHITE);
    UiText::DrawFit("Host configuration. These settings are sent to every client on Start.",
        Rectangle{static_cast<float>(x + 22), static_cast<float>(y + 56), static_cast<float>(width - 44), 26.0f},
        19,
        Color{190, 205, 224, 255});
}
