#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include "lvgl_display.h"
#include "gif/lvgl_gif.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <font_emoji.h>

#include <atomic>
#include <functional>
#include <memory>

#define PREVIEW_IMAGE_DURATION_MS 5000


class LcdDisplay : public LvglDisplay {
protected:
    enum class UiMode {
        Chat,
        Machine,
    };

    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    
    lv_draw_buf_t draw_buf_;
    lv_obj_t* status_bar_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* side_bar_ = nullptr;
    lv_obj_t* preview_image_ = nullptr;
    lv_obj_t* emoji_label_ = nullptr;
    lv_obj_t* emoji_image_ = nullptr;
    std::unique_ptr<LvglGif> gif_controller_ = nullptr;
    lv_obj_t* emoji_box_ = nullptr;
    lv_obj_t* chat_message_label_ = nullptr;
    lv_obj_t* machine_panel_ = nullptr;
    lv_obj_t* machine_title_label_ = nullptr;
    lv_obj_t* machine_status_label_ = nullptr;
    lv_obj_t* machine_recipe_label_ = nullptr;
    lv_obj_t* machine_temp_label_ = nullptr;
    lv_obj_t* machine_volume_label_ = nullptr;
    lv_obj_t* machine_progress_label_ = nullptr;
    lv_obj_t* machine_hint_label_ = nullptr;
    lv_obj_t* machine_start_button_ = nullptr;
    lv_obj_t* machine_start_button_label_ = nullptr;

    lv_obj_t* machine_pages_[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    lv_obj_t* drink_buttons_[4] = {nullptr, nullptr, nullptr, nullptr};
    lv_obj_t* drink_button_labels_[4] = {nullptr, nullptr, nullptr, nullptr};
    lv_obj_t* granule_value_label_ = nullptr;
    lv_obj_t* water_value_label_ = nullptr;
    lv_obj_t* temp_value_label_ = nullptr;
    lv_obj_t* brewing_animated_label_ = nullptr;
    lv_obj_t* machine_progress_bar_ = nullptr;
    lv_obj_t* machine_runtime_temp_label_ = nullptr;
    lv_obj_t* completed_title_label_ = nullptr;
    lv_obj_t* completed_info_label_ = nullptr;
    lv_obj_t* cup_popup_mask_ = nullptr;
    lv_obj_t* cup_popup_ = nullptr;
    lv_obj_t* cup_popup_tip_label_ = nullptr;
    lv_obj_t* cup_popup_confirm_button_ = nullptr;
    lv_obj_t* cup_popup_confirm_button_label_ = nullptr;
    esp_timer_handle_t machine_progress_timer_ = nullptr;
    esp_timer_handle_t machine_brew_anim_timer_ = nullptr;

    int machine_selected_drink_index_ = -1;
    bool cup_popup_validation_mode_ = false;
    bool machine_brewing_started_ = false;
    int machine_granule_g_ = 10;
    int machine_water_ml_ = 200;
    int machine_temp_c_ = 40;
    int machine_progress_percent_ = 0;
    int machine_anim_phase_ = 0;
    uint8_t brewing_stage_ = 0;

    std::function<void(uint16_t)> machine_send_powder_command_;
    std::function<void(uint16_t)> machine_send_water_command_;
    std::function<void(uint8_t)> machine_send_temp_command_;
    std::function<void()> machine_send_start_command_;

    esp_timer_handle_t preview_timer_ = nullptr;
    std::unique_ptr<LvglImage> preview_image_cached_ = nullptr;
    UiMode ui_mode_ = UiMode::Machine;

    void InitializeLcdThemes();
    void SetupUI();
    void SetupMachinePanel();
    void ApplyUiModeLocked();
    void UpdateMachinePanelMessage(const char* role, const char* content);
    void SwitchMachinePage(int page_index);
    void SetDrinkSelection(int index);
    void AdjustGranule(int delta);
    void AdjustWater(int delta);
    void AdjustTemp(int delta);
    void UpdateMachineValueLabels();
    void UpdateDrinkButtonStyles();
    void ShowCupPopup(bool show);
    void StartBrewingFlow();
    void StopBrewingFlow();
    void OnBrewingTick();
    void OnBrewAnimTick();
    void UpdateCompletedSummary();
    void ResetMachineFlow();
    void ApplyButtonStyle(lv_obj_t* button, lv_obj_t* label, lv_color_t bg, lv_color_t text, lv_coord_t radius);
    void AddButtonPressFeedback(lv_obj_t* button);
    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

protected:
    // 添加protected构造函数
    LcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width, int height);
    
public:
    ~LcdDisplay();
    virtual void SetStatus(const char* status) override;
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetChatMessage(const char* role, const char* content) override; 
    virtual void SetPreviewImage(std::unique_ptr<LvglImage> image) override;
    void SetUiModeByName(const std::string& mode_name);
    std::string GetUiModeName() const;
    void SetMachinePowderCommandSender(std::function<void(uint16_t)> callback);
    void SetMachineWaterCommandSender(std::function<void(uint16_t)> callback);
    void SetMachineTempCommandSender(std::function<void(uint8_t)> callback);
    void SetMachineStartCommandSender(std::function<void()> callback);
    void OnStm32StatusReport(uint8_t state);
    void OnStm32ErrorReport(uint8_t err);
    void OnStm32TelemetryReport(uint8_t stage, float current_weight_g, float water_temp_c, uint8_t heat_on, uint8_t set_temp_c);
    void OnStm32BrewDone();

    // Add theme switching function
    virtual void SetTheme(Theme* theme) override;
};

// SPI LCD显示器
class SpiLcdDisplay : public LcdDisplay {
public:
    SpiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                  int width, int height, int offset_x, int offset_y,
                  bool mirror_x, bool mirror_y, bool swap_xy);
};

// RGB LCD显示器
class RgbLcdDisplay : public LcdDisplay {
public:
    RgbLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                  int width, int height, int offset_x, int offset_y,
                  bool mirror_x, bool mirror_y, bool swap_xy);
};

// MIPI LCD显示器
class MipiLcdDisplay : public LcdDisplay {
public:
    MipiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                   int width, int height, int offset_x, int offset_y,
                   bool mirror_x, bool mirror_y, bool swap_xy);
};

#endif // LCD_DISPLAY_H
