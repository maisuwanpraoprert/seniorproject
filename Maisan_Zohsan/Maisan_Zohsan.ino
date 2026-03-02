#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <WebServer.h>

const char* ssid = "maipromax";
const char* password = "17sep2003";
WebServer server(80);

LiquidCrystal_I2C lcd(0x27, 20, 4);

//Pin
const int m1_pul = 4;
const int m1_dir = 5;
const int m1_ena = 27; 
const int m2_pul = 22;
const int m2_dir = 23;
const int m2_ena = 13; 

const int radiationPin = 16;
const int btnUp = 12, btnDown = 14, btnSelect = 25, btnReset = 26, btnBack = 32; 

volatile long pulseCount = 0;
volatile unsigned long lastPulseTime = 0;
void IRAM_ATTR countPulse() { 
  unsigned long currentTime = micros();
  if (currentTime - lastPulseTime > 200) { 
    pulseCount++; 
    lastPulseTime = currentTime;
  }
}

const int stepsPerCm = 4000; 
const int SPEED_CONSTANT = 500; 
float bgCPS = 0.0; 

int High[2]={0,0}, Step[2]={0,0}, Start[2]={0,0}, timerValues[4]={0,0,0,0};
int Allvalue[4]={0,0,0,0};
int HighCursor=0, StepCursor=0, StartCursor=0, timerCursor=0;
bool HighSet=false, StepSet=true, StartSet=true, timerSet=true, Setall=true, setvalue=false; 
unsigned long lastBlink=0; bool blinkOn=true;
static bool summaryDone=false; bool adjust=false; int totalSeconds=0;
const int maxDataPoints = 100;
int savedPositions[maxDataPoints];
long savedPulses[maxDataPoints];
int dataCount = 0;

// ตัวแปรเสริมสำหรับ Web
int currentScanningCm = 0; 
bool webCommandStart = false;
int lastFinishedHeight = -1; 
long lastFinishedNetCount = 0;
bool isMeasuringBG = false;


/* -------- WEB API HANDLERS -------- */
void sendCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void handleStart() {
  sendCORS();
  server.send(200, "text/plain", "GRAPH_ONLY_MODE");
}

void handleData() {
  sendCORS();

  String json = "{";

  // 🟢 PRIORITY 1: กำลังวัด BG
  if (isMeasuringBG) {
    json += "\"status\":\"measuring_bg\",";
  }
  // 🟢 PRIORITY 2: ยังไม่เริ่มงาน
  else if (!summaryDone) {
    json += "\"status\":\"idle\",";
  }
  // 🟢 PRIORITY 3: กำลังสแกน
  else {
    json += "\"status\":\"scanning\",";
  }

  json += "\"bgRate\":" + String(bgCPS) + ",";
  json += "\"currentHeight\":" + String(currentScanningCm) + ",";
  json += "\"liveRaw\":" + String(pulseCount) + ",";
  json += "\"finalHeight\":" + String(lastFinishedHeight) + ",";
  json += "\"finalNetCount\":" + String(lastFinishedNetCount);
  json += "}";

  server.send(200, "application/json", json);
}

void handleStop() {
  sendCORS();
  server.send(200, "text/plain", "GRAPH_ONLY_MODE");
}

void setMotorLock(bool lock) {
  if (lock) { digitalWrite(m1_ena, LOW); digitalWrite(m2_ena, LOW); } 
  else { digitalWrite(m1_ena, HIGH); digitalWrite(m2_ena, HIGH); }
  delay(10);
}

void driveDualStepper(long steps, bool direction, int speedDelay) {
  detachInterrupt(digitalPinToInterrupt(radiationPin));
  setMotorLock(true); 
  digitalWrite(m1_dir, direction ? HIGH : LOW);
  digitalWrite(m2_dir, direction ? HIGH : LOW);
  delayMicroseconds(50); 

  for(long i = 0; i < steps; i++) {
    digitalWrite(m1_pul, HIGH); digitalWrite(m2_pul, HIGH); 
    delayMicroseconds(20); 
    digitalWrite(m1_pul, LOW); digitalWrite(m2_pul, LOW);
    
    int delayLeft = speedDelay - 20;
    if(delayLeft < 0) delayLeft = 0;
    delayMicroseconds(delayLeft); 
    
    if (i % 200 == 0) {
      yield(); 
      server.handleClient(); // ให้หน้าเว็บยังตอบสนองตอนมอเตอร์หมุน
    }
    if(digitalRead(btnReset) == LOW) ESP.restart();
  }
  attachInterrupt(digitalPinToInterrupt(radiationPin), countPulse, FALLING);
}

bool isButtonPressed(int pin) {
  if (digitalRead(pin) == LOW) {
    delay(50); 
    if (digitalRead(pin) == LOW) {
      while(digitalRead(pin) == LOW) server.handleClient(); 
      return true;
    }
  }
  return false;
}

void printField(int val, bool hidden) {
  if (hidden) lcd.print(" "); else lcd.print(val);
}

void calibrateBackground() {
  isMeasuringBG = true;   // 🟢 เริ่มวัด BG
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("BG CALIBRATION");
  lcd.setCursor(0,1); lcd.print("PLS CLEAR SOURCE"); 
  lcd.setCursor(0,3); lcd.print("Press SEL to Start");
  
  while(digitalRead(btnSelect) == HIGH) {
    server.handleClient();
    if(digitalRead(btnReset) == LOW) ESP.restart();
    delay(50);
  }
  while(digitalRead(btnSelect) == LOW); 
  delay(500);

  lcd.clear();
  lcd.setCursor(0,0); lcd.print("MEASURING BG...");
  lcd.setCursor(0,2); lcd.print("TIME: 20 Sec");

  pulseCount = 0;
  unsigned long startTime = millis();
  int calibrateTime = 20; 
  
  while(millis() - startTime < (calibrateTime * 1000)) {
    server.handleClient();
    int rem = calibrateTime - ((millis() - startTime)/1000);
    lcd.setCursor(0,3); lcd.print("Remaining: "); lcd.print(rem); lcd.print(" s ");
    delay(200);
    if(digitalRead(btnReset) == LOW) ESP.restart();
  }
  
  bgCPS = (float)pulseCount / calibrateTime;
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("BG CALIBRATED!");
  lcd.setCursor(0,1); lcd.print("Rate: "); lcd.print(bgCPS); lcd.print(" CPS");
  lcd.setCursor(0,3); lcd.print("Press SEL -> Menu");
  
  while(digitalRead(btnSelect) == HIGH) { server.handleClient(); delay(100); }
  while(digitalRead(btnSelect) == LOW); 
  delay(500);
  lcd.clear();

  isMeasuringBG = false;  // 🔴 วัด BG เสร็จแล้ว
}


void manualZeroAdjust() {
  setMotorLock(true); delay(100); 
  String names[] = {"SOURCE", "DETECTOR"};
  int puls[] = {m1_pul, m2_pul}, dirs[] = {m1_dir, m2_dir};
  
  int i = 0; // เปลี่ยนจาก for loop มาใช้ while เพื่อให้ย้อนกลับได้
  while(i < 2) {
    lcd.clear(); lcd.setCursor(0,0); lcd.print("SET ZERO: "); lcd.print(names[i]);
    lcd.setCursor(0,1); lcd.print("Up/Dn:Move");
    lcd.setCursor(0,2); lcd.print("Select:OK");
    lcd.setCursor(0,3); lcd.print("Back:Prev"); // 🆕 เพิ่มคำแนะนำบนจอ

    // รอจนกว่าจะปล่อยปุ่มทั้งหมด
    while(digitalRead(btnSelect) == LOW || digitalRead(btnUp) == LOW || digitalRead(btnDown) == LOW || digitalRead(btnBack) == LOW) server.handleClient();
    delay(100);

    bool moveNext = false;
    bool movePrev = false;

    while(!moveNext && !movePrev) {
      server.handleClient();
      if(digitalRead(btnUp) == LOW) {
        digitalWrite(dirs[i], HIGH);
        digitalWrite(puls[i], HIGH); delayMicroseconds(20);
        digitalWrite(puls[i], LOW);  delayMicroseconds(800); 
      }
      else if(digitalRead(btnDown) == LOW) {
        digitalWrite(dirs[i], LOW);
        digitalWrite(puls[i], HIGH); delayMicroseconds(20);
        digitalWrite(puls[i], LOW);  delayMicroseconds(800);
      }
      // ยืนยันตำแหน่ง (ไปข้างหน้า)
      else if(digitalRead(btnSelect) == LOW) {
         delay(50);
         if(digitalRead(btnSelect) == LOW) { 
           while(digitalRead(btnSelect) == LOW) server.handleClient(); 
           moveNext = true; 
         }
      }
      // 🆕 ลอจิกปุ่มย้อนกลับ (Back)
      else if(digitalRead(btnBack) == LOW) {
         delay(50);
         if(digitalRead(btnBack) == LOW) { 
           while(digitalRead(btnBack) == LOW) server.handleClient(); 
           movePrev = true; 
         }
      }
      
      if (digitalRead(btnReset) == LOW) ESP.restart();
    }
    
    if (moveNext) {
      lcd.clear(); lcd.print("Saved Zero!"); delay(500);
      i++; // ไปมอเตอร์ตัวถัดไป (0 -> 1 -> จบ)
    } 
    else if (movePrev) {
      if (i > 0) {
        i--; // ย้อนกลับไปมอเตอร์ตัวก่อนหน้า (1 -> 0)
        lcd.clear(); lcd.print("Going Back..."); delay(500);
      } else {
        // ถ้าอยู่ที่ SOURCE (i=0) อยู่แล้ว จะย้อนกลับไม่ได้
        lcd.clear(); lcd.print("Already at Start!"); delay(500);
      }
    }
  }
  setMotorLock(false);
}

void showSelectMode() {
  digitalWrite(m1_pul, LOW); digitalWrite(m2_pul, LOW);
  setMotorLock(false);

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("--- SELECT MODE ---");
  lcd.setCursor(0, 1); lcd.print("UP  : Motor Mode");
  lcd.setCursor(0, 2); lcd.print("DOWN: Set BG Mode");

  while (true) {
    server.handleClient(); 
    
    // กดปุ่ม UP -> ไปโหมดมอเตอร์ (ข้าม BG)
    if (digitalRead(btnUp) == LOW) {
      delay(50);
      while (digitalRead(btnUp) == LOW) server.handleClient(); 
      lcd.clear();
      lcd.setCursor(0, 1); lcd.print(" Entering Motor...");
      delay(1000);
      break; 
    }
    
    // กดปุ่ม DOWN -> ไปโหมดวัด BG ก่อน
    if (digitalRead(btnDown) == LOW) {
      delay(50);
      while (digitalRead(btnDown) == LOW) server.handleClient(); 
      calibrateBackground(); 
      break; 
    }

    if (digitalRead(btnReset) == LOW) ESP.restart();
  }
}

void softReset() {
  for(int i=0; i<2; i++) { High[i]=0; Step[i]=0; Start[i]=0; }
  for(int i=0; i<4; i++) { timerValues[i]=0; Allvalue[i]=0; }
  HighCursor=0; StepCursor=0; StartCursor=0; timerCursor=0;
  HighSet=false; StepSet=true; StartSet=true; timerSet=true; 
  Setall=true; setvalue=false; summaryDone = false; adjust = false; 
  dataCount = 0; totalSeconds = 0; webCommandStart = false;
  for(int i = 0; i < maxDataPoints; i++) { savedPositions[i] = 0; savedPulses[i] = 0; }
}

void runAutoScanProcess() {
  setMotorLock(true); 
  int startCm = Allvalue[2];
  int endCm = Allvalue[0];
  int stepCm = Allvalue[1];
  long currentAbsPosSteps = (long)startCm * stepsPerCm; 
  
  lcd.clear(); lcd.setCursor(0,0); lcd.print("SETUP POSITION...");
  lcd.setCursor(0,1); lcd.print("MOVING TO: "); lcd.print(startCm); lcd.print(" CM");
  delay(1000); 
  
  driveDualStepper(currentAbsPosSteps, true, SPEED_CONSTANT);
  
  dataCount = 0;
  int currentCm = startCm;
  currentScanningCm = currentCm; // อัปเดตค่าไปที่เว็บ
  
  Serial.println("--- START DATA ---");
  Serial.println("Position(cm),NetCount,RawCount"); 

  while(currentCm <= endCm) {
    currentScanningCm = currentCm;
    lcd.clear();
    lcd.setCursor(0,0); lcd.print(">> SCANNING...");
    lcd.setCursor(0,1); lcd.print("POS: "); lcd.print(currentCm); lcd.print(" cm");
    
    pulseCount = 0;
    unsigned long startT = millis();
    unsigned long waitT = (unsigned long)Allvalue[3] * 1000;

    while(millis() - startT < waitT) {
      server.handleClient(); // ยอมให้ดึงข้อมูล JSON ระหว่างนับรังสี
      int rem = (waitT - (millis() - startT)) / 1000;
      lcd.setCursor(0,2); lcd.print("TIME: "); lcd.print(rem); lcd.print(" s   ");
      lcd.setCursor(0,3); lcd.print("RAW: "); lcd.print(pulseCount); lcd.print("   ");
      delay(200); 
      if(digitalRead(btnReset) == LOW) ESP.restart();
    }
    
    long expectedBg = (long)(bgCPS * Allvalue[3]); 
    long netCount = pulseCount - expectedBg;
    if(netCount < 0) netCount = 0;
    lastFinishedHeight = currentCm;
    lastFinishedNetCount = netCount;
    Serial.print(currentCm); Serial.print(","); 
    Serial.print(netCount); Serial.print(",");
    Serial.println(pulseCount);
    
    if (dataCount < maxDataPoints) {
      savedPositions[dataCount] = currentCm;
      savedPulses[dataCount] = (netCount / Allvalue[3]); // หารด้วยเวลาเป็นวินาที
      dataCount++;
    }
    
    int nextCm = currentCm + stepCm; 
    if(nextCm > endCm) break; 
    
    lcd.clear();
    lcd.setCursor(0,0); lcd.print(">> MOVING..."); 
    lcd.setCursor(0,1); lcd.print("NEXT: "); lcd.print(nextCm); lcd.print(" cm");
    
    long moveSteps = (long)stepCm * stepsPerCm;
    driveDualStepper(moveSteps, true, SPEED_CONSTANT);
    
    currentAbsPosSteps += moveSteps;
    currentCm = nextCm; 
  }
  
  lcd.clear(); lcd.print("SCAN FINISHED!");
  delay(2000); 
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("Returning Home...");
  driveDualStepper(currentAbsPosSteps, false, SPEED_CONSTANT);
  
  setMotorLock(false); 
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("JOB DONE!");
  delay(2000); 
  softReset(); 
}

void setup() {
  Serial.begin(115200);
  
  // 1. 🥇 เปิดจอ LCD เป็นอันดับแรกสุด! จะได้แสดงผลสถานะต่างๆ ได้ทันที
  Wire.begin(18, 19);
  lcd.init(); 
  lcd.backlight();

  // 2. เคลียร์ความจำ WiFi เก่าที่บอร์ดอาจจะแอบจำไว้ทิ้งไปก่อน
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  // 3. เริ่มเชื่อมต่อ WiFi
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Connecting WiFi...");
  Serial.print("Connecting to WiFi");
  
  WiFi.begin(ssid, password);
  unsigned long startAttemptTime = millis();
  
  // รอ 10 วินาที
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) { 
    delay(500); 
    Serial.print("."); 
  }

  lcd.clear();
  // เช็คผลลัพธ์การเชื่อมต่อ
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected! IP: " + WiFi.localIP().toString());
    lcd.setCursor(0, 0); lcd.print("WIFI CONNECTED");
    lcd.setCursor(0, 1); lcd.print(WiFi.localIP()); // โชว์ IP บนจอให้ดูด้วย
    
    //server.begin(); // เปิด Server เฉพาะตอนมี WiFi
  } 
  else {
    Serial.println("\nWiFi connection failed. Running in OFFLINE mode.");
    lcd.setCursor(0, 0); lcd.print("OFFLINE MODE");
  }
  
  // หน่วงเวลา 2 วินาทีให้ผู้ใช้อ่านหน้าจอทัน ก่อนจะไปหน้า SELECT MODE
  delay(2000); 

  // API Routes
  server.on("/start", handleStart);
  server.on("/stop", handleStop);
  server.on("/data", handleData);
  if (WiFi.status() == WL_CONNECTED) {
      server.begin(); 
  }
  // ลบ server.begin() ตรงนี้ออกเพราะเราเรียกใน if ข้างบนแล้ว

  pinMode(m1_pul, OUTPUT); pinMode(m1_dir, OUTPUT); pinMode(m1_ena, OUTPUT);
  pinMode(m2_pul, OUTPUT); pinMode(m2_dir, OUTPUT); pinMode(m2_ena, OUTPUT);
  
  digitalWrite(m1_ena, HIGH); digitalWrite(m2_ena, HIGH);
  delay(1000);

  pinMode(btnUp, INPUT_PULLUP);
  pinMode(btnDown, INPUT_PULLUP);
  pinMode(btnSelect, INPUT_PULLUP);
  pinMode(btnReset, INPUT_PULLUP);
  pinMode(btnBack, INPUT_PULLUP);
  
  pinMode(radiationPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(radiationPin), countPulse, FALLING);
  
  digitalWrite(m1_pul, LOW); digitalWrite(m2_pul, LOW);
  setMotorLock(false);
}

void loop() {
  server.handleClient(); // รับคำสั่งเว็บตลอดเวลา

  if (!adjust) { 
    showSelectMode();
    manualZeroAdjust(); 
    adjust = true; }

  while(!summaryDone) {
    server.handleClient(); 
    if(!setvalue){
      lcd.setCursor(0, 0); lcd.print("Set High: ");
      lcd.setCursor(0, 1); lcd.print("Set Step: ");
      lcd.setCursor(0, 2); lcd.print("Set Start: ");
      lcd.setCursor(0, 3); lcd.print("Set time(m:s): ");
    }
    
    if (millis() - lastBlink > 500) { lastBlink = millis(); blinkOn = !blinkOn; }
    
    if (isButtonPressed(btnUp)) {
       if (!HighSet) { High[HighCursor]++; if (High[HighCursor] > 9) High[HighCursor] = 0; }
       else if (!StepSet) { Step[StepCursor]++; if (Step[StepCursor] > 9) Step[StepCursor] = 0; }
       else if (!StartSet) { Start[StartCursor]++; if (Start[StartCursor] > 9) Start[StartCursor] = 0; }
       else if (!timerSet) { 
         timerValues[timerCursor]++; 
         if (timerCursor == 2 && timerValues[2] > 5) timerValues[2] = 0;
         else if (timerValues[timerCursor] > 9) timerValues[timerCursor] = 0;
       }
    }
    if (isButtonPressed(btnDown)) {
       if (!HighSet) { High[HighCursor]--; if (High[HighCursor] < 0) High[HighCursor] = 9; }
       else if (!StepSet) { Step[StepCursor]--; if (Step[StepCursor] < 0) Step[StepCursor] = 9; }
       else if (!StartSet) { Start[StartCursor]--; if (Start[StartCursor] < 0) Start[StartCursor] = 9; }
       else if (!timerSet) { 
         timerValues[timerCursor]--;
         if (timerCursor == 2 && timerValues[2] < 0) timerValues[2] = 5;
         else if (timerValues[timerCursor] < 0) timerValues[timerCursor] = 9;
       }
    }
    
    if (digitalRead(btnSelect) == LOW) {
      delay(50); 
      if (digitalRead(btnSelect) == LOW) {
        unsigned long pressTime = millis();
        bool longPress = false;
        while(digitalRead(btnSelect) == LOW) {
           server.handleClient(); // ให้อาหาร Watchdog
           delay(10);             // ให้บอร์ดได้พักหายใจ
           if (millis() - pressTime > 3000) { 
             longPress = true;
             calibrateBackground(); softReset(); return; 
           }
        }
        if (!longPress) {
          if (!HighSet) { HighCursor++; if (HighCursor > 1) { HighSet = true; StepSet = false; } }
          else if (!StepSet) { StepCursor++; if (StepCursor > 1) { StepSet = true; StartSet = false; } }
          else if (!StartSet) { StartCursor++; if (StartCursor > 1) { StartSet = true; timerSet = false; } }
          else if (!timerSet) {
            timerCursor++;
            if (timerCursor > 3) {
              totalSeconds = ((timerValues[0]*10 + timerValues[1])*60) + (timerValues[2]*10 + timerValues[3]);
              int tHigh = High[0]*10 + High[1];
              if(tHigh > 75 || totalSeconds <= 0) {
                  lcd.clear(); lcd.print("ERROR: Invalid!"); delay(2000); lcd.clear();
                  softReset();
              } else { summaryDone=true; setvalue=true; }
            }
          }
        }
      }
    }

    // 🆕 เพิ่มลอจิกการทำงานของปุ่ม Back (ย้อนกลับ)
    if (isButtonPressed(btnBack)) {
      if (!timerSet) { // กำลังตั้ง Time
        timerCursor--;
        if (timerCursor < 0) { timerSet = true; StartSet = false; StartCursor = 1; timerCursor = 0; }
      }
      else if (!StartSet) { // กำลังตั้ง Start
        StartCursor--;
        if (StartCursor < 0) { StartSet = true; StepSet = false; StepCursor = 1; StartCursor = 0; }
      }
      else if (!StepSet) { // กำลังตั้ง Step
        StepCursor--;
        if (StepCursor < 0) { StepSet = true; HighSet = false; HighCursor = 1; StepCursor = 0; }
      }
      else if (!HighSet) { // กำลังตั้ง High
        HighCursor--;
        if (HighCursor < 0) { HighCursor = 0; } // ติดขอบซ้ายสุดแล้ว
      }
      lcd.clear(); // ล้างจอเพื่ออัปเดตตำแหน่ง Cursor
    }
    
    // แสดงผลหน้าจอ (ตัดสั้นเพื่อความกระชับ)
    if (!HighSet) { lcd.setCursor(10, 0); printField(High[0], HighCursor==0 && blinkOn); printField(High[1], HighCursor==1 && blinkOn); lcd.print(" CM "); } 
    else if(!setvalue) { lcd.setCursor(10, 0); lcd.print(High[0]); lcd.print(High[1]); lcd.print(" CM "); }
    if (!StepSet) { lcd.setCursor(10, 1); printField(Step[0], StepCursor==0 && blinkOn); printField(Step[1], StepCursor==1 && blinkOn); lcd.print(" CM "); } 
    else if(!setvalue) { lcd.setCursor(10, 1); lcd.print(Step[0]); lcd.print(Step[1]); lcd.print(" CM "); }
    if (!StartSet) { lcd.setCursor(11, 2); printField(Start[0], StartCursor==0 && blinkOn); printField(Start[1], StartCursor==1 && blinkOn); lcd.print(" CM "); } 
    else if(!setvalue) { lcd.setCursor(11, 2); lcd.print(Start[0]); lcd.print(Start[1]); lcd.print(" CM "); }
    if (!timerSet) { lcd.setCursor(15, 3); printField(timerValues[0], timerCursor==0 && blinkOn); printField(timerValues[1], timerCursor==1 && blinkOn); lcd.print(":"); printField(timerValues[2], timerCursor==2 && blinkOn); printField(timerValues[3], timerCursor==3 && blinkOn); } 
    else if(!setvalue) { lcd.setCursor(15, 3); lcd.print(timerValues[0]); lcd.print(timerValues[1]); lcd.print(":"); lcd.print(timerValues[2]); lcd.print(timerValues[3]); }
    
    if (summaryDone && setvalue) {
       Allvalue[0] = High[0]*10+High[1]; Allvalue[1] = Step[0]*10+Step[1];
       Allvalue[2] = Start[0]*10+Start[1]; Allvalue[3] = totalSeconds;
       lcd.clear(); lcd.print("Starting..."); delay(1000);
    }
    if (digitalRead(btnReset) == LOW) ESP.restart();
  }
  
  runAutoScanProcess();
}