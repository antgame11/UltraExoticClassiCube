#include "../Window.h"
#include "../Platform.h"
#include "../Input.h"
#include "../Event.h"
#include "../Graphics.h"
#include "../String_.h"
#include "../Funcs.h"
#include "../Bitmap.h"
#include "../Errors.h"
#include "../ExtMath.h"
#include "../Camera.h"

#include <stdint.h>
#include <stdlib.h>
#include "ti84defs.h"

#define SCREEN_WIDTH  80
#define SCREEN_HEIGHT 60

struct _DisplayData DisplayInfo;
struct cc_window WindowInfo;

void Window_PreInit(void) { }

void Window_Init(void) {
    /* Initialize GraphX graphics library */
    gfx_Begin();
    
    DisplayInfo.Width  = SCREEN_WIDTH;
    DisplayInfo.Height = SCREEN_HEIGHT;
    DisplayInfo.ScaleX = 0.25f;
    DisplayInfo.ScaleY = 0.25f;
    
    Window_Main.Width    = DisplayInfo.Width;
    Window_Main.Height   = DisplayInfo.Height;
    Window_Main.Focused  = true;
    Window_Main.Exists   = true;
    Window_Main.UIScaleX = DEFAULT_UI_SCALE_X;
    Window_Main.UIScaleY = DEFAULT_UI_SCALE_Y;
}

void Window_Free(void) {
    gfx_End();
}

void Window_Create2D(int width, int height) { }
void Window_Create3D(int width, int height) { }
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

void Window_ProcessEvents(float delta) { }

void Cursor_SetPosition(int x, int y) { }
void Window_EnableRawMouse(void)  { Input.RawMode = true;  }
void Window_DisableRawMouse(void) { Input.RawMode = false; }
void Window_UpdateRawMouse(void)  { }

/*########################################################################################################################*
*-------------------------------------------------------Gamepads----------------------------------------------------------*
*#########################################################################################################################*/    
static const BindMapping pad_defaults[BIND_COUNT] = {
    [BIND_LOOK_UP]      = { CCPAD_2, CCPAD_UP },
    [BIND_LOOK_DOWN]    = { CCPAD_2, CCPAD_DOWN },
    [BIND_LOOK_LEFT]    = { CCPAD_2, CCPAD_LEFT },
    [BIND_LOOK_RIGHT]   = { CCPAD_2, CCPAD_RIGHT },
    [BIND_FORWARD]      = { CCPAD_UP,    0 },  
    [BIND_BACK]         = { CCPAD_DOWN,  0 },
    [BIND_LEFT]         = { CCPAD_LEFT,  0 },  
    [BIND_RIGHT]        = { CCPAD_RIGHT, 0 },
    [BIND_JUMP]         = { CCPAD_1, 0 },
    [BIND_INVENTORY]    = { CCPAD_START, 0 },
    [BIND_PLACE_BLOCK]  = { CCPAD_L, 0 },
    [BIND_DELETE_BLOCK] = { CCPAD_R, 0 }
};

void Gamepads_PreInit(void) { }
void Gamepads_Init(void)    { }

void Gamepads_Process(float delta) {
    int port = Gamepad_Connect(0x5BA, pad_defaults);
    kb_Scan();
    
    /* Map arrow keys for movement */
    Gamepad_SetButton(port, CCPAD_UP,    kb_Data[7] & kb_Up);
    Gamepad_SetButton(port, CCPAD_DOWN,  kb_Data[7] & kb_Down);
    Gamepad_SetButton(port, CCPAD_LEFT,  kb_Data[7] & kb_Left);
    Gamepad_SetButton(port, CCPAD_RIGHT, kb_Data[7] & kb_Right);
    
    /* Action mappings */
    Gamepad_SetButton(port, CCPAD_1,     kb_Data[1] & kb_2nd);   // Jump / Action
    Gamepad_SetButton(port, CCPAD_L,     kb_Data[6] & kb_Enter); // Place block
    Gamepad_SetButton(port, CCPAD_R,     kb_Data[1] & kb_Del);   // Delete block
    
    /* Pressing Mode key will request closing the game window */
    if (kb_Data[1] & kb_Mode) {
        Window_RequestClose();
    }
}

/*########################################################################################################################*
*------------------------------------------------------Framebuffer--------------------------------------------------------*
*#########################################################################################################################*/
void Window_AllocFramebuffer(struct Bitmap* bmp, int width, int height) {
    bmp->scan0  = (BitmapCol*)malloc(width * height * sizeof(BitmapCol));
    bmp->width  = width;
    bmp->height = height;
}

void Window_DrawFramebuffer(Rect2D r, struct Bitmap* bmp) {
    /* Pixel-quadruple 80x60 backbuffer to the 320x240 calculator VRAM screen */
    uint16_t* src = (uint16_t*)bmp->scan0;
    uint16_t* dst = (uint16_t*)gfx_vram;
    
    for (int y = 0; y < 60; y++) {
        uint16_t* src_row = src + y * 80;
        for (int yy = 0; yy < 4; yy++) {
            uint16_t* dst_row = dst + (4 * y + yy) * 320;
            for (int x = 0; x < 80; x++) {
                uint16_t color = src_row[x];
                dst_row[4 * x]     = color;
                dst_row[4 * x + 1] = color;
                dst_row[4 * x + 2] = color;
                dst_row[4 * x + 3] = color;
            }
        }
    }
}

void Window_FreeFramebuffer(struct Bitmap* bmp) {
    if (bmp->scan0) {
        free(bmp->scan0);
        bmp->scan0 = NULL;
    }
}

/*########################################################################################################################*
*------------------------------------------------------Soft keyboard------------------------------------------------------*
*#########################################################################################################################*/
void OnscreenKeyboard_Open(struct OpenKeyboardArgs* args) { }
void OnscreenKeyboard_SetText(const cc_string* text) { }
void OnscreenKeyboard_Close(void) { }

/*########################################################################################################################*
*-------------------------------------------------------Misc/Other--------------------------------------------------------*
*#########################################################################################################################*/
void Window_ShowDialog(const char* title, const char* msg) {
    Platform_LogConst(title);
    Platform_LogConst(msg);
}

cc_result Window_OpenFileDialog(const struct OpenFileDialogArgs* args) {
    return ERR_NOT_SUPPORTED;
}

cc_result Window_SaveFileDialog(const struct SaveFileDialogArgs* args) {
    return ERR_NOT_SUPPORTED;
}
