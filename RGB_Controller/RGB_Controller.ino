#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <AudioTools.h>
#include <BluetoothA2DPSink.h>
#include <arduinoFFT.h>
#include <FastLED.h>
#include <EEPROM.h>

// ===== Constants =====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define MAX_LEDS 300
#define LED_PIN 14
#define BTN_LEFT 32
#define BTN_RIGHT 33
#define BTN_SAVE 27
#define I2S_BCK 25
#define I2S_WS 22
#define I2S_DOUT 26
#define SAMPLES 512
#define SAMPLING_FREQUENCY 44100
#define EEPROM_SIZE 64

// ===== Global =====
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
CRGB leds[MAX_LEDS];

enum MenuState {
  MENU_AUDIO_MODE,
  MENU_EFFECT_MODE,
  MENU_SINGLE_COLOR,
  MENU_BRIGHTNESS,
  MENU_NUM_STRIPS,
  MENU_NUM_LEDS
};
MenuState currentMenu = MENU_AUDIO_MODE;

int audioMode = 0; // 0: FFT, 1: Beat Detection
int effectMode = 0; // 0: Single Color, 1: Rainbow, 2: Grouped Spectrum
int selectedColorIndex = 0;
int brightness = 128;
int numStrips = 1;
int ledsPerStrip = 60;

const CRGB colorList[] = {
  CRGB::Red, CRGB::Green, CRGB::Blue, CRGB::Yellow, CRGB::Cyan,
  CRGB::Magenta, CRGB::Orange, CRGB::Purple, CRGB::Pink, CRGB::White,
  CRGB::Aqua, CRGB::Lime, CRGB::Gold, CRGB::Teal, CRGB::Violet,
  CRGB::NavajoWhite, CRGB::Coral, CRGB::Turquoise, CRGB::Salmon
};
const char* colorNames[] = {
  "Red", "Green", "Blue", "Yellow", "Cyan", "Magenta", "Orange", "Purple", "Pink",
  "White", "Aqua", "Lime", "Gold", "Teal", "Violet", "Navajo", "Coral", "Turq", "Salmon"
};
const int NUM_COLORS = sizeof(colorList) / sizeof(CRGB);

// ===== Audio & FFT =====
I2SStream i2s;
BluetoothA2DPSink a2dp_sink(i2s);
double vReal[SAMPLES], vImag[SAMPLES];
ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, SAMPLES, SAMPLING_FREQUENCY);
volatile bool newAudioFrame = false;
uint8_t tempAudio[SAMPLES * 2];

// ===== Button Logic with Hold =====
bool heldLeft = false, heldRight = false;
unsigned long lastPressLeft = 0, lastRepeatLeft = 0;
unsigned long lastPressRight = 0, lastRepeatRight = 0;
int holdCountLeft = 0, holdCountRight = 0;

bool readButton(uint8_t pin, bool &held, unsigned long &lastPress, unsigned long &lastRepeat, int &holdCount) {
  bool state = digitalRead(pin) == LOW;
  bool pressed = false;

  if (state && !held) {
    held = true;
    lastPress = millis();
    lastRepeat = millis();
    holdCount = 0;
    pressed = true;
  }

  if (held && state) {
    if (millis() - lastRepeat > 150) {
      lastRepeat = millis();
      holdCount++;
      pressed = true;
    }
  }

  if (!state) held = false;
  return pressed;
}

// ===== EEPROM Save/Load =====
void saveSettings() {
  EEPROM.write(0, audioMode);
  EEPROM.write(1, effectMode);
  EEPROM.write(2, selectedColorIndex);
  EEPROM.write(3, brightness);
  EEPROM.write(4, numStrips);
  EEPROM.write(5, ledsPerStrip);
  EEPROM.commit();
}
void loadSettings() {
  audioMode = EEPROM.read(0);
  effectMode = EEPROM.read(1);
  selectedColorIndex = EEPROM.read(2);
  brightness = EEPROM.read(3);
  numStrips = EEPROM.read(4);
  ledsPerStrip = EEPROM.read(5);
  selectedColorIndex = constrain(selectedColorIndex, 0, NUM_COLORS - 1);
  ledsPerStrip = constrain(ledsPerStrip, 1, MAX_LEDS);
}

// ===== Display =====
void drawMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  switch (currentMenu) {
    case MENU_AUDIO_MODE:
      display.setCursor(0, 0); display.println("Audio Mode");
      display.setCursor(10, 20); display.println(audioMode == 0 ? "> FFT" : "> Beat Detection");
      break;
    case MENU_EFFECT_MODE:
      display.setCursor(0, 0); display.println("Effect Mode");
      display.setCursor(10, 20);
      if (effectMode == 0) display.println("> Single Color");
      else if (effectMode == 1) display.println("> Rainbow");
      else display.println("> Grouped Spectrum");
      break;
    case MENU_SINGLE_COLOR:
      display.setCursor(0, 0); display.println("Single Color");
      display.setCursor(10, 20); display.print("> ");
      display.println(colorNames[selectedColorIndex]);
      break;
    case MENU_BRIGHTNESS:
      display.setCursor(0, 0); display.println("Brightness");
      display.setCursor(10, 20); display.print("> "); display.println(brightness);
      break;
    case MENU_NUM_STRIPS:
      display.setCursor(0, 0); display.println("Strips");
      display.setCursor(10, 20); display.print("> "); display.println(numStrips);
      break;
    case MENU_NUM_LEDS:
      display.setCursor(0, 0); display.println("LEDs/Strip");
      display.setCursor(10, 20); display.print("> "); display.println(ledsPerStrip);
      break;
  }
  display.display();
}

// ===== Beat Detection =====
bool isBeatDetected(double *magnitudes, int fromBin, int toBin) {
  double avg = 0;
  for (int i = fromBin; i <= toBin; i++) avg += magnitudes[i];
  avg /= (toBin - fromBin + 1);
  return avg > 5000;
}

// ===== Audio Callback =====
void read_audio_stream(const uint8_t *data, uint32_t length) {
  if (length >= SAMPLES * 2) length = SAMPLES * 2;
  memcpy(tempAudio, data, length);
  newAudioFrame = true;
}

// ===== Setup =====
void setup() {
  Serial.begin(115200);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_SAVE, INPUT_PULLUP);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
    Serial.println("OLED not found");
    while (true);
  }
  EEPROM.begin(EEPROM_SIZE);
  loadSettings();
  drawMenu();

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, MAX_LEDS);
  FastLED.setBrightness(brightness);

  auto cfg = i2s.defaultConfig();
  cfg.pin_bck = I2S_BCK;
  cfg.pin_ws = I2S_WS;
  cfg.pin_data = I2S_DOUT;
  i2s.begin(cfg);
  a2dp_sink.set_stream_reader(read_audio_stream);
  a2dp_sink.start("ESP32_Music_RGB");
}

// ===== Loop =====
void loop() {
  if (newAudioFrame) {
    newAudioFrame = false;
    for (int i = 0; i < SAMPLES; i++) {
      int16_t sample = (tempAudio[2 * i + 1] << 8) | tempAudio[2 * i];
      vReal[i] = (double)sample;
      vImag[i] = 0.0;
    }

    FFT.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    FFT.compute(FFT_FORWARD);
    FFT.complexToMagnitude();

    if (audioMode == 0) {
      for (int i = 0; i < ledsPerStrip; i++) {
        int bin = map(i, 0, ledsPerStrip - 1, 2, 50);
        float value = constrain(vReal[bin] / 5000.0, 0.0, 1.0);
        if (effectMode == 0) {  // Single Color
          leds[i] = colorList[selectedColorIndex];
          leds[i].fadeToBlackBy(255 - value * 255);
        } else if (effectMode == 1) {  // Rainbow
          leds[i] = CHSV((i * 256 / ledsPerStrip + millis() / 5) % 255, 255, value * 255);
        } else {  // Grouped Spectrum
          leds[i] = CHSV(map(bin, 2, 50, 0, 255), 255, value * 255);
        }
      }
    } else {
      static bool flash = false;
      if (isBeatDetected(vReal, 3, 10)) {
        flash = !flash;
        if (effectMode == 0) {
          fill_solid(leds, ledsPerStrip, colorList[selectedColorIndex]);
        } else if (effectMode == 1) {
          fill_rainbow(leds, ledsPerStrip, random(0, 255), 7);
        } else {
          for (int i = 0; i < ledsPerStrip; i++) {
            leds[i] = CHSV(map(i, 0, ledsPerStrip - 1, 0, 255), 255, 200);
          }
        }
      } else if (!flash) {
        fill_solid(leds, ledsPerStrip, CRGB::Black);
      }
    }

    FastLED.show();
  }

  // Handle Buttons
  if (readButton(BTN_LEFT, heldLeft, lastPressLeft, lastRepeatLeft, holdCountLeft)) {
    switch (currentMenu) {
      case MENU_AUDIO_MODE: audioMode = !audioMode; break;
      case MENU_EFFECT_MODE: effectMode = (effectMode + 2) % 3; break;
      case MENU_SINGLE_COLOR: selectedColorIndex = (selectedColorIndex - 1 + NUM_COLORS) % NUM_COLORS; break;
      case MENU_BRIGHTNESS: if (brightness > 10) brightness -= 10; FastLED.setBrightness(brightness); break;
      case MENU_NUM_STRIPS: if (numStrips > 1) numStrips--; break;
      case MENU_NUM_LEDS: if (ledsPerStrip > 1) ledsPerStrip--; break;
    }
    drawMenu(); saveSettings();
  }

  if (readButton(BTN_RIGHT, heldRight, lastPressRight, lastRepeatRight, holdCountRight)) {
    switch (currentMenu) {
      case MENU_AUDIO_MODE: audioMode = !audioMode; break;
      case MENU_EFFECT_MODE: effectMode = (effectMode + 1) % 3; break;
      case MENU_SINGLE_COLOR: selectedColorIndex = (selectedColorIndex + 1) % NUM_COLORS; break;
      case MENU_BRIGHTNESS: if (brightness < 255) brightness += 10; FastLED.setBrightness(brightness); break;
      case MENU_NUM_STRIPS: if (numStrips < 10) numStrips++; break;
      case MENU_NUM_LEDS: if (ledsPerStrip < MAX_LEDS) ledsPerStrip++; break;
    }
    drawMenu(); saveSettings();
  }

  if (digitalRead(BTN_SAVE) == LOW) {
    delay(250);
    currentMenu = (MenuState)((currentMenu + 1) % 6);
    drawMenu();
  }
}
//saurabh
