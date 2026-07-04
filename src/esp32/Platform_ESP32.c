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

#include <errno.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"

const cc_result ReturnCode_FileShareViolation = 1000000000;
const cc_result ReturnCode_FileNotFound     = ENOENT;
const cc_result ReturnCode_PathNotFound     = ENOENT;
const cc_result ReturnCode_DirectoryExists  = EEXIST;

const char* Platform_AppNameSuffix = " ESP32";
cc_uint8 Platform_Flags = PLAT_FLAG_SINGLE_PROCESS | PLAT_FLAG_APP_EXIT;
cc_bool  Platform_ReadonlyFilesystem = false;
#include "../_PlatformBase.h"

#include "../main_impl.h"

static const char* TAG = "ClassiCubePlatform";

void game_task(void* pvParameters) {
    char* argv[] = { "ClassiCube", NULL };
    int argc = 1;
    
    ESP_LOGI(TAG, "Starting ClassiCube Game loop");
    SetupProgram(argc, argv);
    
    while (Window_Main.Exists) {
        RunGame();
    }
    
    Window_Free();
    ESP_LOGI(TAG, "ClassiCube finished. Restarting ESP32...");
    esp_restart();
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Initializing SPIFFS...");
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount/format SPIFFS: %s", esp_err_to_name(ret));
    } else {
        size_t total = 0, used = 0;
        esp_spiffs_info(conf.partition_label, &total, &used);
        ESP_LOGI(TAG, "SPIFFS Partition size: total: %d, used: %d", total, used);
    }

    // Launch game loop task on Core 0 (leaving Core 1 free for BLE/video interrupts)
    xTaskCreatePinnedToCore(game_task, "game_task", 65536, NULL, 5, NULL, 0);
}

/*########################################################################################################################*
*------------------------------------------------------Logging/Time-------------------------------------------------------*
*#########################################################################################################################*/
void Platform_Log(const char* msg, int len) {
    // Print directly to console stdout
    fwrite(msg, 1, len, stdout);
    putchar('\n');
}

TimeMS DateTime_CurrentUTC(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (cc_uint64)tv.tv_sec + UNIX_EPOCH_SECONDS;
}

void DateTime_CurrentLocal(struct cc_datetime* t) {
    struct timeval tv;
    struct tm loc_time;
    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &loc_time);

    t->year   = loc_time.tm_year + 1900;
    t->month  = loc_time.tm_mon  + 1;
    t->day    = loc_time.tm_mday;
    t->hour   = loc_time.tm_hour;
    t->minute = loc_time.tm_min;
    t->second = loc_time.tm_sec;
}

/*########################################################################################################################*
*--------------------------------------------------------Stopwatch--------------------------------------------------------*
*#########################################################################################################################*/
cc_uint64 Stopwatch_Measure(void) {
    return esp_timer_get_time();
}

cc_uint64 Stopwatch_ElapsedMicroseconds(cc_uint64 beg, cc_uint64 end) {
    if (end < beg) return 0;
    return end - beg;
}

/*########################################################################################################################*
*-----------------------------------------------------Directory/File------------------------------------------------------*
*#########################################################################################################################*/
void Platform_EncodePath(cc_filepath* dst, const cc_string* path) {
    char* str = dst->buffer;
    strcpy(str, "/spiffs/");
    String_EncodeUtf8(str + 8, path);
}

void Platform_DecodePath(cc_string* dst, const cc_filepath* path) {
    const char* str = path->buffer;
    // Skip /spiffs/ prefix when converting back to game path
    if (strncmp(str, "/spiffs/", 8) == 0) {
        str += 8;
    }
    String_AppendUtf8(dst, str, strlen(str));
}

void Directory_GetCachePath(cc_string* path) {
    String_AppendConst(path, "cache");
}

cc_result Directory_Create2(const cc_filepath* path) {
    // SPIFFS doesn't support directories natively, pretend they succeeded
    return 0;
}

int File_Exists(const cc_filepath* path) {
    struct stat sb;
    return stat(path->buffer, &sb) == 0 && S_ISREG(sb.st_mode);
}

cc_result Directory_Enum(const cc_string* dirPath, void* obj, Directory_EnumCallback callback) {
    cc_string path; char pathBuffer[FILENAME_SIZE];
    cc_filepath str;
    DIR* dirPtr;
    struct dirent* entry;
    char* src;
    int len, res, is_dir;

    Platform_EncodePath(&str, dirPath);
    dirPtr = opendir(str.buffer);
    if (!dirPtr) return errno;

    errno = 0;
    String_InitArray(path, pathBuffer);

    while ((entry = readdir(dirPtr))) {
        path.length = 0;
        String_Format1(&path, "%s/", dirPath);

        src = entry->d_name;
        if (src[0] == '.' && src[1] == '\0') continue;
        if (src[0] == '.' && src[1] == '.' && src[2] == '\0') continue;

        len = strlen(src);
        String_AppendUtf8(&path, src, len);

        is_dir = (entry->d_type == DT_DIR);
        callback(&path, obj, is_dir);
        errno = 0;
    }

    res = errno;
    closedir(dirPtr);
    return res;
}

static cc_result File_Do(cc_file* file, const char* path, const char* mode) {
    FILE* f = fopen(path, mode);
    if (!f) {
        return errno;
    }
    // Store pointer in cc_file (which is a typedef int, so we cast it to intptr)
    *file = (cc_file)(uintptr_t)f;
    return 0;
}

cc_result File_Open(cc_file* file, const cc_filepath* path) {
    return File_Do(file, path->buffer, "rb");
}

cc_result File_Create(cc_file* file, const cc_filepath* path) {
    return File_Do(file, path->buffer, "wb+");
}

cc_result File_OpenOrCreate(cc_file* file, const cc_filepath* path) {
    return File_Do(file, path->buffer, "ab+");
}

cc_result File_Read(cc_file file, void* data, cc_uint32 count, cc_uint32* bytesRead) {
    FILE* f = (FILE*)(uintptr_t)file;
    *bytesRead = fread(data, 1, count, f);
    if (*bytesRead == 0 && ferror(f)) {
        return errno;
    }
    return 0;
}

cc_result File_Write(cc_file file, const void* data, cc_uint32 count, cc_uint32* bytesWrote) {
    FILE* f = (FILE*)(uintptr_t)file;
    *bytesWrote = fwrite(data, 1, count, f);
    if (*bytesWrote == 0 && ferror(f)) {
        return errno;
    }
    return 0;
}

cc_result File_Close(cc_file file) {
    FILE* f = (FILE*)(uintptr_t)file;
    return fclose(f) == 0 ? 0 : errno;
}

cc_result File_Seek(cc_file file, int offset, int seekType) {
    static int modes[3] = { SEEK_SET, SEEK_CUR, SEEK_END };
    FILE* f = (FILE*)(uintptr_t)file;
    return fseek(f, offset, modes[seekType]) == 0 ? 0 : errno;
}

cc_result File_Position(cc_file file, cc_uint32* pos) {
    FILE* f = (FILE*)(uintptr_t)file;
    long p = ftell(f);
    if (p == -1) {
        return errno;
    }
    *pos = p;
    return 0;
}

cc_result File_Length(cc_file file, cc_uint32* len) {
    FILE* f = (FILE*)(uintptr_t)file;
    long curr = ftell(f);
    if (curr == -1) return errno;
    if (fseek(f, 0, SEEK_END) != 0) return errno;
    long size = ftell(f);
    if (size == -1) return errno;
    if (fseek(f, curr, SEEK_SET) != 0) return errno;
    *len = size;
    return 0;
}

/*########################################################################################################################*
*--------------------------------------------------------Threading--------------------------------------------------------*
*#########################################################################################################################*/
void Thread_Sleep(cc_uint32 milliseconds) {
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
}

/*########################################################################################################################*
*-------------------------------------------------------Crash handling----------------------------------------------------*
*#########################################################################################################################*/
void CrashHandler_Install(void) { }

void Process_Abort2(cc_result result, const char* raw_msg) {
    ESP_LOGE(TAG, "Aborting ClassiCube: %s (code %d)", raw_msg, result);
    esp_system_abort(raw_msg);
}

/*########################################################################################################################*
*--------------------------------------------------------Platform---------------------------------------------------------*
*#########################################################################################################################*/
void Platform_Init(void) { }

void Platform_Free(void) { }

cc_bool Platform_DescribeError(cc_result res, cc_string* dst) {
    char chars[NATIVE_STR_LEN];
    char* err = strerror_r(res, chars, NATIVE_STR_LEN);
    if (!err) return false;
    String_AppendUtf8(dst, err, strlen(err));
    return true;
}

/*########################################################################################################################*
*-----------------------------------------------------Process/Module------------------------------------------------------*
*#########################################################################################################################*/
cc_bool Process_OpenSupported = false;

cc_result Process_StartGame2(const cc_string* args, int numArgs) {
    return SetGameArgs(args, numArgs);
}

void Process_Exit(cc_result code) {
    ESP_LOGI(TAG, "Process exit called with code %d. Restarting...", code);
    esp_restart();
}

cc_result Process_StartOpen(const cc_string* args) {
    return ERR_NOT_SUPPORTED;
}

cc_result Platform_Encrypt(const void* data, int len, cc_string* dst) {
    return ERR_NOT_SUPPORTED;
}

cc_result Platform_Decrypt(const void* data, int len, cc_string* dst) {
    return ERR_NOT_SUPPORTED;
}

cc_result Platform_GetEntropy(void* data, int len) {
    return ERR_NOT_SUPPORTED;
}

int Platform_GetCommandLineArgs(int argc, STRING_REF char** argv, cc_string* args) {
    return 0;
}

cc_result Platform_SetDefaultCurrentDirectory(int argc, char **argv) {
    return 0;
}
