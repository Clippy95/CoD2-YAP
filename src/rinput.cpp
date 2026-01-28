#include "rinput.h"
#include <helper.hpp>
#include "component_loader.h"
#include <Hooking.Patterns.h>

#include <filesystem>
#include <fstream>
#include "nlohmann/json.hpp"

#include <optional>
#include <buildnumber.h>
#include "framework.h"

#include <MemoryMgr.h>
#include "..\hooking.h"
#include "game.h"
#include "dvars.h"


#include "shared.h"

// from CoD2x https://github.com/callofduty2x/CoD2x/blob/main/src/mss32/rinput.cpp

dvar_t* m_rinput;
dvar_t* m_rinput_hz;
dvar_t* m_rinput_hz_max;

CRITICAL_SECTION    rinput_lock;
long                rinput_x = 0;
long                rinput_y = 0;
long                rinput_cnt = 0;
long                rinput_activated = 0;
LPVOID              rinput_error = NULL;
HWND                rinput_window_hwnd = NULL;
DWORD               rinput_thread_id = 0;
HANDLE              rinput_thread_handle = NULL;

// Variables for measuring input rate
#define SAMPLE_COUNT 10  // 10 intervals of 100ms = 1 second
long                rinput_inputSamples[SAMPLE_COUNT] = { 0 };  // Store last 10 measurements
int                 rinput_sampleIndex = 0;
long                rinput_totalInputCount = 0;  // Rolling sum of last 10 updates
long                rinput_lastTotalCount = 0;  // Tracks total input messages seen so far
LONGLONG            rinput_frequency = 0;

#define win_hwnd    (*((HWND*)0x82D09C))



void rinput_wm_input(LPARAM lParam) {
    UINT uiSize = 40;
    static unsigned char lpb[40];

    if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, lpb, &uiSize, sizeof(RAWINPUTHEADER)) != (UINT)-1) {
        RAWINPUT* raw = (RAWINPUT*)lpb;

        if (raw->header.dwType == RIM_TYPEMOUSE) {
            // Extract raw mouse data
            int deltaX = raw->data.mouse.lLastX;
            int deltaY = raw->data.mouse.lLastY;

            // Offsets for the mouse movement in loop function
            EnterCriticalSection(&rinput_lock);
            rinput_x += deltaX;
            rinput_y += deltaY;
            rinput_cnt++;
            LeaveCriticalSection(&rinput_lock);
        }
    }
}


LRESULT __stdcall rinput_window_proc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INPUT: {
        rinput_wm_input(lParam);
        break;
    }
    default: return DefWindowProc(hWnd, message, wParam, lParam);
    }

    return 0;
}


// RInput has own dedicated thread to handle raw input messages
DWORD WINAPI rinput_thread(LPVOID lpParam) {

    // Register the window class
    tagWNDCLASSEXA wcex;
    memset(&wcex, 0, sizeof(WNDCLASSEX));
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.lpfnWndProc = rinput_window_proc;
    wcex.lpszClassName = "CoD2_Rinput";

    if (!RegisterClassExA(&wcex)) {
        InterlockedExchangePointer(&rinput_error, (PVOID)"Failed to register raw input device.\nFailed to register input class!");
        return 1;
    }

    // Create a hidden "fake" window
    rinput_window_hwnd = CreateWindowExA(0, "CoD2_Rinput", "CoD2_Rinput", 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL);
    if (!rinput_window_hwnd) {
        UnregisterClassA("CoD2_Rinput", NULL);
        InterlockedExchangePointer(&rinput_error, (PVOID)"Failed to register raw input device.\nFailed to create input window!");
        return 1;
    }

    // Register raw input for this window
    RAWINPUTDEVICE rid;
    rid.usUsagePage = 0x01;  // Generic desktop controls
    rid.usUsage = 0x02;      // Mouse
    rid.dwFlags = 0;         // Default behavior (foreground capture)
    rid.hwndTarget = rinput_window_hwnd;

    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        DestroyWindow(rinput_window_hwnd);
        rinput_window_hwnd = NULL;
        UnregisterClassA("CoD2_Rinput", NULL);
        InterlockedExchangePointer(&rinput_error, (PVOID)"Failed to register raw input device.");
        return 1;
    }

    InterlockedExchange(&rinput_activated, 1);

    // Message loop (dedicated to raw input)
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_QUIT) {
            break;
        }
        DispatchMessage(&msg);
    }

    // Destroy window and unregister class
    DestroyWindow(rinput_window_hwnd);
    rinput_window_hwnd = NULL;
    UnregisterClassA("CoD2_Rinput", NULL);

    return 0;
}




void rinput_register() {

    if (m_rinput->value.integer == m_rinput->latchedValue.integer && m_rinput->value.integer > 0) {
        return;
    }

    rinput_reset_offset();

    // Detection of Rinput software
    // If the game is already running with Rinput, don't allow to register another instance
    HWND hwndRInput = FindWindowA("Rinput", NULL);
    if (hwndRInput != NULL)
    {
        dvars::Dvar_SetBool(m_rinput, false);
        Com_Error(ERR_DROP, "Couldn't register raw input device.\nAnother instance of raw input is already running\n\nm_rinput was set to 0.");
        return;
    }

    // Method 1: Dedicated thread for raw input messages
    if (m_rinput->latchedValue.integer == 1) {
        InterlockedExchange(&rinput_activated, 0);
        InterlockedExchangePointer(&rinput_error, NULL);
        rinput_thread_handle = CreateThread(NULL, 0, rinput_thread, NULL, 0, &rinput_thread_id);

        // Wait for thread to activate, thread safely
        while (true) {
            Sleep(1);
            LPVOID error = InterlockedCompareExchangePointer(&rinput_error, NULL, NULL);
            if (error) {
                dvars::Dvar_SetBool(m_rinput, false);
                Com_Error(ERR_DROP, (const char*)error);
                return;
            }
            if (InterlockedCompareExchange(&rinput_activated, 0, 0) == 1) {
                break;
            }
        }

        Com_Printf("Registered raw input device (dedicated thread method)\n");


        // Method 2: Directly in the main game window
    }
    else if (m_rinput->latchedValue.integer == 2) {

        // Register raw input for the game window
        RAWINPUTDEVICE rid;
        rid.usUsagePage = 0x01;  // Generic desktop controls
        rid.usUsage = 0x02;      // Mouse
        rid.dwFlags = 0;         // Default behavior (foreground capture)
        rid.hwndTarget = win_hwnd;

        if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
            Com_Error(ERR_DROP, "Failed to register raw input device.");
            return;
        }

        Com_Printf("Registered raw input device (main window method)\n");
    }
}

void rinput_unregister() {

    if (m_rinput->value.integer == 0) {
        return;
    }

    // Method 1: Dedicated thread for raw input messages
    if (m_rinput->value.integer == 1) {
        // Post a quit message to break the message loop
        PostMessage(rinput_window_hwnd, WM_QUIT, 0, 0);

        // Wait for thread to exit
        WaitForSingleObject(rinput_thread_handle, INFINITE);

        CloseHandle(rinput_thread_handle);
        rinput_thread_handle = NULL;

        Com_Printf("Unregistered raw input device (dedicated thread method)\n");


        // Method 2: Directly in the main game window
    }
    else if (m_rinput->value.integer == 2) {

        // Unregister raw input for the game window
        RAWINPUTDEVICE rid;
        rid.usUsagePage = 0x01;  // Generic desktop controls
        rid.usUsage = 0x02;      // Mouse
        rid.dwFlags = RIDEV_REMOVE;
        rid.hwndTarget = win_hwnd;

        RegisterRawInputDevices(&rid, 1, sizeof(rid));

        Com_Printf("Unregistered raw input device (main window method)\n");
    }
}


void rinput_on_main_window_create() {
    // Keep latched and change current to 0 to detect a change in frame loop
    m_rinput->value.integer = 0;
}

void rinput_on_main_window_destory() {
    // In method 2 the main window is tight to the raw input
    // Since the HWND changed (due to vid_restart) the raw input is automatically unregistered
    // We need to also kill external thread in method 1
    if (m_rinput->value.integer > 0) {
        rinput_unregister();
        m_rinput->latchedValue.integer = m_rinput->value.integer;
        m_rinput->value.integer = 0;
    }
}


bool rinput_is_enabled() {
    return m_rinput->value.integer > 0;
}

void rinput_get_last_offset(long* x, long* y) {
    EnterCriticalSection(&rinput_lock);
    *x = rinput_x;
    *y = rinput_y;
    rinput_x = 0;
    rinput_y = 0;
    LeaveCriticalSection(&rinput_lock);
}

void rinput_reset_offset() {
    EnterCriticalSection(&rinput_lock);
    rinput_x = 0;
    rinput_y = 0;
    LeaveCriticalSection(&rinput_lock);
}


void rinput_mouse_loop() {
    // If the raw input cvar has been modified
    if (m_rinput->latchedValue.integer != m_rinput->value.integer) {

        dvars::Dvar_SetInt(m_rinput_hz, 0);
        dvars::Dvar_SetInt(m_rinput_hz_max, 0);

        // Unregister first
        rinput_unregister();

        if (m_rinput->latchedValue.integer > 0) {
            rinput_register();
        }
        dvars::Dvar_SetInt(m_rinput, m_rinput->latchedValue.integer);
    }

    if (m_rinput->value.integer > 0) {

        static LONGLONG lastUpdateTime = 0;

        // Get current time
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        long elapsedTime = (now.QuadPart - lastUpdateTime) * 1000 / rinput_frequency;

        // Check if 100ms have passed
        if (elapsedTime >= 100) {
            EnterCriticalSection(&rinput_lock);

            // Get the new raw input messages received since last update
            long newMessages = rinput_cnt - rinput_lastTotalCount;
            rinput_lastTotalCount = rinput_cnt;  // Update tracker

            // Subtract the oldest sample from the total and add the latest one
            rinput_totalInputCount -= rinput_inputSamples[rinput_sampleIndex];
            rinput_inputSamples[rinput_sampleIndex] = newMessages;  // Store new sample
            rinput_totalInputCount += newMessages;  // Update total

            // Move to next sample slot
            rinput_sampleIndex = (rinput_sampleIndex + 1) % SAMPLE_COUNT;

            // If no movement is detected, clear the buffer
            if (newMessages == 0) {
                rinput_totalInputCount = 0;  // Force zero to stop displaying old values
                memset(rinput_inputSamples, 0, sizeof(rinput_inputSamples));  // Clear stored samples
            }

            LeaveCriticalSection(&rinput_lock);

            // Display the updated input rate
            dvars::Dvar_SetInt(m_rinput_hz, rinput_totalInputCount);

            // Update max input rate
            if (rinput_totalInputCount > m_rinput_hz_max->value.integer) {
                dvars::Dvar_SetInt(m_rinput_hz_max, rinput_totalInputCount);
            }

            lastUpdateTime = now.QuadPart;
        }
    }
}




/** Called only once on game start after common initialization. Used to initialize variables, cvars, etc. */
void rinput_init() {
    m_rinput = dvars::Dvar_RegisterInt("m_rinput", 0, 0, 2, (enum dvarFlags_e)(DVAR_ARCHIVE | DVAR_LATCH | DVAR_CHANGEABLE_RESET),"");

    m_rinput_hz = dvars::Dvar_RegisterInt("m_rinput_hz", 0, 0, INT32_MAX, (enum dvarFlags_e)(DVAR_ROM | DVAR_CHANGEABLE_RESET));
    m_rinput_hz_max = dvars::Dvar_RegisterInt("m_rinput_hz_max", 0, 0, INT32_MAX, (enum dvarFlags_e)(DVAR_ROM | DVAR_CHANGEABLE_RESET));
}

/** Called before the entry point is called. Used to patch the memory. */
void rinput_patch() {
    InitializeCriticalSection(&rinput_lock);

    // Initialize time measurement
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    rinput_frequency = freq.QuadPart;
    QueryPerformanceCounter(&now);
}


enum clientState_e {
    CLIENT_STATE_DISCONNECTED,    // not talking to a server
    CLIENT_STATE_CINEMATIC,       // playing a cinematic or a static pic, not connected to a server
    CLIENT_STATE_AUTHORIZING,     // not used any more, was checking cd key
    CLIENT_STATE_CONNECTING,      // sending request packets to the server
    CLIENT_STATE_CHALLENGING,     // sending challenge packets to the server
    CLIENT_STATE_CONNECTED,       // netchan_t established, getting gamestate
    CLIENT_STATE_LOADING,         // only during cgame initialization, never during main loop
    CLIENT_STATE_PRIMED,          // got gamestate, waiting for first frame
    CLIENT_STATE_ACTIVE,          // game views should be displayed       
};

#define vid_xpos                    (*((dvar_t**)0x82D0B8))
#define vid_ypos                    (*((dvar_t**)0x82D0B0))
#define r_fullscreen                (*((dvar_t**)0x82D088))
#define r_autopriority              (*((dvar_t**)0x82D090))
#define in_mouse                    (*((dvar_t**)0x825BB4))
//#define cl_bypassMouseInput         (*((dvar_t**)0x006067d0))   // 1 = control ingame mouse movement even if menu is opened




#define mouse_center_x              (*((int*)0x825BC4))
#define mouse_center_y              (*((int*)0x825BBC))
#define mouse_windowIsActive        (*((int*)0x825BC8)) // true when window is activated (in foreground, not minimized)
#define mouse_ingameMouseActive     (*((bool*)0x825BD0))
#define mouse_isEnabled             (*((bool*)0x825BD1))
#define mouse_offset_x_arr          (((int*)0x621538))
#define mouse_offset_y_arr          (((int*)0x621540))
#define mouse_offset_index          (*((int*)0x621548))

#define menu_cursorX                (*((int*)0x10FEC3C))
#define menu_cursorY                (*((int*)0x10FEC40))
#define menu_opened                 (*((bool*)0x10FED5C))


#define win_isActivated             (*((int*)0x82D0A4))
#define win_isMinimized             (*((int*)0x82D0A8))
#define win_wheelRool               (*((UINT*)0x82D08C))

#define input_mode                  (*((int*)0x617F04))
#define clientState                 (*((clientState_e*)0x5D5680))

dvar_t* m_debug;
dvar_t* m_enable = NULL;    // 1 = allow moving ingame and menu mouse cursor; 0 = bypass mouse movement, but still process the system cursor and mouse events



bool in_menu_last = true;

void Mouse_SetMenuCursorPos(int x, int y) {

    menu_cursorX = x;
    menu_cursorY = y;

    if (menu_opened) {
        // Original function to process mouse movement
        ((void(__cdecl*)(void* data, void* arg2, int32_t x, int32_t y))0x4D0060)(((void*)0x010FEC30), nullptr, x, y);
    }
}

void Mouse_DeactivateIngameCursor()
{
    if (!mouse_isEnabled || !mouse_ingameMouseActive)
        return;

    mouse_ingameMouseActive = 0;
    ReleaseCapture();

    while (ShowCursor(TRUE) < 0); // show system cursor
}

void Mouse_ActivateIngameCursor()
{
    SetCapture(win_hwnd);

    struct tagRECT rect;
    GetWindowRect(win_hwnd, &rect);

    mouse_center_x = (rect.right + rect.left) / 2;
    mouse_center_y = (rect.bottom + rect.top) / 2;

    while (ShowCursor(FALSE) >= 0); // hide system cursor
}

namespace rinput {

    void window_init() {
        // We need to register cvars here to fix the issue caused by registering cvars in renderer that are also referenced in CoD2MP_s.exe
        // Cvars that get first registered in renderer DLL have strings allocated in renderer DLL, so when vid_restart is called, the renderer DLL is unloaded and the strings are freed.
        // So we need to register the cvars in this DLL where the strings will be allocated for the whole time of the game.
        vid_xpos = dvars::Dvar_RegisterInt("vid_xpos", 3, -100000, 100000, (enum dvarFlags_e)(DVAR_ARCHIVE | DVAR_CHANGEABLE_RESET));
        vid_ypos = dvars::Dvar_RegisterInt("vid_ypos", 22, -100000, 100000, (enum dvarFlags_e)(DVAR_ARCHIVE | DVAR_CHANGEABLE_RESET));
        r_fullscreen = dvars::Dvar_RegisterBool("r_fullscreen", true, (enum dvarFlags_e)(DVAR_ARCHIVE | DVAR_LATCH | DVAR_CHANGEABLE_RESET));
        r_autopriority = dvars::Dvar_RegisterBool("r_autopriority", false, (enum dvarFlags_e)(DVAR_ARCHIVE | DVAR_CHANGEABLE_RESET));

        m_enable = dvars::Dvar_RegisterBool("m_enable", true, (enum dvarFlags_e)(DVAR_CHANGEABLE_RESET));
        m_debug = dvars::Dvar_RegisterBool("m_debug", false, (enum dvarFlags_e)(DVAR_CHANGEABLE_RESET));
    }

    void Mouse_ProcessMovement() {

        POINT cursorPoint;
        GetCursorPos(&cursorPoint);

        // Get the cursor position relative to the window client area (inner window)
        POINT cursorRelativePoint = cursorPoint;
        ScreenToClient(win_hwnd, &cursorRelativePoint);

        RECT clientRect;
        GetClientRect(win_hwnd, &clientRect); // Get the inner area of the window

        // in_menu = if we control menu cursor
        bool in_menu = ((input_mode & 8) != 0 && m_enable->value.boolean) || clientState < 4;
        bool menu_changed = in_menu != in_menu_last;
        in_menu_last = in_menu;

        // Windowed menu
        if (in_menu && !r_fullscreen->value.boolean) {

            if (menu_changed) {
                // Set cursor position to top left corner of the window
                cursorPoint.x -= cursorRelativePoint.x;
                cursorPoint.y -= cursorRelativePoint.y;
                // If the cursor is outside the window, set it to the center
                if (menu_cursorX < 0 || menu_cursorX > 640 || menu_cursorY < 0 || menu_cursorY > 480) {
                    menu_cursorX = 320;
                    menu_cursorY = 240;
                }
                // Get last menu cursor and translate it to monitor coordinates
                cursorPoint.x += clientRect.left + (menu_cursorX * (clientRect.right - clientRect.left)) / 640;
                cursorPoint.y += clientRect.top + (menu_cursorY * (clientRect.bottom - clientRect.top)) / 480;

                SetCursorPos(cursorPoint.x, cursorPoint.y);
                return;
            }

            // Check if rect has some size (might return 0 if the window is minimized)
            if (clientRect.right - clientRect.left <= 0 || clientRect.bottom - clientRect.top <= 0) {
                Mouse_DeactivateIngameCursor();
                return;
            }

            // Scale the cursor position to the game window resolution
            // So if width is 1280 and cursor is at 1280, it will be 640 in 640x480 resolution
            int newMenuX = (cursorRelativePoint.x * 640) / (clientRect.right - clientRect.left);
            int newMenuY = (cursorRelativePoint.y * 480) / (clientRect.bottom - clientRect.top);

            // Cursor is outside window
            bool isInsideWindow = newMenuX >= 0 && newMenuX <= 640 && newMenuY >= 0 && newMenuY <= 480;
            if (isInsideWindow && !mouse_ingameMouseActive) {
                Mouse_ActivateIngameCursor(); // activate ingame cursor, hide system cursor
                mouse_ingameMouseActive = 1;
                if (m_debug->value.boolean)
                    Com_Printf("Hiding system cursor\n");
            }
            else if (!isInsideWindow && mouse_ingameMouseActive) {
                Mouse_DeactivateIngameCursor(); // deactivate ingame cursor, show system cursor
                mouse_ingameMouseActive = 0;
                if (m_debug->value.boolean)
                    Com_Printf("Show system cursor\n");
            }

            bool isCloseToWindow = newMenuX >= -20 && newMenuX <= 660 && newMenuY >= -20 && newMenuY <= 500;
            if (m_debug->value.boolean && (menu_cursorX != newMenuX || menu_cursorY != newMenuY) && isCloseToWindow) {
                Com_Printf("New menu cursor: (%d %d) (source: system)\n", newMenuX, newMenuY);
            }

            Mouse_SetMenuCursorPos(newMenuX, newMenuY);


            // Fullscreen menu or windowed game, use centering offset method
        }
        else {

            // Menu has been opened or closed
            if (menu_changed) {
                // In windowed mode the system cursor is not in center, avoid artificial movement
                if (!r_fullscreen->value.boolean) {
                    cursorPoint.x = mouse_center_x;
                    cursorPoint.y = mouse_center_y;
                }
            }

            // If the window is not active, deactivate the ingame cursor if it's active
            if (!mouse_windowIsActive || GetForegroundWindow() != win_hwnd)
            {
                Mouse_DeactivateIngameCursor();
                return;
            }

            // Activate the ingame cursor if it's not active yet
            if (mouse_ingameMouseActive == 0) {

                // Cursor is outside the inner area of the window
                if (r_fullscreen->value.boolean == false) {
                    bool cursorIsOutside =
                        cursorRelativePoint.x <= clientRect.left || cursorRelativePoint.x >= clientRect.right ||
                        cursorRelativePoint.y <= clientRect.top || cursorRelativePoint.y >= clientRect.bottom;
                    if (cursorIsOutside)
                        return;
                }

                Mouse_ActivateIngameCursor();

                SetCursorPos(mouse_center_x, mouse_center_y);

                mouse_ingameMouseActive = 1;
                return;
            }

            int32_t x_offset = (cursorPoint.x - mouse_center_x);
            int32_t y_offset = (cursorPoint.y - mouse_center_y);

            // When raw input is enabled, use the raw input offsets
            if (rinput_is_enabled())
            {
                long rinput_x, rinput_y;
                rinput_get_last_offset(&rinput_x, &rinput_y);
                x_offset = rinput_x;
                y_offset = rinput_y;
            }

            SetCursorPos(mouse_center_x, mouse_center_y);

            if (m_debug->value.boolean && (x_offset != 0 || y_offset != 0))
                Com_Printf("Mouse move offset: (%d %d) (source: %s)\n", x_offset, y_offset, rinput_is_enabled() ? "rinput" : "system");

            // Mouse movement is bypassed
            if (!m_enable->value.boolean) {
                return;
            }

            if (in_menu) {

                int newMenuX = menu_cursorX + x_offset;
                int newMenuY = menu_cursorY + y_offset;

                // Make sure the cursor is inside the window
                if (newMenuX < 0) newMenuX = 0;
                else if (newMenuX > 640) newMenuX = 640;
                if (newMenuY < 0) newMenuY = 0;
                else if (newMenuY > 480) newMenuY = 480;

                Mouse_SetMenuCursorPos(newMenuX, newMenuY);

                // In game
            }
            else {
                // 2 dimensional array of mouse offsets used for mouse filtering and game movement
                // index is being swapped between 0 and 1
                mouse_offset_x_arr[mouse_offset_index] += x_offset;
                mouse_offset_y_arr[mouse_offset_index] += y_offset;
            }
        }
    }

    SafetyHookInline MouseLoopog;
    char MouseLoop() {
        if (win_hwnd) {
            rinput_mouse_loop();
        }

        if (mouse_isEnabled)
        {
            Mouse_ProcessMovement();
        }

        return 1;

    }

    LRESULT CALLBACK CoD2WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        bool callOriginal = true;

        switch (uMsg)
        {

        case WM_CREATE: {
            win_hwnd = hwnd;
            win_wheelRool = RegisterWindowMessageA("MSWHEEL_ROLLMSG");

            rinput_on_main_window_create();

            callOriginal = false;
            break;
        }

                      // Called when the window is moved
        case WM_MOVE: {
            if (r_fullscreen->value.boolean == 0) // Windowed mode
            {
                RECT lpRect;
                lpRect.left = 0;
                lpRect.top = 0;
                lpRect.right = 1;
                lpRect.bottom = 1;
                // Get window X and Y - lpRect.left and lpRect.top will contain negative values representing the size of the window borders and title bar.
                AdjustWindowRect(&lpRect, GetWindowLongA(hwnd, GWL_STYLE), 0);

                int xPos = (int)(short)LOWORD(lParam); // X-coordinate
                int yPos = (int)(short)HIWORD(lParam); // Y-coordinate
                RECT rect;
                if (GetWindowRect(hwnd, &rect)) { // Full 32-bit coordinate possible on high-DPI screens
                    xPos = rect.left;
                    yPos = rect.top;
                }

                dvars::Dvar_SetInt(vid_xpos, xPos);
                dvars::Dvar_SetInt(vid_ypos, yPos);

                vid_xpos->modified = false;
                vid_ypos->modified = false;

                if (win_isActivated)
                    mouse_windowIsActive = 1;

            }

            // Original function also contained logic to handle hiding the cursor, thats removed now

            callOriginal = false;

            break;
        }

        case WM_DESTROY: {

            rinput_on_main_window_destory();

            win_hwnd = NULL;

            callOriginal = false;
            break;
        }


        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        {
            // In windowed borderless mode the menu cursor is based on the system cursor.
            // Since we are hiding the system cursor, it wont automatically activate the window when clicking, so do it manually
            if (r_fullscreen->value.boolean == 0 && !win_isActivated) {
                SetForegroundWindow(hwnd);
            }
            break;
        }

        case WM_ACTIVATE: {
            bool bActivated = LOWORD(wParam) != WA_INACTIVE; // was activated (WA_ACTIVE or WA_CLICKACTIVE, not WA_INACTIVE)
            bool bMinimized = HIWORD(wParam) != 0;

            win_isMinimized = bMinimized;

            // Something with bindlist
            ((void (*)())0x40C600)();

            if (bActivated && bMinimized == 0) {
                win_isActivated = 1;
                ((void (*)())0x4269E0)(); // Com_TouchMemory();


            }
            else {
                win_isActivated = 0;


            }

            mouse_windowIsActive = win_isActivated;

            if (!win_isActivated)
                Mouse_DeactivateIngameCursor();

            // Original function also contained logic to handle hiding the cursor, thats removed now as its handled in Mouse_Loop

            callOriginal = false;

            break;
        }
        case WM_INPUT: {
            rinput_wm_input(lParam);
            break;
        }

        }

        if (!callOriginal) {
            return DefWindowProcA(hwnd, uMsg, wParam, lParam);
        }

        // Call the original function
        LRESULT ret = ((LRESULT(CALLBACK*)(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam))0x451DC0)(hwnd, uMsg, wParam, lParam);

        return ret;
    }

    class component final : public component_interface
    {
    public:
        void post_unpack() override {
            if (!exe(1))
                return;
            window_init();
            rinput_init();


        }
        void post_start() override {
            if (!exe(1))
                return;

            Memory::VP::Patch<void*>(exe(0x00450861 + 4), CoD2WindowProc);
            rinput_patch();
            MouseLoopog = safetyhook::create_inline(0x44F0A0, MouseLoop);


            static auto on_WM_DESTROY = safetyhook::create_mid(0x451F01, [](SafetyHookContext& ctx) {
                rinput_on_main_window_destory();
                });

            Memory::VP::Nop(0x451F38, 5);
            Memory::VP::Nop(0x451F1E, 5);

            static auto on_WM_INPUT = safetyhook::create_mid(0x451F01, [](SafetyHookContext& ctx) {
                rinput_on_main_window_destory();
                });

            static auto on_WM_ACTIVATE = safetyhook::create_mid(0x451F10, [](SafetyHookContext& ctx) {
                WPARAM wParam = (ctx.ebx);
                bool bActivated = LOWORD(wParam) != WA_INACTIVE; // was activated (WA_ACTIVE or WA_CLICKACTIVE, not WA_INACTIVE)
                bool bMinimized = HIWORD(wParam) != 0;

                win_isMinimized = bMinimized;

                if (bActivated && bMinimized == 0) {
                    win_isActivated = 1;
                    ((void (*)())0x4269E0)(); // Com_TouchMemory();

                }
                else {
                    win_isActivated = 0;

                }
                mouse_windowIsActive = win_isActivated;

                if (!win_isActivated)
                    Mouse_DeactivateIngameCursor();

                });

        }


    };
}
REGISTER_COMPONENT(rinput::component);