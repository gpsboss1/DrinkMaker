#include "MCPTools.h"
#include <esp_log.h>
#include "driver/uart.h"
#include "board.h"
#include "display/lcd_display.h"

#include <cstring>

#define TAG "uart"

namespace {
static constexpr uint8_t kProtoHead1 = 0xAA;
static constexpr uint8_t kProtoHead2 = 0x55;
static constexpr uint8_t kProtoDevStm32 = 0x01;
static constexpr uint8_t kCmdEmergencyStop = 0x03;

uint8_t CalcProtoCrc(uint8_t dev, uint8_t cmd, uint8_t len, const uint8_t* data)
{
    uint16_t sum = static_cast<uint16_t>(dev + cmd + len);
    for (uint8_t i = 0; i < len; ++i) {
        sum += data[i];
    }
    return static_cast<uint8_t>(sum & 0xFF);
}

bool SendEmergencyStopFrame()
{
    uint8_t tx[2 + 1 + 1 + 1 + 1] = {0};
    uint8_t idx = 0;

    tx[idx++] = kProtoHead1;
    tx[idx++] = kProtoHead2;
    tx[idx++] = kProtoDevStm32;
    tx[idx++] = kCmdEmergencyStop;
    tx[idx++] = 0;
    tx[idx++] = CalcProtoCrc(kProtoDevStm32, kCmdEmergencyStop, 0, nullptr);

    int written = uart_write_bytes(UART_PORT_NUM, reinterpret_cast<const char*>(tx), idx);
    if (written != idx) {
        ESP_LOGW(TAG, "Emergency stop frame write incomplete: %d/%u", written, idx);
        return false;
    }
    return true;
}
}  // namespace

// MCP工具注册
void UartTransmit::InitializeTools()
{
    auto &mcp_server = McpServer::GetInstance();
    ESP_LOGI(TAG, "开始注册串口MCP工具...");

    mcp_server.AddTool("self.screen.set_ui_mode",
                       "设置屏幕UI模式，mode可选 chat 或 machine",
                       PropertyList({
                           Property("mode", kPropertyTypeString)
                       }),
                       [](const PropertyList &properties) -> ReturnValue
                       {
                           auto mode = properties["mode"].value<std::string>();
                           if (mode != "chat" && mode != "machine") {
                               return false;
                           }

                           auto display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay());
                           if (display == nullptr) {
                               return false;
                           }

                           display->SetUiModeByName(mode);
                           return true;
                       });

    mcp_server.AddTool("self.screen.get_ui_mode",
                       "获取当前屏幕UI模式（chat 或 machine）",
                       PropertyList(),
                       [](const PropertyList &properties) -> ReturnValue
                       {
                           auto display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay());
                           if (display == nullptr) {
                               return std::string("unknown");
                           }
                           return display->GetUiModeName();
                       });

    // === Voice Control Tools (Tier 1: Status Query) ===
    mcp_server.AddTool("self.drink.get_status",
                       "获取饮品机当前状态（JSON格式：ui_mode, drink_index, powder_g, water_ml, temp_c, stage, progress_pct, brewing）",
                       PropertyList(),
                       [](const PropertyList &properties) -> ReturnValue
                       {
                           auto display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay());
                           if (display == nullptr) {
                               return std::string(R"({"error":"display_not_found"})");
                           }
                           return display->GetMachineStatusJson();
                       });

    mcp_server.AddTool("self.drink.get_recipe",
                       "获取当前菜单配方（返回选中的饮品索引、粉末克数、水毫升、温度）",
                       PropertyList(),
                       [](const PropertyList &properties) -> ReturnValue
                       {
                           auto display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay());
                           if (display == nullptr) {
                               return std::string(R"({"error":"display_not_found"})");
                           }
                           return display->GetMachineStatusJson();
                       });

    // === Voice Control Tools (Tier 2: Process Control) ===
    mcp_server.AddTool("self.drink.set_recipe",
                       "设置饮品配方参数（powder_g: 粉末克数[5-50], water_ml: 水毫升[100-400], temp_c: 温度[20/40/60/80]）",
                       PropertyList({
                           Property("powder_g", kPropertyTypeInteger),
                           Property("water_ml", kPropertyTypeInteger),
                           Property("temp_c", kPropertyTypeInteger)
                       }),
                       [](const PropertyList &properties) -> ReturnValue
                       {
                           auto display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay());
                           if (display == nullptr) {
                               return false;
                           }

                           try {
                               int powder_g = properties["powder_g"].value<int>();
                               int water_ml = properties["water_ml"].value<int>();
                               int temp_c = properties["temp_c"].value<int>();

                               // 参数范围验证
                               if (powder_g < 5 || powder_g > 50) {
                                   ESP_LOGW(TAG, "powder_g out of range: %d", powder_g);
                                   return false;
                               }
                               if (water_ml < 100 || water_ml > 400) {
                                   ESP_LOGW(TAG, "water_ml out of range: %d", water_ml);
                                   return false;
                               }
                               // 仅允许特定温度
                               if (temp_c != 20 && temp_c != 40 && temp_c != 60 && temp_c != 80) {
                                   ESP_LOGW(TAG, "temp_c not in [20,40,60,80]: %d", temp_c);
                                   return false;
                               }

                               display->ExecutePowderCommand(powder_g);
                               display->ExecuteWaterCommand(water_ml);
                               display->ExecuteTempCommand(temp_c);
                               ESP_LOGI(TAG, "Recipe set: powder=%dg water=%dml temp=%dC", powder_g, water_ml, temp_c);
                               return true;
                           } catch (...) {
                               ESP_LOGE(TAG, "set_recipe exception");
                               return false;
                           }
                       });

    mcp_server.AddTool("self.drink.start_brew",
                       "开始酿造当前配方",
                       PropertyList(),
                       [](const PropertyList &properties) -> ReturnValue
                       {
                           auto display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay());
                           if (display == nullptr) {
                               return false;
                           }
                           display->ExecuteStartCommand();
                           ESP_LOGI(TAG, "Brew started");
                           return true;
                       });

    mcp_server.AddTool("self.drink.cancel_brew",
                       "取消当前酿造过程",
                       PropertyList(),
                       [](const PropertyList &properties) -> ReturnValue
                       {
                           auto display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay());
                           if (display == nullptr) {
                               return false;
                           }
                           display->CancelBrewCommand();
                           ESP_LOGI(TAG, "Brew cancelled");
                           return true;
                       });

    // === Voice Control Tools (Tier 3: Safety) ===
    mcp_server.AddTool("self.drink.emergency_stop",
                       "紧急停止所有制作过程（安全优先，仅在必要时使用）",
                       PropertyList(),
                       [](const PropertyList &properties) -> ReturnValue
                       {
                           auto display = dynamic_cast<LcdDisplay*>(Board::GetInstance().GetDisplay());
                           if (display == nullptr) {
                               return false;
                           }
                           if (!SendEmergencyStopFrame()) {
                               return false;
                           }
                           display->CancelBrewCommand();
                           ESP_LOGW(TAG, "EMERGENCY STOP activated!");
                           return true;
                       });

    // mcp_server.AddTool("self.LED_ON.send",
    //                    "通过串口发送指令0x31打开LED",
    //                    PropertyList(),
    //                    [this](const PropertyList &properties) -> ReturnValue
    //                    {
    //                        uint8_t data = 0x31;
    //                        uart_write_bytes(UART_PORT_NUM, (const char *)&data, 1);
    //                        return "串口已发送指令";
    //                    });

    // mcp_server.AddTool("self.LED_OFF.send",
    //                    "通过串口发送指令0x30关闭LED",
    //                    PropertyList(),
    //                    [this](const PropertyList &properties) -> ReturnValue
    //                    {
    //                        uint8_t data = 0x30;
    //                        uart_write_bytes(UART_PORT_NUM, (const char *)&data, 1);
    //                        return "串口已发送指令";
    //                    });

    // mcp_server.AddTool("self.dog.forward",
    //                    "通过串口发送指令 控制机器狗前进",
    //                    PropertyList(),
    //                    [this](const PropertyList &properties) -> ReturnValue
    //                    {
    //                        char cmd[] = "@QIANJIN#$";
    //                        uart_write_bytes(UART_PORT_NUM, cmd, strlen(cmd));
    //                        return "串口已发送指令";
    //                    });



    ESP_LOGI(TAG, "串口MCP工具注册完成");
}