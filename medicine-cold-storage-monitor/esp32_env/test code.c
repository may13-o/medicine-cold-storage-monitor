
/***************************************************************
 * ESP32 ENVIRONMENT MONITORING SYSTEM
 *
 * Controller : ESP32 DevKit V1
 * Temperature: 7SEMI SHT40
 * RTC        : DS3231
 * Display    : 0.96" OLED (128x64 SSD1306)
 * Alert      : Buzzer
 *
 * I2C CONNECTIONS:
 * SDA = GPIO 21
 * SCL = GPIO 22
 *
 * BUZZER:
 * Signal = GPIO 25
 *
 * SHT40 I2C ADDRESS = 0x44
 * DS3231 I2C ADDRESS = 0x68
 * OLED I2C ADDRESS = 0x3C
 ***************************************************************/

#include <Arduino.h>
#include <Wire.h>

#include <SensirionI2cSht4x.h>
#include <RTClib.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------------- PIN CONFIGURATION ----------------

#define SDA_PIN       21
#define SCL_PIN       22
#define BUZZER_PIN    25

// ---------------- OLED CONFIGURATION ----------------

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_ADDRESS  0x3C

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  -1
);

// ---------------- SENSOR OBJECTS ----------------

SensirionI2cSht4x sht40;
RTC_DS3231 rtc;

// ---------------- SENSOR VARIABLES ----------------

float temperature = 0.0;
float humidity = 0.0;

bool shtOK = false;
bool rtcOK = false;
bool oledOK = false;

bool buzzerState = false;

// ---------------- TEMPERATURE THRESHOLDS ----------------

const float TEMP_ON  = 35.0;
const float TEMP_OFF = 33.0;

// ---------------- TIMING ----------------

unsigned long previousRead = 0;
unsigned long previousDisplay = 0;

const unsigned long READ_INTERVAL = 2000;

// ---------------- SHT40 ERROR HANDLING ----------------

static char errorMessage[64];
static int16_t error;

// ---------------- BUZZER CONTROL ----------------

void updateBuzzer(float temp)
{
  if (!buzzerState && temp >= TEMP_ON)
  {
    buzzerState = true;
    digitalWrite(BUZZER_PIN, HIGH);

    Serial.println("!!! HIGH TEMPERATURE ALERT !!!");
  }

  else if (buzzerState && temp <= TEMP_OFF)
  {
    buzzerState = false;
    digitalWrite(BUZZER_PIN, LOW);

    Serial.println("Temperature returned to normal.");
  }
}

// ---------------- READ SHT40 ----------------

bool readSHT40()
{
  error = sht40.measureLowestPrecision(
    temperature,
    humidity
  );

  if (error != 0)
  {
    errorToString(
      error,
      errorMessage,
      sizeof(errorMessage)
    );

    Serial.print("SHT40 reading error: ");
    Serial.println(errorMessage);

    return false;
  }

  return true;
}

// ---------------- OLED DISPLAY ----------------

void updateDisplay()
{
  if (!oledOK)
    return;

  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);

  // HEADER
  display.setTextSize(1);
  display.setCursor(0, 0);

  display.println("ESP32 MONITORING SYSTEM");

  display.drawLine(
    0, 11, 127, 11,
    SSD1306_WHITE
  );

  // DATE AND TIME
  display.setCursor(0, 15);

  if (rtcOK)
  {
    DateTime now = rtc.now();

    char dateBuffer[20];

    snprintf(
      dateBuffer,
      sizeof(dateBuffer),
      "%02d/%02d/%04d",
      now.day(),
      now.month(),
      now.year()
    );

    display.print("Date: ");
    display.println(dateBuffer);

    char timeBuffer[20];

    snprintf(
      timeBuffer,
      sizeof(timeBuffer),
      "%02d:%02d:%02d",
      now.hour(),
      now.minute(),
      now.second()
    );

    display.print("Time: ");
    display.println(timeBuffer);
  }
  else
  {
    display.println("RTC: NOT DETECTED");
  }

  display.drawLine(
    0, 34, 127, 34,
    SSD1306_WHITE
  );

  // TEMPERATURE AND HUMIDITY
  display.setCursor(0, 38);

  if (shtOK)
  {
    display.print("Temp: ");
    display.print(temperature, 1);
    display.println(" C");

    display.print("Humidity: ");
    display.print(humidity, 1);
    display.println(" %RH");
  }
  else
  {
    display.println("SHT40: READ ERROR");
  }

  // ALERT STATUS
  display.setCursor(0, 56);

  if (buzzerState)
  {
    display.print("STATUS: HIGH TEMP!");
  }
  else
  {
    display.print("STATUS: NORMAL");
  }

  display.display();
}

// ---------------- SETUP ----------------

void setup()
{
  Serial.begin(115200);

  delay(1500);

  Serial.println();
  Serial.println("================================");
  Serial.println(" ESP32 ENVIRONMENT MONITORING");
  Serial.println("================================");

  // BUZZER INITIALIZATION
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // I2C INITIALIZATION
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);

  Serial.println("I2C initialized.");

  // SHT40 INITIALIZATION
  Serial.println("Initializing SHT40...");

  sht40.begin(
    Wire,
    SHT40_I2C_ADDR_44
  );

  error = sht40.softReset();

  if (error == 0)
  {
    delay(20);

    shtOK = true;

    Serial.println("SHT40 initialized successfully!");
  }
  else
  {
    errorToString(
      error,
      errorMessage,
      sizeof(errorMessage)
    );

    Serial.print("SHT40 initialization failed: ");
    Serial.println(errorMessage);
  }

  // DS3231 INITIALIZATION
  Serial.println("Initializing DS3231...");

  rtcOK = rtc.begin();

  if (rtcOK)
  {
    Serial.println("DS3231 initialized successfully!");

    if (rtc.lostPower())
    {
      Serial.println("RTC lost power. Setting compile time.");

      rtc.adjust(
        DateTime(F(__DATE__), F(__TIME__))
      );
    }
  }
  else
  {
    Serial.println("DS3231 not detected!");
  }

  // OLED INITIALIZATION
  Serial.println("Initializing OLED...");

  oledOK = display.begin(
    SSD1306_SWITCHCAPVCC,
    OLED_ADDRESS
  );

  if (oledOK)
  {
    Serial.println("OLED initialized successfully!");

    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(15, 25);
    display.println("SYSTEM STARTING");

    display.display();
  }
  else
  {
    Serial.println("OLED initialization failed!");
  }

  delay(1500);

  Serial.println("System initialization complete.");
}

// ---------------- MAIN LOOP ----------------

void loop()
{
  unsigned long currentMillis = millis();

  if (currentMillis - previousRead >= READ_INTERVAL)
  {
    previousRead = currentMillis;

    Serial.println();
    Serial.println("--------- SENSOR DATA ---------");

    // READ RTC
    if (rtcOK)
    {
      DateTime now = rtc.now();

      Serial.print("Date: ");

      Serial.print(now.day());
      Serial.print("/");
      Serial.print(now.month());
      Serial.print("/");
      Serial.println(now.year());

      Serial.print("Time: ");

      if (now.hour() < 10) Serial.print("0");
      Serial.print(now.hour());

      Serial.print(":");

      if (now.minute() < 10) Serial.print("0");
      Serial.print(now.minute());

      Serial.print(":");

      if (now.second() < 10) Serial.print("0");
      Serial.println(now.second());
    }

    // READ SHT40
    if (shtOK && readSHT40())
    {
      Serial.print("Temperature: ");
      Serial.print(temperature, 2);
      Serial.println(" C");

      Serial.print("Humidity: ");
      Serial.print(humidity, 2);
      Serial.println(" %RH");

      // UPDATE TEMPERATURE ALERT
      updateBuzzer(temperature);
    }
    else if (shtOK)
    {
      Serial.println("SHT40 data unavailable.");
    }
    else
    {
      Serial.println("SHT40 unavailable. Check wiring.");
    }

    // UPDATE OLED
    updateDisplay();

    Serial.println("-------------------------------");
  }
}
