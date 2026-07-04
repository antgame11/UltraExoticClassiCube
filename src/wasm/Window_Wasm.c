#include "../Window.h"
#include "../Platform.h"
#include "../Input.h"
#include "../Event.h"
#include "../Graphics.h"
#include "../String_.h"
#include "../Funcs.h"
#include "../Bitmap.h"
#include "../Errors.h"

#include <stdlib.h>

/*
This backend targets plain/generic WebAssembly rather than any one specific
wasm runtime - it makes no assumptions about WASI, Emscripten, or any other
host environment being present.

The only contract with the host is:
 - the module exports "memory" (standard for wasm32)
 - the host calls the wasm_* functions exported from this file and Platform_Wasm.c
 - pixels are read directly out of the framebuffer returned by wasm_get_framebuffer(),
    which is BitmapCol width*height*4 bytes, laid out row-major as R,G,B,A bytes
    (i.e. it can be wrapped directly in a browser's ImageData without conversion)
 - key/mouse button codes are exactly the CCKEY_/CCMOUSE_/CCWHEEL_ values from
    enum InputButtons in Input.h

See misc/wasm/README.md for the full list of exports and an example host.
*/
#define WASM_EXPORT(name) __attribute__((export_name(name)))

struct _DisplayData DisplayInfo;
struct cc_window WindowInfo;

static BitmapCol* fb_ptr;


/*########################################################################################################################*
*---------------------------------------------------Window APIs implementation---------------------------------------------*
*#########################################################################################################################*/
void Window_PreInit(void) { }

void Window_Init(void) {
	/* WindowInfo.Width/Height are set by wasm_init (see Platform_Wasm.c) before this runs */
	if (WindowInfo.Width  <= 0) WindowInfo.Width  = 320;
	if (WindowInfo.Height <= 0) WindowInfo.Height = 240;

	DisplayInfo.Width  = WindowInfo.Width;
	DisplayInfo.Height = WindowInfo.Height;
	DisplayInfo.Depth  = 32;
	DisplayInfo.ScaleX = 1.0f;
	DisplayInfo.ScaleY = 1.0f;

	WindowInfo.Focused  = true;
	WindowInfo.Exists   = true;
	WindowInfo.UIScaleX = DEFAULT_UI_SCALE_X;
	WindowInfo.UIScaleY = DEFAULT_UI_SCALE_Y;
}

void Window_Free(void) { }

void Window_Create2D(int width, int height) { WindowInfo.Is3D = false; }
void Window_Create3D(int width, int height) { WindowInfo.Is3D = true;  }
void Window_Destroy(void) { }

void Window_SetTitle(const cc_string* title) { }
void Clipboard_GetText(cc_string* value) { }
void Clipboard_SetText(const cc_string* value) { }

int Window_GetWindowState(void) { return WINDOW_STATE_FULLSCREEN; }
cc_result Window_EnterFullscreen(void) { return 0; }
cc_result Window_ExitFullscreen(void)  { return 0; }
int Window_IsObscured(void)            { return 0; }

void Window_Show(void) { }
void Window_SetSize(int width, int height) { }

void Window_RequestClose(void) {
	Event_RaiseVoid(&WindowEvents.Closing);
}

void Window_ProcessEvents(float delta) {
	/* Input arrives synchronously via the exported wasm_* functions below, */
	/*  called directly by the host as it happens - nothing to poll here.  */
}

void Cursor_SetPosition(int x, int y) { }
void Window_EnableRawMouse(void)  { Input.RawMode = true;  }
void Window_DisableRawMouse(void) { Input.RawMode = false; }
void Window_UpdateRawMouse(void)  { }

void Gamepads_PreInit(void) { }
void Gamepads_Init(void)    { }
void Gamepads_Process(float delta) { }

void Window_AllocFramebuffer(struct Bitmap* bmp, int width, int height) {
	fb_ptr = (BitmapCol*)malloc((size_t)width * height * sizeof(BitmapCol));
	if (!fb_ptr) Process_Abort("Failed to allocate framebuffer");

	bmp->scan0  = fb_ptr;
	bmp->width  = width;
	bmp->height = height;
}

void Window_DrawFramebuffer(Rect2D r, struct Bitmap* bmp) {
	/* Nothing to do - the host pulls pixels straight out of linear memory */
	/*  via wasm_get_framebuffer() whenever it wants to present a frame.   */
}

void Window_FreeFramebuffer(struct Bitmap* bmp) {
	free(fb_ptr);
	fb_ptr = NULL;
	bmp->scan0 = NULL;
}

void OnscreenKeyboard_Open(struct OpenKeyboardArgs* args) { }
void OnscreenKeyboard_SetText(const cc_string* text) { }
void OnscreenKeyboard_Close(void) { }

void Window_ShowDialog(const char* title, const char* msg) {
	Platform_LogConst(title);
	Platform_LogConst(msg);
}

cc_result Window_OpenFileDialog(const struct OpenFileDialogArgs* args) { return ERR_NOT_SUPPORTED; }
cc_result Window_SaveFileDialog(const struct SaveFileDialogArgs* args) { return ERR_NOT_SUPPORTED; }


/*########################################################################################################################*
*----------------------------------------------------Host-facing input ABI-------------------------------------------------*
*#########################################################################################################################*/
/* Sets pressed/released state of the given key. 'key' is a CCKEY_/CCMOUSE_/CCWHEEL_ value from Input.h */
WASM_EXPORT("wasm_key_event")
void wasm_key_event(int key, int pressed) {
	Input_Set(key, pressed);
}

/* Raises a unicode character being typed (e.g. for chat input) */
WASM_EXPORT("wasm_char_event")
void wasm_char_event(int codepoint) {
	Event_RaiseInt(&InputEvents.Press, codepoint);
}

/* Moves the mouse/pointer to the given position, in framebuffer pixel coordinates */
WASM_EXPORT("wasm_mouse_move")
void wasm_mouse_move(int x, int y) {
	Pointer_SetPosition(0, x, y);
}

/* Relative mouse motion (e.g. movementX/Y from a browser's Pointer Lock API) - */
/*  this is what actually drives in-game camera look, since that only pays     */
/*  attention to raw deltas rather than absolute cursor position.              */
/*  Call this alongside wasm_mouse_move whenever both are available.           */
WASM_EXPORT("wasm_mouse_delta")
void wasm_mouse_delta(float dx, float dy) {
	if (Input.RawMode) Event_RaiseRawMove(&PointerEvents.RawMoved, dx, dy);
}

/* Sets pressed/released state of a mouse button (CCMOUSE_L/CCMOUSE_R/CCMOUSE_M/etc) */
WASM_EXPORT("wasm_mouse_button")
void wasm_mouse_button(int key, int pressed) {
	Input_SetNonRepeatable(key, pressed);
}

/* Scrolls the mouse wheel. Positive delta scrolls up */
WASM_EXPORT("wasm_mouse_wheel")
void wasm_mouse_wheel(float delta) {
	Mouse_ScrollVWheel(delta);
}

/* Returns a pointer (byte offset into "memory") to the current framebuffer */
/*  pixels, width*height*4 bytes, row-major R,G,B,A. Valid until the next   */
/*  wasm_tick() call, since the game can reallocate it at any time.        */
WASM_EXPORT("wasm_get_framebuffer")
void* wasm_get_framebuffer(void) {
	return fb_ptr;
}

WASM_EXPORT("wasm_get_fb_width")
int wasm_get_fb_width(void) {
	return WindowInfo.Width;
}

WASM_EXPORT("wasm_get_fb_height")
int wasm_get_fb_height(void) {
	return WindowInfo.Height;
}
