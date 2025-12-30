// ============================================
// HE THONG KHOA THONG MINH ESP32
// Tich hop RFID, Van tay, Keypad, LCD, Blynk
// ============================================

// Thong tin Blynk Template
#define BLYNK_TEMPLATE_ID "TMPL6s6cM6q7f"
#define BLYNK_TEMPLATE_NAME "Z Lab"
#define BLYNK_AUTH_TOKEN "xDkoVnT2TyDyVmzzMzUjlCOrjMnvOZuO"

// Thu vien su dung
#include <Adafruit_Fingerprint.h>  // Cam bien van tay AS608
#include <Wire.h>                   // I2C communication
#include <LiquidCrystal_I2C.h>      // LCD I2C
#include <MFRC522.h>                // RFID reader
#include <SPI.h>                    // SPI communication cho RFID
#include <WiFi.h>                   // WiFi
#include <BlynkSimpleEsp32.h>      // Blynk IoT platform
#include <EEPROM.h>                 // Luu tru du lieu
#include <Keypad.h>                 // Ban phim so
#include <freertos/FreeRTOS.h>     // FreeRTOS cho ESP32
#include <freertos/task.h>          // Task management
#include <freertos/queue.h>         // Queue cho inter-task communication
#include <time.h>                   // Thoi gian

// Thong tin ket noi Blynk va WiFi
char auth[] = BLYNK_AUTH_TOKEN;
char ssid[] = "ZET Third Floor2";
char pass[] = "ZET6868@";

// Dinh nghia chan cho cam bien van tay (AS608)
#define RX_PIN 16
#define TX_PIN 17

#define BUZZER_PIN 4    // Chan buzzer
#define LOCK_PIN 2      // Chan dieu khien khoa

HardwareSerial mySerial(2);             // UART2 cho cam bien van tay
Adafruit_Fingerprint finger(&mySerial);

// Dinh nghia chan cho RFID reader (MFRC522)
#define SS_PIN 5
#define RST_PIN 20
MFRC522 rfid(SS_PIN, RST_PIN);

// LCD I2C 16x2
LiquidCrystal_I2C lcd(0x27, 16, 2);
WidgetTerminal terminal(V0);        // Terminal widget trong Blynk

// Bien quan ly van tay va the RFID
uint8_t fingerID = 1;              // ID van tay tiep theo se them
String knownCards[100];            // Mang luu tru UID cua cac the RFID
int cardCount = 0;                 // So luong the RFID da luu

// Cac flag trang thai he thong
bool addCardMode = false;           // Che do them the RFID
bool deleteCardMode = false;        // Che do xoa the RFID
bool changePasswordMode = false;    // Che do doi mat khau
bool systemLocked = false;          // He thong bi khoa do nhieu lan sai
bool isAddingFingerprint = false;   // Dang them van tay

bool isOnline = false;                // Trang thai ket noi Blynk
bool expectingPinEntry = false;       // Dang cho nhap PIN/mat khau
bool offlineAdminModeActive = false;  // Che do admin offline dang hoat dong
uint8_t offlineAdminMenuPage = 0;     // Trang menu admin hien tai

// Flag va hang so cho thao tac the tu Blynk
bool blynkCardOpActive = false;                    // True neu thao tac them/xoa the tu Blynk dang cho quet the
unsigned long blynkCardOpStartTime = 0;            // Thoi diem bat dau thao tac the tu Blynk
const unsigned long BLYNK_CARD_OP_TIMEOUT = 15000; // Timeout 15 giay cho thao tac the tu Blynk

// Trang thai cam bien van tay
enum FingerprintMode 
{ 
  FINGER_IDLE, 
  FINGER_CHECK, 
  FINGER_ADD, 
  FINGER_DELETE 
};
FingerprintMode fingerprintMode = FINGER_CHECK;

// Cau hinh EEPROM
#define EEPROM_SIZE 512
#define PASSWORD_ADDR 400                        // Dia chi bat dau luu mat khau trong EEPROM
String defaultPassword = "8888";                 // Mat khau mac dinh
const String MASTER_OFFLINE_ADMIN_PIN = "2003";  // PIN admin de vao che do quan tri

// Bien quan ly nhap mat khau
String inputPassword = "";                     // Mat khau dang nhap
String displayPassword = "";                   // Mat khau hien thi tren LCD 
unsigned long lastKeyTime = 0;                 // Thoi diem nhan phim cuoi cung
const unsigned long PASSWORD_TIMEOUT = 15000;  // Timeout 15 giay cho nhap mat khau

// Cau hinh ban phim so 4x3
const byte ROWS = 4;
const byte COLS = 3;
char keys[ROWS][COLS] = {
  {'1', '2', '3'},
  {'4', '5', '6'},
  {'7', '8', '9'},
  {'*', '0', '#'}
};
byte rowPins[ROWS] = {12, 14, 27, 26};
byte colPins[COLS] = {25, 33, 32};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// Bien tam cho cac thao tac tu Blynk
String pendingCardUID = "";                 // UID the cho V2 (xoa theo UID)
String pendingFingerID = "";                // ID van tay cho V9 (xoa theo ID)
int failedAttempts = 0;                     // So lan nhap sai
unsigned long lastErrorTime = 0;            // Thoi diem loi cuoi cung
const unsigned long ERROR_COOLDOWN = 5000;  // Thoi gian cho giua cac lan bao loi
unsigned long lastV6TriggerTime = 0;        // Thoi diem trigger V6 cuoi cung
const unsigned long V6_DEBOUNCE = 10000;    // Debounce cho nut V6
int lastV6Value = 0;
bool v6Triggered = false;

// Queue va Task handle cho FreeRTOS
QueueHandle_t keypadQueue;                  // Queue cho keypad
QueueHandle_t rfidQueue;                    // Queue cho RFID
TaskHandle_t fingerprintTaskHandle = NULL;  // Bien luu dia chi cho ham xu ly van tay
unsigned long deleteModeStartTime = 0;      // Thoi diem bat dau che do xoa van tay
const unsigned long DELETE_TIMEOUT = 10000; // Timeout 10 giay cho che do xoa van tay

// Function prototype
void showMainMenu();
void addFingerprint_internal();
int enrollFingerprint(int id_to_enroll);
void showOfflineAdminMenuPage();
void handleOfflineAdminAction(byte page, char choice);
void checkBlynkCardOpTimeout();
void loadCardsFromEEPROM();
void loadPasswordFromEEPROM();
void savePasswordToEEPROM();
bool deleteCardFromEEPROM(String uid);
void clearEEPROM();
void listRFIDCards();
void checkRFID();
void deleteFingerprint(int id_to_delete); 



// Ham khoi tao he thong
void setup() {
  Serial.begin(115200);
  mySerial.begin(57600, SERIAL_8N1, RX_PIN, TX_PIN);  // Khoi tao Serial cho cam bien van tay
  finger.begin(57600);
  EEPROM.begin(EEPROM_SIZE);

  // Cau hinh chan buzzer
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Cau hinh chan cua khoa
  pinMode(LOCK_PIN, OUTPUT);
  digitalWrite(LOCK_PIN, LOW);

  // Cau hinh chan cho keypad
  for (byte i = 0; i < ROWS; i++) 
    pinMode(rowPins[i], INPUT_PULLUP);

  delay(500);
  
  lcd.init();
  lcd.backlight();

  // Ket noi WiFi
  showCentered("CONNECTING...", "PLEASE WAIT...");
  Serial.println("Attempting WiFi connection...");
  WiFi.begin(ssid, pass);
  unsigned long wifiConnectStart = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - wifiConnectStart < 15000)) 
  {
    delay(500); 
    Serial.print(".");
  }
  
  // Ket noi Blynk neu WiFi thanh cong
  if (WiFi.status() == WL_CONNECTED) 
  {
    Serial.println("\nWiFi Connected!"); 
    Serial.print("IP Address: "); 
    Serial.println(WiFi.localIP());

    Blynk.config(auth); // cau hinh blynk

    unsigned long blynkConnectStart = millis();
    bool blynkConnected = false;

    while (!blynkConnected && (millis() - blynkConnectStart < 10000)) 
    {
      blynkConnected = Blynk.connect(1000); 
      if (blynkConnected) 
        break; 
        delay(500);
    }
    if (blynkConnected) 
    {
      isOnline = true; 
      Serial.println("Blynk Connected!");
      terminal.println(getFormattedTime() + " >> SYSTEM :: ONLINE - BLYNK CONNECTED"); 
      terminal.flush();
      configTime(7 * 3600, 0, "pool.ntp.org");  // Cau hinh thoi gian GMT+7
    } 
    else 
    {
      isOnline = false; 
      Serial.println("Blynk Connection Failed!");
      showCentered("BLYNK FAILED", "OFFLINE MODE"); 
      delay(2000);
    }
  } 
  else 
  {
    isOnline = false; 
    Serial.println("\nWiFi Connection Failed!");
    showCentered("WIFI FAILED", "OFFLINE MODE"); 
    delay(2000);
  }
  if (!isOnline) 
  { 
    Serial.println(getFormattedTime() + " >> SYSTEM :: OFFLINE MODE"); 
  }

  // Khoi tao RFID reader
  SPI.begin();
  rfid.PCD_Init();
  
  byte version = rfid.PCD_ReadRegister(MFRC522::VersionReg); // kiem tra version
  if (version == 0x00 || version == 0xFF) 
  {
    Serial.println("WARNING: MFRC522 Communication failure. Check wiring.");
    showCentered("RFID ERROR", "CHECK WIRING");
    if(isOnline) 
    { 
      terminal.println(getFormattedTime() + " >> RFID :: INIT FAIL - CHECK WIRING"); 
      terminal.flush(); // hien thi tuc thi
    }
  } 
  else 
  {
    Serial.println(F("RFID Reader Initialized."));
    if(isOnline) 
    { 
      terminal.println(getFormattedTime() + " >> RFID :: INIT SUCCESS"); 
      terminal.flush(); 
    }
  }

  // Khoi tao cam bien van tay (AS608)
  int sensorRetries = 5;    // So lan duoc thu lai
  bool sensorReady = false;
  while (sensorRetries > 0 && !sensorReady) 
  {
    if (finger.verifyPassword()) 
    {
      beepSuccess(); 
      sensorReady = true; 
      Serial.println("Fingerprint Sensor Initialized");
      if(isOnline) 
      { 
        terminal.println(getFormattedTime() + " >> FINGERPRINT :: INIT SUCCESS"); 
        terminal.flush(); 
      }
    } 
    else 
    {
      Serial.println("Fingerprint Sensor Error. Retries Left: " + String(sensorRetries));
      sensorRetries--; 
      delay(1000);
    }
  }
  if (!sensorReady) 
  {
    showCentered("AS608 FAILED", "CHECK SENSOR");
    String errMsg = getFormattedTime() + " >> FINGERPRINT :: INIT FAIL - SENSOR ERROR";
    if (isOnline) 
    { 
      terminal.println(errMsg); 
      terminal.flush(); 
    } 
    else 
    { 
      Serial.println(errMsg + " (OFFLINE)"); 
    }
    while (1) 
    { 
      delay(1); // Dung he thong neu cam bien van tay loi
    } 
  }

  // Tai du lieu tu EEPROM
  loadCardsFromEEPROM();
  loadPasswordFromEEPROM();

  // Tao queue va task cho FreeRTOS
  keypadQueue = xQueueCreate(10, sizeof(char));
  rfidQueue = xQueueCreate(10, sizeof(String*));
  xTaskCreate(keypadTask, "KeypadTask", 2048, NULL, 6, NULL);
  xTaskCreate(rfidTask, "RFIDTask", 2560, NULL, 5, NULL);
  xTaskCreate(continuousFingerprintTask, "FingerprintTask", 4096, NULL, 5, &fingerprintTaskHandle);
  
  Serial.println("System setup complete. Tasks running.");
  delay(1000);
  showMainMenu();
}

// Ham loop chinh
void loop() {
  if (systemLocked) 
    return;  // Dung neu he thong bi khoa
  if (isOnline) 
    Blynk.run();  // Chay Blynk neu online
  
  checkKeypad();              // Kiem tra ban phim
  checkRFID();                // Kiem tra RFID
  checkBlynkCardOpTimeout();  // Kiem tra timeout cho thao tac the tu Blynk
}

// Ham lay thoi gian dinh dang
String getFormattedTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 1000)) {
    return "TIME N/A";
  }
  char timeString[20];
  strftime(timeString, sizeof(timeString), "%y-%m-%d %H:%M", &timeinfo);
  return String(timeString);
}

// Ham hien thi tin nhan tren LCD
void showCentered(String msg1, String msg2) 
{
  lcd.clear();
  msg1.toUpperCase();
  msg2.toUpperCase();
  int len1 = msg1.length();
  int pad1 = (16 - len1) / 2;
  lcd.setCursor(max(0, pad1), 0);
  lcd.print(msg1.substring(0, min(len1, 16)));
  int len2 = msg2.length();
  int pad2 = (16 - len2) / 2;
  lcd.setCursor(max(0, pad2), 1);
  lcd.print(msg2.substring(0, min(len2, 16)));
}

// Ham phat am thanh thanh cong
void beepSuccess() {
  tone(BUZZER_PIN, 1000, 100); 
  delay(120); 
  noTone(BUZZER_PIN);
}

// Ham phat am thanh that bai
void beepFailure() {
  tone(BUZZER_PIN, 300, 200); 
  delay(220); 
  noTone(BUZZER_PIN);
}

// Ham mo khoa cua
void unlockDoor() {
  digitalWrite(LOCK_PIN, HIGH);
  delay(3000);  // Mo khoa trong 3 giay
  digitalWrite(LOCK_PIN, LOW);
}

// Task doc ban phim trong FreeRTOS
void keypadTask(void *parameter) {
  for (;;) 
  {
    char key = keypad.getKey();
    if (key) 
    {
      xQueueSend(keypadQueue, &key, portMAX_DELAY);  // Gui phim vao queue
    }
    vTaskDelay(20 / portTICK_PERIOD_MS); // task tam nghi
  }
}

// Task doc RFID trong FreeRTOS
void rfidTask(void *parameter) 
{
  for (;;) 
  {
    if (rfid.PICC_IsNewCardPresent()) 
    {  // Kiem tra co the moi
      if (rfid.PICC_ReadCardSerial()) 
      {  // Doc serial cua the

        String uid = ""; // Chuyen doi UID sang chuoi hex
        for (byte i = 0; i < rfid.uid.size; i++) 
        {
          uid += String(rfid.uid.uidByte[i] < 0x10 ? "0" : ""); // Them so 0 neu byte nho hon 16
          uid += String(rfid.uid.uidByte[i], HEX);              // Chuyen byte sang dang HEX
        }
        uid.toUpperCase(); 

        rfid.PICC_HaltA();        // Dung giao tiep voi the
        rfid.PCD_StopCrypto1();   // Dung ma hoa

        String* uidPtr = new String(uid); // cap phat dong 1 vung nho cho bien uidPtr
         
        if (uidPtr) 
        {
            if (xQueueSend(rfidQueue, &uidPtr, pdMS_TO_TICKS(100)) != pdPASS) 
            { // neu gui that bai
                delete uidPtr; 
                Serial.println("RFID Task: Failed to send to queue");
            }
        } 
        else 
        {
            Serial.println("RFID Task: Failed to allocate memory for UID");
        }
      } 
    }
    vTaskDelay(50 / portTICK_PERIOD_MS); // task tam nghi
  }
}

// Task doc van tay lien tuc trong FreeRTOS
void continuousFingerprintTask(void *parameter) 
{
  for (;;) 
  {
    // Tam dung neu he thong bi khoa hoac dang them van tay hoac dang co thao tac the tu Blynk
    if (systemLocked || fingerprintMode == FINGER_ADD || isAddingFingerprint || blynkCardOpActive) 
    {
      vTaskDelay(100 / portTICK_PERIOD_MS);
      continue;
    }
    
    // Kiem tra timeout cho che do xoa van tay
    if (fingerprintMode == FINGER_DELETE && (millis() - deleteModeStartTime > DELETE_TIMEOUT)) 
    {
      Serial.println("FingerprintTask: Delete mode timed out");
      String msg = getFormattedTime() + " >> FINGERPRINT :: DELETE MODE TIMEOUT";
      if (isOnline) 
      { 
        terminal.println(msg); 
        terminal.flush(); 
      }
      else 
      {
        Serial.println(msg + " (OFFLINE)"); 
      }
      fingerprintMode = FINGER_CHECK; // Reset ve che do kiem tra
      showMainMenu(); 
    }

    // Doc van tay neu o che do kiem tra hoac xoa
    if (fingerprintMode == FINGER_CHECK || fingerprintMode == FINGER_DELETE) 
    {
      int p = finger.getImage();  // Yeu cau cam bien quet van tay
      if (p == FINGERPRINT_OK) 
      {
        if (fingerprintMode == FINGER_CHECK) 
          processFingerprint();  // Xu ly kiem tra van tay
        else if (fingerprintMode == FINGER_DELETE) 
          deleteFingerprintByScan();  // Xu ly xoa van tay
      } 
      else if (p != FINGERPRINT_NOFINGER) 
      {
        // Bao loi neu co loi khac ngoai khong co van tay
        if (millis() - lastErrorTime > ERROR_COOLDOWN) 
        {
           Serial.println("FingerprintTask: getImage failed with code " + String(p));
           String msg = getFormattedTime() + " >> FINGERPRINT :: GET_IMAGE ERROR - Code: " + String(p);
           if(isOnline) 
           {
            terminal.println(msg); 
            terminal.flush(); 
           } 
           else 
           {
            Serial.println(msg + " (OFFLINE)"); 
           }
           lastErrorTime = millis();
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
      }
    }
    vTaskDelay(50 / portTICK_PERIOD_MS); 
  }
}


/*******************************************
* CAC HAM BLYNK_WRITE - Xu ly lenh tu Blynk
* Duoc goi khi an nut tuong ung tren ap
*******************************************/

// V1: Liet ke cac the RFID (Online)
BLYNK_WRITE(V1) {
  if (!isOnline) { 
    Serial.println("V1: Ignored, offline."); 
    return; 
    }
  if (!expectingPinEntry && !offlineAdminModeActive && !blynkCardOpActive) {
    Serial.println("V1 Triggered: Listing RFID Cards");
    if(isOnline) 
      terminal.println(getFormattedTime() + " >> BLYNK V1 :: LISTING RFID CARDS...");

    listRFIDCards(); 

    if(isOnline) {
      terminal.println(getFormattedTime() + " >> BLYNK V1 :: CARD LISTING COMPLETE"); 
      terminal.flush();
    }
  } 
  else {
    Serial.println("V1 Ignored: System busy");
    if(isOnline) {
      terminal.println(getFormattedTime() + " >> BLYNK V1 :: IGNORED - SYSTEM BUSY"); 
      terminal.flush();
      }
  }
}

// V2: Xoa the theo UID (Online)
BLYNK_WRITE(V2) {
  if (!isOnline) { 
    Serial.println("V2: Ignored, offline."); 
    return; 
    }
  if (!expectingPinEntry && !offlineAdminModeActive && !blynkCardOpActive) { 
    pendingCardUID = param.asStr();  // lay gia tri UID duoc nhap tu app
    pendingCardUID.toUpperCase();
    if (pendingCardUID != "") {
      Serial.println("V2 Triggered: Deleting Card UID " + pendingCardUID);
      if (deleteCardFromEEPROM(pendingCardUID)) {
        if(isOnline) 
          terminal.println(getFormattedTime() + " >> BLYNK V2 :: CARD DELETE SUCCESS - UID: " + pendingCardUID);
        showCentered("CARD DELETED", "SUCCESS"); 
        beepSuccess();
      } 
      else {
        if(isOnline) {
          terminal.println(getFormattedTime() + " >> BLYNK V2 :: CARD DELETE FAIL - NOT FOUND: " + pendingCardUID);
          terminal.flush;
        }
        showCentered("CARD NOT FOUND", "CHECK UID"); 
        beepFailure();
      }

      pendingCardUID = ""; 
      delay(2000); 
      showMainMenu();
    }
  } 
  else {
    Serial.println("V2 Ignored: System busy");
    if(isOnline) {
      terminal.println(getFormattedTime() + " >> BLYNK V2 :: IGNORED - SYSTEM BUSY"); 
      terminal.flush();
      }
  }
}

// V3: Che do them the (Online - quet the)
BLYNK_WRITE(V3) {
    if (!isOnline) { 
      Serial.println("V3: Ignored, offline."); 
      return; 
      }
    // Kiem tra neu co che do khac dang hoat dong
    if (!expectingPinEntry && !offlineAdminModeActive && !addCardMode && !blynkCardOpActive && !deleteCardMode && !isAddingFingerprint && fingerprintMode == FINGER_CHECK) {
        addCardMode = true; 
        deleteCardMode = false;
        blynkCardOpActive = true;    // Danh dau thao tac the tu Blynk dang hoat dong
        blynkCardOpStartTime = millis(); // Bat dau dem thoi gian
        
        Serial.println("V3 Triggered: Add Card Mode (Swipe) - Waiting for card...");
        showCentered("ADD CARD (BLY)", "SWIPE CARD");
        if (isOnline) {
            terminal.println(getFormattedTime() + " >> BLYNK V3 :: ADD CARD MODE - SWIPE CARD WITHIN " + String(BLYNK_CARD_OP_TIMEOUT/1000) + "s");
            terminal.flush();
        }
        beepSuccess();
    } 
    else {
        Serial.println("V3 Ignored: System busy or already in a conflicting mode.");
        if (isOnline) {
            terminal.println(getFormattedTime() + " >> BLYNK V3 :: IGNORED - SYSTEM BUSY/MODE ACTIVE");
            terminal.flush();
        }
    }
}

// V4: Che do xoa the bang cach quet (Online)
BLYNK_WRITE(V4) {
    if (!isOnline) { 
      Serial.println("V4: Ignored, offline."); 
      return; 
      }
    if (!expectingPinEntry && !offlineAdminModeActive && !deleteCardMode && !blynkCardOpActive && !addCardMode && !isAddingFingerprint && fingerprintMode == FINGER_CHECK) {
        deleteCardMode = true; 
        addCardMode = false;
        blynkCardOpActive = true;
        blynkCardOpStartTime = millis();

        Serial.println("V4 Triggered: Delete Card Mode (Swipe) - Waiting for card...");
        showCentered("DELETE CARD (BLY)", "SWIPE CARD");
        if (isOnline) {
            terminal.println(getFormattedTime() + " >> BLYNK V4 :: DELETE CARD MODE - SWIPE CARD WITHIN " + String(BLYNK_CARD_OP_TIMEOUT/1000) + "s");
            terminal.flush();
        }
        beepSuccess();
    } 
    else {
        Serial.println("V4 Ignored: System busy or already in a conflicting mode.");
        if (isOnline) {
            terminal.println(getFormattedTime() + " >> BLYNK V4 :: IGNORED - SYSTEM BUSY/MODE ACTIVE");
            terminal.flush();
        }
    }
}

// V5: Xoa toan bo EEPROM (Online)
BLYNK_WRITE(V5) {
  if (!isOnline) { 
    Serial.println("V5: Ignored, offline."); 
    return; 
    }
  if (!expectingPinEntry && !offlineAdminModeActive && !blynkCardOpActive) {
    Serial.println("V5 Triggered: Clearing EEPROM");
    clearEEPROM(); 
    if (isOnline) {
      terminal.println(getFormattedTime() + " >> BLYNK V5 :: ALL DATA CLEARED - SYSTEM RESET"); 
      terminal.flush();
      }
    showCentered("ALL DATA CLEARED", "SYSTEM RESET"); 
    beepSuccess();
    delay(2000); 
    showMainMenu();
  } 
  else {
    Serial.println("V5 Ignored: System busy");
    if(isOnline) {
      terminal.println(getFormattedTime() + " >> BLYNK V5 :: IGNORED - SYSTEM BUSY"); 
      terminal.flush();
      }
  }
}

// V6: Them van tay (Online)
BLYNK_WRITE(V6) 
{
  if (!isOnline) 
  { 
    Serial.println("V6: Ignored, offline."); 
    return; 
  }
  int value = param.asInt(); // Lay gia tri tu nut nhan tren app
  if (value == lastV6Value && v6Triggered) 
  { 
    // Tranh trigger lai
    return; 
  } 

  if (millis() - lastV6TriggerTime < V6_DEBOUNCE && value != 0) 
  { 
    // Debounce
    return; 
  } 

  if (value == 1 && !expectingPinEntry && !offlineAdminModeActive && !isAddingFingerprint && fingerprintMode != FINGER_ADD && !blynkCardOpActive) 
  {
    v6Triggered = true;
    lastV6Value = value;
    isAddingFingerprint = true;
    fingerprintMode = FINGER_ADD;
    lastV6TriggerTime = millis();
    Serial.println("V6 Triggered: Starting Fingerprint Addition for ID " + String(fingerID));
    showCentered("ADD FINGER ID " + String(fingerID), "PLACE FINGER");
    if (isOnline) 
    {
      terminal.println(getFormattedTime() + " >> BLYNK V6 :: ADD FINGERPRINT ID " + String(fingerID) + " - PLACE FINGER"); 
      terminal.flush();
    }
    
    if(fingerprintTaskHandle != NULL) 
      vTaskSuspend(fingerprintTaskHandle);  // Tam dung task 

    addFingerprint_internal();  // Bat dau qua trinh them van tay

    if(fingerprintTaskHandle != NULL) 
      vTaskResume(fingerprintTaskHandle);   // Tiep tuc task 
    
    isAddingFingerprint = false;
    fingerprintMode = FINGER_CHECK;
    showMainMenu();
  } 
  else if (value == 0) {
    v6Triggered = false; 
    lastV6Value = 0;
  } 
  else {
    Serial.println("V6 Ignored: System busy/conditions not met.");
    if (isOnline) {
      terminal.println(getFormattedTime() + " >> BLYNK V6 :: IGNORED - SYSTEM BUSY"); 
      terminal.flush();
      }
  }
}

// V7: Xoa van tay bang cach quet (Online)
BLYNK_WRITE(V7) {
  if (!isOnline) { 
    Serial.println("V7: Ignored, offline."); 
    return; 
    }
  if (!expectingPinEntry && !offlineAdminModeActive && !blynkCardOpActive && !isAddingFingerprint) {
    int value = param.asInt();
    if (value == 1 && fingerprintMode != FINGER_DELETE) { 
      fingerprintMode = FINGER_DELETE;
      deleteModeStartTime = millis(); 
      Serial.println("V7 Triggered: Fingerprint Delete Mode Enabled (Scan)");
      showCentered("DELETE FINGER", "PLACE FINGER (BLY)");
      if (isOnline) {
        terminal.println(getFormattedTime() + " >> BLYNK V7 :: FINGERPRINT DELETE MODE (SCAN) - PLACE FINGER"); 
        terminal.flush();
        }
      beepSuccess();
    } 
    else if (value == 0 && fingerprintMode == FINGER_DELETE) { 
      fingerprintMode = FINGER_CHECK; // Huy che do neu nut bi tat
      if (isOnline) {
        terminal.println(getFormattedTime() + " >> BLYNK V7 :: FINGERPRINT DELETE MODE (SCAN) DEACTIVATED"); 
        terminal.flush();
        }
      showMainMenu();
      Serial.println("V7: Fingerprint Delete Mode Deactivated by Blynk");
    }
  } else {
    Serial.println("V7 Ignored: System busy or already in mode");
    if(isOnline) {
      terminal.println(getFormattedTime() + " >> BLYNK V7 :: IGNORED - SYSTEM BUSY/MODE ACTIVE"); 
      terminal.flush();
      }
  }
}

// V8: Mo khoa cua tu xa (Online)
BLYNK_WRITE(V8) {
  if (!isOnline) { 
    Serial.println("V8: Ignored, offline."); 
    return; 
    }
  if (!expectingPinEntry && !offlineAdminModeActive && !blynkCardOpActive) {
    int value = param.asInt();
    if (value == 1) {
      Serial.println("V8 Triggered: Remote Door Unlock");
      if (isOnline) {
        terminal.println(getFormattedTime() + " >> BLYNK V8 :: REMOTE UNLOCK - DOOR OPENED"); 
        terminal.flush();
        }
      showCentered("REMOTE UNLOCK", "DOOR OPENED");
      unlockDoor(); 
      beepSuccess(); 
      delay(2000); 
      showMainMenu();
    }
  } 
  else {
    Serial.println("V8 Ignored: System busy");
    if(isOnline) {
      terminal.println(getFormattedTime() + " >> BLYNK V8 :: IGNORED - SYSTEM BUSY"); 
      terminal.flush();
      }
  }
}

// V9: Xoa van tay theo ID (Online)
BLYNK_WRITE(V9) {
  if (!isOnline) { 
    Serial.println("V9: Ignored, offline."); 
    return; 
    }
  if (!expectingPinEntry && !offlineAdminModeActive && !blynkCardOpActive) {
    pendingFingerID = param.asStr(); // Lay gia tri ID tu blynk
    if (pendingFingerID != "") {
      Serial.println("V9 Triggered: Deleting Fingerprint ID " + pendingFingerID);
      int id = pendingFingerID.toInt();
      deleteFingerprint(id); 
      pendingFingerID = "";
    }
  } 
  else {
    Serial.println("V9 Ignored: System busy");
    if(isOnline) {
      terminal.println(getFormattedTime() + " >> BLYNK V9 :: IGNORED - SYSTEM BUSY"); 
      terminal.flush();
      }
  }
}

// V10: Doi mat khau tu Blynk
BLYNK_WRITE(V10) {
  if (!isOnline) { 
    Serial.println("V10: Ignored, offline."); 
    return; 
    }
  if (!expectingPinEntry && !offlineAdminModeActive && !blynkCardOpActive) {
    String newPassword = param.asStr();
    Serial.println("V10 Triggered: Changing Password to " + newPassword);
    if (newPassword.length() >= 4 && newPassword.length() <= 8) {
      defaultPassword = newPassword;
      savePasswordToEEPROM();
      if (isOnline) 
        terminal.println(getFormattedTime() + " >> BLYNK V10 :: USER PASSWORD CHANGED");

      showCentered("PASSWORD CHANGED", "SUCCESS (BLY)"); 
      beepSuccess();
    } 
    else {
      if (isOnline) 
        terminal.println(getFormattedTime() + " >> BLYNK V10 :: INVALID NEW PASSWORD - MUST BE 4-8 CHARS");

      showCentered("INVALID PASSWORD", "4-8 CHARS (BLY)"); 
      beepFailure();
    }
    if (isOnline) 
      terminal.flush(); 
    
    delay(2000); 
    showMainMenu();
  } 
  else {
    Serial.println("V10 Ignored: System busy");
    if(isOnline) {
      terminal.println(getFormattedTime() + " >> BLYNK V10 :: IGNORED - SYSTEM BUSY"); 
      terminal.flush();
      }
  }
}


// Ham hien thi menu chinh
void showMainMenu() {
  changePasswordMode = false;
  isAddingFingerprint = false; 

  // Chi reset che do the neu khong dang cho thao tac the tu Blynk
  if (!blynkCardOpActive) {
    addCardMode = false;
    deleteCardMode = false;
  }

  // Reset che do xoa van tay neu timeout hoac quay ve menu chinh
  if (fingerprintMode == FINGER_DELETE) {
      if (millis() - deleteModeStartTime > DELETE_TIMEOUT) {
          fingerprintMode = FINGER_CHECK;
      }
  } else {
      fingerprintMode = FINGER_CHECK; 
  }
  
  expectingPinEntry = false; 
  offlineAdminModeActive = false; 
  offlineAdminMenuPage = 0;   
  inputPassword = "";
  displayPassword = "";

  if (isOnline) {
    showCentered("SMART LOCKDOOR", "SCAN OR USE KEY");
  } else {
    showCentered("OFFLINE MODE", "SCAN OR USE KEY"); 
  }
  Serial.println("Displayed Main Menu. Mode: " + String(isOnline ? "Online" : "Offline") +
                 ", FP Mode: " + String(fingerprintMode) + 
                 ", AddCard: " + String(addCardMode) + 
                 ", DelCard: " + String(deleteCardMode) +
                 ", BlynkCardOp: " + String(blynkCardOpActive)
                 );
}

// Ham khoa he thong khi nhieu lan nhap sai
void lockoutSystem() {
  systemLocked = true;
  showCentered("SYSTEM LOCKED", "WAIT 30 SEC");
  String logMsg = getFormattedTime() + " >> SYSTEM :: LOCKED - TOO MANY FAILED ATTEMPTS";
  if (isOnline) { 
    terminal.println(logMsg); 
    terminal.flush(); 
    } 
    else { 
      Serial.println(logMsg + " (OFFLINE)"); 
      }
  
  unsigned long lockoutEndTime = millis() + 30000;  // Khoa trong 30 giay
  while (millis() < lockoutEndTime) {
    tone(BUZZER_PIN, 500, 200); 
    delay(800); 
    noTone(BUZZER_PIN); // Phat am thanh bao hieu
    if (isOnline) 
      Blynk.run(); // Giu ket noi Blynk neu co the
  }
  systemLocked = false; failedAttempts = 0; 
  if (isOnline) { 
    terminal.println(getFormattedTime() + " >> SYSTEM :: UNLOCKED - LOCKOUT PERIOD ENDED"); 
    terminal.flush(); 
    }
  else { 
    Serial.println(getFormattedTime() + " >> SYSTEM :: UNLOCKED (OFFLINE)");
    }
  showMainMenu(); 
}

// Ham hien thi trang menu admin offline
void showOfflineAdminMenuPage() {
  offlineAdminModeActive = true;
  inputPassword = ""; 
  displayPassword = "";
  lastKeyTime = millis(); // Reset timeout cho dieu huong menu

  Serial.print("Showing Offline Admin Menu Page: "); 
  Serial.println(offlineAdminMenuPage);
  if (isOnline) { 
    terminal.println(getFormattedTime() + " >> ADMIN :: MENU PAGE " + String(offlineAdminMenuPage)); 
    terminal.flush(); 
    }

  switch (offlineAdminMenuPage) {
    case 1: showCentered("1.ADD FINGER", "2.DEL FINGER(#>)"); break;  // Trang 1: Quan ly van tay
    case 2: showCentered("1.ADD RFID", "2.DEL RFID (#>)"); break;     // Trang 2: Quan ly the RFID
    case 3: showCentered("1.CHG USER PASS", "2.RESET PASS (*X)"); break;  // Trang 3: Quan ly mat khau
    default:
      offlineAdminModeActive = false;
      offlineAdminMenuPage = 0;
      showMainMenu();
      break;
  }
}

// Ham xu ly hanh dong trong menu admin offline
void handleOfflineAdminAction(byte page, char choice) {
  Serial.print("Admin Action: Page "); 
  Serial.print(page); 
  Serial.print(", Choice "); 
  Serial.println(choice);
  lastKeyTime = millis(); // Reset timeout
  String adminActionLog;

  switch (page) {
    case 1: // Trang quan ly van tay
      if (choice == '1') { // Them van tay
        adminActionLog = " >> ADMIN :: ADD FINGERPRINT SELECTED";
        Serial.println("Admin: Add Fingerprint selected.");

        if(fingerprintTaskHandle != NULL) 
          vTaskSuspend(fingerprintTaskHandle);

        isAddingFingerprint = true;   
        fingerprintMode = FINGER_ADD;  
        showCentered("ADD FINGER ID " + String(fingerID), "PLACE FINGER");

        if(isOnline) { 
          terminal.println(getFormattedTime() + adminActionLog + " - ID " + String(fingerID)); 
          terminal.flush(); 
          }
        else { 
          Serial.println(adminActionLog + " - ID " + String(fingerID) + " (OFFLINE)");
          }

        addFingerprint_internal(); 
        isAddingFingerprint = false;
        fingerprintMode = FINGER_CHECK;

        if(fingerprintTaskHandle != NULL) 
          vTaskResume(fingerprintTaskHandle);

        showOfflineAdminMenuPage(); 
      } 
      else if (choice == '2') { // Xoa van tay bang cach quet
        adminActionLog = " >> ADMIN :: DELETE FINGERPRINT BY SCAN SELECTED";
        Serial.println("Admin: Delete Fingerprint by scan selected.");
        fingerprintMode = FINGER_DELETE;
        deleteModeStartTime = millis(); 
        showCentered("DELETE FINGER", "PLACE FINGER");

        if(isOnline) { 
          terminal.println(getFormattedTime() + adminActionLog); 
          terminal.flush(); 
          }
        else { 
          Serial.println(adminActionLog + " (OFFLINE)");}
        // continuousFingerprintTask se xu ly phan con lai va goi showMainMenu
      } 
      else { 
        beepFailure(); 
        showOfflineAdminMenuPage(); }
      break;

    case 2: // Trang quan ly the RFID
      if (choice == '1') { // Them the RFID
        adminActionLog = " >> ADMIN :: ADD RFID CARD SELECTED - SWIPE CARD";
        Serial.println("Admin: Add Card selected.");
        addCardMode = true;
        deleteCardMode = false;
        showCentered("ADMIN ADD CARD", "SWIPE CARD NOW");

        if(isOnline) { 
          terminal.println(getFormattedTime() + adminActionLog); 
          terminal.flush(); 
          }
        else { 
          Serial.println(adminActionLog + " (OFFLINE)");
          }
        // checkRFID se xu ly, sau do goi showMainMenu
      } 
      else if (choice == '2') { // Xoa the RFID bang cach quet
        adminActionLog = " >> ADMIN :: DELETE RFID CARD BY SWIPE SELECTED - SWIPE CARD";
        Serial.println("Admin: Delete Card by swipe selected.");
        deleteCardMode = true;
        addCardMode = false;
        showCentered("ADMIN DEL CARD", "SWIPE CARD NOW");

        if(isOnline) { 
          terminal.println(getFormattedTime() + adminActionLog); 
          terminal.flush(); 
          }
        else { 
          Serial.println(adminActionLog + " (OFFLINE)");}
        // checkRFID se xu ly, sau do goi showMainMenu
      } 
      else { 
        beepFailure(); 
        showOfflineAdminMenuPage(); }
      break;

    case 3: // Trang quan ly mat khau
      if (choice == '1') { // Doi mat khau nguoi dung
        adminActionLog = " >> ADMIN :: CHANGE USER PASSWORD SELECTED";
        Serial.println("Admin: Change User Password selected.");
        changePasswordMode = true; 
        offlineAdminModeActive = false; // Tam thoi thoat admin de nhap mat khau
        offlineAdminMenuPage = 0;
        inputPassword = ""; displayPassword = "";
        showCentered("NEW USER PWD:", ""); 

        if(isOnline) { 
          terminal.println(getFormattedTime() + adminActionLog); 
          terminal.flush(); 
          }
        else { 
          Serial.println(adminActionLog + " (OFFLINE)");
          }
        // Logic keypad cho changePasswordMode se xu ly
      } 
      else if (choice == '2') { // Reset mat khau ve mac dinh
        adminActionLog = " >> ADMIN :: USER PASSWORD RESET TO DEFAULT (8888)";
        Serial.println("Admin: Reset User Password selected.");
        defaultPassword = "8888";
        savePasswordToEEPROM();
        if(isOnline) { 
          terminal.println(getFormattedTime() + adminActionLog); 
          terminal.flush(); 
          }
        else { 
          Serial.println(adminActionLog + " (OFFLINE)"); 
          }
        showCentered("PWD RESET TO", "DEFAULT (8888)"); 
        beepSuccess();
        delay(2000);
        showOfflineAdminMenuPage(); 
      } 
      else { 
        beepFailure(); 
        showOfflineAdminMenuPage(); 
        }
      break;
      
    default:
      beepFailure();
      showOfflineAdminMenuPage(); 
      break;
  }
}

// Ham kiem tra va xu ly ban phim
void checkKeypad() {
  char key;
  if (xQueueReceive(keypadQueue, &key, 0) == pdTRUE) {  // Nhan phim tu queue
    Serial.print("Key pressed: "); Serial.println(key);
    lastKeyTime = millis();

    if (systemLocked) { 
      beepFailure(); 
      return; 
      }  // Bo qua neu he thong bi khoa
    if (blynkCardOpActive) { // Bo qua neu dang co thao tac the tu Blynk
        Serial.println("Keypad ignored: Blynk card operation active.");
        return;
    }

    // 1. Xu ly nhap PIN/Mat khau (sau khi nhan *)
    if (expectingPinEntry) {
      beepSuccess(); 
      if (key == '#') {  // Xac nhan mat khau
        Serial.print("PIN/Password entered: "); 
        Serial.println(inputPassword);
        String enteredPin = inputPassword;
        inputPassword = ""; 
        displayPassword = "";

        if (enteredPin == MASTER_OFFLINE_ADMIN_PIN) {  // PIN admin
          Serial.println("Admin PIN correct. Entering Admin Menu.");
          if(isOnline) { 
            terminal.println(getFormattedTime() + " >> KEYPAD :: ADMIN LOGIN SUCCESS"); 
            terminal.flush(); 
            }
          expectingPinEntry = false;
          offlineAdminModeActive = true;
          offlineAdminMenuPage = 1; 
          showOfflineAdminMenuPage();
        } 
        else if (enteredPin == defaultPassword) {  // Mat khau nguoi dung
          Serial.println("User password correct. Unlocking door.");
          String msg = getFormattedTime() + " >> KEYPAD :: USER LOGIN SUCCESS - DOOR OPENED";

          if(isOnline) { 
            terminal.println(msg); 
            terminal.flush(); 
            } 
          else { 
              Serial.println(msg + " (OFFLINE)"); 
              }
          showCentered("ACCESS GRANTED", "DOOR UNLOCKED");
          unlockDoor();
          failedAttempts = 0; beepSuccess();
          delay(2000);
          showMainMenu();
        } 
        else {  // Mat khau sai
          Serial.println("Incorrect PIN/Password.");
          String msg = getFormattedTime() + " >> KEYPAD :: LOGIN FAIL - INCORRECT PIN/PASSWORD";
          if(isOnline) { 
            terminal.println(msg); 
            terminal.flush(); 
            } 
          else { 
            Serial.println(msg + " (OFFLINE)"); 
            }
          failedAttempts++;
          showCentered("ACCESS DENIED", "ATTEMPS: " + String(max(0, 5 - failedAttempts)));
          beepFailure();
          if (failedAttempts >= 5) { 
            lockoutSystem(); 
            return; 
            }  // Khoa he thong neu sai 5 lan
          delay(2000);
          expectingPinEntry = false;
          showMainMenu(); 
        }
      } else if (key == '*') {  // Reset mat khau dang nhap
        inputPassword = ""; displayPassword = "";
        showCentered("MAT KHAU:", displayPassword);
        Serial.println("PIN entry reset by *.");

      } else if (isdigit(key)) {  // Nhap so
        if (inputPassword.length() < 8) { 
          inputPassword += key; displayPassword += "*";
          showCentered("MAT KHAU:", displayPassword);
        } else { 
          beepFailure(); 
          } 
      } else { 
        beepFailure(); 
        }
      return; 
    }

    // 2. Xu ly dieu huong menu admin offline
    if (offlineAdminModeActive) {
      if (key == '#') { // Chuyen trang tiep theo
        beepSuccess();
        offlineAdminMenuPage++;
        if (offlineAdminMenuPage > 3) offlineAdminMenuPage = 1;
        showOfflineAdminMenuPage();
      } else if (key == '*') { // Thoat menu admin
        beepSuccess();
        Serial.println("Exiting Admin Menu via *.");
        if(isOnline) { 
          terminal.println(getFormattedTime() + " >> ADMIN :: EXIT MENU"); 
          terminal.flush(); 
          }
        offlineAdminModeActive = false;
        offlineAdminMenuPage = 0;
        showMainMenu();
      } else if (isdigit(key) && (key == '1' || key == '2')) { // Chon chuc nang trong menu
         beepSuccess(); 
         handleOfflineAdminAction(offlineAdminMenuPage, key);
      } else {
        beepFailure(); 
        showOfflineAdminMenuPage();
      }
      return; 
    }

    // 3. Xu ly nhap mat khau moi (tu menu admin)
    if (changePasswordMode) {
      beepSuccess();
      if (key == '*') {  // Reset mat khau dang nhap
        inputPassword = ""; displayPassword = "";
        showCentered("NEW USER PWD:", "");
        Serial.println("New user password entry reset by *.");
      } else if (key == '#') {  // Xac nhan mat khau moi
        if (inputPassword.length() >= 4 && inputPassword.length() <= 8) {
          defaultPassword = inputPassword;
          savePasswordToEEPROM();
          String msg = getFormattedTime() + " >> KEYPAD :: NEW USER PASSWORD SET (ADMIN)";
          if(isOnline) { 
            terminal.println(msg); 
            terminal.flush(); 
            } 
          else { 
            Serial.println(msg + " (OFFLINE)"); 
            }
          showCentered("PWD CHANGED", "SUCCESS (ADMIN)"); beepSuccess();
          Serial.println("User password changed successfully to: " + defaultPassword);
        } else {
          String msg = getFormattedTime() + " >> KEYPAD :: NEW USER PASSWORD FAIL - INVALID LENGTH (4-8 CHARS)";
          if(isOnline) { 
            terminal.println(msg); 
            terminal.flush(); 
            } 
          else 
          { Serial.println(msg + " (OFFLINE)");
          }
          showCentered("INVALID PWD", "4-8 CHARS REQ"); beepFailure();
          Serial.println("Invalid new password length.");
        }
        delay(2000);
        changePasswordMode = false; 
        inputPassword = ""; displayPassword = "";
        showMainMenu(); 
      } else if (isdigit(key)) {  // Nhap so
        if (inputPassword.length() < 8) {
          inputPassword += key; displayPassword += "*";
          showCentered("NEW USER PWD:", displayPassword);
        } else { beepFailure(); } 
      } else { 
        beepFailure(); 
      }
      return; 
    }

    // 4. Nhan * de vao che do nhap PIN/Mat khau (tu trang thai cho)
    if (key == '*' && !expectingPinEntry && !offlineAdminModeActive && !changePasswordMode) {
      beepSuccess();
      expectingPinEntry = true;
      inputPassword = "";
      displayPassword = "";
      showCentered("MAT KHAU:", ""); 
      Serial.println("'*' pressed. Entering PIN/Password mode.");
      if(isOnline) { 
        terminal.println(getFormattedTime() + " >> KEYPAD :: PIN ENTRY MODE ACTIVATED"); 
        terminal.flush(); 
        }
    }
  }

  // Logic timeout cho cac che do nhap (PIN, Admin Menu, Doi mat khau)
  if ((expectingPinEntry || offlineAdminModeActive || changePasswordMode) &&
      !blynkCardOpActive &&
      (millis() - lastKeyTime > PASSWORD_TIMEOUT)) {
    
    String modeTimedOutStr = expectingPinEntry ? "PIN ENTRY" :
                             (offlineAdminModeActive ? "ADMIN MENU" :
                             (changePasswordMode ? "CHANGE PWD" : "INPUT"));
    
    Serial.println(modeTimedOutStr + " timed out due to inactivity.");
    String msg = getFormattedTime() + " >> KEYPAD :: " + modeTimedOutStr + " TIMEOUT";
    if (isOnline) { 
      terminal.println(msg); 
      terminal.flush(); 
      }
    else { 
      Serial.println(msg + " (OFFLINE)"); 
      }
    
    showCentered(modeTimedOutStr, "TIMED OUT");
    beepFailure();
    delay(2000);
    showMainMenu(); 
  }
}

// Ham kiem tra timeout cho thao tac the tu Blynk
void checkBlynkCardOpTimeout() {
    if (blynkCardOpActive && (millis() - blynkCardOpStartTime > BLYNK_CARD_OP_TIMEOUT)) {
        String modeStr = addCardMode ? "ADD CARD" : (deleteCardMode ? "DELETE CARD" : "CARD OP");
        Serial.println("Blynk " + modeStr + " mode timed out. No card swiped.");
        if (isOnline) {
            terminal.println(getFormattedTime() + " >> BLYNK :: " + modeStr + " TIMEOUT - NO CARD SWIPED");
            terminal.flush();
        }
        showCentered(modeStr + " (BLY)", "TIMED OUT");
        beepFailure();

        // Reset tat ca flag lien quan
        addCardMode = false;
        deleteCardMode = false;
        blynkCardOpActive = false;

        delay(2000);
        showMainMenu();
    }
}

// ============================================
// CAC HAM XU LY VAN TAY, RFID, EEPROM
// ============================================

// Ham xu ly kiem tra van tay
void processFingerprint() {
  int p = finger.image2Tz();  // Chuyen doi anh thanh template
  if (p != FINGERPRINT_OK) {
    if (millis() - lastErrorTime > ERROR_COOLDOWN) {
      Serial.println("processFingerprint: Failed to convert image, code " + String(p));
      String msg = getFormattedTime() + " >> FINGERPRINT :: IMAGE CONVERT FAIL - Code: " + String(p);
      if(isOnline){ 
        terminal.println(msg); 
        terminal.flush(); 
        } 
      else { 
        Serial.println(msg + " (OFFLINE)");
        }
      showCentered("FP SCAN ERROR", "TRY AGAIN"); lastErrorTime = millis();
    }
    beepFailure(); 
    delay(1000); 
    showMainMenu(); 
    return;
  }
  
  p = finger.fingerFastSearch();  // Tim kiem van tay trong database
  if (p == FINGERPRINT_OK) {
    int fid = finger.fingerID;
    String logMsg = getFormattedTime() + " >> FINGERPRINT :: ACCESS GRANTED - ID: " + String(fid) + ", Confidence: " + String(finger.confidence);

    if (isOnline) { 
      terminal.println(logMsg); 
      terminal.flush(); 
      } 
    else { 
      Serial.println(logMsg + " (OFFLINE)"); 
      }
    showCentered("ACCESS GRANTED", "FINGER ID: " + String(fid));
    unlockDoor(); 
    failedAttempts = 0; 
    beepSuccess();
    Serial.println("processFingerprint: Access Granted, ID " + String(fid));
  } else {
    String reason = (p == FINGERPRINT_NOTFOUND) ? "NOT FOUND" : ((p == FINGERPRINT_PACKETRECIEVEERR) ? "COMM ERR" : "UNKNOWN ERR (" + String(p) + ")");
    String logMsg = getFormattedTime() + " >> FINGERPRINT :: ACCESS DENIED - Reason: " + reason;

    if (isOnline) { 
      terminal.println(logMsg); 
      terminal.flush(); 
      } 
    else { 
      Serial.println(logMsg + " (OFFLINE)"); 
      }
    failedAttempts++;
    showCentered("ACCESS DENIED", "ATTEMPTS: " + String(max(0,5 - failedAttempts))); 
    beepFailure();
    Serial.println("processFingerprint: Access Denied, Code: " + String(p));
    if (failedAttempts >= 5) { 
      lockoutSystem(); 
      return; 
      }
  }
  delay(2000); 
  showMainMenu();
}

// Ham xoa van tay bang cach quet
void deleteFingerprintByScan() {
  Serial.println("deleteFingerprintByScan: Image captured. Converting...");
  int p = finger.image2Tz(); 
  if (p != FINGERPRINT_OK) {
    if (millis() - lastErrorTime > ERROR_COOLDOWN) {
      Serial.println("deleteFingerprintByScan: Failed to convert image, code " + String(p));
      String msg = getFormattedTime() + " >> FINGERPRINT :: DELETE SCAN - IMAGE CONVERT FAIL - Code: " + String(p);
      if(isOnline){ 
        terminal.println(msg); 
        terminal.flush(); 
        } 
      else { 
        Serial.println(msg + " (OFFLINE)");
        }
      showCentered("FP SCAN ERROR", "TRY AGAIN"); lastErrorTime = millis();
    }
    beepFailure(); 
    delay(1000); 
    fingerprintMode = FINGER_CHECK; 
    showMainMenu(); 
    return;
  }
  
  Serial.println("deleteFingerprintByScan: Searching for match to delete...");
  p = finger.fingerFastSearch();
  if (p == FINGERPRINT_OK) {
    int fid = finger.fingerID;
    Serial.println("deleteFingerprintByScan: Match found, ID " + String(fid) + ". Deleting...");
    if (finger.deleteModel(fid) == FINGERPRINT_OK) {
      String logMsg = getFormattedTime() + " >> FINGERPRINT :: DELETE SCAN SUCCESS - ID " + String(fid) + " DELETED";
      if (isOnline) { terminal.println(logMsg); terminal.flush(); } else { Serial.println(logMsg + " (OFFLINE)"); }
      showCentered("FINGER DELETED", "ID: " + String(fid)); 
      beepSuccess();
      Serial.println("deleteFingerprintByScan: Success, ID " + String(fid));
    } else {
      String logMsg = getFormattedTime() + " >> FINGERPRINT :: DELETE SCAN FAIL - SENSOR ERROR ON DELETE ID " + String(fid);
      if (isOnline) { terminal.println(logMsg); terminal.flush(); } else { Serial.println(logMsg + " (OFFLINE)"); }
      showCentered("DELETE FAILED", "SENSOR ERROR?"); beepFailure();
      Serial.println("deleteFingerprintByScan: Failed to delete ID " + String(fid) + " from sensor.");
    }
  } else {
    String reason = (p == FINGERPRINT_NOTFOUND) ? "NOT FOUND" : "SCAN ERR (" + String(p) + ")";
    String logMsg = getFormattedTime() + " >> FINGERPRINT :: DELETE SCAN FAIL - FINGER " + reason;
    if (isOnline) { terminal.println(logMsg); terminal.flush(); } else { Serial.println(logMsg + " (OFFLINE)"); }
    showCentered("FINGER NOT FOUND", "TRY AGAIN"); beepFailure();
    Serial.println("deleteFingerprintByScan: Fingerprint not found, Code: " + String(p));
  }
  delay(2000); 
  fingerprintMode = FINGER_CHECK; 
  showMainMenu();
}

// Ham them van tay (noi bo)
void addFingerprint_internal() 
{
  Serial.println("addFingerprint_internal: Process for ID " + String(fingerID));
  String opContext = offlineAdminModeActive ? "ADMIN" : (isOnline ? "BLYNK V6" : "OFFLINE");
  String termPrefix = getFormattedTime() + " >> FINGERPRINT ("+opContext+") ID " + String(fingerID) + " :: "; // log cho blynk app
  String serialPrefix = "FP Internal ("+opContext+") ID " + String(fingerID) + ": "; // log cho serial

  if (!finger.verifyPassword()) { 
      if (millis() - lastErrorTime > ERROR_COOLDOWN) {
         Serial.println(serialPrefix + "Sensor comm error pre-enroll.");
         if(isOnline) 
          terminal.println(termPrefix + "ADD FAIL - SENSOR COMM ERROR PRE-ENROLL");
         lastErrorTime = millis();
      }
      showCentered("SENSOR ERROR", "CHECK WIRING"); 
      beepFailure(); 
      delay(2000);
      return; 
  }

  int p = enrollFingerprint(fingerID); // enrollFingerprint xu ly LCD, beep, va log chi tiet

  if (p == FINGERPRINT_OK) {
    if (isOnline) { 
      terminal.println(termPrefix + "ADD SUCCESS"); 
      } 
    else { 
      Serial.println(serialPrefix + "ADD SUCCESS (OFFLINE)"); 
      }
    Serial.println(serialPrefix + "Success. Next available ID: " + String(fingerID + 1));
    fingerID++; 
  } 
  else {
    if (isOnline) { 
      terminal.println(termPrefix + "ADD FAIL - Enroll Error Code: " + String(p)); 
      } 
    else { 
      Serial.println(serialPrefix + "ADD FAIL - Enroll Error Code: " + String(p) + " (OFFLINE)"); 
      }
    Serial.println(serialPrefix + "Failed, error " + String(p));
  }
  delay(2500);
}

// Ham dang ky van tay (2 lan quet)
int enrollFingerprint(int id_to_enroll) {
  int p = -1;
  String opContext = offlineAdminModeActive ? "ADMIN" : (isOnline ? "BLYNK V6" : "FP ENROLL");
  String logPrefixBase = "FINGERPRINT ("+opContext+") ID " + String(id_to_enroll) + " :: ENROLL - ";
  String termPrefix = getFormattedTime() + " >> " + logPrefixBase;
  String serialPrefix = logPrefixBase;

#define SEND_LOG(msg_suffix) \
    do { \
        if(isOnline) { \
          terminal.println(termPrefix + msg_suffix); \
          Serial.println(serialPrefix + msg_suffix + (isOnline ? "" : " (OFFLINE)")); \
          terminal.flush(); \
        } \
    } while(0)

  // Lan quet thu nhat
  SEND_LOG("Waiting for finger (1st scan)...");
  showCentered("ENROLL ID " + String(id_to_enroll), "PLACE FINGER");
  unsigned long startTime = millis();
  while (p != FINGERPRINT_OK && (millis() - startTime < 15000)) { 
    p = finger.getImage();
    if (p == FINGERPRINT_NOFINGER) { 
      vTaskDelay(50 / portTICK_PERIOD_MS); 
      continue; 
      }
    if (p != FINGERPRINT_OK) {
      SEND_LOG("GetImage1 Error: " + String(p));
      showCentered("SCAN ERROR 1", "CODE: " + String(p)); 
      beepFailure(); 
      return p;
    }
  }
  if (p != FINGERPRINT_OK) { 
    SEND_LOG("Timeout Image1");
    showCentered("TIMEOUT IMG 1", "TRY AGAIN"); 
    beepFailure(); 
    return FINGERPRINT_TIMEOUT;
  }
  SEND_LOG("Image 1 taken.");
  p = finger.image2Tz(1);  // Chuyen doi anh lan 1
  if (p != FINGERPRINT_OK) {
    SEND_LOG("Convert1 Error: " + String(p));
    showCentered("CONVERT ERR 1", "CODE: " + String(p)); 
    beepFailure(); 
    return p;
  }
  SEND_LOG("Image 1 converted. Remove finger.");
  showCentered("REMOVE FINGER", "WAIT..."); 
  beepSuccess(); 
  delay(2000); 
  
  // Cho nguoi dung bo tay ra
  startTime = millis(); 
  p = 0; 
  while (p != FINGERPRINT_NOFINGER && (millis() - startTime < 10000)) { 
    p = finger.getImage(); 
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
  if (p != FINGERPRINT_NOFINGER) {
    SEND_LOG("Removal Timeout/Error: " + String(p));
    showCentered("REMOVAL TIMEOUT", "TRY AGAIN"); 
    beepFailure(); 
    return FINGERPRINT_TIMEOUT; 
  }
  
  // Lan quet thu hai
  SEND_LOG("Finger removed. Place same finger again (2nd scan)...");
  showCentered("PLACE SAME FINGER", "AGAIN"); 
  beepSuccess();
  
  startTime = millis(); 
  p = -1;
  while (p != FINGERPRINT_OK && (millis() - startTime < 15000)) { 
    p = finger.getImage();
    if (p == FINGERPRINT_NOFINGER) { 
      vTaskDelay(50 / portTICK_PERIOD_MS); 
      continue; 
      }
    if (p != FINGERPRINT_OK) {
      SEND_LOG("GetImage2 Error: " + String(p));
      showCentered("SCAN ERROR 2", "CODE: " + String(p)); 
      beepFailure(); 
      return p;
    }
  }
  if (p != FINGERPRINT_OK) { 
    SEND_LOG("Timeout Image2");
    showCentered("TIMEOUT IMG 2", "TRY AGAIN"); 
    beepFailure(); 
    return FINGERPRINT_TIMEOUT;
  }
  SEND_LOG("Image 2 taken.");
  p = finger.image2Tz(2);  // Chuyen doi anh lan 2
  if (p != FINGERPRINT_OK) {
    SEND_LOG("Convert2 Error: " + String(p));
    showCentered("CONVERT ERR 2", "CODE: " + String(p)); 
    beepFailure(); 
    return p;
  }
  
  // Tao model tu 2 lan quet
  SEND_LOG("Image 2 converted. Creating model...");
  p = finger.createModel();  // so sanh 2 mau van tay va tao model
  if (p != FINGERPRINT_OK) {
    SEND_LOG("CreateModel Error: " + String(p) + (p == FINGERPRINT_ENROLLMISMATCH ? " - FINGERPRINTS DID NOT MATCH!" : ""));
    if (p == FINGERPRINT_ENROLLMISMATCH) {
      showCentered("NO MATCH", "FINGERS DIFFER?");
    } 
    else { 
      showCentered("MODEL ERR", "CODE: " + String(p)); 
      }
    beepFailure(); 
    return p;
  }
  
  // Luu model vao sensor
  SEND_LOG("Model created. Storing at ID: " + String(id_to_enroll));
  p = finger.storeModel(id_to_enroll);  // luu model vua tao vao
  if (p != FINGERPRINT_OK) {
    SEND_LOG("StoreModel Error: " + String(p));
    showCentered("STORE ERROR", "CODE: " + String(p)); 
    beepFailure(); 
    return p;
  }
  SEND_LOG("Model stored successfully!");
  showCentered("FINGER ADDED", "ID: " + String(id_to_enroll)); 
  beepSuccess();
  return FINGERPRINT_OK; 
#undef SEND_LOG // xoa define
}

// Ham xoa van tay theo ID
void deleteFingerprint(int id_to_delete) {
  Serial.println("deleteFingerprint: Attempting to delete ID " + String(id_to_delete));
  String opContext = "FINGERPRINT";
  if (pendingFingerID != "") opContext = "BLYNK V9";
  else if (offlineAdminModeActive) opContext = "ADMIN";

  String termPrefix = getFormattedTime() + " >> " + opContext + " ID " + String(id_to_delete) + " :: ";
  String serialPrefix = opContext + " ID " + String(id_to_delete) + ": ";

  if (id_to_delete < 1 || id_to_delete > 199) { 
      Serial.println(serialPrefix + "Invalid Fingerprint ID for deletion.");
      if (isOnline) terminal.println(termPrefix + "DELETE FAIL - INVALID ID (1-199)");
      showCentered("INVALID ID", "RANGE 1-199"); beepFailure(); delay(2000); showMainMenu(); return;
  }
  if (finger.deleteModel(id_to_delete) == FINGERPRINT_OK) {
    if (isOnline) { terminal.println(termPrefix + "DELETE SUCCESS"); } 
    else { Serial.println(serialPrefix + "DELETE SUCCESS (OFFLINE)"); }
    showCentered("FINGER DELETED", "ID: " + String(id_to_delete)); beepSuccess();
    Serial.println(serialPrefix + "Delete success.");
  } else {
    if (isOnline) { terminal.println(termPrefix + "DELETE FAIL - SENSOR ERROR/NOT FOUND"); } 
    else { Serial.println(serialPrefix + "DELETE FAIL - SENSOR ERROR/NOT FOUND (OFFLINE)"); }
    showCentered("DELETE FAILED", "CHECK ID/SENSOR"); beepFailure();
    Serial.println(serialPrefix + "Delete failed.");
  }
  if(isOnline) terminal.flush();
  delay(2000); showMainMenu(); 
}

// Ham kiem tra va xu ly RFID
void checkRFID() {
  String* uidPtr = NULL; 
  if (xQueueReceive(rfidQueue, &uidPtr, 0) == pdTRUE && uidPtr != NULL) {  // Nhan UID tu queue
    String uid = *uidPtr; delete uidPtr;   
    Serial.print("Processing RFID UID from queue: "); 
    Serial.println(uid);
    beepSuccess(); 
    
    // Xac dinh nguyen nhan thao tac
    String opContext = "RFID";
    if (addCardMode && blynkCardOpActive) opContext = "BLYNK V3 ADD";
    else if (deleteCardMode && blynkCardOpActive) opContext = "BLYNK V4 DELETE";
    else if (addCardMode && offlineAdminModeActive) opContext = "ADMIN ADD";
    else if (deleteCardMode && offlineAdminModeActive) opContext = "ADMIN DELETE";
    else opContext = "RFID ACCESS";

    String termPrefix = getFormattedTime() + " >> " + opContext + " :: ";
    String serialPrefix = opContext + ": ";

    if (addCardMode) {  // Che do them the
      bool alreadyExists = false;
      for(int i=0; i < cardCount; i++)
      { 
        if(knownCards[i] == uid)
        { 
          alreadyExists = true; 
          break; 
        } 
      }
      
      if(alreadyExists){
        if (isOnline) { 
          terminal.println(termPrefix + "ADD FAIL - CARD ALREADY EXISTS - UID: " + uid); 
          }
        Serial.println(serialPrefix + "Card already exists: " + uid + (isOnline ? "" : " (OFFLINE)"));
        showCentered("CARD EXISTS", uid.substring(0, min((int)uid.length(), 8))); 
        beepFailure();
      } 
      else if (cardCount < 100) {
        knownCards[cardCount] = uid; 
        saveCardToEEPROM(cardCount, uid); 
        if (isOnline) { 
          terminal.println(termPrefix + "ADD SUCCESS - UID: " + uid); 
          }
        Serial.println(serialPrefix + "Card added: " + uid + (isOnline ? "" : " (OFFLINE)")); 
        cardCount++;
        showCentered("CARD ADDED", uid.substring(0, min((int)uid.length(), 8))); 
        beepSuccess();
      } 
      else { 
        if (isOnline) { 
          terminal.println(termPrefix + "ADD FAIL - STORAGE FULL - UID: " + uid); 
          }
        Serial.println(serialPrefix + "Card storage full, cannot add: " + uid + (isOnline ? "" : " (OFFLINE)"));
        showCentered("STORAGE FULL", "CANNOT ADD"); 
        beepFailure();
      }
      addCardMode = false; 
      if(blynkCardOpActive) blynkCardOpActive = false;
    
    } 
    else if (deleteCardMode) {  // Che do xoa the
      if (deleteCardFromEEPROM(uid)) { 
        if (isOnline) { 
          terminal.println(termPrefix + "DELETE SUCCESS - UID: " + uid); 
          }
        Serial.println(serialPrefix + "Card deleted: " + uid + (isOnline ? "" : " (OFFLINE)"));
        showCentered("CARD DELETED", uid.substring(0, min((int)uid.length(), 8))); 
        beepSuccess();
      } 
      else { 
        if (isOnline) { 
          terminal.println(termPrefix + "DELETE FAIL - CARD NOT FOUND - UID: " + uid); 
          }
        Serial.println(serialPrefix + "Card not found to delete: " + uid + (isOnline ? "" : " (OFFLINE)"));
        showCentered("CARD NOT FOUND", "CHECK UID"); 
        beepFailure();
      }
      deleteCardMode = false; 
      if(blynkCardOpActive) 
        blynkCardOpActive = false;

    } else {  // Kiem tra quyen truy cap binh thuong
      if (isCardKnown(uid)) { 
        if (isOnline) { terminal.println(termPrefix + "ACCESS GRANTED - UID: " + uid); }
        Serial.println(serialPrefix + "Valid card, access granted: " + uid + (isOnline ? "" : " (OFFLINE)"));
        showCentered("ACCESS GRANTED", "CARD OK"); unlockDoor(); failedAttempts = 0; beepSuccess();
      } else { 
        if (isOnline) { terminal.println(termPrefix + "ACCESS DENIED - INVALID CARD - UID: " + uid); }
        Serial.println(serialPrefix + "Invalid card, access denied: " + uid + (isOnline ? "" : " (OFFLINE)"));
        failedAttempts++; showCentered("ACCESS DENIED", "ATTEMPTS: " + String(max(0,5-failedAttempts))); beepFailure();
        if (failedAttempts >= 5) { lockoutSystem(); return; }
      }
    }
    if (isOnline) terminal.flush();
    delay(2000);
    showMainMenu(); 
  }
}

// Ham kiem tra the co trong danh sach khong
bool isCardKnown(String uid) {
  for (int i = 0; i < cardCount; i++) if (knownCards[i] == uid) return true;
  return false;
}

// Ham luu the vao EEPROM
void saveCardToEEPROM(int index, String uid) {
  int addr = index * 10;  // Moi the chiem 10 byte
  Serial.print("EEPROM Save Card: Index " + String(index) + ", UID: " + uid + " at Addr: " + String(addr));
  for (int i = 0; i < 10; i++) {
    if (i < uid.length()) EEPROM.write(addr + i, uid[i]);
    else EEPROM.write(addr + i, 0); 
  }
  if (EEPROM.commit()) Serial.println(" ...Committed.");
  else Serial.println(" ...Commit FAILED.");
}

// Ham tai the tu EEPROM
void loadCardsFromEEPROM() {
  cardCount = 0; Serial.println("EEPROM Load Cards: Loading cards...");
  int clearedSlots = 0;
  for (int i = 0; i < 100; i++) {
    String uid = ""; int addr = i * 10; bool validCardCharFound = false; 
    for (int j = 0; j < 10; j++) {
      char c = EEPROM.read(addr + j);
      if (c == 0 || (c == 0xFF && j == 0)) break; 
      uid += c; validCardCharFound = true;
    }
    if (uid.length() > 0 && validCardCharFound && uid.length() <= 8) { 
      knownCards[cardCount++] = uid; Serial.println("EEPROM Load Cards: Loaded Card[" + String(i) + "]: " + uid);
    } else if (uid.length() > 0 || (EEPROM.read(addr) != 0xFF && EEPROM.read(addr) != 0x00) ) {
       // Tim thay du lieu khong chuan hoac bi hong
       bool needsClearing = false;
       for(int k=0; k < 10; ++k) if(EEPROM.read(addr+k) != 0x00 && EEPROM.read(addr+k) != 0xFF) { needsClearing = true; break; }
       if(needsClearing) {
           Serial.println("EEPROM Load Cards: Slot " + String(i) + " (Addr: " + String(addr) + ") contains non-standard data (" + uid + "). Clearing slot.");
           for (int k=0; k < 10; k++) EEPROM.write(addr+k, 0);
           clearedSlots++;
       }
    }
  }
  if(clearedSlots > 0) {
    if(EEPROM.commit()) Serial.println("EEPROM Load Cards: Committed clearing of " + String(clearedSlots) + " corrupted slots.");
    else Serial.println("EEPROM Load Cards: FAILED to commit clearing of corrupted slots.");
  }
  Serial.println("EEPROM Load Cards: Total cards loaded: " + String(cardCount));
  if(isOnline && cardCount > 0) { terminal.println(getFormattedTime() + " >> EEPROM :: " + String(cardCount) + " CARDS LOADED"); terminal.flush(); }
}

// Ham xoa the khoi EEPROM
bool deleteCardFromEEPROM(String uid) {
  int foundIndex = -1;
  for (int i = 0; i < cardCount; i++)
  {
    if (knownCards[i] == uid) 
    { 
      foundIndex = i; 
      break; 
      }
  } 
  
  if (foundIndex != -1) {
    Serial.println("EEPROM Delete Card: UID " + uid + " found at index " + String(foundIndex) + ". Deleting.");
    int addr = foundIndex * 10;
    for (int j = 0; j < 10; j++) EEPROM.write(addr + j, 0); 
    
    // Dich chuyen cac the con lai trong mang
    for (int k = foundIndex; k < cardCount - 1; k++) knownCards[k] = knownCards[k+1];
    knownCards[cardCount - 1] = ""; // Xoa phan tu cuoi
    cardCount--;

    if(EEPROM.commit()) Serial.println("EEPROM Delete Card: Commit SUCCESS. New count: " + String(cardCount));
    else Serial.println("EEPROM Delete Card: Commit FAIL. UID " + uid);
    return true;
  }
  Serial.println("EEPROM Delete Card: UID " + uid + " not found for deletion.");
  return false;
}

// Ham luu mat khau vao EEPROM
void savePasswordToEEPROM() {
  Serial.print("EEPROM Save Password: " + defaultPassword);
  for (int i = 0; i < 8; i++) { 
    if (i < defaultPassword.length()) EEPROM.write(PASSWORD_ADDR + i, defaultPassword[i]);
    else EEPROM.write(PASSWORD_ADDR + i, 0); 
  }
  if(EEPROM.commit()) Serial.println(" ...Committed.");
  else Serial.println(" ...Commit FAILED.");
}

// Ham tai mat khau tu EEPROM
void loadPasswordFromEEPROM() {
  Serial.print("EEPROM Load Password: Loading... ");
  String pwd = ""; bool validPwdCharFound = false;
  for (int i = 0; i < 8; i++) {
    char c = EEPROM.read(PASSWORD_ADDR + i);
    if (c == 0 || (c == 0xFF && i == 0)) break;
    pwd += c; validPwdCharFound = true;
  }
  if (pwd.length() >= 4 && pwd.length() <= 8 && validPwdCharFound) {
    defaultPassword = pwd; Serial.println("Loaded: " + defaultPassword);
    if(isOnline) { terminal.println(getFormattedTime() + " >> EEPROM :: USER PASSWORD LOADED"); terminal.flush(); }
  } else {
    Serial.println("No valid password found or invalid length. Using default '8888' and saving.");
    defaultPassword = "8888"; savePasswordToEEPROM(); 
    if(isOnline) { terminal.println(getFormattedTime() + " >> EEPROM :: USER PASSWORD RESET TO DEFAULT (8888)"); terminal.flush(); }
  }
}

// Ham xoa toan bo EEPROM
void clearEEPROM() {
  Serial.println("EEPROM Clear: Clearing all Cards & Password...");
  // Xoa vung luu the (0 den PASSWORD_ADDR - 1) va vung mat khau
  for (int i = 0; i < PASSWORD_ADDR + 8; i++) EEPROM.write(i, 0); 
  
  bool commitSuccess = EEPROM.commit(); 
  if(commitSuccess) Serial.println("EEPROM Clear: Memory commit SUCCESS.");
  else Serial.println("EEPROM Clear: Memory commit FAIL.");

  cardCount = 0; 
  for(int i=0; i<100; i++) knownCards[i] = ""; 
  
  // Xoa database van tay trong sensor
  Serial.println("EEPROM Clear: Attempting to clear fingerprint sensor database...");
  String fpClearMsg;
  if(finger.verifyPassword()){
    if(finger.emptyDatabase() == FINGERPRINT_OK){
      fpClearMsg = " >> FINGERPRINT :: SENSOR DATABASE CLEARED";
      Serial.println("Fingerprint DB cleared.");
    } else {
      fpClearMsg = " >> FINGERPRINT :: SENSOR DATABASE CLEAR FAIL";
      Serial.println("Failed to clear FP DB from sensor.");
    }
  } else {
    fpClearMsg = " >> FINGERPRINT :: SENSOR COMM ERROR ON CLEAR DB";
    Serial.println("FP Sensor comm error on attempting to clear DB.");
  }
  if(isOnline) { terminal.println(getFormattedTime() + fpClearMsg); }
  else { Serial.println(fpClearMsg + " (OFFLINE)"); }
  
  fingerID = 1; 
  defaultPassword = "8888"; savePasswordToEEPROM(); // Luu lai mat khau mac dinh
  
  Serial.println("EEPROM Clear: All data reset to defaults.");
  if(isOnline && commitSuccess) { terminal.println(getFormattedTime() + " >> EEPROM :: ALL DATA CLEARED AND RESET"); terminal.flush(); }
  else if (isOnline && !commitSuccess) { terminal.println(getFormattedTime() + " >> EEPROM :: DATA CLEAR ATTEMPTED (COMMIT FAIL)"); terminal.flush(); }
}

// Ham liet ke cac the RFID ra terminal Blynk
void listRFIDCards() {
  if (!isOnline) {
    Serial.println("Offline. Cannot list cards to Blynk terminal."); 
    showCentered("OFFLINE", "CANNOT LIST CARDS");
    delay(2000); showMainMenu(); return;
  }
  terminal.println("==== STORED RFID CARDS (" + String(cardCount) + ") ====");
  if (cardCount == 0) terminal.println("NO CARDS STORED");
  else {
    bool any = false;
    for (int i = 0; i < cardCount; i++) {
      if (knownCards[i].length() > 0) {
        terminal.println("CARD[" + String(i) + "]: " + knownCards[i]); 
        any = true; 
      }
    }
    if (!any && cardCount > 0) terminal.println("INFO: Card count is " + String(cardCount) + " but no valid UIDs found in array (potential data issue).");
    else if (!any && cardCount == 0) terminal.println("INFO: No cards currently stored.");
  }
  terminal.println("============================"); terminal.flush();
}