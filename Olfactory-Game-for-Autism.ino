// 包含NFC库 (PN532)
#include <Wire.h>
#include <Adafruit_PN532.h>

// 包含其他库
#include <Adafruit_NeoPixel.h>

// --- ESP32 特定引脚定义 (请根据您的实际接线修改!) ---
const int BUTTON_PIN = 13;          // 连接到开发板的 D13 (通常是 GPIO13)

const int NUM_RELAYS = 6;
// 为继电器选择 ESP32 GPIO 引脚
// (D14 -> GPIO14, D26 -> GPIO26, D25 -> GPIO25, D33 -> GPIO33, D32 -> GPIO32, D19 -> GPIO19)
const int RELAY_PINS[NUM_RELAYS] = {14, 26, 25, 33, 32, 19};

// PN532 NFC 模块引脚定义 (I2C 模式)
// ESP32 默认 I2C 引脚: SDA = GPIO21 (通常标为 D21), SCL = GPIO22 (通常标为 D22)
const int PN532_IRQ_PIN   = 35;    // 连接到开发板的 D35 (通常是 GPIO35, 输入型引脚)
const int PN532_RESET_PIN = 27;    // 连接到开发板的 D27 (通常是 GPIO27)
Adafruit_PN532 nfc(PN532_IRQ_PIN, PN532_RESET_PIN);

// 可寻址LED (NeoPixel) 定义
const int NEOPIXEL_PIN = 12;       // 连接到开发板的 D12 (通常是 GPIO12)
const int NUM_LEDS = 1;            // 灯带上的LED数量 (此处假设用1个作为指示)
Adafruit_NeoPixel strip(NUM_LEDS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// 音频模块触发引脚定义
const int AUDIO_TRIGGER_A1_PIN = 18; // 连接到音频模块A1端口的GPIO (例如 GPIO18) - 错误提示
const int AUDIO_TRIGGER_A2_PIN = 23; // 连接到音频模块A2端口的GPIO (例如 GPIO23) - 正确提示

// 用于随机数种子的模拟引脚
const int ANALOG_RANDOM_PIN = 36; // 通常是 GPIO36 (板子上可能标为 SVP 或 A0)


// --- 时间定义 ---
const unsigned long SHORT_PRESS_DURATION_MS = 5000;
const unsigned long LONG_PRESS_DURATION_MS = 20UL * 60UL * 1000UL;
const unsigned long LONG_PRESS_THRESHOLD_MS = 500;
const unsigned long DEBOUNCE_DELAY_MS = 50;
const unsigned long AUDIO_PULSE_DURATION_MS = 100; // 音频触发脉冲持续时间 (毫秒)
const unsigned int NFC_READ_TIMEOUT_MS = 50;       // NFC读卡超时时间 (毫秒)

// --- 继电器状态 ---
const int RELAY_ON = HIGH;
const int RELAY_OFF = LOW;

// --- 按键模块逻辑电平定义 ---
// 假设按钮模块按下时OUT为HIGH。如果按下为LOW，请修改此定义。
const int BUTTON_STATE_PRESSED = HIGH;
const int BUTTON_STATE_RELEASED = LOW;

// --- NeoPixel 颜色定义 ---
uint32_t colorSuccess;
uint32_t colorWarning;
uint32_t colorOff;

// --- 存储每个雾化器特定信息的结构体 ---
struct AtomizerConfig {
  String expectedNfcUid;
};
AtomizerConfig atomizerConfigs[NUM_RELAYS];

// --- 按键状态变量 ---
int debouncedButtonState = BUTTON_STATE_RELEASED;
int lastRawButtonState = BUTTON_STATE_RELEASED;
unsigned long lastDebounceTime = 0;
unsigned long buttonPressStartTime = 0;
bool actionExpectedOnRelease = false;

// --- 雾化器及NFC交互状态变量 ---
int nfcContextAtomizerIndex = -1;
bool atomizerRelayIsOn = false;
unsigned long atomizerStopTime = 0;
bool awaitingNfcScan = false; // 控制是否应该进行NFC扫描

void initializeSystemData() {
  atomizerConfigs[0] = {"1D 4C FD CC 11 10 80"};
  atomizerConfigs[1] = {"1D 4D FD CC 11 10 80"};
  atomizerConfigs[2] = {"1D 4E FD CC 11 10 80"};
  atomizerConfigs[3] = {"1D 4F FD CC 11 10 80"};
  atomizerConfigs[4] = {"1D 50 FD CC 11 10 80"};
  atomizerConfigs[5] = {"1D 51 FD CC 11 10 80"};
}

void turnOffAllAssociatedEffects() {
  // 关闭NeoPixel灯
  strip.fill(colorOff, 0, NUM_LEDS);
  strip.show();
  // 确保音频触发引脚恢复高电平
  digitalWrite(AUDIO_TRIGGER_A1_PIN, HIGH);
  digitalWrite(AUDIO_TRIGGER_A2_PIN, HIGH);
  Serial.println("所有灯光和音频触发已关闭/复位。");
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10); // 等待串口连接
  }

  pinMode(BUTTON_PIN, INPUT); // 按钮模块直接输出高低电平

  for (int i = 0; i < NUM_RELAYS; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    digitalWrite(RELAY_PINS[i], RELAY_OFF);
  }

  // 初始化NeoPixel灯带
  strip.begin();
  strip.show(); // 确保所有像素初始为灭
  strip.setBrightness(50); // 设置亮度 (0-255)

  // 定义颜色
  colorSuccess = strip.Color(74, 255, 147); // 识别正确颜色 RGB(74, 255, 147)
  colorWarning = strip.Color(237, 96, 81);  // 识别错误颜色 RGB(237, 96, 81)
  colorOff     = strip.Color(0, 0, 0);      // 黑色 (关闭)

  // 初始化音频触发引脚
  pinMode(AUDIO_TRIGGER_A1_PIN, OUTPUT);
  pinMode(AUDIO_TRIGGER_A2_PIN, OUTPUT);
  digitalWrite(AUDIO_TRIGGER_A1_PIN, HIGH); // 默认高电平
  digitalWrite(AUDIO_TRIGGER_A2_PIN, HIGH); // 默认高电平

  initializeSystemData();

  Serial.println("初始化 PN532 NFC 读卡器 (I2C)...");
  Wire.begin(); // 启动I2C，使用默认SDA(GPIO21), SCL(GPIO22)
  nfc.begin();
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.print("未找到 PN532 板。请检查接线和I2C地址！");
    while (1) delay(10); // 阻塞
  }
  Serial.print("找到 PN532! Firmware ver. ");
  Serial.print((versiondata >> 24) & 0xFF, HEX); Serial.print(".");
  Serial.print((versiondata >> 16) & 0xFF, HEX); Serial.print(".");
  Serial.println((versiondata >> 8) & 0xFF, HEX);
  nfc.SAMConfig();
  Serial.println("PN532 配置完成，等待卡片...");

  // 使用 ESP32 的模拟引脚进行随机数种子初始化
  if (pinUsefulForAnalogRead(ANALOG_RANDOM_PIN)) {
      randomSeed(analogRead(ANALOG_RANDOM_PIN));
  } else {
      randomSeed(millis()); // 备用方案
      Serial.println("警告: 指定的模拟引脚不适用于 analogRead，使用 millis() 作为随机种子。");
  }

  Serial.println("系统初始化完成。");
  turnOffAllAssociatedEffects(); // 确保启动时所有效果关闭
}

// 辅助函数检查引脚是否可用于 analogRead (ESP32特有)
bool pinUsefulForAnalogRead(int pin) {
    #if defined(ESP32)
        if (pin >= 32 && pin <= 39) { // ADC1 pins
            return true;
        }
        return false;
    #else // 其他 Arduino 板
        return true;
    #endif
}

void loop() {
  handleButton();
  checkAtomizerRelayTimeout();

  if (awaitingNfcScan && nfcContextAtomizerIndex != -1) {
    handleNfcInteraction();
  }
  // yield(); // 在某些复杂的ESP32项目中，有助于保持系统响应
}

void handleButton() {
  int rawButtonReading = digitalRead(BUTTON_PIN);
  if (rawButtonReading != lastRawButtonState) {
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY_MS) {
    if (rawButtonReading != debouncedButtonState) {
      debouncedButtonState = rawButtonReading;
      if (debouncedButtonState == BUTTON_STATE_PRESSED) {
        if (!actionExpectedOnRelease) {
          buttonPressStartTime = millis();
          actionExpectedOnRelease = true;
          Serial.println("按键按下 (等待释放判断长短按)");
        }
      } else if (debouncedButtonState == BUTTON_STATE_RELEASED) {
        if (actionExpectedOnRelease) {
          unsigned long pressDuration = millis() - buttonPressStartTime;
          Serial.print("按键已释放，持续时间: ");
          Serial.print(pressDuration);
          Serial.println(" ms");

          int randomRelay = random(0, NUM_RELAYS);
          unsigned long atomizerDuration = (pressDuration >= LONG_PRESS_THRESHOLD_MS) ? LONG_PRESS_DURATION_MS : SHORT_PRESS_DURATION_MS;
          
          if (pressDuration >= LONG_PRESS_THRESHOLD_MS) {
              Serial.print("长按触发新一轮。");
          } else {
              Serial.print("短按触发新一轮。");
          }
          activateNewAtomizerCycle(randomRelay, atomizerDuration);
          actionExpectedOnRelease = false;
        }
      }
    }
  }
  lastRawButtonState = rawButtonReading;
}

void activateNewAtomizerCycle(int newAtomizerIndex, unsigned long duration) {
  Serial.println("--- 开始新一轮操作 ---");
  turnOffAllAssociatedEffects(); // 关闭所有灯光并将音频触发引脚复位

  if (atomizerRelayIsOn && nfcContextAtomizerIndex != -1 && nfcContextAtomizerIndex < NUM_RELAYS) {
    digitalWrite(RELAY_PINS[nfcContextAtomizerIndex], RELAY_OFF);
    Serial.print("关闭上一个活动的继电器 (上下文 "); Serial.print(nfcContextAtomizerIndex + 1); Serial.println(")。");
  }
  
  nfcContextAtomizerIndex = newAtomizerIndex;

  Serial.print("激活雾化器 ");
  Serial.print(nfcContextAtomizerIndex + 1);
  Serial.print(" (引脚 ");
  Serial.print(RELAY_PINS[nfcContextAtomizerIndex]);
  Serial.print(")，持续 ");
  Serial.print(duration / 1000);
  Serial.println(" 秒。");

  digitalWrite(RELAY_PINS[nfcContextAtomizerIndex], RELAY_ON);
  atomizerRelayIsOn = true;
  atomizerStopTime = millis() + duration;

  awaitingNfcScan = true; // 激活NFC扫描
  Serial.println("NFC标签检测已激活，等待扫描...");
}

void checkAtomizerRelayTimeout() {
  if (atomizerRelayIsOn && nfcContextAtomizerIndex != -1 && nfcContextAtomizerIndex < NUM_RELAYS) {
    if (millis() >= atomizerStopTime) {
      digitalWrite(RELAY_PINS[nfcContextAtomizerIndex], RELAY_OFF);
      atomizerRelayIsOn = false;
      atomizerStopTime = 0;
      Serial.print("雾化器 ");
      Serial.print(nfcContextAtomizerIndex + 1);
      Serial.println(" 的继电器工作时间到，已关闭。NFC检测将继续等待有效扫描。");
      // 注意：根据需求，即使继电器关闭，NFC扫描依然等待
    }
  }
}

void handleNfcInteraction() {
  uint8_t success_pn532;
  uint8_t uid[] = {0, 0, 0, 0, 0, 0, 0}; // 7位UID缓冲区
  uint8_t uidLength;

  // 确保音频触发引脚在检测前或重试前为高电平
  digitalWrite(AUDIO_TRIGGER_A1_PIN, HIGH);
  digitalWrite(AUDIO_TRIGGER_A2_PIN, HIGH);

  // 尝试读取NFC标签，使用定义的超时时间
  success_pn532 = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, NFC_READ_TIMEOUT_MS);

  if (success_pn532 && uidLength > 0) { // 如果成功读取到卡片
    String scannedUid = "";
    for (uint8_t i = 0; i < uidLength; i++) {
      if (uid[i] < 0x10) {
        scannedUid += "0";
      }
      scannedUid += String(uid[i], HEX);
      if (i < uidLength - 1) {
        scannedUid += " ";
      }
    }
    scannedUid.toUpperCase();

    Serial.print("NFC标签已扫描，UID: "); Serial.println(scannedUid);
    
    // 检查NFC上下文是否有效
    if (nfcContextAtomizerIndex < 0 || nfcContextAtomizerIndex >= NUM_RELAYS) {
        Serial.println("错误：NFC上下文无效，停止扫描。");
        awaitingNfcScan = false; // 停止扫描，因为上下文错误
        return;
    }

    Serial.print("当前NFC上下文对应雾化器 ("); Serial.print(nfcContextAtomizerIndex + 1);
    Serial.print("), 需要的UID: "); Serial.println(atomizerConfigs[nfcContextAtomizerIndex].expectedNfcUid);

    if (scannedUid.equals(atomizerConfigs[nfcContextAtomizerIndex].expectedNfcUid)) {
      // --- 标签正确 ---
      Serial.println("NFC标签正确!");
      strip.fill(colorSuccess, 0, NUM_LEDS);
      strip.show();
      
      // 触发音频模块A2端口 (正确提示音)
      Serial.println("触发音频模块 A2 (正确提示)");
      digitalWrite(AUDIO_TRIGGER_A2_PIN, LOW);
      delay(AUDIO_PULSE_DURATION_MS);
      digitalWrite(AUDIO_TRIGGER_A2_PIN, HIGH);

      awaitingNfcScan = false; // 正确标签找到，停止本轮NFC扫描
      Serial.println("NFC检测成功。等待下一轮按钮操作。");
    } else {
      // --- 标签错误 ---
      Serial.println("NFC标签错误!");
      strip.fill(colorWarning, 0, NUM_LEDS);
      strip.show();

      // 触发音频模块A1端口 (错误提示音)
      Serial.println("触发音频模块 A1 (错误提示)");
      digitalWrite(AUDIO_TRIGGER_A1_PIN, LOW);
      delay(AUDIO_PULSE_DURATION_MS);
      digitalWrite(AUDIO_TRIGGER_A1_PIN, HIGH);

      Serial.println("标签错误，1秒后将重试扫描...");
      delay(1000); // 等待1秒
      // awaitingNfcScan 保持 true, loop() 将再次调用此函数进行扫描
    }
  }
  // 如果 readPassiveTargetID 返回 false (在NFC_READ_TIMEOUT_MS内未读到卡),
  // awaitingNfcScan 仍然是 true, loop() 会再次调用此函数尝试读取。
}
