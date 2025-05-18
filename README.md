# Olfactory Game for Autism - Technical Documentation

## English Version

### 1. Project Overview

This project describes an interactive device designed to help children with autism spectrum disorder (ASD) connect with the real world through an engaging olfactory (sense of smell) game. The device releases a scent, and the child is prompted to identify it by scanning a corresponding Near Field Communication (NFC) tag. Correct or incorrect identification triggers visual (light) and auditory (sound) feedback. A new round can be initiated by pressing a button.

The core of the system is an ESP32 microcontroller that manages various peripherals, including atomizers (via relays), an NFC reader, an addressable LED (NeoPixel), and an audio feedback module.

### 2. Hardware Components

* **Microcontroller:** ESP32
* **NFC Reader:** Adafruit PN532 (I2C mode)
* **Atomizers:** 6 atomizers, each controlled by a relay.
* **Visual Feedback:** Adafruit NeoPixel (1 LED used as an indicator).
* **Auditory Feedback:** Audio module with at least two trigger inputs (for correct/incorrect sounds).
* **User Input:** One push button.
* **Relays:** To control the activation of atomizers.

### 3. Software Logic & Operation

The device operates based on the C++ code provided, running on the ESP32.

#### 3.1. Initialization (`setup()`)

Upon startup, the system performs the following initializations:

1.  **Serial Communication:** Starts serial communication at 115200 baud for debugging and logging.
2.  **Pin Modes:**
    * Sets the `BUTTON_PIN` as an input.
    * Sets `RELAY_PINS` as outputs and ensures all relays are initially `OFF`.
    * Initializes the `NEOPIXEL_PIN` for the LED strip.
    * Sets `AUDIO_TRIGGER_A1_PIN` (error sound) and `AUDIO_TRIGGER_A2_PIN` (correct sound) as outputs, defaulting to `HIGH` (inactive state, assuming low trigger).
3.  **NeoPixel LED:**
    * Initializes the NeoPixel strip.
    * Sets brightness (e.g., to 50 out of 255).
    * Defines colors for `colorSuccess` (greenish), `colorWarning` (reddish), and `colorOff` (black).
4.  **NFC Reader (PN532):**
    * Initializes I2C communication (`Wire.begin()`).
    * Initializes the PN532 NFC reader.
    * Checks the firmware version to ensure the reader is connected and functional. If not found, the system halts.
    * Configures the PN532 for SAM (Secure Access Module).
5.  **System Data:**
    * Calls `initializeSystemData()` to populate the `atomizerConfigs` array. Each element in this array is a structure holding the `expectedNfcUid` (Unique Identifier) for the NFC tag corresponding to a specific atomizer/scent.
    * Example UIDs:
        * Atomizer 1: `"1D 4C FD CC 11 10 80"`
        * Atomizer 2: `"1D 4D FD CC 11 10 80"`
        * ...and so on for 6 atomizers.
6.  **Random Seed:** Initializes the random number generator using an analog pin (`ANALOG_RANDOM_PIN`) for better randomness, or `millis()` as a fallback.
7.  **Initial State:** Calls `turnOffAllAssociatedEffects()` to ensure the LED is off and audio triggers are reset.

#### 3.2. Main Loop (`loop()`)

The `loop()` function continuously executes the following:

1.  **`handleButton()`:** Checks for button presses (short or long) to initiate a new game round.
2.  **`checkAtomizerRelayTimeout()`:** Monitors if the active atomizer's designated run time has expired and turns it off if so.
3.  **`handleNfcInteraction()`:** If the system is `awaitingNfcScan` (i.e., an atomizer has released a scent and is waiting for a tag), this function attempts to read an NFC tag.

#### 3.3. Button Handling (`handleButton()`)

This function implements button debouncing and detects short vs. long presses.

1.  Reads the raw state of the `BUTTON_PIN`.
2.  Debounces the input to prevent multiple triggers from a single press.
3.  **On Press:** Records the `buttonPressStartTime`.
4.  **On Release:**
    * Calculates `pressDuration`.
    * Determines the atomizer activation duration:
        * `SHORT_PRESS_DURATION_MS` (e.g., 5 seconds) if `pressDuration` is less than `LONG_PRESS_THRESHOLD_MS` (e.g., 500 ms).
        * `LONG_PRESS_DURATION_MS` (e.g., 20 minutes, likely for a different mode or testing, might be a typo and intended to be shorter for game play) if `pressDuration` is greater than or equal to `LONG_PRESS_THRESHOLD_MS`. *Note: The code uses the same logic for short and long press to start a new round, only the atomizer duration differs. The serial output indicates "long press triggers new round" or "short press triggers new round".*
    * Selects a `randomRelay` (0 to `NUM_RELAYS - 1`).
    * Calls `activateNewAtomizerCycle()` with the chosen relay index and duration.

#### 3.4. Atomizer Activation (`activateNewAtomizerCycle(int newAtomizerIndex, unsigned long duration)`)

This function starts a new game round:

1.  Prints a "--- Start New Round ---" message to serial.
2.  Calls `turnOffAllAssociatedEffects()` to reset LED and audio.
3.  If a previous atomizer was active, it turns off its relay.
4.  Sets `nfcContextAtomizerIndex` to the `newAtomizerIndex`. This variable stores which atomizer (and thus which scent/NFC tag) is currently active for the NFC interaction.
5.  Activates the relay corresponding to `newAtomizerIndex` by setting its pin `RELAY_ON` (`HIGH`).
6.  Sets `atomizerRelayIsOn = true`.
7.  Calculates `atomizerStopTime = millis() + duration`.
8.  Sets `awaitingNfcScan = true` to enable NFC tag reading in the main loop.

#### 3.5. Atomizer Timeout (`checkAtomizerRelayTimeout()`)

1.  Checks if `atomizerRelayIsOn` is true and if the current time (`millis()`) has passed `atomizerStopTime`.
2.  If the time is up:
    * Turns off the relay for the `nfcContextAtomizerIndex`.
    * Sets `atomizerRelayIsOn = false`.
    * Resets `atomizerStopTime`.
    * Prints a message that the atomizer is off.
    * **Important:** `awaitingNfcScan` remains `true` even if the atomizer stops, meaning the child can still scan the tag after the scent emission period has ended (until a new round starts or a correct tag is scanned).

#### 3.6. NFC Interaction (`handleNfcInteraction()`)

This function is called when `awaitingNfcScan` is true.

1.  Ensures audio trigger pins are `HIGH` (inactive) before attempting a read.
2.  Attempts to read a passive NFC tag (ISO14443A type) using `nfc.readPassiveTargetID()` with a short timeout (`NFC_READ_TIMEOUT_MS`, e.g., 50ms).
3.  **If a tag is successfully read (`success_pn532` is true and `uidLength > 0`):**
    * Formats the scanned UID into a hexadecimal string (`scannedUid`).
    * Verifies that `nfcContextAtomizerIndex` is valid. If not, it stops scanning.
    * Compares `scannedUid` with `atomizerConfigs[nfcContextAtomizerIndex].expectedNfcUid`.
    * **If UIDs match (Correct Tag):**
        * Prints "NFC tag correct!"
        * Sets NeoPixel to `colorSuccess`.
        * Triggers the "correct" sound: sets `AUDIO_TRIGGER_A2_PIN` `LOW` for `AUDIO_PULSE_DURATION_MS`, then back to `HIGH`.
        * Sets `awaitingNfcScan = false` to stop NFC scanning for this round.
        * The system now waits for the next button press to start a new round.
    * **If UIDs do not match (Incorrect Tag):**
        * Prints "NFC tag incorrect!"
        * Sets NeoPixel to `colorWarning`.
        * Triggers the "error" sound: sets `AUDIO_TRIGGER_A1_PIN` `LOW` for `AUDIO_PULSE_DURATION_MS`, then back to `HIGH`.
        * Waits for 1 second (`delay(1000)`).
        * `awaitingNfcScan` remains `true`, so the system will try to read an NFC tag again in the next loop iteration.
4.  **If no tag is read within the timeout:** `awaitingNfcScan` remains `true`, and the function will be called again in the next loop iteration to continue trying to read a tag.

#### 3.7. Feedback System

* **Visual:** A single NeoPixel LED indicates:
    * Greenish (`colorSuccess`): Correct NFC tag.
    * Reddish (`colorWarning`): Incorrect NFC tag.
    * Off (`colorOff`): Idle state or between actions.
* **Auditory:** An external audio module is triggered:
    * Via `AUDIO_TRIGGER_A2_PIN`: Plays a "correct" sound.
    * Via `AUDIO_TRIGGER_A1_PIN`: Plays an "error" sound.
    The triggers are active-low pulses.

#### 3.8. Helper Functions

* **`initializeSystemData()`:** Pre-populates the `atomizerConfigs` array with the UIDs of the NFC tags associated with each atomizer.
* **`turnOffAllAssociatedEffects()`:** Turns off the NeoPixel LED and resets audio trigger pins to their default (inactive) state.
* **`pinUsefulForAnalogRead(int pin)`:** (ESP32 specific) Checks if a given pin is suitable for `analogRead()`, used for seeding the random number generator.

### 4. Pinout Configuration (ESP32)

* **Button:** `GPIO13` (D13)
* **Relay 1:** `GPIO14` (D14)
* **Relay 2:** `GPIO26` (D26)
* **Relay 3:** `GPIO25` (D25)
* **Relay 4:** `GPIO33` (D33)
* **Relay 5:** `GPIO32` (D32)
* **Relay 6:** `GPIO19` (D19)
* **NFC IRQ:** `GPIO35` (D35, input-only)
* **NFC Reset:** `GPIO27` (D27)
* **NFC SDA (I2C):** `GPIO21` (D21, default)
* **NFC SCL (I2C):** `GPIO22` (D22, default)
* **NeoPixel Data:** `GPIO12` (D12)
* **Audio Trigger (Error Sound A1):** `GPIO18`
* **Audio Trigger (Correct Sound A2):** `GPIO23`
* **Analog Random Seed Pin:** `GPIO36` (SVP/A0)

---

## 中文版本 (Chinese Version)

### 1. 项目概述

本项目描述了一种交互式设备，旨在通过一个引人入胜的嗅觉游戏，帮助自闭症谱系障碍（ASD）儿童与现实世界建立连接。该设备会释放一种气味，然后提示儿童通过扫描相应的近场通信（NFC）标签来识别它。正确或错误的识别会触发视觉（灯光）和听觉（声音）反馈。按下按钮可以开始新一轮游戏。

系统的核心是一个ESP32微控制器，它管理各种外围设备，包括雾化器（通过继电器控制）、NFC读取器、可寻址LED（NeoPixel）和音频反馈模块。

### 2. 硬件组件

* **微控制器:** ESP32
* **NFC读取器:** Adafruit PN532 (I2C模式)
* **雾化器:** 6个雾化器，每个由一个继电器控制。
* **视觉反馈:** Adafruit NeoPixel (使用1个LED作为指示灯)。
* **听觉反馈:** 具有至少两个触发输入（用于正确/错误声音）的音频模块。
* **用户输入:** 一个按钮。
* **继电器:** 用于控制雾化器的激活。

### 3. 软件逻辑与操作

设备基于在ESP32上运行的C++代码进行操作。

#### 3.1. 初始化 (`setup()`)

启动时，系统执行以下初始化操作：

1.  **串口通信:** 以115200波特率启动串口通信，用于调试和日志记录。
2.  **引脚模式:**
    * 将 `BUTTON_PIN` 设置为输入模式。
    * 将 `RELAY_PINS` 设置为输出模式，并确保所有继电器初始状态为 `OFF` (关闭)。
    * 初始化用于LED灯带的 `NEOPIXEL_PIN`。
    * 将 `AUDIO_TRIGGER_A1_PIN` (错误提示音) 和 `AUDIO_TRIGGER_A2_PIN` (正确提示音) 设置为输出模式，默认为 `HIGH` (高电平，假设为低电平触发)。
3.  **NeoPixel LED:**
    * 初始化NeoPixel灯带。
    * 设置亮度 (例如，50，范围0-255)。
    * 定义 `colorSuccess` (成功，绿色系)、`colorWarning` (警告，红色系) 和 `colorOff` (关闭，黑色) 的颜色。
4.  **NFC读取器 (PN532):**
    * 初始化I2C通信 (`Wire.begin()`)。
    * 初始化PN532 NFC读取器。
    * 检查固件版本以确保读取器已连接且功能正常。如果未找到，系统将停止。
    * 配置PN532的SAM (安全访问模块)。
5.  **系统数据:**
    * 调用 `initializeSystemData()` 来填充 `atomizerConfigs` 数组。此数组中的每个元素是一个结构体，包含与特定雾化器/气味对应的NFC标签的 `expectedNfcUid` (唯一标识符)。
    * UID示例:
        * 雾化器 1: `"1D 4C FD CC 11 10 80"`
        * 雾化器 2: `"1D 4D FD CC 11 10 80"`
        * ...依此类推，共6个雾化器。
6.  **随机数种子:** 使用模拟引脚 (`ANALOG_RANDOM_PIN`) 初始化随机数生成器以获得更好的随机性，或使用 `millis()` 作为备用方案。
7.  **初始状态:** 调用 `turnOffAllAssociatedEffects()` 以确保LED熄灭且音频触发器复位。

#### 3.2. 主循环 (`loop()`)

`loop()` 函数持续执行以下操作：

1.  **`handleButton()`:** 检查按钮按下（短按或长按）以启动新一轮游戏。
2.  **`checkAtomizerRelayTimeout()`:** 监控当前活动的雾化器的指定运行时间是否已到期，如果是则将其关闭。
3.  **`handleNfcInteraction()`:** 如果系统处于 `awaitingNfcScan` 状态（即雾化器已释放气味并等待扫描标签），此函数将尝试读取NFC标签。

#### 3.3. 按钮处理 (`handleButton()`)

此函数实现按钮防抖并检测短按与长按。

1.  读取 `BUTTON_PIN` 的原始状态。
2.  对输入进行防抖处理，以防止单次按下触发多次。
3.  **按下时:** 记录 `buttonPressStartTime` (按钮按下开始时间)。
4.  **释放时:**
    * 计算 `pressDuration` (按键持续时间)。
    * 确定雾化器激活持续时间：
        * 如果 `pressDuration` 小于 `LONG_PRESS_THRESHOLD_MS` (例如500毫秒)，则为 `SHORT_PRESS_DURATION_MS` (例如5秒)。
        * 如果 `pressDuration` 大于或等于 `LONG_PRESS_THRESHOLD_MS`，则为 `LONG_PRESS_DURATION_MS` (例如20分钟，这可能用于不同模式或测试，对于游戏来说可能过长，或许是笔误，本意应更短)。*注意：代码中短按和长按都用于开始新一轮，仅雾化器持续时间不同。串口输出会提示“长按触发新一轮”或“短按触发新一轮”。*
    * 选择一个 `randomRelay` (随机继电器，范围0到 `NUM_RELAYS - 1`)。
    * 使用选择的继电器索引和持续时间调用 `activateNewAtomizerCycle()`。

#### 3.4. 雾化器激活 (`activateNewAtomizerCycle(int newAtomizerIndex, unsigned long duration)`)

此函数开始新一轮游戏：

1.  向串口打印 "--- 开始新一轮操作 ---" 消息。
2.  调用 `turnOffAllAssociatedEffects()` 来重置LED和音频。
3.  如果前一个雾化器处于活动状态，则关闭其继电器。
4.  将 `nfcContextAtomizerIndex` 设置为 `newAtomizerIndex`。此变量存储当前NFC交互中活动的雾化器（以及对应的气味/NFC标签）。
5.  通过将其引脚设置为 `RELAY_ON` (`HIGH`) 来激活与 `newAtomizerIndex` 对应的继电器。
6.  设置 `atomizerRelayIsOn = true`。
7.  计算 `atomizerStopTime = millis() + duration` (雾化器停止时间)。
8.  设置 `awaitingNfcScan = true` 以在主循环中启用NFC标签读取。

#### 3.5. 雾化器超时 (`checkAtomizerRelayTimeout()`)

1.  检查 `atomizerRelayIsOn` 是否为 `true` 以及当前时间 (`millis()`) 是否已超过 `atomizerStopTime`。
2.  如果时间到：
    * 关闭 `nfcContextAtomizerIndex` 对应雾化器的继电器。
    * 设置 `atomizerRelayIsOn = false`。
    * 重置 `atomizerStopTime`。
    * 打印一条消息，指示雾化器已关闭。
    * **重要提示:** 即使雾化器停止，`awaitingNfcScan` 仍保持 `true`，这意味着儿童在气味释放期结束后仍然可以扫描标签（直到开始新一轮或扫描到正确的标签）。

#### 3.6. NFC交互 (`handleNfcInteraction()`)

当 `awaitingNfcScan` 为 `true` 时调用此函数。

1.  在尝试读取之前，确保音频触发引脚为 `HIGH` (高电平，非活动状态)。
2.  尝试使用 `nfc.readPassiveTargetID()` 读取无源NFC标签（ISO14443A类型），并设置一个较短的超时时间 (`NFC_READ_TIMEOUT_MS`，例如50毫秒）。
3.  **如果成功读取到标签 (`success_pn532` 为 `true` 且 `uidLength > 0`):**
    * 将扫描到的UID格式化为十六进制字符串 (`scannedUid`)。
    * 验证 `nfcContextAtomizerIndex` 是否有效。如果无效，则停止扫描。
    * 将 `scannedUid` 与 `atomizerConfigs[nfcContextAtomizerIndex].expectedNfcUid` 进行比较。
    * **如果UID匹配 (标签正确):**
        * 打印 "NFC标签正确!"
        * 将NeoPixel设置为 `colorSuccess`。
        * 触发“正确”提示音：将 `AUDIO_TRIGGER_A2_PIN` 置为 `LOW` (低电平) 持续 `AUDIO_PULSE_DURATION_MS`，然后恢复为 `HIGH`。
        * 设置 `awaitingNfcScan = false` 以停止本轮的NFC扫描。
        * 系统现在等待下一次按钮按下以开始新一轮。
    * **如果UID不匹配 (标签错误):**
        * 打印 "NFC标签错误!"
        * 将NeoPixel设置为 `colorWarning`。
        * 触发“错误”提示音：将 `AUDIO_TRIGGER_A1_PIN` 置为 `LOW` 持续 `AUDIO_PULSE_DURATION_MS`，然后恢复为 `HIGH`。
        * 等待1秒 (`delay(1000)`)。
        * `awaitingNfcScan` 保持 `true`，因此系统将在下一个循环迭代中再次尝试读取NFC标签。
4.  **如果在超时时间内未读取到标签:** `awaitingNfcScan` 保持 `true`，该函数将在下一个循环迭代中再次被调用，以继续尝试读取标签。

#### 3.7. 反馈系统

* **视觉:** 单个NeoPixel LED指示：
    * 绿色系 (`colorSuccess`): NFC标签正确。
    * 红色系 (`colorWarning`): NFC标签错误。
    * 熄灭 (`colorOff`): 空闲状态或操作之间。
* **听觉:** 外部音频模块被触发：
    * 通过 `AUDIO_TRIGGER_A2_PIN`: 播放“正确”提示音。
    * 通过 `AUDIO_TRIGGER_A1_PIN`: 播放“错误”提示音。
    触发方式为低电平有效脉冲。

#### 3.8. 辅助函数

* **`initializeSystemData()`:** 使用与每个雾化器关联的NFC标签的UID预填充 `atomizerConfigs` 数组。
* **`turnOffAllAssociatedEffects()`:** 关闭NeoPixel LED并将音频触发引脚复位到其默认（非活动）状态。
* **`pinUsefulForAnalogRead(int pin)`:** (ESP32特有) 检查给定引脚是否适用于 `analogRead()`，用于为随机数生成器提供种子。

### 4. 引脚配置 (ESP32)

* **按钮:** `GPIO13` (D13)
* **继电器 1:** `GPIO14` (D14)
* **继电器 2:** `GPIO26` (D26)
* **继电器 3:** `GPIO25` (D25)
* **继电器 4:** `GPIO33` (D33)
* **继电器 5:** `GPIO32` (D32)
* **继电器 6:** `GPIO19` (D19)
* **NFC IRQ (中断请求):** `GPIO35` (D35, 仅输入)
* **NFC Reset (复位):** `GPIO27` (D27)
* **NFC SDA (I2C数据):** `GPIO21` (D21, 默认)
* **NFC SCL (I2C时钟):** `GPIO22` (D22, 默认)
* **NeoPixel 数据:** `GPIO12` (D12)
* **音频触发 (错误提示音 A1):** `GPIO18`
* **音频触发 (正确提示音 A2):** `GPIO23`
* **模拟随机数种子引脚:** `GPIO36` (SVP/A0)

