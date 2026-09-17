// OpenXR input for the Meta Quest build, see VRInput.hpp

#include "VRInput.hpp"
#include "VRInternal.hpp"

#include <cstring>
#include <vector>

namespace VRInput {

namespace {

enum Hand { LEFT = 0, RIGHT = 1, HANDS = 2 };

XrActionSet actionSet_ = XR_NULL_HANDLE;
XrPath handPath_[HANDS] = {XR_NULL_PATH, XR_NULL_PATH};

// Simple (no subaction) boolean actions
XrAction actA_ = XR_NULL_HANDLE, actB_ = XR_NULL_HANDLE, actX_ = XR_NULL_HANDLE, actY_ = XR_NULL_HANDLE,
         actMenu_ = XR_NULL_HANDLE;
// Per-hand actions (subaction paths /user/hand/left|right)
XrAction actTrigger_ = XR_NULL_HANDLE, actGrip_ = XR_NULL_HANDLE, actStick_ = XR_NULL_HANDLE,
         actStickClick_ = XR_NULL_HANDLE, actHaptic_ = XR_NULL_HANDLE;

bool buttons_[BTN_COUNT] = {};
float stick_[HANDS][2] = {};
bool recenterLatch_ = false;
bool recenterRequest_ = false;

constexpr float TRIGGER_THRESHOLD = 0.5f;
constexpr float GRIP_THRESHOLD = 0.6f;

XrPath Path(const char *s) {
    XrPath p = XR_NULL_PATH;
    xrStringToPath(VR::xr.instance, s, &p);
    return p;
}

bool CreateAction(XrAction &out, const char *name, const char *localized, XrActionType type, bool perHand) {
    XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
    info.actionType = type;
    strncpy(info.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
    strncpy(info.localizedActionName, localized, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    if (perHand) {
        info.countSubactionPaths = HANDS;
        info.subactionPaths = handPath_;
    }
    return VR::CheckXr(xrCreateAction(actionSet_, &info, &out), name);
}

bool GetBool(XrAction action, XrPath subaction = XR_NULL_PATH) {
    XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
    info.action = action;
    info.subactionPath = subaction;
    XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
    if (XR_FAILED(xrGetActionStateBoolean(VR::xr.session, &info, &state)))
        return false;
    return state.isActive && state.currentState;
}

float GetFloat(XrAction action, XrPath subaction) {
    XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
    info.action = action;
    info.subactionPath = subaction;
    XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
    if (XR_FAILED(xrGetActionStateFloat(VR::xr.session, &info, &state)) || !state.isActive)
        return 0.0f;
    return state.currentState;
}

void GetVec2(XrAction action, XrPath subaction, float &x, float &y) {
    XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
    info.action = action;
    info.subactionPath = subaction;
    XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};
    x = y = 0.0f;
    if (XR_FAILED(xrGetActionStateVector2f(VR::xr.session, &info, &state)) || !state.isActive)
        return;
    x = state.currentState.x;
    y = state.currentState.y;
}

void ClearState() {
    memset(buttons_, 0, sizeof(buttons_));
    memset(stick_, 0, sizeof(stick_));
}

}  // namespace

bool Init() {
    XrActionSetCreateInfo setInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
    strncpy(setInfo.actionSetName, "gameplay", XR_MAX_ACTION_SET_NAME_SIZE - 1);
    strncpy(setInfo.localizedActionSetName, "Gameplay", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
    setInfo.priority = 0;
    if (!VR::CheckXr(xrCreateActionSet(VR::xr.instance, &setInfo, &actionSet_), "xrCreateActionSet"))
        return false;

    handPath_[LEFT] = Path("/user/hand/left");
    handPath_[RIGHT] = Path("/user/hand/right");

    bool ok = true;
    ok &= CreateAction(actA_, "button_a", "Button A", XR_ACTION_TYPE_BOOLEAN_INPUT, false);
    ok &= CreateAction(actB_, "button_b", "Button B", XR_ACTION_TYPE_BOOLEAN_INPUT, false);
    ok &= CreateAction(actX_, "button_x", "Button X", XR_ACTION_TYPE_BOOLEAN_INPUT, false);
    ok &= CreateAction(actY_, "button_y", "Button Y", XR_ACTION_TYPE_BOOLEAN_INPUT, false);
    ok &= CreateAction(actMenu_, "button_menu", "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT, false);
    ok &= CreateAction(actTrigger_, "trigger", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT, true);
    ok &= CreateAction(actGrip_, "grip", "Grip", XR_ACTION_TYPE_FLOAT_INPUT, true);
    ok &= CreateAction(actStick_, "thumbstick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT, true);
    ok &= CreateAction(actStickClick_, "thumbstick_click", "Thumbstick click", XR_ACTION_TYPE_BOOLEAN_INPUT, true);
    ok &= CreateAction(actHaptic_, "haptic", "Vibration", XR_ACTION_TYPE_VIBRATION_OUTPUT, true);
    if (!ok)
        return false;

    const std::vector<XrActionSuggestedBinding> bindings = {
        {actA_, Path("/user/hand/right/input/a/click")},
        {actB_, Path("/user/hand/right/input/b/click")},
        {actX_, Path("/user/hand/left/input/x/click")},
        {actY_, Path("/user/hand/left/input/y/click")},
        {actMenu_, Path("/user/hand/left/input/menu/click")},
        {actTrigger_, Path("/user/hand/left/input/trigger/value")},
        {actTrigger_, Path("/user/hand/right/input/trigger/value")},
        {actGrip_, Path("/user/hand/left/input/squeeze/value")},
        {actGrip_, Path("/user/hand/right/input/squeeze/value")},
        {actStick_, Path("/user/hand/left/input/thumbstick")},
        {actStick_, Path("/user/hand/right/input/thumbstick")},
        {actStickClick_, Path("/user/hand/left/input/thumbstick/click")},
        {actStickClick_, Path("/user/hand/right/input/thumbstick/click")},
        {actHaptic_, Path("/user/hand/left/output/haptic")},
        {actHaptic_, Path("/user/hand/right/output/haptic")},
    };
    XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggested.interactionProfile = Path("/interaction_profiles/oculus/touch_controller");
    suggested.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
    suggested.suggestedBindings = bindings.data();
    if (!VR::CheckXr(xrSuggestInteractionProfileBindings(VR::xr.instance, &suggested),
                     "xrSuggestInteractionProfileBindings(touch)"))
        return false;

    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets = 1;
    attach.actionSets = &actionSet_;
    if (!VR::CheckXr(xrAttachSessionActionSets(VR::xr.session, &attach), "xrAttachSessionActionSets"))
        return false;

    VR::Log("input actions ready");
    return true;
}

void Shutdown() {
    if (actionSet_ != XR_NULL_HANDLE)
        xrDestroyActionSet(actionSet_);  // destroys the actions too
    actionSet_ = XR_NULL_HANDLE;
    ClearState();
}

void OnInteractionProfileChanged() {
    XrInteractionProfileState state{XR_TYPE_INTERACTION_PROFILE_STATE};
    for (int h = 0; h < HANDS; h++) {
        if (XR_SUCCEEDED(xrGetCurrentInteractionProfile(VR::xr.session, handPath_[h], &state)) &&
            state.interactionProfile != XR_NULL_PATH) {
            char buf[XR_MAX_PATH_LENGTH];
            uint32_t len = 0;
            xrPathToString(VR::xr.instance, state.interactionProfile, sizeof(buf), &len, buf);
            VR::Log("hand %d interaction profile: %s", h, buf);
        }
    }
}

void Update() {
    if (actionSet_ == XR_NULL_HANDLE || !VR::xr.sessionRunning || !VR::xr.focused) {
        ClearState();
        return;
    }

    XrActiveActionSet active{actionSet_, XR_NULL_PATH};
    XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    const XrResult res = xrSyncActions(VR::xr.session, &sync);
    if (res != XR_SUCCESS) {  // XR_SESSION_NOT_FOCUSED etc.
        ClearState();
        return;
    }

    buttons_[BTN_A] = GetBool(actA_);
    buttons_[BTN_B] = GetBool(actB_);
    buttons_[BTN_X] = GetBool(actX_);
    buttons_[BTN_Y] = GetBool(actY_);
    buttons_[BTN_MENU] = GetBool(actMenu_);
    buttons_[BTN_RTRIGGER] = GetFloat(actTrigger_, handPath_[RIGHT]) > TRIGGER_THRESHOLD;
    buttons_[BTN_LTRIGGER] = GetFloat(actTrigger_, handPath_[LEFT]) > TRIGGER_THRESHOLD;
    buttons_[BTN_RGRIP] = GetFloat(actGrip_, handPath_[RIGHT]) > GRIP_THRESHOLD;
    buttons_[BTN_LGRIP] = GetFloat(actGrip_, handPath_[LEFT]) > GRIP_THRESHOLD;
    buttons_[BTN_RSTICK] = GetBool(actStickClick_, handPath_[RIGHT]);
    buttons_[BTN_LSTICK] = GetBool(actStickClick_, handPath_[LEFT]);
    GetVec2(actStick_, handPath_[LEFT], stick_[LEFT][0], stick_[LEFT][1]);
    GetVec2(actStick_, handPath_[RIGHT], stick_[RIGHT][0], stick_[RIGHT][1]);

    // Both sticks clicked together: recenter (edge triggered)
    const bool both = buttons_[BTN_RSTICK] && buttons_[BTN_LSTICK];
    if (both && !recenterLatch_)
        recenterRequest_ = true;
    recenterLatch_ = both;
}

bool ButtonDown(int button) {
    return button >= 0 && button < BTN_COUNT && buttons_[button];
}

float LeftStickX() { return stick_[LEFT][0]; }
float LeftStickY() { return stick_[LEFT][1]; }
float RightStickX() { return stick_[RIGHT][0]; }
float RightStickY() { return stick_[RIGHT][1]; }

bool TakeRecenterRequest() {
    const bool r = recenterRequest_;
    recenterRequest_ = false;
    return r;
}

void Haptic(float amplitude, int durationMs, int hand) {
    if (actHaptic_ == XR_NULL_HANDLE || !VR::xr.sessionRunning || !VR::xr.focused)
        return;
    XrHapticVibration vib{XR_TYPE_HAPTIC_VIBRATION};
    vib.amplitude = amplitude;
    vib.duration = static_cast<XrDuration>(durationMs) * 1000000;
    vib.frequency = XR_FREQUENCY_UNSPECIFIED;
    for (int h = 0; h < HANDS; h++) {
        if (hand != 2 && hand != h)
            continue;
        XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
        info.action = actHaptic_;
        info.subactionPath = handPath_[h];
        xrApplyHapticFeedback(VR::xr.session, &info, reinterpret_cast<const XrHapticBaseHeader *>(&vib));
    }
}

const char *ButtonName(int button) {
    static const char *names[BTN_COUNT] = {"A", "B", "X", "Y", "RT", "LT", "RG", "LG", "RS", "LS", "Menu"};
    if (button < 0 || button >= BTN_COUNT)
        return "?";
    return names[button];
}

}  // namespace VRInput
