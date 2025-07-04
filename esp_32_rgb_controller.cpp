// PART 3: Add beat detection mode
#include <MCUFRIEND_kbv.h>
#include <TouchScreen.h>
#include <FastLED.h>
#include <Preferences.h>
#include <BluetoothA2DPSink.h>
#include <arduinoFFT.h>

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320
#define SAMPLES 512
#define SAMPLING_FREQUENCY 44100

#define YP 15
#define XM 33
#define YM 32
#define XP 4
#define MINPRESSURE 200
#define MAXPRESSURE 1000

TouchScreen ts = TouchScreen(XP, YP, XM, YM, 300);
MCUFRIEND_kbv tft;
Preferences prefs;

#define MAX_STRIPS 4
#define MAX_LEDS 150
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB
const uint8_t LED_PINS[MAX_STRIPS] = {21, 22, 27, 32};
CRGB leds[MAX_STRIPS][MAX_LEDS];

uint8_t stripCount = 1;
uint16_t ledsPerStrip[MAX_STRIPS] = {30, 30, 30, 30};
String audioMode = "fft";

BluetoothA2DPSink a2dp_sink;
arduinoFFT FFT = arduinoFFT();
double vReal[SAMPLES];
double vImag[SAMPLES];

volatile uint16_t sampleIndex = 0;
uint8_t audioBuffer[1024];
uint32_t audioBufferLen = 0;
bool newAudioData = false;

float lastEnergy = 0;
float beatThreshold = 1.5;
unsigned long lastBeatTime = 0;
unsigned long beatCooldown = 150;

enum ScreenID
{
  SPLASH,
  MAIN_MENU,
  AUDIO_MODE,
  LED_CONFIG_STRIPS,
  LED_CONFIG_COUNT,
  VISUALIZER
};
ScreenID currentScreen = SPLASH;
unsigned long splashStartTime;
uint8_t currentStripIndex = 0;

void drawSplash()
{
  tft.fillScreen(BLACK);
  tft.setTextColor(WHITE);
  tft.setTextSize(3);
  tft.setCursor(20, 140);
  tft.print("RGB CONTROLLER");
  splashStartTime = millis();
}

void drawMainMenu()
{
  tft.fillScreen(BLACK);
  tft.setTextColor(YELLOW);
  tft.setTextSize(2);
  tft.setCursor(40, 50);
  tft.print("1. Audio Mode");
  tft.setCursor(40, 100);
  tft.print("2. Configure LEDs");
  tft.setCursor(40, 150);
  tft.print("3. Start Visualizer");
}

void drawAudioModeScreen()
{
  tft.fillScreen(BLACK);
  tft.setCursor(40, 50);
  tft.setTextColor(WHITE);
  tft.setTextSize(2);
  tft.print("SELECT AUDIO MODE");
  tft.setCursor(40, 100);
  tft.print(audioMode == "fft" ? "> FFT" : "  FFT");
  tft.setCursor(40, 140);
  tft.print(audioMode == "beat" ? "> Beat" : "  Beat");
}

void drawLEDStripSelector()
{
  tft.fillScreen(BLACK);
  tft.setTextSize(2);
  tft.setCursor(30, 50);
  tft.print("Strips: ");
  tft.print(stripCount);
  tft.setCursor(30, 100);
  tft.print("Tap to continue");
}

void drawLEDCountConfig()
{
  tft.fillScreen(BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 40);
  tft.print("Strip #");
  tft.print(currentStripIndex + 1);
  tft.setCursor(10, 90);
  tft.print("LEDs: ");
  tft.print(ledsPerStrip[currentStripIndex]);
  tft.setCursor(10, 140);
  tft.print("Tap to next/finish");
}

void drawVisualizerScreen()
{
  tft.fillScreen(BLACK);
  tft.setTextSize(2);
  tft.setCursor(20, 100);
  tft.print("Visualizer Running...");
}

void saveConfig()
{
  prefs.begin("rgbcfg", false);
  prefs.putUInt("stripCount", stripCount);
  for (int i = 0; i < stripCount; i++)
  {
    prefs.putUInt(("leds_" + String(i)).c_str(), ledsPerStrip[i]);
  }
  prefs.putString("audioMode", audioMode);
  prefs.end();
}

void loadConfig()
{
  prefs.begin("rgbcfg", true);
  stripCount = prefs.getUInt("stripCount", 1);
  for (int i = 0; i < stripCount; i++)
  {
    ledsPerStrip[i] = prefs.getUInt(("leds_" + String(i)).c_str(), 30);
  }
  audioMode = prefs.getString("audioMode", "fft");
  prefs.end();
}

void read_data_stream(const uint8_t *data, uint32_t len)
{
  audioBufferLen = len > sizeof(audioBuffer) ? sizeof(audioBuffer) : len;
  memcpy(audioBuffer, data, audioBufferLen);
  newAudioData = true;
}

void processFFT()
{
  if (!newAudioData)
    return;

  for (int i = 0; i < SAMPLES; i++)
  {
    vReal[i] = (i * 2 < audioBufferLen) ? audioBuffer[i * 2] - 128 : 0;
    vImag[i] = 0;
  }
  FFT.Windowing(vReal, SAMPLES, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFT.Compute(vReal, vImag, SAMPLES, FFT_FORWARD);
  FFT.ComplexToMagnitude(vReal, vImag, SAMPLES);

  for (int i = 0; i < stripCount; i++)
  {
    for (int j = 0; j < ledsPerStrip[i]; j++)
    {
      int bin = map(j, 0, ledsPerStrip[i], 1, SAMPLES / 2);
      int level = constrain((int)(vReal[bin] / 10), 0, 255);
      leds[i][j] = CHSV(level, 255, level);
    }
    FastLED.show();
  }
  newAudioData = false;
}

void processBeat()
{
  if (!newAudioData)
    return;
  float energy = 0;
  for (int i = 0; i < audioBufferLen; i++)
  {
    float sample = (float)(audioBuffer[i] - 128);
    energy += sample * sample;
  }
  energy /= audioBufferLen;

  if (energy > beatThreshold * lastEnergy && millis() - lastBeatTime > beatCooldown)
  {
    for (int i = 0; i < stripCount; i++)
    {
      for (int j = 0; j < ledsPerStrip[i]; j++)
      {
        leds[i][j] = CHSV(random8(), 255, 255);
      }
    }
    FastLED.show();
    lastBeatTime = millis();
  }
  else
  {
    for (int i = 0; i < stripCount; i++)
    {
      for (int j = 0; j < ledsPerStrip[i]; j++)
      {
        leds[i][j].fadeToBlackBy(20);
      }
    }
    FastLED.show();
  }
  lastEnergy = energy;
  newAudioData = false;
}

void handleTouch()
{
  TSPoint p = ts.getPoint();
  pinMode(XM, OUTPUT);
  pinMode(YP, OUTPUT);

  if (p.z > MINPRESSURE && p.z < MAXPRESSURE)
  {
    int x = map(p.y, 150, 900, 0, 240);
    int y = map(p.x, 120, 920, 0, 320);

    switch (currentScreen)
    {
    case MAIN_MENU:
      if (y >= 50 && y < 80)
      {
        currentScreen = AUDIO_MODE;
        drawAudioModeScreen();
      }
      else if (y >= 100 && y < 130)
      {
        currentScreen = LED_CONFIG_STRIPS;
        drawLEDStripSelector();
      }
      else if (y >= 150 && y < 180)
      {
        currentScreen = VISUALIZER;
        drawVisualizerScreen();
      }
      break;

    case AUDIO_MODE:
      if (y >= 100 && y < 130)
        audioMode = "fft";
      else if (y >= 140 && y < 170)
        audioMode = "beat";
      saveConfig();
      drawMainMenu();
      currentScreen = MAIN_MENU;
      break;

    case LED_CONFIG_STRIPS:
      stripCount = constrain(stripCount + 1, 1, MAX_STRIPS);
      currentStripIndex = 0;
      currentScreen = LED_CONFIG_COUNT;
      drawLEDCountConfig();
      break;

    case LED_CONFIG_COUNT:
      ledsPerStrip[currentStripIndex] += 10;
      if (ledsPerStrip[currentStripIndex] > 150)
        ledsPerStrip[currentStripIndex] = 10;
      currentStripIndex++;
      if (currentStripIndex >= stripCount)
      {
        saveConfig();
        currentScreen = MAIN_MENU;
        drawMainMenu();
      }
      else
      {
        drawLEDCountConfig();
      }
      break;

    default:
      break;
    }
  }
}

void setup()
{
  Serial.begin(115200);
  uint16_t ID = tft.readID();
  tft.begin(ID);
  tft.setRotation(1);
  loadConfig();
  drawSplash();
  for (int i = 0; i < stripCount; i++)
  {
    FastLED.addLeds<LED_TYPE, LED_PINS[i], COLOR_ORDER>(leds[i], ledsPerStrip[i]);
  }
  a2dp_sink.set_stream_reader(read_data_stream, false);
  a2dp_sink.start("ESP32_RGB_SPEAKER");
}

void loop()
{
  if (currentScreen == SPLASH && millis() - splashStartTime > 2000)
  {
    currentScreen = MAIN_MENU;
    drawMainMenu();
  }
  handleTouch();
  if (currentScreen == VISUALIZER)
  {
    if (audioMode == "fft")
    {
      processFFT();
    }
    else if (audioMode == "beat")
    {
      processBeat();
    }
  }
}
