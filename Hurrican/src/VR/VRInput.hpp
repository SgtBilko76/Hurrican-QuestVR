// OpenXR input for the Meta Quest build: Touch controller actions and haptics.
//
// The game consumes this through a "virtual joystick" (DirectJoystickClass::InitVirtual):
// the buttons below are exposed as joystick button indices, the left thumbstick as
// axes 0/1 + POV hat, the right thumbstick as axes 2/3.

#ifndef _VRINPUT_HPP_
#define _VRINPUT_HPP_

namespace VRInput {

// Button index layout of the virtual joystick (persisted in Hurrican.cfg, keep stable)
enum Button {
    BTN_A = 0,
    BTN_B,
    BTN_X,
    BTN_Y,
    BTN_RTRIGGER,
    BTN_LTRIGGER,
    BTN_RGRIP,
    BTN_LGRIP,
    BTN_RSTICK,
    BTN_LSTICK,
    BTN_MENU,
    BTN_COUNT
};

bool Init();      // create action set + actions + suggested bindings, attach to session
void Shutdown();
void Update();    // xrSyncActions + read all states (once per frame)
void OnInteractionProfileChanged();

bool ButtonDown(int button);
float LeftStickX();   // -1 .. 1, right positive
float LeftStickY();   // -1 .. 1, up positive
float RightStickX();
float RightStickY();

// Both thumbsticks clicked -> recenter the virtual screen (edge triggered, consumed)
bool TakeRecenterRequest();

// amplitude 0..1, duration in milliseconds, hand: 0 = left, 1 = right, 2 = both
void Haptic(float amplitude, int durationMs, int hand);

const char *ButtonName(int button);

}  // namespace VRInput

#endif  // _VRINPUT_HPP_
