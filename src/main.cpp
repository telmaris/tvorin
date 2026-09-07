#include "scenes/GameWindow.h"
#include "core/Log.h"
#include "Version.h"

int main(void)
{
    Log::Initialize();
    Log::Msg("[Main]", "Starting Tvorin ", TVORIN_VERSION_STRING);
    GameWindow window;
    window.LaunchGame();
    Log::Msg("[Main]", "Tvorin shutdown");
    Log::Shutdown();

    return 0;
}
