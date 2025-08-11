#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <AsyncTCP.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <sntp.h>
#include <time.h>
#include <WiFiClientSecure.h> // Added for Nightscout HTTPS

#include "mfactoryfont.h"  // Custom font
#include "tz_lookup.h"     // Timezone lookup, do not duplicate mapping here!
#include "days_lookup.h"   // Languages for the Days of the Week

// --- Pin Definitions ---
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4

// Default Pins
#define CLK_PIN     14  // Default CLK Pin
#define DATA_PIN    27  // Default DATA Pin
#define CS_PIN      25  // Default CS Pin

// The Parola object is now a pointer, to be initialized in setup()
// This allows us to use either default or custom pins from the config file.
MD_Parola *P = nullptr;

AsyncWebServer server(80);

// --- Global Scroll Speed Settings ---
const int GENERAL_SCROLL_SPEED = 85;  // Default: Adjust this for Weather Description and Countdown Label (e.g., 50 for faster, 200 for slower)
const int IP_SCROLL_SPEED = 115;      // Default: Adjust this for the IP Address display (slower for readability)

// WiFi and configuration globals
char ssid[32] = "";
char password[32] = "";
char openWeatherApiKey[64] = "";
char openWeatherCity[64] = "";
char openWeatherCountry[64] = "";
char weatherUnits[12] = "metric";
char timeZone[64] = "";
char language[8] = "en";
String mainDesc = "";
String detailedDesc = "";

// Timing and display settings
unsigned long clockDuration = 10000;
unsigned long weatherDuration = 5000;
int brightness = 7;
bool flipDisplay = false;
bool twelveHourToggle = false;
bool showDayOfWeek = true;
bool showHumidity = false;
bool colonBlinkEnabled = true;
char ntpServer1[64] = "pool.ntp.org";
char ntpServer2[64] = "time.nist.gov"; // Restored to original purpose
char nightscoutUrl[256] = ""; // New dedicated variable for Nightscout

// Custom Pin Configuration
bool useCustomPins = false;
int custom_clk_pin = CLK_PIN;
int custom_data_pin = DATA_PIN;
int custom_cs_pin = CS_PIN;

// Dimming
bool dimmingEnabled = false;
int dimStartHour = 18;  // 6pm default
int dimStartMinute = 0;
int dimEndHour = 8;  // 8am default
int dimEndMinute = 0;
int dimBrightness = 2;  // Dimming level (0-15)

//Countdown Globals
bool countdownEnabled = false;
time_t countdownTargetTimestamp = 0;  // Unix timestamp
char countdownLabel[64] = "";         // Label for the countdown
int countdownScrollCount = 2;         // How many times the countdown message scrolls
bool countdownScheduleEnabled = false;
int countdownStartHour = 0;
int countdownStartMinute = 0;
int countdownEndHour = 23;
int countdownEndMinute = 59;

// Effect Order
const int NUM_EFFECTS = 5;
int effectOrder[NUM_EFFECTS] = {5, 6, 7, 8, 9}; // Effects now start at mode 5
int effectOrderIndex = -1;                      // -1 means not currently in an effect cycle

// Effects Scheduling
bool effectsScheduleEnabled = false;
int effectsStartHour = 0;
int effectsStartMinute = 0;
int effectsEndHour = 23;
int effectsEndMinute = 59;

// Matrix Effect Globals
bool matrixEffectEnabled = false;
unsigned long matrixDuration = 5000; // Default 5 seconds
int matrixLoad = 4; // Default rain density
bool matrixIsFirstRun = true;
unsigned long matrixLastUpdate = 0;

// Ping-Pong Effect Globals
bool pingPongEffectEnabled = false;
unsigned long pingPongDuration = 10000; // Default 10 seconds
unsigned long pingPongLastUpdate = 0;
const int PING_PONG_SPEED = 90; // ms between animation frames (lower is faster)
const int MATRIX_WIDTH = 32;
const int MATRIX_HEIGHT = 8;
#define PADDLE_HEIGHT 3
#define BALL_SIZE 1
struct Paddle { int y, dy; };
struct Ball { int x, y, dx, dy; };
Paddle paddle1;
Paddle paddle2;
Ball ball;
bool pingPongIsFirstRun = true;

// --- Snake Game Globals ---
bool snakeEffectEnabled = false;
unsigned long snakeDuration = 15000; // Default 15 seconds
bool snakeIsFirstRun = true;
const int SNAKE_SPEED = 200; // ms between frames
unsigned long snakeLastUpdate = 0;
struct SnakeSegment { int x, y; };
SnakeSegment snake[MATRIX_WIDTH * MATRIX_HEIGHT];
int snakeLength;
int foodX, foodY;
enum Direction { UP, DOWN, LEFT, RIGHT };
Direction snakeDir;

// --- Knight Rider Effect Globals ---
bool knightRiderEffectEnabled = false;
unsigned long knightRiderDuration = 10000; // Default 10 seconds
int knightRiderSpeed = 5; // Speed for Knight Rider (1-10)
int knightRiderWidth = 8; // Width of the scanner bar
bool knightRiderIsFirstRun = true;
unsigned long knightRiderLastUpdate = 0;
int knightRiderPos = 0;
int knightRiderDir = 1;
const int KR_BAR_Y = 3; // Vertical row for the scanner

// --- EKG Effect Globals ---
bool ekgEffectEnabled = false;
unsigned long ekgDuration = 10000; // Default 10 seconds
int ekgSpeed = 5; // Speed for EKG (1-10)
bool ekgIsFirstRun = true;
unsigned long ekgLastDrawTime = 0;
int ekgCurrentPixel = 0;
int ekg_prev_yPos = -1;
bool ekgPaused = false;
unsigned long ekgPauseStartTime = 0;
const uint8_t heartBeatWaveform[] = { 4, 4, 3, 4, 5, 4, 4, 4, 3, 2, 3, 4, 4, 2, 1, 7, 5, 4, 4, 3, 4, 5, 4, 4, 4 };
const uint8_t ekgWaveformSize = sizeof(heartBeatWaveform) / sizeof(heartBeatWaveform[0]);


// State management
bool weatherCycleStarted = false;
WiFiClient client;
const byte DNS_PORT = 53;
DNSServer dnsServer;

String currentTemp = "";
String weatherDescription = "";
bool showWeatherDescription = false;
bool weatherAvailable = false;
bool weatherFetched = false;
bool weatherFetchInitiated = false;
bool isAPMode = false;
char tempSymbol = '[';
bool shouldFetchWeatherNow = false;

unsigned long lastSwitch = 0;
unsigned long lastColonBlink = 0;
int displayMode = 0;  // 0:Clock, 1:Weather, 2:WeatherDesc, 3:Countdown, 4:Nightscout, 5:Matrix, 6:PingPong, 7:Snake, 8:KnightRider, 9:EKG
int currentHumidity = -1;
bool ntpSyncSuccessful = false;

// NTP Synchronization State Machine
enum NtpState {
  NTP_IDLE,
  NTP_SYNCING,
  NTP_SUCCESS,
  NTP_FAILED
};
NtpState ntpState = NTP_IDLE;
unsigned long ntpStartTime = 0;
const int ntpTimeout = 30000;  // 30 seconds
const int maxNtpRetries = 30;
int ntpRetryCount = 0;
unsigned long lastNtpStatusPrintTime = 0;
const unsigned long ntpStatusPrintInterval = 1000;  // Print status every 1 seconds (adjust as needed)

// Non-blocking IP display globals
bool showingIp = false;
int ipDisplayCount = 0;
const int ipDisplayMax = 1;  // Display IP scroll once
String pendingIpToShow = "";

// Countdown display state
bool countdownScrolling = false;
unsigned long countdownScrollEndTime = 0;
unsigned long countdownStaticStartTime = 0;  // For last-day static display


// --- GLOBAL VARIABLES FOR IMMEDIATE COUNTDOWN FINISH ---
bool countdownFinished = false;                       // Tracks if the countdown has permanently finished
bool countdownShowFinishedMessage = false;            // Flag to indicate "TIMES UP" message is active
unsigned long countdownFinishedMessageStartTime = 0;  // Timer for the 10-second message duration
unsigned long lastFlashToggleTime = 0;                // For controlling the flashing speed
bool currentInvertState = false;                      // Current state of display inversion for flashing
static bool hourglassPlayed = false;

// Weather Description Mode handling
unsigned long descStartTime = 0;  // For static description
bool descScrolling = false;
const unsigned long descriptionDuration = 3000;    // 3s for short text
static unsigned long descScrollEndTime = 0;        // for post-scroll delay (re-used for scroll timing)
const unsigned long descriptionScrollPause = 300;  // 300ms pause after scroll

// Scroll flipped
textEffect_t getEffectiveScrollDirection(textEffect_t desiredDirection, bool isFlipped) {
  if (isFlipped) {
    // If the display is horizontally flipped, reverse the horizontal scroll direction
    if (desiredDirection == PA_SCROLL_LEFT) {
      return PA_SCROLL_RIGHT;
    } else if (desiredDirection == PA_SCROLL_RIGHT) {
       return PA_SCROLL_LEFT;
    }
  }
  return desiredDirection;
}

// -----------------------------------------------------------------------------
// Effect Order Parsing
// -----------------------------------------------------------------------------
void parseEffectOrder(String orderStr) {
  if (orderStr.length() == 0) return; // Keep default if empty or invalid
  
  int current_index = 0;
  int last_delim = -1;
  for (int i = 0; i < orderStr.length() && current_index < NUM_EFFECTS; i++) {
    if (orderStr.charAt(i) == ',') {
      effectOrder[current_index++] = orderStr.substring(last_delim + 1, i).toInt();
      last_delim = i;
    }
  }
  
  // Get the last value after the final comma
  if (current_index < NUM_EFFECTS && last_delim < (int)orderStr.length() - 1) {
    effectOrder[current_index] = orderStr.substring(last_delim + 1).toInt();
  }
}

// -----------------------------------------------------------------------------
// Configuration Load & Save
// -----------------------------------------------------------------------------
void loadConfig() {
  Serial.println(F("[CONFIG] Loading configuration..."));

  // Check if config.json exists, if not, create default
  if (!LittleFS.exists("/config.json")) {
    Serial.println(F("[CONFIG] config.json not found, creating with defaults..."));
    DynamicJsonDocument doc(2048);
    doc[F("ssid")] = "";
    doc[F("password")] = "";
    doc[F("openWeatherApiKey")] = "";
    doc[F("openWeatherCity")] = "";
    doc[F("openWeatherCountry")] = "";
    doc[F("weatherUnits")] = "metric";
    doc[F("clockDuration")] = 10000;
    doc[F("weatherDuration")] = 5000;
    doc[F("timeZone")] = "";
    doc[F("language")] = "en";
    doc[F("brightness")] = brightness;
    doc[F("flipDisplay")] = flipDisplay;
    doc[F("twelveHourToggle")] = twelveHourToggle;
    doc[F("showDayOfWeek")] = showDayOfWeek;
    doc[F("showHumidity")] = showHumidity;
    doc[F("colonBlinkEnabled")] = colonBlinkEnabled; 
    doc[F("ntpServer1")] = ntpServer1;
    doc[F("ntpServer2")] = ntpServer2;
    doc[F("nightscoutUrl")] = ""; // Add new nightscoutUrl field
    doc[F("showWeatherDescription")] = showWeatherDescription;

    // Add custom pin defaults
    doc[F("useCustomPins")] = false;
    doc[F("custom_clk_pin")] = CLK_PIN;
    doc[F("custom_data_pin")] = DATA_PIN;
    doc[F("custom_cs_pin")] = CS_PIN;

    // Add dimming defaults
    doc[F("dimmingEnabled")] = dimmingEnabled;
    doc[F("dimStartHour")] = dimStartHour;
    doc[F("dimStartMinute")] = dimStartMinute;
    doc[F("dimEndHour")] = dimEndHour;
    doc[F("dimEndMinute")] = dimEndMinute;
    doc[F("dimBrightness")] = dimBrightness;

    // Add countdown defaults
    JsonObject countdownObj = doc.createNestedObject("countdown");
    countdownObj["enabled"] = false;
    countdownObj["targetTimestamp"] = 0;
    countdownObj["label"] = "";
    countdownObj["scrollCount"] = 2;
    countdownObj["scheduleEnabled"] = false;
    countdownObj["startHour"] = 0;
    countdownObj["startMinute"] = 0;
    countdownObj["endHour"] = 23;
    countdownObj["endMinute"] = 59;
    
    // Add effect order default
    doc[F("effectOrder")] = "5,6,7,8,9";

    // Add Effects Scheduling defaults
    JsonObject effectsScheduleObj = doc.createNestedObject("effectsSchedule");
    effectsScheduleObj["enabled"] = false;
    effectsScheduleObj["startHour"] = 0;
    effectsScheduleObj["startMinute"] = 0;
    effectsScheduleObj["endHour"] = 23;
    effectsScheduleObj["endMinute"] = 59;

    // Add Matrix defaults
    doc[F("matrixEffectEnabled")] = false;
    doc[F("matrixDuration")] = 5000;
    doc[F("matrixLoad")] = 4;

    // Add Ping-Pong defaults
    doc[F("pingPongEffectEnabled")] = false;
    doc[F("pingPongDuration")] = 10000;

    // Add Snake defaults
    doc[F("snakeEffectEnabled")] = false;
    doc[F("snakeDuration")] = 15000;

    // Add Knight Rider defaults
    doc[F("knightRiderEffectEnabled")] = false;
    doc[F("knightRiderDuration")] = 10000;
    doc[F("knightRiderSpeed")] = 5;
    doc[F("knightRiderWidth")] = 8;

    // Add EKG defaults
    doc[F("ekgEffectEnabled")] = false;
    doc[F("ekgDuration")] = 10000;
    doc[F("ekgSpeed")] = 5;

    File f = LittleFS.open("/config.json", "w");
    if (f) {
      serializeJsonPretty(doc, f);
      f.close();
      Serial.println(F("[CONFIG] Default config.json created."));
    } else {
      Serial.println(F("[ERROR] Failed to create default config.json"));
    }
  }

  Serial.println(F("[CONFIG] Attempting to open config.json for reading."));
  File configFile = LittleFS.open("/config.json", "r");
  if (!configFile) {
    Serial.println(F("[ERROR] Failed to open config.json for reading. Cannot load config."));
    return;
  }

  DynamicJsonDocument doc(2048);  // Size based on ArduinoJson Assistant + buffer
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close();

  if (error) {
    Serial.print(F("[ERROR] JSON parse failed during load: "));
    Serial.println(error.f_str());
    return;
  }

  strlcpy(ssid, doc["ssid"] | "", sizeof(ssid));
  strlcpy(password, doc["password"] | "", sizeof(password));
  strlcpy(openWeatherApiKey, doc["openWeatherApiKey"] | "", sizeof(openWeatherApiKey));
  strlcpy(openWeatherCity, doc["openWeatherCity"] | "", sizeof(openWeatherCity));
  strlcpy(openWeatherCountry, doc["openWeatherCountry"] | "", sizeof(openWeatherCountry));
  strlcpy(weatherUnits, doc["weatherUnits"] | "metric", sizeof(weatherUnits));
  clockDuration = doc["clockDuration"] | 10000;
  weatherDuration = doc["weatherDuration"] | 5000;
  strlcpy(timeZone, doc["timeZone"] | "Etc/UTC", sizeof(timeZone));
  if (doc.containsKey("language")) {
    strlcpy(language, doc["language"], sizeof(language));
  } else {
    strlcpy(language, "en", sizeof(language));
    Serial.println(F("[CONFIG] 'language' key not found in config.json, defaulting to 'en'."));
  }

  brightness = doc["brightness"] | 7;
  flipDisplay = doc["flipDisplay"] | false;
  twelveHourToggle = doc["twelveHourToggle"] | false;
  showDayOfWeek = doc["showDayOfWeek"] | true;
  showHumidity = doc["showHumidity"] | false;
  colonBlinkEnabled = doc.containsKey("colonBlinkEnabled") ? doc["colonBlinkEnabled"].as<bool>() : true;

  // --- Load Custom Pin Config ---
  useCustomPins = doc["useCustomPins"] | false;
  custom_clk_pin = doc["custom_clk_pin"] | CLK_PIN;
  custom_data_pin = doc["custom_data_pin"] | DATA_PIN;
  custom_cs_pin = doc["custom_cs_pin"] | CS_PIN;

  String de = doc["dimmingEnabled"].as<String>();
  dimmingEnabled = (de == "true" || de == "on" || de == "1");

  dimStartHour = doc["dimStartHour"] | 18;
  dimStartMinute = doc["dimStartMinute"] | 0;
  dimEndHour = doc["dimEndHour"] | 8;
  dimEndMinute = doc["dimEndMinute"] | 0;
  dimBrightness = doc["dimBrightness"] | 0;

  strlcpy(ntpServer1, doc["ntpServer1"] | "pool.ntp.org", sizeof(ntpServer1));
  strlcpy(ntpServer2, doc["ntpServer2"] | "time.nist.gov", sizeof(ntpServer2));
  strlcpy(nightscoutUrl, doc["nightscoutUrl"] | "", sizeof(nightscoutUrl)); // Load the new Nightscout URL

  if (strcmp(weatherUnits, "imperial") == 0)
    tempSymbol = ']';
  else
    tempSymbol = '[';

  if (doc.containsKey("showWeatherDescription"))
    showWeatherDescription = doc["showWeatherDescription"];
  else
    showWeatherDescription = false;

  // --- COUNTDOWN CONFIG LOADING ---
  if (doc.containsKey("countdown")) {
    JsonObject countdownObj = doc["countdown"];

    countdownEnabled = countdownObj["enabled"] | false;
    countdownTargetTimestamp = countdownObj["targetTimestamp"] | 0;
    countdownScrollCount = countdownObj["scrollCount"] | 2;
    if (countdownScrollCount < 1 || countdownScrollCount > 5) {
        countdownScrollCount = 2; // Enforce bounds
    }

    countdownScheduleEnabled = countdownObj["scheduleEnabled"] | false;
    countdownStartHour = countdownObj["startHour"] | 0;
    countdownStartMinute = countdownObj["startMinute"] | 0;
    countdownEndHour = countdownObj["endHour"] | 23;
    countdownEndMinute = countdownObj["endMinute"] | 59;


    JsonVariant labelVariant = countdownObj["label"];
    if (labelVariant.isNull() || !labelVariant.is<const char *>()) {
       strcpy(countdownLabel, "");
    } else {
      const char *labelTemp = labelVariant.as<const char *>();
      size_t labelLen = strlen(labelTemp);
      if (labelLen >= sizeof(countdownLabel)) {
        Serial.println(F("[CONFIG] label from JSON too long, truncating."));
      }
      strlcpy(countdownLabel, labelTemp, sizeof(countdownLabel));
    }
    countdownFinished = false;
  } else {
    countdownEnabled = false;
    countdownTargetTimestamp = 0;
    strcpy(countdownLabel, "");
    Serial.println(F("[CONFIG] Countdown object not found, defaulting to disabled."));
    countdownFinished = false;
  }

  // --- Load Effect Order ---
  String orderStr = doc["effectOrder"] | "5,6,7,8,9";
  parseEffectOrder(orderStr);

  // --- Load Effects Scheduling ---
  if (doc.containsKey("effectsSchedule")) {
      JsonObject effectsScheduleObj = doc["effectsSchedule"];
      effectsScheduleEnabled = effectsScheduleObj["enabled"] | false;
      effectsStartHour = effectsScheduleObj["startHour"] | 0;
      effectsStartMinute = effectsScheduleObj["startMinute"] | 0;
      effectsEndHour = effectsScheduleObj["endHour"] | 23;
      effectsEndMinute = effectsScheduleObj["endMinute"] | 59;
  }
  
  // --- Load Matrix config ---
  matrixEffectEnabled = doc["matrixEffectEnabled"] | false;
  matrixDuration = doc["matrixDuration"] | 5000;
  matrixLoad = doc["matrixLoad"] | 4;

  // --- Load Ping-Pong config ---
  pingPongEffectEnabled = doc["pingPongEffectEnabled"] | false;
  pingPongDuration = doc["pingPongDuration"] | 10000;

  // --- Load Snake config ---
  snakeEffectEnabled = doc["snakeEffectEnabled"] | false;
  snakeDuration = doc["snakeDuration"] | 15000;

  // --- Load Knight Rider config ---
  knightRiderEffectEnabled = doc["knightRiderEffectEnabled"] | false;
  knightRiderDuration = doc["knightRiderDuration"] | 10000;
  knightRiderSpeed = doc["knightRiderSpeed"] | 5;
  knightRiderWidth = doc["knightRiderWidth"] | 8;

  // --- Load EKG config ---
  ekgEffectEnabled = doc["ekgEffectEnabled"] | false;
  ekgDuration = doc["ekgDuration"] | 10000;
  ekgSpeed = doc["ekgSpeed"] | 5;


  Serial.println(F("[CONFIG] Configuration loaded."));
}



// -----------------------------------------------------------------------------
// WiFi Setup
// -----------------------------------------------------------------------------
const char *DEFAULT_AP_PASSWORD = "12345678";
const char *AP_SSID = "ESPTimeCast";

void connectWiFi() {
  Serial.println(F("[WIFI] Connecting to WiFi..."));

  bool credentialsExist = (strlen(ssid) > 0);

  if (!credentialsExist) {
    Serial.println(F("[WIFI] No saved credentials. Starting AP mode directly."));
    WiFi.mode(WIFI_AP);
    WiFi.disconnect(true);
    delay(100);

    if (strlen(DEFAULT_AP_PASSWORD) < 8) {
      WiFi.softAP(AP_SSID);
      Serial.println(F("[WIFI] AP Mode started (no password, too short)."));
    } else {
      WiFi.softAP(AP_SSID, DEFAULT_AP_PASSWORD);
      Serial.println(F("[WIFI] AP Mode started."));
    }

    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    Serial.print(F("[WIFI] AP IP address: "));
    Serial.println(WiFi.softAPIP());
    isAPMode = true;

    WiFiMode_t mode = WiFi.getMode();
    Serial.printf("[WIFI] WiFi mode after setting AP: %s\n",
                  mode == WIFI_OFF ? "OFF" : mode == WIFI_STA    ? "STA ONLY"
                                             : mode == WIFI_AP     ? "AP ONLY"
                                                      : mode == WIFI_AP_STA ? "AP + STA (Error!)"
                                                                   : "UNKNOWN");

    Serial.println(F("[WIFI] AP Mode Started"));
    return;
  }

  // If credentials exist, attempt STA connection
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);

  WiFi.begin(ssid, password);
  unsigned long startAttemptTime = millis();

  const unsigned long timeout = 30000;
  unsigned long animTimer = 0;
  int animFrame = 0;
  bool animating = true;

  while (animating) {
    unsigned long now = millis();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("[WiFi] Connected: " + WiFi.localIP().toString());
      isAPMode = false;

      WiFiMode_t mode = WiFi.getMode();
      Serial.printf("[WIFI] WiFi mode after STA connection: %s\n",
                    mode == WIFI_OFF ? "OFF" : mode == WIFI_STA    ? "STA ONLY"
                                              : mode == WIFI_AP     ? "AP ONLY"
                                                      : mode == WIFI_AP_STA ? "AP + STA (Error!)"
                                                                    : "UNKNOWN");

      // --- IP Display initiation ---
      pendingIpToShow = WiFi.localIP().toString();
      showingIp = true;
      ipDisplayCount = 0;  // Reset count for IP display
      P->displayClear();
      P->setCharSpacing(1);  // Set spacing for IP scroll
      textEffect_t actualScrollDirection = getEffectiveScrollDirection(PA_SCROLL_LEFT, flipDisplay);
      P->displayScroll(pendingIpToShow.c_str(), PA_CENTER, actualScrollDirection, IP_SCROLL_SPEED);
      // --- END IP Display initiation ---

      animating = false;  // Exit the connection loop
      break;
    } else if (now - startAttemptTime >= timeout) {
      Serial.println(F("[WiFi] Failed. Starting AP mode..."));
      WiFi.mode(WIFI_AP);
      WiFi.softAP(AP_SSID, DEFAULT_AP_PASSWORD);
      Serial.print(F("[WiFi] AP IP address: "));
      Serial.println(WiFi.softAPIP());
      dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
      isAPMode = true;

      auto mode = WiFi.getMode();
      Serial.printf("[WIFI] WiFi mode after STA failure and setting AP: %s\n",
                    mode == WIFI_OFF ? "OFF" : mode == WIFI_STA    ? "STA ONLY"
                                               : mode == WIFI_AP     ? "AP ONLY"
                                                     : mode == WIFI_AP_STA ? "AP + STA (Error!)"
                                                                   : "UNKNOWN");

      animating = false;
      Serial.println(F("[WIFI] AP Mode Started"));
      break;
    }
    if (now - animTimer > 750) {
      animTimer = now;
      P->setTextAlignment(PA_CENTER);
      switch (animFrame % 3) {
        case 0: P->print(F("# ©")); break;
        case 1: P->print(F("# ª")); break;
        case 2: P->print(F("# «")); break;
      }
      animFrame++;
    }
    delay(1);
  }
}

// -----------------------------------------------------------------------------
// Time / NTP Functions
// -----------------------------------------------------------------------------
void setupTime() {
  // sntp_stop();
  if (!isAPMode) {
    Serial.println(F("[TIME] Starting NTP sync"));
  }
  configTime(0, 0, ntpServer1, ntpServer2);
  setenv("TZ", ianaToPosix(timeZone), 1);
  tzset();
  ntpState = NTP_SYNCING;
  ntpStartTime = millis();
  ntpRetryCount = 0;
  ntpSyncSuccessful = false;
}

// -----------------------------------------------------------------------------
// Utility
// -----------------------------------------------------------------------------
// Helper function to check if the current time is within a given schedule
bool isTimeWithinSchedule(int startHour, int startMinute, int endHour, int endMinute) {
    if (!ntpSyncSuccessful) return false; // Cannot check schedule without time

    time_t now_time = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now_time, &timeinfo);
    int curTotalMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
    int startTotalMinutes = startHour * 60 + startMinute;
    int endTotalMinutes = endHour * 60 + endMinute;

    if (startTotalMinutes <= endTotalMinutes) {
        // Non-overnight schedule (e.g., 08:00 to 18:00)
        return (curTotalMinutes >= startTotalMinutes && curTotalMinutes <= endTotalMinutes);
    } else {
        // Overnight schedule (e.g., 22:00 to 06:00)
        return (curTotalMinutes >= startTotalMinutes || curTotalMinutes <= endTotalMinutes);
    }
}


void printConfigToSerial() {
  Serial.println(F("========= Loaded Configuration ========="));
  Serial.print(F("WiFi SSID: "));
  Serial.println(ssid);
  Serial.print(F("WiFi Password: "));
  Serial.println(password);
  Serial.print(F("OpenWeather City: "));
  Serial.println(openWeatherCity);
  Serial.print(F("OpenWeather Country: "));
  Serial.println(openWeatherCountry);
  Serial.print(F("OpenWeather API Key: "));
  Serial.println(openWeatherApiKey);
  Serial.print(F("Temperature Unit: "));
  Serial.println(weatherUnits);
  Serial.print(F("Clock duration: "));
  Serial.println(clockDuration);
  Serial.print(F("Weather duration: "));
  Serial.println(weatherDuration);
  Serial.print(F("TimeZone (IANA): "));
  Serial.println(timeZone);
  Serial.print(F("Days of the Week/Weather description language: "));
  Serial.println(language);
  Serial.print(F("Brightness: "));
  Serial.println(brightness);
  Serial.print(F("Flip Display: "));
  Serial.println(flipDisplay ? "Yes" : "No");
  Serial.print(F("Show 12h Clock: "));
  Serial.println(twelveHourToggle ? "Yes" : "No");
  Serial.print(F("Show Day of the Week: "));
  Serial.println(showDayOfWeek ? "Yes" : "No");
  Serial.print(F("Show Weather Description: "));
  Serial.println(showWeatherDescription ? "Yes" : "No");
  Serial.print(F("Show Humidity: "));
  Serial.println(showHumidity ? "Yes" : "No");
  Serial.print(F("Blinking colon: "));
  Serial.println(colonBlinkEnabled ? "Yes" : "No");
  Serial.print(F("NTP Server 1: "));
  Serial.println(ntpServer1);
  Serial.print(F("NTP Server 2: "));
  Serial.println(ntpServer2);
  Serial.print(F("Nightscout URL: "));
  Serial.println(nightscoutUrl);
  Serial.print(F("Use Custom Pins: "));
  Serial.println(useCustomPins ? "Yes" : "No");
  if(useCustomPins) {
    Serial.printf("  - CLK: %d, DATA: %d, CS: %d\n", custom_clk_pin, custom_data_pin, custom_cs_pin);
  }
  Serial.print(F("Dimming Enabled: "));
  Serial.println(dimmingEnabled);
  Serial.print(F("Dimming Start Hour: "));
  Serial.println(dimStartHour);
  Serial.print(F("Dimming Start Minute: "));
  Serial.println(dimStartMinute);
  Serial.print(F("Dimming End Hour: "));
  Serial.println(dimEndHour);
  Serial.print(F("Dimming End Minute: "));
  Serial.println(dimEndMinute);
  Serial.print(F("Dimming Brightness: "));
  Serial.println(dimBrightness);
  Serial.print(F("Countdown Enabled: "));
  Serial.println(countdownEnabled ? "Yes" : "No");
  Serial.print(F("Countdown Target Timestamp: "));
  Serial.println(countdownTargetTimestamp);
  Serial.print(F("Countdown Label: "));
  Serial.println(countdownLabel);
  Serial.print(F("Countdown Scroll Count: "));
  Serial.println(countdownScrollCount);
  Serial.print(F("Countdown Schedule Enabled: "));
  Serial.println(countdownScheduleEnabled ? "Yes" : "No");
  Serial.printf("Countdown Schedule Time: %02d:%02d to %02d:%02d\n", countdownStartHour, countdownStartMinute, countdownEndHour, countdownEndMinute);

  Serial.print(F("Effect Order: "));
  for(int i = 0; i < NUM_EFFECTS; i++) {
    Serial.print(effectOrder[i]);
    if (i < NUM_EFFECTS - 1) {
      Serial.print(F(", "));
    }
  }
  Serial.println();

  Serial.print(F("Effects Schedule Enabled: "));
  Serial.println(effectsScheduleEnabled ? "Yes" : "No");
  Serial.printf("Effects Schedule Time: %02d:%02d to %02d:%02d\n", effectsStartHour, effectsStartMinute, effectsEndHour, effectsEndMinute);

  // --- Print Matrix config ---
  Serial.print(F("Matrix Effect Enabled: "));
  Serial.println(matrixEffectEnabled ? "Yes" : "No");
  Serial.print(F("Matrix Duration: "));
  Serial.println(matrixDuration);
  Serial.print(F("Matrix Load: "));
  Serial.println(matrixLoad);
  // --- Print Ping-Pong config ---
  Serial.print(F("Ping-Pong Effect Enabled: "));
  Serial.println(pingPongEffectEnabled ? "Yes" : "No");
  Serial.print(F("Ping-Pong Duration: "));
  Serial.println(pingPongDuration);
  // --- Print Snake config ---
  Serial.print(F("Snake Effect Enabled: "));
  Serial.println(snakeEffectEnabled ? "Yes" : "No");
  Serial.print(F("Snake Duration: "));
  Serial.println(snakeDuration);
  // --- Print Knight Rider config ---
  Serial.print(F("Knight Rider Effect Enabled: "));
  Serial.println(knightRiderEffectEnabled ? "Yes" : "No");
  Serial.print(F("Knight Rider Duration: "));
  Serial.println(knightRiderDuration);
  Serial.print(F("Knight Rider Speed: "));
  Serial.println(knightRiderSpeed);
  Serial.print(F("Knight Rider Width: "));
  Serial.println(knightRiderWidth);
  // --- Print EKG config ---
  Serial.print(F("EKG Effect Enabled: "));
  Serial.println(ekgEffectEnabled ? "Yes" : "No");
  Serial.print(F("EKG Duration: "));
  Serial.println(ekgDuration);
  Serial.print(F("EKG Speed: "));
  Serial.println(ekgSpeed);

  Serial.println(F("========================================"));
  Serial.println();
}

// -----------------------------------------------------------------------------
// Web Server and Captive Portal
// -----------------------------------------------------------------------------
void handleCaptivePortal(AsyncWebServerRequest *request);

void setupWebServer() {
  Serial.println(F("[WEBSERVER] Setting up web server..."));

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println(F("[WEBSERVER] Request: /"));
    request->send(LittleFS, "/index.html", "text/html");
  });

  server.on("/config.json", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println(F("[WEBSERVER] Request: /config.json"));
    File f = LittleFS.open("/config.json", "r");
    if (!f) {
      Serial.println(F("[WEBSERVER] Error opening /config.json"));
      request->send(500, "application/json", "{\"error\":\"Failed to open config.json\"}");
      return;
    }
    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
      Serial.print(F("[WEBSERVER] Error parsing /config.json: "));
      Serial.println(err.f_str());
      request->send(500, "application/json", "{\"error\":\"Failed to parse config.json\"}");
      return;
    }
    doc[F("mode")] = isAPMode ? "ap" : "sta";
    // Add nightscoutUrl to the response explicitly to ensure the frontend gets it.
    doc[F("nightscoutUrl")] = nightscoutUrl; 
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
    Serial.println(F("[WEBSERVER] Request: /save"));
    DynamicJsonDocument doc(2048);

    File configFile = LittleFS.open("/config.json", "r");
    if (configFile) {
      Serial.println(F("[WEBSERVER] Existing config.json found, loading for update..."));
      DeserializationError err = deserializeJson(doc, configFile);
      configFile.close();
      if (err) {
        Serial.print(F("[WEBSERVER] Error parsing existing config.json: "));
        Serial.println(err.f_str());
      }
    } else {
      Serial.println(F("[WEBSERVER] config.json not found, starting with empty doc for save."));
    }

    for (int i = 0; i < request->params(); i++) {
      const AsyncWebParameter *p = request->getParam(i);
      String n = p->name();
      String v = p->value();

      if (n == "brightness") doc[n] = v.toInt();
      else if (n == "clockDuration") doc[n] = v.toInt();
      else if (n == "weatherDuration") doc[n] = v.toInt();
      else if (n == "matrixDuration") doc[n] = v.toInt();
      else if (n == "matrixLoad") doc[n] = v.toInt();
      else if (n == "pingPongDuration") doc[n] = v.toInt();
      else if (n == "snakeDuration") doc[n] = v.toInt();
      else if (n == "knightRiderDuration") doc[n] = v.toInt();
      else if (n == "knightRiderSpeed") doc[n] = v.toInt();
      else if (n == "knightRiderWidth") doc[n] = v.toInt();
      else if (n == "ekgDuration") doc[n] = v.toInt();
      else if (n == "ekgSpeed") doc[n] = v.toInt();
      else if (n == "flipDisplay") doc[n] = (v == "true" || v == "on" || v == "1");
      else if (n == "twelveHourToggle") doc[n] = (v == "true" || v == "on" || v == "1");
      else if (n == "showDayOfWeek") doc[n] = (v == "true" || v == "on" || v == "1");
      else if (n == "showHumidity") doc[n] = (v == "true" || v == "on" || v == "1");
      else if (n == "colonBlinkEnabled") doc[n] = (v == "true" || v == "on" || v == "1");
      else if (n == "dimStartHour") doc[n] = v.toInt();
      else if (n == "dimStartMinute") doc[n] = v.toInt();
      else if (n == "dimEndHour") doc[n] = v.toInt();
      else if (n == "dimEndMinute") doc[n] = v.toInt();
      else if (n == "dimBrightness") doc[n] = v.toInt();
      else if (n == "showWeatherDescription") doc[n] = (v == "true" || v == "on" || v == "1");
      else if (n == "dimmingEnabled") doc[n] = (v == "true" || v == "on" || v == "1");
      else if (n == "useCustomPins") doc[n] = (v == "true" || v == "on" || v == "1");
      else if (n == "custom_clk_pin") doc[n] = v.toInt();
      else if (n == "custom_data_pin") doc[n] = v.toInt();
      else if (n == "custom_cs_pin") doc[n] = v.toInt();
      else if (n == "matrixEffectEnabled") doc[n] = (v == "true" || v == "on" || v == "1");
      else if (n == "pingPongEffectEnabled") doc[n] = (v == "true" || v == "on" || v == "1");
      else if (n == "snakeEffectEnabled") doc[n] = (v == "true" || v == "on" || v == "1");
      else if (n == "knightRiderEffectEnabled") doc[n] = (v == "true" || v == "on" || v == "1");
      else if (n == "ekgEffectEnabled") doc[n] = (v == "true" || v == "on" || v == "1");
      else if (n == "weatherUnits") doc[n] = v;
      else {
        // Skip countdown and schedule params, handle them separately
        if (n.startsWith("countdown") || n.startsWith("effectsSchedule")) continue;
        doc[n] = v;
      }
    }

    // --- COUNTDOWN OBJECT HANDLING ---
    bool newCountdownEnabled = (request->hasParam("countdownEnabled", true) && (request->getParam("countdownEnabled", true)->value() == "true" || request->getParam("countdownEnabled", true)->value() == "on" || request->getParam("countdownEnabled", true)->value() == "1"));
    String countdownDateStr = request->hasParam("countdownDate", true) ? request->getParam("countdownDate", true)->value() : "";
    String countdownTimeStr = request->hasParam("countdownTime", true) ? request->getParam("countdownTime", true)->value() : "";
    String countdownLabelStr = request->hasParam("countdownLabel", true) ? request->getParam("countdownLabel", true)->value() : "";
    int newCountdownScrollCount = request->hasParam("countdownScrollCount", true) ? request->getParam("countdownScrollCount", true)->value().toInt() : 2;
    if (newCountdownScrollCount < 1 || newCountdownScrollCount > 5) newCountdownScrollCount = 2;

    time_t newTargetTimestamp = 0;
    if (newCountdownEnabled && countdownDateStr.length() > 0 && countdownTimeStr.length() > 0) {
      int year = countdownDateStr.substring(0, 4).toInt();
      int month = countdownDateStr.substring(5, 7).toInt();
      int day = countdownDateStr.substring(8, 10).toInt();
      int hour = countdownTimeStr.substring(0, 2).toInt();
      int minute = countdownTimeStr.substring(3, 5).toInt();

      struct tm tm;
      tm.tm_year = year - 1900;
      tm.tm_mon = month - 1;
      tm.tm_mday = day;
      tm.tm_hour = hour;
      tm.tm_min = minute;
      tm.tm_sec = 0;
      tm.tm_isdst = -1;

      newTargetTimestamp = mktime(&tm);
      if (newTargetTimestamp == (time_t)-1) {
        Serial.println("[SAVE] Error converting countdown date/time to timestamp.");
        newTargetTimestamp = 0;
      } else {
        Serial.printf("[SAVE] Converted countdown target: %s -> %lu\n", countdownDateStr.c_str(), newTargetTimestamp);
      }
    }

    JsonObject countdownObj = doc.createNestedObject("countdown");
    countdownObj["enabled"] = newCountdownEnabled;
    countdownObj["targetTimestamp"] = newTargetTimestamp;
    countdownObj["label"] = countdownLabelStr;
    countdownObj["scrollCount"] = newCountdownScrollCount;
    countdownObj["scheduleEnabled"] = (request->hasParam("countdownScheduleEnabled", true) && request->getParam("countdownScheduleEnabled", true)->value() == "true");
    String cdStartTimeStr = request->hasParam("countdownStartTime", true) ? request->getParam("countdownStartTime", true)->value() : "00:00";
    String cdEndTimeStr = request->hasParam("countdownEndTime", true) ? request->getParam("countdownEndTime", true)->value() : "23:59";
    countdownObj["startHour"] = cdStartTimeStr.substring(0, 2).toInt();
    countdownObj["startMinute"] = cdStartTimeStr.substring(3, 5).toInt();
    countdownObj["endHour"] = cdEndTimeStr.substring(0, 2).toInt();
    countdownObj["endMinute"] = cdEndTimeStr.substring(3, 5).toInt();

    // --- EFFECTS SCHEDULE OBJECT HANDLING ---
    JsonObject effectsScheduleObj = doc.createNestedObject("effectsSchedule");
    effectsScheduleObj["enabled"] = (request->hasParam("effectsScheduleEnabled", true) && request->getParam("effectsScheduleEnabled", true)->value() == "true");
    String effectsStartTimeStr = request->hasParam("effectsStartTime", true) ? request->getParam("effectsStartTime", true)->value() : "00:00";
    String effectsEndTimeStr = request->hasParam("effectsEndTime", true) ? request->getParam("effectsEndTime", true)->value() : "23:59";
    effectsScheduleObj["startHour"] = effectsStartTimeStr.substring(0, 2).toInt();
    effectsScheduleObj["startMinute"] = effectsStartTimeStr.substring(3, 5).toInt();
    effectsScheduleObj["endHour"] = effectsEndTimeStr.substring(0, 2).toInt();
    effectsScheduleObj["endMinute"] = effectsEndTimeStr.substring(3, 5).toInt();


    size_t total = LittleFS.totalBytes();
    size_t used  = LittleFS.usedBytes();
    Serial.printf("[SAVE] LittleFS total bytes: %llu, used bytes: %llu\n", LittleFS.totalBytes(), LittleFS.usedBytes());

    if (LittleFS.exists("/config.json")) {
      Serial.println(F("[SAVE] Renaming /config.json to /config.bak"));
      LittleFS.rename("/config.json", "/config.bak");
    }
    File f = LittleFS.open("/config.json", "w");
    if (!f) {
      Serial.println(F("[SAVE] ERROR: Failed to open /config.json for writing!"));
      DynamicJsonDocument errorDoc(256);
      errorDoc[F("error")] = "Failed to write config file.";
      String response;
      serializeJson(errorDoc, response);
      request->send(500, "application/json", response);
      return;
    }

    size_t bytesWritten = serializeJson(doc, f);
    Serial.printf("[SAVE] Bytes written to /config.json: %u\n", bytesWritten);
    f.close();
    Serial.println(F("[SAVE] /config.json file closed."));

    File verify = LittleFS.open("/config.json", "r");
    if (!verify) {
      Serial.println(F("[SAVE] ERROR: Failed to open /config.json for reading during verification!"));
      DynamicJsonDocument errorDoc(256);
      errorDoc[F("error")] = "Verification failed: Could not re-open config file.";
      String response;
      serializeJson(errorDoc, response);
      request->send(500, "application/json", response);
      return;
    }

    while (verify.available()) {
      verify.read();
    }
    verify.seek(0);

    DynamicJsonDocument test(2048);
    DeserializationError err = deserializeJson(test, verify);
    verify.close();

    if (err) {
      Serial.print(F("[SAVE] Config corrupted after save: "));
      Serial.println(err.f_str());
      DynamicJsonDocument errorDoc(256);
      errorDoc[F("error")] = String("Config corrupted. Reboot cancelled. Error: ") + err.f_str();
      String response;
      serializeJson(errorDoc, response);
      request->send(500, "application/json", response);
      return;
    }

    Serial.println(F("[SAVE] Config verification successful."));
    DynamicJsonDocument okDoc(128);
    okDoc[F("message")] = "Saved successfully. Rebooting...";
    String response;
    serializeJson(okDoc, response);
    request->send(200, "application/json", response);
    Serial.println(F("[WEBSERVER] Sending success response and scheduling reboot..."));

    request->onDisconnect([]() {
      Serial.println(F("[WEBSERVER] Client disconnected, rebooting ESP..."));
      ESP.restart();
    });
  });

  server.on("/restore", HTTP_POST, [](AsyncWebServerRequest *request) {
    Serial.println(F("[WEBSERVER] Request: /restore"));
    if (LittleFS.exists("/config.bak")) {
      File src = LittleFS.open("/config.bak", "r");
      if (!src) {
        Serial.println(F("[WEBSERVER] Failed to open /config.bak"));
        DynamicJsonDocument errorDoc(128);
        errorDoc[F("error")] = "Failed to open backup file.";
        String response;
        serializeJson(errorDoc, response);
        request->send(500, "application/json", response);
        return;
      }
      File dst = LittleFS.open("/config.json", "w");
      if (!dst) {
        src.close();
        Serial.println(F("[WEBSERVER] Failed to open /config.json for writing"));
        DynamicJsonDocument errorDoc(128);
        errorDoc[F("error")] = "Failed to open config for writing.";
        String response;
        serializeJson(errorDoc, response);
        request->send(500, "application/json", response);
        return;
      }

      while (src.available()) {
        dst.write(src.read());
      }
      src.close();
      dst.close();

      DynamicJsonDocument okDoc(128);
      okDoc[F("message")] = "✅ Backup restored! Device will now reboot.";
      String response;
      serializeJson(okDoc, response);
      request->send(200, "application/json", response);
      request->onDisconnect([]() {
        Serial.println(F("[WEBSERVER] Rebooting after restore..."));
        ESP.restart();
      });

    } else {
      Serial.println(F("[WEBSERVER] No backup found"));
      DynamicJsonDocument errorDoc(128);
      errorDoc[F("error")] = "No backup found.";
      String response;
      serializeJson(errorDoc, response);
      request->send(404, "application/json", response);
    }
  });

  server.on("/ap_status", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.print(F("[WEBSERVER] Request: /ap_status. isAPMode = "));
    Serial.println(isAPMode);
    String json = "{\"isAP\": ";
    json += (isAPMode) ? "true" : "false";
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/set_brightness", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("value", true)) {
      request->send(400, "application/json", "{\"error\":\"Missing value\"}");
      return;
    }
    int newBrightness = request->getParam("value", true)->value().toInt();
    if (newBrightness < 0) newBrightness = 0;
    if (newBrightness > 15) newBrightness = 15;
    brightness = newBrightness;
    P->setIntensity(brightness);
    Serial.printf("[WEBSERVER] Set brightness to %d\n", brightness);
    request->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/set_flip", HTTP_POST, [](AsyncWebServerRequest *request) {
    bool flip = false;
    if (request->hasParam("value", true)) {
      String v = request->getParam("value", true)->value();
      flip = (v == "1" || v == "true" || v == "on");
    }
    flipDisplay = flip;
    P->setZoneEffect(0, flipDisplay, PA_FLIP_UD);
    P->setZoneEffect(0, flipDisplay, PA_FLIP_LR);
    Serial.printf("[WEBSERVER] Set flipDisplay to %d\n", flipDisplay);
    request->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/set_twelvehour", HTTP_POST, [](AsyncWebServerRequest *request) {
    bool twelveHour = false;
    if (request->hasParam("value", true)) {
      String v = request->getParam("value", true)->value();
      twelveHour = (v == "1" || v == "true" || v == "on");
    }
    twelveHourToggle = twelveHour;
    Serial.printf("[WEBSERVER] Set twelveHourToggle to %d\n", twelveHourToggle);
    request->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/set_dayofweek", HTTP_POST, [](AsyncWebServerRequest *request) {
    bool showDay = false;
    if (request->hasParam("value", true)) {
      String v = request->getParam("value", true)->value();
      showDay = (v == "1" || v == "true" || v == "on");
    }
    showDayOfWeek = showDay;
    Serial.printf("[WEBSERVER] Set showDayOfWeek to %d\n", showDayOfWeek);
    request->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/set_humidity", HTTP_POST, [](AsyncWebServerRequest *request) {
    bool showHumidityNow = false;
    if (request->hasParam("value", true)) {
      String v = request->getParam("value", true)->value();
      showHumidityNow = (v == "1" || v == "true" || v == "on");
    }
    showHumidity = showHumidityNow;
    Serial.printf("[WEBSERVER] Set showHumidity to %d\n", showHumidity);
    request->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/set_colon_blink", HTTP_POST, [](AsyncWebServerRequest *request) {
    bool enableBlink = false;
    if (request->hasParam("value", true)) {
      String v = request->getParam("value", true)->value();
      enableBlink = (v == "1" || v == "true" || v == "on");
    }
    colonBlinkEnabled = enableBlink;
    Serial.printf("[WEBSERVER] Set colonBlinkEnabled to %d\n", colonBlinkEnabled);
    request->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/set_language", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("value", true)) {
      request->send(400, "application/json", "{\"error\":\"Missing value\"}");
      return;
    }

    String lang = request->getParam("value", true)->value();
    lang.trim();         // Remove whitespace/newlines
    lang.toLowerCase();  // Normalize to lowercase

    strlcpy(language, lang.c_str(), sizeof(language));              // Safe copy to char[]
    Serial.printf("[WEBSERVER] Set language to '%s'\n", language);  // Use quotes for debug

    shouldFetchWeatherNow = true;

    request->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/set_weatherdesc", HTTP_POST, [](AsyncWebServerRequest *request) {
    bool showDesc = false;
    if (request->hasParam("value", true)) {
      String v = request->getParam("value", true)->value();
      showDesc = (v == "1" || v == "true" || v == "on");
    }

    if (showWeatherDescription == true && showDesc == false) {
      Serial.println(F("[WEBSERVER] showWeatherDescription toggled OFF. Checking display mode..."));
      if (displayMode == 2) {
        Serial.println(F("[WEBSERVER] Currently in Weather Description mode. Forcing mode advance/cleanup."));
        advanceDisplayMode();
      }
    }

    showWeatherDescription = showDesc;
    Serial.printf("[WEBSERVER] Set Show Weather Description to %d\n", showWeatherDescription);
    request->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/set_units", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("value", true)) {
      String v = request->getParam("value", true)->value();
      if (v == "1" || v == "true" || v == "on") {
        strcpy(weatherUnits, "imperial");
        tempSymbol = ']';
      } else {
        strcpy(weatherUnits, "metric");
        tempSymbol = '[';
      }
      Serial.printf("[WEBSERVER] Set weatherUnits to %s\n", weatherUnits);
      shouldFetchWeatherNow = true;
      request->send(200, "application/json", "{\"ok\":true}");
    } else {
      request->send(400, "application/json", "{\"error\":\"Missing value parameter\"}");
    }
  });

  server.on("/set_countdown_enabled", HTTP_POST, [](AsyncWebServerRequest *request) {
    bool enableCountdownNow = false;
    if (request->hasParam("value", true)) {
      String v = request->getParam("value", true)->value();
      enableCountdownNow = (v == "1" || v == "true" || v == "on");
    }

    if (countdownEnabled == enableCountdownNow) {
      Serial.println(F("[WEBSERVER] Countdown enable state unchanged, ignoring."));
      request->send(200, "application/json", "{\"ok\":true}");
      return;
    }

    if (countdownEnabled == true && enableCountdownNow == false) {
      Serial.println(F("[WEBSERVER] Countdown toggled OFF. Checking display mode..."));
      if (displayMode == 3) {
        Serial.println(F("[WEBSERVER] Currently in Countdown mode. Forcing mode advance/cleanup."));
        advanceDisplayMode();
      }
    }

    countdownEnabled = enableCountdownNow;
    Serial.printf("[WEBSERVER] Set Countdown Enabled to %d\n", countdownEnabled);
    request->send(200, "application/json", "{\"ok\":true}");
  });



  server.begin();
  Serial.println(F("[WEBSERVER] Web server started"));
}

void handleCaptivePortal(AsyncWebServerRequest *request) {
  Serial.print(F("[WEBSERVER] Captive Portal Redirecting: "));
  Serial.println(request->url());
  request->redirect(String("http://") + WiFi.softAPIP().toString() + "/");
}



String normalizeWeatherDescription(String str) {
  // Serbian Cyrillic → Latin
  str.replace("а", "a");
  str.replace("б", "b");
  str.replace("в", "v");
  str.replace("г", "g");
  str.replace("д", "d");
  str.replace("ђ", "dj");
  str.replace("е", "e");
  str.replace("ж", "z");
  str.replace("з", "z");
  str.replace("и", "i");
  str.replace("ј", "j");
  str.replace("к", "k");
  str.replace("л", "l");
  str.replace("љ", "lj");
  str.replace("м", "m");
  str.replace("н", "n");
  str.replace("њ", "nj");
  str.replace("о", "o");
  str.replace("п", "p");
  str.replace("р", "r");
  str.replace("с", "s");
  str.replace("т", "t");
  str.replace("ћ", "c");
  str.replace("у", "u");
  str.replace("ф", "f");
  str.replace("х", "h");
  str.replace("ц", "c");
  str.replace("ч", "c");
  str.replace("џ", "dz");
  str.replace("ш", "s");

  // Latin diacritics → ASCII
  str.replace("å", "a");
  str.replace("ä", "a");
  str.replace("à", "a");
  str.replace("á", "a");
  str.replace("â", "a");
  str.replace("ã", "a");
  str.replace("ā", "a");
  str.replace("ă", "a");
  str.replace("ą", "a");

  str.replace("æ", "ae");

  str.replace("ç", "c");
  str.replace("č", "c");
  str.replace("ć", "c");

  str.replace("ď", "d");

  str.replace("é", "e");
  str.replace("è", "e");
  str.replace("ê", "e");
  str.replace("ë", "e");
  str.replace("ē", "e");
  str.replace("ė", "e");
  str.replace("ę", "e");

  str.replace("ğ", "g");
  str.replace("ģ", "g");

  str.replace("ĥ", "h");

  str.replace("í", "i");
  str.replace("ì", "i");
  str.replace("î", "i");
  str.replace("ï", "i");
  str.replace("ī", "i");
  str.replace("į", "i");

  str.replace("ĵ", "j");

  str.replace("ķ", "k");

  str.replace("ľ", "l");
  str.replace("ł", "l");

  str.replace("ñ", "n");
  str.replace("ń", "n");
  str.replace("ņ", "n");

  str.replace("ó", "o");
  str.replace("ò", "o");
  str.replace("ô", "o");
  str.replace("ö", "o");
  str.replace("õ", "o");
  str.replace("ø", "o");
  str.replace("ō", "o");
  str.replace("ő", "o");

  str.replace("œ", "oe");

  str.replace("ŕ", "r");

  str.replace("ś", "s");
  str.replace("š", "s");
  str.replace("ș", "s");
  str.replace("ŝ", "s");

  str.replace("ß", "ss");

  str.replace("ť", "t");
  str.replace("ț", "t");

  str.replace("ú", "u");
  str.replace("ù", "u");
  str.replace("û", "u");
  str.replace("ü", "u");
  str.replace("ū", "u");
  str.replace("ů", "u");
  str.replace("ű", "u");

  str.replace("ŵ", "w");

  str.replace("ý", "y");
  str.replace("ÿ", "y");
  str.replace("ŷ", "y");

  str.replace("ž", "z");
  str.replace("ź", "z");
  str.replace("ż", "z");

  str.toUpperCase();

  String result = "";
  for (unsigned int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if ((c >= 'A' && c <= 'Z') || c == ' ') {
      result += c;
    }
  }
  return result;
}


bool isNumber(const char *str) {
  for (int i = 0; str[i]; i++) {
    if (!isdigit(str[i]) && str[i] != '.' && str[i] != '-') return false;
  }
  return true;
}

bool isFiveDigitZip(const char *str) {
  if (strlen(str) != 5) return false;
  for (int i = 0; i < 5; i++) {
    if (!isdigit(str[i])) return false;
  }
  return true;
}



// -----------------------------------------------------------------------------
// Weather Fetching and API settings
// -----------------------------------------------------------------------------
String buildWeatherURL() {
  String base = "http://api.openweathermap.org/data/2.5/weather?";

  float lat = atof(openWeatherCity);
  float lon = atof(openWeatherCountry);

  bool latValid = isNumber(openWeatherCity) && isNumber(openWeatherCountry) && lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0;

  if (latValid) {
    base += "lat=" + String(lat, 8) + "&lon=" + String(lon, 8);
  } else if (isFiveDigitZip(openWeatherCity) && String(openWeatherCountry).equalsIgnoreCase("US")) {
    base += "zip=" + String(openWeatherCity) + "," + String(openWeatherCountry);
  } else {
    base += "q=" + String(openWeatherCity) + "," + String(openWeatherCountry);
  }

  base += "&appid=" + String(openWeatherApiKey);
  base += "&units=" + String(weatherUnits);

  String langForAPI = String(language);

  if (langForAPI == "eo" || langForAPI == "sw" || langForAPI == "ja") {
    langForAPI = "en";
  }
  base += "&lang=" + langForAPI;

  return base;
}



void fetchWeather() {
  Serial.println(F("[WEATHER] Fetching weather data..."));
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[WEATHER] Skipped: WiFi not connected"));
    weatherAvailable = false;
    weatherFetched = false;
    return;
  }
  if (!openWeatherApiKey || strlen(openWeatherApiKey) != 32) {
    Serial.println(F("[WEATHER] Skipped: Invalid API key (must be exactly 32 characters)"));
    weatherAvailable = false;
    weatherFetched = false;
    return;
  }
  if (!(strlen(openWeatherCity) > 0 && strlen(openWeatherCountry) > 0)) {
    Serial.println(F("[WEATHER] Skipped: City or Country is empty."));
    weatherAvailable = false;
    return;
  }

  Serial.println(F("[WEATHER] Connecting to OpenWeatherMap..."));
  String url = buildWeatherURL();
  Serial.print(F("[WEATHER] URL: "));
  Serial.println(url);

  HTTPClient http;
  http.begin(url); 

  http.setTimeout(10000);

  Serial.println(F("[WEATHER] Sending GET request..."));
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    Serial.println(F("[WEATHER] HTTP 200 OK. Reading payload..."));

    String payload = http.getString();
    Serial.println(F("[WEATHER] Response received."));

    DynamicJsonDocument doc(1536);
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.print(F("[WEATHER] JSON parse error: "));
      Serial.println(error.f_str());
      weatherAvailable = false;
      return;
    }

    if (doc.containsKey(F("main")) && doc[F("main")].containsKey(F("temp"))) {
      float temp = doc[F("main")][F("temp")];
      currentTemp = String((int)round(temp)) + "º";
      Serial.printf("[WEATHER] Temp: %s\n", currentTemp.c_str());
      weatherAvailable = true;
    } else {
      Serial.println(F("[WEATHER] Temperature not found in JSON payload"));
      weatherAvailable = false;
      return;
    }

    if (doc.containsKey(F("main")) && doc[F("main")].containsKey(F("humidity"))) {
      currentHumidity = doc[F("main")][F("humidity")];
      Serial.printf("[WEATHER] Humidity: %d%%\n", currentHumidity);
    } else {
      currentHumidity = -1;
    }

    if (doc.containsKey(F("weather")) && doc[F("weather")].is<JsonArray>()) {
      JsonObject weatherObj = doc[F("weather")][0];
      if (weatherObj.containsKey(F("main"))) {
        mainDesc = weatherObj[F("main")].as<String>();
      }
      if (weatherObj.containsKey(F("description"))) {
        detailedDesc = weatherObj[F("description")].as<String>();
      }
    } else {
      Serial.println(F("[WEATHER] Weather description not found in JSON payload"));
    }

    weatherDescription = normalizeWeatherDescription(detailedDesc);
    Serial.printf("[WEATHER] Description used: %s\n", weatherDescription.c_str());
    weatherFetched = true;

  } else {
    Serial.printf("[WEATHER] HTTP GET failed, error code: %d, reason: %s\n", httpCode, http.errorToString(httpCode).c_str());
    weatherAvailable = false;
    weatherFetched = false;
  }

  http.end();
}



// -----------------------------------------------------------------------------
// Main setup() and loop()
// -----------------------------------------------------------------------------
/*
DisplayMode key:
  0: Clock
  1: Weather
  2: Weather Description
  3: Countdown
  4: Nightscout
  5: Matrix Effect
  6: Ping-Pong Effect
  7: Snake Game
  8: Knight Rider Effect
  9: EKG Effect
*/

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println(F("[SETUP] Starting setup..."));

  if (!LittleFS.begin(true)) {
    Serial.println(F("[ERROR] LittleFS mount failed in setup! Halting."));
    while (true) {
      delay(1000);
      yield();
    }
  }
  Serial.println(F("[SETUP] LittleFS file system mounted successfully."));
  
  loadConfig();  // Load configuration first to get pin settings

  // Initialize Parola hardware with correct pins (default or custom)
  if (useCustomPins) {
    Serial.printf("[SETUP] Initializing Parola with CUSTOM pins: CLK=%d, DATA=%d, CS=%d\n", custom_clk_pin, custom_data_pin, custom_cs_pin);
    P = new MD_Parola(HARDWARE_TYPE, custom_data_pin, custom_clk_pin, custom_cs_pin, MAX_DEVICES);
  } else {
    Serial.printf("[SETUP] Initializing Parola with DEFAULT pins: CLK=%d, DATA=%d, CS=%d\n", CLK_PIN, DATA_PIN, CS_PIN);
    P = new MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);
  }

  P->begin();  // Initialize Parola library

  P->setCharSpacing(0);
  P->setFont(mFactory);

  // Apply loaded settings
  P->setIntensity(brightness);
  P->setZoneEffect(0, flipDisplay, PA_FLIP_UD);
  P->setZoneEffect(0, flipDisplay, PA_FLIP_LR);

  Serial.println(F("[SETUP] Parola (LED Matrix) initialized"));

  connectWiFi();

  if (isAPMode) {
    Serial.println(F("[SETUP] WiFi connection failed. Device is in AP Mode."));
  } else if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("[SETUP] WiFi connected successfully to local network."));
  } else {
    Serial.println(F("[SETUP] WiFi state is uncertain after connection attempt."));
  }

  setupWebServer();
  Serial.println(F("[WEBSERVER] Webserver setup complete"));
  Serial.println(F("[SETUP] Setup complete"));
  Serial.println();
  printConfigToSerial();
  setupTime();
  displayMode = 0;
  lastSwitch = millis();
  lastColonBlink = millis();
}


void advanceDisplayMode() {
    int oldMode = displayMode;
    
    // --- Common cleanup for modes being EXITED ---
    if (oldMode == 2) { descScrolling = false; descStartTime = 0; descScrollEndTime = 0; }
    if (oldMode == 3) { countdownScrolling = false; countdownStaticStartTime = 0; countdownScrollEndTime = 0; }
    if (oldMode == 5) { matrixIsFirstRun = true; } // Matrix is mode 5 now
    if (oldMode == 6) { pingPongIsFirstRun = true; } // Ping-Pong is mode 6 now
    if (oldMode == 7) { snakeIsFirstRun = true; } // Snake is mode 7 now
    if (oldMode == 8) { knightRiderIsFirstRun = true; } // Knight Rider is mode 8 now
    if (oldMode == 9) { ekgIsFirstRun = true; } // EKG is mode 9 now
    if (oldMode != 0) { P->displayClear(); }

    int nextMode = 0; // Default to Clock
    bool advancedToPrimary = false;

    // Check if Nightscout is configured
    String nsUrl = String(nightscoutUrl);
    bool nightscoutConfigured = !nsUrl.isEmpty() && nsUrl.startsWith("https://");

    // Check if countdown should be shown
    bool showCountdown = countdownEnabled && !countdownFinished && ntpSyncSuccessful &&
                         (!countdownScheduleEnabled || isTimeWithinSchedule(countdownStartHour, countdownStartMinute, countdownEndHour, countdownEndMinute));

    // 1. Determine next state in PRIMARY sequence (Clock -> Weather -> Desc -> Countdown -> Nightscout)
    if (oldMode == 0) { // From Clock
        if (weatherAvailable && strlen(openWeatherApiKey) == 32) { nextMode = 1; advancedToPrimary = true; }
        else if (showCountdown) { nextMode = 3; advancedToPrimary = true; }
        else if (nightscoutConfigured) { nextMode = 4; advancedToPrimary = true; }
    } else if (oldMode == 1) { // From Weather
        if (showWeatherDescription && weatherAvailable && weatherDescription.length() > 0) { nextMode = 2; advancedToPrimary = true; }
        else if (showCountdown) { nextMode = 3; advancedToPrimary = true; }
        else if (nightscoutConfigured) { nextMode = 4; advancedToPrimary = true; }
    } else if (oldMode == 2) { // From Weather Description
        if (showCountdown) { nextMode = 3; advancedToPrimary = true; }
        else if (nightscoutConfigured) { nextMode = 4; advancedToPrimary = true; }
    } else if (oldMode == 3) { // From Countdown
        if (nightscoutConfigured) { nextMode = 4; advancedToPrimary = true; }
    }
    // Any other mode (Nightscout or an Effect) will fall through to check for effects.

    // 2. If not advanced to a primary mode, or if we are ready to transition to effects, find the next enabled effect
    if (!advancedToPrimary) {
        bool effectsActive = !effectsScheduleEnabled || isTimeWithinSchedule(effectsStartHour, effectsStartMinute, effectsEndHour, effectsEndMinute);
        
        if (effectsActive) {
            int searchStartIndex = (oldMode >= 5 && effectOrderIndex != -1) ? (effectOrderIndex + 1) : 0;
            bool effectFound = false;

            for (int i = searchStartIndex; i < NUM_EFFECTS; i++) {
                int candidateMode = effectOrder[i];
                bool isEnabled = false;
                switch (candidateMode) {
                    case 5: isEnabled = matrixEffectEnabled; break;
                    case 6: isEnabled = pingPongEffectEnabled; break;
                    case 7: isEnabled = snakeEffectEnabled; break;
                    case 8: isEnabled = knightRiderEffectEnabled; break;
                    case 9: isEnabled = ekgEffectEnabled; break;
                }

                if (isEnabled) {
                    nextMode = candidateMode;
                    effectOrderIndex = i; // Store the index of the effect we are switching to
                    effectFound = true;
                    break; // Exit the for loop once an enabled effect is found
                }
            }
            
            if (!effectFound) {
                // No more enabled effects, or none were enabled. Go back to Clock.
                nextMode = 0;
                effectOrderIndex = -1; // Reset effect cycle
            }
        } else {
            // Effects are not active due to schedule, go back to clock
            nextMode = 0;
            effectOrderIndex = -1;
        }
    } else {
        // We are in a primary mode, so we are not in the effect cycle
        effectOrderIndex = -1;
    }

    displayMode = nextMode;
    lastSwitch = millis();
}


//config save after countdown finishes
bool saveCountdownConfig(bool enabled, time_t targetTimestamp, const String &label) {
  DynamicJsonDocument doc(2048);

  File configFile = LittleFS.open("/config.json", "r");
  if (configFile) {
    DeserializationError err = deserializeJson(doc, configFile);
    configFile.close();
    if (err) {
      Serial.print(F("[saveCountdownConfig] Error parsing config.json: "));
      Serial.println(err.f_str());
      return false;
    }
  }

  JsonObject countdownObj = doc["countdown"].is<JsonObject>() ? doc["countdown"].as<JsonObject>() : doc.createNestedObject("countdown");
  countdownObj["enabled"] = enabled;
  countdownObj["targetTimestamp"] = targetTimestamp;
  countdownObj["label"] = label;
  doc.remove("countdownEnabled");
  doc.remove("countdownDate");
  doc.remove("countdownTime");
  doc.remove("countdownLabel");

  if (LittleFS.exists("/config.json")) {
    LittleFS.rename("/config.json", "/config.bak");
  }

  File f = LittleFS.open("/config.json", "w");
  if (!f) {
    Serial.println(F("[saveCountdownConfig] ERROR: Cannot write to /config.json"));
    return false;
  }

  size_t bytesWritten = serializeJson(doc, f);
  f.close();

  Serial.printf("[saveCountdownConfig] Config updated. %u bytes written.\n", bytesWritten);
  return true;
}

// Helper function for snake AI to check for future collisions
bool isCollision(int x, int y) {
    for (uint8_t i = 0; i < snakeLength; i++) {
        if (x == snake[i].x && y == snake[i].y) {
            return true;
        }
    }
    return false;
}


void loop() {
  if (isAPMode) {
    dnsServer.processNextRequest();

    static unsigned long apAnimTimer = 0;
    static int apAnimFrame = 0;
    
    if (millis() - apAnimTimer > 750) {
      apAnimTimer = millis();
      P->setTextAlignment(PA_CENTER);
      switch (apAnimFrame % 3) {
        case 0: P->print(F("= ©")); break;
        case 1: P->print(F("= ª")); break;
        case 2: P->print(F("= «")); break;
      }
      apAnimFrame++;
    }
    
    P->displayAnimate();
    yield();
    return;
  }

  // Handle all other device logic only when not in AP mode
  static bool colonVisible = true;
  const unsigned long colonBlinkInterval = 800;
  if (millis() - lastColonBlink > colonBlinkInterval) {
    colonVisible = !colonVisible;
    lastColonBlink = millis();
  }

  static unsigned long ntpAnimTimer = 0;
  static int ntpAnimFrame = 0;
  static bool tzSetAfterSync = false;

  static unsigned long lastFetch = 0;
  const unsigned long fetchInterval = 300000;  // 5 minutes

  // Dimming
  time_t now_time = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now_time, &timeinfo);
  
  bool isDimmingActive = false;
  if (dimmingEnabled) {
      isDimmingActive = isTimeWithinSchedule(dimStartHour, dimStartMinute, dimEndHour, dimEndMinute);
  }

  if (isDimmingActive) {
      P->setIntensity(dimBrightness);
  } else {
      P->setIntensity(brightness);
  }

  // --- IMMEDIATE COUNTDOWN FINISH TRIGGER ---
  if (countdownEnabled && !countdownFinished && ntpSyncSuccessful && countdownTargetTimestamp > 0 && now_time >= countdownTargetTimestamp) {
    countdownFinished = true;
    displayMode = 3;  // Let main loop handle animation + TIMES UP
    countdownShowFinishedMessage = true;
    hourglassPlayed = false;
    countdownFinishedMessageStartTime = millis();

    Serial.println("[SYSTEM] Countdown target reached! Switching to Mode 3 to display finish sequence.");
    yield();
  }


  // --- IP Display ---
  if (showingIp) {
    if (P->displayAnimate()) {
      ipDisplayCount++;
      if (ipDisplayCount < ipDisplayMax) {
        textEffect_t actualScrollDirection = getEffectiveScrollDirection(PA_SCROLL_LEFT, flipDisplay);
        P->displayScroll(pendingIpToShow.c_str(), PA_CENTER, actualScrollDirection, 120);
      } else {
        showingIp = false;
        P->displayClear();
        delay(500);  // Blocking delay as in working copy
        displayMode = 0;
        lastSwitch = millis();
      }
    }
    yield();
    return;  // Exit loop early if showing IP
  }



  // --- NTP State Machine ---
  switch (ntpState) {
    case NTP_IDLE: break;
    case NTP_SYNCING:
      {
        time_t now = time(nullptr);
        if (now > 1000) {  // NTP sync successful
          Serial.println(F("[TIME] NTP sync successful."));
          ntpSyncSuccessful = true;
          ntpState = NTP_SUCCESS;
        } else if (millis() - ntpStartTime > ntpTimeout || ntpRetryCount >= maxNtpRetries) {
          Serial.println(F("[TIME] NTP sync failed."));
          ntpSyncSuccessful = false;
          ntpState = NTP_FAILED;
        } else {
          // Periodically print a more descriptive status message
          if (millis() - lastNtpStatusPrintTime >= ntpStatusPrintInterval) {
            Serial.printf("[TIME] NTP sync in progress (attempt %d of %d)...\n", ntpRetryCount + 1, maxNtpRetries);
            lastNtpStatusPrintTime = millis();
          }
          // Still increment ntpRetryCount based on your original timing for the timeout logic
          // (even if you don't print a dot for every increment)
          if (millis() - ntpStartTime > ((unsigned long)(ntpRetryCount + 1) * 1000UL)) {
            ntpRetryCount++;
          }
        }
        break;
      }
    case NTP_SUCCESS:
      if (!tzSetAfterSync) {
        const char *posixTz = ianaToPosix(timeZone);
        setenv("TZ", posixTz, 1);
        tzset();
        tzSetAfterSync = true;
      }
      ntpAnimTimer = 0;
      ntpAnimFrame = 0;
      break;
    case NTP_FAILED:
      ntpAnimTimer = 0;
      ntpAnimFrame = 0;
      break;
  }



  // Only advance mode by timer for clock/weather and effects
  unsigned long displayDuration = 0;
  if (displayMode == 0) displayDuration = clockDuration;
  else if (displayMode == 1) displayDuration = weatherDuration;
  else if (displayMode == 5) displayDuration = matrixDuration;
  else if (displayMode == 6) displayDuration = pingPongDuration;
  else if (displayMode == 7) displayDuration = snakeDuration;
  else if (displayMode == 8) displayDuration = knightRiderDuration;
  else if (displayMode == 9) displayDuration = ekgDuration;


  if (displayDuration > 0 && millis() - lastSwitch > displayDuration) {
    advanceDisplayMode();
  }



  // --- MODIFIED WEATHER FETCHING LOGIC ---
  if (WiFi.status() == WL_CONNECTED) {
    if (!weatherFetchInitiated || shouldFetchWeatherNow || (millis() - lastFetch > fetchInterval)) {
      if (shouldFetchWeatherNow) {
        Serial.println(F("[LOOP] Immediate weather fetch requested by web server."));
        shouldFetchWeatherNow = false;
      } else if (!weatherFetchInitiated) {
        Serial.println(F("[LOOP] Initial weather fetch."));
      } else {
        Serial.println(F("[LOOP] Regular interval weather fetch."));
      }
      weatherFetchInitiated = true;
      weatherFetched = false;
      fetchWeather();
      lastFetch = millis();
    }
  } else {
    weatherFetchInitiated = false;
    shouldFetchWeatherNow = false;
  }

  const char *const *daysOfTheWeek = getDaysOfWeek(language);
  const char *daySymbol = daysOfTheWeek[timeinfo.tm_wday];

  char timeStr[9];
  if (twelveHourToggle) {
    int hour12 = timeinfo.tm_hour % 12;
    if (hour12 == 0) hour12 = 12;
    sprintf(timeStr, " %d:%02d", hour12, timeinfo.tm_min);
  } else {
    sprintf(timeStr, " %02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
  }

  char timeSpacedStr[20];
  int j = 0;
  for (int i = 0; timeStr[i] != '\0'; i++) {
    timeSpacedStr[j++] = timeStr[i];
    if (timeStr[i + 1] != '\0') {
      timeSpacedStr[j++] = ' ';
    }
  }
  timeSpacedStr[j] = '\0';

  String formattedTime;
  if (showDayOfWeek) {
    formattedTime = String(daySymbol) + " " + String(timeSpacedStr);
  } else {
    formattedTime = String(timeSpacedStr);
  }

  // --- CLOCK Display Mode ---
  if (displayMode == 0) {
    P->setTextAlignment(PA_CENTER);
    P->setCharSpacing(0);

    if (ntpState == NTP_SYNCING) {
      if (ntpSyncSuccessful || ntpRetryCount >= maxNtpRetries || millis() - ntpStartTime > ntpTimeout) {
        ntpState = NTP_FAILED;
      } else {
        if (millis() - ntpAnimTimer > 750) {
          ntpAnimTimer = millis();
          switch (ntpAnimFrame % 3) {
            case 0: P->print(F("S Y N C ®")); break;
            case 1: P->print(F("S Y N C ¯")); break;
            case 2: P->print(F("S Y N C °")); break;
          }
          ntpAnimFrame++;
        }
      }
    } else if (!ntpSyncSuccessful) {
      P->setTextAlignment(PA_CENTER);

      static unsigned long errorAltTimer = 0;
      static bool showNtpError = true;

      if (!ntpSyncSuccessful && !weatherAvailable) {
        if (millis() - errorAltTimer > 2000) {
          errorAltTimer = millis();
          showNtpError = !showNtpError;
        }

        if (showNtpError) {
          P->print(F("?/"));  // NTP error glyph
        } else {
          P->print(F("?*"));  // Weather error glyph
        }

      } else if (!ntpSyncSuccessful) {
        P->print(F("?/"));  // NTP only
      } else if (!weatherAvailable) {
        P->print(F("?*"));  // Weather only
      }

    } else {
      // NTP and weather are OK — show time
      String timeString = formattedTime;
      if (colonBlinkEnabled && !colonVisible) {
        timeString.replace(":", " ");
      }
      P->print(timeString);
    }
  }



  // --- WEATHER Display Mode ---
  else if (displayMode == 1) {
    if (weatherAvailable) {
        P->setTextAlignment(PA_CENTER);
        P->setCharSpacing(1);
        String weatherDisplay;
        if (showHumidity && currentHumidity != -1) {
            int cappedHumidity = (currentHumidity > 99) ? 99 : currentHumidity;
            weatherDisplay = currentTemp + " " + String(cappedHumidity) + "%";
        } else {
            weatherDisplay = currentTemp + tempSymbol;
        }
        P->print(weatherDisplay.c_str());
    } else {
        Serial.println(F("[DISPLAY] Weather not available. Skipping to next mode immediately."));
        advanceDisplayMode();
    }
  }



  // --- WEATHER DESCRIPTION Display Mode ---
  else if (displayMode == 2 && showWeatherDescription && weatherAvailable && weatherDescription.length() > 0) {
    String desc = weatherDescription;
    desc.toUpperCase();

    if (desc.length() > 8) {
      if (!descScrolling) {
        P->displayClear();
        textEffect_t actualScrollDirection = getEffectiveScrollDirection(PA_SCROLL_LEFT, flipDisplay);
        P->displayScroll(desc.c_str(), PA_CENTER, actualScrollDirection, GENERAL_SCROLL_SPEED);
        descScrolling = true;
        descScrollEndTime = 0;
      }
      if (P->displayAnimate()) {
        if (descScrollEndTime == 0) {
          descScrollEndTime = millis();
        }
        if (millis() - descScrollEndTime > descriptionScrollPause) {
          descScrolling = false;
          descScrollEndTime = 0;
          advanceDisplayMode();
        }
      } else {
        descScrollEndTime = 0;
      }
    } else {
      if (descStartTime == 0) {
        P->setTextAlignment(PA_CENTER);
        P->setCharSpacing(1);
        P->print(desc.c_str());
        descStartTime = millis();
      }
      if (millis() - descStartTime > descriptionDuration) {
        descStartTime = 0;
        advanceDisplayMode();
      }
    }
  }


  // --- Countdown Display Mode ---
  else if (displayMode == 3 && countdownEnabled && ntpSyncSuccessful) {
    long timeRemaining = countdownTargetTimestamp - now_time;

    if (timeRemaining <= 0 || countdownShowFinishedMessage) {
      // FINISHED LOGIC (UNCHANGED)
    } else {
        // Calculate days, hours, and minutes from the remaining seconds.
        long days = timeRemaining / (24 * 3600);
        long hours = (timeRemaining % (24 * 3600)) / 3600;
        long minutes = (timeRemaining % 3600) / 60;

        // Build the display string piece by piece for better grammar
        String displayText = "";
        
        // Add the label if it exists, otherwise use a random fallback.
        if (strlen(countdownLabel) > 0) {
            displayText += String(countdownLabel) + " IN:";
        } else {
            // If no label is set, pick a random one
            static const char *fallbackLabels[] = {
                "PARTY TIME", "SHOWTIME", "CLOCKOUT", "BLASTOFF", "GO TIME",
                "LIFTOFF", "BIG REVEAL", "ZERO HOUR", "FINAL COUNT", "MISSION COMPLETE"
            };
            int randomIndex = random(0, 10);
            displayText += String(fallbackLabels[randomIndex]) + " IN:";
        }

        if (days > 0) {
            displayText += String(days);
            displayText += (days == 1) ? " D " : " D ";
        }
        if (hours > 0 || days > 0) { // Show hours if there are days or hours
            displayText += String(hours);
            displayText += (hours == 1) ? " H " : " H ";
        }
        displayText += String(minutes);
        displayText += (minutes == 1) ? " M" : " M";
    
    
        // Loop for the configured number of scrolls
        for (int i = 0; i < countdownScrollCount; i++) {
            // Start the scrolling animation
            P->displayClear();
            P->setTextAlignment(PA_LEFT);
            P->setCharSpacing(1);
            textEffect_t actualScrollDirection = getEffectiveScrollDirection(PA_SCROLL_LEFT, flipDisplay);
            P->displayScroll(displayText.c_str(), PA_LEFT, actualScrollDirection, GENERAL_SCROLL_SPEED);
            
            // Log the scroll action to the Serial Monitor
            Serial.printf("[COUNTDOWN] Scrolling text (Pass %d/%d): %s\n", i + 1, countdownScrollCount, displayText.c_str());
        
            // Wait here until the current scroll animation is done
            while (!P->displayAnimate()) {
                yield();
            }
        }

        // Move to the next display mode
        advanceDisplayMode();
    }
  }
  
  // --- NIGHTSCOUT Display Mode ---
  else if (displayMode == 4) {
    String nsUrl = String(nightscoutUrl);

    static unsigned long lastNightscoutFetchTime = 0;
    const unsigned long NIGHTSCOUT_FETCH_INTERVAL = 150000;  // 2.5 minutes
    static int currentGlucose = -1;
    static String currentDirection = "?";

    if (currentGlucose == -1 || millis() - lastNightscoutFetchTime >= NIGHTSCOUT_FETCH_INTERVAL) {
      WiFiClientSecure client;
      client.setInsecure();
      HTTPClient https;
      https.begin(client, nsUrl);
      https.setConnectTimeout(5000);
      https.setTimeout(5000);

      Serial.print("[HTTPS] Nightscout fetch initiated...\n");
      int httpCode = https.GET();

      if (httpCode == HTTP_CODE_OK) {
        String payload = https.getString();
        StaticJsonDocument<1024> doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (!error && doc.is<JsonArray>() && doc.size() > 0) {
          JsonObject firstReading = doc[0].as<JsonObject>();
          currentGlucose = firstReading["glucose"] | firstReading["sgv"] | -1;
          currentDirection = firstReading["direction"] | "?";

          Serial.printf("Nightscout data fetched: mg/dL %d %s\n", currentGlucose, currentDirection.c_str());
        } else {
          Serial.println("Failed to parse Nightscout JSON");
        }
      } else {
        Serial.printf("[HTTPS] GET failed, error: %s\n", https.errorToString(httpCode).c_str());
      }

      https.end();
      lastNightscoutFetchTime = millis();
    }

    if (currentGlucose != -1) {
      char arrow;
      if (currentDirection == "Flat") arrow = 139;
      else if (currentDirection == "SingleUp") arrow = 134;
      else if (currentDirection == "DoubleUp") arrow = 135;
      else if (currentDirection == "SingleDown") arrow = 136;
      else if (currentDirection == "DoubleDown") arrow = 137;
      else if (currentDirection == "FortyFiveUp") arrow = 138;
      else if (currentDirection == "FortyFiveDown") arrow = 140;
      else arrow = '?';

      String displayText = String(currentGlucose) + String(arrow);

      P->setTextAlignment(PA_CENTER);
      P->setCharSpacing(1);
      P->print(displayText.c_str());

      // Use a non-blocking delay for the display duration
      if (millis() - lastSwitch > weatherDuration) {
          advanceDisplayMode();
      }
    } else {
      P->setTextAlignment(PA_CENTER);
      P->setCharSpacing(0);
      P->print(F("?)"));
      // Use a non-blocking delay
      if (millis() - lastSwitch > 2000) {
          advanceDisplayMode();
      }
    }
  }

  // --- Matrix Effect Display Mode ---
  else if (displayMode == 5 && matrixEffectEnabled) {
    if (millis() - matrixLastUpdate > 100) { // Replaces delay()
        matrixLastUpdate = millis();
        static uint8_t matrix[MAX_DEVICES*8];

        if (matrixIsFirstRun) {
            for (uint8_t i=0; i<MAX_DEVICES*8; i++)
                matrix[i] = 0;
            matrixIsFirstRun = false;
        }

        for (uint8_t i=0; i<MAX_DEVICES*8; i++)
            matrix[i] <<= 1;

        for (int k=0; k < matrixLoad; k++) {
          if (random(4) == 0) {
              uint8_t x = random(MAX_DEVICES*8);
              if (matrix[x] == 0)
                  matrix[x] = 1;
          }
        }

        for (uint8_t i=0; i<MAX_DEVICES*8; i++)
            P->getGraphicObject()->setColumn(i, matrix[i]);
    }
  }

  // --- Ping-Pong Effect Display Mode ---
  else if (displayMode == 6 && pingPongEffectEnabled) {
      if (pingPongIsFirstRun) {
          paddle1 = {MATRIX_HEIGHT / 2, 1};
          paddle2 = {MATRIX_HEIGHT / 2, -1};
          ball = {MATRIX_WIDTH / 2, MATRIX_HEIGHT / 2, (random(2) == 0) ? 1 : -1, (random(2) == 0) ? 1 : -1};
          pingPongIsFirstRun = false;
      }

      if (millis() - pingPongLastUpdate > PING_PONG_SPEED) {
          pingPongLastUpdate = millis();
          paddle1.y += paddle1.dy;
          paddle2.y += paddle2.dy;

          if (paddle1.y <= 0 || paddle1.y + PADDLE_HEIGHT >= MATRIX_HEIGHT) {
              paddle1.dy = -paddle1.dy;
          }
          if (paddle2.y <= 0 || paddle2.y + PADDLE_HEIGHT >= MATRIX_HEIGHT) {
              paddle2.dy = -paddle2.dy;
          }

          ball.x += ball.dx;
          ball.y += ball.dy;

          if (ball.y <= 0 || ball.y + BALL_SIZE >= MATRIX_HEIGHT) {
              ball.dy = -ball.dy;
          }

          if (ball.x <= 1 && ball.y >= paddle1.y && ball.y < paddle1.y + PADDLE_HEIGHT) {
              ball.dx = -ball.dx;
          }
          if (ball.x + BALL_SIZE >= MATRIX_WIDTH - 1 && ball.y >= paddle2.y && ball.y < paddle2.y + PADDLE_HEIGHT) {
              ball.dx = -ball.dx;
          }

          if (ball.x < 0 || ball.x > MATRIX_WIDTH) {
              ball.x = MATRIX_WIDTH / 2;
              ball.y = MATRIX_HEIGHT / 2;
              ball.dx = (random(2) == 0) ? 1 : -1;
              ball.dy = (random(2) == 0) ? 1 : -1;
          }
          
          P->displayClear();
          for (int i = 0; i < PADDLE_HEIGHT; i++) {
              if (paddle1.y + i >= 0 && paddle1.y + i < MATRIX_HEIGHT)
                  P->getGraphicObject()->setPoint(paddle1.y + i, 0, true);
          }
          for (int i = 0; i < PADDLE_HEIGHT; i++) {
              if (paddle2.y + i >= 0 && paddle2.y + i < MATRIX_HEIGHT)
                  P->getGraphicObject()->setPoint(paddle2.y + i, MATRIX_WIDTH - 1, true);
          }
          if (ball.x >= 0 && ball.x < MATRIX_WIDTH && ball.y >= 0 && ball.y < MATRIX_HEIGHT)
              P->getGraphicObject()->setPoint(ball.y, ball.x, true);
      }
  }

  // --- Snake Game Display Mode ---
  else if (displayMode == 7 && snakeEffectEnabled) {
    if (snakeIsFirstRun) {
        snakeLength = 3;
        snake[0] = {MATRIX_WIDTH / 2, MATRIX_HEIGHT / 2};
        snake[1] = {MATRIX_WIDTH / 2 - 1, MATRIX_HEIGHT / 2};
        snake[2] = {MATRIX_WIDTH / 2 - 2, MATRIX_HEIGHT / 2};
        do {
            foodX = random(MATRIX_WIDTH);
            foodY = random(MATRIX_HEIGHT);
        } while (isCollision(foodX, foodY));
        snakeDir = RIGHT;
        snakeIsFirstRun = false;
        P->displayClear(); // Initial clear
    }

    if (millis() - snakeLastUpdate > SNAKE_SPEED) {
        snakeLastUpdate = millis();
        int hx = snake[0].x;
        int hy = snake[0].y;

        Direction possible_dirs[3];
        int num_possible = 0;
        if (snakeDir != DOWN) possible_dirs[num_possible++] = UP;
        if (snakeDir != UP) possible_dirs[num_possible++] = DOWN;
        if (snakeDir != RIGHT) possible_dirs[num_possible++] = LEFT;
        if (snakeDir != LEFT) possible_dirs[num_possible++] = RIGHT;

        Direction best_dir = snakeDir;
        int min_dist = 1001;

        for (int i = 0; i < num_possible; i++) {
            Direction current_dir = possible_dirs[i];
            int next_x = hx;
            int next_y = hy;

            if (current_dir == UP) next_y--;
            if (current_dir == DOWN) next_y++;
            if (current_dir == LEFT) next_x--;
            if (current_dir == RIGHT) next_x++;

            if (next_x < 0) next_x = MATRIX_WIDTH - 1;
            if (next_x >= MATRIX_WIDTH) next_x = 0;
            if (next_y < 0) next_y = MATRIX_HEIGHT - 1;
            if (next_y >= MATRIX_HEIGHT) next_y = 0;
            
            if (!isCollision(next_x, next_y)) {
                int dist = abs(next_x - foodX) + abs(next_y - foodY);
                if (dist < min_dist) {
                    min_dist = dist;
                    best_dir = current_dir;
                }
            }
        }
        snakeDir = best_dir;

        SnakeSegment newHead = snake[0];
        if (snakeDir == UP) newHead.y--;
        if (snakeDir == DOWN) newHead.y++;
        if (snakeDir == LEFT) newHead.x--;
        if (snakeDir == RIGHT) newHead.x++;

        if (newHead.x < 0) newHead.x = MATRIX_WIDTH - 1;
        if (newHead.x >= MATRIX_WIDTH) newHead.x = 0;
        if (newHead.y < 0) newHead.y = MATRIX_HEIGHT - 1;
        if (newHead.y >= MATRIX_HEIGHT) newHead.y = 0;

        if (snakeLength >= 13 && isCollision(newHead.x, newHead.y)) {
            snakeIsFirstRun = true;
            return;
        }
        
        memmove(&snake[1], &snake[0], sizeof(SnakeSegment) * (snakeLength));
        snake[0] = newHead;

        if (newHead.x == foodX && newHead.y == foodY) {
            if (snakeLength < MAX_DEVICES * 8 * 8) {
                snakeLength++;
            }
            do {
                foodX = random(MATRIX_WIDTH);
                foodY = random(MATRIX_HEIGHT);
            } while (isCollision(foodX, foodY));
        }

        P->displayClear();
        P->getGraphicObject()->setPoint(foodY, foodX, true);
        for (int i = 0; i < snakeLength; i++) {
            P->getGraphicObject()->setPoint(snake[i].y, snake[i].x, true);
        }
    }
  }
  // --- Knight Rider Effect Display Mode ---
  else if (displayMode == 8 && knightRiderEffectEnabled) {
    // Map the 1-10 speed to a delay. Lower delay = faster.
    // (11 - speed) makes slider intuitive (10 is fastest).
    int scan_delay = (11 - knightRiderSpeed) * 8; 

    if (millis() - knightRiderLastUpdate > scan_delay) {
      knightRiderLastUpdate = millis();

      if (knightRiderIsFirstRun) {
        knightRiderPos = 0;
        knightRiderDir = 1;
        knightRiderIsFirstRun = false;
      }

      P->displayClear();

      // Draw the scanner bar with a tail
      for (int i = 0; i < knightRiderWidth; i++) {
        int trailPos = knightRiderPos - (knightRiderDir * i);
        if (trailPos >= 0 && trailPos < MATRIX_WIDTH) {
          P->getGraphicObject()->setPoint(KR_BAR_Y, trailPos, true);
        }
      }
      
      // Move the scanner and check for bounce
      knightRiderPos += knightRiderDir;
      if (knightRiderPos <= 0 || knightRiderPos >= MATRIX_WIDTH - 1) {
        knightRiderDir *= -1;
      }
    }
  }

  // --- EKG Effect Display Mode ---
  else if (displayMode == 9 && ekgEffectEnabled) {
    if (ekgIsFirstRun) {
      ekgCurrentPixel = 0;
      ekg_prev_yPos = -1;
      ekgPaused = false;
      ekgIsFirstRun = false;
      P->displayClear(); // Clear the display at the very beginning of the effect
    }
    
    // Handle the pause after one full animation cycle
    if (ekgPaused) {
      if(millis() - ekgPauseStartTime > 250) { // Non-blocking delay
        ekgIsFirstRun = true; // This will trigger a reset on the next loop
      }
      return; // Do nothing else while paused
    }

    // Map the 1-10 speed to a drawing delay. Lower delay = faster.
    int pixel_draw_delay = 150 - (ekgSpeed * 10);

    if (millis() - ekgLastDrawTime > pixel_draw_delay) {
      ekgLastDrawTime = millis();

      // Calculate X position, drawing from right to left
      int draw_x = (MATRIX_WIDTH - 1) - ekgCurrentPixel;
      
      int yPos = 4; // Flatline
      if (ekgCurrentPixel >= 4 && ekgCurrentPixel < 4 + ekgWaveformSize) {
        int waveIndex = ekgCurrentPixel - 4;
        yPos = heartBeatWaveform[waveIndex];
      }
      
      // Draw the current point
      P->getGraphicObject()->setPoint(yPos, draw_x, true);

      // Connect points with a vertical line if the jump is large
      if (ekgCurrentPixel > 0 && ekg_prev_yPos != -1) {
        int dy = abs(ekg_prev_yPos - yPos);
        if (dy > 1) {
          int start_y = min(yPos, ekg_prev_yPos);
          int end_y = max(yPos, ekg_prev_yPos);
          for (int y = start_y + 1; y < end_y; y++) {
            // Draw the connecting line on the previous column
            P->getGraphicObject()->setPoint(y, draw_x + 1, true);
          }
        }
      }
      
      ekg_prev_yPos = yPos;
      ekgCurrentPixel++;

      // Check if animation cycle is complete
      if (ekgCurrentPixel >= MATRIX_WIDTH) {
        ekgPaused = true;
        ekgPauseStartTime = millis(); // Start the pause timer
      }
    }
  }
  
  P->displayAnimate();
  yield();
}
