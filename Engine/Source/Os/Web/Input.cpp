// wow335modding: browser build of Os/Win64/Input.cpp. Page events (keyboard on the window; mouse and wheel on the
// canvas; focus, close) are handled on the page's main thread and pushed into the same OSEVENT queue the
// Windows window procedure fills, with the same parameters, so the client on its worker thread sees no difference.
// The first click captures the pointer (a pointer lock) and keeps it, so the browser shows its "press Esc" notice once
// rather than at every camera turn. While captured the client's cursor (it draws its own) moves by the pointer's
// movement, and relative mode (camera turning) passes that movement on instead. Esc or leaving the tab frees it.
#if defined(WOWDECOMP_PLATFORM_WEB)

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <emscripten/html5.h>
#include <pthread.h>
#include <windows.h>
#include "Engine/Source/Os/Input.h"
#include "Engine/Source/Os/TimeManager.h"

#define CANVAS "#canvas"

namespace {

pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
OSEVENT s_queue[64];
int32_t s_queueHead;
int32_t s_queueTail;
bool s_keyDown[KEY_LAST + 1];
uint32_t s_mouseButtons;
int32_t s_mouseX;
int32_t s_mouseY;
OS_MOUSE_MODE s_mouseMode = OS_MOUSE_MODE_NORMAL;
bool s_captured;
float s_mouseSpeed = 1.0f;

struct Lock {
    Lock() { pthread_mutex_lock(&s_lock); }
    ~Lock() { pthread_mutex_unlock(&s_lock); }
};

// Called with s_lock held. A full queue drops its oldest event, like the Windows queue.
void Put(OSINPUT id, int32_t param0, int32_t param1, int32_t param2) {
    int32_t next = (s_queueTail + 1) % 64;
    if (next == s_queueHead) {
        s_queueHead = (s_queueHead + 1) % 64;
    }

    OSEVENT& event = s_queue[s_queueTail];
    event.id = id;
    event.param[0] = param0;
    event.param[1] = param1;
    event.param[2] = param2;
    event.param[3] = static_cast<int32_t>(OsGetAsyncTimeMs());
    s_queueTail = next;
}

struct KeyName {
    const char* code;
    KEY key;
};

// KeyboardEvent.code (the physical key) to the client's KEY. Letters, digits, numpad digits and F-keys are computed.
const KeyName s_keys[] = {
    { "ShiftLeft", KEY_LSHIFT }, { "ShiftRight", KEY_RSHIFT }, { "ControlLeft", KEY_LCONTROL }, { "ControlRight", KEY_RCONTROL },
    { "AltLeft", KEY_LALT }, { "AltRight", KEY_RALT }, { "Space", KEY_SPACE }, { "Backquote", KEY_TILDE },
    { "Minus", KEY_MINUS }, { "Equal", KEY_PLUS }, { "BracketLeft", KEY_BRACKET_OPEN }, { "BracketRight", KEY_BRACKET_CLOSE },
    { "Slash", KEY_SLASH }, { "Backslash", KEY_BACKSLASH }, { "Semicolon", KEY_SEMICOLON }, { "Quote", KEY_APOSTROPHE },
    { "Comma", KEY_COMMA }, { "Period", KEY_PERIOD }, { "Escape", KEY_ESCAPE }, { "Enter", KEY_ENTER },
    { "NumpadEnter", KEY_ENTER }, { "Backspace", KEY_BACKSPACE }, { "Tab", KEY_TAB }, { "ArrowLeft", KEY_LEFT },
    { "ArrowUp", KEY_UP }, { "ArrowRight", KEY_RIGHT }, { "ArrowDown", KEY_DOWN }, { "Insert", KEY_INSERT },
    { "Delete", KEY_DELETE }, { "Home", KEY_HOME }, { "End", KEY_END }, { "PageUp", KEY_PAGEUP },
    { "PageDown", KEY_PAGEDOWN }, { "CapsLock", KEY_CAPSLOCK }, { "NumLock", KEY_NUMLOCK }, { "ScrollLock", KEY_SCROLLLOCK },
    { "Pause", KEY_PAUSE }, { "PrintScreen", KEY_PRINTSCREEN }, { "NumpadAdd", KEY_NUMPAD_PLUS },
    { "NumpadSubtract", KEY_NUMPAD_MINUS }, { "NumpadMultiply", KEY_NUMPAD_MULTIPLY }, { "NumpadDivide", KEY_NUMPAD_DIVIDE },
    { "NumpadDecimal", KEY_NUMPAD_DECIMAL }, { "NumpadEqual", KEY_NUMPAD_EQUALS },
};

bool ConvertKey(const char* code, KEY* key) {
    if (!strncmp(code, "Key", 3) && code[3] >= 'A' && code[3] <= 'Z' && !code[4]) {
        *key = static_cast<KEY>(code[3]);
        return true;
    }
    if (!strncmp(code, "Digit", 5) && code[5] >= '0' && code[5] <= '9' && !code[6]) {
        *key = static_cast<KEY>(code[5]);
        return true;
    }
    if (!strncmp(code, "Numpad", 6) && code[6] >= '0' && code[6] <= '9' && !code[7]) {
        *key = static_cast<KEY>(KEY_NUMPAD0 + (code[6] - '0'));
        return true;
    }
    if (code[0] == 'F' && code[1] >= '1' && code[1] <= '9') {
        int32_t number = atoi(code + 1);
        if (number >= 1 && number <= 12) {
            *key = static_cast<KEY>(KEY_F1 + number - 1);
            return true;
        }
    }
    for (const KeyName& name : s_keys) {
        if (!strcmp(code, name.code)) {
            *key = name.key;
            return true;
        }
    }
    return false;
}

// The code point of a KeyboardEvent.key that is one printable character, else 0.
uint32_t PrintableCharacter(const char* text) {
    const uint8_t* s = reinterpret_cast<const uint8_t*>(text);
    uint32_t c;
    int32_t length;
    if (s[0] < 0x80) {
        c = s[0], length = 1;
    } else if ((s[0] & 0xE0) == 0xC0) {
        c = s[0] & 0x1F, length = 2;
    } else if ((s[0] & 0xF0) == 0xE0) {
        c = s[0] & 0x0F, length = 3;
    } else if ((s[0] & 0xF8) == 0xF0) {
        c = s[0] & 0x07, length = 4;
    } else {
        return 0;
    }
    for (int32_t i = 1; i < length; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            return 0;
        }
        c = (c << 6) | (s[i] & 0x3F);
    }
    return !s[length] && c >= 0x20 && c != 0x7F ? c : 0;
}

EM_BOOL OnKey(int eventType, const EmscriptenKeyboardEvent* event, void*) {
    KEY key;
    bool down = eventType == EMSCRIPTEN_EVENT_KEYDOWN;
    {
        Lock lock;
        if (ConvertKey(event->code, &key)) {
            s_keyDown[key] = down;
            Put(down ? OS_INPUT_KEY_DOWN : OS_INPUT_KEY_UP, key, 1, 0);
        }
        uint32_t character = down && !event->ctrlKey && !event->metaKey ? PrintableCharacter(event->key) : 0;
        if (character) {
            Put(OS_INPUT_CHAR, static_cast<int32_t>(character), 1, 0);
        }
    }
    // keep F5 (reload), F11 (full screen) and F12 (developer tools) for the browser; the game gets everything else
    return strcmp(event->code, "F5") && strcmp(event->code, "F11") && strcmp(event->code, "F12");
}

// Canvas pixels per CSS pixel. The page sizes the canvas in display pixels (Tools/WebClient/index.html) while events
// arrive in CSS pixels; the client, like on Windows, works in the pixels it renders.
double CanvasScale() {
    int width = 0;
    int height = 0;
    double cssWidth = 0.0;
    double cssHeight = 0.0;
    emscripten_get_canvas_element_size(CANVAS, &width, &height);
    emscripten_get_element_css_size(CANVAS, &cssWidth, &cssHeight);
    return cssWidth > 0.0 ? width / cssWidth : 1.0;
}

MOUSEBUTTON ConvertButton(unsigned short button) {
    switch (button) {
        case 0: return MOUSE_BUTTON_LEFT;
        case 1: return MOUSE_BUTTON_MIDDLE;
        case 2: return MOUSE_BUTTON_RIGHT;
        case 3: return MOUSE_BUTTON_XBUTTON1;
        case 4: return MOUSE_BUTTON_XBUTTON2;
        default: return MOUSE_BUTTON_NONE;
    }
}

EM_BOOL OnMouse(int eventType, const EmscriptenMouseEvent* event, void*) {
    Lock lock;

    if (eventType == EMSCRIPTEN_EVENT_MOUSEMOVE) {
        if (s_mouseMode == OS_MOUSE_MODE_RELATIVE) {
            Put(OS_INPUT_MOUSE_MOVE_RELATIVE, 0, static_cast<int32_t>(event->movementX * s_mouseSpeed), static_cast<int32_t>(event->movementY * s_mouseSpeed));
        } else if (s_captured) {
            double scale = CanvasScale();
            int width = 0;
            int height = 0;
            emscripten_get_canvas_element_size(CANVAS, &width, &height);
            s_mouseX = std::clamp(s_mouseX + static_cast<int32_t>(event->movementX * scale), 0, width - 1);
            s_mouseY = std::clamp(s_mouseY + static_cast<int32_t>(event->movementY * scale), 0, height - 1);
            Put(OS_INPUT_MOUSE_MOVE, 0, s_mouseX, s_mouseY);
        } else {
            double scale = CanvasScale();
            s_mouseX = static_cast<int32_t>(event->targetX * scale);
            s_mouseY = static_cast<int32_t>(event->targetY * scale);
            Put(OS_INPUT_MOUSE_MOVE, 0, s_mouseX, s_mouseY);
        }
        return EM_TRUE;
    }

    MOUSEBUTTON button = ConvertButton(event->button);
    if (button == MOUSE_BUTTON_NONE) {
        return EM_FALSE;
    }

    if (eventType == EMSCRIPTEN_EVENT_MOUSEDOWN) {
        if (!s_captured) {
            emscripten_request_pointerlock(CANVAS, true);
        }
        s_mouseButtons |= button;
        Put(OS_INPUT_MOUSE_DOWN, button, s_mouseX, s_mouseY);
    } else {
        s_mouseButtons &= ~button;
        Put(OS_INPUT_MOUSE_UP, button, s_mouseX, s_mouseY);
    }
    return EM_TRUE;
}

EM_BOOL OnWheel(int, const EmscriptenWheelEvent* event, void*) {
    Lock lock;
    if (event->deltaY != 0) {
        Put(OS_INPUT_MOUSE_WHEEL, event->deltaY < 0 ? 120 : -120, s_mouseX, s_mouseY); // one notch, Windows sign
    }
    return EM_TRUE;
}

EM_BOOL OnFocus(int eventType, const EmscriptenFocusEvent*, void*) {
    Lock lock;
    bool active = eventType == EMSCRIPTEN_EVENT_FOCUS;
    if (!active) {
        memset(s_keyDown, 0, sizeof(s_keyDown)); // the key-ups for held keys go to whatever got the focus
        s_mouseButtons = 0;
    }
    Put(OS_INPUT_FOCUS, active, 0, 0);
    return EM_FALSE;
}

const char* OnBeforeUnload(int, const void*, void*) {
    Lock lock;
    Put(OS_INPUT_CLOSE, 0, 0, 0);
    return nullptr;
}

EM_BOOL OnPointerLockChange(int, const EmscriptenPointerlockChangeEvent* event, void*) {
    Lock lock;
    s_captured = event->isActive;
    return EM_FALSE;
}

EM_BOOL OnContextMenu(int, const EmscriptenMouseEvent*, void*) {
    return EM_TRUE; // right-click belongs to the game
}

}

void OsQueuePut(OSINPUT id, int32_t param0, int32_t param1, int32_t param2, int32_t) {
    Lock lock;
    Put(id, param0, param1, param2);
}

int32_t OsQueueGet(OSINPUT* id, int32_t* param0, int32_t* param1, int32_t* param2, int32_t* param3) {
    Lock lock;
    if (s_queueHead == s_queueTail) {
        return 0;
    }

    OSEVENT& event = s_queue[s_queueHead];
    *id = event.id;
    *param0 = event.param[0];
    *param1 = event.param[1];
    *param2 = event.param[2];
    *param3 = event.param[3];
    s_queueHead = (s_queueHead + 1) % 64;
    return 1;
}

void OsInputInitialize() {
    pthread_t main = EM_CALLBACK_THREAD_CONTEXT_MAIN_RUNTIME_THREAD;
    emscripten_set_keydown_callback_on_thread(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, OnKey, main);
    emscripten_set_keyup_callback_on_thread(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, OnKey, main);
    emscripten_set_mousedown_callback_on_thread(CANVAS, nullptr, true, OnMouse, main);
    emscripten_set_mouseup_callback_on_thread(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, OnMouse, main);
    emscripten_set_mousemove_callback_on_thread(CANVAS, nullptr, true, OnMouse, main);
    emscripten_set_wheel_callback_on_thread(CANVAS, nullptr, true, OnWheel, main);
    emscripten_set_focus_callback_on_thread(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, OnFocus, main);
    emscripten_set_blur_callback_on_thread(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, OnFocus, main);
    emscripten_set_beforeunload_callback_on_thread(nullptr, OnBeforeUnload, main);
    emscripten_set_contextmenu_callback_on_thread(CANVAS, nullptr, true, OnContextMenu, main);
    emscripten_set_pointerlockchange_callback_on_thread(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, true, OnPointerLockChange, main);
}

void OsInputDestroy() {
    emscripten_html5_remove_all_event_listeners();
}

int32_t OsInputGet(OSINPUT* id, int32_t* param0, int32_t* param1, int32_t* param2, int32_t* param3) {
    *id = static_cast<OSINPUT>(-1);
    return OsQueueGet(id, param0, param1, param2, param3);
}

int32_t OsInputIsKeyDown(KEY key) {
    Lock lock;
    return key >= 0 && key <= KEY_LAST && s_keyDown[key];
}

int32_t OsInputIsMouseButtonDown(MOUSEBUTTON button) {
    Lock lock;
    return (s_mouseButtons & button) != 0;
}

// Any click captures the pointer (OnMouse), so a camera turn only changes what its movement does. Uncaptured (the
// browser refused, or right after Esc) the turn still works from the plain movement, until the screen edge.
void OsInputSetMouseMode(OS_MOUSE_MODE mode) {
    Lock lock;
    s_mouseMode = mode;
}

void OsInputSetMouseSpeed(float speed) {
    Lock lock;
    s_mouseSpeed = speed;
}

float OsInputGetMouseSpeed() {
    Lock lock;
    return s_mouseSpeed;
}

void OsInputGetMousePosition(int32_t* x, int32_t* y) {
    Lock lock;
    *x = s_mouseX;
    *y = s_mouseY;
}

// A page cannot move the pointer; the client's idea of where it is moves, the real pointer stays.
void OsInputSetMousePosition(int32_t x, int32_t y) {
    Lock lock;
    s_mouseX = x;
    s_mouseY = y;
}

int32_t OsGetDefaultWindowRect(tagRECT* rect) {
    int width = 0;
    int height = 0;
    emscripten_get_canvas_element_size(CANVAS, &width, &height);
    rect->left = 0;
    rect->top = 0;
    rect->right = width;
    rect->bottom = height;
    return 1;
}

void OsInputSetEventPollProc(void (*)()) {
}

void OsInputSetWindowResizeLock(int32_t) {
}

void OsInputEnableSecureMode(int32_t) {
}

intptr_t OsWindowProc(void*, uint32_t, uintptr_t, intptr_t) {
    return 0;
}

#endif
