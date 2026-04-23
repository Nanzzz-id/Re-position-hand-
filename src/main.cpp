#include <jni.h>
#include <string>
#include <vector>
#include <android/log.h>
#include <android/input.h>
#include <android/native_window.h>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>

// ImGui Headers
#include "imgui.h"
#include "imgui_impl_android.h"
#include "imgui_impl_opengl3.h"

// EGL/GLES Headers
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES3/gl3.h>

// Dobby for general hooking
#include <dobby.h>

#define LOG_TAG "LeviViewModel"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// ===========================================================================
// Variabel Global dan State Mod
// ===========================================================================
struct HandOffset {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
} g_handOffset;

bool g_showMenu = true;

// Pointer ke fungsi asli ItemInHandRenderer::renderItemInHand
void (*orig_ItemInHandRenderer_renderItemInHand)(
    void* self, void* player, float partialTicks, float pitch,
    void* hand, float swingProgress, void* itemStack, float equippedProgress
);

// Hook untuk ItemInHandRenderer::renderItemInHand
void hook_ItemInHandRenderer_renderItemInHand(
    void* self, void* player, float partialTicks, float pitch,
    void* hand, float swingProgress, void* itemStack, float equippedProgress
) {
    LOGI("Hand Offset: x=%.2f, y=%.2f, z=%.2f", g_handOffset.x, g_handOffset.y, g_handOffset.z);
    orig_ItemInHandRenderer_renderItemInHand(
        self, player, partialTicks, pitch,
        hand, swingProgress, itemStack, equippedProgress
    );
}

// ===========================================================================
// ImGui Inisialisasi, Rendering, dan Hooking EGL
// ===========================================================================
static bool g_imgui_initialized = false;
static int g_display_width = 0, g_display_height = 0;
static EGLContext g_target_egl_context = EGL_NO_CONTEXT;
static EGLSurface g_target_egl_surface = EGL_NO_SURFACE;
static ANativeWindow* g_native_window = nullptr;

static EGLBoolean (*orig_eglSwapBuffers)(EGLDisplay, EGLSurface) = nullptr;
static EGLSurface (*orig_eglCreateWindowSurface)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*) = nullptr;

// Hook eglCreateWindowSurface untuk capture ANativeWindow*
static EGLSurface hook_eglCreateWindowSurface(
    EGLDisplay dpy, EGLConfig config,
    EGLNativeWindowType win, const EGLint* attribs
) {
    g_native_window = (ANativeWindow*)win;
    LOGI("Captured ANativeWindow from eglCreateWindowSurface.");
    return orig_eglCreateWindowSurface(dpy, config, win, attribs);
}

// Fungsi untuk menggambar menu ImGui
void DrawMenu() {
    if (!g_showMenu) return;

    ImGui::SetNextWindowSize(ImVec2(320, 200), ImGuiCond_FirstUseEver);
    ImGui::Begin("Levi Hand ViewModel", &g_showMenu);
    ImGui::Text("Adjust Hand Position (Real-time)");
    ImGui::Separator();

    ImGui::SliderFloat("Offset X", &g_handOffset.x, -2.0f, 2.0f);
    ImGui::SliderFloat("Offset Y", &g_handOffset.y, -2.0f, 2.0f);
    ImGui::SliderFloat("Offset Z", &g_handOffset.z, -2.0f, 2.0f);

    ImGui::Spacing();
    if (ImGui::Button("Reset", ImVec2(80, 30))) {
        g_handOffset = {0.0f, 0.0f, 0.0f};
    }

    ImGui::End();
}

static void setup_imgui() {
    // Tunggu sampai semua syarat terpenuhi
    if (g_imgui_initialized || g_display_width <= 0 || g_display_height <= 0 || !g_native_window) return;

    LOGI("Initializing ImGui context...");
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.FontGlobalScale = 1.5f;

    // ✅ Fix 1: ImGui_ImplAndroid_Init sekarang pakai g_native_window
    ImGui_ImplAndroid_Init(g_native_window);
    ImGui_ImplOpenGL3_Init("#version 300 es");

    g_imgui_initialized = true;
    LOGI("ImGui context initialized successfully.");
}

static void render_imgui() {
    if (!g_imgui_initialized) return;

    // Simpan state OpenGL saat ini
    GLint last_active_texture; glGetIntegerv(GL_ACTIVE_TEXTURE, &last_active_texture);
    glActiveTexture(GL_TEXTURE0);
    GLint last_texture; glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
    GLint last_program; glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);
    GLint last_array_buffer; glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &last_array_buffer);
    GLint last_element_array_buffer; glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &last_element_array_buffer);
    GLint last_viewport[4]; glGetIntegerv(GL_VIEWPORT, last_viewport);
    GLint last_scissor_box[4]; glGetIntegerv(GL_SCISSOR_BOX, last_scissor_box);
    GLenum last_blend_src_rgb; glGetIntegerv(GL_BLEND_SRC_RGB, (GLint*)&last_blend_src_rgb);
    GLenum last_blend_dst_rgb; glGetIntegerv(GL_BLEND_DST_RGB, (GLint*)&last_blend_dst_rgb);
    GLenum last_blend_src_alpha; glGetIntegerv(GL_BLEND_SRC_ALPHA, (GLint*)&last_blend_src_alpha);
    GLenum last_blend_dst_alpha; glGetIntegerv(GL_BLEND_DST_ALPHA, (GLint*)&last_blend_dst_alpha);
    GLenum last_blend_equation_rgb; glGetIntegerv(GL_BLEND_EQUATION_RGB, (GLint*)&last_blend_equation_rgb);
    GLenum last_blend_equation_alpha; glGetIntegerv(GL_BLEND_EQUATION_ALPHA, (GLint*)&last_blend_equation_alpha);
    GLboolean last_enable_blend = glIsEnabled(GL_BLEND);
    GLboolean last_enable_cull_face = glIsEnabled(GL_CULL_FACE);
    GLboolean last_enable_depth_test = glIsEnabled(GL_DEPTH_TEST);
    GLboolean last_enable_scissor_test = glIsEnabled(GL_SCISSOR_TEST);

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)g_display_width, (float)g_display_height);

    // Mulai frame ImGui baru
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame(); // ✅ Fix 2: hapus argumen width/height
    ImGui::NewFrame();

    DrawMenu();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Restore state OpenGL
    glUseProgram(last_program);
    glBindTexture(GL_TEXTURE_2D, last_texture);
    glActiveTexture(last_active_texture);
    glBindBuffer(GL_ARRAY_BUFFER, last_array_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, last_element_array_buffer);
    glViewport(last_viewport[0], last_viewport[1], last_viewport[2], last_viewport[3]);
    glScissor(last_scissor_box[0], last_scissor_box[1], last_scissor_box[2], last_scissor_box[3]);
    if (last_enable_blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (last_enable_cull_face) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (last_enable_depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (last_enable_scissor_test) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    glBlendEquationSeparate(last_blend_equation_rgb, last_blend_equation_alpha);
    glBlendFuncSeparate(last_blend_src_rgb, last_blend_dst_rgb, last_blend_src_alpha, last_blend_dst_alpha);
}

// Hook untuk eglSwapBuffers
static EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surf) {
    if (!orig_eglSwapBuffers) return EGL_FALSE;

    EGLContext current_context = eglGetCurrentContext();
    if (current_context == EGL_NO_CONTEXT ||
        (g_target_egl_context != EGL_NO_CONTEXT &&
         (current_context != g_target_egl_context || surf != g_target_egl_surface))) {
        return orig_eglSwapBuffers(dpy, surf);
    }

    EGLint w, h;
    eglQuerySurface(dpy, surf, EGL_WIDTH, &w);
    eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);

    if (w <= 0 || h <= 0) {
        return orig_eglSwapBuffers(dpy, surf);
    }

    if (g_target_egl_context == EGL_NO_CONTEXT) {
        g_target_egl_context = current_context;
        g_target_egl_surface = surf;
        LOGI("Target EGL context and surface set.");
    }

    g_display_width = w;
    g_display_height = h;

    setup_imgui();
    render_imgui();

    return orig_eglSwapBuffers(dpy, surf);
}

// ===========================================================================
// Input Handling untuk ImGui
// ===========================================================================
typedef bool (*PreloaderInput_OnTouch_Fn)(int action, int pointerId, float x, float y);

struct PreloaderInput_Interface {
    void (*RegisterTouchCallback)(PreloaderInput_OnTouch_Fn callback);
};

typedef PreloaderInput_Interface* (*GetPreloaderInput_Fn)();

bool OnTouchCallback(int action, int pointerId, float x, float y) {
    if (!g_imgui_initialized) return false;

    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(x, y);

    if (action == AMOTION_EVENT_ACTION_DOWN) {
        io.AddMouseButtonEvent(0, true);
    } else if (action == AMOTION_EVENT_ACTION_UP) {
        io.AddMouseButtonEvent(0, false);
    }

    return io.WantCaptureMouse;
}

// ===========================================================================
// Entry Point Mod
// ===========================================================================
static void* main_mod_thread(void*) {
    LOGI("Mod thread started. Waiting for game initialization...");
    sleep(5);

    void* egl_handle = dlopen("libEGL.so", RTLD_LAZY);
    if (egl_handle) {
        // Hook eglSwapBuffers
        void* eglSwapBuffers_sym = dlsym(egl_handle, "eglSwapBuffers");
        if (eglSwapBuffers_sym) {
            DobbyHook(eglSwapBuffers_sym, (void*)hook_eglSwapBuffers, (void**)&orig_eglSwapBuffers);
            LOGI("Hooked eglSwapBuffers successfully!");
        } else {
            LOGI("Failed to find eglSwapBuffers symbol.");
        }

        // ✅ Fix 3: Hook eglCreateWindowSurface untuk capture ANativeWindow*
        void* eglCreateWindowSurface_sym = dlsym(egl_handle, "eglCreateWindowSurface");
        if (eglCreateWindowSurface_sym) {
            DobbyHook(eglCreateWindowSurface_sym, (void*)hook_eglCreateWindowSurface, (void**)&orig_eglCreateWindowSurface);
            LOGI("Hooked eglCreateWindowSurface successfully!");
        } else {
            LOGI("Failed to find eglCreateWindowSurface symbol.");
        }
    } else {
        LOGI("Failed to open libEGL.so.");
    }

    // Hook ItemInHandRenderer::renderItemInHand
    void* mcpe_handle = dlopen("libminecraftpe.so", RTLD_LAZY);
    if (mcpe_handle) {
        void* target_render_item = dlsym(
            mcpe_handle,
            "_ZN18ItemInHandRenderer16renderItemInHandEP6PlayerffffRK9ItemStackf"
        );
        if (target_render_item) {
            DobbyHook(
                target_render_item,
                (void*)hook_ItemInHandRenderer_renderItemInHand,
                (void**)&orig_ItemInHandRenderer_renderItemInHand
            );
            LOGI("Hooked renderItemInHand successfully!");
        } else {
            LOGI("Failed to find renderItemInHand symbol. Check function signature.");
        }
    } else {
        LOGI("Failed to open libminecraftpe.so.");
    }

    // Hook input dari libpreloader.so
    void* preloader_lib = dlopen("libpreloader.so", RTLD_NOW);
    if (preloader_lib) {
        GetPreloaderInput_Fn GetInput = (GetPreloaderInput_Fn)dlsym(preloader_lib, "GetPreloaderInput");
        if (GetInput) {
            PreloaderInput_Interface* input = GetInput();
            if (input && input->RegisterTouchCallback) {
                input->RegisterTouchCallback(OnTouchCallback);
                LOGI("Registered touch callback for ImGui input.");
            }
        } else {
            LOGI("Failed to find GetPreloaderInput symbol in libpreloader.so.");
        }
    } else {
        LOGI("Failed to open libpreloader.so.");
    }

    return nullptr;
}

__attribute__((constructor))
void mod_entry_point() {
    pthread_t t;
    pthread_create(&t, nullptr, main_mod_thread, nullptr);
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    LOGI("JNI_OnLoad called by LeviLauncher!");
    return JNI_VERSION_1_6;
}
