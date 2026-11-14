#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <inttypes.h>

#include "M5PM1.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_chip_info.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_event.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_afe_config.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_mac.h"
#include "sdkconfig.h"

// NimBLE host (BLE-only transport; replaces v15 WiFi + WebSocket).
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

// Opus encoder (esp_audio_codec direct API; no registration needed).
#include "esp_audio_enc.h"
#include "esp_opus_enc.h"

static const char *TAG = "sticks3_v16";

// Audio: capture 16 kHz mono directly (no resample). 60 ms frame aligns with
// xiaozhi OPUS_FRAME_DURATION_MS so the future Opus swap is a drop-in.
static constexpr int kSampleRate = 16000;
static constexpr int kFrameMs = 60;
static constexpr int kSamplesPerFrame = kSampleRate * kFrameMs / 1000;  // 960
static constexpr int kFrameBytes = kSamplesPerFrame * sizeof(int16_t);  // 1920
static constexpr int kInputGain = 24;
static constexpr int kClipAbs = 32000;
static constexpr int kQueueDepth = 16;  // ~1s of 60ms frames

// Opus: 60ms frame @16k mono = 960 samples in, ~180 bytes out at 24kbps. The
// 2-byte big-endian sequence header lets the Mac detect dropped notifications.
static constexpr int kOpusBitrate = CONFIG_STICKS3_OPUS_BITRATE;
static constexpr int kSeqHeaderBytes = 2;
static constexpr int kMaxOpusBytes = 400;  // generous ceiling for one frame

static constexpr gpio_num_t kI2sMclk = GPIO_NUM_18;
static constexpr gpio_num_t kI2sBclk = GPIO_NUM_17;
static constexpr gpio_num_t kI2sWs = GPIO_NUM_15;
static constexpr gpio_num_t kI2sDout = GPIO_NUM_14;
static constexpr gpio_num_t kI2sDin = GPIO_NUM_16;

static constexpr gpio_num_t kI2cSda = GPIO_NUM_47;
static constexpr gpio_num_t kI2cScl = GPIO_NUM_48;

// BtnA (front button) is active-low on GPIO11. Toggle mode: press to start
// streaming, press again to stop.
static constexpr gpio_num_t kBtnA = GPIO_NUM_11;
// BtnB is the RIGHT button (KEY2). Hardware-verified on the M5StickS3 board port
// (thc1006/zephyr-m5stack-sticks3 gpio-keys map): middle=G11, right=G12. Active
// low, internal pull-up. A short press sends control cmd=2 so the host injects Enter.
static constexpr gpio_num_t kBtnB = GPIO_NUM_12;
// Long-press thresholds. A's voice hold-to-talk needs a short enough delay to
// feel responsive; B's wake-workbench is specced at exactly 0.5s.
static constexpr int64_t kBtnLongPressVoiceUs = 350 * 1000;  // A: hold-to-talk
static constexpr int64_t kBtnLongPressWakeUs  = 500 * 1000;  // B: wake workbench

// LCD ST7789 over SPI2. 135x240 panel; backlight is powered by PM1 gpio2 in
// init_pm1. offset_x/y map the visible window into the controller's 240x320 RAM.
static constexpr gpio_num_t kLcdMosi = GPIO_NUM_39;
static constexpr gpio_num_t kLcdSclk = GPIO_NUM_40;
static constexpr gpio_num_t kLcdDc = GPIO_NUM_45;
static constexpr gpio_num_t kLcdCs = GPIO_NUM_41;
static constexpr gpio_num_t kLcdRst = GPIO_NUM_21;
// Backlight enable is a host GPIO (G38), active high, and only lights once the
// PM1 L3B rail (gpio2, set in init_pm1) is already powered.
static constexpr gpio_num_t kLcdBacklight = GPIO_NUM_38;
static constexpr int kLcdW = 135;
static constexpr int kLcdH = 240;
static constexpr int kLcdGapX = 52;
static constexpr int kLcdGapY = 40;
static constexpr uint8_t kEs8311Addr = ES8311_CODEC_DEFAULT_ADDR;
static constexpr uint8_t kEs8311Addr7Bit = kEs8311Addr >> 1;

struct PcmFrame {
    int16_t samples[kSamplesPerFrame];
};

static i2c_master_bus_handle_t g_i2c_bus = nullptr;
static i2s_chan_handle_t g_i2s_tx = nullptr;
static i2s_chan_handle_t g_i2s_rx = nullptr;
static esp_codec_dev_handle_t g_codec_dev = nullptr;
static QueueHandle_t g_frame_queue = nullptr;
static esp_audio_enc_handle_t g_opus_enc = nullptr;
static M5PM1 g_pmic;  // power manager: battery charge/monitor (readVbat for %)

// Subset Chinese font (zh14.c, generated with lv_font_conv) — the built-in
// montserrat font only carries Latin glyphs, so CJK hint lines would otherwise
// render as empty tofu boxes on the LCD.
extern "C" lv_font_t zh14;
extern "C" lv_font_t zh10;  // 10px CJK font for the bottom hint line
extern "C" lv_font_t zh28;  // 28px CJK font for the big status label

static lv_display_t *g_lvgl_disp = nullptr;
static lv_obj_t *g_ui_status = nullptr;  // big REC / IDLE / WAKE label
static lv_obj_t *g_ui_sub = nullptr;     // timer / net sub-line
static lv_obj_t *g_ui_bar = nullptr;     // mic level bar
static lv_obj_t *g_ui_line = nullptr;    // thin divider above the hint line
static lv_obj_t *g_ui_foot = nullptr;    // bottom hint line (matches the demo UI)
// Battery indicator (top-right, persistent across all states): a small % label
// plus a battery body whose fill width tracks the remaining charge.
static lv_obj_t *g_batt_label = nullptr;  // "--%" / "86%"
static lv_obj_t *g_batt_icon = nullptr;   // battery body (rounded rect, border)
static lv_obj_t *g_batt_fill = nullptr;   // body fill, width scaled to percent
// When a workbench wake fires (B long-press), the LCD shows the wake feedback
// for this many microseconds instead of the normal status.
static volatile int64_t g_wake_flash_us = 0;
static constexpr int64_t kWakeFlashDurationUs = 3000 * 1000;
// How long the green "WAKING" chip shows before the workbench window slides in.
static constexpr int64_t kWakeChipDurationUs = 800 * 1000;
// The LCD progress bar fills 0..100% over this recording time, matching the
// host's --max-segment-seconds (14.0) so the bar reads "how far into the
// segment we are" instead of jittering with the mic level.
static constexpr int64_t kRecProgressMaxUs = 14 * 1000 * 1000;

// WAKE feedback widgets (B long-press): a green "WAKING" chip that hands off to
// a white workbench window with a blinking caret in its input box. Created
// hidden in build_ui, toggled by lcd_task. Matches the demo HTML.
static lv_obj_t *g_wake_chip = nullptr;
static lv_obj_t *g_wake_win = nullptr;
static lv_obj_t *g_wake_win_title = nullptr;
static lv_obj_t *g_wake_input = nullptr;
static lv_obj_t *g_wake_caret = nullptr;
static lv_timer_t *g_caret_timer = nullptr;
static bool g_wake_phase2 = false;  // true once the window has slid in

// Per-device identity derived from the chip MAC (last 3 bytes). g_ble_name is the
// advertised name (CONFIG_STICKS3_BLE_NAME + "-XXXXXX"); g_pair_code is just the
// 6-hex suffix, shown big on the LCD while unpaired so the user can match the
// physical stick to the entry in the host's scan list. Filled once by init_identity().
static char g_ble_name[40] = {0};
static char g_pair_code[8] = {0};

// AFE (esp-sr): WebRTC-based NS/AGC/VAD, no flashed models. Voice-communication
// type at 16 kHz mono. Feed raw mic in feed-chunk units, fetch clean 16-bit PCM.
static const esp_afe_sr_iface_t *g_afe = nullptr;
static esp_afe_sr_data_t *g_afe_data = nullptr;
static int g_feed_chunk = 0;   // samples per feed() call (per channel)
static int g_feed_channels = 0;  // channels the feed buffer must carry

// BLE link state. g_conn_handle is valid while a central (the Mac) is connected;
// g_audio_subscribed / g_ctrl_subscribed track CCCD notify-enable per char.
static volatile bool g_ble_connected = false;
static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t g_audio_val_handle = 0;   // audio characteristic value handle
static uint16_t g_ctrl_val_handle = 0;    // control characteristic value handle
static volatile bool g_audio_subscribed = false;
static volatile bool g_ctrl_subscribed = false;
static volatile uint32_t g_dropped_frames = 0;
static uint16_t g_audio_seq = 0;          // per-frame sequence for loss detection
static uint32_t g_last_drop_log_ms = 0;   // rate-limit the notify-failure log
static uint32_t g_last_enc_log_ms = 0;    // rate-limit the encode-time log

// Recording mode. None=idle, Voice=hold-to-talk voice input (A long-press),
// Memo=press-to-toggle memo note (A short-press). fetch_task only enqueues
// frames while non-None; send_task emits the matching control start/stop on
// transitions (voice 1/0, memo 4/5).
enum RecMode : uint8_t { kRecNone = 0, kRecVoice = 1, kRecMemo = 2 };
static volatile uint8_t g_rec_mode = kRecNone;
// What send_task last told the Mac. Reset to a "needs announce" state on connect.
static volatile uint8_t g_ctrl_synced_mode = kRecNone;
static volatile bool g_ctrl_synced_valid = false;
// Per-second peak of the clean AGC output; logged for diagnostics.
static volatile int g_level_peak = 0;

static void log_chip_info()
{
    esp_chip_info_t chip_info;
    uint32_t flash_size = 0;
    esp_chip_info(&chip_info);
    if (esp_flash_get_size(nullptr, &flash_size) != ESP_OK) {
        flash_size = 0;
    }
    ESP_LOGI(TAG, "StickS3 v16 M5: BtnA toggle + AFE + Opus + BLE + LCD");
    ESP_LOGI(TAG, "target=%s cores=%d rev=%u.%u flash=%" PRIu32 "MB rate=%d frame=%dms",
             CONFIG_IDF_TARGET, chip_info.cores,
             chip_info.revision / 100, chip_info.revision % 100,
             flash_size / (1024 * 1024), kSampleRate, kFrameMs);
}

static void init_nvs()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static void init_i2c()
{
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = I2C_NUM_0;
    bus_cfg.sda_io_num = kI2cSda;
    bus_cfg.scl_io_num = kI2cScl;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = 1;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &g_i2c_bus));
    ESP_LOGI(TAG, "I2C ready sda=%d scl=%d", kI2cSda, kI2cScl);
}

static void init_pm1()
{
    g_pmic.begin(g_i2c_bus, M5PM1_DEFAULT_ADDR);
    g_pmic.setChargeEnable(true);
    g_pmic.setBoostEnable(false);

    g_pmic.pinMode(2, OUTPUT);
    g_pmic.gpioSetDrive(M5PM1_GPIO_NUM_2, M5PM1_GPIO_DRIVE_PUSHPULL);
    g_pmic.digitalWrite(2, HIGH);
    vTaskDelay(pdMS_TO_TICKS(100));

    g_pmic.pinMode(3, OUTPUT);
    g_pmic.gpioSetDrive(M5PM1_GPIO_NUM_3, M5PM1_GPIO_DRIVE_PUSHPULL);
    g_pmic.digitalWrite(3, HIGH);
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_LOGI(TAG, "PM1 audio/display power enabled");
}

static void init_i2s()
{
    i2s_chan_config_t chan_cfg = {};
    chan_cfg.id = I2S_NUM_0;
    chan_cfg.role = I2S_ROLE_MASTER;
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 240;
    chan_cfg.auto_clear_after_cb = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &g_i2s_tx, &g_i2s_rx));

    i2s_std_config_t std_cfg = {};
    std_cfg.clk_cfg.sample_rate_hz = kSampleRate;
    std_cfg.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    std_cfg.slot_cfg.data_bit_width = I2S_DATA_BIT_WIDTH_16BIT;
    std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO;
    std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_STEREO;
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    std_cfg.slot_cfg.ws_width = I2S_DATA_BIT_WIDTH_16BIT;
    std_cfg.slot_cfg.bit_shift = true;
    std_cfg.slot_cfg.left_align = true;
    std_cfg.gpio_cfg.mclk = kI2sMclk;
    std_cfg.gpio_cfg.bclk = kI2sBclk;
    std_cfg.gpio_cfg.ws = kI2sWs;
    std_cfg.gpio_cfg.dout = kI2sDout;
    std_cfg.gpio_cfg.din = kI2sDin;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(g_i2s_tx, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(g_i2s_rx, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(g_i2s_tx));
    ESP_ERROR_CHECK(i2s_channel_enable(g_i2s_rx));
    ESP_LOGI(TAG, "I2S0 ready mclk=%d bclk=%d ws=%d rate=%d",
             kI2sMclk, kI2sBclk, kI2sWs, kSampleRate);
}

static void init_es8311()
{
    audio_codec_i2s_cfg_t i2s_cfg = {};
    i2s_cfg.port = I2S_NUM_0;
    i2s_cfg.rx_handle = g_i2s_rx;
    i2s_cfg.tx_handle = g_i2s_tx;
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    assert(data_if != nullptr);

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = I2C_NUM_0,
        .addr = kEs8311Addr,
        .bus_handle = g_i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    assert(ctrl_if != nullptr);

    uint8_t reset_value = 0x1F;
    ESP_ERROR_CHECK(static_cast<esp_err_t>(
        ctrl_if->write_reg(ctrl_if, 0x00, 1, &reset_value, 1)));
    vTaskDelay(pdMS_TO_TICKS(5));

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    assert(gpio_if != nullptr);

    es8311_codec_cfg_t es8311_cfg = {};
    es8311_cfg.ctrl_if = ctrl_if;
    es8311_cfg.gpio_if = gpio_if;
    es8311_cfg.codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH;
    es8311_cfg.pa_pin = GPIO_NUM_NC;
    es8311_cfg.use_mclk = false;
    es8311_cfg.hw_gain.pa_voltage = 5.0;
    es8311_cfg.hw_gain.codec_dac_voltage = 3.3;
    es8311_cfg.pa_reverted = false;
    const audio_codec_if_t *codec_if = es8311_codec_new(&es8311_cfg);
    assert(codec_if != nullptr);

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    g_codec_dev = esp_codec_dev_new(&dev_cfg);
    assert(g_codec_dev != nullptr);

    esp_codec_dev_sample_info_t sample_info = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = kSampleRate,
        .mclk_multiple = 0,
    };
    ESP_ERROR_CHECK(esp_codec_dev_open(g_codec_dev, &sample_info));
    ESP_ERROR_CHECK(esp_codec_dev_set_in_gain(g_codec_dev, kInputGain));
    ESP_LOGI(TAG, "ES8311 ready addr=0x%02x input_gain=%d rate=%d",
             kEs8311Addr7Bit, kInputGain, kSampleRate);
}

static void init_afe()
{
    // "M" = single mic channel, no reference. VC type enables nonlinear NS for
    // 16 kHz comms. HIGH_PERF picks the stronger algorithms (PSRAM is enabled).
    afe_config_t *cfg = afe_config_init("M", nullptr, AFE_TYPE_VC, AFE_MODE_HIGH_PERF);
    assert(cfg != nullptr);
    // No wake word on this device — mic streams continuously to the Mac.
    cfg->wakenet_init = false;
    // Single mic, no playback reference wired back, so AEC has nothing to cancel.
    cfg->aec_init = false;
    // Keep NS + AGC + VAD (all default-on for VC). VAD is logged only this phase;
    // endpoint splitting happens on the Mac side later.
    cfg->vad_init = true;
    cfg->agc_init = true;

    g_afe = esp_afe_handle_from_config(cfg);
    assert(g_afe != nullptr);
    g_afe_data = g_afe->create_from_config(cfg);
    assert(g_afe_data != nullptr);
    afe_config_free(cfg);

    g_feed_chunk = g_afe->get_feed_chunksize(g_afe_data);
    g_feed_channels = g_afe->get_feed_channel_num(g_afe_data);
    int fetch_chunk = g_afe->get_fetch_chunksize(g_afe_data);
    ESP_LOGI(TAG, "AFE ready feed_chunk=%d feed_ch=%d fetch_chunk=%d rate=%d",
             g_feed_chunk, g_feed_channels, fetch_chunk,
             g_afe->get_samp_rate(g_afe_data));
}

static void build_ui();

static void init_lcd()
{
    // Backlight enable (G38). PM1 gpio2 already powered the LCD rail in init_pm1;
    // without driving this high the panel initializes fine but stays dark.
    gpio_config_t bl = {};
    bl.pin_bit_mask = 1ULL << kLcdBacklight;
    bl.mode = GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&bl));
    gpio_set_level(kLcdBacklight, 1);

    spi_bus_config_t bus = {};
    bus.mosi_io_num = kLcdMosi;
    bus.miso_io_num = -1;
    bus.sclk_io_num = kLcdSclk;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = kLcdW * kLcdH * sizeof(uint16_t);
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io = nullptr;
    esp_lcd_panel_io_spi_config_t io_cfg = {};
    io_cfg.cs_gpio_num = kLcdCs;
    io_cfg.dc_gpio_num = kLcdDc;
    io_cfg.spi_mode = 0;
    io_cfg.pclk_hz = 40 * 1000 * 1000;
    io_cfg.trans_queue_depth = 10;
    io_cfg.lcd_cmd_bits = 8;
    io_cfg.lcd_param_bits = 8;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &io));

    esp_lcd_panel_handle_t panel = nullptr;
    esp_lcd_panel_dev_config_t panel_cfg = {};
    panel_cfg.reset_gpio_num = kLcdRst;
    // ST7789 color config on the StickS3. The ESP32 is little-endian, so RGB565
    // pixels land on the SPI bus low-byte-first; the panel must be told to read
    // them as little-endian or every pixel is byte-swapped (greys turn purple,
    // dark green turns red). Element order stays RGB.
    panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_cfg.data_endian = LCD_RGB_DATA_ENDIAN_LITTLE;
    panel_cfg.bits_per_pixel = 16;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel_cfg, &panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel, kLcdGapX, kLcdGapY));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    lvgl_port_cfg_t lv_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lv_cfg));

    lvgl_port_display_cfg_t disp_cfg = {};
    disp_cfg.io_handle = io;
    disp_cfg.panel_handle = panel;
    disp_cfg.buffer_size = kLcdW * kLcdH;
    disp_cfg.double_buffer = false;
    disp_cfg.hres = kLcdW;
    disp_cfg.vres = kLcdH;
    disp_cfg.flags.buff_dma = true;
    g_lvgl_disp = lvgl_port_add_disp(&disp_cfg);
    assert(g_lvgl_disp != nullptr);

    build_ui();
    ESP_LOGI(TAG, "LCD ready %dx%d st7789 spi2", kLcdW, kLcdH);
}

// ---- Opus encoder (esp_audio_codec direct API; no registration needed) ----
static void init_opus()
{
    esp_opus_enc_config_t cfg = ESP_OPUS_ENC_CONFIG_DEFAULT();
    cfg.sample_rate = kSampleRate;
    cfg.channel = 1;
    cfg.bits_per_sample = 16;
    cfg.bitrate = kOpusBitrate;
    cfg.frame_duration = ESP_OPUS_ENC_FRAME_DURATION_60_MS;
    cfg.application_mode = ESP_OPUS_ENC_APPLICATION_VOIP;
    // complexity 0: the stack lives in PSRAM (slow), and the SILK pitch search
    // hammers stack scratch arrays. At 5 a 60ms encode took ~60ms and starved
    // IDLE0 (task_wdt). 0 cuts SILK work several-fold; VOIP@24k stays fine for
    // speech ASR. Encode time is logged in send_task to confirm the headroom.
    cfg.complexity = 0;
    esp_audio_err_t err = esp_opus_enc_open(&cfg, sizeof(cfg), &g_opus_enc);
    assert(err == ESP_AUDIO_ERR_OK && g_opus_enc != nullptr);

    int in_size = 0, out_size = 0;
    esp_opus_enc_get_frame_size(g_opus_enc, &in_size, &out_size);
    ESP_LOGI(TAG, "Opus ready bitrate=%d in_frame=%d out_frame=%d (pcm frame=%d)",
             kOpusBitrate, in_size, out_size, kFrameBytes);
    // 60ms @16k mono = 1920 bytes in; encoder must agree or framing is off.
    assert(in_size == kFrameBytes);
}

// ---- BLE GATT: one service with audio(notify) + control(notify) ----
// Custom 128-bit UUIDs. Bytes are the little-endian form of
// 6e4000xx-b5a3-f393-e0a9-e50e24dcca9e (xx = 01 service, 02 audio, 03 control).
static const ble_uuid128_t kSvcUuid = BLE_UUID128_INIT(
    0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x01,0x00,0x40,0x6e);
static const ble_uuid128_t kAudioUuid = BLE_UUID128_INIT(
    0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x02,0x00,0x40,0x6e);
static const ble_uuid128_t kCtrlUuid = BLE_UUID128_INIT(
    0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x03,0x00,0x40,0x6e);

static uint8_t g_own_addr_type = 0;
static void start_advertising();

// Notify-only characteristics: nothing to read/write, so the access cb is a stub.
static int gatt_access_cb(uint16_t, uint16_t, struct ble_gatt_access_ctxt *, void *)
{
    return 0;
}

// NimBLE's GATT tables use C99 designated initializers; in C++ with -Wextra
// the unlisted trailing fields (zero-initialized, as intended) trip
// -Wmissing-field-initializers. Silence just this block.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
static const struct ble_gatt_chr_def kChrs[] = {
    {
        .uuid = &kAudioUuid.u,
        .access_cb = gatt_access_cb,
        .flags = BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &g_audio_val_handle,
    },
    {
        .uuid = &kCtrlUuid.u,
        .access_cb = gatt_access_cb,
        .flags = BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &g_ctrl_val_handle,
    },
    {0},
};

static const struct ble_gatt_svc_def kSvcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &kSvcUuid.u,
        .characteristics = kChrs,
    },
    {0},
};
#pragma GCC diagnostic pop

static int gap_event_cb(struct ble_gap_event *event, void *)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            g_conn_handle = event->connect.conn_handle;
            g_ble_connected = true;
            // Allow 1M+2M (let the controller pick). Forcing 2M-only has been
            // a source of link drops with some macOS/CoreBluetooth versions.
            ble_gap_set_prefered_le_phy(g_conn_handle,
                                        BLE_GAP_LE_PHY_1M_MASK | BLE_GAP_LE_PHY_2M_MASK,
                                        BLE_GAP_LE_PHY_1M_MASK | BLE_GAP_LE_PHY_2M_MASK, 0);
            ESP_LOGI(TAG, "BLE connected handle=%d", g_conn_handle);
        } else {
            ESP_LOGW(TAG, "BLE connect failed status=%d", event->connect.status);
            start_advertising();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGW(TAG, "BLE disconnected reason=%d", event->disconnect.reason);
        g_ble_connected = false;
        g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        g_audio_subscribed = false;
        g_ctrl_subscribed = false;
        start_advertising();
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == g_audio_val_handle) {
            g_audio_subscribed = event->subscribe.cur_notify;
        } else if (event->subscribe.attr_handle == g_ctrl_val_handle) {
            g_ctrl_subscribed = event->subscribe.cur_notify;
            // Re-announce recording state to a freshly-subscribed central.
            g_ctrl_synced_valid = false;
        }
        ESP_LOGI(TAG, "BLE subscribe attr=%d notify=%d (audio_val=%d ctrl_val=%d)",
                 event->subscribe.attr_handle, event->subscribe.cur_notify,
                 g_audio_val_handle, g_ctrl_val_handle);
        return 0;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "BLE MTU=%d", event->mtu.value);
        return 0;
    default:
        return 0;
    }
}

// Derive this device's unique name + pairing code from the chip MAC. The last 3
// bytes (24 bits) make every stick flashed with the same firmware advertise a
// distinct name like "StickS3-Mic-3F2A9C". Not globally unique across all ESP32s
// ever made, but collision odds are negligible at fleet scale, and reconnection
// keys off the full BLE address anyway — the suffix is a human-facing label.
static void init_identity()
{
    uint8_t mac[6] = {0};
    // BT MAC keeps the suffix stable per device and tied to the radio it's seen on.
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(g_pair_code, sizeof(g_pair_code), "%02X%02X%02X", mac[3], mac[4], mac[5]);
    snprintf(g_ble_name, sizeof(g_ble_name), "%s-%s", CONFIG_STICKS3_BLE_NAME, g_pair_code);
    ESP_LOGI(TAG, "device identity: name=\"%s\" code=%s", g_ble_name, g_pair_code);
}

static void start_advertising()
{
    struct ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    const char *name = g_ble_name;
    fields.name = reinterpret_cast<const uint8_t *>(name);
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields rc=%d", rc);
        return;
    }
    struct ble_gap_adv_params adv_params = {};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(g_own_addr_type, nullptr, BLE_HS_FOREVER,
                           &adv_params, gap_event_cb, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_start rc=%d", rc);
        return;
    }
    ESP_LOGI(TAG, "BLE advertising as \"%s\"", name);
}

// Notify one characteristic with a copy of buf. Returns the NimBLE rc (0 = ok).
// Non-zero tells us WHY a frame dropped: BLE_HS_ENOMEM (mbuf/ACL buffers full,
// i.e. sending faster than the link drains), BLE_HS_EMSGSIZE (payload > MTU-3,
// i.e. MTU never grew past the 23-byte default), etc.
static int ble_notify(uint16_t val_handle, const uint8_t *buf, int len)
{
    if (!g_ble_connected || g_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return BLE_HS_ENOTCONN;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, len);
    if (om == nullptr) {
        return BLE_HS_ENOMEM;
    }
    return ble_gatts_notify_custom(g_conn_handle, val_handle, om);
}

static void on_sync()
{
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &g_own_addr_type);
    start_advertising();
}

static void ble_host_task(void *)
{
    nimble_port_run();  // returns only at nimble_port_stop()
    nimble_port_freertos_deinit();
}

static void init_ble()
{
    ESP_ERROR_CHECK(nimble_port_init());
    init_identity();  // fill g_ble_name / g_pair_code before advertising uses them
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ESP_ERROR_CHECK(ble_gatts_count_cfg(kSvcs));
    ESP_ERROR_CHECK(ble_gatts_add_svcs(kSvcs));
    ESP_ERROR_CHECK(ble_svc_gap_device_name_set(g_ble_name));
    ESP_LOGI(TAG, "GATT handles: audio_val=%d ctrl_val=%d",
             g_audio_val_handle, g_ctrl_val_handle);
    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(ble_host_task);
    ESP_LOGI(TAG, "BLE host started name=%s", g_ble_name);
}

// Feed task: read raw mic from the codec in AFE feed-chunk units and push into
// the AFE. Runs continuously so the AFE ring buffer never starves. Reading from
// I2S paces this loop (blocking read), so no explicit delay is needed.
static void feed_task(void *)
{
    const int chunk_samples = g_feed_chunk * g_feed_channels;
    const size_t chunk_bytes = chunk_samples * sizeof(int16_t);
    int16_t *buf = static_cast<int16_t *>(malloc(chunk_bytes));
    assert(buf != nullptr);

    while (true) {
        esp_err_t err = esp_codec_dev_read(g_codec_dev, buf, chunk_bytes);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "audio read failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        g_afe->feed(g_afe_data, buf);
    }
}

// Fetch task: pull AFE-enhanced PCM (post NS/AGC), accumulate into 60ms frames,
// enqueue for the sender. Never blocks on the network; a full queue drops and
// counts (decouples capture from transmission, the v13 "40s->17s" fix). Also
// computes per-second diagnostics over the *clean* output and logs VAD state.
static void fetch_task(void *)
{
    PcmFrame frame;
    int filled = 0;  // samples accumulated into frame so far

    uint32_t frames = 0, samples = 0, clipped = 0, speech_frames = 0;
    uint64_t sum_squares = 0;
    int peak = 0;
    float last_volume = 0.0f;
    int64_t next_report = esp_timer_get_time() + 1000000;

    while (true) {
        afe_fetch_result_t *res = g_afe->fetch(g_afe_data);
        if (res == nullptr || res->ret_value == ESP_FAIL) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        last_volume = res->data_volume;
        if (res->vad_state == VAD_SPEECH) speech_frames += 1;

        int n = res->data_size / static_cast<int>(sizeof(int16_t));
        const int16_t *pcm = res->data;
        int consumed = 0;
        while (consumed < n) {
            int take = kSamplesPerFrame - filled;
            if (take > n - consumed) take = n - consumed;
            memcpy(&frame.samples[filled], &pcm[consumed], take * sizeof(int16_t));

            for (int i = 0; i < take; ++i) {
                int value = pcm[consumed + i];
                int abs_value = std::abs(value);
                if (abs_value > peak) peak = abs_value;
                if (abs_value >= kClipAbs) clipped += 1;
                sum_squares += static_cast<uint64_t>(value * value);
            }
            samples += take;
            filled += take;
            consumed += take;

            if (filled == kSamplesPerFrame) {
                // Only stream while recording. Idle keeps the AFE warm (feed/
                // fetch run) but drops frames before the queue so nothing is sent.
                if (g_rec_mode != kRecNone) {
                    if (xQueueSend(g_frame_queue, &frame, 0) != pdTRUE) {
                        g_dropped_frames += 1;
                    }
                }
                frames += 1;
                filled = 0;
            }
        }

        int64_t now = esp_timer_get_time();
        if (now >= next_report) {
            float rms = samples ? std::sqrt(static_cast<float>(sum_squares) / samples) : 0.0f;
            float clip_pct = samples ? (100.0f * clipped / samples) : 0.0f;
            g_level_peak = peak;  // hand the LCD a fresh bar height each second
            ESP_LOGI(TAG, "afe rec=%d frames=%" PRIu32 " rms=%.1f peak=%d clip=%.3f%% vol=%.1fdB speech=%" PRIu32 " drop=%" PRIu32 " heap=%" PRIu32,
                     g_rec_mode, frames, rms, peak, clip_pct, last_volume,
                     speech_frames, g_dropped_frames, esp_get_free_heap_size());
            frames = 0; sum_squares = 0; samples = 0; clipped = 0;
            peak = 0; speech_frames = 0;
            next_report = now + 1000000;
        }
    }
}

// Send task: drain the frame queue, Opus-encode each 60ms frame, and push it as
// a BLE notification on the audio characteristic. Payload = 2-byte big-endian
// sequence + Opus bytes, so the Mac can spot dropped notifications. A control
// notification (1 byte: 1=start, 0=stop) announces recording transitions,
// mirroring the v15 xiaozhi "listen" protocol.
static void send_task(void *)
{
    PcmFrame frame;
    uint8_t out[kSeqHeaderBytes + kMaxOpusBytes];
    while (true) {
        // Announce recording transitions first. The 100ms queue timeout lets this
        // run even when idle so "stop" reaches the Mac promptly after the button.
        // Control codes: 1=voice start, 0=voice stop, 4=memo start, 5=memo stop.
        // The stop code depends on which mode we last announced.
        if (g_ble_connected && g_ctrl_subscribed &&
            (!g_ctrl_synced_valid || g_ctrl_synced_mode != g_rec_mode)) {
            uint8_t state;
            if (g_rec_mode == kRecVoice) {
                state = 1;
            } else if (g_rec_mode == kRecMemo) {
                state = 4;
            } else {
                state = (g_ctrl_synced_mode == kRecMemo) ? 5 : 0;
            }
            int rc = ble_notify(g_ctrl_val_handle, &state, 1);
            if (rc == 0) {
                g_ctrl_synced_mode = g_rec_mode;
                g_ctrl_synced_valid = true;
                ESP_LOGI(TAG, "ctrl notify cmd=%u", state);
            } else {
                ESP_LOGW(TAG, "ctrl notify failed rc=%d", rc);
            }
        }

        if (xQueueReceive(g_frame_queue, &frame, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;  // idle tick: loop back to re-check the recording state
        }
        if (!g_ble_connected || !g_audio_subscribed) {
            continue;  // drop while no subscriber; capture keeps running
        }

        esp_audio_enc_in_frame_t in = {};
        in.buffer = reinterpret_cast<uint8_t *>(frame.samples);
        in.len = kFrameBytes;
        esp_audio_enc_out_frame_t enc = {};
        enc.buffer = out + kSeqHeaderBytes;
        enc.len = kMaxOpusBytes;
        int64_t t_enc = esp_timer_get_time();
        esp_audio_err_t err = esp_opus_enc_process(g_opus_enc, &in, &enc);
        int enc_ms = (int)((esp_timer_get_time() - t_enc) / 1000);
        if (err != ESP_AUDIO_ERR_OK || enc.encoded_bytes == 0) {
            ESP_LOGW(TAG, "opus encode err=%d bytes=%" PRIu32, err, enc.encoded_bytes);
            continue;
        }
        // Encode must stay well under the 60ms frame period or the queue backs
        // up and CPU0 starves IDLE0. Log ~once/sec so we can see the margin.
        {
            uint32_t now = esp_log_timestamp();
            if (now - g_last_enc_log_ms > 1000) {
                g_last_enc_log_ms = now;
                ESP_LOGI(TAG, "opus encode %dms -> %" PRIu32 "B",
                         enc_ms, enc.encoded_bytes);
            }
        }
        out[0] = static_cast<uint8_t>(g_audio_seq >> 8);
        out[1] = static_cast<uint8_t>(g_audio_seq & 0xff);
        g_audio_seq += 1;
        int rc = ble_notify(g_audio_val_handle,
                            out, kSeqHeaderBytes + enc.encoded_bytes);
        if (rc != 0) {
            // Notify failed. Drop this frame and count it; the sequence gap tells
            // the Mac. No reconnect needed — NimBLE keeps the link and the next
            // frame retries. Log the rc at most ~once/sec so we can see WHY:
            //   BLE_HS_ENOMEM(6)  = ACL/mbuf buffers full (sending too fast)
            //   BLE_HS_EMSGSIZE(? ) = payload > MTU-3 (MTU never grew)
            g_dropped_frames += 1;
            uint32_t now = esp_log_timestamp();
            if (now - g_last_drop_log_ms > 1000) {
                g_last_drop_log_ms = now;
                ESP_LOGW(TAG, "audio notify failed rc=%d len=%d",
                         rc, kSeqHeaderBytes + (int)enc.encoded_bytes);
            }
        }
    }
}

// Build the static LVGL scene once: a big status word, a network sub-line, and
// a mic level bar. lcd_task mutates their text/colors. Must hold the LVGL lock.

// --- WAKE animation helpers (must hold the LVGL lock) ---
static void wake_caret_timer_cb(lv_timer_t *t)
{
    lv_obj_t *c = (lv_obj_t *)lv_timer_get_user_data(t);
    bool on = lv_obj_get_style_opa(c, LV_PART_MAIN) > LV_OPA_TRANSP;
    lv_obj_set_style_opa(c, on ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
}
static void wake_caret_blink_start()
{
    if (!g_caret_timer) {
        g_caret_timer = lv_timer_create(wake_caret_timer_cb, 500, g_wake_caret);
    } else {
        lv_timer_resume(g_caret_timer);
    }
}
static void wake_caret_blink_stop()
{
    if (g_caret_timer) lv_timer_pause(g_caret_timer);
    lv_obj_set_style_opa(g_wake_caret, LV_OPA_COVER, 0);
}
static void wake_win_y_cb(void *obj, int32_t v)
{
    lv_obj_set_style_translate_y((lv_obj_t *)obj, v, 0);
}
static void wake_win_slide_in()
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, g_wake_win);
    lv_anim_set_exec_cb(&a, wake_win_y_cb);
    lv_anim_set_values(&a, 14, 0);
    lv_anim_set_time(&a, 400);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}
static void wake_show_phase1()
{
    lv_obj_clear_flag(g_wake_chip, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_wake_win, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_translate_y(g_wake_win, 0, 0);
    wake_caret_blink_stop();
    g_wake_phase2 = false;
}
static void wake_show_phase2()
{
    if (!g_wake_phase2) {
        g_wake_phase2 = true;
        lv_obj_add_flag(g_wake_chip, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_wake_win, LV_OBJ_FLAG_HIDDEN);
        wake_win_slide_in();
        wake_caret_blink_start();
    }
}
static void wake_hide_all()
{
    lv_obj_add_flag(g_wake_chip, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_wake_win, LV_OBJ_FLAG_HIDDEN);
    wake_caret_blink_stop();
    g_wake_phase2 = false;
}

// Battery indicator helpers. Body inner area is 14x5 (18x9 outer, 1px border +
// 1px pad each side); fill width scales with the percentage.
static void batt_update(int pct)
{
    uint32_t col = 0x5a5a5a;  // idle gray (matches the IDLE status color)
    if (pct < 0) {
        lv_label_set_text(g_batt_label, "--%");
        lv_obj_set_style_text_color(g_batt_label, lv_color_hex(col), 0);
        lv_obj_set_style_bg_color(g_batt_fill, lv_color_hex(0x2a2a2a), 0);
        lv_obj_set_size(g_batt_fill, 1, 5);
        return;
    }
    if (pct > 100) pct = 100;
    if (pct <= 15) col = 0xe04040;       // critical: red (REC palette)
    else if (pct <= 30) col = 0xe0a040;  // low: amber (MEMO palette)
    lv_label_set_text_fmt(g_batt_label, "%d%%", pct);
    lv_obj_set_style_text_color(g_batt_label, lv_color_hex(col), 0);
    int fill_w = pct * 14 / 100;
    if (fill_w < 1) fill_w = 1;
    lv_obj_set_style_bg_color(g_batt_fill, lv_color_hex(col), 0);
    lv_obj_set_size(g_batt_fill, fill_w, 5);
}

// Read the battery voltage off the PMIC and map it to a percent. Pure I2C +
// math, no LVGL — safe to call outside the display lock. Returns -1 when there
// is no battery or the read failed (UI then shows "--%").
static int batt_read_pct()
{
    uint16_t mv = 0;
    if (g_pmic.readVbat(&mv) != M5PM1_OK || mv < 3000) return -1;
    // Linear LiPo mapping: 3.3V empty .. 4.2V full.
    int pct = (int)((int32_t)mv - 3300) * 100 / 900;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

static void build_ui()
{
    lvgl_port_lock(0);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    // Big status label + small sub-line, centered as a pair (like the demo).
    g_ui_status = lv_label_create(scr);
    lv_obj_set_style_text_font(g_ui_status, &zh28, 0);  // CJK-capable (语音输入/碎碎念/Controller)
    lv_obj_set_style_text_color(g_ui_status, lv_color_hex(0x888888), 0);
    lv_label_set_text(g_ui_status, "IDLE");
    lv_obj_align(g_ui_status, LV_ALIGN_CENTER, 0, -14);

    g_ui_sub = lv_label_create(scr);
    lv_obj_set_style_text_font(g_ui_sub, &zh14, 0);  // CJK-capable (timer + wake text)
    lv_obj_set_style_text_color(g_ui_sub, lv_color_hex(0x00aaff), 0);
    lv_label_set_text(g_ui_sub, "ble...");
    lv_obj_align(g_ui_sub, LV_ALIGN_CENTER, 0, 20);

    // Mic level bar along the bottom, like the demo.
    g_ui_bar = lv_bar_create(scr);
    lv_obj_set_size(g_ui_bar, kLcdW - 30, 5);  // demo bar is 5px tall
    lv_obj_align(g_ui_bar, LV_ALIGN_BOTTOM_MID, 0, -66);
    lv_bar_set_range(g_ui_bar, 0, 100);
    lv_bar_set_value(g_ui_bar, 0, LV_ANIM_OFF);
    // Track matches the demo (#1a1a1a). The default theme paints the track at
    // LV_OPA_20, so force full opacity or it's nearly invisible on black. Same
    // for the radius: the demo uses 3px, not the theme's full-circle pill.
    lv_obj_set_style_bg_color(g_ui_bar, lv_color_hex(0x1a1a1a), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_ui_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(g_ui_bar, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(g_ui_bar, 3, LV_PART_INDICATOR);

    // Thin divider + bottom hint line (matches the demo HTML).
    g_ui_line = lv_obj_create(scr);
    lv_obj_remove_style_all(g_ui_line);
    lv_obj_set_size(g_ui_line, kLcdW - 30, 1);
    lv_obj_set_style_bg_color(g_ui_line, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_bg_opa(g_ui_line, LV_OPA_COVER, 0);  // remove_style_all cleared bg_opa
    lv_obj_align(g_ui_line, LV_ALIGN_BOTTOM_MID, 0, -54);

    g_ui_foot = lv_label_create(scr);
    lv_obj_set_style_text_font(g_ui_foot, &zh10, 0);  // 10px CJK so the hint fits
    lv_obj_set_style_text_color(g_ui_foot, lv_color_hex(0x555555), 0);
    lv_label_set_text(g_ui_foot, "");
    lv_obj_align(g_ui_foot, LV_ALIGN_BOTTOM_MID, 0, -18);

    // --- WAKE feedback widgets (hidden until B long-press) ---
    g_wake_chip = lv_label_create(scr);
    lv_obj_add_flag(g_wake_chip, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(g_wake_chip, "WAKING");
    lv_obj_set_style_text_font(g_wake_chip, &zh14, 0);
    lv_obj_set_style_text_color(g_wake_chip, lv_color_hex(0x2ee08f), 0);
    lv_obj_set_style_bg_color(g_wake_chip, lv_color_hex(0x16382a), 0);
    lv_obj_set_style_bg_opa(g_wake_chip, LV_OPA_COVER, 0);  // labels default to bg_opa 0
    lv_obj_set_style_border_color(g_wake_chip, lv_color_hex(0x1f5c41), 0);
    lv_obj_set_style_border_width(g_wake_chip, 1, 0);
    lv_obj_set_style_radius(g_wake_chip, 4, 0);
    lv_obj_set_style_pad_hor(g_wake_chip, 8, 0);
    lv_obj_set_style_pad_ver(g_wake_chip, 3, 0);
    lv_obj_align(g_wake_chip, LV_ALIGN_CENTER, 0, -34);

    // White "workbench window": title on top, bordered input box below.
    g_wake_win = lv_obj_create(scr);
    lv_obj_add_flag(g_wake_win, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_style_all(g_wake_win);
    lv_obj_set_size(g_wake_win, 90, 44);
    lv_obj_set_style_bg_color(g_wake_win, lv_color_hex(0xf2f2f2), 0);
    lv_obj_set_style_bg_opa(g_wake_win, LV_OPA_COVER, 0);  // remove_style_all cleared bg_opa
    lv_obj_set_style_radius(g_wake_win, 5, 0);
    lv_obj_set_style_pad_all(g_wake_win, 6, LV_PART_MAIN);
    lv_obj_align(g_wake_win, LV_ALIGN_CENTER, 0, 16);

    g_wake_win_title = lv_label_create(g_wake_win);
    lv_label_set_text(g_wake_win_title, "AI 工作台");
    lv_obj_set_style_text_font(g_wake_win_title, &zh14, 0);
    lv_obj_set_style_text_color(g_wake_win_title, lv_color_hex(0x666666), 0);
    lv_obj_align(g_wake_win_title, LV_ALIGN_TOP_MID, 0, 0);

    g_wake_input = lv_obj_create(g_wake_win);
    lv_obj_remove_style_all(g_wake_input);
    lv_obj_set_size(g_wake_input, 74, 12);
    lv_obj_align(g_wake_input, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_bg_color(g_wake_input, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(g_wake_input, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(g_wake_input, lv_color_hex(0x00b36b), 0);
    lv_obj_set_style_border_width(g_wake_input, 1, 0);
    lv_obj_set_style_radius(g_wake_input, 2, 0);

    // Blinking caret inside the input box.
    g_wake_caret = lv_obj_create(g_wake_input);
    lv_obj_remove_style_all(g_wake_caret);
    lv_obj_set_size(g_wake_caret, 1, 6);
    lv_obj_align(g_wake_caret, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_set_style_bg_color(g_wake_caret, lv_color_hex(0x00b36b), 0);
    lv_obj_set_style_bg_opa(g_wake_caret, LV_OPA_COVER, 0);

    // Battery indicator, top-right. Persistent across all states (wake window is
    // centered, so it never overlaps). % label sits left of a small battery body
    // whose fill tracks the charge; refreshed in lcd_task every ~7.5s.
    g_batt_icon = lv_obj_create(scr);
    lv_obj_remove_style_all(g_batt_icon);
    lv_obj_set_size(g_batt_icon, 18, 9);
    lv_obj_set_style_border_color(g_batt_icon, lv_color_hex(0x666666), 0);
    lv_obj_set_style_border_width(g_batt_icon, 1, 0);
    lv_obj_set_style_radius(g_batt_icon, 2, 0);
    lv_obj_set_style_pad_all(g_batt_icon, 1, 0);
    lv_obj_align(g_batt_icon, LV_ALIGN_TOP_RIGHT, -3, 8);  // y=8: same line as "Controller"

    g_batt_fill = lv_obj_create(g_batt_icon);
    lv_obj_remove_style_all(g_batt_fill);
    lv_obj_set_style_bg_color(g_batt_fill, lv_color_hex(0x5a5a5a), 0);
    lv_obj_set_style_bg_opa(g_batt_fill, LV_OPA_COVER, 0);
    lv_obj_set_size(g_batt_fill, 1, 5);
    lv_obj_align(g_batt_fill, LV_ALIGN_LEFT_MID, 0, 0);

    // Little nub poking out of the body's right edge, so it reads as a battery.
    lv_obj_t *batt_nub = lv_obj_create(g_batt_icon);
    lv_obj_remove_style_all(batt_nub);
    lv_obj_set_size(batt_nub, 2, 5);
    lv_obj_set_style_bg_color(batt_nub, lv_color_hex(0x666666), 0);
    lv_obj_set_style_bg_opa(batt_nub, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(batt_nub, 1, 0);
    lv_obj_align(batt_nub, LV_ALIGN_RIGHT_MID, 1, 0);

    g_batt_label = lv_label_create(scr);
    lv_obj_set_style_text_font(g_batt_label, &zh10, 0);
    lv_obj_set_style_text_color(g_batt_label, lv_color_hex(0x5a5a5a), 0);
    lv_label_set_text(g_batt_label, "--%");
    lv_obj_align(g_batt_label, LV_ALIGN_TOP_RIGHT, -25, 8);  // y=8: same line as "Controller"

    // Brand wordmark, top-left — mirrors the demo HTML .scr-brand ("Controller",
    // ~9px, letter-spaced, #333). Persistent like the battery, since it's on
    // every demo screen; the centered status/wake widgets never reach it.
    lv_obj_t *g_brand = lv_label_create(scr);
    lv_obj_set_style_text_font(g_brand, &zh10, 0);
    lv_obj_set_style_text_color(g_brand, lv_color_hex(0x333333), 0);
    lv_obj_set_style_text_letter_space(g_brand, 1, 0);
    lv_label_set_text(g_brand, "Controller");
    lv_obj_align(g_brand, LV_ALIGN_TOP_LEFT, 10, 8);

    batt_update(-1);  // "--%" until the first vbat read lands
    lvgl_port_unlock();
}

// LCD task: reflects the Controller Controller demo UI states — pairing code when
// unpaired, IDLE when connected, REC/MEMO with elapsed time + level bar while
// recording, and a short WAKE flash after B long-press. A bottom hint line
// shows what the buttons do in the current state. Colors are matched exactly
// to the demo HTML palette (#e04040 REC, #e0a040 MEMO, #00ffaa pair,
// #2ee08f wake, #5a5a5a/#3a6a4a idle). Polls every 150ms.
static void lcd_task(void *)
{
    bool prev_rec = false;
    int64_t rec_start = 0;
    int batt_pct = -1;
    int batt_tick = 50;  // read vbat on the first loop, then every 50 (~7.5s)
    while (true) {
        bool rec = g_rec_mode != kRecNone;
        bool net = g_ble_connected;
        if (rec && !prev_rec) rec_start = esp_timer_get_time();
        prev_rec = rec;

        // B long-press just woke the workbench: flash the feedback for ~3s.
        bool wake = esp_timer_get_time() - g_wake_flash_us < kWakeFlashDurationUs;

        // Battery: read the PMIC outside the LVGL lock (I2C ~ms), update the
        // widgets below inside it. First loop and then every ~7.5s.
        if (++batt_tick >= 50) {
            batt_tick = 0;
            batt_pct = batt_read_pct();
        }

        lvgl_port_lock(0);
        if (wake) {
            // B long-press: green "WAKING" chip for a moment, then the white
            // workbench window slides in with a blinking caret. cmd=3 has
            // already fired to the host, which decides what to actually wake.
            lv_obj_add_flag(g_ui_status, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(g_ui_sub, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(g_ui_bar, lv_color_hex(0x1a1a1a), LV_PART_INDICATOR);
            if (esp_timer_get_time() - g_wake_flash_us < kWakeChipDurationUs) {
                wake_show_phase1();
                lv_label_set_text(g_ui_foot, "");
            } else {
                wake_show_phase2();
                lv_label_set_text(g_ui_foot, "已定位到输入框");
                lv_obj_set_style_text_color(g_ui_foot, lv_color_hex(0x2a2a2a), 0);
            }
        } else if (rec) {
            // Palette matches the demo exactly: voice REC #e04040, memo #e0a040.
            wake_hide_all();
            lv_obj_clear_flag(g_ui_status, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(g_ui_sub, LV_OBJ_FLAG_HIDDEN);
            bool memo = g_rec_mode == kRecMemo;
            uint32_t col = memo ? 0xe0a040 : 0xe04040;
            lv_label_set_text(g_ui_status, memo ? "碎碎念" : "语音输入");
            lv_obj_set_style_text_color(g_ui_status, lv_color_hex(col), 0);
            int secs = (int)((esp_timer_get_time() - rec_start) / 1000000);
            lv_label_set_text_fmt(g_ui_sub, "%02d:%02d", secs / 60, secs % 60);
            lv_obj_set_style_text_color(g_ui_sub, lv_color_hex(col), 0);
            lv_label_set_text(g_ui_foot, memo ? "再点 A 键停止" : "松手停止");
            lv_obj_set_style_text_color(g_ui_foot, lv_color_hex(0x555555), 0);
            lv_obj_set_style_bg_color(g_ui_bar, lv_color_hex(col), LV_PART_INDICATOR);
        } else if (net) {
            wake_hide_all();
            lv_obj_clear_flag(g_ui_status, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(g_ui_sub, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(g_ui_status, "Controller");
            lv_obj_set_style_text_color(g_ui_status, lv_color_hex(0x5a5a5a), 0);
            lv_label_set_text(g_ui_sub, "IDLE");
            lv_obj_set_style_text_color(g_ui_sub, lv_color_hex(0x3a6a4a), 0);
            lv_label_set_text(g_ui_foot, "长按语音输入/短按碎碎念");
            lv_obj_set_style_text_color(g_ui_foot, lv_color_hex(0x3a3a3a), 0);
            lv_obj_set_style_bg_color(g_ui_bar, lv_color_hex(0x1a1a1a), LV_PART_INDICATOR);
        } else {
            // Unpaired: show the pairing code big so the user can match this
            // stick to the "StickS3-Mic-XXXXXX" entry in the host's scan list.
            wake_hide_all();
            lv_obj_clear_flag(g_ui_status, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(g_ui_sub, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(g_ui_status, "Controller");
            lv_obj_set_style_text_color(g_ui_status, lv_color_hex(0x00ffaa), 0);
            lv_label_set_text(g_ui_sub, g_pair_code);
            lv_obj_set_style_text_color(g_ui_sub, lv_color_hex(0x00ffaa), 0);
            lv_label_set_text(g_ui_foot, "等待连接");
            lv_obj_set_style_text_color(g_ui_foot, lv_color_hex(0x2a2a2a), 0);
            lv_obj_set_style_bg_color(g_ui_bar, lv_color_hex(0x1a1a1a), LV_PART_INDICATOR);
        }
        // Recording progress: fill 0..100% over the 14s segment cap, so the bar
        // grows steadily from the left edge instead of jittering with the mic.
        // At the cap the host cuts a new segment (Segmenter "max"), so the bar
        // wraps back to 0 for the next one rather than sitting at 100%.
        int pct = 0;
        if (rec) {
            int64_t rec_elapsed = esp_timer_get_time() - rec_start;
            pct = (int)((rec_elapsed % kRecProgressMaxUs) * 100 / kRecProgressMaxUs);
        }
        lv_bar_set_value(g_ui_bar, pct, LV_ANIM_OFF);

        // Top-right battery. Hidden only while build_ui hasn't run yet — the
        // widgets live on the root screen, so they never conflict with states.
        if (g_batt_icon != nullptr && batt_pct >= 0) batt_update(batt_pct);

        lvgl_port_unlock();

        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

// Button task. Both keys are active-low with internal pull-ups, polled every 20ms
// (plenty of debounce for a tactile switch). Each key is a 3-state machine
// (up -> down -> long) so a single hold can never fire both the short and the
// long action. Short/long decisions happen on the boundary:
//   A short = memo note toggle (control 4/5)
//   A long  = voice input, hold-to-talk (control 1 on press, 0 on release)
//   B short = Enter/confirm (control 2)
//   B long  = wake the AI workbench (control 3)
// send_task announces recording transitions; host does the rest.
static void button_task(void *)
{
    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << kBtnA) | (1ULL << kBtnB);
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&io));
    ESP_LOGI(TAG, "BtnA gpio=%d (short=memo, long=voice), BtnB gpio=%d (short=enter, long=wake)",
             kBtnA, kBtnB);

    enum BtnState { kUp, kDown, kLong };
    BtnState a_state = kUp, b_state = kUp;
    int64_t a_down_us = 0, b_down_us = 0;

    while (true) {
        // ---- A (GPIO11): short = memo toggle, long = hold-to-talk voice ----
        bool a = gpio_get_level(kBtnA) == 0;  // active low
        switch (a_state) {
        case kUp:
            if (a) { a_state = kDown; a_down_us = esp_timer_get_time(); }
            break;
        case kDown:
            if (!a) {
                // Released before the long-press threshold: a short press.
                // Toggle the memo note (host saves it, never injects to cursor).
                if (g_ble_connected) {
                    g_rec_mode = (g_rec_mode == kRecMemo) ? kRecNone : kRecMemo;
                    ESP_LOGI(TAG, "BtnA short -> %s",
                             g_rec_mode == kRecMemo ? "MEMO ON" : "MEMO OFF");
                } else {
                    ESP_LOGI(TAG, "BtnA short ignored (not connected)");
                }
                a_state = kUp;
            } else if (esp_timer_get_time() - a_down_us >= kBtnLongPressVoiceUs) {
                // Held past threshold: voice input. Record while held, stop on
                // release. Ignored until a host is connected (audio needs a sink).
                if (g_ble_connected) {
                    g_rec_mode = kRecVoice;
                    ESP_LOGI(TAG, "BtnA long -> VOICE (hold to talk)");
                } else {
                    ESP_LOGI(TAG, "BtnA long ignored (not connected)");
                }
                a_state = kLong;
            }
            break;
        case kLong:
            if (!a) {  // released: end the voice session
                if (g_rec_mode == kRecVoice) g_rec_mode = kRecNone;
                ESP_LOGI(TAG, "BtnA release -> mode=%u", g_rec_mode);
                a_state = kUp;
            }
            break;
        }

        // ---- B (GPIO12): short = Enter (2), long = wake workbench (3) ----
        bool b = gpio_get_level(kBtnB) == 0;  // active low
        switch (b_state) {
        case kUp:
            if (b) { b_state = kDown; b_down_us = esp_timer_get_time(); }
            break;
        case kDown:
            if (!b) {
                // Released before 0.5s: confirm/send (Enter).
                if (g_ble_connected && g_ctrl_subscribed) {
                    uint8_t cmd = 2;
                    int rc = ble_notify(g_ctrl_val_handle, &cmd, 1);
                    ESP_LOGI(TAG, "BtnB short -> enter cmd=2 rc=%d", rc);
                } else {
                    ESP_LOGI(TAG, "BtnB short ignored (not connected/subscribed)");
                }
                b_state = kUp;
            } else if (esp_timer_get_time() - b_down_us >= kBtnLongPressWakeUs) {
                // Held 0.5s: wake the AI workbench (one-shot, not repeated).
                if (g_ble_connected && g_ctrl_subscribed) {
                    uint8_t cmd = 3;
                    int rc = ble_notify(g_ctrl_val_handle, &cmd, 1);
                    if (rc == 0) g_wake_flash_us = esp_timer_get_time();
                    ESP_LOGI(TAG, "BtnB long -> wake workbench cmd=3 rc=%d", rc);
                } else {
                    ESP_LOGI(TAG, "BtnB long ignored (not connected/subscribed)");
                }
                b_state = kLong;
            }
            break;
        case kLong:
            if (!b) b_state = kUp;  // long action already fired; nothing on release
            break;
        }

        // Force idle whenever the link drops mid-recording.
        if (!g_ble_connected && g_rec_mode != kRecNone) {
            g_rec_mode = kRecNone;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

extern "C" void app_main(void)
{
    log_chip_info();
    init_nvs();
    init_i2c();
    init_pm1();
    init_i2s();
    init_es8311();
    init_afe();
    init_lcd();

    g_frame_queue = xQueueCreate(kQueueDepth, sizeof(PcmFrame));
    assert(g_frame_queue != nullptr);

    init_opus();
    init_ble();

    // feed on core1 (tight I2S read loop), fetch on core1 too (consumes AFE
    // output), send on core0 (Opus encode + BLE notify). AFE's own SE task and
    // the NimBLE host task are created internally / by init_ble.
    xTaskCreatePinnedToCore(feed_task, "feed", 4096, nullptr, 6, nullptr, 1);
    xTaskCreatePinnedToCore(fetch_task, "fetch", 6144, nullptr, 5, nullptr, 1);
    // 48KB stack: the prebuilt libopus is built with VAR_ARRAYS, so the SILK
    // encoder (silk_find_pitch_lags_FIX etc.) puts large scratch arrays on the
    // stack — 16k full-band encode peaks well past 20KB. 6KB and 20KB both
    // overflowed the moment recording began; 48KB gives solid headroom.
    // send_task needs a 48KB stack (prebuilt libopus VAR_ARRAYS puts big SILK
    // scratch arrays on the stack). Internal DRAM can't give a 48KB contiguous
    // block after AFE/LCD/Opus, so put the stack in PSRAM via ...WithCaps. The
    // stack is only touched from this task (never an ISR), so PSRAM is fine.
    BaseType_t send_ok = xTaskCreatePinnedToCoreWithCaps(
        send_task, "send", 49152, nullptr, 5, nullptr, 0,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "send_task create %s", send_ok == pdPASS ? "OK" : "FAILED");
    xTaskCreatePinnedToCore(button_task, "button", 2560, nullptr, 4, nullptr, 0);
    xTaskCreatePinnedToCore(lcd_task, "lcd", 4096, nullptr, 3, nullptr, 0);
    ESP_LOGI(TAG, "feed + fetch + send + button + lcd tasks started");
}








