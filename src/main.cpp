#include <jni.h>
#include <string>
#include <vector>
#include <android/log.h>
#include <android/input.h> // Untuk AMOTION_EVENT_ACTION_DOWN/UP
#include <dlfcn.h>
#include <pthread.h> // Untuk threading
#include <unistd.h>  // Untuk sleep

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
// Variabel Global dan State Mod Anda
// ===========================================================================
struct HandOffset {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
} g_handOffset;

bool g_showMenu = true;

// Pointer ke fungsi asli ItemInHandRenderer::renderItemInHand
// Signature ini mungkin perlu disesuaikan dengan versi Minecraft Bedrock yang Anda targetkan.
// Anda mungkin perlu melakukan reverse engineering lebih lanjut untuk mendapatkan signature yang tepat.
void (*orig_ItemInHandRenderer_renderItemInHand)(
    void* self, void* player, float partialTicks, float pitch,
    void* hand, float swingProgress, void* itemStack, float equippedProgress
);

// Hook untuk ItemInHandRenderer::renderItemInHand
void hook_ItemInHandRenderer_renderItemInHand(
    void* self, void* player, float partialTicks, float pitch,
    void* hand, float swingProgress, void* itemStack, float equippedProgress
) {
    // --- Logika Modifikasi Posisi Tangan --- 
    // Ini adalah bagian yang paling menantang dan sangat bergantung pada implementasi internal game.
    // Secara umum, Anda perlu menemukan cara untuk memodifikasi matriks transformasi yang digunakan
    // untuk merender item di tangan. Ini bisa berarti:
    // 1. Mencari parameter di 'self' atau 'player' yang mengontrol transformasi.
    // 2. Menggunakan Dobby untuk hook fungsi OpenGL ES yang relevan (misalnya glTranslate, glRotate)
    //    yang dipanggil oleh renderItemInHand dan menyuntikkan offset Anda di sana.
    // 3. Mengubah nilai di memori yang digunakan oleh game untuk posisi tangan.
    //
    // Sebagai placeholder, kita akan mencetak offset dan memanggil fungsi asli.
    // Untuk implementasi nyata, Anda perlu melakukan reverse engineering lebih lanjut.
    LOGI("Hand Offset: x=%.2f, y=%.2f, z=%.2f", g_handOffset.x, g_handOffset.y, g_handOffset.z);

    // Contoh hipotesis: Jika ada cara untuk memodifikasi matriks model-view sebelum rendering
    // glMatrixMode(GL_MODELVIEW);
    // glPushMatrix();
    // glTranslatef(g_handOffset.x, g_handOffset.y, g_handOffset.z);
    
    orig_ItemInHandRenderer_renderItemInHand(self, player, partialTicks, pitch, hand, swingProgress, itemStack, equippedProgress);

    // glPopMatrix(); // Jika glPushMatrix() dipanggil
}

// ===========================================================================
// ImGui Inisialisasi, Rendering, dan Hooking EGL
// ===========================================================================
static bool g_imgui_initialized = false;
static int g_display_width = 0, g_display_height = 0;
static EGLContext g_target_egl_context = EGL_NO_CONTEXT;
static EGLSurface g_target_egl_surface = EGL_NO_SURFACE;
static EGLBoolean (*orig_eglSwapBuffers)(EGLDisplay, EGLSurface) = nullptr;

// Fungsi untuk menggambar menu ImGui Anda
void DrawMenu() {
    if (!g_showMenu) return;

    ImGui::Begin("Levi Hand ViewModel", &g_showMenu);
    ImGui::Text("Adjust Hand Position (Real-time)");
    
    ImGui::SliderFloat("Offset X", &g_handOffset.x, -2.0f, 2.0f);
    ImGui::SliderFloat("Offset Y", &g_handOffset.y, -2.0f, 2.0f);
    ImGui::SliderFloat("Offset Z", &g_handOffset.z, -2.0f, 2.0f);

    if (ImGui::Button("Reset")) {
        g_handOffset = {0.0f, 0.0f, 0.0f};
    }
    
    ImGui::End();
}

static void setup_imgui() {
    if (g_imgui_initialized || g_display_width <= 0 || g_display_height <= 0) return;
    LOGI("Initializing ImGui context...");
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // Jangan simpan konfigurasi ImGui ke file
    io.FontGlobalScale = 1.5f; // Sesuaikan skala font jika perlu

    // Inisialisasi backend ImGui untuk Android dan OpenGL3
    ImGui_ImplAndroid_Init();
    // Gunakan #version 300 es jika Minecraft menggunakan GLES 3.0+, atau #version 100 es untuk GLES 2.0
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
    ImGui_ImplAndroid_NewFrame(g_display_width, g_display_height); // Perlu width, height
    ImGui::NewFrame();

    // Panggil fungsi gambar menu Anda
    DrawMenu();

    // Render ImGui
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
    // Pastikan kita berada di konteks yang benar
    if (current_context == EGL_NO_CONTEXT || 
        (g_target_egl_context != EGL_NO_CONTEXT && 
         (current_context != g_target_egl_context || surf != g_target_egl_surface))) {
        return orig_eglSwapBuffers(dpy, surf);
    }

    EGLint w, h;
    eglQuerySurface(dpy, surf, EGL_WIDTH, &w);
    eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);

    // Abaikan permukaan yang terlalu kecil atau tidak valid
    if (w <= 0 || h <= 0) {
        return orig_eglSwapBuffers(dpy, surf);
    }

    // Set target context dan surface jika belum diset
    if (g_target_egl_context == EGL_NO_CONTEXT) {
        g_target_egl_context = current_context;
        g_target_egl_surface = surf;
        LOGI("Target EGL context and surface set.");
    }

    g_display_width = w;
    g_display_height = h;

    setup_imgui(); // Inisialisasi ImGui jika belum
    render_imgui(); // Render ImGui GUI

    return orig_eglSwapBuffers(dpy, surf);
}

// ===========================================================================
// Input Handling untuk ImGui
// ===========================================================================
// Struktur dan typedef dari libpreloader.so
typedef bool (*PreloaderInput_OnTouch_Fn)(int action, int pointerId, float x, float y);

struct PreloaderInput_Interface {
    void (*RegisterTouchCallback)(PreloaderInput_OnTouch_Fn callback);
};

typedef PreloaderInput_Interface* (*GetPreloaderInput_Fn)();

bool OnTouchCallback(int action, int pointerId, float x, float y) {
    if (!g_imgui_initialized) return false;
    
    ImGuiIO& io = ImGui::GetIO();
    // Perbarui posisi mouse/sentuhan
    // ImGui_ImplAndroid_HandleInputEvent() mungkin lebih baik jika tersedia
    io.AddMousePosEvent(x, y);
    
    // Perbarui status tombol mouse/sentuhan
    if (action == AMOTION_EVENT_ACTION_DOWN) {
        io.AddMouseButtonEvent(0, true);
    } else if (action == AMOTION_EVENT_ACTION_UP) {
        io.AddMouseButtonEvent(0, false);
    } else if (action == AMOTION_EVENT_ACTION_MOVE) {
        // ImGui secara otomatis menangani pergerakan mouse jika posisi diperbarui
    }

    // Jika ImGui ingin menangkap input, kembalikan true untuk mencegah game memprosesnya
    return io.WantCaptureMouse;
}

// ===========================================================================
// Entry Point Mod (Constructor dan Threading)
// ===========================================================================
static void* main_mod_thread(void*) {
    LOGI("Mod thread started. Waiting for game initialization...");
    sleep(5); // Beri waktu game untuk inisialisasi

    // Hook eglSwapBuffers menggunakan Dobby
    void* egl_handle = dlopen("libEGL.so", RTLD_LAZY);
    if (egl_handle) {
        void* eglSwapBuffers_sym = dlsym(egl_handle, "eglSwapBuffers");
        if (eglSwapBuffers_sym) {
            DobbyHook(eglSwapBuffers_sym, (void*)hook_eglSwapBuffers, (void**)&orig_eglSwapBuffers);
            LOGI("Hooked eglSwapBuffers successfully using Dobby!");
        } else {
            LOGI("Failed to find eglSwapBuffers symbol.");
        }
    } else {
        LOGI("Failed to open libEGL.so.");
    }

    // Hook ItemInHandRenderer::renderItemInHand (mod Anda)
    void* mcpe_handle = dlopen("libminecraftpe.so", RTLD_LAZY);
    if (mcpe_handle) {
        // Pastikan signature fungsi ini benar untuk versi Minecraft Anda
        // Contoh signature dari referensi lain: _ZN18ItemInHandRenderer16renderItemInHandEP6PlayerffffRK9ItemStackf
        // Anda mungkin perlu menyesuaikan ini.
        void* target_render_item = dlsym(mcpe_handle, "_ZN18ItemInHandRenderer16renderItemInHandEP6PlayerffffRK9ItemStackf");
        if (target_render_item) {
            DobbyHook(target_render_item, (void*)hook_ItemInHandRenderer_renderItemInHand, (void**)&orig_ItemInHandRenderer_renderItemInHand);
            LOGI("Hooked renderItemInHand successfully!");
        } else {
            LOGI("Failed to find renderItemInHand symbol. Check function signature.");
        }
    } else {
        LOGI("Failed to open libminecraftpe.so.");
    }

    // Hook input handling dari libpreloader.so
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

// Fungsi constructor yang akan dipanggil saat library dimuat
__attribute__((constructor))
void mod_entry_point() {
    pthread_t t;
    pthread_create(&t, nullptr, main_mod_thread, nullptr);
}

// JNI_OnLoad bisa tetap ada jika Levi Launcher memerlukannya, 
// tetapi inisialisasi utama dipindahkan ke thread terpisah.
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    LOGI("JNI_OnLoad called by LeviLauncher!");
    // Anda bisa menambahkan inisialisasi ringan di sini jika perlu,
    // tetapi inisialisasi berat dan hooking sebaiknya di thread terpisah.
    return JNI_VERSION_1_6;
}
