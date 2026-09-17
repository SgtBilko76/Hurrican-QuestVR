// Datei : DX8Input.cpp

// --------------------------------------------------------------------------------------
//
// DirectInput Klasse
// zum Erfassen von Eingaben durch Tastaur, Maus und Joystick/Joypad
//
// (c) 2002 Jörg M. Winterstein
//
// --------------------------------------------------------------------------------------

// --------------------------------------------------------------------------------------
// Include Dateien
// --------------------------------------------------------------------------------------

#include "DX8Input.hpp"
#include <algorithm>
#include "Gameplay.hpp"
#include "Globals.hpp"
#include "Logdatei.hpp"
#include "Main.hpp"
#if defined(USE_VR)
#  include "VR/VRInput.hpp"
#endif

// --------------------------------------------------------------------------------------
// Variablen
// --------------------------------------------------------------------------------------

const Uint8 *TastaturPuffer;

#if 0
// --------------------------------------------------------------------------------------
// Joystick mit ForceFeedback Enumeration Callback-Funktion
// --------------------------------------------------------------------------------------

bool EnumForceFeedbackDevices(int lpddi, void *pv) {
    return true;
}
// --------------------------------------------------------------------------------------
// Joystick Enumeration Callback-Funktion
// --------------------------------------------------------------------------------------

bool EnumJoystickDevices(int lpddi, void *pv) {
    return true;
}
#endif


// --------------------------------------------------------------------------------------
// Klassenfunktionen
// --------------------------------------------------------------------------------------

// --------------------------------------------------------------------------------------
// DirectInput Klasse
// --------------------------------------------------------------------------------------

// --------------------------------------------------------------------------------------
// Konstruktor
// --------------------------------------------------------------------------------------

DirectInputClass::DirectInputClass() {
#if 0
    // Zu Beginn alle Eingabegeräte zurücksetzen
    MausX = 0;
    MausY = 0;
    MausButtons.reset();
#endif

    JoysticksFound = 0;
    UseForceFeedback = false;
#if defined(GCW)
    InternalJoystickIndex = 0;  // DKS: Set to the device's internal joystick controls, default is 0.
#endif                          // GCW
}

// --------------------------------------------------------------------------------------
// Destruktor
// --------------------------------------------------------------------------------------

DirectInputClass::~DirectInputClass() = default;

// --------------------------------------------------------------------------------------
// DirectInput initialisieren
// Keyboard und Maus initialisieren und Joystick, falls vorhanden
// --------------------------------------------------------------------------------------
bool DirectInputClass::Init() {
#if SDL_VERSION_ATLEAST(2,0,0)
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
#endif

#if SDL_VERSION_ATLEAST(2, 0, 0)
    TastaturPuffer = SDL_GetKeyboardState(&NumberOfKeys);
#else
    TastaturPuffer = SDL_GetKeyState(&NumberOfKeys);
#endif
    Protokoll << "DirectInput8 polling for " << NumberOfKeys << " keys!" << std::endl;

#  if SDL_VERSION_ATLEAST(2,0,2)
    std::string gcdb(g_storage_ext);
    gcdb.append("/data/gamecontrollerdb.txt");
    int res = SDL_GameControllerAddMappingsFromFile(gcdb.c_str());
    if (res > 0) {
        Protokoll << "Loaded " << res << " mappings from game controller db" << std::endl;
    }
#  endif
    for (int i = 0; i < JoysticksFound; i++)
        Joysticks[i].Exit(i);

    int first_sdl_slot = 0;
#if defined(USE_VR)
    // Slot 0 is always the Touch controllers; Bluetooth gamepads follow after it
    Joysticks[0].InitVirtual("Quest Touch");
    JoystickFound = true;
    first_sdl_slot = 1;
#endif
    int const sdl_joysticks = std::min(SDL_NumJoysticks(), MAX_JOYSTICKS - first_sdl_slot);
    JoysticksFound = first_sdl_slot + sdl_joysticks;

    for (int sdl_idx = 0; sdl_idx < sdl_joysticks; sdl_idx++) {
        int const i = sdl_idx + first_sdl_slot;
        if (!Joysticks[i].Init(sdl_idx)) {
            Protokoll << "Error opening joystick" << std::endl;
        } else {
            JoystickFound = true;

#if defined(GCW)
            if (strcmp(Joysticks[i].JoystickName, "linkdev device (Analog 2-axis 8-button 2-hat)") == 0) {
                // Replace GCW control's name with something that is clearer
                strcpy_s(Joysticks[i].JoystickName, "GCW Zero");
                InternalJoystickIndex = i;
                Protokoll << "Found GCW Zero's built-in controls." << std::endl;
            }
#endif  // GCW
        }
    }

    return true;
}

// --------------------------------------------------------------------------------------
// DirectInput beenden
// --------------------------------------------------------------------------------------

void DirectInputClass::Exit() {
    for (int i = 0; i < JoysticksFound; i++) {
        Joysticks[i].Exit(i);
    }

    Protokoll << "-> SDL/OpenGL input shutdown successfully completed !" << std::endl;
}

// --------------------------------------------------------------------------------------
// Tasten abfragen und im Puffer speichern
// --------------------------------------------------------------------------------------

bool DirectInputClass::UpdateTastatur() {
    SDL_PumpEvents();
    return true;
}

#if 0
// --------------------------------------------------------------------------------------
// Maus abfragen
// true  = gepuffert
// false = ungepuffert
// --------------------------------------------------------------------------------------

bool DirectInputClass::UpdateMaus(bool gepuffert) {
    // Position
    uint8_t buttons = SDL_GetMouseState(&MausX, &MausY);

    // Buttons
    MausButtons[0] = ((buttons & SDL_BUTTON(1)) == SDL_BUTTON(1));
    MausButtons[1] = ((buttons & SDL_BUTTON(2)) == SDL_BUTTON(2));
    MausButtons[2] = ((buttons & SDL_BUTTON(3)) == SDL_BUTTON(3));

    MausX = std::clamp(MausX, 0, RENDERWIDTH);
    MausY = std::clamp(MausY, 0, RENDERHEIGHT);

    return true;
}

// --------------------------------------------------------------------------------------
// Keyboard wieder akquirieren (wenn Fenster gewechselt zB)
// --------------------------------------------------------------------------------------

void DirectInputClass::AcquireKeyboard() {}
#endif

// --------------------------------------------------------------------------------------
// Alle Joysticks updaten
// --------------------------------------------------------------------------------------

void DirectInputClass::UpdateJoysticks() {
    SDL_PumpEvents();
#if defined(USE_VR)
    VRInput::Update();
#endif
    for (int i = 0; i < JoysticksFound; i++)
        Joysticks[i].Update();
}

// --------------------------------------------------------------------------------------
// Checken ob keine Taste mehr gedrückt ist
// --------------------------------------------------------------------------------------

bool DirectInputClass::AreAllKeysReleased() const {
    for (int i = 0; i < std::min(NumberOfKeys, MAX_KEYS); i++)
        if (TastaturPuffer[i] != 0)
            return false;

    return true;
}

// --------------------------------------------------------------------------------------
// Checken ob irgendeine Taste gedrückt ist
// --------------------------------------------------------------------------------------

bool DirectInputClass::AnyKeyDown() const {
    for (int i = 0; i < std::min(NumberOfKeys, MAX_KEYS); i++)
        if (KeyDown(i))
            return true;

    return false;
}

// --------------------------------------------------------------------------------------
// Checken ob irgendein Button gedrückt ist
// --------------------------------------------------------------------------------------

bool DirectInputClass::AnyButtonDown() const {
    if (!JoystickFound)
        return false;

    for (int i = 0; i < JoysticksFound; i++)
        if (Joysticks[i].JoystickButtons.any())
            return true;

    return false;
}

// DKS - GCW Zero handheld-specific button strings
#if defined(GCW)
#define GCW_MAX_BUTTONS 8
static char InternalButtonNumToStringMap[8][10] = {
    "B",  // Button 0
    "A", "Y", "X", "Select", "Start", "L",
    "R"  // Button 7
};
#endif  // GCW

// DKS - Added helper function to facilitate customized naming of joystick buttons and
//      when not customized, to report joy buttons as ranging 1..99 instead of 0..98
char *DirectInputClass::MapButtonToString(int joy_idx, int button) {
    static char buf[60];

    if (button < 0) {
        return TextArray[TEXT::NICHT_DEFINIERT];
    } else {
#if defined(USE_VR)
        if (joy_idx >= 0 && joy_idx < MAX_JOYSTICKS && Joysticks[joy_idx].IsVirtual) {
            snprintf(buf, sizeof(buf), "%s", VRInput::ButtonName(button));
            return buf;
        }
#endif
#ifdef GCW
        // Special case for GCW Zero's internal controls:
        if (joy_idx == GetInternalJoystickIndex() && button < GCW_MAX_BUTTONS) {
            return InternalButtonNumToStringMap[button];
        } else
#endif  // GCW
        // Non-platform-specific generic code for all joysticks:
        {
            // Report button numbers as ranging from 1..99 instead of 0..98
            snprintf(buf, 20, "%d", button + 1);
            return buf;
        }
    }
}

// DKS - Convenience function for when in the actual game
#if defined(GCW)
bool DirectInputClass::InternalJoystickMainMenuButtonDown() {
    int joy_idx = GetInternalJoystickIndex();
    int button_idx = GetInternalJoystickMainMenuButton();
    if (joy_idx >= 0 && joy_idx < JoysticksFound && Joysticks[joy_idx].Active &&
        button_idx < Joysticks[joy_idx].NumButtons)
        return Joysticks[joy_idx].JoystickButtons[button_idx];
    else
        return false;
}
#endif  // GCW
