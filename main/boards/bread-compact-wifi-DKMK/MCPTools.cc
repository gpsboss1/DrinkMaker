#include "MCPTools.h"
#include <esp_log.h>
#include "driver/uart.h"
#include "board.h"
#include "display/lcd_display.h"

#include <cstring>

#define TAG "uart"

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