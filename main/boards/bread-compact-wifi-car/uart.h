#ifndef __UART_H__
#define __UART_H__

#include <driver/ledc.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <functional>
#include "config.h"
#include "mcp_server.h"

class UartTransmit {
public:
    // 基本控制方法
    void Initialize();
    void InitializeTools();  // 初始化MCP工具
    bool uart_send_byte(uint8_t b);
};

#endif // __UART_H__
