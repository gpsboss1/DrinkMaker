#include "lcd_display.h"
#include "gif/lvgl_gif.h"
#include "settings.h"
#include "lvgl_theme.h"
#include "assets/lang_config.h"

#include <vector>
#include <algorithm>
#include <tuple>
#include <font_awesome.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_lvgl_port.h>
#include <esp_psram.h>
#include <cstring>
#include <cstdio>

#include "board.h"

#define TAG "LcdDisplay"

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(BUILTIN_ICON_FONT);
LV_FONT_DECLARE(font_awesome_30_4);
LV_FONT_DECLARE(font_puhui_20_4);

void LcdDisplay::InitializeLcdThemes() {
    auto text_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_TEXT_FONT);
    auto icon_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_ICON_FONT);
    auto large_icon_font = std::make_shared<LvglBuiltInFont>(&font_awesome_30_4);

    // light theme
    auto light_theme = new LvglTheme("light");
    light_theme->set_background_color(lv_color_hex(0xFFFFFF));          //rgb(255, 255, 255)
    light_theme->set_text_color(lv_color_hex(0x000000));                //rgb(0, 0, 0)
    light_theme->set_chat_background_color(lv_color_hex(0xE0E0E0));     //rgb(224, 224, 224)
    light_theme->set_user_bubble_color(lv_color_hex(0x00FF00));         //rgb(0, 128, 0)
    light_theme->set_assistant_bubble_color(lv_color_hex(0xDDDDDD));    //rgb(221, 221, 221)
    light_theme->set_system_bubble_color(lv_color_hex(0xFFFFFF));       //rgb(255, 255, 255)
    light_theme->set_system_text_color(lv_color_hex(0x000000));         //rgb(0, 0, 0)
    light_theme->set_border_color(lv_color_hex(0x000000));              //rgb(0, 0, 0)
    light_theme->set_low_battery_color(lv_color_hex(0x000000));         //rgb(0, 0, 0)
    light_theme->set_text_font(text_font);
    light_theme->set_icon_font(icon_font);
    light_theme->set_large_icon_font(large_icon_font);

    // dark theme
    auto dark_theme = new LvglTheme("dark");
    dark_theme->set_background_color(lv_color_hex(0x000000));           //rgb(0, 0, 0)
    dark_theme->set_text_color(lv_color_hex(0xFFFFFF));                 //rgb(255, 255, 255)
    dark_theme->set_chat_background_color(lv_color_hex(0x1F1F1F));      //rgb(31, 31, 31)
    dark_theme->set_user_bubble_color(lv_color_hex(0x00FF00));          //rgb(0, 128, 0)
    dark_theme->set_assistant_bubble_color(lv_color_hex(0x222222));     //rgb(34, 34, 34)
    dark_theme->set_system_bubble_color(lv_color_hex(0x000000));        //rgb(0, 0, 0)
    dark_theme->set_system_text_color(lv_color_hex(0xFFFFFF));          //rgb(255, 255, 255)
    dark_theme->set_border_color(lv_color_hex(0xFFFFFF));               //rgb(255, 255, 255)
    dark_theme->set_low_battery_color(lv_color_hex(0xFF0000));          //rgb(255, 0, 0)
    dark_theme->set_text_font(text_font);
    dark_theme->set_icon_font(icon_font);
    dark_theme->set_large_icon_font(large_icon_font);

    auto& theme_manager = LvglThemeManager::GetInstance();
    theme_manager.RegisterTheme("light", light_theme);
    theme_manager.RegisterTheme("dark", dark_theme);
}

LcdDisplay::LcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width, int height)
    : panel_io_(panel_io), panel_(panel) {
    width_ = width;
    height_ = height;

    // Initialize LCD themes
    InitializeLcdThemes();

    // Load theme from settings
    Settings settings("display", false);
    std::string theme_name = settings.GetString("theme", "light");
    current_theme_ = LvglThemeManager::GetInstance().GetTheme(theme_name);
    std::string ui_mode = settings.GetString("ui_mode", "machine");
    ui_mode_ = (ui_mode == "chat") ? UiMode::Chat : UiMode::Machine;

    // Create a timer to hide the preview image
    esp_timer_create_args_t preview_timer_args = {
        .callback = [](void* arg) {
            LcdDisplay* display = static_cast<LcdDisplay*>(arg);
            display->SetPreviewImage(nullptr);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "preview_timer",
        .skip_unhandled_events = false,
    };
    esp_timer_create(&preview_timer_args, &preview_timer_);
}

SpiLcdDisplay::SpiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                           int width, int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy)
    : LcdDisplay(panel_io, panel, width, height) {

    // draw white
    std::vector<uint16_t> buffer(width_, 0xFFFF);
    for (int y = 0; y < height_; y++) {
        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());
    }

    // Set the display to on
    ESP_LOGI(TAG, "Turning display on");
    {
        esp_err_t __err = esp_lcd_panel_disp_on_off(panel_, true);
        if (__err == ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGW(TAG, "Panel does not support disp_on_off; assuming ON");
        } else {
            ESP_ERROR_CHECK(__err);
        }
    }

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

#if CONFIG_SPIRAM
    // lv image cache, currently only PNG is supported
    size_t psram_size_mb = esp_psram_get_size() / 1024 / 1024;
    if (psram_size_mb >= 8) {
        lv_image_cache_resize(2 * 1024 * 1024, true);
        ESP_LOGI(TAG, "Use 2MB of PSRAM for image cache");
    } else if (psram_size_mb >= 2) {
        lv_image_cache_resize(512 * 1024, true);
        ESP_LOGI(TAG, "Use 512KB of PSRAM for image cache");
    }
#endif

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
#if CONFIG_SOC_CPU_CORES_NUM > 1
    port_cfg.task_affinity = 1;
#endif
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * 20),
        .double_buffer = false,
        .trans_size = 0,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = false,
        .rotation = {
            .swap_xy = swap_xy,
            .mirror_x = mirror_x,
            .mirror_y = mirror_y,
        },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = 1,
            .buff_spiram = 0,
            .sw_rotate = 0,
            .swap_bytes = 1,
            .full_refresh = 0,
            .direct_mode = 0,
        },
    };

    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }

    SetupUI();
}


// RGB LCD实现
RgbLcdDisplay::RgbLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                           int width, int height, int offset_x, int offset_y,
                           bool mirror_x, bool mirror_y, bool swap_xy)
    : LcdDisplay(panel_io, panel, width, height) {

    // draw white
    std::vector<uint16_t> buffer(width_, 0xFFFF);
    for (int y = 0; y < height_; y++) {
        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());
    }

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
    port_cfg.timer_period_ms = 50;
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .buffer_size = static_cast<uint32_t>(width_ * 20),
        .double_buffer = true,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .rotation = {
            .swap_xy = swap_xy,
            .mirror_x = mirror_x,
            .mirror_y = mirror_y,
        },
        .flags = {
            .buff_dma = 1,
            .swap_bytes = 0,
            .full_refresh = 1,
            .direct_mode = 1,
        },
    };

    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = true,
            .avoid_tearing = true,
        }
    };
    
    display_ = lvgl_port_add_disp_rgb(&display_cfg, &rgb_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add RGB display");
        return;
    }
    
    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }

    SetupUI();
}

MipiLcdDisplay::MipiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                            int width, int height,  int offset_x, int offset_y,
                            bool mirror_x, bool mirror_y, bool swap_xy)
    : LcdDisplay(panel_io, panel, width, height) {

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = panel_io,
        .panel_handle = panel,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * 50),
        .double_buffer = false,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = false,
        /* Rotation values must be same as used in esp_lcd for initial settings of the screen */
        .rotation = {
            .swap_xy = swap_xy,
            .mirror_x = mirror_x,
            .mirror_y = mirror_y,
        },
        .flags = {
            .buff_dma = true,
            .buff_spiram =false,
            .sw_rotate = true,
        },
    };

    const lvgl_port_display_dsi_cfg_t dpi_cfg = {
        .flags = {
            .avoid_tearing = false,
        }
    };
    display_ = lvgl_port_add_disp_dsi(&disp_cfg, &dpi_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }

    SetupUI();
}

LcdDisplay::~LcdDisplay() {
    if (machine_progress_timer_ != nullptr) {
        esp_timer_stop(machine_progress_timer_);
        esp_timer_delete(machine_progress_timer_);
        machine_progress_timer_ = nullptr;
    }
    if (machine_brew_anim_timer_ != nullptr) {
        esp_timer_stop(machine_brew_anim_timer_);
        esp_timer_delete(machine_brew_anim_timer_);
        machine_brew_anim_timer_ = nullptr;
    }

    SetPreviewImage(nullptr);
    
    // Clean up GIF controller
    if (gif_controller_) {
        gif_controller_->Stop();
        gif_controller_.reset();
    }
    
    if (preview_timer_ != nullptr) {
        esp_timer_stop(preview_timer_);
        esp_timer_delete(preview_timer_);
    }

    if (preview_image_ != nullptr) {
        lv_obj_del(preview_image_);
    }
    if (chat_message_label_ != nullptr) {
        lv_obj_del(chat_message_label_);
    }
    if (emoji_label_ != nullptr) {
        lv_obj_del(emoji_label_);
    }
    if (emoji_image_ != nullptr) {
        lv_obj_del(emoji_image_);
    }
    if (emoji_box_ != nullptr) {
        lv_obj_del(emoji_box_);
    }
    if (content_ != nullptr) {
        lv_obj_del(content_);
    }
    if (status_bar_ != nullptr) {
        lv_obj_del(status_bar_);
    }
    if (side_bar_ != nullptr) {
        lv_obj_del(side_bar_);
    }
    if (machine_panel_ != nullptr) {
        lv_obj_del(machine_panel_);
    }
    if (container_ != nullptr) {
        lv_obj_del(container_);
    }
    if (display_ != nullptr) {
        lv_display_delete(display_);
    }

    if (panel_ != nullptr) {
        esp_lcd_panel_del(panel_);
    }
    if (panel_io_ != nullptr) {
        esp_lcd_panel_io_del(panel_io_);
    }
}

bool LcdDisplay::Lock(int timeout_ms) {
    return lvgl_port_lock(timeout_ms);
}

void LcdDisplay::Unlock() {
    lvgl_port_unlock();
}

void LcdDisplay::SetupMachinePanel() {
    if (container_ == nullptr) {
        return;
    }

    const lv_font_t* machine_text_font = &font_puhui_20_4;

    const lv_color_t bg_white = lv_color_hex(0xFFFFFF);
    const lv_color_t text_dark = lv_color_hex(0x333333);
    const lv_color_t text_highlight = lv_color_hex(0x0066CC);
    const lv_color_t btn_default = lv_color_hex(0xE0E0E0);
    const lv_color_t btn_active = lv_color_hex(0x0066CC);
    const lv_color_t btn_minus = lv_color_hex(0xCCCCCC);
    const lv_color_t btn_next = lv_color_hex(0x009933);
    const lv_color_t btn_back = lv_color_hex(0xCC3300);
    const lv_color_t panel_bg = lv_color_hex(0xF0F0F0);

    machine_panel_ = lv_obj_create(container_);
    lv_obj_set_width(machine_panel_, LV_HOR_RES);
    lv_obj_set_flex_grow(machine_panel_, 1);
    lv_obj_set_style_radius(machine_panel_, 0, 0);
    lv_obj_set_style_border_width(machine_panel_, 0, 0);
    lv_obj_set_style_pad_all(machine_panel_, 0, 0);
    lv_obj_set_style_bg_color(machine_panel_, bg_white, 0);
    lv_obj_set_scrollbar_mode(machine_panel_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(machine_panel_, LV_OBJ_FLAG_SCROLLABLE);

    auto create_page = [&](int index, const char* title_text, bool has_action_area) {
        machine_pages_[index] = lv_obj_create(machine_panel_);
        lv_obj_set_size(machine_pages_[index], LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_pad_all(machine_pages_[index], 10, 0);
        lv_obj_set_style_pad_row(machine_pages_[index], 8, 0);
        lv_obj_set_style_border_width(machine_pages_[index], 0, 0);
        lv_obj_set_style_radius(machine_pages_[index], 0, 0);
        lv_obj_set_style_bg_color(machine_pages_[index], bg_white, 0);
        lv_obj_set_style_bg_opa(machine_pages_[index], LV_OPA_COVER, 0);
        lv_obj_set_flex_flow(machine_pages_[index], LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scrollbar_mode(machine_pages_[index], LV_SCROLLBAR_MODE_OFF);
        lv_obj_remove_flag(machine_pages_[index], LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* title = lv_label_create(machine_pages_[index]);
        lv_obj_set_width(title, LV_HOR_RES - 20);
        lv_obj_set_height(title, LV_SIZE_CONTENT);
        lv_obj_set_style_text_font(title, machine_text_font, 0);
        lv_obj_set_style_text_color(title, text_dark, 0);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_all(title, 0, 0);
        lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
        lv_label_set_text(title, title_text);

        lv_obj_t* body = lv_obj_create(machine_pages_[index]);
        lv_obj_set_width(body, LV_HOR_RES - 20);
        lv_obj_set_flex_grow(body, 1);
        lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(body, 0, 0);
        lv_obj_set_style_pad_all(body, 0, 0);
        lv_obj_set_style_pad_row(body, 10, 0);
        lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
        lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* action = nullptr;
        if (has_action_area) {
            action = lv_obj_create(machine_pages_[index]);
            lv_obj_set_size(action, LV_HOR_RES - 20, 45);
            lv_obj_set_style_bg_opa(action, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(action, 0, 0);
            lv_obj_set_style_pad_all(action, 0, 0);
            lv_obj_set_flex_flow(action, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(action, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_scrollbar_mode(action, LV_SCROLLBAR_MODE_OFF);
            lv_obj_remove_flag(action, LV_OBJ_FLAG_SCROLLABLE);
        }

        return std::make_tuple(body, action);
    };

    auto create_round_button = [&](lv_obj_t* parent, const char* text, lv_color_t bg) {
        lv_obj_t* btn = lv_obj_create(parent);
        lv_obj_set_size(btn, 40, 40);
        lv_obj_set_style_radius(btn, 20, 0);
        lv_obj_set_style_bg_color(btn, bg, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_scrollbar_mode(btn, LV_SCROLLBAR_MODE_OFF);
        lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t* label = lv_label_create(btn);
        lv_obj_set_style_text_font(label, machine_text_font, 0);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_label_set_text(label, text);
        lv_obj_center(label);
        AddButtonPressFeedback(btn);
        return btn;
    };

    auto [page1_body, page1_action] = create_page(0, "选择饮品&浓度", true);
    lv_obj_set_flex_grow(page1_body, 1);
    lv_obj_set_flex_align(page1_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_height(page1_action, 52);
    lv_obj_set_style_pad_bottom(page1_action, 10, 0);
    lv_obj_set_flex_align(page1_action, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_t* drink_wrap = lv_obj_create(page1_body);
    lv_obj_set_size(drink_wrap, 220, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(drink_wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(drink_wrap, 0, 0);
    lv_obj_set_style_pad_all(drink_wrap, 0, 0);
    lv_obj_set_style_pad_row(drink_wrap, 10, 0);
    lv_obj_set_style_pad_column(drink_wrap, 10, 0);
    lv_obj_set_flex_flow(drink_wrap, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(drink_wrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(drink_wrap, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(drink_wrap, LV_OBJ_FLAG_SCROLLABLE);

    const char* drink_names[4] = {"奶茶", "咖啡", "豆浆", "麦片"};
    for (int i = 0; i < 4; ++i) {
        drink_buttons_[i] = lv_obj_create(drink_wrap);
        lv_obj_set_size(drink_buttons_[i], 100, 40);
        lv_obj_set_style_radius(drink_buttons_[i], 8, 0);
        lv_obj_set_style_bg_color(drink_buttons_[i], btn_default, 0);
        lv_obj_set_style_border_width(drink_buttons_[i], 0, 0);
        lv_obj_set_style_pad_all(drink_buttons_[i], 0, 0);
        lv_obj_set_scrollbar_mode(drink_buttons_[i], LV_SCROLLBAR_MODE_OFF);
        lv_obj_remove_flag(drink_buttons_[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(drink_buttons_[i], LV_OBJ_FLAG_CLICKABLE);
        drink_button_labels_[i] = lv_label_create(drink_buttons_[i]);
        lv_obj_set_style_text_font(drink_button_labels_[i], machine_text_font, 0);
        lv_obj_set_style_text_color(drink_button_labels_[i], text_dark, 0);
        lv_label_set_text(drink_button_labels_[i], drink_names[i]);
        lv_obj_center(drink_button_labels_[i]);
        AddButtonPressFeedback(drink_buttons_[i]);

        lv_obj_add_event_cb(drink_buttons_[i], [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
                return;
            }
            auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            auto* target = reinterpret_cast<lv_obj_t*>(lv_event_get_target(e));
            if (display == nullptr || target == nullptr) {
                return;
            }
            for (int idx = 0; idx < 4; ++idx) {
                if (display->drink_buttons_[idx] == target) {
                    display->SetDrinkSelection(idx);
                    break;
                }
            }
        }, LV_EVENT_CLICKED, this);
    }

    lv_obj_t* granule_box = lv_obj_create(page1_body);
    lv_obj_set_size(granule_box, 220, 60);
    lv_obj_set_style_radius(granule_box, 8, 0);
    lv_obj_set_style_bg_color(granule_box, panel_bg, 0);
    lv_obj_set_style_border_width(granule_box, 0, 0);
    lv_obj_set_style_pad_all(granule_box, 5, 0);
    lv_obj_set_flex_flow(granule_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(granule_box, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(granule_box, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(granule_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* granule_name = lv_label_create(granule_box);
    lv_obj_set_width(granule_name, 88);
    lv_obj_set_style_text_align(granule_name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(granule_name, text_dark, 0);
    lv_obj_set_style_text_font(granule_name, machine_text_font, 0);
    lv_label_set_text(granule_name, "重量");

    granule_value_label_ = lv_label_create(granule_box);
    lv_obj_set_width(granule_value_label_, 66);
    lv_obj_set_style_text_align(granule_value_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_translate_x(granule_value_label_, -10, 0);
    lv_obj_set_style_text_color(granule_value_label_, text_highlight, 0);
    lv_obj_set_style_text_font(granule_value_label_, machine_text_font, 0);

    lv_obj_t* granule_buttons = lv_obj_create(granule_box);
    lv_obj_set_size(granule_buttons, 86, 40);
    lv_obj_set_style_bg_opa(granule_buttons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(granule_buttons, 0, 0);
    lv_obj_set_style_pad_all(granule_buttons, 0, 0);
    lv_obj_set_flex_flow(granule_buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(granule_buttons, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(granule_buttons, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(granule_buttons, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* granule_minus = create_round_button(granule_buttons, "-", btn_minus);
    lv_obj_t* granule_plus = create_round_button(granule_buttons, "+", btn_active);

    lv_obj_add_event_cb(granule_minus, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            if (display != nullptr) {
                display->AdjustGranule(-1);
            }
        }
    }, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(granule_plus, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            if (display != nullptr) {
                display->AdjustGranule(1);
            }
        }
    }, LV_EVENT_CLICKED, this);

    lv_obj_t* page1_next = lv_obj_create(page1_action);
    ApplyButtonStyle(page1_next, nullptr, btn_next, lv_color_white(), 8);
    lv_obj_set_size(page1_next, 200, 42);
    lv_obj_set_scrollbar_mode(page1_next, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(page1_next, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(page1_next, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* page1_next_label = lv_label_create(page1_next);
    lv_obj_set_style_text_font(page1_next_label, machine_text_font, 0);
    lv_obj_set_style_text_color(page1_next_label, lv_color_white(), 0);
    lv_label_set_text(page1_next_label, "下一步");
    lv_obj_center(page1_next_label);
    AddButtonPressFeedback(page1_next);
    lv_obj_add_event_cb(page1_next, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
            return;
        }
        auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
        if (display == nullptr) {
            return;
        }
        if (display->machine_selected_drink_index_ < 0) {
            display->cup_popup_validation_mode_ = true;
            lv_label_set_text(display->cup_popup_tip_label_, "请先选择饮品");
            lv_label_set_text(display->cup_popup_confirm_button_label_, "知道了");
            display->ShowCupPopup(true);
            return;
        }
        display->SwitchMachinePage(1);
    }, LV_EVENT_CLICKED, this);

    auto [page2_body, page2_action] = create_page(1, "选择冲调水量", true);
    lv_obj_t* water_box = lv_obj_create(page2_body);
    lv_obj_set_size(water_box, 220, 60);
    lv_obj_set_style_radius(water_box, 8, 0);
    lv_obj_set_style_bg_color(water_box, panel_bg, 0);
    lv_obj_set_style_border_width(water_box, 0, 0);
    lv_obj_set_style_pad_all(water_box, 5, 0);
    lv_obj_set_flex_flow(water_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(water_box, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(water_box, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(water_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* water_name = lv_label_create(water_box);
    lv_obj_set_width(water_name, 88);
    lv_obj_set_style_text_align(water_name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_translate_x(water_name, -5, 0);
    lv_obj_set_style_text_color(water_name, text_dark, 0);
    lv_obj_set_style_text_font(water_name, machine_text_font, 0);
    lv_label_set_text(water_name, "水量");

    water_value_label_ = lv_label_create(water_box);
    lv_obj_set_width(water_value_label_, 66);
    lv_obj_set_style_text_align(water_value_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_translate_x(water_value_label_, -15, 0);
    lv_obj_set_style_text_color(water_value_label_, text_highlight, 0);
    lv_obj_set_style_text_font(water_value_label_, machine_text_font, 0);

    lv_obj_t* water_buttons = lv_obj_create(water_box);
    lv_obj_set_size(water_buttons, 86, 40);
    lv_obj_set_style_bg_opa(water_buttons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(water_buttons, 0, 0);
    lv_obj_set_style_pad_all(water_buttons, 0, 0);
    lv_obj_set_flex_flow(water_buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(water_buttons, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(water_buttons, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(water_buttons, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* water_minus = create_round_button(water_buttons, "-", btn_minus);
    lv_obj_t* water_plus = create_round_button(water_buttons, "+", btn_active);
    lv_obj_add_event_cb(water_minus, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            if (display != nullptr) {
                display->AdjustWater(-10);
            }
        }
    }, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(water_plus, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            if (display != nullptr) {
                display->AdjustWater(10);
            }
        }
    }, LV_EVENT_CLICKED, this);

    lv_obj_t* page2_back = lv_obj_create(page2_action);
    ApplyButtonStyle(page2_back, nullptr, btn_back, lv_color_white(), 8);
    lv_obj_set_size(page2_back, 100, 45);
    lv_obj_set_scrollbar_mode(page2_back, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(page2_back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* page2_back_label = lv_label_create(page2_back);
    lv_obj_set_style_text_font(page2_back_label, machine_text_font, 0);
    lv_obj_set_style_text_color(page2_back_label, lv_color_white(), 0);
    lv_label_set_text(page2_back_label, "返回");
    lv_obj_center(page2_back_label);
    AddButtonPressFeedback(page2_back);

    lv_obj_t* page2_next = lv_obj_create(page2_action);
    ApplyButtonStyle(page2_next, nullptr, btn_next, lv_color_white(), 8);
    lv_obj_set_size(page2_next, 100, 45);
    lv_obj_set_scrollbar_mode(page2_next, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(page2_next, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* page2_next_label = lv_label_create(page2_next);
    lv_obj_set_style_text_font(page2_next_label, machine_text_font, 0);
    lv_obj_set_style_text_color(page2_next_label, lv_color_white(), 0);
    lv_label_set_text(page2_next_label, "下一步");
    lv_obj_center(page2_next_label);
    AddButtonPressFeedback(page2_next);

    lv_obj_add_event_cb(page2_back, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            if (display != nullptr) {
                display->SwitchMachinePage(0);
            }
        }
    }, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(page2_next, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            if (display != nullptr) {
                if (display->machine_send_water_command_) {
                    display->machine_send_water_command_(static_cast<uint16_t>(display->machine_water_ml_));
                }
                display->SwitchMachinePage(2);
            }
        }
    }, LV_EVENT_CLICKED, this);

    auto [page3_body, page3_action] = create_page(2, "选择冲调温度", true);
    lv_obj_t* temp_box = lv_obj_create(page3_body);
    lv_obj_set_size(temp_box, 220, 60);
    lv_obj_set_style_radius(temp_box, 8, 0);
    lv_obj_set_style_bg_color(temp_box, panel_bg, 0);
    lv_obj_set_style_border_width(temp_box, 0, 0);
    lv_obj_set_style_pad_all(temp_box, 5, 0);
    lv_obj_set_flex_flow(temp_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(temp_box, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(temp_box, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(temp_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* temp_name = lv_label_create(temp_box);
    lv_obj_set_width(temp_name, 88);
    lv_obj_set_style_text_align(temp_name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(temp_name, text_dark, 0);
    lv_obj_set_style_text_font(temp_name, machine_text_font, 0);
    lv_label_set_text(temp_name, "温度");

    temp_value_label_ = lv_label_create(temp_box);
    lv_obj_set_width(temp_value_label_, 66);
    lv_obj_set_style_text_align(temp_value_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_translate_x(temp_value_label_, -10, 0);
    lv_obj_set_style_text_color(temp_value_label_, text_highlight, 0);
    lv_obj_set_style_text_font(temp_value_label_, machine_text_font, 0);

    lv_obj_t* temp_buttons = lv_obj_create(temp_box);
    lv_obj_set_size(temp_buttons, 86, 40);
    lv_obj_set_style_bg_opa(temp_buttons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(temp_buttons, 0, 0);
    lv_obj_set_style_pad_all(temp_buttons, 0, 0);
    lv_obj_set_flex_flow(temp_buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(temp_buttons, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(temp_buttons, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(temp_buttons, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* temp_minus = create_round_button(temp_buttons, "-", btn_minus);
    lv_obj_t* temp_plus = create_round_button(temp_buttons, "+", btn_active);
    lv_obj_add_event_cb(temp_minus, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            if (display != nullptr) {
                display->AdjustTemp(-1);
            }
        }
    }, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(temp_plus, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            if (display != nullptr) {
                display->AdjustTemp(1);
            }
        }
    }, LV_EVENT_CLICKED, this);

    lv_obj_t* page3_back = lv_obj_create(page3_action);
    ApplyButtonStyle(page3_back, nullptr, btn_back, lv_color_white(), 8);
    lv_obj_set_size(page3_back, 100, 45);
    lv_obj_set_scrollbar_mode(page3_back, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(page3_back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* page3_back_label = lv_label_create(page3_back);
    lv_obj_set_style_text_font(page3_back_label, machine_text_font, 0);
    lv_obj_set_style_text_color(page3_back_label, lv_color_white(), 0);
    lv_label_set_text(page3_back_label, "返回");
    lv_obj_center(page3_back_label);
    AddButtonPressFeedback(page3_back);

    machine_start_button_ = lv_obj_create(page3_action);
    ApplyButtonStyle(machine_start_button_, nullptr, btn_next, lv_color_white(), 8);
    lv_obj_set_size(machine_start_button_, 100, 45);
    lv_obj_set_scrollbar_mode(machine_start_button_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(machine_start_button_, LV_OBJ_FLAG_SCROLLABLE);
    machine_start_button_label_ = lv_label_create(machine_start_button_);
    lv_obj_set_style_text_font(machine_start_button_label_, machine_text_font, 0);
    lv_obj_set_style_text_color(machine_start_button_label_, lv_color_white(), 0);
    lv_label_set_text(machine_start_button_label_, "开始冲调");
    lv_obj_center(machine_start_button_label_);
    AddButtonPressFeedback(machine_start_button_);

    lv_obj_add_event_cb(page3_back, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            if (display != nullptr) {
                display->SwitchMachinePage(1);
            }
        }
    }, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(machine_start_button_, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            if (display != nullptr) {
                if (display->machine_send_start_command_) {
                    display->machine_send_start_command_();
                }
                display->machine_brewing_started_ = false;
                display->cup_popup_validation_mode_ = false;
                lv_label_set_text(display->cup_popup_tip_label_, "请放置杯子后确认");
                lv_label_set_text(display->cup_popup_confirm_button_label_, "确认");
                display->ShowCupPopup(true);
            }
        }
    }, LV_EVENT_CLICKED, this);

    auto [page4_body, _page4_action] = create_page(3, "冲调中", false);
    brewing_animated_label_ = lv_label_create(page4_body);
    lv_obj_set_style_text_font(brewing_animated_label_, machine_text_font, 0);
    lv_obj_set_style_text_color(brewing_animated_label_, text_highlight, 0);
    lv_obj_set_style_text_opa(brewing_animated_label_, LV_OPA_100, 0);
    lv_obj_set_style_text_align(brewing_animated_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(brewing_animated_label_, "[BREW] 冲调中...");

    machine_progress_label_ = lv_label_create(page4_body);
    lv_obj_set_style_text_font(machine_progress_label_, machine_text_font, 0);
    lv_obj_set_style_text_color(machine_progress_label_, text_dark, 0);
    lv_obj_set_style_text_align(machine_progress_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(machine_progress_label_, "进度：0%");

    auto [page5_body, page5_action] = create_page(4, "冲调完成", true);
    lv_obj_set_flex_align(page5_action, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    completed_title_label_ = lv_label_create(page5_body);
    lv_obj_set_style_text_font(completed_title_label_, machine_text_font, 0);
    lv_obj_set_style_text_color(completed_title_label_, lv_color_hex(0x009933), 0);
    lv_obj_set_style_text_align(completed_title_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(completed_title_label_, "制作完成");

    completed_info_label_ = lv_label_create(page5_body);
    lv_obj_set_width(completed_info_label_, LV_HOR_RES - 40);
    lv_obj_set_style_text_font(completed_info_label_, machine_text_font, 0);
    lv_obj_set_style_text_color(completed_info_label_, text_dark, 0);
    lv_obj_set_style_text_align(completed_info_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(completed_info_label_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(completed_info_label_, "请取走您的饮品");

    lv_obj_t* remake_button = lv_obj_create(page5_action);
    ApplyButtonStyle(remake_button, nullptr, btn_next, lv_color_white(), 8);
    lv_obj_set_size(remake_button, 200, 42);
    lv_obj_set_scrollbar_mode(remake_button, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(remake_button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* remake_label = lv_label_create(remake_button);
    lv_obj_set_style_text_font(remake_label, machine_text_font, 0);
    lv_obj_set_style_text_color(remake_label, lv_color_white(), 0);
    lv_label_set_text(remake_label, "继续制作");
    lv_obj_center(remake_label);
    AddButtonPressFeedback(remake_button);
    lv_obj_add_event_cb(remake_button, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            if (display != nullptr) {
                display->ResetMachineFlow();
            }
        }
    }, LV_EVENT_CLICKED, this);

    cup_popup_mask_ = lv_obj_create(machine_panel_);
    lv_obj_set_size(cup_popup_mask_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(cup_popup_mask_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(cup_popup_mask_, LV_OPA_50, 0);
    lv_obj_set_style_border_width(cup_popup_mask_, 0, 0);
    lv_obj_set_style_radius(cup_popup_mask_, 0, 0);
    lv_obj_set_style_pad_all(cup_popup_mask_, 0, 0);
    lv_obj_add_flag(cup_popup_mask_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(cup_popup_mask_, LV_OBJ_FLAG_SCROLLABLE);

    cup_popup_ = lv_obj_create(cup_popup_mask_);
    lv_obj_set_size(cup_popup_, 200, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(cup_popup_, bg_white, 0);
    lv_obj_set_style_border_width(cup_popup_, 0, 0);
    lv_obj_set_style_radius(cup_popup_, 8, 0);
    lv_obj_set_style_pad_top(cup_popup_, 20, 0);
    lv_obj_set_style_pad_bottom(cup_popup_, 20, 0);
    lv_obj_set_style_pad_left(cup_popup_, 10, 0);
    lv_obj_set_style_pad_right(cup_popup_, 10, 0);
    lv_obj_set_style_pad_row(cup_popup_, 12, 0);
    lv_obj_set_flex_flow(cup_popup_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cup_popup_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(cup_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(cup_popup_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(cup_popup_);

    cup_popup_tip_label_ = lv_label_create(cup_popup_);
    lv_obj_set_style_text_font(cup_popup_tip_label_, machine_text_font, 0);
    lv_obj_set_style_text_color(cup_popup_tip_label_, text_dark, 0);
    lv_obj_set_style_text_align(cup_popup_tip_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(cup_popup_tip_label_, "请放置杯子后确认");

    cup_popup_confirm_button_ = lv_obj_create(cup_popup_);
    lv_obj_set_size(cup_popup_confirm_button_, 80, 35);
    lv_obj_set_scrollbar_mode(cup_popup_confirm_button_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(cup_popup_confirm_button_, LV_OBJ_FLAG_SCROLLABLE);
    ApplyButtonStyle(cup_popup_confirm_button_, nullptr, btn_active, lv_color_white(), 6);
    cup_popup_confirm_button_label_ = lv_label_create(cup_popup_confirm_button_);
    lv_obj_set_style_text_font(cup_popup_confirm_button_label_, machine_text_font, 0);
    lv_obj_set_style_text_color(cup_popup_confirm_button_label_, lv_color_white(), 0);
    lv_label_set_text(cup_popup_confirm_button_label_, "确认");
    lv_obj_center(cup_popup_confirm_button_label_);
    AddButtonPressFeedback(cup_popup_confirm_button_);
    lv_obj_add_event_cb(cup_popup_confirm_button_, [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
            auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(e));
            if (display != nullptr) {
                bool was_validation = display->cup_popup_validation_mode_;
                display->cup_popup_validation_mode_ = false;
                if (was_validation) {
                    display->ShowCupPopup(false);
                }
            }
        }
    }, LV_EVENT_CLICKED, this);

    machine_status_label_ = completed_title_label_;
    machine_recipe_label_ = completed_info_label_;
    machine_temp_label_ = temp_value_label_;
    machine_volume_label_ = water_value_label_;
    machine_hint_label_ = cup_popup_tip_label_;

    esp_timer_create_args_t machine_progress_timer_args = {
        .callback = [](void* arg) {
        auto* display = static_cast<LcdDisplay*>(arg);
        if (display != nullptr) {
            display->OnBrewingTick();
        }
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "brew_progress",
        .skip_unhandled_events = false,
    };
    esp_timer_create(&machine_progress_timer_args, &machine_progress_timer_);

    esp_timer_create_args_t machine_brew_anim_timer_args = {
        .callback = [](void* arg) {
        auto* display = static_cast<LcdDisplay*>(arg);
        if (display != nullptr) {
            display->OnBrewAnimTick();
        }
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "brew_anim",
        .skip_unhandled_events = false,
    };
    esp_timer_create(&machine_brew_anim_timer_args, &machine_brew_anim_timer_);

    UpdateMachineValueLabels();
    UpdateDrinkButtonStyles();
    SwitchMachinePage(0);
}

void LcdDisplay::ApplyButtonStyle(lv_obj_t* button, lv_obj_t* label, lv_color_t bg, lv_color_t text, lv_coord_t radius) {
    if (button == nullptr) {
        return;
    }
    lv_obj_set_style_bg_color(button, bg, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_radius(button, radius, 0);
    if (label != nullptr) {
        lv_obj_set_style_text_color(label, text, 0);
    }
}

void LcdDisplay::AddButtonPressFeedback(lv_obj_t* button) {
    if (button == nullptr) {
        return;
    }
    lv_obj_add_event_cb(button, [](lv_event_t* e) {
        auto* target = reinterpret_cast<lv_obj_t*>(lv_event_get_target(e));
        if (target == nullptr) {
            return;
        }
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_PRESSED) {
            lv_obj_set_style_bg_opa(target, LV_OPA_80, 0);
            lv_obj_set_style_transform_zoom(target, 244, 0);
        } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
            lv_obj_set_style_bg_opa(target, LV_OPA_COVER, 0);
            lv_obj_set_style_transform_zoom(target, 256, 0);
        }
    }, LV_EVENT_ALL, this);
}

void LcdDisplay::SwitchMachinePage(int page_index) {
    if (page_index < 0 || page_index > 4) {
        return;
    }
    for (int i = 0; i < 5; ++i) {
        if (machine_pages_[i] == nullptr) {
            continue;
        }
        if (i == page_index) {
            lv_obj_remove_flag(machine_pages_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(machine_pages_[i]);
        } else {
            lv_obj_add_flag(machine_pages_[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (machine_panel_ != nullptr) {
        lv_obj_move_foreground(machine_panel_);
    }
    if (cup_popup_mask_ != nullptr && lv_obj_has_flag(cup_popup_mask_, LV_OBJ_FLAG_HIDDEN) == false) {
        lv_obj_move_foreground(cup_popup_mask_);
    }
}

void LcdDisplay::SetDrinkSelection(int index) {
    if (index < 0 || index > 3) {
        return;
    }
    machine_selected_drink_index_ = index;
    UpdateDrinkButtonStyles();
}

void LcdDisplay::AdjustGranule(int delta) {
    machine_granule_g_ += delta;
    if (machine_granule_g_ < 5) {
        machine_granule_g_ = 5;
    }
    if (machine_granule_g_ > 50) {
        machine_granule_g_ = 50;
    }
    UpdateMachineValueLabels();
}

void LcdDisplay::AdjustWater(int delta) {
    machine_water_ml_ += delta;
    if (machine_water_ml_ < 50) {
        machine_water_ml_ = 50;
    }
    if (machine_water_ml_ > 500) {
        machine_water_ml_ = 500;
    }
    UpdateMachineValueLabels();
}

void LcdDisplay::AdjustTemp(int delta) {
    machine_temp_c_ += delta;
    if (machine_temp_c_ < 40) {
        machine_temp_c_ = 40;
    }
    if (machine_temp_c_ > 95) {
        machine_temp_c_ = 95;
    }
    UpdateMachineValueLabels();
}

void LcdDisplay::UpdateMachineValueLabels() {
    char text_buffer[64] = {0};
    if (granule_value_label_ != nullptr) {
        std::snprintf(text_buffer, sizeof(text_buffer), "%dg", machine_granule_g_);
        lv_label_set_text(granule_value_label_, text_buffer);
    }
    if (water_value_label_ != nullptr) {
        std::snprintf(text_buffer, sizeof(text_buffer), "%dml", machine_water_ml_);
        lv_label_set_text(water_value_label_, text_buffer);
    }
    if (temp_value_label_ != nullptr) {
        std::snprintf(text_buffer, sizeof(text_buffer), "%dC", machine_temp_c_);
        lv_label_set_text(temp_value_label_, text_buffer);
    }
    if (machine_progress_label_ != nullptr) {
        std::snprintf(text_buffer, sizeof(text_buffer), "进度：%d%%", machine_progress_percent_);
        lv_label_set_text(machine_progress_label_, text_buffer);
    }
}

void LcdDisplay::UpdateDrinkButtonStyles() {
    const lv_color_t btn_default = lv_color_hex(0xE0E0E0);
    const lv_color_t btn_active = lv_color_hex(0x0066CC);
    const lv_color_t text_dark = lv_color_hex(0x333333);
    for (int i = 0; i < 4; ++i) {
        if (drink_buttons_[i] == nullptr || drink_button_labels_[i] == nullptr) {
            continue;
        }
        bool active = (i == machine_selected_drink_index_);
        lv_obj_set_style_bg_color(drink_buttons_[i], active ? btn_active : btn_default, 0);
        lv_obj_set_style_text_color(drink_button_labels_[i], active ? lv_color_white() : text_dark, 0);
    }
}

void LcdDisplay::ShowCupPopup(bool show) {
    if (cup_popup_mask_ == nullptr) {
        return;
    }
    if (show) {
        lv_obj_remove_flag(cup_popup_mask_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(cup_popup_mask_);
    } else {
        lv_obj_add_flag(cup_popup_mask_, LV_OBJ_FLAG_HIDDEN);
    }
}

void LcdDisplay::StartBrewingFlow() {
    if (machine_brewing_started_) {
        return;
    }
    machine_brewing_started_ = true;
    machine_progress_percent_ = 0;
    machine_anim_phase_ = 0;
    UpdateMachineValueLabels();
    SwitchMachinePage(3);
    if (machine_progress_timer_ != nullptr) {
        esp_timer_stop(machine_progress_timer_);
        esp_timer_start_periodic(machine_progress_timer_, 500 * 1000);
    }
    if (machine_brew_anim_timer_ != nullptr) {
        esp_timer_stop(machine_brew_anim_timer_);
        esp_timer_start_periodic(machine_brew_anim_timer_, 250 * 1000);
    }
}

void LcdDisplay::StopBrewingFlow() {
    if (machine_progress_timer_ != nullptr) {
        esp_timer_stop(machine_progress_timer_);
    }
    if (machine_brew_anim_timer_ != nullptr) {
        esp_timer_stop(machine_brew_anim_timer_);
    }
}

void LcdDisplay::OnBrewingTick() {
    DisplayLockGuard lock(this);
    machine_progress_percent_ += 10;
    if (machine_progress_percent_ > 100) {
        machine_progress_percent_ = 100;
    }
    UpdateMachineValueLabels();
    if (machine_progress_percent_ >= 100) {
        StopBrewingFlow();
        UpdateCompletedSummary();
        SwitchMachinePage(4);
    }
}

void LcdDisplay::OnBrewAnimTick() {
    DisplayLockGuard lock(this);
    if (brewing_animated_label_ == nullptr) {
        return;
    }
    machine_anim_phase_ = (machine_anim_phase_ + 1) % 5;
    lv_opa_t opa = static_cast<lv_opa_t>(LV_OPA_60 + machine_anim_phase_ * 10);
    lv_obj_set_style_text_opa(brewing_animated_label_, opa, 0);
}

void LcdDisplay::UpdateCompletedSummary() {
    if (completed_info_label_ == nullptr) {
        return;
    }
    const char* drink_names[4] = {"奶茶", "咖啡", "豆浆", "麦片"};
    const char* drink_name = (machine_selected_drink_index_ >= 0 && machine_selected_drink_index_ < 4)
                                 ? drink_names[machine_selected_drink_index_]
                                 : "未选择";
    char summary[128] = {0};
    std::snprintf(summary, sizeof(summary), "%s | %dg | %dml | %dC",
                  drink_name, machine_granule_g_, machine_water_ml_, machine_temp_c_);
    lv_label_set_text(completed_info_label_, summary);
}

void LcdDisplay::ResetMachineFlow() {
    StopBrewingFlow();
    machine_brewing_started_ = false;
    machine_selected_drink_index_ = -1;
    machine_granule_g_ = 20;
    machine_water_ml_ = 200;
    machine_temp_c_ = 85;
    machine_progress_percent_ = 0;
    machine_anim_phase_ = 0;
    UpdateMachineValueLabels();
    UpdateDrinkButtonStyles();
    ShowCupPopup(false);
    if (completed_info_label_ != nullptr) {
        lv_label_set_text(completed_info_label_, "请取走您的饮品");
    }
    SwitchMachinePage(0);
}

void LcdDisplay::ApplyUiModeLocked() {
    // UI模式切换核心：
    // - machine 模式显示 machine_panel_，隐藏原聊天容器
    // - chat 模式反向处理
    // 注意：该函数假设调用方已经持有 LVGL 锁。
    bool show_machine = (ui_mode_ == UiMode::Machine);

    if (content_ != nullptr) {
        if (show_machine) {
            lv_obj_add_flag(content_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(content_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (emoji_label_ != nullptr) {
        if (show_machine) {
            lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (emoji_image_ != nullptr && show_machine) {
        lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
    }

    if (machine_panel_ != nullptr) {
        if (show_machine) {
            lv_obj_remove_flag(machine_panel_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(machine_panel_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void LcdDisplay::SetUiModeByName(const std::string& mode_name) {
    // 对外模式入口：解析字符串、持久化到 settings、立即刷新可见性。
    DisplayLockGuard lock(this);
    if (mode_name == "chat") {
        ui_mode_ = UiMode::Chat;
    } else {
        ui_mode_ = UiMode::Machine;
    }

    Settings settings("display", true);
    settings.SetString("ui_mode", GetUiModeName());
    ApplyUiModeLocked();
}

std::string LcdDisplay::GetUiModeName() const {
    // 给 MCP/上层调用返回当前模式名称。
    return ui_mode_ == UiMode::Machine ? "machine" : "chat";
}

void LcdDisplay::SetMachineWaterCommandSender(std::function<void(uint16_t)> callback) {
    machine_send_water_command_ = std::move(callback);
}

void LcdDisplay::SetMachineStartCommandSender(std::function<void()> callback) {
    machine_send_start_command_ = std::move(callback);
}

void LcdDisplay::OnStm32StatusReport(uint8_t state) {
    DisplayLockGuard lock(this);
    if (ui_mode_ != UiMode::Machine) {
        return;
    }

    constexpr uint8_t STATE_REPORT_IDLE = 0x00;
    constexpr uint8_t STATE_REPORT_WAIT_CUP = 0x01;
    constexpr uint8_t STATE_REPORT_FILLING = 0x04;

    if (state == STATE_REPORT_IDLE) {
        bool waiting_cup_popup_shown = (cup_popup_mask_ != nullptr) && !lv_obj_has_flag(cup_popup_mask_, LV_OBJ_FLAG_HIDDEN);
        bool waiting_cup_text_active = (cup_popup_tip_label_ != nullptr) &&
                                       (std::strcmp(lv_label_get_text(cup_popup_tip_label_), "等待检测杯子...") == 0);
        if (!cup_popup_validation_mode_ && waiting_cup_popup_shown && waiting_cup_text_active) {
            ResetMachineFlow();
        }
        return;
    }

    if (state == STATE_REPORT_WAIT_CUP) {
        if (cup_popup_tip_label_ != nullptr && !cup_popup_validation_mode_) {
            lv_label_set_text(cup_popup_tip_label_, "等待检测杯子...");
        }
        return;
    }

    if (state == STATE_REPORT_FILLING) {
        if (cup_popup_mask_ != nullptr && !lv_obj_has_flag(cup_popup_mask_, LV_OBJ_FLAG_HIDDEN) && !cup_popup_validation_mode_) {
            ShowCupPopup(false);
        }
        StartBrewingFlow();
    }
}

void LcdDisplay::SetStatus(const char* status) {
    // 保持原有状态栏逻辑，同时把状态镜像到机器面板。
    LvglDisplay::SetStatus(status);

    // 新版冲调流程中，页面内容由专用状态机维护，不再覆盖页面标题/提示文案。
    (void)status;
}

void LcdDisplay::UpdateMachinePanelMessage(const char* role, const char* content) {
    // 冲调机工作流页面文案固定，不与聊天消息混写。
    (void)role;
    (void)content;
}

#if CONFIG_USE_WECHAT_MESSAGE_STYLE
void LcdDisplay::SetupUI() {
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto large_icon_font = lvgl_theme->large_icon_font()->font();

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, lvgl_theme->text_color(), 0);
    lv_obj_set_style_bg_color(screen, lvgl_theme->background_color(), 0);

    /* Container */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_radius(container_, 0, 0);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, 0, 0);
    lv_obj_set_style_bg_color(container_, lvgl_theme->background_color(), 0);
    lv_obj_set_style_border_color(container_, lvgl_theme->border_color(), 0);

    /* Status bar */
    status_bar_ = lv_obj_create(container_);
    lv_obj_set_size(status_bar_, LV_HOR_RES, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_color(status_bar_, lvgl_theme->background_color(), 0);
    lv_obj_set_style_text_color(status_bar_, lvgl_theme->text_color(), 0);
    
    /* Content - Chat area */
    content_ = lv_obj_create(container_);
    lv_obj_set_style_radius(content_, 0, 0);
    lv_obj_set_width(content_, LV_HOR_RES);
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_set_style_pad_all(content_, lvgl_theme->spacing(4), 0);
    lv_obj_set_style_border_width(content_, 0, 0);
    lv_obj_set_style_bg_color(content_, lvgl_theme->chat_background_color(), 0); // Background for chat area

    // Enable scrolling for chat content
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(content_, LV_DIR_VER);
    
    // Create a flex container for chat messages
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(content_, lvgl_theme->spacing(4), 0); // Space between messages

    // We'll create chat messages dynamically in SetChatMessage
    chat_message_label_ = nullptr;

    /* Status bar */
    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_column(status_bar_, 0, 0);
    lv_obj_set_style_pad_top(status_bar_, lvgl_theme->spacing(2), 0);
    lv_obj_set_style_pad_bottom(status_bar_, lvgl_theme->spacing(2), 0);
    lv_obj_set_style_pad_left(status_bar_, lvgl_theme->spacing(4), 0);
    lv_obj_set_style_pad_right(status_bar_, lvgl_theme->spacing(4), 0);
    lv_obj_set_scrollbar_mode(status_bar_, LV_SCROLLBAR_MODE_OFF);
    // 设置状态栏的内容垂直居中
    lv_obj_set_flex_align(status_bar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    network_label_ = lv_label_create(status_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);
    lv_obj_set_style_text_color(network_label_, lvgl_theme->text_color(), 0);

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(notification_label_, 1);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(notification_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(status_label_, 1);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    
    volume_label_ = lv_label_create(status_bar_);
    lv_label_set_text(volume_label_, "");
    lv_obj_set_style_text_font(volume_label_, icon_font, 0);
    lv_obj_set_style_text_color(volume_label_, lvgl_theme->text_color(), 0);

    battery_label_ = lv_label_create(status_bar_);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);
    lv_obj_set_style_text_color(battery_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_margin_left(battery_label_, lvgl_theme->spacing(2), 0); // 添加左边距，与前面的元素分隔

    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, -lvgl_theme->spacing(4));
    lv_obj_set_style_bg_color(low_battery_popup_, lvgl_theme->low_battery_color(), 0);
    lv_obj_set_style_radius(low_battery_popup_, lvgl_theme->spacing(4), 0);
    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);

    emoji_image_ = lv_img_create(screen);
    lv_obj_align(emoji_image_, LV_ALIGN_TOP_MID, 0, text_font->line_height + lvgl_theme->spacing(8));

    // Display AI logo while booting
    emoji_label_ = lv_label_create(screen);
    lv_obj_center(emoji_label_);
    lv_obj_set_style_text_font(emoji_label_, large_icon_font, 0);
    lv_obj_set_style_text_color(emoji_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(emoji_label_, FONT_AWESOME_MICROCHIP_AI);

    SetupMachinePanel();
    ApplyUiModeLocked();
}
#if CONFIG_IDF_TARGET_ESP32P4
#define  MAX_MESSAGES 40
#else
#define  MAX_MESSAGES 20
#endif
void LcdDisplay::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    UpdateMachinePanelMessage(role, content);
    if (content_ == nullptr) {
        return;
    }
    
    // 检查消息数量是否超过限制
    uint32_t child_count = lv_obj_get_child_cnt(content_);
    if (child_count >= MAX_MESSAGES) {
        // 删除最早的消息（第一个子对象）
        lv_obj_t* first_child = lv_obj_get_child(content_, 0);
        lv_obj_t* last_child = lv_obj_get_child(content_, child_count - 1);
        if (first_child != nullptr) {
            lv_obj_del(first_child);
        }
        // Scroll to the last message immediately
        if (last_child != nullptr) {
            lv_obj_scroll_to_view_recursive(last_child, LV_ANIM_OFF);
        }
    }
    
    // 折叠系统消息（如果是系统消息，检查最后一个消息是否也是系统消息）
    if (strcmp(role, "system") == 0) {
        if (child_count > 0) {
            // 获取最后一个消息容器
            lv_obj_t* last_container = lv_obj_get_child(content_, child_count - 1);
            if (last_container != nullptr && lv_obj_get_child_cnt(last_container) > 0) {
                // 获取容器内的气泡
                lv_obj_t* last_bubble = lv_obj_get_child(last_container, 0);
                if (last_bubble != nullptr) {
                    // 检查气泡类型是否为系统消息
                    void* bubble_type_ptr = lv_obj_get_user_data(last_bubble);
                    if (bubble_type_ptr != nullptr && strcmp((const char*)bubble_type_ptr, "system") == 0) {
                        // 如果最后一个消息也是系统消息，则删除它
                        lv_obj_del(last_container);
                    }
                }
            }
        }
    } else {
        // 隐藏居中显示的 AI logo
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
    }

    //避免出现空的消息框
    if(strlen(content) == 0) {
        return;
    }

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();

    // Create a message bubble
    lv_obj_t* msg_bubble = lv_obj_create(content_);
    lv_obj_set_style_radius(msg_bubble, 8, 0);
    lv_obj_set_scrollbar_mode(msg_bubble, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(msg_bubble, 0, 0);
    lv_obj_set_style_pad_all(msg_bubble, lvgl_theme->spacing(4), 0);

    // Create the message text
    lv_obj_t* msg_text = lv_label_create(msg_bubble);
    lv_label_set_text(msg_text, content);
    
    // 计算文本实际宽度
    lv_coord_t text_width = lv_txt_get_width(content, strlen(content), text_font, 0);

    // 计算气泡宽度
    lv_coord_t max_width = LV_HOR_RES * 85 / 100 - 16;  // 屏幕宽度的85%
    lv_coord_t min_width = 20;  
    lv_coord_t bubble_width;
    
    // 确保文本宽度不小于最小宽度
    if (text_width < min_width) {
        text_width = min_width;
    }

    // 如果文本宽度小于最大宽度，使用文本宽度
    if (text_width < max_width) {
        bubble_width = text_width; 
    } else {
        bubble_width = max_width;
    }
    
    // 设置消息文本的宽度
    lv_obj_set_width(msg_text, bubble_width);  // 减去padding
    lv_label_set_long_mode(msg_text, LV_LABEL_LONG_WRAP);

    // 设置气泡宽度
    lv_obj_set_width(msg_bubble, bubble_width);
    lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);

    // Set alignment and style based on message role
    if (strcmp(role, "user") == 0) {
        // User messages are right-aligned with green background
        lv_obj_set_style_bg_color(msg_bubble, lvgl_theme->user_bubble_color(), 0);
        lv_obj_set_style_bg_opa(msg_bubble, LV_OPA_70, 0);
        // Set text color for contrast
        lv_obj_set_style_text_color(msg_text, lvgl_theme->text_color(), 0);
        
        // 设置自定义属性标记气泡类型
        lv_obj_set_user_data(msg_bubble, (void*)"user");
        
        // Set appropriate width for content
        lv_obj_set_width(msg_bubble, LV_SIZE_CONTENT);
        lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);
        
        // Don't grow
        lv_obj_set_style_flex_grow(msg_bubble, 0, 0);
    } else if (strcmp(role, "assistant") == 0) {
        // Assistant messages are left-aligned with white background
        lv_obj_set_style_bg_color(msg_bubble, lvgl_theme->assistant_bubble_color(), 0);
        lv_obj_set_style_bg_opa(msg_bubble, LV_OPA_70, 0);
        // Set text color for contrast
        lv_obj_set_style_text_color(msg_text, lvgl_theme->text_color(), 0);
        
        // 设置自定义属性标记气泡类型
        lv_obj_set_user_data(msg_bubble, (void*)"assistant");
        
        // Set appropriate width for content
        lv_obj_set_width(msg_bubble, LV_SIZE_CONTENT);
        lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);
        
        // Don't grow
        lv_obj_set_style_flex_grow(msg_bubble, 0, 0);
    } else if (strcmp(role, "system") == 0) {
        // System messages are center-aligned with light gray background
        lv_obj_set_style_bg_color(msg_bubble, lvgl_theme->system_bubble_color(), 0);
        lv_obj_set_style_bg_opa(msg_bubble, LV_OPA_70, 0);
        // Set text color for contrast
        lv_obj_set_style_text_color(msg_text, lvgl_theme->system_text_color(), 0);
        
        // 设置自定义属性标记气泡类型
        lv_obj_set_user_data(msg_bubble, (void*)"system");
        
        // Set appropriate width for content
        lv_obj_set_width(msg_bubble, LV_SIZE_CONTENT);
        lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);
        
        // Don't grow
        lv_obj_set_style_flex_grow(msg_bubble, 0, 0);
    }
    
    // Create a full-width container for user messages to ensure right alignment
    if (strcmp(role, "user") == 0) {
        // Create a full-width container
        lv_obj_t* container = lv_obj_create(content_);
        lv_obj_set_width(container, LV_HOR_RES);
        lv_obj_set_height(container, LV_SIZE_CONTENT);
        
        // Make container transparent and borderless
        lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(container, 0, 0);
        lv_obj_set_style_pad_all(container, 0, 0);
        
        // Move the message bubble into this container
        lv_obj_set_parent(msg_bubble, container);
        
        // Right align the bubble in the container
        lv_obj_align(msg_bubble, LV_ALIGN_RIGHT_MID, -25, 0);
        
        // Auto-scroll to this container
        lv_obj_scroll_to_view_recursive(container, LV_ANIM_ON);
    } else if (strcmp(role, "system") == 0) {
        // 为系统消息创建全宽容器以确保居中对齐
        lv_obj_t* container = lv_obj_create(content_);
        lv_obj_set_width(container, LV_HOR_RES);
        lv_obj_set_height(container, LV_SIZE_CONTENT);
        
        // 使容器透明且无边框
        lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(container, 0, 0);
        lv_obj_set_style_pad_all(container, 0, 0);
        
        // 将消息气泡移入此容器
        lv_obj_set_parent(msg_bubble, container);
        
        // 将气泡居中对齐在容器中
        lv_obj_align(msg_bubble, LV_ALIGN_CENTER, 0, 0);
        
        // 自动滚动底部
        lv_obj_scroll_to_view_recursive(container, LV_ANIM_ON);
    } else {
        // For assistant messages
        // Left align assistant messages
        lv_obj_align(msg_bubble, LV_ALIGN_LEFT_MID, 0, 0);

        // Auto-scroll to the message bubble
        lv_obj_scroll_to_view_recursive(msg_bubble, LV_ANIM_ON);
    }
    
    // Store reference to the latest message label
    chat_message_label_ = msg_text;
}

void LcdDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {
    DisplayLockGuard lock(this);
    if (content_ == nullptr) {
        return;
    }

    if (image == nullptr) {
        return;
    }
    
    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    // Create a message bubble for image preview
    lv_obj_t* img_bubble = lv_obj_create(content_);
    lv_obj_set_style_radius(img_bubble, 8, 0);
    lv_obj_set_scrollbar_mode(img_bubble, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(img_bubble, 0, 0);
    lv_obj_set_style_pad_all(img_bubble, lvgl_theme->spacing(4), 0);
    
    // Set image bubble background color (similar to system message)
    lv_obj_set_style_bg_color(img_bubble, lvgl_theme->assistant_bubble_color(), 0);
    lv_obj_set_style_bg_opa(img_bubble, LV_OPA_70, 0);
    
    // 设置自定义属性标记气泡类型
    lv_obj_set_user_data(img_bubble, (void*)"image");

    // Create the image object inside the bubble
    lv_obj_t* preview_image = lv_image_create(img_bubble);
    
    // Calculate appropriate size for the image
    lv_coord_t max_width = LV_HOR_RES * 70 / 100;  // 70% of screen width
    lv_coord_t max_height = LV_VER_RES * 50 / 100; // 50% of screen height
    
    // Calculate zoom factor to fit within maximum dimensions
    auto img_dsc = image->image_dsc();
    lv_coord_t img_width = img_dsc->header.w;
    lv_coord_t img_height = img_dsc->header.h;
    if (img_width == 0 || img_height == 0) {
        img_width = max_width;
        img_height = max_height;
        ESP_LOGW(TAG, "Invalid image dimensions: %ld x %ld, using default dimensions: %ld x %ld", img_width, img_height, max_width, max_height);
    }
    
    lv_coord_t zoom_w = (max_width * 256) / img_width;
    lv_coord_t zoom_h = (max_height * 256) / img_height;
    lv_coord_t zoom = (zoom_w < zoom_h) ? zoom_w : zoom_h;
    
    // Ensure zoom doesn't exceed 256 (100%)
    if (zoom > 256) zoom = 256;
    
    // Set image properties
    lv_image_set_src(preview_image, img_dsc);
    lv_image_set_scale(preview_image, zoom);
    
    // Add event handler to clean up LvglImage when image is deleted
    // We need to transfer ownership of the unique_ptr to the event callback
    LvglImage* raw_image = image.release(); // 释放智能指针的所有权
    lv_obj_add_event_cb(preview_image, [](lv_event_t* e) {
        LvglImage* img = (LvglImage*)lv_event_get_user_data(e);
        if (img != nullptr) {
            delete img; // 通过删除 LvglImage 对象来正确释放内存
        }
    }, LV_EVENT_DELETE, (void*)raw_image);
    
    // Calculate actual scaled image dimensions
    lv_coord_t scaled_width = (img_width * zoom) / 256;
    lv_coord_t scaled_height = (img_height * zoom) / 256;
    
    // Set bubble size to be 16 pixels larger than the image (8 pixels on each side)
    lv_obj_set_width(img_bubble, scaled_width + 16);
    lv_obj_set_height(img_bubble, scaled_height + 16);
    
    // Don't grow in flex layout
    lv_obj_set_style_flex_grow(img_bubble, 0, 0);
    
    // Center the image within the bubble
    lv_obj_center(preview_image);
    
    // Left align the image bubble like assistant messages
    lv_obj_align(img_bubble, LV_ALIGN_LEFT_MID, 0, 0);

    // Auto-scroll to the image bubble
    lv_obj_scroll_to_view_recursive(img_bubble, LV_ANIM_ON);
}
#else
void LcdDisplay::SetupUI() {
    DisplayLockGuard lock(this);
    LvglTheme* lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto large_icon_font = lvgl_theme->large_icon_font()->font();

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, lvgl_theme->text_color(), 0);
    lv_obj_set_style_bg_color(screen, lvgl_theme->background_color(), 0);

    /* Container */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_radius(container_, 0, 0);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, 0, 0);
    lv_obj_set_style_bg_color(container_, lvgl_theme->background_color(), 0);
    lv_obj_set_style_border_color(container_, lvgl_theme->border_color(), 0);

    /* Status bar */
    status_bar_ = lv_obj_create(container_);
    lv_obj_set_size(status_bar_, LV_HOR_RES, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_color(status_bar_, lvgl_theme->background_color(), 0);
    lv_obj_set_style_text_color(status_bar_, lvgl_theme->text_color(), 0);
    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_top(status_bar_, lvgl_theme->spacing(2), 0);
    lv_obj_set_style_pad_bottom(status_bar_, lvgl_theme->spacing(2), 0);
    lv_obj_set_style_pad_left(status_bar_, lvgl_theme->spacing(4), 0);
    lv_obj_set_style_pad_right(status_bar_, lvgl_theme->spacing(4), 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_column(status_bar_, 0, 0);
    
    /* Content */
    content_ = lv_obj_create(container_);
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_radius(content_, 0, 0);
    lv_obj_set_width(content_, LV_HOR_RES);
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_set_style_pad_all(content_, 0, 0);
    lv_obj_set_style_border_width(content_, 0, 0);
    lv_obj_set_style_bg_color(content_, lvgl_theme->chat_background_color(), 0);

    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN); // 垂直布局（从上到下）
    lv_obj_set_flex_align(content_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_EVENLY); // 子对象居中对齐，等距分布

    emoji_box_ = lv_obj_create(content_);
    lv_obj_set_size(emoji_box_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(emoji_box_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(emoji_box_, 0, 0);
    lv_obj_set_style_border_width(emoji_box_, 0, 0);

    emoji_label_ = lv_label_create(emoji_box_);
    lv_obj_set_style_text_font(emoji_label_, large_icon_font, 0);
    lv_obj_set_style_text_color(emoji_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(emoji_label_, FONT_AWESOME_MICROCHIP_AI);

    emoji_image_ = lv_img_create(emoji_box_);
    lv_obj_center(emoji_image_);
    lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);

    preview_image_ = lv_image_create(content_);
    lv_obj_set_size(preview_image_, width_ / 2, height_ / 2);
    lv_obj_align(preview_image_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);

    chat_message_label_ = lv_label_create(content_);
    lv_label_set_text(chat_message_label_, "");
    lv_obj_set_width(chat_message_label_, width_ * 0.9); // 限制宽度为屏幕宽度的 90%
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_WRAP); // 设置为自动换行模式
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0); // 设置文本居中对齐
    lv_obj_set_style_text_color(chat_message_label_, lvgl_theme->text_color(), 0);

    /* Status bar */
    network_label_ = lv_label_create(status_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);
    lv_obj_set_style_text_color(network_label_, lvgl_theme->text_color(), 0);

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(notification_label_, 1);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(notification_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(status_label_, 1);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);

    volume_label_ = lv_label_create(status_bar_);
    lv_label_set_text(volume_label_, "");
    lv_obj_set_style_text_font(volume_label_, icon_font, 0);
    lv_obj_set_style_text_color(volume_label_, lvgl_theme->text_color(), 0);

    battery_label_ = lv_label_create(status_bar_);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);
    lv_obj_set_style_text_color(battery_label_, lvgl_theme->text_color(), 0);

    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, -lvgl_theme->spacing(4));
    lv_obj_set_style_bg_color(low_battery_popup_, lvgl_theme->low_battery_color(), 0);
    lv_obj_set_style_radius(low_battery_popup_, lvgl_theme->spacing(4), 0);
    
    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);

    SetupMachinePanel();
    ApplyUiModeLocked();
}

void LcdDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {
    DisplayLockGuard lock(this);
    if (preview_image_ == nullptr) {
        ESP_LOGE(TAG, "Preview image is not initialized");
        return;
    }

    if (image == nullptr) {
        esp_timer_stop(preview_timer_);
        lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
        preview_image_cached_.reset();
        if (gif_controller_) {
            gif_controller_->Start();
        }
        return;
    }

    preview_image_cached_ = std::move(image);
    auto img_dsc = preview_image_cached_->image_dsc();
    // 设置图片源并显示预览图片
    lv_image_set_src(preview_image_, img_dsc);
    if (img_dsc->header.w > 0 && img_dsc->header.h > 0) {
        // zoom factor 0.5
        lv_image_set_scale(preview_image_, 128 * width_ / img_dsc->header.w);
    }

    // Hide emoji_box_
    if (gif_controller_) {
        gif_controller_->Stop();
    }
    lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    esp_timer_stop(preview_timer_);
    ESP_ERROR_CHECK(esp_timer_start_once(preview_timer_, PREVIEW_IMAGE_DURATION_MS * 1000));
}

void LcdDisplay::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    UpdateMachinePanelMessage(role, content);
    if (chat_message_label_ == nullptr) {
        return;
    }
    lv_label_set_text(chat_message_label_, content);
}
#endif

void LcdDisplay::SetEmotion(const char* emotion) {
    // Stop any running GIF animation
    if (gif_controller_) {
        DisplayLockGuard lock(this);
        gif_controller_->Stop();
        gif_controller_.reset();
    }
    
    if (emoji_image_ == nullptr) {
        return;
    }

    // Machine UI模式下不显示emoji，避免遮挡面板内容
    if (ui_mode_ == UiMode::Machine) {
        DisplayLockGuard lock(this);
        if (emoji_image_ != nullptr) {
            lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
        }
        if (emoji_label_ != nullptr) {
            lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    auto emoji_collection = static_cast<LvglTheme*>(current_theme_)->emoji_collection();
    auto image = emoji_collection != nullptr ? emoji_collection->GetEmojiImage(emotion) : nullptr;
    if (image == nullptr) {
        const char* utf8 = font_awesome_get_utf8(emotion);
        if (utf8 != nullptr && emoji_label_ != nullptr) {
            DisplayLockGuard lock(this);
            lv_label_set_text(emoji_label_, utf8);
            lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    DisplayLockGuard lock(this);
    if (image->IsGif()) {
        // Create new GIF controller
        gif_controller_ = std::make_unique<LvglGif>(image->image_dsc());
        
        if (gif_controller_->IsLoaded()) {
            // Set up frame update callback
            gif_controller_->SetFrameCallback([this]() {
                lv_image_set_src(emoji_image_, gif_controller_->image_dsc());
            });
            
            // Set initial frame and start animation
            lv_image_set_src(emoji_image_, gif_controller_->image_dsc());
            gif_controller_->Start();
            
            // Show GIF, hide others
            lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
        } else {
            ESP_LOGE(TAG, "Failed to load GIF for emotion: %s", emotion);
            gif_controller_.reset();
        }
    } else {
        lv_image_set_src(emoji_image_, image->image_dsc());
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
    }

#if CONFIG_USE_WECHAT_MESSAGE_STYLE
    // Wechat message style中，如果emotion是neutral，则不显示
    uint32_t child_count = lv_obj_get_child_cnt(content_);
    if (strcmp(emotion, "neutral") == 0 && child_count > 0) {
        // Stop GIF animation if running
        if (gif_controller_) {
            gif_controller_->Stop();
            gif_controller_.reset();
        }
        
        lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
    }
#endif
}

void LcdDisplay::SetTheme(Theme* theme) {
    DisplayLockGuard lock(this);
    
    auto lvgl_theme = static_cast<LvglTheme*>(theme);
    
    // Get the active screen
    lv_obj_t* screen = lv_screen_active();

    // Set font
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto large_icon_font = lvgl_theme->large_icon_font()->font();

    if (text_font->line_height >= 40) {
        lv_obj_set_style_text_font(volume_label_, large_icon_font, 0);
        lv_obj_set_style_text_font(battery_label_, large_icon_font, 0);
        lv_obj_set_style_text_font(network_label_, large_icon_font, 0);
    } else {
        lv_obj_set_style_text_font(volume_label_, icon_font, 0);
        lv_obj_set_style_text_font(battery_label_, icon_font, 0);
        lv_obj_set_style_text_font(network_label_, icon_font, 0);
    }

    // Set parent text color
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, lvgl_theme->text_color(), 0);

    // Set background image
    if (lvgl_theme->background_image() != nullptr) {
        lv_obj_set_style_bg_image_src(container_, lvgl_theme->background_image()->image_dsc(), 0);
    } else {
        lv_obj_set_style_bg_image_src(container_, nullptr, 0);
        lv_obj_set_style_bg_color(container_, lvgl_theme->background_color(), 0);
    }
    
    // Update status bar background color with 50% opacity
    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_50, 0);
    lv_obj_set_style_bg_color(status_bar_, lvgl_theme->background_color(), 0);
    
    // Update status bar elements
    lv_obj_set_style_text_color(network_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_text_color(status_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_text_color(notification_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_text_color(volume_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_text_color(battery_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_text_color(emoji_label_, lvgl_theme->text_color(), 0);

    // Set content background opacity
    lv_obj_set_style_bg_opa(content_, LV_OPA_TRANSP, 0);

    if (machine_panel_ != nullptr) {
        lv_obj_set_style_bg_color(machine_panel_, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_color(machine_panel_, lv_color_hex(0x333333), 0);
    }
    if (granule_value_label_ != nullptr) {
        lv_obj_set_style_text_color(granule_value_label_, lv_color_hex(0x0066CC), 0);
    }
    if (water_value_label_ != nullptr) {
        lv_obj_set_style_text_color(water_value_label_, lv_color_hex(0x0066CC), 0);
    }
    if (temp_value_label_ != nullptr) {
        lv_obj_set_style_text_color(temp_value_label_, lv_color_hex(0x0066CC), 0);
    }
    if (machine_progress_label_ != nullptr) {
        lv_obj_set_style_text_color(machine_progress_label_, lv_color_hex(0x333333), 0);
    }
    if (completed_title_label_ != nullptr) {
        lv_obj_set_style_text_color(completed_title_label_, lv_color_hex(0x009933), 0);
    }
    if (completed_info_label_ != nullptr) {
        lv_obj_set_style_text_color(completed_info_label_, lv_color_hex(0x333333), 0);
    }
    if (brewing_animated_label_ != nullptr) {
        lv_obj_set_style_text_color(brewing_animated_label_, lv_color_hex(0x0066CC), 0);
    }

    // If we have the chat message style, update all message bubbles
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
    // Iterate through all children of content (message containers or bubbles)
    uint32_t child_count = lv_obj_get_child_cnt(content_);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t* obj = lv_obj_get_child(content_, i);
        if (obj == nullptr) continue;
        
        lv_obj_t* bubble = nullptr;
        
        // 检查这个对象是容器还是气泡
        // 如果是容器（用户或系统消息），则获取其子对象作为气泡
        // 如果是气泡（助手消息），则直接使用
        if (lv_obj_get_child_cnt(obj) > 0) {
            // 可能是容器，检查它是否为用户或系统消息容器
            // 用户和系统消息容器是透明的
            lv_opa_t bg_opa = lv_obj_get_style_bg_opa(obj, 0);
            if (bg_opa == LV_OPA_TRANSP) {
                // 这是用户或系统消息的容器
                bubble = lv_obj_get_child(obj, 0);
            } else {
                // 这可能是助手消息的气泡自身
                bubble = obj;
            }
        } else {
            // 没有子元素，可能是其他UI元素，跳过
            continue;
        }
        
        if (bubble == nullptr) continue;
        
        // 使用保存的用户数据来识别气泡类型
        void* bubble_type_ptr = lv_obj_get_user_data(bubble);
        if (bubble_type_ptr != nullptr) {
            const char* bubble_type = static_cast<const char*>(bubble_type_ptr);
            
            // 根据气泡类型应用正确的颜色
            if (strcmp(bubble_type, "user") == 0) {
                lv_obj_set_style_bg_color(bubble, lvgl_theme->user_bubble_color(), 0);
            } else if (strcmp(bubble_type, "assistant") == 0) {
                lv_obj_set_style_bg_color(bubble, lvgl_theme->assistant_bubble_color(), 0); 
            } else if (strcmp(bubble_type, "system") == 0) {
                lv_obj_set_style_bg_color(bubble, lvgl_theme->system_bubble_color(), 0);
            } else if (strcmp(bubble_type, "image") == 0) {
                lv_obj_set_style_bg_color(bubble, lvgl_theme->system_bubble_color(), 0);
            }
            
            // Update border color
            lv_obj_set_style_border_color(bubble, lvgl_theme->border_color(), 0);
            
            // Update text color for the message
            if (lv_obj_get_child_cnt(bubble) > 0) {
                lv_obj_t* text = lv_obj_get_child(bubble, 0);
                if (text != nullptr) {
                    // 根据气泡类型设置文本颜色
                    if (strcmp(bubble_type, "system") == 0) {
                        lv_obj_set_style_text_color(text, lvgl_theme->system_text_color(), 0);
                    } else {
                        lv_obj_set_style_text_color(text, lvgl_theme->text_color(), 0);
                    }
                }
            }
        } else {
            ESP_LOGW(TAG, "child[%lu] Bubble type is not found", i);
        }
    }
#else
    // Simple UI mode - just update the main chat message
    if (chat_message_label_ != nullptr) {
        lv_obj_set_style_text_color(chat_message_label_, lvgl_theme->text_color(), 0);
    }
    
    if (emoji_label_ != nullptr) {
        lv_obj_set_style_text_color(emoji_label_, lvgl_theme->text_color(), 0);
    }
#endif
    
    // Update low battery popup
    lv_obj_set_style_bg_color(low_battery_popup_, lvgl_theme->low_battery_color(), 0);

    ApplyUiModeLocked();

    // No errors occurred. Save theme to settings
    Display::SetTheme(lvgl_theme);
}
