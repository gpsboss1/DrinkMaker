#include "uart.h"
#include <esp_log.h>
#include "driver/uart.h"

#define TAG "uart"

// MCP工具注册
void UartTransmit::InitializeTools()
{
    auto &mcp_server = McpServer::GetInstance();
    ESP_LOGI(TAG, "开始注册串口MCP工具...");

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

    mcp_server.AddTool("self.dog.forward",
                       "通过串口发送指令 控制机器狗前进",
                       PropertyList(),
                       [this](const PropertyList &properties) -> ReturnValue
                       {
                           char cmd[] = "@QIANJIN#$";
                           uart_write_bytes(UART_PORT_NUM, cmd, strlen(cmd));
                           return "串口已发送指令";
                       });

    mcp_server.AddTool("self.dog.lizheng",
                       "通过串口发送指令 控制机器狗：立正",
                       PropertyList(),
                       [this](const PropertyList &properties) -> ReturnValue
                       {
                           char cmd[] = "@LIZHENG#$";
                           uart_write_bytes(UART_PORT_NUM, cmd, strlen(cmd));
                           return "串口已发送指令";
                       });

    mcp_server.AddTool("self.dog.paxia",
                       "通过串口发送指令 控制机器狗：趴下",
                       PropertyList(),
                       [this](const PropertyList &properties) -> ReturnValue
                       {
                           char cmd[] = "@PAXIA#$";
                           uart_write_bytes(UART_PORT_NUM, cmd, strlen(cmd));
                           return "串口已发送指令";
                       });

    // mcp_server.AddTool("self.dog.juepigu",
    //                    "通过串口发送指令 控制机器狗：撅屁股（抬尾姿势）",
    //                    PropertyList(),
    //                    [this](const PropertyList &properties) -> ReturnValue
    //                    {
    //                        char cmd[] = "@JUEPIGU#$";
    //                        uart_write_bytes(UART_PORT_NUM, cmd, strlen(cmd));
    //                        return "串口已发送指令";
    //                    });

    // mcp_server.AddTool("self.dog.dunxia",
    //                    "通过串口发送指令 控制机器狗：蹲下",
    //                    PropertyList(),
    //                    [this](const PropertyList &properties) -> ReturnValue
    //                    {
    //                        char cmd[] = "@DUNXIA#$";
    //                        uart_write_bytes(UART_PORT_NUM, cmd, strlen(cmd));
    //                        return "串口已发送指令";
    //                    });

    // mcp_server.AddTool("self.dog.pingbanzhicheng",
    //                    "通过串口发送指令 控制机器狗：平板支撑",
    //                    PropertyList(),
    //                    [this](const PropertyList &properties) -> ReturnValue
    //                    {
    //                        char cmd[] = "@PINGBANZHICHENG#$";
    //                        uart_write_bytes(UART_PORT_NUM, cmd, strlen(cmd));
    //                        return "串口已发送指令";
    //                    });

    // mcp_server.AddTool("self.dog.woshou",
    //                    "通过串口发送指令 控制机器狗：握手",
    //                    PropertyList(),
    //                    [this](const PropertyList &properties) -> ReturnValue
    //                    {
    //                        char cmd[] = "@WOSHOU#$";
    //                        uart_write_bytes(UART_PORT_NUM, cmd, strlen(cmd));
    //                        return "串口已发送指令";
    //                    });

    // mcp_server.AddTool("self.dog.shuijiao",
    //                    "通过串口发送指令 控制机器狗：睡觉姿态",
    //                    PropertyList(),
    //                    [this](const PropertyList &properties) -> ReturnValue
    //                    {
    //                        char cmd[] = "@SHUIJIAO#$";
    //                        uart_write_bytes(UART_PORT_NUM, cmd, strlen(cmd));
    //                        return "串口已发送指令";
    //                    });

    mcp_server.AddTool("self.dog.houtui",
                       "通过串口发送指令 控制机器狗：后退",
                       PropertyList(),
                       [this](const PropertyList &properties) -> ReturnValue
                       {
                           char cmd[] = "@HOUTUI#$";
                           uart_write_bytes(UART_PORT_NUM, cmd, strlen(cmd));
                           return "串口已发送指令";
                       });

    mcp_server.AddTool("self.dog.youzhuan",
                       "通过串口发送指令 控制机器狗：右转",
                       PropertyList(),
                       [this](const PropertyList &properties) -> ReturnValue
                       {
                           char cmd[] = "@YOUZHUAN#$";
                           uart_write_bytes(UART_PORT_NUM, cmd, strlen(cmd));
                           return "串口已发送指令";
                       });

    mcp_server.AddTool("self.dog.zuozhuan",
                       "通过串口发送指令 控制机器狗：左转",
                       PropertyList(),
                       [this](const PropertyList &properties) -> ReturnValue
                       {
                           char cmd[] = "@ZUOZHUAN#$";
                           uart_write_bytes(UART_PORT_NUM, cmd, strlen(cmd));
                           return "串口已发送指令";
                       });

    // mcp_server.AddTool("self.dog.yaobai",
    //                    "通过串口发送指令 控制机器狗：摇摆",
    //                    PropertyList(),
    //                    [this](const PropertyList &properties) -> ReturnValue
    //                    {
    //                        char cmd[] = "@YAOBAI#$";
    //                        uart_write_bytes(UART_PORT_NUM, cmd, strlen(cmd));
    //                        return "串口已发送指令";
    //                    });

    // mcp_server.AddTool("self.dog.batu",
    //                    "通过串口发送指令 控制机器狗：扒土（挖地动作）",
    //                    PropertyList(),
    //                    [this](const PropertyList &properties) -> ReturnValue
    //                    {
    //                        char cmd[] = "@BATU#$";
    //                        uart_write_bytes(UART_PORT_NUM, cmd, strlen(cmd));
    //                        return "串口已发送指令";
    //                    });

    // mcp_server.AddTool("self.dog.sajiao",
    //                    "通过串口发送指令 控制机器狗：撒娇动作",
    //                    PropertyList(),
    //                    [this](const PropertyList &properties) -> ReturnValue
    //                    {
    //                        char cmd[] = "@SAJIAO#$";
    //                        uart_write_bytes(UART_PORT_NUM, cmd, strlen(cmd));
    //                        return "串口已发送指令";
    //                    });

    // mcp_server.AddTool("self.dog.goudengtui",
    //                    "通过串口发送指令 控制机器狗：狗蹬腿动作",
    //                    PropertyList(),
    //                    [this](const PropertyList &properties) -> ReturnValue
    //                    {
    //                        char cmd[] = "@GOUDENGTUI#$";
    //                        uart_write_bytes(UART_PORT_NUM, cmd, strlen(cmd));
    //                        return "串口已发送指令";
    //                    });

    // mcp_server.AddTool("self.dog.guibai",
    //                    "通过串口发送指令 控制机器狗：跪拜动作",
    //                    PropertyList(),
    //                    [this](const PropertyList &properties) -> ReturnValue
    //                    {
    //                        char cmd[] = "@GUIBAI#$";
    //                        uart_write_bytes(UART_PORT_NUM, cmd, strlen(cmd));
    //                        return "串口已发送指令";
    //                    });

    // mcp_server.AddTool("self.dog.bianlian",
    //                    "通过串口发送指令 控制机器狗：变脸（快速变换动作）",
    //                    PropertyList(),
    //                    [this](const PropertyList &properties) -> ReturnValue
    //                    {
    //                        char cmd[] = "@BIANLIAN#$";
    //                        uart_write_bytes(UART_PORT_NUM, cmd, strlen(cmd));
    //                        return "串口已发送指令";
    //                    });

    // mcp_server.AddTool("self.dog.kaideng",
    //                    "通过串口发送指令 控制机器狗：开灯",
    //                    PropertyList(),
    //                    [this](const PropertyList &properties) -> ReturnValue
    //                    {
    //                        char cmd[] = "@KAIDENG#$";
    //                        uart_write_bytes(UART_PORT_NUM, cmd, strlen(cmd));
    //                        return "串口已发送指令";
    //                    });

    // mcp_server.AddTool("self.dog.guandeng",
    //                    "通过串口发送指令 控制机器狗：关灯",
    //                    PropertyList(),
    //                    [this](const PropertyList &properties) -> ReturnValue
    //                    {
    //                        char cmd[] = "@GUANDENG#$";
    //                        uart_write_bytes(UART_PORT_NUM, cmd, strlen(cmd));
    //                        return "串口已发送指令";
    //                    });

    // mcp_server.AddTool("self.dog.wendu",
    //                    "通过串口发送指令 控制机器狗：获取温度",
    //                    PropertyList(),
    //                    [this](const PropertyList &properties) -> ReturnValue
    //                    {
    //                        char cmd[] = "@WENDU#$";
    //                        uart_write_bytes(UART_PORT_NUM, cmd, strlen(cmd));
    //                        return "串口已发送指令";
    //                    });

    // mcp_server.AddTool("self.dog.yaqiang",
    //                    "通过串口发送指令 控制机器狗：气压（气强）检测",
    //                    PropertyList(),
    //                    [this](const PropertyList &properties) -> ReturnValue
    //                    {
    //                        char cmd[] = "@YAQIANG#$";
    //                        uart_write_bytes(UART_PORT_NUM, cmd, strlen(cmd));
    //                        return "串口已发送指令";
    //                    });

    // mcp_server.AddTool("self.dog.daqiya",
    //                    "通过串口发送指令 控制机器狗：大气压检测",
    //                    PropertyList(),
    //                    [this](const PropertyList &properties) -> ReturnValue
    //                    {
    //                        char cmd[] = "@DAQIYA#$";
    //                        uart_write_bytes(UART_PORT_NUM, cmd, strlen(cmd));
    //                        return "串口已发送指令";
    //                    });

    // mcp_server.AddTool("self.dog.haiba",
    //                    "通过串口发送指令 控制机器狗：海拔高度查询",
    //                    PropertyList(),
    //                    [this](const PropertyList &properties) -> ReturnValue
    //                    {
    //                        char cmd[] = "@HAIBA#$";
    //                        uart_write_bytes(UART_PORT_NUM, cmd, strlen(cmd));
    //                        return "串口已发送指令";
    //                    });

    ESP_LOGI(TAG, "串口MCP工具注册完成");
}