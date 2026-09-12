/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>
#include <memory>
#include <queue>
#include <string>
#include <lvgl.h>
#include <mutex>
#include <vector>

/**
 * @brief Hardware abstraction layer
 *
 */
namespace hal {

/**
 * @brief
 *
 */
class HalBase {
public:
    virtual ~HalBase() = default;

    /**
     * @brief
     *
     * @return std::string
     */
    virtual std::string type()
    {
        return "Base";
    }

    /**
     * @brief
     *
     */
    virtual void init()
    {
    }

    /* --------------------------------- System --------------------------------- */
    virtual void delay(uint32_t ms)
    {
    }
    virtual uint32_t millis()
    {
        return 0;
    }
    virtual int getCpuTemp()
    {
        return 0.0f;
    }

    /* --------------------------------- Display -------------------------------- */
    virtual int getDisplayWidth()
    {
        return 1280;
    }
    virtual int getDisplayHeight()
    {
        return 720;
    }
    virtual void setDisplayBrightness(uint8_t brightness)
    {
    }
    virtual uint8_t getDisplayBrightness()
    {
        return 0;
    }
    virtual std::string getDisplayPanelIc()
    {
        return "ILI9881C";
        // return "ST7123";
    }

    /* ---------------------------------- Lvgl ---------------------------------- */
    lv_indev_t* lvTouchpad = nullptr;
    virtual void lvglLock()
    {
    }
    virtual void lvglUnlock()
    {
    }

    /* ---------------------------------- Power --------------------------------- */
    struct PMData_t {
        float busVoltage   = 0.0f;
        float busPower     = 0.0f;
        float shuntVoltage = 0.0f;
        float shuntCurrent = 0.0f;
    };
    PMData_t powerMonitorData;
    virtual void updatePowerMonitorData()
    {
    }
    virtual void setChargeQcEnable(bool enable)
    {
    }
    virtual bool getChargeQcEnable()
    {
        return false;
    }
    virtual void setChargeEnable(bool enable)
    {
    }
    virtual bool getChargeEnable()
    {
        return false;
    }
    virtual void setUsb5vEnable(bool enable)
    {
    }
    virtual bool getUsb5vEnable()
    {
        return false;
    }
    virtual void setExt5vEnable(bool enable)
    {
    }
    virtual bool getExt5vEnable()
    {
        return false;
    }
    virtual void powerOff()
    {
    }
    virtual void sleepAndTouchWakeup()
    {
    }
    virtual void sleepAndShakeWakeup()
    {
    }
    virtual void sleepAndRtcWakeup()
    {
    }

    /* ----------------------------------- IMU ---------------------------------- */
    struct IMUData_t {
        float accelX = 0.0f;
        float accelY = 0.0f;
        float accelZ = 0.0f;
        float gyroX  = 0.0f;
        float gyroY  = 0.0f;
        float gyroZ  = 0.0f;
    };
    IMUData_t imuData;
    virtual void updateImuData()
    {
    }
    virtual void clearImuIrq()
    {
    }

    /* ----------------------------------- RTC ---------------------------------- */
    virtual void getRtcTime(tm* time)
    {
    }
    virtual void setRtcTime(tm time)
    {
    }
    virtual void clearRtcIrq()
    {
    }

    /* --------------------------------- Camera --------------------------------- */
    virtual void startCameraCapture(lv_obj_t* imgCanvas)
    {
    }
    virtual void stopCameraCapture()
    {
    }
    virtual bool isCameraCapturing()
    {
        return false;
    }

    /* ---------------------------------- USB-A --------------------------------- */
    struct HidMouseData_t {
        std::mutex mutex;
        int x         = 0;
        int y         = 0;
        bool btnLeft  = false;
        bool btnRight = false;
    };
    HidMouseData_t hidMouseData;

    /* ---------------------------------- Audio --------------------------------- */
    virtual void setSpeakerVolume(uint8_t volume)
    {
    }
    virtual uint8_t getSpeakerVolume()
    {
        return 0;
    }
    // [MIC-L, AEC, MIC-R, MIC-HP]
    virtual void audioRecord(std::vector<int16_t>& data, uint16_t durationMs, float gain = 80.0f)
    {
    }
    virtual void audioPlay(std::vector<int16_t>& data, bool async = true)
    {
    }

    // Mic record test
    enum MicTestState_t {
        MIC_TEST_IDLE,
        MIC_TEST_RECORDING,
        MIC_TEST_PLAYING,
    };
    virtual void startDualMicRecordTest()
    {
    }
    virtual MicTestState_t getDualMicRecordTestState()
    {
        return MIC_TEST_IDLE;
    }
    virtual void startHeadphoneMicRecordTest()
    {
    }
    virtual MicTestState_t getHeadphoneMicRecordTestState()
    {
        return MIC_TEST_IDLE;
    }

    // Play music test
    enum MusicPlayState_t {
        MUSIC_PLAY_IDLE,
        MUSIC_PLAY_PLAYING,
    };
    virtual void startPlayMusicTest()
    {
    }
    virtual MusicPlayState_t getMusicPlayTestState()
    {
        return MUSIC_PLAY_IDLE;
    }
    virtual void stopPlayMusicTest()
    {
    }

    // Sfx
    virtual void playStartupSfx()
    {
    }
    virtual void playShutdownSfx()
    {
    }

    /* --------------------------------- Network -------------------------------- */
    virtual void setExtAntennaEnable(bool enable)
    {
    }
    virtual bool getExtAntennaEnable()
    {
        return false;
    }
    virtual void startWifiAp()
    {
    }

    /* -------------------------------- Keyboard -------------------------------- */
    // Tab5 官方键盘（I2C 0x6D @ ExtPort1）：STRING 模式直接输出 ASCII
    virtual bool keyboardInit()
    {
        return false;
    }
    virtual bool keyboardIsReady()
    {
        return false;
    }
    virtual uint8_t keyboardReadChar()
    {
        return 0;
    }
    // 取键值：0x20~0x7E=ASCII，0x101+=特殊键（enter/backspace/delete/tab/esc/space）
    virtual uint16_t keyboardReadKey()
    {
        return 0;
    }

    // 键盘事件所在的 LVGL group（输入框要加进去才能收到实体键盘输入）
    virtual void* keyboardGetGroup()
    {
        return nullptr;
    }
    // STA（客户端）连接：连到外部路由器
    virtual bool wifiConnectSta(const char* ssid, const char* pass)
    {
        return false;
    }
    virtual bool wifiIsStaConnected()
    {
        return false;
    }
    virtual std::string wifiGetStaIp()
    {
        return "-";
    }

    /* --------------------------------- SD Card -------------------------------- */
    struct FileEntry_t {
        std::string name;
        bool isDir;
    };
    virtual bool isSdCardMounted()
    {
        return false;
    }
    virtual std::vector<FileEntry_t> scanSdCard(const std::string& dirPath)
    {
        return {};
    }

    /* -------------------------------- Interface ------------------------------- */
    virtual bool usbCDetect()
    {
        return false;
    }
    virtual bool usbADetect()
    {
        return false;
    }
    virtual bool headPhoneDetect()
    {
        return false;
    }
    virtual std::vector<uint8_t> i2cScan(bool isInternal)
    {
        return {};
    }
    virtual void initPortAI2c()
    {
    }
    virtual void deinitPortAI2c()
    {
    }

    virtual void gpioInitOutput(uint8_t pin)
    {
    }
    virtual void gpioSetLevel(uint8_t pin, bool level)
    {
    }
    virtual void gpioReset(uint8_t pin)
    {
    }

    /* ------------------------------ UART monitor ------------------------------ */
    struct UartMonitorData_t {
        std::mutex mutex;
        std::queue<uint8_t> rxQueue;
        std::queue<uint8_t> txQueue;
    };
    UartMonitorData_t uartMonitorData;
    // 串口参数（波特率）
    virtual bool setRs485Baudrate(uint32_t baud)
    {
        return false;
    }
    virtual uint32_t getRs485Baudrate()
    {
        return 115200;
    }
    // 完整串口参数：data_bits 5-8 / parity 0=none 2=even 3=odd / stop_bits 1,2(=1.5),2
    virtual bool setRs485Config(uint32_t baud, int data_bits, int parity, int stop_bits)
    {
        return false;
    }
    virtual void uartMonitorSend(std::string msg, bool newLine = true)
    {
        std::lock_guard<std::mutex> lock(uartMonitorData.mutex);
        for (auto c : msg) {
            uartMonitorData.txQueue.push(c);
        }
        if (newLine) {
            uartMonitorData.txQueue.push('\n');
        }
    }

    // ================= USB 串口（外接 USB↔485/232 适配器）=================
    // 和板载 RS485 各自独立的队列，互不干扰；谁在用由工具页的"端口"决定
    UartMonitorData_t usbSerialData;

    /**
     * 异步打开：底层 VCP::open 会阻塞等设备，故内部起任务，不能占 UI 线程
     * @param channel 1..4 = 多串口适配器的第几路（CH344 对应接口 0/2/4/6）
     */
    virtual bool usbSerialOpenAsync(int channel)
    {
        return false;
    }
    virtual void usbSerialClose()
    {
    }
    /** 0=未打开 1=正在找设备 2=已打开 3=失败或已拔出 */
    virtual int usbSerialState()
    {
        return 0;
    }
    /** 取走"设备刚被拔出"标志（取完清零），UI 用它自动收尾 */
    virtual bool usbSerialTakeDisconnected()
    {
        return false;
    }
    /** 串口参数下发到**适配器**（USB CDC line coding），不是 ESP32 的 UART */
    virtual bool usbSerialSetLineCoding(uint32_t baud, int data_bits, int parity, int stop_bits)
    {
        return false;
    }
    virtual void usbSerialSend(const std::string& msg, bool newLine = true)
    {
    }
};

/**
 * @brief Get the HAL instance
 *
 * @return HalBase&
 */
HalBase* Get();

/**
 * @brief Inject the HAL, which will call init() to initialize the HAL
 *
 * @param hal
 */
void Inject(std::unique_ptr<HalBase> hal);

/**
 * @brief Destroy the HAL instance
 *
 */
void Destroy();

/**
 * @brief Check if the HAL instance exists
 *
 * @return true
 * @return false
 */
bool Check();

}  // namespace hal

/**
 * @brief Get the HAL instance
 *
 * @return hal::HalBase&
 */
inline hal::HalBase* GetHAL()
{
    return hal::Get();
}

/**
 * @brief
 *
 */
class LvglLockGuard {
public:
    LvglLockGuard()
    {
        GetHAL()->lvglLock();
    }
    ~LvglLockGuard()
    {
        GetHAL()->lvglUnlock();
    }
};
