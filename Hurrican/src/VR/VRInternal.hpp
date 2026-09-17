// Shared OpenXR handles between VRSystem.cpp and VRInput.cpp (not for engine code).

#ifndef _VRINTERNAL_HPP_
#define _VRINTERNAL_HPP_

#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <jni.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <string>

namespace VR {

struct XrState {
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId systemId = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace localSpace = XR_NULL_HANDLE;
    XrSpace viewSpace = XR_NULL_HANDLE;
    XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
    bool sessionRunning = false;
    bool focused = false;
    XrTime predictedDisplayTime = 0;
};

extern XrState xr;

// Log an XrResult failure (returns true on success).
bool CheckXr(XrResult res, const char *what);

void Log(const char *fmt, ...);

}  // namespace VR

#endif  // _VRINTERNAL_HPP_
