// [Các thư viện giữ nguyên như cũ]
#define BLYNK_TEMPLATE_ID "TMPL6Y4mtLhLM"
#define BLYNK_TEMPLATE_NAME "Do An"


#include <Wire.h> 
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <SPI.h>
#include <MFRC522.h>
#include <JQ6500_Serial.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <BlynkSimpleEsp32.h>


// [Khai báo và setup giữ nguyên]

HardwareSerial mySerial(2);
JQ6500_Serial mp3(mySerial);
#define SS_PIN_IN  5
#define RST_PIN_IN 4
MFRC522 rfid(SS_PIN_IN, RST_PIN_IN);

LiquidCrystal_I2C lcd(0x3F, 16, 2);

#define servo1Pin 16
#define servo2Pin 17
Servo servo1;
Servo servo2;

int pos1 = 0;
int pos2 = 0;
#define posClose 0
#define posOpen  90

#define inPos 32
#define outPos 13
#define buzzer 15
const int slots[3] = {26, 25, 33};
int carNum = 0;

String validCards[] = {"4b2af74", "EFE63EC3","D38F3416","23122433"};
#define NUM_CARDS (sizeof(validCards) / sizeof(validCards[0]))
bool uidInUse[NUM_CARDS] = {false};
String slotUIDs[3] = {"", "", ""};

String currentUIDIn = "";
String currentUIDOut = "";
bool waitingForCardOut[3] = {false, false, false};
int prevSlotState[3];
bool gateAuthorized = false;

char auth[] = "n-awG1yKA-D3VM6iS-BvQkiq_-CDkjiB";

// --- Thanh toán ---
const char* ssid = "bobop";
const char* password = "bobop10961011";
const char* webhook_url = "https://128ed1d2-e9ba-47a2-a20c-ed78b0a54972-00-hnok4hggh3qj.pike.replit.dev/latest";

unsigned long paymentStartTime = 0;
const unsigned long paymentTimeout = 2 * 60 * 1000;
bool waitingForPayment = false;
String lastTransactionId = "";

void setup() {
  Blynk.begin(auth, ssid, password);
  Serial.begin(115200);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(4, 0);
  lcd.print("Welcome!");
  delay(1000);
  lcd.clear();

  pinMode(buzzer, OUTPUT);
  digitalWrite(buzzer, HIGH);

  pinMode(inPos, INPUT_PULLUP);
  pinMode(outPos, INPUT_PULLUP);

  for (int i = 0; i < 3; i++) {
    pinMode(slots[i], INPUT_PULLUP);
    prevSlotState[i] = digitalRead(slots[i]);
  }

  mySerial.begin(9600, SERIAL_8N1, 27, 14);
  mp3.setVolume(30);

  servo1.setPeriodHertz(50);
  servo2.setPeriodHertz(50);
  servo1.attach(servo1Pin, 500, 2400);
  servo2.attach(servo2Pin, 500, 2400);

  servo1.write(posClose);
  servo2.write(posClose);
  pos1 = posClose;
  pos2 = posClose;
  gateAuthorized = false;

  SPI.begin();
  rfid.PCD_Init();

  WiFi.begin(ssid, password);
  Serial.print("Đang kết nối WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi đã kết nối!");
  beep(500, 1);
  resetWebhook();
  showLCD();
}

bool anyCarWaitingToExit() {
  for (int i = 0; i < 3; i++) {
    if (waitingForCardOut[i]) return true;
  }
  return false;
}

int checkRFIDIn() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return 0;
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) uid += String(rfid.uid.uidByte[i], HEX);
  rfid.PICC_HaltA();
  currentUIDIn = uid;
  for (int i = 0; i < NUM_CARDS; i++) {
    if (uid.equalsIgnoreCase(validCards[i])) {
      if (uidInUse[i]) return -2;
      uidInUse[i] = true;
      return 1;
    }
  }
  return -1;
}

int checkRFIDOut() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return 0;
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) uid += String(rfid.uid.uidByte[i], HEX);
  rfid.PICC_HaltA();
  currentUIDOut = uid;
  for (int i = 0; i < NUM_CARDS; i++) {
    if (uid.equalsIgnoreCase(validCards[i])) return 1;
  }
  return -1;
}
String nameOnly = "";  // Khai báo nameOnly là biến toàn cục
int amount = 0;  // Khai báo amount là biến toàn cục
void updateBlynkStatus() {
  static int lastCarNum = -1;
  static unsigned long lastUpdateTime = 0;
  static int lastSlotStatus[3] = {-1, -1, -1};
  static String lastGateIn = "", lastGateOut = "";
  static int lastRemaining = -1;

  unsigned long now = millis();
  if (now - lastUpdateTime < 3000) return;  // Chỉ cập nhật mỗi 3 giây
  lastUpdateTime = now;

  // 1. Số lượng xe
  if (carNum != lastCarNum) {
    Blynk.virtualWrite(V1, carNum);
    lastCarNum = carNum;
  }

  // 2. Thời gian thanh toán còn lại
  int remaining = 0;
  if (waitingForPayment) {
    unsigned long elapsed = millis() - paymentStartTime;
    remaining = (paymentTimeout > elapsed) ? (paymentTimeout - elapsed) / 1000 : 0;
  }
  if (remaining != lastRemaining) {
    Blynk.virtualWrite(V2, remaining);
    lastRemaining = remaining;
  }

  // 3. Tình trạng các slot
  for (int i = 0; i < 3; i++) {
    int state = (digitalRead(slots[i]) == LOW) ? 1 : 0;
    if (state != lastSlotStatus[i]) {
      Blynk.virtualWrite(V3 + i, state);
      lastSlotStatus[i] = state;
    }
  }

  // 4. Trạng thái cổng
  String gateInStatus = (pos1 == posOpen) ? "OPEN" : "CLOSE";
  if (gateInStatus != lastGateIn) {
    Blynk.virtualWrite(V6, gateInStatus);
    lastGateIn = gateInStatus;
  }

  String gateOutStatus = (pos2 == posOpen) ? "OPEN" : "CLOSE";
  if (gateOutStatus != lastGateOut) {
    Blynk.virtualWrite(V7, gateOutStatus);
    lastGateOut = gateOutStatus;
  }
}


void openGateIn() {
  if (pos1 == posClose) {
    for (int i = posClose; i < posOpen; i += 3) {
      servo2.write(i); delay(15);
    }
    servo2.write(posOpen); 
    pos1 = posOpen;
    lcd.clear(); lcd.print("Car Entered"); delay(2000); lcd.clear(); showLCD();
  }
}

void closeGateIn() {
  for (int i = posOpen; i > posClose; i -= 3) {
    servo2.write(i); delay(15);
  }
  servo2.write(posClose); 
  pos1 = posClose;
}

void openGateOut() {
  if (pos2 == posClose) {
    for (int i = posClose; i < posOpen; i += 3) {
      servo1.write(i); delay(15);
    }
    servo1.write(posOpen); 
    pos2 = posOpen;
    lcd.clear(); lcd.print("Car is out"); delay(2000); lcd.clear(); showLCD();
  }
}

void closeGateOut() {
  for (int i = posOpen; i > posClose; i -= 3) {
    servo1.write(i); delay(15);
  }
  servo1.write(posClose); 
  pos2 = posClose;
}

void beep(int d, int num) {
  for (int i = 0; i < num; i++) {
    digitalWrite(buzzer, LOW); delay(d);
    digitalWrite(buzzer, HIGH); delay(d);
  }
}

void showLCD() {
  carNum = 0;
  for (int n = 0; n < 3; n++) {
    if (n == 0) lcd.setCursor(0, 0), lcd.print("S1:");
    if (n == 1) lcd.setCursor(5, 0), lcd.print("S2:");
    if (n == 2) lcd.setCursor(10, 0), lcd.print("S3:");
    if (digitalRead(slots[n]) == LOW) { lcd.print("F"); carNum++; }
    else lcd.print("E");
  }
  lcd.setCursor(10, 1); lcd.print("CARS:"); lcd.print(carNum);
}

void resetWebhook() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(String(webhook_url) + "/reset");
    int httpCode = http.GET();
    
    if (httpCode == 200) {
      Serial.println("🔄 Webhook reset thành công");
    } else {
      Serial.printf("❌ Webhook reset lỗi: %d\n", httpCode);
    }
    
    http.end();
  }
}

bool fetchPaymentInfo() {
  static String lastNameSent = "";
  static int lastAmountSent = -1;

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(webhook_url);
    int httpCode = http.GET();

    if (httpCode == 200) {
      String payload = http.getString();
      Serial.println("📥 Dữ liệu từ webhook:");
      Serial.println(payload);

      if (payload.indexOf("No transaction yet") > -1) {
        Serial.println("⏳ Không có giao dịch mới.");
        http.end();
        return false;
      }

      DynamicJsonDocument doc(1024);
      DeserializationError error = deserializeJson(doc, payload);
      if (error) {
        Serial.print("❌ JSON lỗi: ");
        Serial.println(error.c_str());
        http.end();
        return false;
      }

      if (!doc.containsKey("id")) {
        Serial.println("⏳ Không có giao dịch hợp lệ.");
        http.end();
        return false;
      }

      String transactionId = String((long)doc["id"]);
      if (transactionId != lastTransactionId) {
        lastTransactionId = transactionId;
        int amount = doc["transferAmount"];
        String content = doc["content"];

        int secondSpacePos = content.indexOf(' ', content.indexOf(' ') + 1);
        String nameOnly = "";
        if (secondSpacePos != -1) {
          nameOnly = content.substring(secondSpacePos + 1);
        } else {
          nameOnly = content;
        }

        // So sánh dữ liệu cũ mới, tránh cập nhật Blynk không cần thiết
        if (nameOnly != lastNameSent || amount != lastAmountSent) {
         lcd.clear();
         lcd.setCursor(0, 0);
         lcd.print("DA NHAN: ");
         lcd.print(amount);
         lcd.setCursor(0, 1);
         lcd.print(nameOnly.substring(0, 16));
         Blynk.virtualWrite(V8, nameOnly);
          Blynk.virtualWrite(V9, amount);
          lastNameSent = nameOnly;
          lastAmountSent = amount;
        }

        Serial.println("✅ Giao dịch mới!");
                // ✅ Gửi dữ liệu lên Google Sheet
        HTTPClient sheetHttp;
        sheetHttp.begin("https://script.google.com/macros/s/AKfycbwvWMXvzUOsR4rveAFSleqb5Nxt0l8Kk3nBKB8T_QimTchM3k-uTKUm5pM8s-o_9AZ33Q/exec");
        sheetHttp.addHeader("Content-Type", "application/json");

        DynamicJsonDocument sheetDoc(256);
        sheetDoc["transactionId"] = transactionId;
        sheetDoc["amount"] = amount;
        sheetDoc["name"] = nameOnly;

        String sheetJson;
        serializeJson(sheetDoc, sheetJson);
        int sheetCode = sheetHttp.POST(sheetJson);
        if (sheetCode == 200) {
          Serial.println("✅ Đã gửi thông tin lên Google Sheet!");
        } else {
          Serial.printf("❌ Gửi Sheet lỗi: %d\n", sheetCode);
        }
        sheetHttp.end();

        resetWebhook();
        http.end();
        return true;
      } else {
        Serial.println("⏳ Không có giao dịch mới.");
      }
    } else {
      Serial.printf("❌ HTTP lỗi: %d\n", httpCode);
    }
    http.end();
  } else {
    Serial.println("⚠️ WiFi mất kết nối.");
  }
  return false;
}

void loop() {
  Blynk.run();
  if (pos1 != posOpen && digitalRead(inPos) != LOW && digitalRead(outPos) != LOW) showLCD();

  for (int i = 0; i < 3; i++) {
    int current = digitalRead(slots[i]);
    if (prevSlotState[i] == LOW && current == HIGH && slotUIDs[i] != "") {
      lcd.clear(); lcd.print("Quet the ra");
      waitingForCardOut[i] = true;
    }

    if (waitingForCardOut[i]) {
      int result = checkRFIDOut();
      if (result == 1 && currentUIDOut == slotUIDs[i]) {
        beep(700, 1);
        lcd.clear(); lcd.print("Dang xu ly TT");
        Serial.println("🟡 Quet the OK, cho thanh toan...");
        waitingForCardOut[i] = false;
        paymentStartTime = millis();
        waitingForPayment = true;
      } else if (result == 1) {
        lcd.clear(); lcd.print("Sai the!");
        beep(200, 2);
        delay(1500);
        lcd.clear(); lcd.print("Quet lai the");
      }
    }
    prevSlotState[i] = current;
  }

  if (waitingForPayment) {
    if (fetchPaymentInfo()) {
      gateAuthorized = true;
      waitingForPayment = false;

      for (int j = 0; j < NUM_CARDS; j++) {
        if (currentUIDOut.equalsIgnoreCase(validCards[j])) {
          uidInUse[j] = false;
          break;
        }
      }
      for (int i = 0; i < 3; i++) {
        if (slotUIDs[i] == currentUIDOut) {
          slotUIDs[i] = "";
          break;
        }
      }
      delay(3000);
      lcd.clear(); showLCD();
    } else if (millis() - paymentStartTime >= paymentTimeout) {
      lcd.clear(); lcd.print("Het thoi gian!");
      beep(200, 2);
      delay(1500);
      lcd.clear(); lcd.print("Quet lai the");

      // ✅ Fix: Cho quét lại thẻ sau timeout
      for (int i = 0; i < 3; i++) {
        if (slotUIDs[i] == currentUIDOut) {
          waitingForCardOut[i] = true;
          break;
        }
      }

      waitingForPayment = false;
    }
  }

  if (gateAuthorized && digitalRead(outPos) == LOW && pos2 == posClose) {
    mp3.playFileByIndexNumber(2);
    openGateOut();
  }

  if (pos2 == posOpen && digitalRead(outPos) == HIGH) {
    closeGateOut();
    gateAuthorized = false;
  }

  if (!gateAuthorized && !anyCarWaitingToExit() && digitalRead(inPos) == LOW) {
    int rfidIn = checkRFIDIn();
    if (rfidIn == 1) {
      if (carNum >= 3) {
        lcd.clear(); lcd.print("Car Full");
        lcd.setCursor(0, 1); lcd.print("Slot Unavailable");
        mp3.playFileByIndexNumber(3);
        delay(1500); lcd.clear(); showLCD();
        for (int i = 0; i < NUM_CARDS; i++) {
          if (currentUIDIn.equalsIgnoreCase(validCards[i])) {
            uidInUse[i] = false; // Bỏ đánh dấu "thẻ đang sử dụng"
            break;
          }
        }
      } else {
        lcd.clear(); lcd.print("Valid Card!");
        beep(700, 1);
        delay(1500); lcd.clear(); showLCD();
        for (int i = 0; i < 3; i++) {
          if (digitalRead(slots[i]) == HIGH) {
            slotUIDs[i] = currentUIDIn;
            lcd.clear();lcd.print("Moi xe vao bai");
            lcd.setCursor(0, 1);
            lcd.print(i+1);
            break;
          }
        }
        gateAuthorized = true;
      }
    } else if (rfidIn == -1) {
      lcd.clear(); lcd.print("Invalid Card!");
      beep(300, 3);
      delay(1500); lcd.clear(); showLCD();
    } else if (rfidIn == -2) {
      
      lcd.clear(); lcd.print("Card In Use!");
      beep(200, 2);
      delay(1500); lcd.clear(); showLCD();
    }
  }

  if (gateAuthorized && digitalRead(inPos) == LOW && pos1 == posClose) {
    mp3.playFileByIndexNumber(1);
    openGateIn();
  }

  if (pos1 == posOpen && digitalRead(inPos) == HIGH) {
    closeGateIn();
    gateAuthorized = false;
  }

  bool allSlotsEmpty = true;
  for (int i = 0; i < 3; i++) {
    if (slotUIDs[i] != "") {
      allSlotsEmpty = false;
      break;
    }
  }

  if (allSlotsEmpty) {
    for (int i = 0; i < 3; i++) waitingForCardOut[i] = false;
    for (int i = 0; i < NUM_CARDS; i++) uidInUse[i] = false;
    gateAuthorized = false;
  }
  updateBlynkStatus();
}