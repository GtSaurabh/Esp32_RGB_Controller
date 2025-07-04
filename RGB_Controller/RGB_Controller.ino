#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "AudioTools.h"
#include "BluetoothA2DPSink.h"


// OLED Display
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Buttons
#define BTN_LEFT   32
#define BTN_RIGHT  33
#define BTN_SAVE   27

//  Main Menu logic
enum MenuState { MENU_AUDIO_MODE, MENU_RGB_CONFIG, MENU_NUM_STRIPS, MENU_NUM_LEDS };
MenuState currentMenu = MENU_AUDIO_MODE;
int audioMode = 0; // 0: FFT, 1: Beat Detection
int numStrips = 1;
int ledsPerStrip = 30;

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

// Define I2S pins
#define I2S_BCK  25
#define I2S_WS   22
#define I2S_DOUT 26

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


// Splash screen
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

// Menu drawing
void drawMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  switch (currentMenu) {
    case MENU_AUDIO_MODE:
      display.setCursor(0, 0);
      display.println("Audio Mode");
      display.setCursor(10, 20);
      display.println(audioMode == 0 ? "> FFT" : "> Beat Detection");
      break;

    case MENU_RGB_CONFIG:
      display.setCursor(0, 0);
      display.println("RGB Config");
      display.setCursor(10, 20);
      display.println("> Press SAVE");
      break;

    case MENU_NUM_STRIPS:
      display.setCursor(0, 0);
      display.println("Set Num Strips");
      display.setCursor(10, 20);
      display.print("> ");
      display.print(numStrips);
      break;

    case MENU_NUM_LEDS:
      display.setCursor(0, 0);
      display.println("LEDs per Strip");
      display.setCursor(10, 20);
      display.print("> ");
      display.print(ledsPerStrip);
      break;
  }

  display.display();
}

void setup() {
  Serial.begin(115200);

  // Init Buttons
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_SAVE, INPUT_PULLUP);

  // Init OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
    Serial.println("OLED not found");
    while (true);
  }
  showSplash();

  // Setup I2S pins
  auto cfg = i2s.defaultConfig();
  cfg.pin_bck = I2S_BCK;
  cfg.pin_ws  = I2S_WS;
  cfg.pin_data = I2S_DOUT;
  i2s.begin(cfg);

  // Start Bluetooth A2DP
  a2dp_sink.start("ESP32_Music_RX");
}

void loop() {
  // Show splash for 2 seconds
  if (!splashDone) {
    if (millis() - splashStartTime > 2000) {
      splashDone = true;
      drawMenu();
    }
    return;
  }

  // Button logic
  if (readButton(BTN_LEFT)) {
    switch (currentMenu) {
      case MENU_AUDIO_MODE: audioMode = !audioMode; break;
      case MENU_NUM_STRIPS: if (numStrips > 1) numStrips--; break;
      case MENU_NUM_LEDS:   if (ledsPerStrip > 1) ledsPerStrip--; break;
      default: break;
    }
    drawMenu();
  }

  if (readButton(BTN_RIGHT)) {
    switch (currentMenu) {
      case MENU_AUDIO_MODE: audioMode = !audioMode; break;
      case MENU_NUM_STRIPS: if (numStrips < 10) numStrips++; break;
      case MENU_NUM_LEDS:   if (ledsPerStrip < 300) ledsPerStrip++; break;
      default: break;
    }
    drawMenu();
  }

  if (readButton(BTN_SAVE)) {
    switch (currentMenu) {
      case MENU_AUDIO_MODE: currentMenu = MENU_RGB_CONFIG; break;
      case MENU_RGB_CONFIG: currentMenu = MENU_NUM_STRIPS; break;
      case MENU_NUM_STRIPS: currentMenu = MENU_NUM_LEDS; break;
      case MENU_NUM_LEDS:   currentMenu = MENU_AUDIO_MODE; break;
    }
    drawMenu();
  }
}
