#include <jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <link.h>
#include <elf.h>
#include <cstdio>
#include <cstdarg>
#include <vector>
#include <string>

#include "imgui.h"
#include "imgui_impl_android.h"
#include "imgui_impl_opengl3.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <dobby.h>

#define LOG_TAG "LeviViewModel"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// ============================================================
// Hasil scan disimpan di sini, ditampilkan di ImGui
// ============================================================
static std::vector<std::string> g_found_symbols;
static std::string              g_status = "Scanning...";
static bool                     g_scan_done = false;

// ============================================================
// EGL / ImGui state
// ============================================================
static bool        g_imgui_init    = false;
static ANativeWindow* g_native_win = nullptr;
static EGLContext  g_egl_ctx       = EGL_NO_CONTEXT;
static EGLSurface  g_egl_surf      = EGL_NO_SURFACE;
static int         g_width = 0, g_height = 0;

static EGLBoolean (*orig_eglSwapBuffers)(EGLDisplay, EGLSurface)                                         = nullptr;
static EGLSurface (*orig_eglCreateWindowSurface)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*) = nullptr;

// ============================================================
// Hook eglCreateWindowSurface — capture ANativeWindow
// ============================================================
static EGLSurface hook_eglCreateWindowSurface(
    EGLDisplay dpy, EGLConfig cfg, EGLNativeWindowType win, const EGLint* attr)
{
    g_native_win = (ANativeWindow*)win;
    LOGI("ANativeWindow captured.");
    return orig_eglCreateWindowSurface(dpy, cfg, win, attr);
}

// ============================================================
// Setup ImGui
// ============================================================
static void setup_imgui() {
    if (g_imgui_init || !g_native_win || g_width <= 0) return;
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.FontGlobalScale = 1.8f; // Besar agar mudah dibaca
    ImGui_ImplAndroid_Init(g_native_win);
    ImGui_ImplOpenGL3_Init("#version 300 es");
    g_imgui_init = true;
    LOGI("ImGui initialized.");
}

// ============================================================
// Render overlay — tampilkan hasil scan
// ============================================================
static void render_overlay() {
    if (!g_imgui_init) return;

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)g_width, (float)g_height);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame();
    ImGui::NewFrame();

    // Window selebar layar, transparan sebagian
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)g_width, (float)g_height));
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::Begin("LeviViewModel Symbol Finder", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_HorizontalScrollbar);

    ImGui::TextColored(ImVec4(0,1,1,1), "=== LeviViewModel Diagnostic ===");
    ImGui::TextColored(ImVec4(1,1,0,1), "MC 1.26.13.1  |  arm64");
    ImGui::Separator();

    ImGui::Text("Status: %s", g_status.c_str());
    ImGui::Separator();

    if (g_found_symbols.empty() && g_scan_done) {
        ImGui::TextColored(ImVec4(1,0,0,1), "Tidak ada symbol ditemukan!");
        ImGui::TextColored(ImVec4(1,0.5f,0,1), "Library kemungkinan stripped.");
        ImGui::TextColored(ImVec4(1,0.5f,0,1), "Perlu pattern scan.");
    } else {
        ImGui::TextColored(ImVec4(0,1,0,1), "Symbols ditemukan (%d):", (int)g_found_symbols.size());
        ImGui::Separator();
        for (auto& sym : g_found_symbols) {
            ImGui::TextWrapped("%s", sym.c_str());
            ImGui::Separator();
        }
    }

    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ============================================================
// Hook eglSwapBuffers
// ============================================================
static EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surf) {
    if (!orig_eglSwapBuffers) return EGL_FALSE;

    EGLContext ctx = eglGetCurrentContext();
    if (ctx == EGL_NO_CONTEXT) return orig_eglSwapBuffers(dpy, surf);

    EGLint w, h;
    eglQuerySurface(dpy, surf, EGL_WIDTH, &w);
    eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);
    if (w <= 0 || h <= 0) return orig_eglSwapBuffers(dpy, surf);

    if (g_egl_ctx == EGL_NO_CONTEXT) {
        g_egl_ctx  = ctx;
        g_egl_surf = surf;
    }
    if (ctx != g_egl_ctx || surf != g_egl_surf)
        return orig_eglSwapBuffers(dpy, surf);

    g_width  = w;
    g_height = h;

    setup_imgui();
    render_overlay();

    return orig_eglSwapBuffers(dpy, surf);
}

// ============================================================
// Scan ELF untuk symbol yang relevan
// ============================================================
static void do_symbol_scan() {
    // Cari path via /proc/self/maps
    char mc_path[512] = {};
    FILE* maps = fopen("/proc/self/maps", "r");
    if (maps) {
        char line[1024];
        while (fgets(line, sizeof(line), maps)) {
            if (strstr(line, "libminecraftpe.so")) {
                char* p = strchr(line, '/');
                if (p) {
                    size_t len = strlen(p);
                    while (len > 0 && (p[len-1] == '\n' || p[len-1] == '\r'))
                        p[--len] = 0;
                    strncpy(mc_path, p, sizeof(mc_path)-1);
                    break;
                }
            }
        }
        fclose(maps);
    }

    if (!mc_path[0]) {
        g_status = "ERROR: Path libminecraftpe tidak ditemukan!";
        g_scan_done = true;
        return;
    }

    g_status = std::string("Scanning: ") + mc_path;
    LOGI("Scanning: %s", mc_path);

    FILE* f = fopen(mc_path, "rb");
    if (!f) {
        g_status = "ERROR: Tidak bisa buka ELF file!";
        g_scan_done = true;
        return;
    }

    Elf64_Ehdr ehdr;
    fread(&ehdr, sizeof(ehdr), 1, f);
    if (ehdr.e_ident[0] != 0x7f) {
        g_status = "ERROR: Bukan file ELF valid!";
        fclose(f); g_scan_done = true; return;
    }

    fseek(f, (long)ehdr.e_shoff, SEEK_SET);
    Elf64_Shdr* shdrs = new Elf64_Shdr[ehdr.e_shnum];
    fread(shdrs, sizeof(Elf64_Shdr), ehdr.e_shnum, f);

    Elf64_Shdr* dynsym_sh = nullptr;
    Elf64_Shdr* dynstr_sh = nullptr;
    for (int i = 0; i < ehdr.e_shnum; i++) {
        if (shdrs[i].sh_type == SHT_DYNSYM) dynsym_sh = &shdrs[i];
        if (shdrs[i].sh_type == SHT_STRTAB && i != ehdr.e_shstrndx && !dynstr_sh)
            dynstr_sh = &shdrs[i];
    }

    if (!dynsym_sh || !dynstr_sh) {
        g_status = "ERROR: Section dynsym/dynstr tidak ada (mungkin stripped)";
        delete[] shdrs; fclose(f); g_scan_done = true; return;
    }

    char* strtab = new char[dynstr_sh->sh_size];
    fseek(f, (long)dynstr_sh->sh_offset, SEEK_SET);
    fread(strtab, 1, dynstr_sh->sh_size, f);

    int nsym = (int)(dynsym_sh->sh_size / sizeof(Elf64_Sym));
    Elf64_Sym* syms = new Elf64_Sym[nsym];
    fseek(f, (long)dynsym_sh->sh_offset, SEEK_SET);
    fread(syms, sizeof(Elf64_Sym), nsym, f);

    const char* kw[] = {
        "renderItem", "ItemInHand", "HandRender",
        "HeldItem",   "ViewModel",  "viewModel",
        "renderHand", "ItemRender", "HandModel",
        "firstPerson","FirstPerson","InHandRend"
    };
    const int nkw = 12;

    for (int i = 0; i < nsym; i++) {
        const char* name = strtab + syms[i].st_name;
        if (!name || !*name) continue;
        for (int k = 0; k < nkw; k++) {
            if (strstr(name, kw[k])) {
                g_found_symbols.push_back(std::string(name));
                LOGI("SYM: %s", name);
                break;
            }
        }
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "Scan selesai. %d symbols ditemukan.", (int)g_found_symbols.size());
    g_status = buf;

    delete[] syms;
    delete[] strtab;
    delete[] shdrs;
    fclose(f);
    g_scan_done = true;
}

// ============================================================
// Thread utama
// ============================================================
static void* main_mod_thread(void*) {
    LOGI("Thread started.");
    sleep(3);

    // Hook EGL
    void* egl = dlopen("libEGL.so", RTLD_LAZY);
    if (egl) {
        void* sym;
        sym = dlsym(egl, "eglSwapBuffers");
        if (sym) DobbyHook(sym, (void*)hook_eglSwapBuffers, (void**)&orig_eglSwapBuffers);
        sym = dlsym(egl, "eglCreateWindowSurface");
        if (sym) DobbyHook(sym, (void*)hook_eglCreateWindowSurface, (void**)&orig_eglCreateWindowSurface);
        LOGI("EGL hooked.");
    }

    // Tunggu sampai ImGui siap, baru scan
    int wait = 0;
    while (!g_imgui_init && wait < 60) {
        sleep(1); wait++;
    }

    if (g_imgui_init) {
        g_status = "Scanning symbols...";
        do_symbol_scan();
    } else {
        g_status = "ERROR: ImGui tidak bisa init (EGL/window issue)";
        g_scan_done = true;
        LOGI("ImGui init timeout!");
    }

    return nullptr;
}

// ============================================================
// Input handling sederhana
// ============================================================
typedef bool (*TouchCb)(int, int, float, float);
struct InputIface { void (*reg)(TouchCb); };
typedef InputIface* (*GetInput)();

// ============================================================
// Entry point
// ============================================================
__attribute__((constructor))
void mod_entry_point() {
    LOGI("=== LeviViewModel Diagnostic Loaded ===");
    pthread_t t;
    pthread_create(&t, nullptr, main_mod_thread, nullptr);
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    return JNI_VERSION_1_6;
}
