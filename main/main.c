#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_camera.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_board_init.h"
#include "model_path.h"

static const char *TAG = "AEGIS";

#define WIFI_SSID      "Mahee"
#define WIFI_PASSWORD  "35793579"

#define CAM_PIN_PWDN  -1
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK  15
#define CAM_PIN_SIOD  4
#define CAM_PIN_SIOC  5
#define CAM_PIN_D7    16
#define CAM_PIN_D6    17
#define CAM_PIN_D5    18
#define CAM_PIN_D4    12
#define CAM_PIN_D3    10
#define CAM_PIN_D2    8
#define CAM_PIN_D1    9
#define CAM_PIN_D0    11
#define CAM_PIN_VSYNC 6
#define CAM_PIN_HREF  7
#define CAM_PIN_PCLK  13

typedef enum { STATE_IDLE, STATE_ACTIVE } warden_state_t;

static volatile warden_state_t   g_state     = STATE_IDLE;
static volatile int              g_task_flag = 0;
static const esp_afe_sr_iface_t *afe_handle  = NULL;
static httpd_handle_t            g_server    = NULL;

#define WAKE_WORD_BIT BIT0
static EventGroupHandle_t g_event_group;

// MJPEG stream
#define PART_BOUNDARY "aegisboundary"
static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    char part_buf[64];
    httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    ESP_LOGI(TAG, "Stream client connected");
    while (g_state == STATE_ACTIVE) {
        fb = esp_camera_fb_get();
        if (!fb) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        if (httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY)) != ESP_OK) { esp_camera_fb_return(fb); break; }
        size_t hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, fb->len);
        if (httpd_resp_send_chunk(req, part_buf, hlen) != ESP_OK) { esp_camera_fb_return(fb); break; }
        if (httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len) != ESP_OK) { esp_camera_fb_return(fb); break; }
        esp_camera_fb_return(fb);
    }
    return ESP_OK;
}

static esp_err_t index_handler(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html><html><head><title>A.E.G.I.S. Live</title>"
        "<style>body{background:#111;color:#0f0;font-family:monospace;"
        "display:flex;flex-direction:column;align-items:center;"
        "justify-content:center;height:100vh;margin:0;}"
        "h2{letter-spacing:4px;margin-bottom:16px;}"
        "img{border:2px solid #0f0;max-width:95vw;}"
        "p{font-size:12px;margin-top:10px;opacity:0.6;}</style></head>"
        "<body><h2>A.E.G.I.S. LIVE</h2><img src='/stream'/>"
        "<p>Wake word activated - streaming</p></body></html>";
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t camera_init(void)
{
    camera_config_t config = {
        .pin_pwdn=CAM_PIN_PWDN, .pin_reset=CAM_PIN_RESET,
        .pin_xclk=CAM_PIN_XCLK, .pin_sccb_sda=CAM_PIN_SIOD, .pin_sccb_scl=CAM_PIN_SIOC,
        .pin_d7=CAM_PIN_D7, .pin_d6=CAM_PIN_D6, .pin_d5=CAM_PIN_D5, .pin_d4=CAM_PIN_D4,
        .pin_d3=CAM_PIN_D3, .pin_d2=CAM_PIN_D2, .pin_d1=CAM_PIN_D1, .pin_d0=CAM_PIN_D0,
        .pin_vsync=CAM_PIN_VSYNC, .pin_href=CAM_PIN_HREF, .pin_pclk=CAM_PIN_PCLK,
        .xclk_freq_hz=20000000, .ledc_timer=LEDC_TIMER_0, .ledc_channel=LEDC_CHANNEL_0,
        .pixel_format=PIXFORMAT_JPEG, .frame_size=FRAMESIZE_VGA,
        .jpeg_quality=12, .fb_count=2,
        .fb_location=CAMERA_FB_IN_PSRAM, .grab_mode=CAMERA_GRAB_LATEST,
    };
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) ESP_LOGE(TAG, "Camera init failed: 0x%x", err);
    return err;
}

static void start_stream_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    if (httpd_start(&g_server, &config) != ESP_OK) { ESP_LOGE(TAG, "HTTP server failed"); return; }
    httpd_uri_t index_uri  = {"/",       HTTP_GET, index_handler,  NULL};
    httpd_uri_t stream_uri = {"/stream", HTTP_GET, stream_handler, NULL};
    httpd_register_uri_handler(g_server, &index_uri);
    httpd_register_uri_handler(g_server, &stream_uri);
    ESP_LOGI(TAG, "Stream server up on port 80");
}

static void camera_activation_task(void *arg)
{
    while (1) {
        xEventGroupWaitBits(g_event_group, WAKE_WORD_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
        ESP_LOGI(TAG, "Activating camera...");
        if (camera_init() == ESP_OK) {
            g_state = STATE_ACTIVE;
            start_stream_server();
            ESP_LOGI(TAG, "Camera ACTIVE - open browser to stream");
        } else {
            ESP_LOGE(TAG, "Camera init failed");
            g_state = STATE_IDLE;
        }
        vTaskDelay(portMAX_DELAY);
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "=========================================");
        ESP_LOGI(TAG, " WiFi ready! Say 'Hi ESP' to activate.");
        ESP_LOGI(TAG, " Stream will appear at:");
        ESP_LOGI(TAG, " http://" IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "=========================================");
    }
}

static void wifi_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);
    wifi_config_t wifi_config = { .sta = { .ssid = WIFI_SSID, .password = WIFI_PASSWORD } };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}

// AFE feed task: mic -> pipeline
static void feed_task(void *arg)
{
    esp_afe_sr_data_t *afe_data = arg;
    int chunksize = afe_handle->get_feed_chunksize(afe_data);
    int nch       = afe_handle->get_feed_channel_num(afe_data);
    int feed_ch   = esp_get_feed_channel();
    assert(nch == feed_ch);
    int16_t *buf = malloc(chunksize * sizeof(int16_t) * feed_ch);
    assert(buf);
    while (g_task_flag) {
        esp_get_feed_data(true, buf, chunksize * sizeof(int16_t) * feed_ch);
        afe_handle->feed(afe_data, buf);
    }
    free(buf);
    vTaskDelete(NULL);
}

// AFE detect task: check for wake word, signal camera task
static void detect_task(void *arg)
{
    esp_afe_sr_data_t *afe_data = arg;
    afe_handle->set_wakenet_threshold(afe_data, 1, 0.6);
    afe_handle->set_wakenet_threshold(afe_data, 2, 0.6);
    afe_handle->reset_wakenet_threshold(afe_data, 1);
    afe_handle->reset_wakenet_threshold(afe_data, 2);
    printf("----------- detect start -----------\n");
    while (g_task_flag) {
        afe_fetch_result_t *res = afe_handle->fetch(afe_data);
        if (!res || res->ret_value == ESP_FAIL) { printf("fetch error!\n"); break; }
        if (res->wakeup_state == WAKENET_DETECTED) {
            printf(">>> WAKE WORD DETECTED <<<\n");
            printf("model: %d  word: %d\n", res->wakenet_model_index, res->wake_word_index);
            if (g_state == STATE_IDLE)
                xEventGroupSetBits(g_event_group, WAKE_WORD_BIT);
            else
                printf("(already active, ignoring)\n");
        }
    }
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "A.E.G.I.S. Warden booting...");
    nvs_flash_init();

    // Init board I2S mic + codec - same as working afe example
    ESP_ERROR_CHECK(esp_board_init(16000, 1, 16));

    wifi_init();
    g_event_group = xEventGroupCreate();

    // Load models from "model" flash partition
    srmodel_list_t *models = esp_srmodel_init("model");
    if (models) {
        for (int i = 0; i < models->num; i++) {
            if (strstr(models->model_name[i], ESP_WN_PREFIX) != NULL)
                printf("wakenet model: %s\n", models->model_name[i]);
        }
    }

    // Init AFE - exact same API as working afe example
    afe_config_t *afe_config = afe_config_init(
        esp_get_input_format(), models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    if (afe_config->wakenet_model_name)
        printf("wakeword in config: %s\n", afe_config->wakenet_model_name);

    afe_handle = esp_afe_handle_from_config(afe_config);
    esp_afe_sr_data_t *afe_data = afe_handle->create_from_config(afe_config);
    afe_config_free(afe_config);

    // Camera task - blocked until wake word fires
    xTaskCreatePinnedToCore(camera_activation_task, "cam_task", 4096, NULL, 4, NULL, 1);

    // AFE tasks on separate cores (same as working example)
    g_task_flag = 1;
    xTaskCreatePinnedToCore(feed_task,   "feed",   8192, (void *)afe_data, 5, NULL, 0);
    xTaskCreatePinnedToCore(detect_task, "detect", 4096, (void *)afe_data, 5, NULL, 1);

    ESP_LOGI(TAG, "Boot done. Listening for 'Hi ESP'...");
}
