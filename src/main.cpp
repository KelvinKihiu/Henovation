#include <Arduino.h>
#include "data.h"

// Pin definition
#define dht_apin A0
#define buzzer 9
#define tft_cs 3
#define tft_dc 4
#define tft_rst 5
#define interruptPin 2
#define ds1302_io 6
#define ds1302_sclk 7
#define ds1302_ce 8
#define ledPin 10

// Constants
#define __SETTIME__ 0

// DS1302
RtcDS1302<ThreeWire> Rtc(*new ThreeWire(ds1302_io, ds1302_sclk, ds1302_ce));

// DHT11
dht DHT;
unsigned long dhtLastRead = 0;

// TFT
TFT screen = TFT(tft_cs, tft_dc, tft_rst);

// Date formatters
const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char *dayOfWeek[] = {"Sunday   ", "Monday   ", "Tuesday  ", "Wednesday", "Thursday ", "Friday   ", "Saturday "};
const uint8_t t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};

// RTC
RtcDateTime now;

// BMP280
Adafruit_BMP280 bmp;
unsigned bmpStatus = 0;

// In-memory Cache
uint8_t dateCache = -1;
uint8_t minsCache = -1;
uint8_t secsCache = -1;
int temperatureCache = 0;
int humidityCache = 0;
int pressureCache = 0;

// Buttons
ezButton btnUp(A1);
ezButton btnDown(A2);

// Timers
volatile unsigned long clk = 0;
unsigned long atime = millis();
unsigned long menuIdle = 0;
unsigned long rtcLastRead = 0;

// Constants
const char space = ' ';

// Configuration
Config config;

// Reusable String Buffers
char datestring[23];
char timestring[6];
char secstring[3];
char alarmHrstring[3];
char alarmMinstring[3];
char tempstring[4];
char humstring[4];
char prestring[4];
char histring[3];

// Flags
Mode mode = MENU; /* 0. Home Mode 1. Alarm Mode: Set Hour 2. Alarm Mode: Set Mins 3. Menu */
volatile bool modeChanged = false;
uint8_t selectedMenuOption = 0;

// Tone counters
#ifdef __FEATURE_ALARM__
uint8_t currNote = 0;
unsigned int pauseBetweenNotes = 0;
#endif

void setup()
{
  // Serial.begin(9600);

  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH);

  // Load the configuration
  EEPROM.get(0, config);

  // Start and read the RTC module
  startRTC();

  // Initialize the Tft display
  initScreen();

  // Display the main menu
  displayMenu();

  // OK button to interrupt 0
  pinMode(interruptPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(interruptPin), isrChangeMode, FALLING);

  // Direction buttons
  btnUp.setDebounceTime(50);
  btnDown.setDebounceTime(50);

  // Start the BMP sensor
  bmpStatus = bmp.begin(0x76);

  /* Default settings from datasheet. */
  bmp.setSampling(Adafruit_BMP280::MODE_FORCED,     /* Operating Mode. */
                  Adafruit_BMP280::SAMPLING_X2,     /* Temp. oversampling */
                  Adafruit_BMP280::SAMPLING_X16,    /* Pressure oversampling */
                  Adafruit_BMP280::FILTER_X16,      /* Filtering. */
                  Adafruit_BMP280::STANDBY_MS_500); /* Standby time. */
}

void loop()
{
  if (modeChanged)
  {
    modeChanged = false;

    if (mode == MENU)
    {
      switch (selectedMenuOption)
      {
      case 0:
        mode = HOME;
        displayShell();
        break;
      case 1:
#ifdef __FEATURE_ALARM__
        mode = ALARM_HR;
        alarmShell();
#endif
        break;
      case 2:
        mode = CONFIG;
        configShell();
        break;
      case 3:
        reboot();
        break;
      }
    }
    else
    {
      menuIdle = millis();
      mode = MENU;
      displayMenu();
    }
  }

  if (!modeChanged)
  {
    switch (mode)
    {
    case HOME:
      digitalWrite(ledPin, LOW);
      displayMode();
      break;

#ifdef __FEATURE_ALARM__
    case ALARM_HR:
    case ALARM_MIN:
      digitalWrite(ledPin, HIGH);
      setAlarmMode();
      break;
#endif
    case CONFIG:
      digitalWrite(ledPin, HIGH);
      configMode();
      break;

    default:
      digitalWrite(ledPin, HIGH);
      menuMode();
      break;
    }
  }
}

void isrChangeMode()
{
  if ((millis() - clk) < 1000)
    return;
  clk = millis();
  modeChanged = true;
}

void displayMenu()
{
  selectedMenuOption = 0;
  screen.background(0, 0, 0);
  screen.setTextSize(2);
  screen.stroke(144, 228, 220);
  screen.text("Home", 48, 5);
  screen.text("Alarm", 48, 38);
  screen.text("Config", 48, 71);
  screen.text("Reboot", 48, 104);
  drawImage(arrow, 15, 3, 24, 24);
}

// Helper function to handle arrow drawing and clearing
void updateArrowPosition(uint8_t newSelectedOption, bool isIncrement)
{
  // Calculate Y position based on selection
  byte arrowYPosition = 3 + (newSelectedOption * 33);
  byte oldArrowYPosition = isIncrement ? arrowYPosition - 33 : arrowYPosition + 33;

  // Clear previous arrow
  clearImage(15, oldArrowYPosition, 24, 24);

  // Draw arrow at the new position
  drawImage(arrow, 15, arrowYPosition, 24, 24);
}

void menuMode()
{
  // Timeout to go back to HOME
  if ((millis() - menuIdle) >= 12000)
  {
    mode = HOME;
    displayShell();

    return;
  }

  // Handle button inputs
  bool optionChanged = false;
  bool isIncrement;

  btnUp.loop();
  if (btnUp.isPressed())
  {
    menuIdle = millis();
    if (selectedMenuOption > 0) // Only decrease if it's not the first option
    {
      selectedMenuOption--;
      optionChanged = true;
      isIncrement = false;
    }
  }

  btnDown.loop();
  if (btnDown.isPressed())
  {
    menuIdle = millis();
    if (selectedMenuOption < 3) // Only increase if it's not the last option
    {
      selectedMenuOption++;
      optionChanged = true;
      isIncrement = true;
    }
  }

  // Update arrow position if option has changed
  if (optionChanged)
  {
    updateArrowPosition(selectedMenuOption, isIncrement);
  }
}

void displayShell()
{
  screen.background(0, 0, 0);
  screen.drawRoundRect(5, 5, 152, 60, 10, 0x04D3);
  screen.drawRoundRect(6, 6, 150, 58, 10, 0x04D3);
  screen.setTextSize(2);
  screen.stroke(255, 128, 0);
  screen.text(config.temp == 0 ? "C" : "F", 90, 79);
  screen.text("%", 90, 109);
  screen.text("hPa", 118, 105);

  drawImage(tempImg, 5, 72, 24, 24);
  drawImage(humidityImg, 5, 100, 24, 24);

  temperatureCache = 0;
  humidityCache = 0;

  dateCache = -1;
  minsCache = -1;

  datestring[0] = '\0';
  timestring[0] = '\0';
  secstring[0] = '\0';
  tempstring[0] = '\0';
  humstring[0] = '\0';
}

void displayMode()
{
  readData();
  displayDateTime();
  displayTempAndHum();

#ifdef __FEATURE_ALARM__
  if (now.Hour() == config.alarmHr && now.Minute() == config.alarmMin)
  {
    ringAlarm();
  }
  else
  {
    currNote = 0;
    pauseBetweenNotes = 0;
  }
#endif
}

#ifdef __FEATURE_ALARM__

void alarmShell()
{
  screen.background(0, 0, 0);
  screen.setTextSize(2);
  screen.stroke(0, 255, 0);
  screen.text("Set Alarm", 5, 5);

  screen.setTextSize(4);
  screen.stroke(0, 255, 255);
  screen.text(":", 68, 48);

  alarmHrstring[0] = '\0';
  alarmMinstring[0] = '\0';
}

void ringAlarm()
{
  if ((millis() - atime) >= pauseBetweenNotes)
  {
    noTone(buzzer);
    atime = millis();
    int noteDuration = 1000 / pgm_read_byte(&Pirates_duration[currNote]);
    tone(buzzer, pgm_read_word(&Pirates_note[currNote]), noteDuration);
    pauseBetweenNotes = noteDuration * 1.05;
    currNote++;
  }

  if (currNote >= (sizeof(Pirates_note) / sizeof(uint16_t)))
  {
    currNote = 0;
  }
}

void setAlarmMode()
{
  formatString(alarmHrstring, 3, "%02u", config.alarmHr);
  formatString(alarmMinstring, 3, "%02u", config.alarmMin);

  screen.setTextSize(4);
  screen.stroke(0, 255, 255);
  screen.text(alarmHrstring, 20, 48);
  screen.text(alarmMinstring, 92, 48);

  long tm = 0;
  bool visibleState = false;
  while (true)
  {
    if (mode == ALARM_HR)
    {
      if ((millis() - tm) >= 500)
      {
        tm = millis();
        visibleState = !visibleState;

        if (visibleState)
        {
          formatString(alarmHrstring, 3, "%02u", config.alarmHr);
          screen.stroke(0, 255, 255);
        }
        else
        {
          screen.stroke(0, 0, 0);
        }
        screen.text(alarmHrstring, 20, 48);
      }
    }
    else
    {
      if ((millis() - tm) >= 500)
      {
        tm = millis();
        visibleState = !visibleState;

        if (visibleState)
        {
          formatString(alarmMinstring, 3, "%02u", config.alarmMin);
          screen.stroke(0, 255, 255);
        }
        else
        {
          screen.stroke(0, 0, 0);
        }
        screen.text(alarmMinstring, 92, 48);
      }
    }

    btnUp.loop();
    if (btnUp.isPressed())
    {
      if (mode == ALARM_HR)
      {
        config.alarmHr++;
        if (config.alarmHr > 23)
        {
          config.alarmHr = 0;
        }
      }
      else
      {
        config.alarmMin++;
        if (config.alarmMin > 59)
        {
          config.alarmMin = 0;
        }
      }
    }

    btnDown.loop();
    if (btnDown.isPressed())
    {
      if (mode == ALARM_HR)
      {
        if (config.alarmHr == 0)
        {
          config.alarmHr = 23;
        }
        else
        {
          config.alarmHr--;
        }
      }
      else
      {
        if (config.alarmMin == 0)
        {
          config.alarmMin = 59;
        }
        else
        {
          config.alarmMin--;
        }
      }
    }

    if (modeChanged)
    {
      if (mode == ALARM_HR)
      {
        screen.stroke(0, 0, 0);
        screen.text(alarmHrstring, 20, 48);
        screen.text(alarmMinstring, 92, 48);

        formatString(alarmHrstring, 3, "%02u", config.alarmHr);
        formatString(alarmMinstring, 3, "%02u", config.alarmMin);

        screen.stroke(0, 255, 255);
        screen.text(alarmHrstring, 20, 48);
        screen.text(alarmMinstring, 92, 48);

        tm = 0;
        visibleState = false;
        modeChanged = false;
        mode = (Mode)((int)mode + 1);
      }
      else
      {
        EEPROM.put(0, config);
        break;
      }
    }
  }
}

#endif

void configShell()
{
  screen.background(0, 0, 0);
  screen.setTextSize(2);
  screen.stroke(0, 255, 0);
  screen.text("Config", 50, 5);

  screen.setTextSize(2);
  screen.stroke(255, 255, 0);
  screen.text("Tmp Unit:", 8, 35);
}

void configMode()
{
  char units[2] = {config.temp == 0 ? 'C' : 'F', '\0'};

  screen.setTextSize(2);
  screen.stroke(0, 255, 255);
  screen.text(units, 138, 35);

  unsigned long tm = 0;
  bool visibleState = false;

  while (true)
  {
    if ((millis() - tm) >= 500)
    {
      tm = millis();
      visibleState = !visibleState;

      if (visibleState)
      {
        units[0] = config.temp == 0 ? 'C' : 'F';
        screen.stroke(0, 255, 255);
      }
      else
      {
        screen.stroke(0, 0, 0);
      }

      screen.text(units, 138, 35);
    }

    // Handle button presses
    btnUp.loop();
    btnDown.loop();

    if (btnUp.isPressed() || btnDown.isPressed())
    {
      config.temp = (config.temp == 0) ? 1 : 0;
    }

    if (modeChanged)
    {
      EEPROM.put(0, config);
      break;
    }
  }
}

void startRTC()
{
  Rtc.Begin();
  delay(20);
  setDateTime();
}

void setDateTime()
{
  RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);

  if (!Rtc.IsDateTimeValid())
  {
    Rtc.SetDateTime(compiled);
  }

  if (Rtc.GetIsWriteProtected())
  {
    Rtc.SetIsWriteProtected(false);
  }

  if (!Rtc.GetIsRunning())
  {
    Rtc.SetIsRunning(true);
  }

  now = Rtc.GetDateTime();
  if (__SETTIME__ && now < compiled)
  {
    Rtc.SetDateTime(compiled);
  }
}

static void splashScreen()
{
  const char *text = "Henovation";
  const uint8_t x = 24;
  const uint8_t y = 58;
  uint8_t len = strlen(text);

  screen.setTextSize(2);
  screen.stroke(144, 228, 220);

  char buffer[11]; // Buffer large enough to hold the string "Henovation" + null terminator
  for (uint8_t i = 1; i <= len; i++)
  {
    strncpy(buffer, text, i);  // Copy i characters into the buffer
    buffer[i] = '\0';          // Null-terminate the string
    screen.text(buffer, x, y); // Display the buffer
    delay(200);
  }

  delay(1000); // Hold the full text for 1 second

  // Change colors and display full text
  screen.stroke(0, 255, 255);
  screen.text(text, x, y);
  delay(1000);

  screen.stroke(0, 255, 0);
  screen.text(text, x, y);
  delay(2000);
}

void initScreen()
{
  // initialize the screen
  screen.initR(INITR_BLACKTAB);
  screen.setRotation(1);
  delay(20);

  screen.background(0, 0, 0);
  splashScreen();

  __asm__ __volatile__("sei");
  menuIdle = millis();
}

void readData()
{
  // Read Inputs
  if ((millis() - rtcLastRead) >= 1000)
  {
    now = Rtc.GetDateTime();
    rtcLastRead = millis();
  }

  if ((millis() - dhtLastRead) >= 2000)
  {
    DHT.read11(dht_apin);
    if (bmpStatus)
    {
      bmp.takeForcedMeasurement();
    }
    dhtLastRead = millis();
  }
}

void displayDateTime()
{
  uint8_t seconds = now.Second();

  if (secsCache != seconds)
  {
    screen.setTextSize(2);

    screen.stroke(0, 0, 0);
    screen.text(secstring, 130, 24);

    formatString(secstring, 3, "%02u", seconds);

    screen.stroke(0, 255, 255);
    screen.text(secstring, 130, 24);

    secsCache = seconds;

    __asm__ __volatile__("sbi %0, %1 \n" : : "I"(_SFR_IO_ADDR(PINB)), "I"(PINB2));
    // analogWrite(ledPin, map(seconds / 2, 0, 29, 0, 255));

    // Display the time [HH:MM]
    uint8_t minutes = now.Minute();
    if (minsCache != minutes)
    {
      screen.setTextSize(4);

      screen.stroke(0, 0, 0);
      screen.text(timestring, 10, 10);

      formatString(timestring, 6, "%02u:%02u", now.Hour(), minutes);

      screen.stroke(0, 255, 255);
      screen.text(timestring, 10, 10);

      minsCache = minutes;

      // Display the date
      uint8_t day = now.Day();
      if (dateCache != day)
      {
        screen.setTextSize(1);

        screen.stroke(0, 0, 0);
        screen.text(datestring, 15, 52);

        formatString(datestring, 23, "%s %s %02u, %04u", dayOfWeek[dow(now.Year(), now.Month(), day)], months[now.Month() - 1], day, now.Year());

        screen.stroke(0, 255, 0);
        screen.text(datestring, 15, 52);

        dateCache = day;
      }
    }
  }
}

// Helper function to display formatted text
void displayFormattedValue(char *buffer, uint8_t bufferSize, uint16_t value, uint8_t x, uint8_t y, uint8_t textSize, uint8_t strokeR, uint8_t strokeG, uint8_t strokeB)
{
  // Clear previous value
  screen.setTextSize(textSize);
  screen.stroke(0, 0, 0);
  screen.text(buffer, x, y);

  // Format the new value into the buffer
  if (value > 99)
  {
    formatString(buffer, bufferSize, "%d", value);
  }
  else if (value >= 0 && value < 10)
  {
    formatString(buffer, bufferSize, "  %d", value);
  }
  else
  {
    formatString(buffer, bufferSize, " %d", value);
  }

  // Display the new value
  screen.stroke(strokeR, strokeG, strokeB);
  screen.text(buffer, x, y);
}

void displayTempAndHum()
{
  // Handle temperature display
  int newTemp = getTemp(bmpStatus ? (int)bmp.readTemperature() : DHT.temperature, config.temp);
  if (newTemp != temperatureCache)
  {
    temperatureCache = newTemp;
    displayFormattedValue(tempstring, 4, temperatureCache, 32, 72, 3, 218, 233, 255);
  }

  // Handle humidity display
  if ((int)DHT.humidity != humidityCache)
  {
    humidityCache = (int)DHT.humidity;
    displayFormattedValue(humstring, 4, humidityCache, 32, 102, 3, 64, 224, 208);
  }

  // Handle pressure display
  int newPressure = (int)(bmp.readPressure() / 100);
  if (newPressure != pressureCache)
  {
    pressureCache = newPressure;
    displayFormattedValue(prestring, 4, pressureCache, 108, 79, 3, 64, 224, 208);
  }
}

inline void formatString(char *buffer, size_t size, const char *format, ...)
{
  va_list argptr;
  va_start(argptr, format);
  vsnprintf(buffer, size, format, argptr);
  va_end(argptr);
}

inline uint8_t dow(uint16_t y, uint8_t m, uint8_t d)
{
  y -= m < 3;
  uint16_t yDiv4 = y / 4;
  uint16_t yDiv100 = y / 100;
  uint16_t yDiv400 = y / 400;
  return (y + yDiv4 - yDiv100 + yDiv400 + t[m - 1] + d) % 7;
}

void drawImage(const uint16_t *image, byte xPos, byte yPos, byte height, byte width)
{
  for (byte y = 0; y < height; ++y)
  {
    for (byte x = 0; x < width; ++x)
    {
      uint16_t color = pgm_read_word(&image[y * width + x]);
      screen.drawPixel(xPos + x, yPos + y, color);
    }
  }
}

void clearImage(byte xPos, byte yPos, byte height, byte width)
{
  screen.stroke(0, 0, 0);
  screen.fill(0, 0, 0);
  screen.rect(xPos, yPos, width, height);
}