#pragma once

extern "C" {
#include "q_shared.h"
#ifndef USE_RENDERER_DLOPEN
#define USE_RENDERER_DLOPEN
#endif
#include "tr_public.h"
}

enum class QrayNativeWindowType
{
    None,
    Win32,
    X11,
    Wayland,
};

struct QrayNativeWindow
{
    QrayNativeWindowType type = QrayNativeWindowType::None;
    void *display = nullptr;
    unsigned long long window = 0;
};

bool QrayWindow_Init(const refimport_t &ri, glconfig_t *config);
void QrayWindow_Shutdown(const refimport_t &ri);
bool QrayWindow_IsOpen();
bool QrayWindow_GetNativeHandle(QrayNativeWindow *out);
void QrayWindow_CheckFullscreenToggle(const refimport_t &ri);
