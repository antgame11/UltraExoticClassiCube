#define CC_NO_UPDATER
#define CC_NO_DYNLIB
#define CC_NO_SOCKETS
#define CC_NO_THREADING

#include "../Stream.h"
#include "../ExtMath.h"
#include "../Funcs.h"
#include "../Window.h"
#include "../Utils.h"
#include "../Errors.h"
#include "../Options.h"
#include "../Animations.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "ti84defs.h"
#include <debug.h> // Debug console output support for emulators (like CEmu)

const cc_result ReturnCode_FileShareViolation = 1000000000; // Not used
const cc_result ReturnCode_FileNotFound     = -1;
const cc_result ReturnCode_PathNotFound     = -1;
const cc_result ReturnCode_DirectoryExists  = -1;

const char* Platform_AppNameSuffix = " TI84";
cc_bool Platform_ReadonlyFilesystem = true;
cc_uint8 Platform_Flags = PLAT_FLAG_SINGLE_PROCESS | PLAT_FLAG_APP_EXIT;
#include "../_PlatformBase.h"

/*########################################################################################################################*
*-----------------------------------------------------Main entrypoint-----------------------------------------------------*
*#########################################################################################################################*/
#include "../main_impl.h"

int main(int argc, char** argv) {
    SetupProgram(argc, argv);
    while (Window_Main.Exists) { 
        RunGame();
    }
    
    Window_Free();
    return 0;
}

/*########################################################################################################################*
*---------------------------------------------------------Memory----------------------------------------------------------*
*#########################################################################################################################*/
void* Mem_TryAlloc(cc_uint32 numElems, cc_uint32 elemsSize) {
    cc_uint32 size = CalcMemSize(numElems, elemsSize);
    return size ? malloc(size) : NULL;
}

void* Mem_TryAllocCleared(cc_uint32 numElems, cc_uint32 elemsSize) {
    cc_uint32 size = CalcMemSize(numElems, elemsSize);
    return size ? calloc(numElems, elemsSize) : NULL;
}

void* Mem_TryRealloc(void* mem, cc_uint32 numElems, cc_uint32 elemsSize) {
    cc_uint32 size = CalcMemSize(numElems, elemsSize);
    return size ? realloc(mem, size) : NULL;
}

void Mem_Free(void* mem) {
    if (mem) free(mem);
}

/*########################################################################################################################*
*------------------------------------------------------Logging/Time-------------------------------------------------------*
*#########################################################################################################################*/
void Platform_Log(const char* msg, int len) {
    /* Write to the emulator debug console if connected */
    dbg_write(msg, len);
    dbg_write("\n", 1);
}

cc_uint64 Stopwatch_Measure(void) {
    return (cc_uint64)clock();
}

cc_uint64 Stopwatch_ElapsedMicroseconds(cc_uint64 beg, cc_uint64 end) {
    if (end < beg) return 0;
    return ((end - beg) * 1000000) / CLOCKS_PER_SEC;
}

TimeMS DateTime_CurrentUTC(void) {
    return (TimeMS)rtc_Time(NULL) * 1000;
}

void DateTime_CurrentLocal(struct cc_datetime* t) {
    time_t rawtime = rtc_Time(NULL);
    struct tm* timeinfo = localtime(&rawtime);
    if (!timeinfo) {
        Mem_Set(t, 0, sizeof(*t));
        return;
    }
    t->year = timeinfo->tm_year + 1900;
    t->month = timeinfo->tm_mon + 1;
    t->day = timeinfo->tm_mday;
    t->hour = timeinfo->tm_hour;
    t->minute = timeinfo->tm_min;
    t->second = timeinfo->tm_sec;
}

/*########################################################################################################################*
*-------------------------------------------------------Crash handling----------------------------------------------------*
*#########################################################################################################################*/
void CrashHandler_Install(void) { }

void Process_Abort2(cc_result result, const char* raw_msg) {
    Platform_LogConst(raw_msg);
    Process_Exit(0);
}

/*########################################################################################################################*
*-----------------------------------------------------Directory/File------------------------------------------------------*
*#########################################################################################################################*/
void Platform_EncodePath(cc_filepath* dst, const cc_string* path) {
    int len = String_CopyToRaw(dst->buffer, sizeof(dst->buffer) - 1, path);
    dst->buffer[len] = '\0';
}

void Platform_DecodePath(cc_string* dst, const cc_filepath* path) {
    String_AppendConst(dst, path->buffer);
}

void Directory_GetCachePath(cc_string* path) { }

cc_result Directory_Create2(const cc_filepath* path) {
    return ReturnCode_DirectoryExists;
}

int File_Exists(const cc_filepath* path) {
    return false;
}

cc_result Directory_Enum(const cc_string* dirPath, void* obj, Directory_EnumCallback callback) {
    return ERR_NOT_SUPPORTED;
}

cc_result File_Open(cc_file* file, const cc_filepath* path) {
    return ERR_NOT_SUPPORTED;
}

cc_result File_Create(cc_file* file, const cc_filepath* path) {
    return ERR_NOT_SUPPORTED;
}

cc_result File_OpenOrCreate(cc_file* file, const cc_filepath* path) {
    return ERR_NOT_SUPPORTED;
}

cc_result File_Read(cc_file file, void* data, cc_uint32 count, cc_uint32* bytesRead) {
    return ERR_NOT_SUPPORTED;
}

cc_result File_Write(cc_file file, const void* data, cc_uint32 count, cc_uint32* bytesWrote) {
    return ERR_NOT_SUPPORTED;
}

cc_result File_Close(cc_file file) {
    return ERR_NOT_SUPPORTED;
}

cc_result File_Seek(cc_file file, int offset, int seekType) {
    return ERR_NOT_SUPPORTED;
}

cc_result File_Position(cc_file file, cc_uint32* pos) {
    return ERR_NOT_SUPPORTED;
}

cc_result File_Length(cc_file file, cc_uint32* len) {
    return ERR_NOT_SUPPORTED;
}

/*########################################################################################################################*
*--------------------------------------------------------Threading--------------------------------------------------------*
*#########################################################################################################################*/
void Thread_Sleep(cc_uint32 milliseconds) {
    clock_t goal = clock() + (milliseconds * CLOCKS_PER_SEC) / 1000;
    while (clock() < goal) {
        /* Busy wait */
    }
}

/*########################################################################################################################*
*--------------------------------------------------------Platform---------------------------------------------------------*
*#########################################################################################################################*/
void Platform_Init(void) { }

void Platform_Free(void) { }

cc_bool Platform_DescribeError(cc_result res, cc_string* dst) {
    return false;
}

cc_bool Process_OpenSupported = false;
cc_result Process_StartOpen(const cc_string* args) {
    return ERR_NOT_SUPPORTED;
}

cc_result Platform_Encrypt(const void* data, int len, cc_string* dst) {
    return ERR_NOT_SUPPORTED;
}

cc_result Platform_Decrypt(const void* data, int len, cc_string* dst) {
    return ERR_NOT_SUPPORTED;
}

/*########################################################################################################################*
*-----------------------------------------------------Process/Module------------------------------------------------------*
*#########################################################################################################################*/
cc_result Process_StartGame2(const cc_string* args, int numArgs) {
    return 0;
}

int Platform_GetCommandLineArgs(int argc, STRING_REF char** argv, cc_string* args) {
    return 0;
}

cc_result Platform_SetDefaultCurrentDirectory(int argc, char **argv) { 
    return 0; 
}

void Process_Exit(cc_result code) { 
    /* Close graphics mode and return to TI-OS */
    gfx_End();
    exit(0);
}
