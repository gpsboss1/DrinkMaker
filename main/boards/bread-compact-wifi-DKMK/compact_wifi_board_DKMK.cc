#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"

#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <driver/uart.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>
#include <esp_lcd_touch_gt911.h>
#include <esp_lvgl_port.h>
#include "MCPTools.h"

#if defined(LCD_TYPE_ILI9341_SERIAL)
#include "esp_lcd_ili9341.h"
#endif

#if defined(LCD_TYPE_GC9A01_SERIAL)
#include "esp_lcd_gc9a01.h"
static const gc9a01_lcd_init_cmd_t gc9107_lcd_init_cmds[] = {
    //  {cmd, { data }, data_size, delay_ms}
    {0xfe, (uint8_t[]){0x00}, 0, 0},
    {0xef, (uint8_t[]){0x00}, 0, 0},
    {0xb0, (uint8_t[]){0xc0}, 1, 0},
    {0xb1, (uint8_t[]){0x80}, 1, 0},
    {0xb2, (uint8_t[]){0x27}, 1, 0},
    {0xb3, (uint8_t[]){0x13}, 1, 0},
    {0xb6, (uint8_t[]){0x19}, 1, 0},
    {0xb7, (uint8_t[]){0x05}, 1, 0},
    {0xac, (uint8_t[]){0xc8}, 1, 0},
    {0xab, (uint8_t[]){0x0f}, 1, 0},
    {0x3a, (uint8_t[]){0x05}, 1, 0},
    {0xb4, (uint8_t[]){0x04}, 1, 0},
    {0xa8, (uint8_t[]){0x08}, 1, 0},
    {0xb8, (uint8_t[]){0x08}, 1, 0},
    {0xea, (uint8_t[]){0x02}, 1, 0},
    {0xe8, (uint8_t[]){0x2A}, 1, 0},
    {0xe9, (uint8_t[]){0x47}, 1, 0},
    {0xe7, (uint8_t[]){0x5f}, 1, 0},
    {0xc6, (uint8_t[]){0x21}, 1, 0},
    {0xc7, (uint8_t[]){0x15}, 1, 0},
    {0xf0,
    (uint8_t[]){0x1D, 0x38, 0x09, 0x4D, 0x92, 0x2F, 0x35, 0x52, 0x1E, 0x0C,
                0x04, 0x12, 0x14, 0x1f},
    14, 0},
    {0xf1,
    (uint8_t[]){0x16, 0x40, 0x1C, 0x54, 0xA9, 0x2D, 0x2E, 0x56, 0x10, 0x0D,
                0x0C, 0x1A, 0x14, 0x1E},
    14, 0},
    {0xf4, (uint8_t[]){0x00, 0x00, 0xFF}, 3, 0},
    {0xba, (uint8_t[]){0xFF, 0xFF}, 2, 0},
};
#endif
 
#define TAG "CompactWifiBoardDKMK"

class CompactWifiBoardDKMK : public WifiBoard {
private:
 
    Button boot_button_;
    LcdDisplay* display_;
    i2c_master_bus_handle_t touch_i2c_bus_ = nullptr;
    esp_lcd_touch_handle_t touch_ = nullptr;

    bool ProbeI2cAddress(uint8_t addr) {
        if (touch_i2c_bus_ == nullptr) {
            return false;
        }
        esp_err_t err = i2c_master_probe(touch_i2c_bus_, addr, 50);
        return err == ESP_OK;
    }

    void LogTouchI2cProbe() {
        const bool found_5d = ProbeI2cAddress(0x5D);
        const bool found_14 = ProbeI2cAddress(0x14);
        ESP_LOGI(TAG, "Touch I2C probe result: 0x5D=%s, 0x14=%s",
                 found_5d ? "ACK" : "NACK",
                 found_14 ? "ACK" : "NACK");
    }

    void InitializeTouchI2cBus() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_1,
            .sda_io_num = TOUCHPAD_SDA_PIN,
            .scl_io_num = TOUCHPAD_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &touch_i2c_bus_));
    }

    void InitializeTouch() {
        if (touch_i2c_bus_ == nullptr) {
            ESP_LOGW(TAG, "Touch I2C bus is not initialized");
            return;
        }

        LogTouchI2cProbe();

        esp_lcd_touch_config_t tp_cfg = {
            .x_max = DISPLAY_WIDTH - 1,
            .y_max = DISPLAY_HEIGHT - 1,
            .rst_gpio_num = TOUCHPAD_RST_PIN,
            .int_gpio_num = TOUCHPAD_INT_PIN,
            .levels = {
                .reset = 0,
                .interrupt = 0,
            },
            .flags = {
                .swap_xy = DISPLAY_SWAP_XY,
                .mirror_x = DISPLAY_MIRROR_X,
                .mirror_y = DISPLAY_MIRROR_Y,
            },
        };

        esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
        tp_io_config.scl_speed_hz = 100 * 1000;

        const uint8_t candidate_addrs[] = {
            static_cast<uint8_t>(tp_io_config.dev_addr),
#ifdef ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP
            static_cast<uint8_t>(ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP),
#else
            static_cast<uint8_t>(tp_io_config.dev_addr == 0x5D ? 0x14 : 0x5D),
#endif
        };

        esp_err_t last_err = ESP_FAIL;
        bool touch_inited = false;
        for (size_t i = 0; i < sizeof(candidate_addrs) / sizeof(candidate_addrs[0]); ++i) {
            tp_io_config.dev_addr = candidate_addrs[i];

            esp_lcd_panel_io_handle_t tp_io_handle = nullptr;
            last_err = esp_lcd_new_panel_io_i2c(touch_i2c_bus_, &tp_io_config, &tp_io_handle);
            if (last_err != ESP_OK) {
                ESP_LOGW(TAG, "Create GT911 panel IO failed at address 0x%02X: %s",
                         tp_io_config.dev_addr, esp_err_to_name(last_err));
                continue;
            }

            last_err = esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &touch_);
            if (last_err == ESP_OK) {
                ESP_LOGI(TAG, "GT911 initialized at I2C address 0x%02X", tp_io_config.dev_addr);
                touch_inited = true;
                break;
            }

            ESP_LOGW(TAG, "GT911 init failed at address 0x%02X: %s",
                     tp_io_config.dev_addr, esp_err_to_name(last_err));
        }

        if (!touch_inited) {
            ESP_LOGE(TAG, "GT911 init failed on all candidate addresses, touch disabled");
            return;
        }

        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = lv_display_get_default(),
            .handle = touch_,
        };
        lvgl_port_add_touch(&touch_cfg);
        ESP_LOGI(TAG, "GT911 touch initialized successfully");
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
#if defined(LCD_TYPE_ILI9341_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
#elif defined(LCD_TYPE_GC9A01_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(panel_io, &panel_config, &panel));
        gc9a01_vendor_config_t gc9107_vendor_config = {
            .init_cmds = gc9107_lcd_init_cmds,
            .init_cmds_size = sizeof(gc9107_lcd_init_cmds) / sizeof(gc9a01_lcd_init_cmd_t),
        };        
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
#endif
        
        esp_lcd_panel_reset(panel);

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
#ifdef  LCD_TYPE_GC9A01_SERIAL
        panel_config.vendor_config = &gc9107_vendor_config;
#endif
        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });
    }

    // 物联网初始化，逐步迁移到 MCP 协议
    void InitializeTools() {
        static UartTransmit uart_transmit;
        uart_transmit.InitializeTools();
        ESP_LOGI(TAG, "UartTransmit MCP tool initialized");
    }

    void InitializeUart() {
        ESP_LOGI(TAG, "初始化串口,引脚rx: %d,tx: %d", UART_RX_PIN, UART_TX_PIN);
        uart_config_t uart_config = {
            .baud_rate = 9600,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_APB,
        };
        // Apply UART configuration and install driver
        // Install driver first (RX buffer size 2048, no TX buffer, no queue)
        ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, 2048, 0, 0, NULL, 0));
        ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));
        ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    }
    
public:
    CompactWifiBoardDKMK():
        boot_button_(BOOT_BUTTON_GPIO) {
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeTouchI2cBus();
        InitializeTouch();
        InitializeButtons();
        InitializeUart();
        InitializeTools();
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }
        
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }

};

DECLARE_BOARD(CompactWifiBoardDKMK);
