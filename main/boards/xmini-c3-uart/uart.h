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

private:
    // 硬件相关
    gpio_num_t servo_pin_;
    
    // 状态变量

    
    // 命令类型
    enum CommandType {
        CMD_SET_ANGLE,
        CMD_ROTATE_CW,
        CMD_ROTATE_CCW,
        CMD_SWEEP,
        CMD_STOP,
        CMD_RESET
    };
    
    // 命令结构
    struct ServoCommand {
        CommandType type;
        int param1;  // 角度或度数
        int param2;  // 最大角度（用于扫描）或速度
        int param3;  // 速度参数
    };
    
    // 私有方法
    void WriteAngle(int angle);
    uint32_t AngleToCompare(int angle);
    bool IsValidAngle(int angle) const;
    int ConstrainAngle(int angle) const;
    
    // 任务函数
    static void ServoTask(void* parameter);
    void ProcessCommands();
    void ExecuteSetAngle(int angle);
    void ExecuteRotate(int degrees, bool clockwise);
    void ExecuteSweep(int min_angle, int max_angle, int speed_ms);
    void SmoothMoveTo(int target_angle, int speed_ms = 500);
};

#endif // __UART_H__
