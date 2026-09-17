// OpenXR session management for the Meta Quest build.
//
// Owns the EGL context (SDL is told not to touch EGL via SDL_HINT_VIDEO_EXTERNAL_CONTEXT),
// the OpenXR instance/session/spaces/swapchains, the frame loop (xrWaitFrame ...
// xrEndFrame) and the pose of the virtual screen the 2D game is projected onto.

#ifndef _VRSYSTEM_HPP_
#define _VRSYSTEM_HPP_

#include <cstdint>
#include <string>

#include <glm/mat4x4.hpp>

namespace VR {

struct Config {
    float screenWidth = 3.2f;      // metres, 4:3 aspect
    float screenDistance = 2.8f;   // metres in front of the (recentered) viewer
    float screenHeight = -0.15f;   // vertical offset of the screen centre vs. the eyes
    float depthStrength = 0.10f;   // 0 = flat, 1 = full layer separation (tuned down from 1.0 in steps on user feedback)
    float refreshRate = 72.0f;     // requested display refresh rate (0 = runtime default)
};

extern Config config;

// Read `path` (key = value lines, see Config) and write a default file if missing.
void LoadConfig(const std::string &path);

// EGL context (ES 3.x, pbuffer surface). Must be called before Init().
bool CreateEGL();
void DestroyEGL();

// OpenXR bring-up (loader, instance, session, spaces, swapchains, input).
bool Init();
void Shutdown();

// Session state machine. Call once per loop iteration (also safe to call more often).
void PollEvents();
bool SessionRunning();  // xrBeginSession has been called and the session wasn't stopped
bool ExitRequested();   // runtime asked us to quit (session exiting / instance lost)

// Frame loop.
bool FrameInProgress();
bool BeginFrame();                // xrWaitFrame + xrBeginFrame; false if not running
bool LocateViews();               // update per-eye poses for the current frame
void EndFrame(bool rendered);     // submit the projection layer (or nothing)

int EyeCount();
void GetEyeMatrices(int eye, glm::mat4 &proj, glm::mat4 &view);
// Acquire the eye's swapchain image; returns the FBO to render into (0 on failure).
uint32_t AcquireEyeFramebuffer(int eye, int &width, int &height);
void ReleaseEyeFramebuffer(int eye);
bool SwapchainIsSRGB();

// Virtual screen (centre of the 4:3 game plane, in LOCAL space).
void Recenter();
const glm::mat4 &ScreenModel();
int64_t PredictedDisplayTime();

}  // namespace VR

#endif  // _VRSYSTEM_HPP_
