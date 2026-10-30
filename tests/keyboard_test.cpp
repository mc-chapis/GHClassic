// Exercise the real host input path with scripted keys and a controller,
// without a game window, a disc, or the player's configuration.
#include "host/bindings.h"
#include "host/devices.h"
#include "host/input.h"
#include "host/layouts.h"

#include <SDL3/SDL.h>

#include <filesystem>
#include <iostream>
#include <string>

namespace gh2::settings
{
    std::string userDataPath(const std::string &file)
    {
        static const std::string prefix = "GHClassic-keyboard-test-" + std::to_string(SDL_GetPerformanceCounter()) + "-";
        return (std::filesystem::temp_directory_path() / (prefix + file)).string();
    }
}

namespace
{
    using namespace gh2;
    using namespace gh2::input;
    int s_failed = 0;

#define CHECK(condition) \
    do \
    { \
        if (!(condition)) \
        { \
            std::cerr << "failed: " #condition << " (line " << __LINE__ << ")" << std::endl; \
            ++s_failed; \
        } \
    } while (0)

    void key(SDL_Scancode scancode, bool down)
    {
        setScriptedKey(scancode, down);
        pollInput(false);
    }

    void screen(bool on)
    {
        setControllersScreen(on);
        pollInput(false);
    }
}

int main()
{
    using namespace gh2;
    using namespace gh2::input;
    const DeviceId keyboard = DeviceId::keyboard();
    const std::string path = bindingFilePath();
    // Loading mode must affect gameplay without changing custom bindings.
    CHECK(writeSection(path, keyboardSection(),
                       {{kGreen, {*parseSource("key A"), *parseSource("key Z"), *parseSource("key H"),
                                  *parseSource("key N"), *parseSource("key J")}},
                        {kRed, {*parseSource("key S")}}}, "pad"));
    loadLayouts();
    CHECK(kindOf("", "", DeviceKind::kKeyboard) == DeviceKind::kGamepad);
    addScriptedDevice("guitar");
    setScriptedDevice("guitar", pad::kDown);
    key(SDL_SCANCODE_A, false);
    CHECK(seatHas(0, "guitar"));
    setScriptedDevice("guitar", 0);
    pollInput(false);

    // An unseated keyboard cannot mirror a connected P1 during a song.
    lockSeats(true);
    songScreen(SongScreen::kPlaying);
    pollInput(false);
    key(SDL_SCANCODE_A, true);
    CHECK(hostPad(0).pressed == 0 && !seatHas(0, "keyboard"));
    key(SDL_SCANCODE_A, false);
    key(SDL_SCANCODE_ESCAPE, true);
    CHECK(hostPad(0).pressed == 0);
    key(SDL_SCANCODE_ESCAPE, false);
    lockSeats(false);
    pollInput(false);

    // Keyboard joins P2, swallowing Enter until released.
    setPlayersStep(true);
    key(SDL_SCANCODE_RETURN, true);
    CHECK(seatHas(0, "guitar") && seatHas(1, "keyboard"));
    CHECK(seatPlugged(1));
    CHECK(hostPad(0).pressed == 0 && hostPad(1).pressed == 0);
    key(SDL_SCANCODE_RETURN, false);
    setPlayersStep(false);

    // The configured keyboard mode becomes the seat's DualShock in a song.
    lockSeats(true);
    songScreen(SongScreen::kPlaying);
    pollInput(false);
    key(SDL_SCANCODE_A, true);
    CHECK(hostPad(1).joypad && hostPad(1).pressed == pad::kL2);
    CHECK(hostPad(0).pressed == 0 && !hostPad(0).joypad);
    CHECK(readHostPad(1).pressed == pad::kL2);
    key(SDL_SCANCODE_Z, true);
    pollInput(false); // more host polls cannot hide the required release
    CHECK(hostPad(1).pressed == pad::kL2);
    CHECK(readHostPad(1).pressed == 0);
    CHECK(readHostPad(1).pressed == pad::kL2);
    key(SDL_SCANCODE_A, false); // releasing one binding keeps the other held
    CHECK(readHostPad(1).pressed == pad::kL2);
    key(SDL_SCANCODE_A, true);
    CHECK(readHostPad(1).pressed == 0);
    CHECK(readHostPad(1).pressed == pad::kL2);
    key(SDL_SCANCODE_Z, false);
    CHECK(readHostPad(1).pressed == pad::kL2);
    key(SDL_SCANCODE_A, false);
    CHECK(readHostPad(1).pressed == 0);

    // Five presses before the next game read all survive, on P2 alone.
    for (auto k : {SDL_SCANCODE_A, SDL_SCANCODE_Z, SDL_SCANCODE_H, SDL_SCANCODE_N, SDL_SCANCODE_J})
        key(k, true);
    for (int i = 0; i < 5; ++i)
    {
        CHECK(readHostPad(1).pressed == pad::kL2);
        if (i < 4)
            CHECK(readHostPad(1).pressed == 0);
        CHECK(readHostPad(0).pressed == 0);
    }
    for (auto k : {SDL_SCANCODE_A, SDL_SCANCODE_Z, SDL_SCANCODE_H, SDL_SCANCODE_N, SDL_SCANCODE_J})
        key(k, false);
    CHECK(readHostPad(1).pressed == 0);

    // Retrying green leaves another held fret and the live state alone.
    key(SDL_SCANCODE_S, true);
    key(SDL_SCANCODE_A, true);
    CHECK(readHostPad(1).pressed == (pad::kL1 | pad::kL2));
    key(SDL_SCANCODE_Z, true);
    CHECK(readHostPad(1).pressed == pad::kL1);
    CHECK(readHostPad(1).pressed == (pad::kL1 | pad::kL2));
    key(SDL_SCANCODE_Z, false);
    key(SDL_SCANCODE_A, false);
    key(SDL_SCANCODE_S, false);
    key(SDL_SCANCODE_ESCAPE, true);
    CHECK(hostPad(1).pressed == pad::kStart && hostPad(0).pressed == 0);
    key(SDL_SCANCODE_ESCAPE, false);

    // Mode edits on the Controllers screen preserve keyboard identity,
    // custom bindings and the mode of a song already in progress.
    screen(true);
    swapMode(keyboard);
    pollInput(false);
    CHECK(sectionMode("", "").empty());
    CHECK(formatSource(keyboardLayout()[kGreen].front()) == "key A");
    auto views = deviceViews();
    CHECK(!views.empty() && views.back().id == keyboard);
    CHECK(views.back().kind != DeviceKind::kGamepad && views.back().seat == 1);
    screen(false);
    key(SDL_SCANCODE_A, true);
    CHECK(hostPad(1).joypad && hostPad(1).pressed == pad::kL2);
    key(SDL_SCANCODE_A, false);

    lockSeats(false);
    pollInput(false);
    lockSeats(true);
    pollInput(false);
    key(SDL_SCANCODE_A, true);
    CHECK(!hostPad(1).joypad && hostPad(1).pressed == pad::kR2);
    CHECK(hostPad(0).pressed == 0);
    CHECK(readHostPad(1).pressed == pad::kR2);
    key(SDL_SCANCODE_Z, true);
    CHECK(readHostPad(1).pressed == 0);
    CHECK(readHostPad(1).pressed == pad::kR2);
    key(SDL_SCANCODE_H, true); // leave a pending press as the screen changes
    screen(true);
    CHECK(readHostPad(1).pressed == 0);
    key(SDL_SCANCODE_Z, false);
    key(SDL_SCANCODE_H, false);
    key(SDL_SCANCODE_A, false);
    screen(false);
    CHECK(readHostPad(1).pressed == 0);
    lockSeats(false);
    pollInput(false);

    screen(true);
    swapMode(keyboard);
    pollInput(false);
    CHECK(loadBindingFile(path).modes.at(keyboardSection()) == "pad");
    views = deviceViews();
    CHECK(views.back().id == keyboard && views.back().kind == DeviceKind::kGamepad);
    resetBindings(keyboard);
    pollInput(false);
    CHECK(sectionMode("", "").empty());
    CHECK(formatSource(keyboardLayout()[kGreen].front()) == "key 1");
    screen(false);

    // Keyboard can trade to P1 and use menu shortcuts independently of P2.
    setPlayersStep(true);
    key(SDL_SCANCODE_3, true);
    CHECK(seatHas(0, "keyboard") && seatHas(1, "guitar"));
    key(SDL_SCANCODE_3, false);
    setPlayersStep(false);
    pollInput(false);
    key(SDL_SCANCODE_RETURN, true);
    CHECK(hostPad(0).pressed == pad::kR2 && hostPad(1).pressed == 0);
    key(SDL_SCANCODE_RETURN, false);
    CHECK(menuControls().kind == DeviceKind::kKeyboard);

    // A seated keyboard cannot also play for a disconnected P2.
    removeScriptedDevice("guitar");
    lockSeats(true);
    pollInput(false);
    key(SDL_SCANCODE_1, true);
    CHECK(hostPad(0).pressed == pad::kR2 && hostPad(1).pressed == 0);
    CHECK(!seatPlugged(1));
    key(SDL_SCANCODE_1, false);

    std::filesystem::remove(path);
    SDL_Quit();
    if (s_failed)
        return 1;
    std::cerr << "keyboard: all passed" << std::endl;
    return 0;
}
