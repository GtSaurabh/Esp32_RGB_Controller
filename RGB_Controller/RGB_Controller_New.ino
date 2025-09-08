#include <WiFi.h>
#include <EEPROM.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <FastLED.h>
#include <arduinoFFT.h>
#include <driver/i2s.h>

// Hardware Pin Definitions
#define OLED_SDA 21
#define OLED_SCL 22
#define LED_DATA_PIN 14
#define ADC_BCK_PIN 25      // PCM1808 Bit Clock
#define ADC_WS_PIN 26       // PCM1808 Word Select (LRCK)
#define ADC_DATA_PIN 27     // PCM1808 Data Out
#define BTN_LEFT 32
#define BTN_RIGHT 33
#define BTN_MENU 35

// Display Configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// LED Configuration
#define MAX_LEDS 300
#define DEFAULT_LEDS_PER_STRIP 60
CRGB leds[MAX_LEDS];

// Audio Processing Configuration
#define SAMPLES 512
#define SAMPLING_FREQUENCY 44100
#define FREQUENCY_BANDS 8
#define I2S_PORT I2S_NUM_0

// FFT Variables
arduinoFFT FFT = arduinoFFT();
double vReal[SAMPLES];
double vImag[SAMPLES];
uint8_t bandValues[FREQUENCY_BANDS];
int16_t audioBuffer[SAMPLES];

// Menu System Enums
enum AudioMode { FFT_MODE, BEAT_MODE };
enum EffectMode { SINGLE_COLOR, RAINBOW_FLOW, GROUPED_SPECTRUM };
enum MenuState { 
  MENU_AUDIO_MODE, 
  MENU_EFFECT_MODE, 
  MENU_COLOR_SELECT, 
  MENU_BRIGHTNESS, 
  MENU_NUM_STRIPS, 
  MENU_LEDS_PER_STRIP,
  MENU_GAIN
};

// Color Palette
CRGB colorPalette[] = {
  CRGB::Red, CRGB::Green, CRGB::Blue, CRGB::Yellow,
  CRGB::Cyan, CRGB::Magenta, CRGB::White, CRGB::Orange,
  CRGB::Purple, CRGB::Pink, CRGB::Lime, CRGB::Aqua
};
const uint8_t numColors = sizeof(colorPalette) / sizeof(colorPalette[0]);

// Settings Structure
struct Settings {
  AudioMode audioMode;
  EffectMode effectMode;
  uint8_t selectedColor;
  uint8_t brightness;
  uint8_t numStrips;
  uint16_t ledsPerStrip;
  uint8_t audioGain;
  uint32_t checksum;
};

// Global Variables
Settings settings;
MenuState currentMenu = MENU_AUDIO_MODE;
bool menuChanged = true;
unsigned long lastButtonPress = 0;
const unsigned long DEBOUNCE_DELAY = 200;
uint8_t beatThreshold = 30;
bool beatDetected = false;
unsigned long lastBeatTime = 0;
uint8_t rainbowHue = 0;
float peakLevel = 0;
unsigned long lastFFTUpdate = 0;

// EEPROM Configuration
#define EEPROM_SIZE 512
#define SETTINGS_ADDRESS 0

// Function Prototypes
void initializeSystem();
void initializeI2S();
void loadSettings();
void saveSettings();
uint32_t calculateChecksum(Settings& s);
void handleButtons();
void updateDisplay();
void processAudio();
void updateLEDs();
void performFFT();
void detectBeat();
void singleColorEffect();
void rainbowFlowEffect();
void groupedSpectrumEffect();
void readAudioData();

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("ESP32 Music Reactive RGB Controller with PCM1808 ADC");
  
  initializeSystem();
  initializeI2S();
  loadSettings();
  
  Serial.println("System initialized successfully!");
}

void loop() {
  handleButtons();
  
  if (menuChanged) {
    updateDisplay();
    menuChanged = false;
  }
  
  processAudio();
  updateLEDs();
  
  delay(10); // 100 FPS update rate
}

void initializeSystem() {
  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);
  
  // Initialize I2C for OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  
  // Initialize OLED Display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 allocation failed");
    while(1);
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Initializing...");
  display.display();
  
  // Initialize button pins
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_MENU, INPUT_PULLUP);
  
  // Initialize FastLED
  FastLED.addLeds<WS2812B, LED_DATA_PIN, GRB>(leds, MAX_LEDS);
  FastLED.setBrightness(100);
  FastLED.clear();
  FastLED.show();
  
  // Initialize default settings
  settings.audioMode = FFT_MODE;
  settings.effectMode = GROUPED_SPECTRUM;
  settings.selectedColor = 0;
  settings.brightness = 100;
  settings.numStrips = 1;
  settings.ledsPerStrip = DEFAULT_LEDS_PER_STRIP;
  settings.audioGain = 50;
  
  Serial.println("Hardware initialization complete");
}

void initializeI2S() {
  // I2S configuration for PCM1808 ADC
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLING_FREQUENCY,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  
  // I2S pin configuration for PCM1808
  i2s_pin_config_t pin_config = {
    .bck_io_num = ADC_BCK_PIN,
    .ws_io_num = ADC_WS_PIN,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = ADC_DATA_PIN
  };
  
  // Install and start I2S driver
  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("I2S driver install failed: %s\n", esp_err_to_name(err));
    return;
  }
  
  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("I2S pin config failed: %s\n", esp_err_to_name(err));
    return;
  }
  
  Serial.println("I2S initialized for PCM1808 ADC");
}

void processAudio() {
  readAudioData();
  
  if (settings.audioMode == FFT_MODE) {
    performFFT();
  } else {
    detectBeat();
  }
}

void readAudioData() {
  size_t bytes_read = 0;
  i2s_read(I2S_PORT, audioBuffer, sizeof(audioBuffer), &bytes_read, portMAX_DELAY);
  
  // Apply gain adjustment
  float gainMultiplier = settings.audioGain / 50.0f;
  for (int i = 0; i < SAMPLES; i++) {
    audioBuffer[i] = (int16_t)(audioBuffer[i] * gainMultiplier);
  }
}

void performFFT() {
  // Copy audio data to FFT arrays
  for (int i = 0; i < SAMPLES; i++) {
    vReal[i] = audioBuffer[i];
    vImag[i] = 0;
  }
  
  // Perform FFT
  FFT.Windowing(vReal, SAMPLES, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFT.Compute(vReal, vImag, SAMPLES, FFT_FORWARD);
  FFT.ComplexToMagnitude(vReal, vImag, SAMPLES);
  
  // Map frequency bands
  int samplesPerBand = (SAMPLES / 2) / FREQUENCY_BANDS;
  for (int band = 0; band < FREQUENCY_BANDS; band++) {
    float sum = 0;
    int startIdx = band * samplesPerBand;
    int endIdx = startIdx + samplesPerBand;
    
    for (int i = startIdx; i < endIdx && i < SAMPLES/2; i++) {
      sum += vReal[i];
    }
    
    // Average and scale the band value
    float average = sum / samplesPerBand;
    bandValues[band] = constrain(map(average, 0, 5000, 0, 255), 0, 255);
  }
}

void detectBeat() {
  // Simple beat detection based on RMS energy
  float rms = 0;
  for (int i = 0; i < SAMPLES; i++) {
    rms += (audioBuffer[i] * audioBuffer[i]);
  }
  rms = sqrt(rms / SAMPLES);
  
  // Update peak level with decay
  peakLevel = max(rms, peakLevel * 0.95f);
  
  // Beat detection
  if (rms > (peakLevel * 0.7f) && millis() - lastBeatTime > 200) {
    beatDetected = true;
    lastBeatTime = millis();
  } else if (millis() - lastBeatTime > 100) {
    beatDetected = false;
  }
}

void updateLEDs() {
  switch (settings.effectMode) {
    case SINGLE_COLOR:
      singleColorEffect();
      break;
    case RAINBOW_FLOW:
      rainbowFlowEffect();
      break;
    case GROUPED_SPECTRUM:
      groupedSpectrumEffect();
      break;
  }
  
  FastLED.setBrightness(settings.brightness);
  FastLED.show();
}

void singleColorEffect() {
  CRGB color = colorPalette[settings.selectedColor];
  
  if (settings.audioMode == BEAT_MODE && beatDetected) {
    // Flash effect on beat
    fill_solid(leds, settings.ledsPerStrip, color);
  } else if (settings.audioMode == FFT_MODE) {
    // Intensity based on average frequency content
    uint8_t intensity = 0;
    for (int i = 0; i < FREQUENCY_BANDS; i++) {
      intensity += bandValues[i];
    }
    intensity = intensity / FREQUENCY_BANDS;
    
    color.nscale8_video(intensity);
    fill_solid(leds, settings.ledsPerStrip, color);
  } else {
    // Fade out when no beat
    fadeToBlackBy(leds, settings.ledsPerStrip, 20);
  }
}

void rainbowFlowEffect() {
  rainbowHue += 2;
  
  if (settings.audioMode == FFT_MODE) {
    // Speed based on audio intensity
    uint8_t speed = 0;
    for (int i = 0; i < FREQUENCY_BANDS; i++) {
      speed += bandValues[i];
    }
    speed = speed / FREQUENCY_BANDS / 10;
    rainbowHue += speed;
  }
  
  fill_rainbow(leds, settings.ledsPerStrip, rainbowHue, 7);
  
  if (settings.audioMode == BEAT_MODE && beatDetected) {
    // Brightness boost on beat
    for (int i = 0; i < settings.ledsPerStrip; i++) {
      leds[i].nscale8_video(200);
    }
  }
}

void groupedSpectrumEffect() {
  if (settings.audioMode != FFT_MODE) return;
  
  int ledsPerBand = settings.ledsPerStrip / FREQUENCY_BANDS;
  
  for (int band = 0; band < FREQUENCY_BANDS; band++) {
    CRGB color = CHSV(band * 32, 255, bandValues[band]);
    
    int startLed = band * ledsPerBand;
    int endLed = min(startLed + ledsPerBand, (int)settings.ledsPerStrip);
    
    for (int led = startLed; led < endLed; led++) {
      leds[led] = color;
    }
  }
}

void handleButtons() {
  if (millis() - lastButtonPress < DEBOUNCE_DELAY) return;
  
  bool leftPressed = !digitalRead(BTN_LEFT);
  bool rightPressed = !digitalRead(BTN_RIGHT);
  bool menuPressed = !digitalRead(BTN_MENU);
  
  if (leftPressed || rightPressed || menuPressed) {
    lastButtonPress = millis();
    menuChanged = true;
    
    if (menuPressed) {
      // Cycle through menu items
      currentMenu = (MenuState)((currentMenu + 1) % 7);
    } else if (leftPressed) {
      // Decrease current menu value
      switch (currentMenu) {
        case MENU_AUDIO_MODE:
          settings.audioMode = (settings.audioMode == FFT_MODE) ? BEAT_MODE : FFT_MODE;
          break;
        case MENU_EFFECT_MODE:
          settings.effectMode = (EffectMode)((settings.effectMode + 2) % 3);
          break;
        case MENU_COLOR_SELECT:
          settings.selectedColor = (settings.selectedColor + numColors - 1) % numColors;
          break;
        case MENU_BRIGHTNESS:
          settings.brightness = max(10, settings.brightness - 10);
          break;
        case MENU_NUM_STRIPS:
          settings.numStrips = max(1, settings.numStrips - 1);
          break;
        case MENU_LEDS_PER_STRIP:
          settings.ledsPerStrip = max(10, settings.ledsPerStrip - 10);
          break;
        case MENU_GAIN:
          settings.audioGain = max(10, settings.audioGain - 5);
          break;
      }
    } else if (rightPressed) {
      // Increase current menu value
      switch (currentMenu) {
        case MENU_AUDIO_MODE:
          settings.audioMode = (settings.audioMode == FFT_MODE) ? BEAT_MODE : FFT_MODE;
          break;
        case MENU_EFFECT_MODE:
          settings.effectMode = (EffectMode)((settings.effectMode + 1) % 3);
          break;
        case MENU_COLOR_SELECT:
          settings.selectedColor = (settings.selectedColor + 1) % numColors;
          break;
        case MENU_BRIGHTNESS:
          settings.brightness = min(255, settings.brightness + 10);
          break;
        case MENU_NUM_STRIPS:
          settings.numStrips = min(10, settings.numStrips + 1);
          break;
        case MENU_LEDS_PER_STRIP:
          settings.ledsPerStrip = min(MAX_LEDS, settings.ledsPerStrip + 10);
          break;
        case MENU_GAIN:
          settings.audioGain = min(100, settings.audioGain + 5);
          break;
      }
    }
    
    saveSettings();
  }
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  // Display title
  display.setCursor(0, 0);
  display.println("Music RGB Controller");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
  
  // Display current menu item
  display.setCursor(0, 15);
  display.print("> ");
  
  switch (currentMenu) {
    case MENU_AUDIO_MODE:
      display.print("Audio: ");
      display.println(settings.audioMode == FFT_MODE ? "FFT" : "Beat");
      break;
    case MENU_EFFECT_MODE:
      display.print("Effect: ");
      switch (settings.effectMode) {
        case SINGLE_COLOR: display.println("Single"); break;
        case RAINBOW_FLOW: display.println("Rainbow"); break;
        case GROUPED_SPECTRUM: display.println("Spectrum"); break;
      }
      break;
    case MENU_COLOR_SELECT:
      display.print("Color: ");
      display.println(settings.selectedColor);
      break;
    case MENU_BRIGHTNESS:
      display.print("Bright: ");
      display.println(settings.brightness);
      break;
    case MENU_NUM_STRIPS:
      display.print("Strips: ");
      display.println(settings.numStrips);
      break;
    case MENU_STRIP1_LEDS:
      display.print("Strip1: ");
      display.println(settings.ledsPerStrip[0]);
      break;
    case MENU_STRIP2_LEDS:
      display.print("Strip2: ");
      display.println(settings.ledsPerStrip[1]);
      break;
    case MENU_STRIP3_LEDS:
      display.print("Strip3: ");
      display.println(settings.ledsPerStrip[2]);
      break;
    case MENU_STRIP4_LEDS:
      display.print("Strip4: ");
      display.println(settings.ledsPerStrip[3]);
      break;
    case MENU_GAIN:
      display.print("Gain: ");
      display.println(settings.audioGain);
      break;
  }
  
  // Display audio visualization
  if (settings.audioMode == FFT_MODE) {
    display.setCursor(0, 35);
    display.println("FFT Bands:");
    for (int i = 0; i < FREQUENCY_BANDS; i++) {
      int barHeight = map(bandValues[i], 0, 255, 0, 15);
      int x = i * 15;
      display.drawLine(x, 63, x, 63 - barHeight, SSD1306_WHITE);
    }
  } else {
    display.setCursor(0, 35);
    display.print("Beat: ");
    display.println(beatDetected ? "YES" : "NO");
    
    // Beat indicator
    if (beatDetected) {
      display.fillCircle(100, 45, 5, SSD1306_WHITE);
    } else {
      display.drawCircle(100, 45, 5, SSD1306_WHITE);
    }
  }
  
  display.display();
}

uint32_t calculateChecksum(Settings& s) {
  uint32_t checksum = 0;
  uint8_t* data = (uint8_t*)&s;
  for (size_t i = 0; i < sizeof(Settings) - sizeof(uint32_t); i++) {
    checksum += data[i];
  }
  return checksum;
}

void saveSettings() {
  settings.checksum = calculateChecksum(settings);
  EEPROM.put(SETTINGS_ADDRESS, settings);
  EEPROM.commit();
  Serial.println("Settings saved to EEPROM");
}

void loadSettings() {
  Settings loadedSettings;
  EEPROM.get(SETTINGS_ADDRESS, loadedSettings);
  
  // Verify checksum
  uint32_t expectedChecksum = calculateChecksum(loadedSettings);
  if (loadedSettings.checksum == expectedChecksum) {
    settings = loadedSettings;
    Serial.println("Settings loaded from EEPROM");
  } else {
    Serial.println("Invalid settings in EEPROM, using defaults");
    saveSettings(); // Save default settings
  }
}
