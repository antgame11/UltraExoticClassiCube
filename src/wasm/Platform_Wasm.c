#define CC_NO_UPDATER
#define CC_NO_DYNLIB
#define CC_NO_SOCKETS
#define CC_NO_THREADING

#include "../Stream.h"
#include "../ExtMath.h"
#include "../SystemFonts.h"
#include "../Funcs.h"
#include "../Window.h"
#include "../Utils.h"
#include "../Errors.h"
#include "../PackedCol.h"
#include "../Game.h"
#include "../Options.h"
#include "../Constants.h"

#include <stdlib.h>
#include <string.h>

#define WASM_EXPORT(name) __attribute__((export_name(name)))

const cc_result ReturnCode_FileShareViolation = 1000000000; /* not used, no filesystem */
const cc_result ReturnCode_FileNotFound     = ERR_NOT_SUPPORTED;
const cc_result ReturnCode_PathNotFound     = ERR_NOT_SUPPORTED;
const cc_result ReturnCode_DirectoryExists  = ERR_NOT_SUPPORTED;

const char* Platform_AppNameSuffix = " Wasm";
cc_uint8 Platform_Flags = PLAT_FLAG_SINGLE_PROCESS | PLAT_FLAG_APP_EXIT;
cc_bool  Platform_ReadonlyFilesystem = true;
#include "../_PlatformBase.h"


/*########################################################################################################################*
*------------------------------------------------------Logging/Time-------------------------------------------------------*
*#########################################################################################################################*/
/* There is no wall clock available in plain wasm, so time is tracked as a  */
/*  virtual clock that only advances by the deltaMs the host passes into    */
/*  wasm_tick() - this keeps the module needing zero imports from the host. */
static cc_uint64 virtualTimeUs;

cc_uint64 Stopwatch_Measure(void) {
	return virtualTimeUs;
}

cc_uint64 Stopwatch_ElapsedMicroseconds(cc_uint64 beg, cc_uint64 end) {
	if (end < beg) return 0;
	return end - beg;
}

/* Recent log messages, exposed to the host as a simple ring buffer instead of */
/*  requiring an imported logging function - see wasm_get_log/wasm_take_log.  */
#define LOG_BUF_SIZE 4096
static char logBuf[LOG_BUF_SIZE];
static int  logLen;

void Platform_Log(const char* msg, int len) {
	if (len > LOG_BUF_SIZE) len = LOG_BUF_SIZE;
	memcpy(logBuf, msg, len);
	logBuf[len] = '\n';
	logLen = len + 1;
}

WASM_EXPORT("wasm_get_log")
const char* wasm_get_log(void) { return logBuf; }
WASM_EXPORT("wasm_get_log_length")
int wasm_get_log_length(void) { return logLen; }

TimeMS DateTime_CurrentUTC(void) {
	return (virtualTimeUs / (1000 * 1000)) + UNIX_EPOCH_SECONDS;
}

void DateTime_CurrentLocal(struct cc_datetime* t) {
	memset(t, 0, sizeof(*t));
}


/*########################################################################################################################*
*-------------------------------------------------------Crash handling----------------------------------------------------*
*#########################################################################################################################*/
void CrashHandler_Install(void) { }

void Process_Abort2(cc_result result, const char* raw_msg) {
	Platform_LogConst(raw_msg);
	Window_Main.Exists = false;
	Game_Running       = false;
	__builtin_trap();
}


/*########################################################################################################################*
*-----------------------------------------------------Directory/File------------------------------------------------------*
*#########################################################################################################################*/
/* CC_BUILD_FILESYSTEM is undefined for this target - see Core.h. A host that wants */
/*  persistent storage or resource loading can add its own src/wasm/ backend files  */
/*  implementing these the same way, e.g. backed by a WASI preopened directory.     */
void Platform_EncodePath(cc_filepath* dst, const cc_string* path) {
	int len = String_CopyToRaw(dst->buffer, sizeof(dst->buffer) - 1, path);
	dst->buffer[len] = '\0';
}

void Platform_DecodePath(cc_string* dst, const cc_filepath* path) {
	String_AppendConst(dst, path->buffer);
}

void Directory_GetCachePath(cc_string* path) { }

cc_result Directory_Create2(const cc_filepath* path) { return ERR_NOT_SUPPORTED; }
int File_Exists(const cc_filepath* path) { return false; }
cc_result Directory_Enum(const cc_string* dirPath, void* obj, Directory_EnumCallback callback) { return ERR_NOT_SUPPORTED; }
cc_result File_Open(cc_file* file, const cc_filepath* path) { return ERR_NOT_SUPPORTED; }
cc_result File_Create(cc_file* file, const cc_filepath* path) { return ERR_NOT_SUPPORTED; }
cc_result File_OpenOrCreate(cc_file* file, const cc_filepath* path) { return ERR_NOT_SUPPORTED; }
cc_result File_Read(cc_file file, void* data, cc_uint32 count, cc_uint32* bytesRead) { return ERR_NOT_SUPPORTED; }
cc_result File_Write(cc_file file, const void* data, cc_uint32 count, cc_uint32* bytesWrote) { return ERR_NOT_SUPPORTED; }
cc_result File_Close(cc_file file) { return ERR_NOT_SUPPORTED; }
cc_result File_Seek(cc_file file, int offset, int seekType) { return ERR_NOT_SUPPORTED; }
cc_result File_Position(cc_file file, cc_uint32* pos) { return ERR_NOT_SUPPORTED; }
cc_result File_Length(cc_file file, cc_uint32* len) { return ERR_NOT_SUPPORTED; }


/*########################################################################################################################*
*--------------------------------------------------------Threading--------------------------------------------------------*
*#########################################################################################################################*/
void Thread_Sleep(cc_uint32 milliseconds) {
	/* No-op: the host paces calls to wasm_tick() itself, so blocking here */
	/*  would just stall whatever event loop is driving this module.      */
}


/*########################################################################################################################*
*--------------------------------------------------------Platform---------------------------------------------------------*
*#########################################################################################################################*/
void Platform_Init(void) { }
void Platform_Free(void) { }

cc_bool Platform_DescribeError(cc_result res, cc_string* dst) { return false; }

cc_bool Process_OpenSupported = false;
cc_result Process_StartOpen(const cc_string* args) { return ERR_NOT_SUPPORTED; }
cc_result Process_StartGame2(const cc_string* args, int numArgs) { return ERR_NOT_SUPPORTED; }

void Process_Exit(cc_result code) {
	Window_Main.Exists = false;
	Game_Running       = false;
}

cc_result Platform_Encrypt(const void* data, int len, cc_string* dst) { return ERR_NOT_SUPPORTED; }
cc_result Platform_Decrypt(const void* data, int len, cc_string* dst) { return ERR_NOT_SUPPORTED; }
cc_result Platform_GetEntropy(void* data, int len) { return ERR_NOT_SUPPORTED; }

int Platform_GetCommandLineArgs(int argc, STRING_REF char** argv, cc_string* args) { return 0; }
cc_result Platform_SetDefaultCurrentDirectory(int argc, char **argv) { return 0; }


/*########################################################################################################################*
*------------------------------------------------------Main driver--------------------------------------------------------*
*#########################################################################################################################*/
#include "../main_impl.h"

/* Sets up ClassiCube and jumps straight into singleplayer - this minimal target has no */
/*  networking, so the multiplayer server-list launcher UI is skipped entirely (see also */
/*  CC_DISABLE_LAUNCHER in Core.h), matching how other offline-only ports (GBA, N64) work. */
WASM_EXPORT("wasm_init")
void wasm_init(int width, int height) {
	Window_Main.Width  = width;
	Window_Main.Height = height;

	SetupProgram(0, NULL);
	Options_Get(LOPT_USERNAME, &Game_Username, DEFAULT_USERNAME);
	Game_Setup();
}

/* Advances the game by one frame. deltaMs is how long it's been since the previous  */
/*  call, in milliseconds - the host is entirely responsible for pacing this (e.g.   */
/*  via requestAnimationFrame in a browser, or a fixed-rate timer elsewhere).        */
/* Returns 0 once the game has closed itself (e.g. user quit) - the host should stop */
/*  calling wasm_tick after that; the module cannot be restarted afterwards.         */
WASM_EXPORT("wasm_tick")
int wasm_tick(double deltaMs) {
	if (deltaMs > 0) virtualTimeUs += (cc_uint64)(deltaMs * 1000.0);
	if (!Game_Running) return 0;

	Game_RenderFrame();
	if (Game_Running) return 1;

	Game_Free();
	Window_Free();
	return 0;
}
