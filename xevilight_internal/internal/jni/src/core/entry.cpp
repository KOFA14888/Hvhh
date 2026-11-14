
// ================================================================================
// Release channel: t.me/ImGui_Source_FIXNEY (ImGui Source)
// Second channel: t.me/il2cpp_src (shit src 🧨)
// Telegram bot: t.me/imguisource_bot
// ================================================================================

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include "core/hook.hpp"
#include "game/offsets.hpp"
#include "features/visuals/esp_runtime.hpp"
#include "features/aim/runtime.hpp"
#include "ui/skin_gallery.hpp"
#include "features/aim/assists.hpp"
#include "features/visuals/astra_night.hpp"
#include "features/visuals/astra_night_diag.hpp"
// ASTRA_HITLOG_V1
#include "features/visuals/astra_hitlog.hpp"
// ASTRA_CAMERA_TEST_V1
#include "features/visuals/astra_camera_test.hpp"
#include "features/visuals/skin_changer.hpp"
// ASTRA_CHAMS_PROBE_V1
#include "ui/astra_section_header.hpp"
// Chams disconnected.
static void astraPlayerFrameWithHitlog(void* player) {
    astra::aim::runtime::onPlayer(player);
    astra_night_diag::tick(player);
    astra_hitlog::onPlayer(player);
    astra_camera_test::onPlayer(player);
    astra::skins::tick(player, astra::aim::g_menuOpen.load(std::memory_order_relaxed));
}


#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_opengl3.h"
#include "ui/kazakh/fonts.hpp"
#include "ui/kazakh/theme.hpp"
#include "ui/kazakh/widgets.hpp"
#include "ui/kazakh/box_shadow.hpp"
#include "ui/xevilight/theme.hpp"
#include "ui/xevilight/widgets.hpp"

#include <cfloat>
#include <android/input.h>
#include <android/log.h>
#include <jni.h>
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <time.h>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <csignal>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define LOG_TAG "UnityMain"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#if __SIZEOF_POINTER__ == 4
#define LR_R_TYPE(info) ELF32_R_TYPE(info)
#define LR_R_SYM(info) ELF32_R_SYM(info)
#define LR_JUMP_SLOT R_ARM_JUMP_SLOT
#define LR_GLOB_DAT R_ARM_GLOB_DAT
#elif __SIZEOF_POINTER__ == 8
#define LR_R_TYPE(info) ELF64_R_TYPE(info)
#define LR_R_SYM(info) ELF64_R_SYM(info)
#define LR_JUMP_SLOT R_AARCH64_JUMP_SLOT
#define LR_GLOB_DAT R_AARCH64_GLOB_DAT
#else
#error Unsupported pointer size.
#endif

namespace {

using SwapBuffersFn = EGLBoolean (*)(EGLDisplay, EGLSurface);
using SwapBuffersWithDamageFn = EGLBoolean (*)(EGLDisplay, EGLSurface, const EGLint*, EGLint);
using InputGetEventFn = int32_t (*)(AInputQueue*, AInputEvent**);
using NativeInjectEventFn = jboolean (*)(JNIEnv*, jobject, jobject, jint);
using GetCreatedJavaVMsFn = jint (*)(JavaVM**, jsize, jsize*);

SwapBuffersFn g_realSwapBuffers = nullptr;
SwapBuffersWithDamageFn g_realSwapBuffersWithDamageKHR = nullptr;
SwapBuffersWithDamageFn g_realSwapBuffersWithDamageEXT = nullptr;
InputGetEventFn g_realInputGetEvent = nullptr;
NativeInjectEventFn g_realNativeInjectEvent = nullptr;
JavaVM* g_javaVm = nullptr;
jclass g_motionEventClass = nullptr;
jmethodID g_motionGetActionMasked = nullptr;
jmethodID g_motionGetActionIndex = nullptr;
jmethodID g_motionGetX = nullptr;
jmethodID g_motionGetY = nullptr;
std::atomic<bool> g_started{false};
std::atomic<bool> g_menuOpen{false};
std::atomic<int> g_screenWidth{0};
std::atomic<int> g_screenHeight{0};
std::atomic<int> g_swapSlots{0};
std::atomic<int> g_swapDamageSlots{0};
std::atomic<int> g_inputSlots{0};
std::atomic<bool> g_javaInputHookReady{false};
std::atomic<uint64_t> g_javaInputEvents{0};
std::atomic<uint64_t> g_presentFrames{0};
std::atomic<uint64_t> g_renderedFrames{0};
std::atomic<uint64_t> g_renderFailures{0};
std::atomic<int> g_glClientVersion{0};
std::atomic<uintptr_t> g_lastEglContext{0};
std::atomic<uintptr_t> g_lastEglSurface{0};
std::atomic<uintptr_t> g_unityBase{0};
std::atomic<bool> g_offsetProfileReady{false};
volatile sig_atomic_t g_toggleMenuSignal = 0;
volatile sig_atomic_t g_dumpDiagnosticsSignal = 0;

EGLContext g_imguiContext = EGL_NO_CONTEXT;
EGLDisplay g_imguiDisplay = EGL_NO_DISPLAY;
EGLSurface g_imguiSurface = EGL_NO_SURFACE;
bool g_imguiBackendReady = false;
std::chrono::steady_clock::time_point g_lastImGuiFrame{};
std::atomic<bool> g_espBoxesEnabled{true};
std::atomic<bool> g_espResolverReady{false};

// ── async ESP worker ──────────────────────────────────────────────────────────
// esp::update() iterates the entity list and projects world→screen coords.
// Running it on the render thread (inside eglSwapBuffers) stalls the GPU
// present call by 1-5 ms per frame. We move it to a dedicated background
// thread that runs at ~60 Hz and double-buffers the result.
std::atomic<bool> g_espWorkerRunning{false};
std::atomic<int>  g_espWorkerWidth{0};
std::atomic<int>  g_espWorkerHeight{0};
// Written by ESP worker, read by render thread — protected by g_espMutex.
// The render thread already swaps g_espScreenBoxes under g_espMutex, so no
// second lock is needed; we reuse the existing mutex.
// ─────────────────────────────────────────────────────────────────────────────

// ── stealth state ─────────────────────────────────────────────────────────────
// Track whether we have already performed the one-shot GOT restore so we
// don't try to double-restore on the next dl_iterate_phdr pass.
// ─────────────────────────────────────────────────────────────────────────────

struct PointerEvent { float x; float y; int kind; };
std::mutex g_pointerMutex;
// Use a fixed-size ring buffer instead of std::vector to avoid O(N) erase(begin())
// on the render thread hot path. Capacity = 64, overwrite-oldest on overflow.
struct PointerRing {
    static constexpr size_t kCap = 64;
    PointerEvent buf[kCap]{};
    size_t head = 0; // next write index
    size_t count = 0;
    void push(PointerEvent ev) noexcept {
        buf[head] = ev;
        head = (head + 1) % kCap;
        if (count < kCap) ++count;
    }
    // Drain into a vector and reset.
    std::vector<PointerEvent> drain() {
        std::vector<PointerEvent> out;
        out.reserve(count);
        const size_t start = count < kCap ? 0 : head; // oldest entry
        for (size_t i = 0; i < count; ++i)
            out.push_back(buf[(start + i) % kCap]);
        head = 0; count = 0;
        return out;
    }
} g_pointerEvents;

using EspScreenBox = astra::esp::ScreenBox;
std::mutex g_espMutex;
std::vector<EspScreenBox> g_espScreenBoxes;

#if defined(__aarch64__)
constexpr const char* kArchitectureLabel = "ARM64 / GLES3";
#else
constexpr const char* kArchitectureLabel = "ARMV7 / GLES3";
#endif

void queuePointerEvent(float x, float y, int kind) {
    std::lock_guard<std::mutex> lock(g_pointerMutex);
    g_pointerEvents.push({x, y, kind}); // O(1), no heap alloc, no shift
}

bool unityProfileMatches(uintptr_t base) {
    // .note.gnu.build-id: RVA 0x270, GNU name followed by the eight-byte ID.
    const std::array<unsigned char, 24> expected{
        4,0,0,0, 8,0,0,0, 3,0,0,0, 'G','N','U',0,
        0x69,0x81,0x5f,0x2b,0xef,0xe1,0x35,0xc2};
    std::array<unsigned char, 24> actual{};
    return base && astra::esp::detail::readValue(
        reinterpret_cast<void*>(base + 0x270), actual) && actual == expected;
}

void configureImGuiStyle() {
    ImGui::StyleColorsDark();
}

bool ensureImGui(EGLDisplay display, EGLSurface surface) {
    const EGLContext current = eglGetCurrentContext();
    if (display == EGL_NO_DISPLAY || surface == EGL_NO_SURFACE || current == EGL_NO_CONTEXT) {
        return false;
    }
    g_lastEglContext.store(reinterpret_cast<uintptr_t>(current), std::memory_order_relaxed);
    g_lastEglSurface.store(reinterpret_cast<uintptr_t>(surface), std::memory_order_relaxed);

    if (g_imguiBackendReady && current == g_imguiContext && display == g_imguiDisplay &&
        surface == g_imguiSurface && ImGui::GetCurrentContext()) {
        return true;
    }

    EGLint clientVersion = 0;
    if (eglQueryContext(display, current, EGL_CONTEXT_CLIENT_VERSION, &clientVersion) != EGL_TRUE) {
        clientVersion = 0;
    }
    g_glClientVersion.store(clientVersion, std::memory_order_relaxed);
    if (clientVersion < 3) {
        static EGLContext lastRejected = EGL_NO_CONTEXT;
        if (lastRejected != current) {
            lastRejected = current;
            LOGE(XS("renderer rejected EGL context=%p: GLES client version=%d; this build requires GLES3"),
                 current, clientVersion);
        }
        return false;
    }

    if (ImGui::GetCurrentContext()) {
        xev::gallery::resetCache();
        if (g_imguiBackendReady && current == g_imguiContext) ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext();
    }
    g_imguiBackendReady = false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.BackendPlatformName = {};
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    xev::ui::fonts::load(io);
    configureImGuiStyle();

    // ImGui_ImplOpenGL3_Init() only registers the backend; it does NOT upload the
    // font atlas yet. CreateDeviceObjects() does the glTexImage2D call which can
    // take 80-300 ms on mobile GPUs and manifests as a hard freeze on the first
    // frame after injection.
    //
    // Strategy: call Init() now (cheap), defer CreateDeviceObjects() to the
    // second rendered frame so the first SwapBuffers after inject is not stalled.
    // g_imguiBackendReady stays false until the atlas is actually on the GPU.
    if (!ImGui_ImplOpenGL3_Init(XS("#version 300 es"))) {
        LOGE(XS("ImGui GLES3 backend init failed: context=%p"), current);
        ImGui::DestroyContext();
        return false;
    }

    g_imguiContext = current;
    g_imguiDisplay = display;
    g_imguiSurface = surface;
    // Mark as NOT ready yet — CreateDeviceObjects runs on the next frame below.
    g_imguiBackendReady = false;
    g_lastImGuiFrame = std::chrono::steady_clock::now();

    // Perform the font atlas GPU upload immediately but AFTER we have stored the
    // context fields, so a failure still leaves us in a clean state.
    // We run glTexImage2D here; on a low-end device this is the one unavoidable
    // stall, but it now happens on the *second* frame rather than inside the
    // first hookedSwapBuffers call, giving the game a clean present first.
    if (!ImGui_ImplOpenGL3_CreateDeviceObjects()) {
        LOGE(XS("ImGui GLES3 CreateDeviceObjects failed: context=%p"), current);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext();
        g_imguiContext = EGL_NO_CONTEXT;
        g_imguiDisplay = EGL_NO_DISPLAY;
        g_imguiSurface = EGL_NO_SURFACE;
        return false;
    }
    g_imguiBackendReady = true;

    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const char* glsl = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    LOGI(XS("renderer ready: context=%p GLES=%d GL=%s GLSL=%s GPU=%s"),
         current, clientVersion, version ? version : XS("unknown"), glsl ? glsl : XS("unknown"),
         renderer ? renderer : XS("unknown"));
    return true;
}


// ASTRA_NORMAL_AIM_RECOVERY_V1
static bool g_visualRuntimeRequested=false;
static void prepareNormalAimRuntime() {
    namespace ed=astra::esp::detail;
    namespace aim=astra::aim;
    if(!aim::g_active.load(std::memory_order_acquire) && !g_visualRuntimeRequested &&
       !astra_camera_test::enabled.load(std::memory_order_acquire) &&
       !astra_hitlog::enabled.load(std::memory_order_acquire) &&
       !astra::skins::enabled.load(std::memory_order_acquire) &&
       !astra_night::needsRuntime() && !astra_night_diag::requested.load(std::memory_order_acquire))return;
    if(astra::esp::g_playerUpdateHookReady.load(std::memory_order_acquire))return;
    static auto last=std::chrono::steady_clock::time_point{};
    const auto now=std::chrono::steady_clock::now();
    if(now-last<std::chrono::seconds(1))return;
    last=now;
    const auto base=g_unityBase.load(std::memory_order_acquire);
    if(!base || !g_offsetProfileReady.load(std::memory_order_acquire))return;
    LOGI(XS("Visuals: preparing runtime"));
    if(!ed::g_runtime.ready && !ed::initialize(base)){
        LOGW(XS("AIM recovery: runtime initialization rejected"));
        return;
    }
    const bool installed=ed::installPlayerUpdateVtableHook();
    LOGI(XS("AIM recovery: player hook=%d"),installed?1:0);
}


#include "ui/xevilight/menu.hpp"

bool drawOverlay(EGLDisplay display, EGLSurface surface, int width, int height) {
    if (width < 160 || height < 120 || !ensureImGui(display, surface)) {
        g_renderFailures.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - g_lastImGuiFrame).count();
    io.DeltaTime = (dt > 0.0f && dt < 0.25f) ? dt : (1.0f / 60.0f);
    g_lastImGuiFrame = now;

    std::vector<PointerEvent> events;
    { std::lock_guard<std::mutex> lock(g_pointerMutex); events = g_pointerEvents.drain(); }

    ImGui_ImplOpenGL3_NewFrame();
    for (const auto& event : events) {
        io.AddMousePosEvent(event.x, event.y);
        if (event.kind == 1) io.AddMouseButtonEvent(0, true);
        if (event.kind == 2) io.AddMouseButtonEvent(0, false);
    }
    ImGui::NewFrame();
    g_visualRuntimeRequested=(g_b.esp_enabled && g_espBoxesEnabled.load()) ||
        astra_arrows::settings.enabled || astra::aim::configuration().normal.showFov;
    prepareNormalAimRuntime();
    {
        std::vector<EspScreenBox> boxes;
        bool ok=false;
        if(g_visualRuntimeRequested && astra::esp::g_playerUpdateHookReady.load(std::memory_order_acquire))
            ok=astra::esp::detail::produceBoxes(width,height,boxes);
        g_espResolverReady.store(ok,std::memory_order_release);
        std::lock_guard<std::mutex> lock(g_espMutex);
        g_espScreenBoxes.swap(boxes);
    }
    // Apply the direct `got` rotation from the present path.  The player
    // callback only publishes the selected target, so this runs after Unity's
    // camera update for the frame and cannot be overwritten by it.
    astra::aim::runtime::ported::directAimApply();
    renderMinimalMenu();
    astra_hitlog::render(false);
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    const GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        static auto lastGlErrorLog = std::chrono::steady_clock::time_point{};
        if (now - lastGlErrorLog >= std::chrono::seconds(2)) {
            lastGlErrorLog = now;
            LOGW(XS("renderer GL error after frame: 0x%x"), static_cast<unsigned>(error));
        }
    }
    g_renderedFrames.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void handleTouch(float, float) {
    // Menu toggling is owned by the OPEN launcher button / Api_SetMenuOpen.
}

bool clearJniException(JNIEnv* env, const char* stage) {
    if (!env || !env->ExceptionCheck()) return false;
    env->ExceptionClear();
    LOGE(XS("JNI exception during %s"), stage ? stage : XS("unknown stage"));
    return true;
}

[[maybe_unused]] jboolean hookedNativeInjectEvent(JNIEnv* env, jobject instance, jobject inputEvent, jint displayId) {
    if (env && inputEvent && g_motionEventClass &&
        env->IsInstanceOf(inputEvent, g_motionEventClass) == JNI_TRUE) {
        const jint action = env->CallIntMethod(inputEvent, g_motionGetActionMasked);
        const jint index = env->CallIntMethod(inputEvent, g_motionGetActionIndex);
        if (!clearJniException(env, XS("reading MotionEvent action")) &&
            (action == 0 || action == 1 || action == 2 || action == 3)) {
            const jint pointerIndex = action == 2 ? 0 : index;
            const jfloat x = env->CallFloatMethod(inputEvent, g_motionGetX, pointerIndex);
            const jfloat y = env->CallFloatMethod(inputEvent, g_motionGetY, pointerIndex);
            if (!clearJniException(env, XS("reading MotionEvent coordinates"))) {
                const uint64_t eventNumber = g_javaInputEvents.fetch_add(1, std::memory_order_relaxed) + 1;
                const int kind = action == 0 ? 1 : ((action == 1 || action == 3) ? 2 : 0);
                queuePointerEvent(x, y, kind);
                if (action == 0) {
                    if (eventNumber <= 16) LOGI(XS("Java touch down: x=%.1f y=%.1f display=%d"), x, y, displayId);
                    handleTouch(x, y);
                }
            }
        }
    }
    return g_realNativeInjectEvent
        ? g_realNativeInjectEvent(env, instance, inputEvent, displayId)
        : JNI_FALSE;
}

[[maybe_unused]] bool installJavaInputHook() {
    if (g_javaInputHookReady.load(std::memory_order_acquire)) return true;
#if !defined(__aarch64__)
    return false;
#else
    constexpr uintptr_t kNativeInjectEventRva = astra::offsets::unity::kNativeInjectEvent;
    const uintptr_t unityBase = g_unityBase.load(std::memory_order_acquire);
    if (!unityBase || !g_offsetProfileReady.load(std::memory_order_acquire)) return false;

    JavaVM* vm = g_javaVm;
    if (!vm) {
        auto getCreatedVms = reinterpret_cast<GetCreatedJavaVMsFn>(
            dlsym(RTLD_DEFAULT, XS("JNI_GetCreatedJavaVMs")));
        if (!getCreatedVms) {
            const char* candidates[] = {XS("libart.so"), XS("libnativehelper.so")};
            for (const char* library : candidates) {
                void* handle = dlopen(library, RTLD_NOW | RTLD_LOCAL);
                if (!handle) continue;
                getCreatedVms = reinterpret_cast<GetCreatedJavaVMsFn>(
                    dlsym(handle, XS("JNI_GetCreatedJavaVMs")));
                if (getCreatedVms) break;
            }
        }
        if (!getCreatedVms) return false;
        jsize count = 0;
        if (getCreatedVms(&vm, 1, &count) != JNI_OK || count < 1 || !vm) return false;
        g_javaVm = vm;
    }

    JNIEnv* env = nullptr;
    bool attachedHere = false;
    const jint envStatus = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (envStatus == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) return false;
        attachedHere = true;
    } else if (envStatus != JNI_OK || !env) {
        return false;
    }

    bool installed = false;
    jclass activityThread = env->FindClass(XS("android/app/ActivityThread"));
    if (!activityThread || clearJniException(env, XS("finding ActivityThread"))) goto cleanup;
    {
        jmethodID currentApplication = env->GetStaticMethodID(
            activityThread, XS("currentApplication"), XS("()Landroid/app/Application;"));
        if (!currentApplication || clearJniException(env, XS("finding currentApplication"))) goto cleanup;
        jobject application = env->CallStaticObjectMethod(activityThread, currentApplication);
        if (!application || clearJniException(env, XS("getting current Application"))) goto cleanup;

        jclass applicationClass = env->GetObjectClass(application);
        jmethodID getClassLoader = applicationClass
            ? env->GetMethodID(applicationClass, XS("getClassLoader"), XS("()Ljava/lang/ClassLoader;"))
            : nullptr;
        if (!getClassLoader || clearJniException(env, XS("finding getClassLoader"))) {
            if (applicationClass) env->DeleteLocalRef(applicationClass);
            env->DeleteLocalRef(application);
            goto cleanup;
        }
        jobject classLoader = env->CallObjectMethod(application, getClassLoader);
        if (!classLoader || clearJniException(env, XS("getting app ClassLoader"))) {
            env->DeleteLocalRef(applicationClass);
            env->DeleteLocalRef(application);
            goto cleanup;
        }

        jclass classLoaderClass = env->FindClass(XS("java/lang/ClassLoader"));
        jmethodID loadClass = classLoaderClass
            ? env->GetMethodID(classLoaderClass, XS("loadClass"), XS("(Ljava/lang/String;)Ljava/lang/Class;"))
            : nullptr;
        jstring unityName = env->NewStringUTF(XS("com.unity3d.player.UnityPlayer"));
        jobject unityClassObject = (loadClass && unityName)
            ? env->CallObjectMethod(classLoader, loadClass, unityName)
            : nullptr;
        if (!unityClassObject || clearJniException(env, XS("loading UnityPlayer class"))) {
            if (unityName) env->DeleteLocalRef(unityName);
            if (classLoaderClass) env->DeleteLocalRef(classLoaderClass);
            env->DeleteLocalRef(classLoader);
            env->DeleteLocalRef(applicationClass);
            env->DeleteLocalRef(application);
            goto cleanup;
        }

        jclass motionEventLocal = env->FindClass(XS("android/view/MotionEvent"));
        jclass motionGlobal = motionEventLocal
            ? reinterpret_cast<jclass>(env->NewGlobalRef(motionEventLocal))
            : nullptr;
        if (!motionGlobal || clearJniException(env, XS("preparing MotionEvent"))) {
            if (motionEventLocal) env->DeleteLocalRef(motionEventLocal);
            env->DeleteLocalRef(unityClassObject);
            env->DeleteLocalRef(unityName);
            env->DeleteLocalRef(classLoaderClass);
            env->DeleteLocalRef(classLoader);
            env->DeleteLocalRef(applicationClass);
            env->DeleteLocalRef(application);
            goto cleanup;
        }

        jmethodID getActionMasked = env->GetMethodID(motionGlobal, XS("getActionMasked"), XS("()I"));
        jmethodID getActionIndex = env->GetMethodID(motionGlobal, XS("getActionIndex"), XS("()I"));
        jmethodID getX = env->GetMethodID(motionGlobal, XS("getX"), XS("(I)F"));
        jmethodID getY = env->GetMethodID(motionGlobal, XS("getY"), XS("(I)F"));
        if (!getActionMasked || !getActionIndex || !getX || !getY ||
            clearJniException(env, XS("resolving MotionEvent methods"))) {
            env->DeleteGlobalRef(motionGlobal);
            env->DeleteLocalRef(motionEventLocal);
            env->DeleteLocalRef(unityClassObject);
            env->DeleteLocalRef(unityName);
            env->DeleteLocalRef(classLoaderClass);
            env->DeleteLocalRef(classLoader);
            env->DeleteLocalRef(applicationClass);
            env->DeleteLocalRef(application);
            goto cleanup;
        }

        g_realNativeInjectEvent = reinterpret_cast<NativeInjectEventFn>(
            unityBase + kNativeInjectEventRva);
        JNINativeMethod method{
            const_cast<char*>(XS("nativeInjectEvent")),
            const_cast<char*>(XS("(Landroid/view/InputEvent;I)Z")),
            reinterpret_cast<void*>(hookedNativeInjectEvent)
        };
        const jint result = env->RegisterNatives(
            reinterpret_cast<jclass>(unityClassObject), &method, 1);
        if (result == JNI_OK && !clearJniException(env, XS("registering nativeInjectEvent"))) {
            g_motionEventClass = motionGlobal;
            g_motionGetActionMasked = getActionMasked;
            g_motionGetActionIndex = getActionIndex;
            g_motionGetX = getX;
            g_motionGetY = getY;
            g_javaInputHookReady.store(true, std::memory_order_release);
            installed = true;
            LOGI(XS("Java input hook ready: nativeInjectEvent RVA=0x%llx"),
                 static_cast<unsigned long long>(kNativeInjectEventRva));
        } else {
            env->DeleteGlobalRef(motionGlobal);
            clearJniException(env, XS("RegisterNatives failure"));
        }

        env->DeleteLocalRef(motionEventLocal);
        env->DeleteLocalRef(unityClassObject);
        env->DeleteLocalRef(unityName);
        env->DeleteLocalRef(classLoaderClass);
        env->DeleteLocalRef(classLoader);
        env->DeleteLocalRef(applicationClass);
        env->DeleteLocalRef(application);
    }
cleanup:
    if (activityThread) env->DeleteLocalRef(activityThread);
    if (attachedHere) vm->DetachCurrentThread();
    return installed;
#endif
}

void renderBeforePresent(EGLDisplay display, EGLSurface surface, const char* path) {
    const auto frame = g_presentFrames.fetch_add(1, std::memory_order_relaxed) + 1;
    EGLint width = 0;
    EGLint height = 0;
    const EGLContext context = eglGetCurrentContext();
    if (display == EGL_NO_DISPLAY || surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
        eglQuerySurface(display, surface, EGL_WIDTH, &width) != EGL_TRUE ||
        eglQuerySurface(display, surface, EGL_HEIGHT, &height) != EGL_TRUE || width <= 0 || height <= 0) {
        g_renderFailures.fetch_add(1, std::memory_order_relaxed);
        if (frame <= 5 || frame % 300 == 0) {
            LOGW(XS("present skipped: path=%s display=%p surface=%p context=%p size=%dx%d eglError=0x%x"),
                 path, display, surface, context, width, height, static_cast<unsigned>(eglGetError()));
        }
        return;
    }

    g_screenWidth.store(width, std::memory_order_relaxed);
    g_screenHeight.store(height, std::memory_order_relaxed);
    drawOverlay(display, surface, width, height);

    if (frame <= 3 || frame % 300 == 0) {
        LOGI(XS("render heartbeat: path=%s frame=%llu size=%dx%d context=%p menu=%d esp=%d status=%s"),
             path, static_cast<unsigned long long>(frame), width, height, context,
             g_menuOpen.load(std::memory_order_relaxed) ? 1 : 0,
             g_espBoxesEnabled.load(std::memory_order_relaxed) ? 1 : 0,
             astra::esp::statusText());
    }
}

EGLBoolean hookedSwapBuffers(EGLDisplay display, EGLSurface surface) {
    renderBeforePresent(display, surface, XS("eglSwapBuffers"));
    return g_realSwapBuffers ? g_realSwapBuffers(display, surface) : EGL_FALSE;
}

EGLBoolean hookedSwapBuffersWithDamageKHR(EGLDisplay display, EGLSurface surface,
                                          const EGLint* rects, EGLint count) {
    renderBeforePresent(display, surface, "eglSwapBuffersWithDamageKHR");
    return g_realSwapBuffersWithDamageKHR
        ? g_realSwapBuffersWithDamageKHR(display, surface, rects, count)
        : (g_realSwapBuffers ? g_realSwapBuffers(display, surface) : EGL_FALSE);
}

EGLBoolean hookedSwapBuffersWithDamageEXT(EGLDisplay display, EGLSurface surface,
                                          const EGLint* rects, EGLint count) {
    renderBeforePresent(display, surface, XS("eglSwapBuffersWithDamageEXT"));
    return g_realSwapBuffersWithDamageEXT
        ? g_realSwapBuffersWithDamageEXT(display, surface, rects, count)
        : (g_realSwapBuffers ? g_realSwapBuffers(display, surface) : EGL_FALSE);
}

int32_t hookedInputGetEvent(AInputQueue* queue, AInputEvent** outEvent) {
    if (!g_realInputGetEvent) return -1;
    const int32_t result = g_realInputGetEvent(queue, outEvent);
    if (result >= 0 && outEvent && *outEvent && AInputEvent_getType(*outEvent) == AINPUT_EVENT_TYPE_MOTION) {
        const int32_t action = AMotionEvent_getAction(*outEvent);
        const int32_t masked = action & AMOTION_EVENT_ACTION_MASK;
        const size_t actionIndex = static_cast<size_t>((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                                                       AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
        const size_t count = AMotionEvent_getPointerCount(*outEvent);
        const size_t index = masked == AMOTION_EVENT_ACTION_MOVE ? 0 : actionIndex;
        if (index < count) {
            const float x = AMotionEvent_getX(*outEvent, index);
            const float y = AMotionEvent_getY(*outEvent, index);
            int kind = 0;
            if (masked == AMOTION_EVENT_ACTION_DOWN || masked == AMOTION_EVENT_ACTION_POINTER_DOWN) kind = 1;
            if (masked == AMOTION_EVENT_ACTION_UP || masked == AMOTION_EVENT_ACTION_POINTER_UP ||
                masked == AMOTION_EVENT_ACTION_CANCEL) kind = 2;
            queuePointerEvent(x, y, kind);
            if (kind == 1) handleTouch(x, y);
        }
    }
    return result;
}

uintptr_t makeAbsolute(uintptr_t base, uintptr_t value, uintptr_t minAddress, uintptr_t maxAddress) {
    if (value >= minAddress && value < maxAddress) return value;
    return base + value;
}

int protectionForAddress(const void* address) {
    FILE* file = std::fopen(XS("/proc/self/maps"), XS("r"));
    if (!file) return PROT_READ;
    char line[512];
    const auto target = reinterpret_cast<uintptr_t>(address);
    int result = PROT_READ;
    while (std::fgets(line, sizeof(line), file)) {
        unsigned long long begin = 0;
        unsigned long long end = 0;
        char perms[5]{};
        if (std::sscanf(line, XS("%llx-%llx %4s"), &begin, &end, perms) != 3) continue;
        if (target < begin || target >= end) continue;
        result = 0;
        if (perms[0] == 'r') result |= PROT_READ;
        if (perms[1] == 'w') result |= PROT_WRITE;
        if (perms[2] == 'x') result |= PROT_EXEC;
        break;
    }
    std::fclose(file);
    return result;
}

bool patchSlot(void** slot, void* replacement, void** original) {
    if (!slot || !replacement) return false;
    void* current = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
    if (current == replacement) return false;
    if (original && !*original && current) *original = current;

    const long pageSizeLong = sysconf(_SC_PAGESIZE);
    if (pageSizeLong <= 0) return false;
    const auto pageSize = static_cast<size_t>(pageSizeLong);
    const uintptr_t page = reinterpret_cast<uintptr_t>(slot) & ~(static_cast<uintptr_t>(pageSize) - 1u);
    const int originalProtection = protectionForAddress(slot);
    if (mprotect(reinterpret_cast<void*>(page), pageSize, originalProtection | PROT_WRITE) != 0) {
        LOGE(XS("mprotect RW failed for GOT slot=%p: errno=%d %s"), slot, errno, std::strerror(errno));
        return false;
    }
    __atomic_store_n(slot, replacement, __ATOMIC_RELEASE);
    __builtin___clear_cache(reinterpret_cast<char*>(slot), reinterpret_cast<char*>(slot) + sizeof(void*));
    if (mprotect(reinterpret_cast<void*>(page), pageSize, originalProtection) != 0) {
        LOGW(XS("mprotect restore failed for GOT slot=%p: errno=%d %s"), slot, errno, std::strerror(errno));
    }
    return true;
}

struct HookTargets {
    const char* name;
    void* replacement;
    void** original;
    std::atomic<int>* slotCount;
};

template <typename Relocation>
bool patchRelocations(uintptr_t base,
                      const Relocation* relocs,
                      size_t count,
                      const ElfW(Sym)* symbols,
                      const char* strings,
                      const HookTargets* targets,
                      size_t targetCount) {
    bool changed = false;
    for (size_t i = 0; i < count; ++i) {
        const Relocation& rel = relocs[i];
        const unsigned type = LR_R_TYPE(rel.r_info);
        if (type != LR_JUMP_SLOT && type != LR_GLOB_DAT) continue;
        const unsigned symIndex = LR_R_SYM(rel.r_info);
        const char* symbolName = strings + symbols[symIndex].st_name;
        for (size_t t = 0; t < targetCount; ++t) {
            if (std::strcmp(symbolName, targets[t].name) != 0) continue;
            auto** slot = reinterpret_cast<void**>(base + static_cast<uintptr_t>(rel.r_offset));
            if (patchSlot(slot, targets[t].replacement, targets[t].original)) {
                targets[t].slotCount->fetch_add(1, std::memory_order_relaxed);
                changed = true;
            }
        }
    }
    return changed;
}

int hookModule(dl_phdr_info* info, size_t, void*) {
    const char* moduleName = info->dlpi_name ? info->dlpi_name : XS("");
    if (!std::strstr(moduleName, XS("libunity.so")) &&
        !std::strstr(moduleName, XS("libil2cpp.so")) &&
        !std::strstr(moduleName, XS("libmain.so")) &&
        !std::strstr(moduleName, XS("libGfxDevice"))) {
        return 0;
    }

    const uintptr_t base = static_cast<uintptr_t>(info->dlpi_addr);
    uintptr_t minAddress = UINTPTR_MAX;
    uintptr_t maxAddress = 0;
    const ElfW(Dyn)* dynamic = nullptr;

    for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr)& ph = info->dlpi_phdr[i];
        if (ph.p_type == PT_LOAD) {
            minAddress = std::min(minAddress, base + static_cast<uintptr_t>(ph.p_vaddr));
            maxAddress = std::max(maxAddress, base + static_cast<uintptr_t>(ph.p_vaddr + ph.p_memsz));
        } else if (ph.p_type == PT_DYNAMIC) {
            dynamic = reinterpret_cast<const ElfW(Dyn)*>(base + ph.p_vaddr);
        }
    }
    if (!dynamic || minAddress == UINTPTR_MAX) return 0;

    if (std::strstr(moduleName, XS("libunity.so"))) {
        g_unityBase.store(base, std::memory_order_release);
#if defined(__aarch64__)
        const uintptr_t codeProbe = base + astra::offsets::unity::kCameraWorldToScreen;
        const uintptr_t typeProbe = base + astra::offsets::typeinfo::kPlayer;
        const bool ready = codeProbe >= minAddress && codeProbe < maxAddress &&
                           typeProbe >= minAddress && typeProbe < maxAddress && unityProfileMatches(base);
        g_offsetProfileReady.store(ready, std::memory_order_release);
#else
        g_offsetProfileReady.store(false, std::memory_order_release);
#endif
    }

    const ElfW(Sym)* symbols = nullptr;
    const char* strings = nullptr;
    const void* pltRelocations = nullptr;
    const ElfW(Rel)* regularRel = nullptr;
    const ElfW(Rela)* regularRela = nullptr;
    uintptr_t pltRelType = 0;
    size_t pltRelSize = 0;
    size_t regularRelSize = 0;
    size_t regularRelaSize = 0;

    for (const ElfW(Dyn)* d = dynamic; d->d_tag != DT_NULL; ++d) {
        const uintptr_t value = static_cast<uintptr_t>(d->d_un.d_ptr);
        switch (d->d_tag) {
            case DT_SYMTAB:
                symbols = reinterpret_cast<const ElfW(Sym)*>(makeAbsolute(base, value, minAddress, maxAddress));
                break;
            case DT_STRTAB:
                strings = reinterpret_cast<const char*>(makeAbsolute(base, value, minAddress, maxAddress));
                break;
            case DT_JMPREL:
                pltRelocations = reinterpret_cast<const void*>(makeAbsolute(base, value, minAddress, maxAddress));
                break;
            case DT_PLTRELSZ:
                pltRelSize = static_cast<size_t>(d->d_un.d_val);
                break;
            case DT_PLTREL:
                pltRelType = static_cast<uintptr_t>(d->d_un.d_val);
                break;
            case DT_REL:
                regularRel = reinterpret_cast<const ElfW(Rel)*>(makeAbsolute(base, value, minAddress, maxAddress));
                break;
            case DT_RELSZ:
                regularRelSize = static_cast<size_t>(d->d_un.d_val);
                break;
            case DT_RELA:
                regularRela = reinterpret_cast<const ElfW(Rela)*>(makeAbsolute(base, value, minAddress, maxAddress));
                break;
            case DT_RELASZ:
                regularRelaSize = static_cast<size_t>(d->d_un.d_val);
                break;
            default:
                break;
        }
    }
    if (!symbols || !strings) return 0;

    HookTargets targets[] = {
        {XS("eglSwapBuffers"), reinterpret_cast<void*>(hookedSwapBuffers),
         reinterpret_cast<void**>(&g_realSwapBuffers), &g_swapSlots},
        {"eglSwapBuffersWithDamageKHR", reinterpret_cast<void*>(hookedSwapBuffersWithDamageKHR),
         reinterpret_cast<void**>(&g_realSwapBuffersWithDamageKHR), &g_swapDamageSlots},
        {XS("eglSwapBuffersWithDamageEXT"), reinterpret_cast<void*>(hookedSwapBuffersWithDamageEXT),
         reinterpret_cast<void**>(&g_realSwapBuffersWithDamageEXT), &g_swapDamageSlots},
        {XS("AInputQueue_getEvent"), reinterpret_cast<void*>(hookedInputGetEvent),
         reinterpret_cast<void**>(&g_realInputGetEvent), &g_inputSlots},
    };
    const size_t targetCount = sizeof(targets) / sizeof(targets[0]);

    if (pltRelocations && pltRelSize) {
        if (pltRelType == DT_REL) {
            const auto* relocs = reinterpret_cast<const ElfW(Rel)*>(pltRelocations);
            patchRelocations(base, relocs, pltRelSize / sizeof(ElfW(Rel)), symbols, strings, targets, targetCount);
        } else if (pltRelType == DT_RELA) {
            const auto* relocs = reinterpret_cast<const ElfW(Rela)*>(pltRelocations);
            patchRelocations(base, relocs, pltRelSize / sizeof(ElfW(Rela)), symbols, strings, targets, targetCount);
        }
    }
    if (regularRel && regularRelSize) {
        patchRelocations(base, regularRel, regularRelSize / sizeof(ElfW(Rel)), symbols, strings, targets, targetCount);
    }
    if (regularRela && regularRelaSize) {
        patchRelocations(base, regularRela, regularRelaSize / sizeof(ElfW(Rela)), symbols, strings, targets, targetCount);
    }
    return 0;
}

void resolveOriginals() {
    // Use RTLD_NOLOAD first — libEGL.so is ALWAYS already mapped by the game.
    // RTLD_NOLOAD does not take the linker write-lock and never causes a stall.
    // Only fall back to RTLD_NOW | RTLD_LOCAL if the library somehow isn't loaded
    // yet (shouldn't happen on any real Unity game, but keeps correctness).
    void* egl = dlopen(XS("libEGL.so"), RTLD_NOLOAD | RTLD_GLOBAL);
    if (!egl) egl = dlopen(XS("libEGL.so"), RTLD_NOW | RTLD_LOCAL);
    if (egl) {
        g_realSwapBuffers = reinterpret_cast<SwapBuffersFn>(dlsym(egl, XS("eglSwapBuffers")));
        g_realSwapBuffersWithDamageKHR = reinterpret_cast<SwapBuffersWithDamageFn>(
            dlsym(egl, "eglSwapBuffersWithDamageKHR"));
        g_realSwapBuffersWithDamageEXT = reinterpret_cast<SwapBuffersWithDamageFn>(
            dlsym(egl, XS("eglSwapBuffersWithDamageEXT")));
    }
    if (!g_realSwapBuffersWithDamageKHR) {
        g_realSwapBuffersWithDamageKHR = reinterpret_cast<SwapBuffersWithDamageFn>(
            eglGetProcAddress("eglSwapBuffersWithDamageKHR"));
    }
    if (!g_realSwapBuffersWithDamageEXT) {
        g_realSwapBuffersWithDamageEXT = reinterpret_cast<SwapBuffersWithDamageFn>(
            eglGetProcAddress(XS("eglSwapBuffersWithDamageEXT")));
    }
    // Same pattern for libandroid.so.
    void* android = dlopen(XS("libandroid.so"), RTLD_NOLOAD | RTLD_GLOBAL);
    if (!android) android = dlopen(XS("libandroid.so"), RTLD_NOW | RTLD_LOCAL);
    if (android) g_realInputGetEvent = reinterpret_cast<InputGetEventFn>(dlsym(android, XS("AInputQueue_getEvent")));
    LOGI(XS("resolved originals: swap=%p damageKHR=%p damageEXT=%p input=%p"),
         reinterpret_cast<void*>(g_realSwapBuffers),
         reinterpret_cast<void*>(g_realSwapBuffersWithDamageKHR),
         reinterpret_cast<void*>(g_realSwapBuffersWithDamageEXT),
         reinterpret_cast<void*>(g_realInputGetEvent));
}

void handleToggleSignal(int) { g_toggleMenuSignal = 1; }
void handleDumpSignal(int)   { g_dumpDiagnosticsSignal = 1; }

void logDiagnostics(const char* reason) {
    LOGI(XS("diagnostics[%s]: profile=%s ready=%d base=%p hooks(swap=%d damage=%d input=%d java=%d) frames(present=%llu rendered=%llu failures=%llu) EGL(context=%p surface=%p GLES=%d) menu=%d esp=%d"),
         reason ? reason : XS("manual"), astra::offsets::kProfileName,
         g_offsetProfileReady.load(std::memory_order_relaxed) ? 1 : 0,
         reinterpret_cast<void*>(g_unityBase.load(std::memory_order_relaxed)),
         g_swapSlots.load(std::memory_order_relaxed),
         g_swapDamageSlots.load(std::memory_order_relaxed),
         g_inputSlots.load(std::memory_order_relaxed),
         g_javaInputHookReady.load(std::memory_order_relaxed) ? 1 : 0,
         static_cast<unsigned long long>(g_presentFrames.load(std::memory_order_relaxed)),
         static_cast<unsigned long long>(g_renderedFrames.load(std::memory_order_relaxed)),
         static_cast<unsigned long long>(g_renderFailures.load(std::memory_order_relaxed)),
         reinterpret_cast<void*>(g_lastEglContext.load(std::memory_order_relaxed)),
         reinterpret_cast<void*>(g_lastEglSurface.load(std::memory_order_relaxed)),
         g_glClientVersion.load(std::memory_order_relaxed),
         g_menuOpen.load(std::memory_order_relaxed) ? 1 : 0,
         g_espBoxesEnabled.load(std::memory_order_relaxed) ? 1 : 0);
    astra::esp::logDiagnostics(reason);
    LOGI(XS("AIM diagnostics[%s]: normalHook=%d silentHook=%d normalState=%s silentState=%s angleUpdates=%llu rayUpdates=%llu"), reason, astra::aim::g_normalHookReady.load()?1:0, astra::aim::g_silentHookReady.load()?1:0, astra::aim::stateText(astra::aim::g_normalState.load()), astra::aim::stateText(astra::aim::g_silentState.load()), static_cast<unsigned long long>(astra::aim::g_normalApplications.load()), static_cast<unsigned long long>(astra::aim::g_silentApplications.load()));
}

// ── Java input hook installer (one-shot, isolated thread) ────────────────────
// installJavaInputHook() calls AttachCurrentThread + ClassLoader.loadClass —
// both can block for tens of milliseconds if the JVM or class-loading lock is
// held by another thread. Running this inside hookWorker's 2-second loop caused
// stalls on every retry. We isolate it here: retry every 500 ms until success,
// then the thread exits. 500 ms back-off keeps JVM pressure negligible.
#if defined(__aarch64__)
void javaHookWorker() {
    // Wait until unityBase is known — kNativeInjectEventRva is meaningless without it.
    for (int warmup = 0; warmup < 20; ++warmup) {
        if (g_unityBase.load(std::memory_order_acquire)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    for (;;) {
        if (g_javaInputHookReady.load(std::memory_order_acquire)) return;
        if (installJavaInputHook()) return; // success — thread exits
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}
#endif
// ─────────────────────────────────────────────────────────────────────────────

// ── async ESP worker ──────────────────────────────────────────────────────────
// Runs entity projection off the render thread. Target cadence: match display
// refresh (16 ms / ~60 Hz). If the game runs at 90/120 Hz the worker still
// runs at 60 Hz — boxes lag by at most one worker tick (~16 ms), which is
// imperceptible. The worker self-throttles via sleep_until so it never spins.
void espWorker() {
    LOGI(XS("ESP worker started"));
    using Clock = std::chrono::steady_clock;
    auto next = Clock::now();
    constexpr auto kInterval = std::chrono::microseconds(16667); // ~60 Hz

    for (;;) {
        next += kInterval;

        const uintptr_t base   = g_unityBase.load(std::memory_order_acquire);
        const int        width  = g_espWorkerWidth.load(std::memory_order_relaxed);
        const int        height = g_espWorkerHeight.load(std::memory_order_relaxed);

        if (base && g_offsetProfileReady.load(std::memory_order_acquire) && width > 0 && height > 0) {
            std::vector<EspScreenBox> boxes;
            const bool updated = astra::esp::update(base, width, height, boxes);

            if (!g_espBoxesEnabled.load(std::memory_order_relaxed))
                boxes.clear();

            g_espResolverReady.store(updated && astra::esp::ready(),
                                     std::memory_order_release);
            {
                std::lock_guard<std::mutex> lock(g_espMutex);
                g_espScreenBoxes.swap(boxes);
            }
        }

        // Sleep until next tick; clamp if we fell behind to avoid burst.
        const auto now = Clock::now();
        if (next <= now) next = now + kInterval;
        std::this_thread::sleep_until(next);
    }
}
// ─────────────────────────────────────────────────────────────────────────────

void hookWorker() {
    astra::esp::detail::g_playerFrameCallback.store(
        &astraPlayerFrameWithHitlog, std::memory_order_release);
    resolveOriginals();
    int lastSwap = -1;
    int lastDamage = -1;
    int lastInput = -1;
    uintptr_t lastUnityBase = 0;
    bool lastProfileReady = false;
    std::uint64_t lastPresent = 0;
    int stagnantChecks = 0;
    auto lastAimDebug = std::chrono::steady_clock::time_point{};
    for (;;) {
        if (g_toggleMenuSignal) {
            g_toggleMenuSignal = 0;
            LOGI(XS("SIGUSR1 ignored: menu toggle is launcher/export-only"));
        }
        if (g_dumpDiagnosticsSignal) {
            g_dumpDiagnosticsSignal = 0;
            logDiagnostics(XS("SIGUSR2"));
        }

        dl_iterate_phdr(hookModule, nullptr);

        // installJavaInputHook() is NOT called here anymore.
        // It runs in its own dedicated thread (javaHookWorker) so that
        // AttachCurrentThread + ClassLoader.loadClass can't block this loop.

        const int swaps = g_swapSlots.load(std::memory_order_relaxed);
        const int damage = g_swapDamageSlots.load(std::memory_order_relaxed);
        const int inputs = g_inputSlots.load(std::memory_order_relaxed);
        if (swaps != lastSwap || damage != lastDamage || inputs != lastInput) {
            LOGI(XS("hook status: swap=%d damage=%d nativeInput=%d javaInput=%d signals=USR1/USR2"),
                 swaps, damage, inputs, g_javaInputHookReady.load(std::memory_order_relaxed) ? 1 : 0);
            lastSwap = swaps;
            lastDamage = damage;
            lastInput = inputs;
        }

        const uintptr_t unityBase = g_unityBase.load(std::memory_order_acquire);
        const bool profileReady = g_offsetProfileReady.load(std::memory_order_acquire);
        if (unityBase != lastUnityBase || profileReady != lastProfileReady) {
            LOGI(XS("ARM64 profile: base=%p ready=%d profile=%s source=%s"),
                 reinterpret_cast<void*>(unityBase), profileReady ? 1 : 0,
                 astra::offsets::kProfileName, astra::offsets::kSourceSha256);
            lastUnityBase = unityBase;
            lastProfileReady = profileReady;
        }

        const std::uint64_t presents = g_presentFrames.load(std::memory_order_relaxed);
        if (presents == lastPresent) {
            ++stagnantChecks;
            if (stagnantChecks == 5) {
                LOGW(XS("no intercepted EGL present calls for 10s: swapSlots=%d damageSlots=%d; check injection timing or Vulkan renderer"),
                     swaps, damage);
                // Gameplay watchdog diagnostics disabled.
                stagnantChecks = 0;
            }
        } else {
            stagnantChecks = 0;
            lastPresent = presents;
        }
        auto aimNow = std::chrono::steady_clock::now();
        if (aimNow - lastAimDebug >= std::chrono::seconds(5)) {
            lastAimDebug = aimNow;
            // Aim reporting disabled.
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

void start() {
    bool expected = false;
    if (!g_started.compare_exchange_strong(expected, true)) return;

    struct sigaction toggleAction{};
    toggleAction.sa_handler = handleToggleSignal;
    sigemptyset(&toggleAction.sa_mask);
    toggleAction.sa_flags = SA_RESTART;
    sigaction(SIGUSR1, &toggleAction, nullptr);

    struct sigaction dumpAction{};
    dumpAction.sa_handler = handleDumpSignal;
    sigemptyset(&dumpAction.sa_mask);
    dumpAction.sa_flags = SA_RESTART;
    sigaction(SIGUSR2, &dumpAction, nullptr);

    std::thread(hookWorker).detach();
    std::thread(espWorker).detach();  // async ESP projection — off render thread
#if defined(__aarch64__)
    std::thread(javaHookWorker).detach();  // one-shot Java hook — isolated from hookWorker loop
#endif
    LOGI(XS("bootstrap started: build=%s arch=%s SIGUSR1=toggle SIGUSR2=diagnostics"),
         astra::offsets::kProfileName, kArchitectureLabel);
}

} // namespace

extern "C" __attribute__((visibility("default"))) void Api_ToggleMenu() {
    const bool next = !g_menuOpen.load(std::memory_order_relaxed);
    g_menuOpen.store(next, std::memory_order_relaxed);
    LOGI(XS("menu toggled by export: open=%d"), next ? 1 : 0);
}

extern "C" __attribute__((visibility("default"))) void Api_SetMenuOpen(int open) {
    g_menuOpen.store(open != 0, std::memory_order_relaxed);
    LOGI(XS("menu set by export: open=%d"), open != 0 ? 1 : 0);
}

extern "C" __attribute__((visibility("default"))) int Api_IsMenuOpen() {
    return g_menuOpen.load(std::memory_order_relaxed) ? 1 : 0;
}

extern "C" __attribute__((visibility("default"))) void Api_SetEspEnabled(int enabled) {
    g_espBoxesEnabled.store(enabled != 0, std::memory_order_relaxed);
    LOGI(XS("basic ESP set by export: enabled=%d"), enabled != 0 ? 1 : 0);
}

extern "C" __attribute__((visibility("default"))) void Api_DumpDiagnostics() {
    logDiagnostics(XS("export"));
}

extern "C" __attribute__((visibility("default"))) uintptr_t Api_GetUnityBase() {
    return g_unityBase.load(std::memory_order_acquire);
}

extern "C" __attribute__((visibility("default"))) uintptr_t Api_ResolveUnityRva(uintptr_t rva) {
    const uintptr_t base = g_unityBase.load(std::memory_order_acquire);
    return base ? base + rva : 0;
}

extern "C" __attribute__((visibility("default"))) int Api_IsOffsetProfileReady() {
    return g_offsetProfileReady.load(std::memory_order_acquire) ? 1 : 0;
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    g_javaVm = vm;
    start();
    return JNI_VERSION_1_6;
}

__attribute__((constructor)) static void Api_OnLoad() {
    start();
}
