#include "qray_window.h"

#ifdef USE_INTERNAL_SDL_HEADERS
#include "SDL.h"
#include "SDL_syswm.h"
#else
#include <SDL.h>
#include <SDL_syswm.h>
#endif

// X11 headers pulled by SDL_syswm.h define lowercase/common identifiers as macros.
#ifdef None
#undef None
#endif
#ifdef Bool
#undef Bool
#endif
#ifdef True
#undef True
#endif
#ifdef False
#undef False
#endif
#ifdef Status
#undef Status
#endif
#ifdef Success
#undef Success
#endif
#ifdef Always
#undef Always
#endif
#ifdef Convex
#undef Convex
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

struct VidModeEntry
{
    int width;
    int height;
    float pixelAspect;
};

constexpr VidModeEntry kVidModes[] = {
    {320, 240, 1.0f},
    {400, 300, 1.0f},
    {512, 384, 1.0f},
    {640, 480, 1.0f},
    {800, 600, 1.0f},
    {960, 720, 1.0f},
    {1024, 768, 1.0f},
    {1152, 864, 1.0f},
    {1280, 1024, 1.0f},
    {1600, 1200, 1.0f},
    {2048, 1536, 1.0f},
    {856, 480, 1.0f},
};
constexpr int kNumVidModes = static_cast<int>(sizeof(kVidModes) / sizeof(kVidModes[0]));

SDL_Window *sdlWindow = nullptr;
cvar_t *rFullscreen = nullptr;
float displayAspect = 1.0f;

void CopyWindowString(char *dst, const char *src, size_t dstSize)
{
    if (dst == nullptr || dstSize == 0) {
        return;
    }
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    std::snprintf(dst, dstSize, "%s", src);
}

int CompareModes(const void *a, const void *b)
{
    constexpr float kAspectEpsilon = 0.001f;
    const auto *modeA = static_cast<const SDL_Rect *>(a);
    const auto *modeB = static_cast<const SDL_Rect *>(b);
    const float aspectA = static_cast<float>(modeA->w) / static_cast<float>(modeA->h);
    const float aspectB = static_cast<float>(modeB->w) / static_cast<float>(modeB->h);
    const int areaA = modeA->w * modeA->h;
    const int areaB = modeB->w * modeB->h;
    const float diffA = std::fabs(aspectA - displayAspect);
    const float diffB = std::fabs(aspectB - displayAspect);
    const float diff = diffA - diffB;

    if (diff > kAspectEpsilon) {
        return 1;
    }
    if (diff < -kAspectEpsilon) {
        return -1;
    }
    return areaA - areaB;
}

void DetectAvailableModes(const refimport_t &ri)
{
    if (sdlWindow == nullptr) {
        return;
    }
    const int display = SDL_GetWindowDisplayIndex(sdlWindow);
    if (display < 0) {
        ri.Printf(PRINT_WARNING, "Couldn't get window display index, no resolutions detected: %s\n",
            SDL_GetError());
        return;
    }

    SDL_DisplayMode windowMode{};
    const int numSdlModes = SDL_GetNumDisplayModes(display);
    if (SDL_GetWindowDisplayMode(sdlWindow, &windowMode) < 0 || numSdlModes <= 0) {
        ri.Printf(PRINT_WARNING, "Couldn't get window display mode, no resolutions detected: %s\n",
            SDL_GetError());
        return;
    }

    auto *modes = static_cast<SDL_Rect *>(SDL_calloc(static_cast<size_t>(numSdlModes), sizeof(SDL_Rect)));
    if (modes == nullptr) {
        ri.Error(ERR_FATAL, "Out of memory");
        return;
    }

    int numModes = 0;
    for (int i = 0; i < numSdlModes; ++i) {
        SDL_DisplayMode mode{};
        if (SDL_GetDisplayMode(display, i, &mode) < 0) {
            continue;
        }
        if (mode.w == 0 || mode.h == 0) {
            ri.Printf(PRINT_ALL, "Display supports any resolution\n");
            SDL_free(modes);
            return;
        }
        if (windowMode.format != mode.format) {
            continue;
        }

        int j = 0;
        for (; j < numModes; ++j) {
            if (mode.w == modes[j].w && mode.h == modes[j].h) {
                break;
            }
        }
        if (j != numModes) {
            continue;
        }
        modes[numModes].w = mode.w;
        modes[numModes].h = mode.h;
        ++numModes;
    }

    if (numModes > 1) {
        std::qsort(modes, static_cast<size_t>(numModes), sizeof(SDL_Rect), CompareModes);
    }

    char buf[MAX_STRING_CHARS] = {};
    size_t len = 0;
    for (int i = 0; i < numModes; ++i) {
        char entry[32] = {};
        const int entryLen = std::snprintf(entry, sizeof(entry), "%ux%u ",
            static_cast<unsigned>(modes[i].w), static_cast<unsigned>(modes[i].h));
        if (entryLen > 0 && len + static_cast<size_t>(entryLen) < sizeof(buf)) {
            std::memcpy(buf + len, entry, static_cast<size_t>(entryLen));
            len += static_cast<size_t>(entryLen);
            buf[len] = '\0';
        }
    }

    if (len > 0) {
        buf[len - 1] = '\0';
        ri.Printf(PRINT_ALL, "Available modes: '%s'\n", buf);
        ri.Cvar_Set("r_availableModes", buf);
    }
    SDL_free(modes);
}

bool ResolveModeDimensions(int mode, cvar_t *customWidth, cvar_t *customHeight,
    cvar_t *customPixelAspect, const SDL_DisplayMode &desktopMode, int &outWidth, int &outHeight,
    float &outWindowAspect)
{
    if (mode == -2) {
        if (desktopMode.w > 0 && desktopMode.h > 0) {
            outWidth = desktopMode.w;
            outHeight = desktopMode.h;
        } else {
            outWidth = std::max(320, customWidth->integer);
            outHeight = std::max(240, customHeight->integer);
        }
        outWindowAspect = static_cast<float>(outWidth) / static_cast<float>(outHeight);
        return true;
    }
    if (mode == -1) {
        outWidth = std::max(320, customWidth->integer);
        outHeight = std::max(240, customHeight->integer);
        const float pixelAspect = customPixelAspect->value > 0.0f ? customPixelAspect->value : 1.0f;
        outWindowAspect = static_cast<float>(outWidth) / (static_cast<float>(outHeight) * pixelAspect);
        return true;
    }
    if (mode >= 0 && mode < kNumVidModes) {
        const VidModeEntry &entry = kVidModes[mode];
        outWidth = entry.width;
        outHeight = entry.height;
        outWindowAspect = static_cast<float>(outWidth) / (static_cast<float>(outHeight) * entry.pixelAspect);
        return true;
    }
    return false;
}

} // namespace

bool QrayWindow_Init(const refimport_t &ri, glconfig_t *config)
{
    if (config == nullptr) {
        return false;
    }
    if (sdlWindow != nullptr) {
        return true;
    }

    cvar_t *rMode = ri.Cvar_Get("r_mode", "-2", CVAR_ARCHIVE | CVAR_LATCH);
    cvar_t *rCustomWidth = ri.Cvar_Get("r_customwidth", "1280", CVAR_ARCHIVE | CVAR_LATCH);
    cvar_t *rCustomHeight = ri.Cvar_Get("r_customheight", "720", CVAR_ARCHIVE | CVAR_LATCH);
    cvar_t *rCustomPixelAspect = ri.Cvar_Get("r_customPixelAspect", "1", CVAR_ARCHIVE | CVAR_LATCH);
    rFullscreen = ri.Cvar_Get("r_fullscreen", "0", CVAR_ARCHIVE);
    cvar_t *rNoborder = ri.Cvar_Get("r_noborder", "0", CVAR_ARCHIVE | CVAR_LATCH);
    cvar_t *rCenterWindow = ri.Cvar_Get("r_centerWindow", "0", CVAR_ARCHIVE | CVAR_LATCH);
    cvar_t *rAllowResize = ri.Cvar_Get("r_allowResize", "0", CVAR_ARCHIVE | CVAR_LATCH);
    cvar_t *rDisplayRefresh = ri.Cvar_Get("r_displayRefresh", "0", CVAR_LATCH);
    ri.Cvar_Get("r_availableModes", "", CVAR_ROM);
    ri.Cvar_Get("r_sdlDriver", "", CVAR_ROM);

    ri.Sys_GLimpInit();

    if (!SDL_WasInit(SDL_INIT_VIDEO)) {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            ri.Printf(PRINT_WARNING, "SDL_Init( SDL_INIT_VIDEO ) FAILED (%s)\n", SDL_GetError());
            return false;
        }
        const char *driverName = SDL_GetCurrentVideoDriver();
        ri.Printf(PRINT_ALL, "SDL using driver \"%s\"\n", driverName != nullptr ? driverName : "unknown");
        ri.Cvar_Set("r_sdlDriver", driverName != nullptr ? driverName : "");
    }

    bool fullscreen = rFullscreen->integer != 0;
    if (fullscreen && ri.Cvar_VariableIntegerValue("in_nograb") != 0) {
        ri.Printf(PRINT_ALL, "Fullscreen not allowed with in_nograb 1\n");
        ri.Cvar_Set("r_fullscreen", "0");
        fullscreen = false;
    }
    rFullscreen->modified = qfalse;

    SDL_DisplayMode desktopMode{};
    if (SDL_GetDesktopDisplayMode(0, &desktopMode) == 0 && desktopMode.h > 0) {
        displayAspect = static_cast<float>(desktopMode.w) / static_cast<float>(desktopMode.h);
    } else {
        std::memset(&desktopMode, 0, sizeof(desktopMode));
        displayAspect = 16.0f / 9.0f;
    }

    int mode = rMode->integer;
    int width = 0;
    int height = 0;
    float aspect = 1.0f;
    if (!ResolveModeDimensions(mode, rCustomWidth, rCustomHeight, rCustomPixelAspect, desktopMode,
            width, height, aspect)) {
        ri.Printf(PRINT_WARNING, "rendererqray: invalid r_mode %d, falling back to mode 3 (640x480)\n", mode);
        mode = 3;
        ResolveModeDimensions(mode, rCustomWidth, rCustomHeight, rCustomPixelAspect, desktopMode,
            width, height, aspect);
    }

    int x = SDL_WINDOWPOS_UNDEFINED;
    int y = SDL_WINDOWPOS_UNDEFINED;
    if (rCenterWindow->integer != 0 && !fullscreen && desktopMode.w > 0 && desktopMode.h > 0) {
        x = (desktopMode.w - width) / 2;
        y = (desktopMode.h - height) / 2;
    }

    Uint32 flags = SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN;
    if (rAllowResize->integer != 0) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (fullscreen) {
        flags |= (mode == -2) ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_FULLSCREEN;
    } else if (rNoborder->integer != 0) {
        flags |= SDL_WINDOW_BORDERLESS;
    }

    sdlWindow = SDL_CreateWindow("ioquake3 (QuakeRay)", x, y, width, height, flags);
    if (sdlWindow == nullptr) {
        ri.Printf(PRINT_WARNING, "SDL_CreateWindow(Vulkan) failed: %s\n", SDL_GetError());
        return false;
    }

    if (fullscreen && mode != -2) {
        SDL_DisplayMode desiredMode{};
        desiredMode.format = desktopMode.format;
        desiredMode.w = width;
        desiredMode.h = height;
        desiredMode.refresh_rate = rDisplayRefresh->integer;
        if (SDL_SetWindowDisplayMode(sdlWindow, &desiredMode) < 0) {
            ri.Printf(PRINT_DEVELOPER, "SDL_SetWindowDisplayMode failed: %s\n", SDL_GetError());
        }
    }

    int actualWidth = width;
    int actualHeight = height;
    SDL_GetWindowSize(sdlWindow, &actualWidth, &actualHeight);

    std::memset(config, 0, sizeof(*config));
    config->vidWidth = std::max(1, actualWidth);
    config->vidHeight = std::max(1, actualHeight);
    config->windowAspect = config->vidHeight > 0
        ? static_cast<float>(config->vidWidth) / static_cast<float>(config->vidHeight)
        : aspect;
    config->isFullscreen = fullscreen ? qtrue : qfalse;
    config->displayFrequency = desktopMode.refresh_rate > 0 ? desktopMode.refresh_rate : 60;
    config->colorBits = 24;
    config->depthBits = 24;
    config->stencilBits = 8;
    config->maxTextureSize = 4096;
    config->numTextureUnits = 4;
    config->driverType = GLDRV_ICD;
    config->hardwareType = GLHW_GENERIC;
    config->deviceSupportsGamma = qfalse;
    config->textureCompression = TC_NONE;
    config->textureEnvAddAvailable = qtrue;
    CopyWindowString(config->vendor_string, "QuakeRay", sizeof(config->vendor_string));
    CopyWindowString(config->renderer_string, "QuakeRay Vulkan RT", sizeof(config->renderer_string));
    CopyWindowString(config->version_string, "QuakeRaySDK 0.2.0", sizeof(config->version_string));

    DetectAvailableModes(ri);
    ri.IN_Init(sdlWindow);
    return true;
}

void QrayWindow_Shutdown(const refimport_t &ri)
{
    if (sdlWindow == nullptr) {
        return;
    }
    ri.IN_Shutdown();
    SDL_DestroyWindow(sdlWindow);
    sdlWindow = nullptr;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

bool QrayWindow_IsOpen()
{
    return sdlWindow != nullptr;
}

bool QrayWindow_GetNativeHandle(QrayNativeWindow *out)
{
    if (out == nullptr || sdlWindow == nullptr) {
        return false;
    }
    *out = QrayNativeWindow{};

    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(sdlWindow, &info)) {
        return false;
    }

#ifdef _WIN32
    if (info.subsystem == SDL_SYSWM_WINDOWS) {
        out->type = QrayNativeWindowType::Win32;
        out->display = info.info.win.hinstance;
        out->window = reinterpret_cast<unsigned long long>(info.info.win.window);
        return out->window != 0;
    }
#else
#ifdef SDL_VIDEO_DRIVER_X11
    if (info.subsystem == SDL_SYSWM_X11) {
        out->type = QrayNativeWindowType::X11;
        out->display = info.info.x11.display;
        out->window = static_cast<unsigned long long>(info.info.x11.window);
        return out->display != nullptr && out->window != 0;
    }
#endif
#ifdef SDL_VIDEO_DRIVER_WAYLAND
    if (info.subsystem == SDL_SYSWM_WAYLAND) {
        out->type = QrayNativeWindowType::Wayland;
        out->display = info.info.wl.display;
        out->window = reinterpret_cast<unsigned long long>(info.info.wl.surface);
        return false;
    }
#endif
#endif
    return false;
}

void QrayWindow_CheckFullscreenToggle(const refimport_t &ri)
{
    if (rFullscreen == nullptr || !rFullscreen->modified) {
        return;
    }
    rFullscreen->modified = qfalse;
    ri.Cmd_ExecuteText(EXEC_APPEND, "vid_restart\n");
}
