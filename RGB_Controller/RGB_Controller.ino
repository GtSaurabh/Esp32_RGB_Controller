#include <Wire.h>
#include <EEPROM.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <AudioTools.h>
#include <BluetoothA2DPSink.h>

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Buttons
#define BTN_LEFT   32
#define BTN_RIGHT  33
#define BTN_SAVE   27

// Menu states
enum MenuState {
  MENU_AUDIO_MODE,
  MENU_NUM_STRIPS,
  MENU_LED_COUNT
};
MenuState currentMenu = MENU_AUDIO_MODE;

// Config
#define MAX_STRIPS 5
#define MAX_LEDS_PER_STRIP 300
uint8_t audioMode = 0; // 0 = FFT, 1 = Beat Detection
uint8_t numStrips = 1;
uint8_t ledsPerStrip[MAX_STRIPS] = {60, 60, 60, 60, 60};
uint8_t currentStrip = 0;

unsigned long splashStartTime;
bool splashDone = false;

// Music logo (16x16)
const unsigned char music_logo [] PROGMEM = {
  0x00, 0x00, 0x0F, 0x80, 0x3F, 0xC0, 0x38, 0x40, 0x38, 0x60, 0x38, 0x60, 0x38, 0x60, 0x3F, 0xE0,
  0x3F, 0xE0, 0x38, 0x60, 0x38, 0x60, 0x38, 0x60, 0x38, 0x40, 0x3F, 0xC0, 0x0F, 0x80, 0x00, 0x00
};

// A2DP + I2S
I2SStream i2s;
BluetoothA2DPSink a2dp_sink(i2s);
#define I2S_BCK  25
#define I2S_WS   22
#define I2S_DOUT 26

// EEPROM addresses
#define ADDR_AUDIO_MODE 0
#define ADDR_NUM_STRIPS 1
#define ADDR_LEDS_START 10

// Button debounce
bool readButton(uint8_t pin) {
  static uint32_t lastPressTime[3] = {0, 0, 0};
  static bool lastState[3] = {HIGH, HIGH, HIGH};

  int index = (pin == BTN_LEFT) ? 0 : (pin == BTN_RIGHT) ? 1 : 2;
  bool currentState = digitalRead(pin);

  if (currentState != lastState[index]) {
    lastState[index] = currentState;
    if (currentState == LOW && millis() - lastPressTime[index] > 250) {
      lastPressTime[index] = millis();
      return true;
    }
  }
  return false;
}

// EEPROM
void saveToEEPROM() {
  EEPROM.write(ADDR_AUDIO_MODE, audioMode);
  EEPROM.write(ADDR_NUM_STRIPS, numStrips);
  for (uint8_t i = 0; i < MAX_STRIPS; i++) {
    EEPROM.write(ADDR_LEDS_START + i, ledsPerStrip[i]);
  }
  EEPROM.commit();
}

void loadFromEEPROM() {
  audioMode = EEPROM.read(ADDR_AUDIO_MODE);
  numStrips = EEPROM.read(ADDR_NUM_STRIPS);
  if (numStrips < 1 || numStrips > MAX_STRIPS) numStrips = 1;
  for (uint8_t i = 0; i < MAX_STRIPS; i++) {
    ledsPerStrip[i] = EEPROM.read(ADDR_LEDS_START + i);
    if (ledsPerStrip[i] == 0 || ledsPerStrip[i] > MAX_LEDS_PER_STRIP) ledsPerStrip[i] = 60;
  }
}

// Splash
void showSplash() {
  display.clearDisplay();
  display.drawBitmap((SCREEN_WIDTH - 16) / 2, 10, music_logo, 16, 16, WHITE);
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(18, 35);
  display.println("Music RGB");
  display.display();
  splashStartTime = millis();
}

// Menu UI
void drawMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  switch (currentMenu) {
    case MENU_AUDIO_MODE:
      display.setCursor(0, 0);
      display.println("Audio Mode");
      display.setCursor(10, 20);
      display.println(audioMode == 0 ? "> FFT" : "> Beat Detect");
      break;

    case MENU_NUM_STRIPS:
      display.setCursor(0, 0);
      display.println("Number of Strips");
      display.setCursor(10, 20);
      display.print("> ");
      display.print(numStrips);
      break;

    case MENU_LED_COUNT:
      display.setCursor(0, 0);
      display.print("LEDs: Strip ");
      display.print(currentStrip + 1);
      display.setCursor(10, 20);
      display.print("> ");
      display.print(ledsPerStrip[currentStrip]);
      break;
  }

  display.display();
}

void setup() {
  Serial.begin(115200);

  // Init buttons
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_SAVE, INPUT_PULLUP);

  // Init EEPROM
  EEPROM.begin(64);
  loadFromEEPROM();

  // Init OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
    Serial.println("OLED not found");
    while (1);
  }
  showSplash();

  // Init I2S
  auto cfg = i2s.defaultConfig();
  cfg.pin_bck = I2S_BCK;
  cfg.pin_ws  = I2S_WS;
  cfg.pin_data = I2S_DOUT;
  i2s.begin(cfg);

  // A2DP start
  a2dp_sink.start("ESP32_Music_RX");
}

void loop() {
  if (!splashDone) {
    if (millis() - splashStartTime > 2000) {
      splashDone = true;
      drawMenu();
    }
    return;
  }

  if (readButton(BTN_LEFT)) {
    switch (currentMenu) {
      case MENU_AUDIO_MODE:
        audioMode = !audioMode;
        break;
      case MENU_NUM_STRIPS:
        if (numStrips > 1) numStrips--;
        break;
      case MENU_LED_COUNT:
        if (ledsPerStrip[currentStrip] > 10) ledsPerStrip[currentStrip] -= 10;
        break;
    }
    drawMenu();
  }

  if (readButton(BTN_RIGHT)) {
    switch (currentMenu) {
      case MENU_AUDIO_MODE:
        audioMode = !audioMode;
        break;
      case MENU_NUM_STRIPS:
        if (numStrips < MAX_STRIPS) numStrips++;
        break;
      case MENU_LED_COUNT:
        if (ledsPerStrip[currentStrip] < MAX_LEDS_PER_STRIP) ledsPerStrip[currentStrip] += 10;
        break;
    }
    drawMenu();
  }

  if (readButton(BTN_SAVE)) {
    switch (currentMenu) {
      case MENU_AUDIO_MODE:
        currentMenu = MENU_NUM_STRIPS;
        break;
      case MENU_NUM_STRIPS:
        currentStrip = 0;
        currentMenu = MENU_LED_COUNT;
        break;
      case MENU_LED_COUNT:
        currentStrip++;
        if (currentStrip >= numStrips) {
          saveToEEPROM();
          currentMenu = MENU_AUDIO_MODE;
        }
        break;
    }
    drawMenu();
  }
}
