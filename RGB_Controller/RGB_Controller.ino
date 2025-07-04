#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "AudioTools.h"
#include "BluetoothA2DPSink.h"
#include "arduinoFFT.h"

// OLED Display
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Buttons
#define BTN_LEFT   32
#define BTN_RIGHT  33
#define BTN_SAVE   27

// Menu states
enum MenuState { MENU_AUDIO_MODE, MENU_RGB_CONFIG, MENU_NUM_STRIPS, MENU_NUM_LEDS };
MenuState currentMenu = MENU_AUDIO_MODE;
int audioMode = 0; // 0: FFT, 1: Beat Detection
int numStrips = 1;
int ledsPerStrip = 30;

// Splash screen
unsigned long splashStartTime;
bool splashDone = false;

// Music icon
const unsigned char music_logo [] PROGMEM = {
  0x00, 0x00, 0x0F, 0x80, 0x3F, 0xC0, 0x38, 0x40, 0x38, 0x60, 0x38, 0x60, 0x38, 0x60, 0x3F, 0xE0,
  0x3F, 0xE0, 0x38, 0x60, 0x38, 0x60, 0x38, 0x60, 0x38, 0x40, 0x3F, 0xC0, 0x0F, 0x80, 0x00, 0x00
};

// A2DP + I2S
I2SStream i2s;
BluetoothA2DPSink a2dp_sink(i2s);

// I2S Pins
#define I2S_BCK  25
#define I2S_WS   22
#define I2S_DOUT 26

// FFT Setup
#define SAMPLES 512
#define SAMPLING_FREQUENCY 44100
double vReal[SAMPLES];
double vImag[SAMPLES];
ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, SAMPLES, SAMPLING_FREQUENCY);

// LED pin (onboard)
#define ONBOARD_LED 2

// Audio data
volatile bool newAudioFrame = false;
uint8_t tempAudio[SAMPLES * 2]; // Buffer to hold raw audio

// Beat detection
bool isBeatDetected(double *magnitudes, int fromBin, int toBin) {
  double avg = 0;
  for (int i = fromBin; i <= toBin; i++) avg += magnitudes[i];
  avg /= (toBin - fromBin + 1);
  return avg > 5000; // Threshold can be tuned
}

// Debounce
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

// Audio callback
void read_audio_stream(const uint8_t *data, uint32_t length) {
  if (length >= SAMPLES * 2) length = SAMPLES * 2;
  memcpy(tempAudio, data, length);
  newAudioFrame = true;
}

void setup() {
  Serial.begin(115200);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_SAVE, INPUT_PULLUP);
  pinMode(ONBOARD_LED, OUTPUT);

  // OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
    Serial.println("OLED not found");
    while (true);
  }
  showSplash();

  // I2S
  auto cfg = i2s.defaultConfig();
  cfg.pin_bck = I2S_BCK;
  cfg.pin_ws  = I2S_WS;
  cfg.pin_data = I2S_DOUT;
  i2s.begin(cfg);

  a2dp_sink.set_stream_reader(read_audio_stream);
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

  if (newAudioFrame) {
    newAudioFrame = false;

    // Convert 16-bit PCM to double
    for (int i = 0; i < SAMPLES; i++) {
      int16_t sample = (tempAudio[2 * i + 1] << 8) | tempAudio[2 * i];
      vReal[i] = (double)sample;
      vImag[i] = 0.0;
    }

    FFT.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    FFT.compute(FFT_FORWARD);
    FFT.complexToMagnitude();

        if (audioMode == 0) {
      Serial.println("FFT Output:");
      double avg = 0;
      for (int i = 3; i <= 10; i++) {
        Serial.print("Bin ");
        Serial.print(i);
        Serial.print(": ");
        Serial.println(vReal[i]);
        avg += vReal[i];
      }
      avg /= 8;  // bins 3 to 10

      // Blink LED if average magnitude exceeds threshold
      if (avg > 2000) {
        digitalWrite(ONBOARD_LED, HIGH);
      } else {
        digitalWrite(ONBOARD_LED, LOW);
      }
    }else {
      bool beat = isBeatDetected(vReal, 3, 10);
      Serial.println(beat ? "Beat!" : "No Beat");
      digitalWrite(ONBOARD_LED, beat ? HIGH : LOW);
    }
  }

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
