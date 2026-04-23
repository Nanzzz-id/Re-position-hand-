#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include <cmath>
#include <GLES3/gl3.h>
#include <EGL/egl.h>

#include <dobby.h>

#define LOG_TAG "LeviViewModel"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// ============================================================
// ✏️  EDIT NILAI DI SINI — sama seperti slider Atlas Client
// ============================================================

// -- Displacement (posisi) --
static float VM_DISP_X =  0.0f;   // kiri/kanan
static float VM_DISP_Y =  0.0f;   // atas/bawah
static float VM_DISP_Z = -0.3f;   // depan/belakang (default Atlas: -0.3)

// -- Rotation (rotasi dalam derajat) --
static float VM_ROT_X  =  0.0f;
static float VM_ROT_Y  =  0.0f;
static float VM_ROT_Z  =  0.0f;

// -- Scale (ukuran) --
static float VM_SCALE_X = 1.2f;   // default Atlas: 1.2
static float VM_SCALE_Y = 1.2f;   // default Atlas: 1.2
static float VM_SCALE_Z = 2.0f;   // default Atlas: 2.0

// ============================================================

// ============================================================
// Matrix 4x4 helper (kolom-major, standar OpenGL)
// ============================================================
struct Mat4 {
    float m[16];

    static Mat4 identity() {
        Mat4 r = {};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }

    static Mat4 translate(float x, float y, float z) {
        Mat4 r = identity();
        r.m[12] = x; r.m[13] = y; r.m[14] = z;
        return r;
    }

    static Mat4 scale(float x, float y, float z) {
        Mat4 r = identity();
        r.m[0] = x; r.m[5] = y; r.m[10] = z;
        return r;
    }

    static Mat4 rotateX(float deg) {
        float rad = deg * (float)M_PI / 180.0f;
        Mat4 r = identity();
        r.m[5]  =  cosf(rad); r.m[9]  = -sinf(rad);
        r.m[6]  =  sinf(rad); r.m[10] =  cosf(rad);
        return r;
    }

    static Mat4 rotateY(float deg) {
        float rad = deg * (float)M_PI / 180.0f;
        Mat4 r = identity();
        r.m[0]  =  cosf(rad); r.m[8]  =  sinf(rad);
        r.m[2]  = -sinf(rad); r.m[10] =  cosf(rad);
        return r;
    }

    static Mat4 rotateZ(float deg) {
        float rad = deg * (float)M_PI / 180.0f;
        Mat4 r = identity();
        r.m[0]  =  cosf(rad); r.m[4]  = -sinf(rad);
        r.m[1]  =  sinf(rad); r.m[5]  =  cosf(rad);
        return r;
    }

    // Perkalian matrix: this * b
    Mat4 operator*(const Mat4& b) const {
        Mat4 out = {};
        for (int col = 0; col < 4; col++) {
            for (int row = 0; row < 4; row++) {
                for (int k = 0; k < 4; k++) {
                    out.m[col*4 + row] += m[k*4 + row] * b.m[col*4 + k];
                }
            }
        }
        return out;
    }
};

// ============================================================
// Fungsi pointer untuk eglSwapBuffers dan glUniformMatrix4fv
// ============================================================
static EGLBoolean (*orig_eglSwapBuffers)(EGLDisplay, EGLSurface) = nullptr;
static void       (*orig_glUniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*) = nullptr;

// State untuk intercept uniform matrix
static bool  g_in_hand_render    = false;
static bool  g_matrix_patched    = false;
static GLint g_mvp_uniform_loc   = -1;
static GLuint g_last_program     = 0;

// ============================================================
// Hook glUniformMatrix4fv
// Intercept upload MVP matrix dan inject transform tangan
// ============================================================
void hook_glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) {
    if (g_in_hand_render && !g_matrix_patched && count == 1) {
        // Bangun transform kita: T * Rx * Ry * Rz * S
        Mat4 T  = Mat4::translate(VM_DISP_X, VM_DISP_Y, VM_DISP_Z);
        Mat4 Rx = Mat4::rotateX(VM_ROT_X);
        Mat4 Ry = Mat4::rotateY(VM_ROT_Y);
        Mat4 Rz = Mat4::rotateZ(VM_ROT_Z);
        Mat4 S  = Mat4::scale(VM_SCALE_X, VM_SCALE_Y, VM_SCALE_Z);

        // Original matrix dari game
        Mat4 orig;
        for (int i = 0; i < 16; i++) orig.m[i] = value[i];

        // Terapkan transform kita di atas matrix asli: orig * T * Rx * Ry * Rz * S
        Mat4 result = orig * T * Rx * Ry * Rz * S;

        g_matrix_patched = true;
        orig_glUniformMatrix4fv(location, count, transpose, result.m);
        return;
    }

    orig_glUniformMatrix4fv(location, count, transpose, value);
}

// ============================================================
// Pointer ke renderItemInHand asli
// ============================================================
void (*orig_renderItemInHand)(
    void* self, void* player, float partialTicks, float pitch,
    void* hand, float swingProgress, void* itemStack, float equippedProgress
) = nullptr;

// ============================================================
// Hook renderItemInHand
// Set flag sebelum/sesudah render agar glUniformMatrix4fv
// tahu kapan harus intercept
// ============================================================
void hook_renderItemInHand(
    void* self, void* player, float partialTicks, float pitch,
    void* hand, float swingProgress, void* itemStack, float equippedProgress
) {
    g_in_hand_render  = true;
    g_matrix_patched  = false;

    orig_renderItemInHand(
        self, player, partialTicks, pitch,
        hand, swingProgress, itemStack, equippedProgress
    );

    g_in_hand_render = false;
    g_matrix_patched = false;
}

// ============================================================
// Hook eglSwapBuffers — hanya untuk log ukuran layar (opsional)
// ============================================================
static EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surf) {
    return orig_eglSwapBuffers(dpy, surf);
}

// ============================================================
// Thread utama
// ============================================================
static void* main_mod_thread(void*) {
    LOGI("=== LeviViewModel Mod Thread Started ===");
    sleep(5);

    // --- Hook eglSwapBuffers (opsional, bisa dihapus jika tidak perlu) ---
    void* egl_handle = dlopen("libEGL.so", RTLD_LAZY);
    if (egl_handle) {
        void* sym = dlsym(egl_handle, "eglSwapBuffers");
        if (sym) {
            DobbyHook(sym, (void*)hook_eglSwapBuffers, (void**)&orig_eglSwapBuffers);
            LOGI("Hooked eglSwapBuffers.");
        }
    }

    // --- Hook glUniformMatrix4fv ---
    void* gles_handle = dlopen("libGLESv3.so", RTLD_LAZY);
    if (!gles_handle) gles_handle = dlopen("libGLESv2.so", RTLD_LAZY);
    if (gles_handle) {
        void* sym = dlsym(gles_handle, "glUniformMatrix4fv");
        if (sym) {
            DobbyHook(sym, (void*)hook_glUniformMatrix4fv, (void**)&orig_glUniformMatrix4fv);
            LOGI("Hooked glUniformMatrix4fv.");
        } else {
            LOGI("ERROR: glUniformMatrix4fv symbol tidak ditemukan.");
        }
    } else {
        LOGI("ERROR: libGLESv3.so / libGLESv2.so tidak bisa dibuka.");
    }

    // --- Hook renderItemInHand ---
    void* mcpe = dlopen("libminecraftpe.so", RTLD_LAZY);
    if (!mcpe) {
        LOGI("ERROR: libminecraftpe.so tidak bisa dibuka.");
        return nullptr;
    }

    const char* sym = "_ZN18ItemInHandRenderer16renderItemInHandEP6PlayerffffRK9ItemStackf";
    void* target = dlsym(mcpe, sym);
    if (!target) {
        LOGI("ERROR: Symbol renderItemInHand tidak ditemukan. Cek versi MC.");
        return nullptr;
    }

    int res = DobbyHook(target, (void*)hook_renderItemInHand, (void**)&orig_renderItemInHand);
    if (res == 0) {
        LOGI("SUCCESS: Hook renderItemInHand aktif!");
        LOGI("Config: Disp(%.2f, %.2f, %.2f) Rot(%.1f, %.1f, %.1f) Scale(%.2f, %.2f, %.2f)",
             VM_DISP_X, VM_DISP_Y, VM_DISP_Z,
             VM_ROT_X,  VM_ROT_Y,  VM_ROT_Z,
             VM_SCALE_X, VM_SCALE_Y, VM_SCALE_Z);
    } else {
        LOGI("ERROR: DobbyHook gagal, code: %d", res);
    }

    return nullptr;
}

// ============================================================
// Entry point
// ============================================================
__attribute__((constructor))
void mod_entry_point() {
    LOGI("=== LeviViewModel Mod Loaded ===");
    pthread_t t;
    pthread_create(&t, nullptr, main_mod_thread, nullptr);
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    LOGI("JNI_OnLoad called.");
    return JNI_VERSION_1_6;
}
