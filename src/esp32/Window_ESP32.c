#include "../Window.h"
#include "../Platform.h"
#include "../Input.h"
#include "../Event.h"
#include "../Graphics.h"
#include "../String_.h"
#include "../Funcs.h"
#include "../Bitmap.h"
#include "../Errors.h"

#include <string.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include <esp_http_server.h>

#define SCREEN_WIDTH  400
#define SCREEN_HEIGHT 400

struct _DisplayData DisplayInfo;
struct cc_window WindowInfo;

static const char* TAG = "ClassiCubeWindow";
static BitmapCol* fb_ptr = NULL;
static uint16_t* display_buf = NULL; // last complete frame, 200x200 RGB565
static SemaphoreHandle_t display_mutex = NULL;

// Wi-Fi Configuration - Add your home Wi-Fi SSID and Password here to connect to it!
// If left empty or "YOUR_SSID", it will start an Access Point "ClassiCube-ESP32" instead.
#define WIFI_SSID ""
#define WIFI_PASSWORD ""

/*########################################################################################################################*
*---------------------------------------------------Wi-Fi & Web Server----------------------------------------------------*
*#########################################################################################################################*/
static httpd_handle_t server = NULL;
static int ws_client_fd = -1;

#define MAX_EVENTS 64
struct WebEvent {
    uint8_t type;
    uint8_t key;
    uint16_t x;
    uint16_t y;
};
static struct WebEvent event_queue[MAX_EVENTS];
static int event_head = 0;
static int event_tail = 0;

static void queue_event(uint8_t type, uint8_t key, uint16_t x, uint16_t y) {
    int next = (event_tail + 1) % MAX_EVENTS;
    if (next != event_head) {
        event_queue[event_tail].type = type;
        event_queue[event_tail].key = key;
        event_queue[event_tail].x = x;
        event_queue[event_tail].y = y;
        event_tail = next;
    }
}

// WebSocket handler
static esp_err_t ws_handler(httpd_req_t *req) {
    ws_client_fd = httpd_req_to_sockfd(req);

    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Handshake done, new WS client connection");
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_BINARY;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get length: %d", ret);
        return ret;
    }

    if (ws_pkt.len > 0) {
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory for WS packet");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed: %d", ret);
            free(buf);
            return ret;
        }

        if (ws_pkt.len >= 6) {
            uint8_t type = buf[0];
            uint8_t key = buf[1];
            uint16_t x = (buf[2] << 8) | buf[3];
            uint16_t y = (buf[4] << 8) | buf[5];
            queue_event(type, key, x, y);
        }
        free(buf);
    }
    return ESP_OK;
}

// HTTP GET handler for "/" (loads the HTML webpage)
static esp_err_t index_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    extern const char index_html_start[] asm("_binary_index_html_start");
    extern const char index_html_end[]   asm("_binary_index_html_end");
    const size_t index_html_len = index_html_end - index_html_start;
    return httpd_resp_send(req, index_html_start, index_html_len);
}

static httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting HTTP server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t index_uri = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = index_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &index_uri);

        httpd_uri_t ws_uri = {
            .uri       = "/ws",
            .method    = HTTP_GET,
            .handler   = ws_handler,
            .user_ctx  = NULL,
            .is_websocket = true
        };
        httpd_register_uri_handler(server, &ws_uri);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Disconnected from Wi-Fi. Retrying to connect...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "=======================================================");
        ESP_LOGI(TAG, "CONNECTED TO WI-FI NETWORK!");
        ESP_LOGI(TAG, "Open http://" IPSTR " in your browser to play!", IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "=======================================================");
    }
}

static void wifi_init(void) {
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
    }
    
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGD(TAG, "esp_event_loop_create_default already created: %s", esp_err_to_name(err));
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    if (strlen(WIFI_SSID) > 0 && strcmp(WIFI_SSID, "YOUR_SSID") != 0) {
        // Station Mode
        esp_netif_create_default_wifi_sta();
        
        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            &wifi_event_handler,
                                                            NULL,
                                                            NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                            IP_EVENT_STA_GOT_IP,
                                                            &wifi_event_handler,
                                                            NULL,
                                                            NULL));

        wifi_config_t wifi_config = {
            .sta = {
                .ssid = WIFI_SSID,
                .password = WIFI_PASSWORD,
            },
        };
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_LOGI(TAG, "Connecting to Wi-Fi SSID: %s...", WIFI_SSID);
    } else {
        // SoftAP Mode
        esp_netif_create_default_wifi_ap();

        wifi_config_t wifi_config = {
            .ap = {
                .ssid = "ClassiCube-ESP32",
                .ssid_len = strlen("ClassiCube-ESP32"),
                .channel = 1,
                .password = "",
                .max_connection = 4,
                .authmode = WIFI_AUTH_OPEN
            },
        };

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());

        ESP_LOGI(TAG, "=======================================================");
        ESP_LOGI(TAG, "Wi-Fi SoftAP started. SSID: ClassiCube-ESP32");
        ESP_LOGI(TAG, "Connect to 'ClassiCube-ESP32' and open http://192.168.4.1 in your browser to play!");
        ESP_LOGI(TAG, "=======================================================");
    }
}

static void video_task(void* pvParameters) {
    wifi_init();
    start_webserver();

    display_buf = calloc(200 * 200, sizeof(uint16_t));
    if (!display_buf) {
        ESP_LOGE(TAG, "Failed to allocate display buffer");
        vTaskDelete(NULL);
        return;
    }
    display_mutex = xSemaphoreCreateMutex();

    uint16_t* send_buf = malloc(200 * 200 * sizeof(uint16_t));
    if (!send_buf) {
        ESP_LOGE(TAG, "Failed to allocate send buffer");
        free(display_buf);
        display_buf = NULL;
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        if (ws_client_fd != -1) {
            // Copy the last complete frame under the lock (fast memcpy, no tearing)
            if (xSemaphoreTake(display_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                memcpy(send_buf, display_buf, 200 * 200 * sizeof(uint16_t));
                xSemaphoreGive(display_mutex);
            }

            // Stream over WebSocket in fragments to prevent TCP buffer overflow
            int total_size = 200 * 200 * sizeof(uint16_t); // 80000 bytes
            int chunk_size = 8000;
            int total_chunks = total_size / chunk_size;

            for (int i = 0; i < total_chunks; i++) {
                httpd_ws_frame_t ws_frame = {
                    .final = (i == total_chunks - 1),
                    .fragmented = true,
                    .type = (i == 0) ? HTTPD_WS_TYPE_BINARY : HTTPD_WS_TYPE_CONTINUE,
                    .payload = ((uint8_t*)send_buf) + (i * chunk_size),
                    .len = chunk_size
                };

                esp_err_t err = httpd_ws_send_frame_async(server, ws_client_fd, &ws_frame);
                if (err != ESP_OK) {
                    ws_client_fd = -1;
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(2));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(66)); // ~15 FPS
    }

    free(send_buf);
    free(display_buf);
    display_buf = NULL;
    vTaskDelete(NULL);
}

/*########################################################################################################################*
*--------------------------------------------------Window APIs implementation---------------------------------------------*
*#########################################################################################################################*/
void Window_PreInit(void) { }

void Window_Init(void) {
    DisplayInfo.Width  = SCREEN_WIDTH;
    DisplayInfo.Height = SCREEN_HEIGHT;
    DisplayInfo.ScaleX = 1.0f;
    DisplayInfo.ScaleY = 1.0f;
    
    Window_Main.Width    = DisplayInfo.Width;
    Window_Main.Height   = DisplayInfo.Height;
    Window_Main.Focused  = true;
    Window_Main.Exists   = true;
    Window_Main.UIScaleX = DEFAULT_UI_SCALE_X;
    Window_Main.UIScaleY = DEFAULT_UI_SCALE_Y;

    // Launch Video/Web server task on Core 1 (leaving Core 0 for main game loop)
    xTaskCreatePinnedToCore(video_task, "video_task", 8192, NULL, 10, NULL, 1);
}

void Window_Free(void) { }

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

void Window_ProcessEvents(float delta) {
    // Process input events queued from the WebSocket connection
    while (event_head != event_tail) {
        struct WebEvent ev = event_queue[event_head];
        event_head = (event_head + 1) % MAX_EVENTS;
        
        if (ev.type == 0) { // KeyDown
            Input_SetPressed(ev.key);
        } else if (ev.type == 1) { // KeyUp
            Input_SetReleased(ev.key);
        } else if (ev.type == 2) { // MouseMove
            Pointer_SetPosition(0, ev.x, ev.y);
        } else if (ev.type == 3) { // MouseButtonDown
            Input_SetNonRepeatable(ev.key, true);
        } else if (ev.type == 4) { // MouseButtonUp
            Input_SetNonRepeatable(ev.key, false);
        }
    }
}

void Cursor_SetPosition(int x, int y) { }
void Window_EnableRawMouse(void)  { Input.RawMode = true; }
void Window_DisableRawMouse(void) { Input.RawMode = false; }
void Window_UpdateRawMouse(void)  { }

void Gamepads_PreInit(void) { }
void Gamepads_Init(void)    { }
void Gamepads_Process(float delta) { }

void Window_AllocFramebuffer(struct Bitmap* bmp, int width, int height) {
    fb_ptr = (BitmapCol*)heap_caps_malloc(width * height * sizeof(BitmapCol), MALLOC_CAP_SPIRAM);
    if (!fb_ptr) {
        ESP_LOGE(TAG, "Failed to allocate framebuffer in PSRAM!");
        Process_Abort("Out of PSRAM memory for Framebuffer");
    }
    memset(fb_ptr, 0, width * height * sizeof(BitmapCol));
    
    bmp->scan0  = fb_ptr;
    bmp->width  = width;
    bmp->height = height;
}

void Window_DrawFramebuffer(Rect2D r, struct Bitmap* bmp) {
    if (!display_buf || !display_mutex) return;

    // Downsample the completed frame 400x400 -> 200x200 RGB565 into display_buf.
    // Doing this here (not in video_task) ensures we only ever send complete frames.
    if (xSemaphoreTake(display_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        for (int y = 0; y < 200; y++) {
            BitmapCol* fb_row = fb_ptr + (y * 2) * SCREEN_WIDTH;
            for (int x = 0; x < 200; x++) {
                BitmapCol color = fb_row[x * 2];
                uint8_t r = BitmapCol_R(color);
                uint8_t g = BitmapCol_G(color);
                uint8_t b = BitmapCol_B(color);
                display_buf[y * 200 + x] = htons(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
            }
        }
        xSemaphoreGive(display_mutex);
    }
}

void Window_FreeFramebuffer(struct Bitmap* bmp) {
    if (fb_ptr) {
        heap_caps_free(fb_ptr);
        fb_ptr = NULL;
    }
    bmp->scan0 = NULL;
}

void OnscreenKeyboard_Open(struct OpenKeyboardArgs* args) { }
void OnscreenKeyboard_SetText(const cc_string* text) { }
void OnscreenKeyboard_Close(void) { }

void Window_ShowDialog(const char* title, const char* msg) {
    ESP_LOGI(TAG, "DIALOG: %s - %s", title, msg);
}

cc_result Window_OpenFileDialog(const struct OpenFileDialogArgs* args) { return ERR_NOT_SUPPORTED; }
cc_result Window_SaveFileDialog(const struct SaveFileDialogArgs* args) { return ERR_NOT_SUPPORTED; }

