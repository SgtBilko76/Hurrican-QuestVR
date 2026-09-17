// OpenXR session management for the Meta Quest build, see VRSystem.hpp

#include "VRSystem.hpp"
#include "VRInternal.hpp"
#include "VRInput.hpp"

#include <android/log.h>
#include <SDL.h>

#include <cmath>
#include <cstdarg>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#ifndef EGL_OPENGL_ES3_BIT_KHR
#define EGL_OPENGL_ES3_BIT_KHR 0x0040
#endif

extern bool GameRunning;

namespace VR {

Config config;
XrState xr;

namespace {

constexpr int NUM_EYES = 2;

// ---- EGL --------------------------------------------------------------------------
EGLDisplay eglDisplay_ = EGL_NO_DISPLAY;
EGLConfig eglConfig_ = nullptr;
EGLContext eglContext_ = EGL_NO_CONTEXT;
EGLSurface eglPbuffer_ = EGL_NO_SURFACE;

// ---- OpenXR objects ---------------------------------------------------------------
struct Swapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    int32_t width = 0;
    int32_t height = 0;
    std::vector<XrSwapchainImageOpenGLESKHR> images;
    std::vector<GLuint> framebuffers;
    bool acquired = false;
};

Swapchain swapchains_[NUM_EYES];
int64_t swapchainFormat_ = 0;
XrViewConfigurationView viewConfigs_[NUM_EYES];
XrView views_[NUM_EYES];
bool viewsValid_ = false;
bool frameInProgress_ = false;
XrFrameState frameState_{XR_TYPE_FRAME_STATE};
bool exitRequested_ = false;
bool recenterPending_ = true;
glm::mat4 screenModel_(1.0f);
jobject activityGlobalRef_ = nullptr;
bool hasRefreshRateExt_ = false;
PFN_xrRequestDisplayRefreshRateFB pfnRequestDisplayRefreshRateFB_ = nullptr;

glm::mat4 PoseToMatrix(const XrPosef &pose) {
    const glm::quat q(pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z);
    glm::mat4 m = glm::mat4_cast(q);
    m[3] = glm::vec4(pose.position.x, pose.position.y, pose.position.z, 1.0f);
    return m;
}

glm::mat4 ProjectionFromFov(const XrFovf &fov, float nearZ, float farZ) {
    const float l = tanf(fov.angleLeft);
    const float r = tanf(fov.angleRight);
    const float d = tanf(fov.angleDown);
    const float u = tanf(fov.angleUp);

    glm::mat4 m(0.0f);
    m[0][0] = 2.0f / (r - l);
    m[1][1] = 2.0f / (u - d);
    m[2][0] = (r + l) / (r - l);
    m[2][1] = (u + d) / (u - d);
    m[2][2] = -(farZ + nearZ) / (farZ - nearZ);
    m[2][3] = -1.0f;
    m[3][2] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
    return m;
}

bool HasExtension(const std::vector<XrExtensionProperties> &exts, const char *name) {
    for (const auto &e : exts)
        if (strcmp(e.extensionName, name) == 0)
            return true;
    return false;
}

bool CreateSwapchains() {
    uint32_t formatCount = 0;
    xrEnumerateSwapchainFormats(xr.session, 0, &formatCount, nullptr);
    std::vector<int64_t> formats(formatCount);
    xrEnumerateSwapchainFormats(xr.session, formatCount, &formatCount, formats.data());

    swapchainFormat_ = 0;
    for (int64_t f : formats) {
        if (f == GL_SRGB8_ALPHA8) {
            swapchainFormat_ = f;
            break;
        }
    }
    if (swapchainFormat_ == 0) {
        for (int64_t f : formats)
            if (f == GL_RGBA8) {
                swapchainFormat_ = f;
                break;
            }
    }
    if (swapchainFormat_ == 0) {
        Log("no usable swapchain format (need GL_SRGB8_ALPHA8 or GL_RGBA8)");
        return false;
    }
    Log("swapchain format 0x%llx (%s)", static_cast<long long>(swapchainFormat_),
        swapchainFormat_ == GL_SRGB8_ALPHA8 ? "sRGB" : "linear");

    for (int eye = 0; eye < NUM_EYES; eye++) {
        Swapchain &sc = swapchains_[eye];
        XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        info.format = swapchainFormat_;
        info.sampleCount = 1;
        info.width = viewConfigs_[eye].recommendedImageRectWidth;
        info.height = viewConfigs_[eye].recommendedImageRectHeight;
        info.faceCount = 1;
        info.arraySize = 1;
        info.mipCount = 1;
        if (!CheckXr(xrCreateSwapchain(xr.session, &info, &sc.handle), "xrCreateSwapchain"))
            return false;
        sc.width = info.width;
        sc.height = info.height;

        uint32_t imageCount = 0;
        xrEnumerateSwapchainImages(sc.handle, 0, &imageCount, nullptr);
        sc.images.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
        if (!CheckXr(xrEnumerateSwapchainImages(sc.handle, imageCount, &imageCount,
                                                reinterpret_cast<XrSwapchainImageBaseHeader *>(sc.images.data())),
                     "xrEnumerateSwapchainImages"))
            return false;

        sc.framebuffers.resize(imageCount, 0);
        glGenFramebuffers(imageCount, sc.framebuffers.data());
        for (uint32_t i = 0; i < imageCount; i++) {
            glBindTexture(GL_TEXTURE_2D, sc.images[i].image);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindFramebuffer(GL_FRAMEBUFFER, sc.framebuffers[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sc.images[i].image, 0);
            const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE) {
                Log("eye %d swapchain FBO %u incomplete: 0x%x", eye, i, status);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                return false;
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        Log("eye %d swapchain %dx%d, %u images", eye, sc.width, sc.height, imageCount);
    }
    return true;
}

void DestroySwapchains() {
    for (auto &sc : swapchains_) {
        if (!sc.framebuffers.empty())
            glDeleteFramebuffers(static_cast<GLsizei>(sc.framebuffers.size()), sc.framebuffers.data());
        sc.framebuffers.clear();
        sc.images.clear();
        if (sc.handle != XR_NULL_HANDLE)
            xrDestroySwapchain(sc.handle);
        sc.handle = XR_NULL_HANDLE;
    }
}

void HandleSessionStateChange(const XrEventDataSessionStateChanged &ev) {
    xr.sessionState = ev.state;
    switch (ev.state) {
        case XR_SESSION_STATE_READY: {
            XrSessionBeginInfo info{XR_TYPE_SESSION_BEGIN_INFO};
            info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            if (CheckXr(xrBeginSession(xr.session, &info), "xrBeginSession")) {
                xr.sessionRunning = true;
                recenterPending_ = true;
                Log("session running");
                if (hasRefreshRateExt_ && pfnRequestDisplayRefreshRateFB_ != nullptr && config.refreshRate > 0.0f) {
                    const XrResult r = pfnRequestDisplayRefreshRateFB_(xr.session, config.refreshRate);
                    Log("requested %.0f Hz -> %d", config.refreshRate, static_cast<int>(r));
                }
            }
            break;
        }
        case XR_SESSION_STATE_STOPPING:
            Log("session stopping");
            CheckXr(xrEndSession(xr.session), "xrEndSession");
            xr.sessionRunning = false;
            xr.focused = false;
            frameInProgress_ = false;
            break;
        case XR_SESSION_STATE_FOCUSED:
            xr.focused = true;
            break;
        case XR_SESSION_STATE_VISIBLE:
        case XR_SESSION_STATE_SYNCHRONIZED:
            xr.focused = false;
            break;
        case XR_SESSION_STATE_EXITING:
        case XR_SESSION_STATE_LOSS_PENDING:
            Log("session exiting/lost -> quitting");
            exitRequested_ = true;
            GameRunning = false;
            break;
        default:
            break;
    }
}

void UpdateScreenPoseFromHead() {
    XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
    if (!CheckXr(xrLocateSpace(xr.viewSpace, xr.localSpace, xr.predictedDisplayTime, &loc), "xrLocateSpace"))
        return;
    const XrSpaceLocationFlags needed = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
    if ((loc.locationFlags & needed) != needed)
        return;

    // Yaw of the head: project the forward vector (-Z) onto the XZ plane
    const glm::quat q(loc.pose.orientation.w, loc.pose.orientation.x, loc.pose.orientation.y,
                      loc.pose.orientation.z);
    const glm::vec3 fwd = q * glm::vec3(0.0f, 0.0f, -1.0f);
    const float yaw = atan2f(-fwd.x, -fwd.z);

    const glm::vec3 head(loc.pose.position.x, loc.pose.position.y, loc.pose.position.z);
    const glm::mat4 rot = glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 centre = head + glm::vec3(rot * glm::vec4(0.0f, config.screenHeight, -config.screenDistance, 0.0f));

    screenModel_ = glm::translate(glm::mat4(1.0f), centre) * rot;
    recenterPending_ = false;
    Log("screen recentred at (%.2f %.2f %.2f), yaw %.1f deg", centre.x, centre.y, centre.z, glm::degrees(yaw));
}

}  // namespace

// ---------------------------------------------------------------------------------------

void Log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    __android_log_vprint(ANDROID_LOG_INFO, "HurricanVR", fmt, ap);
    va_end(ap);
}

bool CheckXr(XrResult res, const char *what) {
    if (XR_SUCCEEDED(res))
        return true;
    char buf[XR_MAX_RESULT_STRING_SIZE] = "?";
    if (xr.instance != XR_NULL_HANDLE)
        xrResultToString(xr.instance, res, buf);
    Log("%s failed: %s (%d)", what, buf, static_cast<int>(res));
    return false;
}

void LoadConfig(const std::string &path) {
    std::ifstream in(path);
    if (!in) {
        std::ofstream out(path);
        out << "# Hurrican VR settings (metres / factors). Delete this file to restore defaults.\n"
            << "screen_width = " << config.screenWidth << "\n"
            << "screen_distance = " << config.screenDistance << "\n"
            << "screen_height = " << config.screenHeight << "\n"
            << "depth_strength = " << config.depthStrength << "\n"
            << "refresh_rate = " << config.refreshRate << "\n";
        Log("wrote default %s", path.c_str());
        return;
    }
    std::string line;
    while (std::getline(in, line)) {
        const size_t hash = line.find('#');
        if (hash != std::string::npos)
            line.erase(hash);
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = line.substr(0, eq);
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t\r") + 1);
        const float value = strtof(line.c_str() + eq + 1, nullptr);
        if (key == "screen_width" && value > 0.2f) config.screenWidth = value;
        else if (key == "screen_distance" && value > 0.3f) config.screenDistance = value;
        else if (key == "screen_height") config.screenHeight = value;
        else if (key == "depth_strength" && value >= 0.0f) config.depthStrength = value;
        else if (key == "refresh_rate" && value >= 0.0f) config.refreshRate = value;
    }
    Log("config: screen %.2fm at %.2fm (h %.2f), depth %.2f, %.0f Hz", config.screenWidth, config.screenDistance,
        config.screenHeight, config.depthStrength, config.refreshRate);
}

// ---- EGL ----------------------------------------------------------------------------

bool CreateEGL() {
    eglDisplay_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    if (eglDisplay_ == EGL_NO_DISPLAY || !eglInitialize(eglDisplay_, &major, &minor)) {
        Log("eglInitialize failed: 0x%x", eglGetError());
        return false;
    }
    Log("EGL %d.%d", major, minor);

    // Pick a config explicitly (eglChooseConfig may ignore some of the bits we need).
    EGLint numConfigs = 0;
    eglGetConfigs(eglDisplay_, nullptr, 0, &numConfigs);
    std::vector<EGLConfig> configs(numConfigs);
    eglGetConfigs(eglDisplay_, configs.data(), numConfigs, &numConfigs);

    const EGLint wanted[][2] = {{EGL_RED_SIZE, 8},   {EGL_GREEN_SIZE, 8}, {EGL_BLUE_SIZE, 8},
                                {EGL_ALPHA_SIZE, 8}, {EGL_DEPTH_SIZE, 0}, {EGL_STENCIL_SIZE, 0},
                                {EGL_SAMPLES, 0}};
    eglConfig_ = nullptr;
    for (EGLint i = 0; i < numConfigs && eglConfig_ == nullptr; i++) {
        EGLint value = 0;
        eglGetConfigAttrib(eglDisplay_, configs[i], EGL_RENDERABLE_TYPE, &value);
        if ((value & EGL_OPENGL_ES3_BIT_KHR) != EGL_OPENGL_ES3_BIT_KHR)
            continue;
        eglGetConfigAttrib(eglDisplay_, configs[i], EGL_SURFACE_TYPE, &value);
        if ((value & (EGL_WINDOW_BIT | EGL_PBUFFER_BIT)) != (EGL_WINDOW_BIT | EGL_PBUFFER_BIT))
            continue;
        bool ok = true;
        for (const auto &w : wanted) {
            eglGetConfigAttrib(eglDisplay_, configs[i], w[0], &value);
            if (value != w[1]) {
                ok = false;
                break;
            }
        }
        if (ok)
            eglConfig_ = configs[i];
    }
    if (eglConfig_ == nullptr) {
        Log("no suitable EGLConfig found");
        return false;
    }

    const EGLint ctxAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    eglContext_ = eglCreateContext(eglDisplay_, eglConfig_, EGL_NO_CONTEXT, ctxAttribs);
    if (eglContext_ == EGL_NO_CONTEXT) {
        Log("eglCreateContext failed: 0x%x", eglGetError());
        return false;
    }

    const EGLint surfAttribs[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    eglPbuffer_ = eglCreatePbufferSurface(eglDisplay_, eglConfig_, surfAttribs);
    if (eglPbuffer_ == EGL_NO_SURFACE) {
        Log("eglCreatePbufferSurface failed: 0x%x", eglGetError());
        return false;
    }
    if (!eglMakeCurrent(eglDisplay_, eglPbuffer_, eglPbuffer_, eglContext_)) {
        Log("eglMakeCurrent failed: 0x%x", eglGetError());
        return false;
    }
    Log("EGL context ready: %s", reinterpret_cast<const char *>(glGetString(GL_VERSION)));
    return true;
}

void DestroyEGL() {
    if (eglDisplay_ == EGL_NO_DISPLAY)
        return;
    eglMakeCurrent(eglDisplay_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (eglPbuffer_ != EGL_NO_SURFACE)
        eglDestroySurface(eglDisplay_, eglPbuffer_);
    if (eglContext_ != EGL_NO_CONTEXT)
        eglDestroyContext(eglDisplay_, eglContext_);
    eglTerminate(eglDisplay_);
    eglPbuffer_ = EGL_NO_SURFACE;
    eglContext_ = EGL_NO_CONTEXT;
    eglDisplay_ = EGL_NO_DISPLAY;
}

// ---- OpenXR -------------------------------------------------------------------------

bool Init() {
    // Loader initialisation needs the JavaVM and the activity (via SDL's JNI glue)
    JNIEnv *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    JavaVM *vm = nullptr;
    env->GetJavaVM(&vm);
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    activityGlobalRef_ = env->NewGlobalRef(activity);
    env->DeleteLocalRef(activity);

    PFN_xrInitializeLoaderKHR pfnInitializeLoader = nullptr;
    xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
                          reinterpret_cast<PFN_xrVoidFunction *>(&pfnInitializeLoader));
    if (pfnInitializeLoader != nullptr) {
        XrLoaderInitInfoAndroidKHR loaderInfo{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
        loaderInfo.applicationVM = vm;
        loaderInfo.applicationContext = activityGlobalRef_;
        CheckXr(pfnInitializeLoader(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR *>(&loaderInfo)),
                "xrInitializeLoaderKHR");
    }

    uint32_t extCount = 0;
    xrEnumerateInstanceExtensionProperties(nullptr, 0, &extCount, nullptr);
    std::vector<XrExtensionProperties> exts(extCount, {XR_TYPE_EXTENSION_PROPERTIES});
    xrEnumerateInstanceExtensionProperties(nullptr, extCount, &extCount, exts.data());

    std::vector<const char *> enabled;
    for (const char *required : {XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME, XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME}) {
        if (!HasExtension(exts, required)) {
            Log("required OpenXR extension %s missing", required);
            return false;
        }
        enabled.push_back(required);
    }
    hasRefreshRateExt_ = HasExtension(exts, XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
    if (hasRefreshRateExt_)
        enabled.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);

    XrInstanceCreateInfoAndroidKHR androidInfo{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    androidInfo.applicationVM = vm;
    androidInfo.applicationActivity = activityGlobalRef_;

    XrInstanceCreateInfo instInfo{XR_TYPE_INSTANCE_CREATE_INFO};
    instInfo.next = &androidInfo;
    strncpy(instInfo.applicationInfo.applicationName, "Hurrican", XR_MAX_APPLICATION_NAME_SIZE - 1);
    instInfo.applicationInfo.applicationVersion = 1;
    strncpy(instInfo.applicationInfo.engineName, "Hurrican", XR_MAX_ENGINE_NAME_SIZE - 1);
    instInfo.applicationInfo.engineVersion = 1;
    instInfo.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    instInfo.enabledExtensionCount = static_cast<uint32_t>(enabled.size());
    instInfo.enabledExtensionNames = enabled.data();
    if (!CheckXr(xrCreateInstance(&instInfo, &xr.instance), "xrCreateInstance"))
        return false;

    XrInstanceProperties instProps{XR_TYPE_INSTANCE_PROPERTIES};
    xrGetInstanceProperties(xr.instance, &instProps);
    Log("runtime: %s %d.%d.%d", instProps.runtimeName, XR_VERSION_MAJOR(instProps.runtimeVersion),
        XR_VERSION_MINOR(instProps.runtimeVersion), XR_VERSION_PATCH(instProps.runtimeVersion));

    XrSystemGetInfo sysInfo{XR_TYPE_SYSTEM_GET_INFO};
    sysInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!CheckXr(xrGetSystem(xr.instance, &sysInfo, &xr.systemId), "xrGetSystem"))
        return false;

    PFN_xrGetOpenGLESGraphicsRequirementsKHR pfnGetReqs = nullptr;
    xrGetInstanceProcAddr(xr.instance, "xrGetOpenGLESGraphicsRequirementsKHR",
                          reinterpret_cast<PFN_xrVoidFunction *>(&pfnGetReqs));
    XrGraphicsRequirementsOpenGLESKHR reqs{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
    if (pfnGetReqs == nullptr || !CheckXr(pfnGetReqs(xr.instance, xr.systemId, &reqs), "xrGetOpenGLESGraphicsRequirementsKHR"))
        return false;

    if (hasRefreshRateExt_)
        xrGetInstanceProcAddr(xr.instance, "xrRequestDisplayRefreshRateFB",
                              reinterpret_cast<PFN_xrVoidFunction *>(&pfnRequestDisplayRefreshRateFB_));

    XrGraphicsBindingOpenGLESAndroidKHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    binding.display = eglDisplay_;
    binding.config = eglConfig_;
    binding.context = eglContext_;

    XrSessionCreateInfo sessInfo{XR_TYPE_SESSION_CREATE_INFO};
    sessInfo.next = &binding;
    sessInfo.systemId = xr.systemId;
    if (!CheckXr(xrCreateSession(xr.instance, &sessInfo, &xr.session), "xrCreateSession"))
        return false;

    XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    if (!CheckXr(xrCreateReferenceSpace(xr.session, &spaceInfo, &xr.localSpace), "xrCreateReferenceSpace(LOCAL)"))
        return false;
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    if (!CheckXr(xrCreateReferenceSpace(xr.session, &spaceInfo, &xr.viewSpace), "xrCreateReferenceSpace(VIEW)"))
        return false;

    uint32_t viewCount = 0;
    xrEnumerateViewConfigurationViews(xr.instance, xr.systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
                                      &viewCount, nullptr);
    if (viewCount != NUM_EYES) {
        Log("unexpected view count %u", viewCount);
        return false;
    }
    for (auto &v : viewConfigs_)
        v = {XR_TYPE_VIEW_CONFIGURATION_VIEW};
    xrEnumerateViewConfigurationViews(xr.instance, xr.systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, NUM_EYES,
                                      &viewCount, viewConfigs_);

    if (!CreateSwapchains())
        return false;

    if (!VRInput::Init())
        return false;

    for (auto &v : views_)
        v = {XR_TYPE_VIEW};
    screenModel_ = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, config.screenHeight, -config.screenDistance));
    Log("OpenXR ready");
    return true;
}

void Shutdown() {
    VRInput::Shutdown();
    DestroySwapchains();
    if (xr.viewSpace != XR_NULL_HANDLE)
        xrDestroySpace(xr.viewSpace);
    if (xr.localSpace != XR_NULL_HANDLE)
        xrDestroySpace(xr.localSpace);
    if (xr.session != XR_NULL_HANDLE)
        xrDestroySession(xr.session);
    if (xr.instance != XR_NULL_HANDLE)
        xrDestroyInstance(xr.instance);
    xr = XrState();
    if (activityGlobalRef_ != nullptr) {
        JNIEnv *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
        if (env != nullptr)
            env->DeleteGlobalRef(activityGlobalRef_);
        activityGlobalRef_ = nullptr;
    }
}

void PollEvents() {
    if (xr.instance == XR_NULL_HANDLE)
        return;
    XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(xr.instance, &ev) == XR_SUCCESS) {
        switch (ev.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
                HandleSessionStateChange(*reinterpret_cast<const XrEventDataSessionStateChanged *>(&ev));
                break;
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                Log("instance loss pending -> quitting");
                exitRequested_ = true;
                GameRunning = false;
                break;
            case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
                recenterPending_ = true;
                break;
            case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
                VRInput::OnInteractionProfileChanged();
                break;
            default:
                break;
        }
        ev = {XR_TYPE_EVENT_DATA_BUFFER};
    }
}

bool SessionRunning() {
    return xr.sessionRunning;
}

bool ExitRequested() {
    return exitRequested_;
}

bool FrameInProgress() {
    return frameInProgress_;
}

bool BeginFrame() {
    if (!xr.sessionRunning)
        return false;
    if (frameInProgress_)
        return true;

    XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
    frameState_ = {XR_TYPE_FRAME_STATE};
    if (!CheckXr(xrWaitFrame(xr.session, &waitInfo, &frameState_), "xrWaitFrame"))
        return false;
    XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    if (!CheckXr(xrBeginFrame(xr.session, &beginInfo), "xrBeginFrame"))
        return false;

    xr.predictedDisplayTime = frameState_.predictedDisplayTime;
    frameInProgress_ = true;
    viewsValid_ = false;
    return true;
}

bool LocateViews() {
    if (!frameInProgress_)
        return false;

    XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
    locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locateInfo.displayTime = xr.predictedDisplayTime;
    locateInfo.space = xr.localSpace;
    XrViewState viewState{XR_TYPE_VIEW_STATE};
    uint32_t count = 0;
    for (auto &v : views_)
        v = {XR_TYPE_VIEW};
    if (!CheckXr(xrLocateViews(xr.session, &locateInfo, &viewState, NUM_EYES, &count, views_), "xrLocateViews"))
        return false;
    viewsValid_ = (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
    if (viewsValid_ && recenterPending_)
        UpdateScreenPoseFromHead();
    return viewsValid_;
}

void EndFrame(bool rendered) {
    if (!frameInProgress_)
        return;
    frameInProgress_ = false;

    XrCompositionLayerProjectionView projViews[NUM_EYES];
    for (int eye = 0; eye < NUM_EYES; eye++) {
        projViews[eye] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
        projViews[eye].pose = views_[eye].pose;
        projViews[eye].fov = views_[eye].fov;
        projViews[eye].subImage.swapchain = swapchains_[eye].handle;
        projViews[eye].subImage.imageRect.offset = {0, 0};
        projViews[eye].subImage.imageRect.extent = {swapchains_[eye].width, swapchains_[eye].height};
        projViews[eye].subImage.imageArrayIndex = 0;
    }
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    layer.space = xr.localSpace;
    layer.viewCount = NUM_EYES;
    layer.views = projViews;
    const XrCompositionLayerBaseHeader *layers[] = {reinterpret_cast<const XrCompositionLayerBaseHeader *>(&layer)};

    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = xr.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = (rendered && viewsValid_) ? 1 : 0;
    endInfo.layers = layers;
    CheckXr(xrEndFrame(xr.session, &endInfo), "xrEndFrame");
}

int EyeCount() {
    return NUM_EYES;
}

void GetEyeMatrices(int eye, glm::mat4 &proj, glm::mat4 &view) {
    proj = ProjectionFromFov(views_[eye].fov, 0.05f, 100.0f);
    view = glm::inverse(PoseToMatrix(views_[eye].pose));
}

uint32_t AcquireEyeFramebuffer(int eye, int &width, int &height) {
    Swapchain &sc = swapchains_[eye];
    uint32_t index = 0;
    XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (!CheckXr(xrAcquireSwapchainImage(sc.handle, &acquireInfo, &index), "xrAcquireSwapchainImage"))
        return 0;
    XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    if (!CheckXr(xrWaitSwapchainImage(sc.handle, &waitInfo), "xrWaitSwapchainImage")) {
        XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        xrReleaseSwapchainImage(sc.handle, &releaseInfo);
        return 0;
    }
    sc.acquired = true;
    width = sc.width;
    height = sc.height;
    return sc.framebuffers[index];
}

void ReleaseEyeFramebuffer(int eye) {
    Swapchain &sc = swapchains_[eye];
    if (!sc.acquired)
        return;
    XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    CheckXr(xrReleaseSwapchainImage(sc.handle, &releaseInfo), "xrReleaseSwapchainImage");
    sc.acquired = false;
}

bool SwapchainIsSRGB() {
    return swapchainFormat_ == GL_SRGB8_ALPHA8;
}

void Recenter() {
    recenterPending_ = true;
}

const glm::mat4 &ScreenModel() {
    return screenModel_;
}

int64_t PredictedDisplayTime() {
    return xr.predictedDisplayTime;
}

}  // namespace VR
