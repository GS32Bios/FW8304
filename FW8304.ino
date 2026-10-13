#define HAS_BUTTONS  1
#define HAS_DISPLAY  1
#define HAS_WIFI     1
#define HAS_SENSORS  1
#define HAS_IR       0
#define HAS_AUDIO    0
#define HAS_REPORT   1
#include <Arduino.h>
#include <Update.h>
#include <GS32BIOS.h>
#include <GS32Config.h>

GS32BIOS Bios;        ///! Инициализация GS32Bios
GS32Config Config;    ///! Инициализация Конфигурации

unsigned long lastDisplayUpdate = 0;
int currentScreen = 0;           // 0: Время, 1+: Датчики
unsigned long showChipIdUntil = 0; // Таймер для ID чипа
bool forceDisplayUpdate = false;

///! Конфигурация
///! Обратите внимание! Изменения параметров после первого старта системы нужно выполнять через код или сбрасывать NVS

int8_t GBtn1;
int8_t GBtn2;
int8_t GBtn3;

int8_t GDspMOSI;
int8_t GDspCLK;
int8_t GDspDC;
int8_t GDspRST;
int8_t GDspSC;
int8_t GDspOffX;
int8_t GDspOffY;

int WiFiMode = 0; 
int WiFiModeAuth = 0; 
char WiFiSTASSID[32] = "";
char WiFiSTAPass[32] = "";
char WiFiAPSSID[32]  = "GS32_AP";
char WiFiAPPass[32]  = "12345678";
int WiFiSelNet = 0;
const char* WiFiAuthModes[] = {"Open", "WPA2-PSK", "WPA3-SAE", "WEE"};
const char* WiFiStatus[] = {"OFF", "STA", "AP", "STA + AP"};

const char COMP_TIME[21] = __DATE__ " " __TIME__; 
char GBuildTime[21];

char wifi_rssi[16] = {0};
char wifi_mode_state[32] = {0};
char wifi_ip_addr[16] = {0};
char wifi_ap_ip_addr[16] = {0};
char wifi_ap_clients[16] = {0};

bool GIRState = false;
int8_t GIRPIN;

uint8_t GAudioBCK = 26;
uint8_t GAudioLRCK = 25;
uint8_t GAudioDIN = 22;
int GAudioStatus = 1; 
int GAudioVolume = 15;
int GEQPreset = 0;
int8_t GEQLow = 0, GEQMid = 0, GEQHigh = 0;
const char* GAudioModes[] = {"OFF", "BT Sink", "BT Source", "WebRadio"};
const char* GEQPresets[] = {"Pop", "Jazz", "Rock", "Classic", "Vocal", "Custom"};

char GBTSinkName[32] = "GS32_Audio";
char GBTSourceTarget[32] = "";


bool GReportEnabled       = false;
char GReportServer[128]   = {0};
char GReportToken[128]    = {0};
bool GReportChipModel     = false;
bool GReportChipCore      = false;
bool GReportChipRev       = false;
bool GReportCPUFreq       = false;
bool GReportChipID        = false;
bool GReportHeap          = false;
bool GReportMinHeap       = false;
bool GReportMaxHeap       = false;
bool GReportFlashSize     = false;
bool GReportFlashSpeed    = false;
bool GReportSDKVer        = false;
bool GReportFWSize        = false;
bool GReportFWFree        = false;
bool GReportUptime        = false;

unsigned long lastSendTime = 0;
const unsigned long sendInterval = 60000;

#if HAS_BUTTONS
  #include <GS32Buttons.h>
  GS32Buttons GBtn;
#endif
#if HAS_DISPLAY
  #include <GS32Display1306.h>
  GS32Display1306 Display;
#endif
#if HAS_WIFI
  #include <GS32WIFI.h>
  GS32WIFI GSWiFi;
#endif
#if HAS_SENSORS
  #include "GS32SensorHub.h"
  GS32SensorHub GSensors;
  const DHTType SensorsMap[] = { DHT_11, DHT_11, DHT_21, DHT_22 };
#endif
#if HAS_IR
  #include "GS32IR.h"
  GS32IR IR;
#endif
#if HAS_AUDIO
  #include "GS32AudioI2S.h"
  GS32AudioI2S Audio;
#endif
#if HAS_REPORT
  #include <GS32ReportWeb.h>
  GS32ReportWeb Report;
#endif

const int MAX_SENSORS = 9;
uint8_t SensorPins[MAX_SENSORS]; 
int SensorTypes[MAX_SENSORS];
const char* SensorsType[] = {"Disabled", "DHT11", "DHT21 / AM2301", "DHT22 / AM2302"};

// --- Функция отрисовки иконки WiFi со статусом ---
void drawWiFiStatus() {
  int x = 110, y = 0;
  bool isConnected = GSWiFi.isConnected();
  bool isConnecting = (WiFi.status() == WL_DISCONNECTED && WiFiMode > 0 && WiFiMode != 2);

  if (isConnecting) {
    if ((millis() / 500) % 2 == 0) return; // Мигание
  }

  // Рисуем иконку WiFi
  Display.drawIconText(GS32_ICON_WIFI, 16, 16, "", x, y, 1, 0);

  if (!isConnected && !isConnecting) {
    // Перечеркиваем иконку (X), если нет связи
    Display.drawLine(x, y, x + 15, y + 15, 1);
    Display.drawLine(x + 15, y, x, y + 15, 1);
  }
}

// --- Главная функция отрисовки экрана ---
void updateDisplayUI() {
  #if HAS_DISPLAY
    if (!Display.isReady()) return;

    Display.clear();

    // 1. Проверка: нужно ли показать ID чипа (Кнопка 3)
    if (millis() < showChipIdUntil) {
      uint64_t chipid = ESP.getEfuseMac();
      String idStr = String((uint32_t)(chipid >> 32), HEX) + String((uint32_t)chipid, HEX);
      Display.drawText("CHIP ID:", 0, 0, 1);
      Display.drawText(idStr, 0, 15, 1);
      Display.send();
      return;
    }

    // 2. Отрисовка значка WiFi (в углу)
    bool connected = GSWiFi.isConnected();
    bool connecting = (WiFi.status() == WL_DISCONNECTED && WiFiMode > 0 && WiFiMode != 2);
    
    if (!connecting || (millis() / 500) % 2 != 0) {
      if (!connected && !connecting) Display.drawText("X", 115, 0, 1);
      else if (connecting) Display.drawText("?", 115, 0, 1);
      else Display.drawText("W", 115, 0, 1);
    }

    // 3. Логика цикличных экранов
    if (currentScreen == 0) {
      // ЭКРАН: ВРЕМЯ (Uptime, так как нет RTC/NTP в коде)
      unsigned long sec = millis() / 1000;
      char timeBuf[16];
      snprintf(timeBuf, sizeof(timeBuf), "%02lu:%02lu:%02lu", (sec/3600)%24, (sec/60)%60, sec%60);
      Display.drawText("UPTIME", 0, 0, 1);
      Display.drawText(timeBuf, 0, 15, 2);
    } 
    else {
      // ЭКРАН: ДАТЧИКИ
      int sensorIdx = currentScreen - 1;
      GS32_SensorData data;
      if (GSensors.getSensorData(sensorIdx, data) && data.isValid) {
        Display.drawText("SENSOR " + String(sensorIdx + 1), 0, 0, 1);
        Display.drawIconText(GS32_ICON_TEMP, 16, 16, String(data.temperature, 1) + " C", 0, 12, 1);
        Display.drawIconText(GS32_ICON_HUM, 16, 16, String(data.humidity, 1) + " %", 0, 28, 1);
      } else {
        // Если датчик не готов или пустой, прыгаем на следующий при обновлении
        Display.drawText("SCANNING...", 0, 15, 1);
      }
    }

    Display.send();
  #endif
}


void GS_Buttons(){
  #if HAS_BUTTONS
    Serial.printf("[GS32] [Buttons] Configurate pin: %d, %d, %d\r\n", (int)GBtn1, (int)GBtn2, (int)GBtn3);

    ///! Добавляем пункты в Bios

    Bios.addPage("Buttons", "Advanced");                                              ///! Advanced -> Buttons
    Bios.addSubMenuAction("Advanced", "Configure Buttons...", "Buttons");             ///! Регистрируем пункт меню

    Bios.addInt8("Buttons", "GPIO Button 1", &GBtn1, 1, 35);
    Bios.addInt8("Buttons", "GPIO Button 2", &GBtn2, 1, 35);
    Bios.addInt8("Buttons", "GPIO Button 3", &GBtn3, 1, 35);

    ///! Реализация логики

    auto& btn1  = GBtn.add(GBtn1); 
    auto& btn2  = GBtn.add(GBtn2); 
    auto& btn3  = GBtn.add(GBtn3); 

    btn1.onSingleClick([](GS32Button& b){
      #if HAS_DISPLAY
        currentScreen++;
        int totalSensors = GSensors.getSensorCount();
        if (currentScreen > totalSensors) currentScreen = 0;
        lastDisplayUpdate = millis(); // Сбрасываем таймер автопереключения
        forceDisplayUpdate = true;
        Serial.println("Manual switch screen");
      #endif
    });
    
    btn2.onSingleClick([](GS32Button& b){
      #if HAS_WIFI
        WiFiMode++;
        if (WiFiMode > 3) WiFiMode = 0;
        GSWiFi.begin((GS32WifiMode)WiFiMode, WiFiSTASSID, WiFiSTAPass, WiFiAPSSID, WiFiAPPass);
        Config.save(); // Сохраняем новый режим
        Serial.printf("Station mode changed to: %s\n", WiFiStatus[WiFiMode]);
      #endif
    });

    btn3.onSingleClick([](GS32Button& b){
      #if HAS_DISPLAY
        showChipIdUntil = millis() + 3000;
        forceDisplayUpdate = true;
        Serial.println("Show Chip ID");
      #endif
    });

    GBtn.begin();
  #endif
}

void GS_Display(){
  #if HAS_DISPLAY
    Serial.printf("[GS32] [Display] Configurate pin: %d, %d, %d, %d, %d (%d, %d)\r\n", (int)GDspMOSI, (int)GDspCLK, (int)GDspDC, (int)GDspRST, (int)GDspSC, (int)GDspOffX, (int)GDspOffY);
    Bios.addPage("Display", "Advanced");
    Bios.addSubMenuAction("Advanced", "Configure Display...", "Display");

    Bios.addInt8("Display", "MOSI GPIO", &GDspMOSI, 1, 35);
    Bios.addInt8("Display", "CLK GPIO",  &GDspCLK, 1, 35);
    Bios.addInt8("Display", "DC GPIO",   &GDspDC, 1, 35);
    Bios.addInt8("Display", "RST GPIO",  &GDspRST, 1, 35);
    Bios.addInt8("Display", "SC GPIO",   &GDspSC, 1, 35);
    Bios.addInt8("Display", "Offset X",  &GDspOffX, 0, 127);
    Bios.addInt8("Display", "Offset Y",  &GDspOffY, 0, 127);

    if(Display.begin(GDspMOSI, GDspCLK, GDspDC, GDspRST, GDspSC)) {
      Serial.printf("[GS32] [Display] Ready!\r\n");
      Display.setGlobalOffset(GDspOffX, GDspOffY);
      Display.drawText("boot", 20, 10, 2);
    }
  #endif
}

void GS_OTA(){
  Bios.addPage("OTA", "Advanced");         // Advanced -> OTA
  Bios.addSubMenuAction("Advanced", "Configure OTA-Update...", "OTA");

  char sys_flash_size[16] = {0};
  char sys_fw_size[16] = {0};
  char sys_ota_size[16] = {0};

  // Проверяем общую Flash-память чипа (обычно 4 МБ = 4194304 Байт)
  uint32_t flashSize = ESP.getFlashChipSize();
  // Узнаем размер текущей запущенной прошивки
  uint32_t sketchSize = ESP.getSketchSize();
  // Узнаем, сколько места выделено под OTA-раздел
  uint32_t maxOtaSize = Update.size(); 

  if (maxOtaSize == 0){
    Bios.addInfoEx("OTA", "Status", "[Disabled]");
  } else if (sketchSize > maxOtaSize){
    Bios.addInfoEx("OTA", "Status", "[Not Enough Space]");
  } else {
    Bios.addInfoEx("OTA", "Status", "[Enabled]");
  }

  snprintf(sys_flash_size, sizeof(sys_flash_size), "%d B", (int)(flashSize));
  snprintf(sys_fw_size, sizeof(sys_fw_size), "%d B", (int)(sketchSize));
  snprintf(sys_ota_size, sizeof(sys_ota_size), "%d B", (int)(maxOtaSize));

  Bios.addInfoEx("OTA", "Flash size", sys_flash_size);
  Bios.addInfoEx("OTA", "FW size", sys_fw_size);
  Bios.addInfoEx("OTA", "OTA size", sys_ota_size);

  ///! Тут при желании можно добавить автообновление по воздуху
}

void GS_WiFi(){
  #if HAS_WIFI
    Bios.addPage("WiFi", "Advanced");                 // Advanced -> WiFi
    Bios.addPage("WiFi STA settings", "WiFi");        // Advanced -> WiFi -> STA
    Bios.addPage("WiFi AP settings", "WiFi");         // Advanced -> WiFi -> AP
    Bios.addSubMenuAction("Advanced", "Configure WiFi...",      "WiFi");
    
    Bios.addSelect("WiFi", "WiFi Status", &WiFiMode, 4, WiFiStatus);

    Bios.addDynamicInfoEx("WiFi", "Mode", []() {
      return GSWiFi.getModeString();
    });

    Bios.addDynamicInfoEx("WiFi", "Connect (Any)", []() {
      return GSWiFi.isConnected() ? "OK" : "ERR";
    });

    ///! STA MODE

    Bios.addText("WiFi STA settings", "STA SSID", WiFiSTASSID, 32, true);
    Bios.addText("WiFi STA settings", "STA Password", WiFiSTAPass, 32, true);
    Bios.addSelect("WiFi STA settings", "Security", &WiFiModeAuth, 4, WiFiAuthModes);
    
    Bios.addDynamicInfoEx("WiFi STA settings", "IP-address", []() {
      return GSWiFi.getSTAIP();
    });

    Bios.addDynamicInfoEx("WiFi STA settings", "Signal", []() {
      char buf[32];
      snprintf(buf, sizeof(buf), "%d %% (%d dBm)", GSWiFi.getRSSIPercentage(), GSWiFi.getRSSI());
      return String(buf);
    });
    
    ///! AP MODE
    
    Bios.addText("WiFi AP settings", "AP SSID", WiFiAPSSID, 32, true);
    Bios.addText("WiFi AP settings", "AP Password", WiFiAPPass, 32, true);
    
    Bios.addDynamicInfoEx("WiFi AP settings", "IP-address", []() {
      return GSWiFi.getAPIP();
    });

    Bios.addDynamicInfoEx("WiFi AP settings", "Clients", []() {
      return String(GSWiFi.getConnectedClientsCount());
    });

    ///! Others...
    Bios.addSubMenuAction("WiFi", "WiFi STA settings...",   "WiFi STA settings");
    Bios.addSubMenuAction("WiFi", "WiFi AP settings...",    "WiFi AP settings");

    
    if(WiFiMode > 0) {
      Serial.printf("[GS32] [WiFi] Configurate mode: %d\r\n", (int)WiFiMode);
      GSWiFi.begin((GS32WifiMode)WiFiMode, WiFiSTASSID, WiFiSTAPass, WiFiAPSSID, WiFiAPPass);
    }

    /**
      Код рабочий, но слишком часто вызывается, нужно подумать над кэшированием в будущем...
      Bios.addDynamicSelect("WiFi STA settings", "Scan & Select AP", &WiFiSelNet, [=]() { return GSWiFi.scanAvailableNetworks(); }, [=]() {
        Serial.printf("\n[GS32] [WIFI] Selected: %d -> %s", WiFiSelNet, FindWiFi[WiFiSelNet].c_str());
      });
    */
    
    
  #endif
}

void GS_Sensors(){
  #if HAS_SENSORS
    Bios.addPage("Sensors", "Advanced");         // Advanced -> Sensors
    Bios.addSubMenuAction("Advanced", "Configure Sensors...", "Sensors");

    for(int i=0; i<MAX_SENSORS; i++) {
      char ItemMenuSensor[32] = {0};
      snprintf(ItemMenuSensor, sizeof(ItemMenuSensor), "Sensor %d", i + 1);
      
      Bios.addPage(ItemMenuSensor, "Sensors");        // Advanced -> Sensors -> Sensors 1
      Bios.addSubMenuAction("Sensors", ItemMenuSensor,   ItemMenuSensor);

      Bios.addUInt8(ItemMenuSensor, "GPIO", &SensorPins[i], 1, 35);
      Bios.addSelect(ItemMenuSensor, "Type", &SensorTypes[i], 4, SensorsType);

      Bios.addDynamicInfoEx(ItemMenuSensor, "Result", [i]() -> String {
        if (SensorTypes[i] == 0 || SensorPins[i] == 0){
          return "Disabled";
        }
        GS32_SensorData data;
        // Запрашиваем актуальные данные из Хаба по ID
        if (GSensors.getSensorData(i, data)) {
          if (data.isValid) {
            char buf[32] = {0};
            snprintf(buf, sizeof(buf), "%.2f C | %.2f %%", data.temperature, data.humidity);
            return String(buf);
          }
          return "Reading..."; // Если данные еще не успели прочитаться (isValid = false)
        }
        return "Error";
      });

      if (SensorTypes[i] != 0 && SensorPins[i] != 0){
        ///! Если позиция не Disabled регистрируем датчик      
        Serial.printf("[GS32] [Sensors] Configurate pin: %d (mode=%d)\r\n", (int)SensorPins[i], (int)SensorTypes[i]);
        DHTType t = SensorsMap[SensorTypes[i]];
        GSensors.addDHT(t, SensorPins[i]);
      }

    }

  #endif
}

void GS_IR(){
  #if HAS_IR
    Bios.addPage("IR Receiver", "Advanced");         // Advanced -> IR
    Bios.addSubMenuAction("Advanced", "Configure IR Receiver...", "IR Receiver");

    Bios.addBool("IR Receiver", "Status", &GIRState);
    Bios.addInt8("IR Receiver", "GPIO", &GIRPIN, 1, 35);

    if (GIRState){
      Serial.printf("[GS32] [IR Receiver] Configurate pin: %d\r\n", (int)GIRPIN);
      IR.begin(GIRPIN, IR_PROT_ANY);
    }

  #endif
}

void GS_Audio(){
  #if HAS_AUDIO
    Bios.addPage("Audio", "Advanced");         // Advanced -> Audio
    Bios.addSubMenuAction("Advanced", "Configure Audio...", "Audio");
    Bios.addPage("Audio GPIO", "Audio");        // Advanced -> Audio -> Audio GPIO
    Bios.addPage("Equalizer", "Audio");        // Advanced -> Audio -> Equalizer
    Bios.addPage("Bluetooth", "Audio");       // Advanced -> Audio -> Bluetooth

    Bios.addSelect("Audio", "Enabled", &GAudioStatus, 4, GAudioModes);
    Bios.addInt("Audio", "Volume", &GAudioVolume, 0, 21);

    Bios.addUInt8("Audio GPIO", "GPIO BCK", &GAudioBCK, 1, 35);
    Bios.addUInt8("Audio GPIO", "GPIO RCK", &GAudioLRCK, 1, 35);
    Bios.addUInt8("Audio GPIO", "GPIO DIN", &GAudioDIN, 1, 35);

    Bios.addSelect("Equalizer", "Config", &GEQPreset, 6, GEQPresets);
    Bios.addInt8("Equalizer", "Custom LOW", &GEQLow, -12, 12);
    Bios.addInt8("Equalizer", "Custom MID", &GEQMid, -12, 12);
    Bios.addInt8("Equalizer", "Custom High", &GEQHigh, -12, 12);

    Bios.addText("Bluetooth", "Sink Device Name", GBTSinkName, 32, true);
    Bios.addText("Bluetooth", "Source Target", GBTSourceTarget, 32, true);

    Bios.addSubMenuAction("Audio", "GPIO settings...",   "Audio GPIO");
    Bios.addSubMenuAction("Audio", "Equalizer settings...",   "Equalizer");
    Bios.addSubMenuAction("Audio", "Bluetooth settings...",   "Bluetooth");
    
    Serial.printf("[GS32] [Audio] Configurate pin: %d,%d,%d\r\n", (int)GAudioBCK, (int)GAudioLRCK, (int)GAudioDIN);
    Audio.begin(GAudioBCK, GAudioLRCK, GAudioDIN);
    Audio.setVolume(GAudioVolume);
    Serial.printf("[GS32] [Audio] Configurate volume: %d\r\n", (int)GAudioVolume);
    Serial.printf("[GS32] [Audio] Configurate eq-mode: %d\r\n", (int)GEQPreset);
    if(GEQPreset == 5) 
       Audio.setCustomEqualizer(GEQLow, GEQMid, GEQHigh);
    else 
       Audio.setEqualizer((GS32EqualizerPreset)GEQPreset);
    
    Serial.printf("[GS32] [Audio] Configurate boot: %d\r\n", (int)GAudioStatus);
    if(GAudioStatus == 1) Audio.setMode(AUDIO_MODE_BT_SINK);
    else if(GAudioStatus == 3) Audio.setMode(AUDIO_MODE_AUX_OUT);
  #endif
}

void GS_ReportData(JsonObject custom) {
  #if HAS_REPORT
    custom["Token"] = GReportToken;
    #if HAS_SENSORS
      const auto& allSensors = GSensors.getAllSensorsData();
      for (const auto& sensor : allSensors) {
          if (sensor.isValid) {
              String sensorKey = "Sensor" + String(sensor.id);
              custom["sensors"][sensorKey]["id"]          = sensor.id;
              custom["sensors"][sensorKey]["pin"]         = sensor.pin;
              custom["sensors"][sensorKey]["temperature"] = sensor.temperature;
              custom["sensors"][sensorKey]["humidity"]    = sensor.humidity;
          } 
      }
    #endif
  #endif
}

void GS_ReportWeb(){
  #if HAS_REPORT
    Bios.addPage("ReportWeb", "Advanced");         // Advanced -> Sensors
    Bios.addSubMenuAction("Advanced", "Configure Report Web...", "ReportWeb");
    
    Bios.addBool("ReportWeb", "Status", &GReportEnabled);
    Bios.addText("ReportWeb", "Server address", GReportServer, 128, true);
    Bios.addText("ReportWeb", "Access token", GReportToken, 128, true);

    Bios.addBool("ReportWeb", "Chip ID", &GReportChipID);
    Bios.addBool("ReportWeb", "Chip Model", &GReportChipModel);
    Bios.addBool("ReportWeb", "Chip Core", &GReportChipCore);
    Bios.addBool("ReportWeb", "Chip Rev", &GReportChipRev);
    Bios.addBool("ReportWeb", "CPU Freq", &GReportCPUFreq);
    Bios.addBool("ReportWeb", "Heap", &GReportHeap);
    Bios.addBool("ReportWeb", "Min. Heap", &GReportMinHeap);
    Bios.addBool("ReportWeb", "Max. Heap", &GReportMaxHeap);
    Bios.addBool("ReportWeb", "Flash Size", &GReportFlashSize);
    Bios.addBool("ReportWeb", "Flash Speed", &GReportFlashSpeed);
    Bios.addBool("ReportWeb", "SDK Ver", &GReportSDKVer);
    Bios.addBool("ReportWeb", "FW Size", &GReportFWSize);
    Bios.addBool("ReportWeb", "FW Free", &GReportFWFree);
    Bios.addBool("ReportWeb", "Uptime", &GReportUptime);

    if (!GReportEnabled)return;
    Report.begin(GReportServer);
    
    Report.chipModel(GReportChipModel);
    Report.chipCores(GReportChipCore);
    Report.chipRevision(GReportChipRev);
    Report.cpuFreq(GReportCPUFreq);
    Report.chipId(GReportChipID);
    Report.heap(GReportHeap);
    Report.minHeap(GReportMinHeap);
    Report.maxAllocHeap(GReportMaxHeap);
    Report.flashSize(GReportFlashSize);
    Report.flashSpeed(GReportFlashSpeed);
    Report.sdkVersion(GReportSDKVer);
    Report.sketchSize(GReportFWSize);
    Report.freeSketch(GReportFWFree);
    Report.uptime(GReportUptime);

    Report.onCustomData(GS_ReportData);
  #endif
}

void setup() {
  Serial.println("\n");
  Serial.begin(115200);
  if (!Config.begin("GS32Bios")) {
    Serial.println("The NVS partition may be damaged! We recommend completely flashing the device and cleaning the NVS partition.");
    return;
  }

  Bios.setHeaderTitle("GS32");
  Bios.setProductInfo("FW8304", "v1.0.0");

  Bios.addPage("Advanced");                  // Страница верхнего уровня

  ///! Добавляем кнопку выхода из Bios (до перезапуска он больше отвечать не будет)
  Bios.addAction("Exit", "Exit the bios", []() {
    Bios.disable(); // Отключаем BIOS, освобождаем Serial и возвращаем видимый курсор
    Serial.println("\n--- You have exited GS32 mode. Restart the device to return. ---\n");
  });

  ///! Дополнительные события при сохранении настроек
  Bios.onSave([]() {
    Config.save();

    delay(3000);
  });

  ///! Дополнительные события при сбросе до заводских настроек
  Bios.onFactoryReset([]() {
    Config.factoryReset();
  });

  Bios.onKeyPress([](int key, const char* activePage) {
    if (key == '1') {
      Serial.printf("\r\nButton: %d | Page: %s\r\n", key, activePage);
    }
  });


  ///! Подгружаем данные о дате и времени сборки
  Config.addText("bldt", GBuildTime, sizeof(GBuildTime), COMP_TIME);
  Bios.addInfo("Build time", GBuildTime);

  ///! Подгружаем данные кнопок
  Config.addInt8("btn_1", &GBtn1, 12);
  Config.addInt8("btn_2", &GBtn2, 13);
  Config.addInt8("btn_3", &GBtn3, 14);

  ///! Подгружаем данные дисплея
  Config.addInt8("dsp_mosi", &GDspMOSI, 23);
  Config.addInt8("dsp_clk", &GDspCLK, 18);
  Config.addInt8("dsp_dc", &GDspDC, 2);
  Config.addInt8("dsp_rst", &GDspRST, 4);
  Config.addInt8("dsp_sc", &GDspSC, 5);
  Config.addInt8("dsp_x", &GDspOffX, 0);
  Config.addInt8("dsp_y", &GDspOffY, 0);

  ///! Подгружаем данные WiFi 
  Config.addInt("wifi_status", &WiFiMode, 0);
  Config.addInt("wifi_mode", &WiFiModeAuth, 0);
  Config.addText("wifi_sta_ssid", WiFiSTASSID, sizeof(WiFiSTASSID), "MyHomeWiFi");
  Config.addText("wifi_sta_pass", WiFiSTAPass, sizeof(WiFiSTAPass), "11112222");
  Config.addText("wifi_ap_ssid", WiFiAPSSID, sizeof(WiFiAPSSID), "FW8304");
  Config.addText("wifi_ap_pass", WiFiAPPass, sizeof(WiFiAPPass), "12345678");

  ///! Подгружаем данные Audio + Bluetooch
  Config.addUInt8("audio_bck", &GAudioBCK, 26);
  Config.addUInt8("audio_lrck", &GAudioLRCK, 25);
  Config.addUInt8("audio_din", &GAudioDIN, 22);
  Config.addInt("audio_status", &GAudioStatus, 0);
  Config.addInt("audio_volume", &GAudioVolume, 15);
  Config.addInt("audio_eq_m", &GEQPreset, 0);
  Config.addInt8("audio_eq_l", &GEQLow, 0);
  Config.addInt8("audio_eq_c", &GEQMid, 0);
  Config.addInt8("audio_eq_h", &GEQHigh, 0);
  Config.addText("bt_devname", GBTSinkName, sizeof(GBTSinkName), "FW8304");
  Config.addText("bt_source", GBTSourceTarget, sizeof(GBTSourceTarget), "");

  Config.addText("report_server", GReportServer, sizeof(GReportServer), "https://gs32.piminoff.ru/api/send/");
  Config.addText("report_token", GReportToken, sizeof(GReportToken), "");
  Config.addBool("report_status", &GReportEnabled, false);
  Config.addBool("report_chipmod", &GReportChipModel, false);
  Config.addBool("report_chipcore", &GReportChipCore, false);
  Config.addBool("report_chiprev", &GReportChipRev, false);
  Config.addBool("report_cpufreq", &GReportCPUFreq, false);
  Config.addBool("report_chipid", &GReportChipID, false);
  Config.addBool("report_heap", &GReportHeap, false);
  Config.addBool("report_minheap", &GReportMinHeap, false);
  Config.addBool("report_maxheap", &GReportMaxHeap, false);
  Config.addBool("report_flashsize", &GReportFlashSize, false);
  Config.addBool("report_flashspeed", &GReportFlashSpeed, false);
  Config.addBool("report_sdkver", &GReportSDKVer, false);
  Config.addBool("report_fwsize", &GReportFWSize, false);
  Config.addBool("report_fwfree", &GReportFWFree, false);
  Config.addBool("report_uptime", &GReportUptime, false);
  ///! Подгружаем данные датчиков
  for(int i=0; i<MAX_SENSORS; i++) {
    char ItemMenuConfig1[16] = {0};
    snprintf(ItemMenuConfig1, sizeof(ItemMenuConfig1), "sen_p%d", i);
    char ItemMenuConfig2[16] = {0};
    snprintf(ItemMenuConfig2, sizeof(ItemMenuConfig2), "sen_c%d", i);

    Config.addUInt8(ItemMenuConfig1, &SensorPins[i], 0);
    Config.addInt(ItemMenuConfig2,  &SensorTypes[i], 0);
  }

  
  Config.addBool("irrec_state", &GIRState, false);
  Config.addInt8("irrec_pin", &GIRPIN, 16);

  
  Config.load(); ///! Загружаем полностью конфигурацию из NVS
  GS_Buttons(); ///! Настройка кнопок
  GS_OTA();
  GS_Display();
  GS_WiFi();
  GS_Sensors();
  GS_IR();
  GS_Audio();
  GS_ReportWeb();
  /// Проверяем время сборки
  if (strcmp(GBuildTime, COMP_TIME) != 0) {
    Serial.printf("\r\nOLD VERSION: %s\r\nNEW VERSION: %s\r\n", GBuildTime, COMP_TIME);
    snprintf(GBuildTime, sizeof(GBuildTime), "%s", COMP_TIME);
    Config.save();
    delay(1000);
    ESP.restart();
  }

  Bios.begin();
}

void loop() {
  unsigned long currentMillis = millis();
  // Обязательный вызов обработчика BIOS в штатном режиме
  Bios.handle();
  #if HAS_BUTTONS
    GBtn.tick();
  #endif

  #if HAS_WIFI
    GSWiFi.handle();
  #endif

  #if HAS_SENSORS
    GSensors.tick();
  #endif
  
  #if HAS_IR
    IR.tick();
  #endif

  #if HAS_AUDIO
    Audio.tick();
  #endif

  #if HAS_REPORT
    #if HAS_WIFI
      if (GSWiFi.isConnected()){
        ///! Отправляем репорт только если есть WIFI + есть подключение + отправляем только раз в xx время
          if (currentMillis - lastSendTime >= sendInterval) {
            lastSendTime = currentMillis;
            Report.sendReport();
          }
      }
    #endif
  #endif
  #if HAS_DISPLAY
    // --- Логика циклической смены экранов (3 сек) ---
    if (currentMillis - lastDisplayUpdate >= 3000 || forceDisplayUpdate) {
      lastDisplayUpdate = currentMillis;
      forceDisplayUpdate = false;

      // Если мы не в режиме показа Chip ID, меняем экраны
      if (currentMillis > showChipIdUntil) {
        currentScreen++;
        int totalSensors = GSensors.getSensorCount();
        if (currentScreen > totalSensors) currentScreen = 0; 
      }
      
      updateDisplayUI();
    }

    // Быстрое мигание значка WiFi (каждые 500мс), если идет подключение
    static unsigned long lastBlink = 0;
    if (currentMillis - lastBlink > 500) {
      lastBlink = currentMillis;
      #if HAS_WIFI
      if (WiFi.status() == WL_DISCONNECTED && WiFiMode > 0 && WiFiMode != 2) {
        updateDisplayUI();
      }
      #endif
    }
  #endif
}