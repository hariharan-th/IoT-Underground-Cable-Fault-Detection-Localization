#include <WiFi.h>
#include <ThingSpeak.h>
#include <LiquidCrystal.h>
const char* WIFI_SSID = "XXX";
const char* WIFI_PASSWORD = "XXXX";
unsigned long CHANNEL_ID = 0;  // Set your ThingSpeak channel ID before use.
const char* WRITE_API_KEY = "YOUR_THINGSPEAK_WRITE_API_KEY";

WiFiClient thingspeakClient;
HardwareSerial gsm(2);

const int GSM_RX_PIN = 16;
const int GSM_TX_PIN = 17;
const char* AUTHORIZED_NUMBER = "RECEIVER_NUMBER";
const int CURRENT_SENSOR_PIN = 34;
const int VOLTAGE_SENSOR_PIN = 35;
const int FAN_PIN = 18;
const int MOTOR_PIN = 5;
const int LIGHT_PIN = 15;

const int FAULT_SWITCH_1 = 23;
const int FAULT_SWITCH_2 = 22;
const int FAULT_SWITCH_3 = 21;
const int FAULT_SWITCH_4 = 19;

LiquidCrystal lcd(27,14,26,25,33,32);
const float VOLTAGE_DIVIDER_RATIO = 5.0;
const float CURRENT_SENSITIVITY_MV_PER_A = 185.0;
const float CURRENT_ZERO_MV = 2500.0;
const bool RELAY_ACTIVE_HIGH = true;
const unsigned long SENSOR_INTERVAL_MS = 1000;
const unsigned long LCD_INTERVAL_MS = 1500;
const unsigned long THINGSPEAK_INTERVAL_MS = 20000;
const unsigned long WIFI_RETRY_INTERVAL_MS = 10000;
const unsigned long SMS_RETRY_INTERVAL_MS = 15000;

float voltage = 0.0;
float current = 0.0;

int faultCode = 0;
int previousFaultCode = 0;

bool loadsRunning = false;
bool smsPending = false;

unsigned long lastSensorTime = 0;
unsigned long lastLCDTime = 0;
unsigned long lastThingSpeakTime = 0;
unsigned long lastWiFiRetryTime = 0;
unsigned long lastSMSAttemptTime = 0;

bool lcdPage = false;

String getFaultName(int code) {
  switch (code) {
    case 1:
      return "OPEN CIRCUIT";

    case 2:
      return "SHORT CIRCUIT";

    case 3:
      return "LINE TO GROUND";

    case 4:
      return "EARTH LEAKAGE";

    case 5:
      return "MULTIPLE FAULTS";

    default:
      return "NORMAL";
  }
}

int readFaultCode() {
  bool s1 = digitalRead(FAULT_SWITCH_1) == HIGH;
  bool s2 = digitalRead(FAULT_SWITCH_2) == HIGH;
  bool s3 = digitalRead(FAULT_SWITCH_3) == HIGH;
  bool s4 = digitalRead(FAULT_SWITCH_4) == HIGH;

  int activeSwitches =
      (int)s1 + (int)s2 + (int)s3 + (int)s4;

  if (activeSwitches == 0) {
    return 0;
  }

  if (activeSwitches > 1) {
    return 5;
  }

  if (s1) return 1;
  if (s2) return 2;
  if (s3) return 3;
  if (s4) return 4;

  return 0;
}
String getFaultMessage() {
  String message = "Electrical Protection Alert: ";

  bool firstFault = true;

  if (digitalRead(FAULT_SWITCH_1) == HIGH) {
    message += "OPEN CIRCUIT";
    firstFault = false;
  }

  if (digitalRead(FAULT_SWITCH_2) == HIGH) {
    if (!firstFault) message += ", ";
    message += "SHORT CIRCUIT";
    firstFault = false;
  }

  if (digitalRead(FAULT_SWITCH_3) == HIGH) {
    if (!firstFault) message += ", ";
    message += "LINE TO GROUND";
    firstFault = false;
  }

  if (digitalRead(FAULT_SWITCH_4) == HIGH) {
    if (!firstFault) message += ", ";
    message += "EARTH LEAKAGE";
  }

  message += ". All loads OFF.";
  message += " Voltage=" + String(voltage, 2) + "V";
  message += " Current=" + String(current, 2) + "A.";

  return message;
}
void setLoad(int pin, bool turnOn) {
  if (RELAY_ACTIVE_HIGH) {
    digitalWrite(pin, turnOn ? HIGH : LOW);
  } else {
    digitalWrite(pin, turnOn ? LOW : HIGH);
  }
}

void controlLoads() {
  loadsRunning = (faultCode == 0);

  setLoad(FAN_PIN, loadsRunning);
  setLoad(MOTOR_PIN, loadsRunning);
  setLoad(LIGHT_PIN, loadsRunning);
}

float readVoltage() {
  const int samples = 40;
  uint64_t totalMilliVolts = 0;

  for (int i = 0; i < samples; i++) {
    totalMilliVolts += analogReadMilliVolts(VOLTAGE_SENSOR_PIN);
    delay(2);
  }

  float sensorMilliVolts =
      totalMilliVolts / (float)samples;

  float measuredVoltage =
      (sensorMilliVolts / 1000.0) *
      VOLTAGE_DIVIDER_RATIO;

  return measuredVoltage;
}
float readCurrent() {
  const int samples = 100;
  uint64_t totalMilliVolts = 0;

  for (int i = 0; i < samples; i++) {
    totalMilliVolts += analogReadMilliVolts(CURRENT_SENSOR_PIN);
    delay(2);
  }

  float sensorMilliVolts =
      totalMilliVolts / (float)samples;

  float measuredCurrent =
      (sensorMilliVolts - CURRENT_ZERO_MV) /
      CURRENT_SENSITIVITY_MV_PER_A;
  if (measuredCurrent < 0) {
    measuredCurrent = -measuredCurrent;
  }

  return measuredCurrent;
}
bool sendSMS(String message) {
  Serial.println("GSM: Sending fault alert");

  while (gsm.available()) {
    gsm.read();
  }

  gsm.println("AT+CMGF=1");

  String response = "";
  unsigned long startTime = millis();

  while (millis() - startTime < 2000) {
    while (gsm.available()) {
      response += (char)gsm.read();
    }

    if (response.indexOf("OK") >= 0) {
      break;
    }

    if (response.indexOf("ERROR") >= 0) {
      Serial.println("GSM text mode failed");
      return false;
    }

    delay(10);
  }

  if (response.indexOf("OK") < 0) {
    Serial.println("GSM modem not responding");
    return false;
  }

  gsm.print("AT+CMGS=\"");
  gsm.print(AUTHORIZED_NUMBER);
  gsm.println("\"");

  response = "";
  startTime = millis();

  while (millis() - startTime < 5000) {
    while (gsm.available()) {
      response += (char)gsm.read();
    }

    if (response.indexOf(">") >= 0) {
      break;
    }

    if (response.indexOf("ERROR") >= 0) {
      Serial.println("GSM recipient command failed");
      return false;
    }

    delay(10);
  }

  if (response.indexOf(">") < 0) {
    Serial.println("GSM SMS prompt timeout");
    return false;
  }

  gsm.print(message);
  gsm.write(26);  // Ctrl+Z sends the SMS.

  response = "";
  startTime = millis();

  while (millis() - startTime < 20000) {
    while (gsm.available()) {
      response += (char)gsm.read();
    }

    if (response.indexOf("+CMGS:") >= 0) {
      Serial.println("GSM SMS accepted by modem");
      return true;
    }

    if (response.indexOf("ERROR") >= 0) {
      Serial.println("GSM SMS sending error");
      return false;
    }

    delay(10);
  }

  Serial.println("GSM SMS confirmation timeout");
  return false;
}
void updateLCD() {
  lcd.clear();

  if (!lcdPage) {
    lcd.setCursor(0, 0);
    lcd.print("V:");
    lcd.print(voltage, 2);
    lcd.print(" I:");
    lcd.print(current, 2);

    lcd.setCursor(0, 1);

    if (loadsRunning) {
      lcd.print("LOADS: ON");
    } else {
      lcd.print("LOADS: OFF");
    }
  } else {
    lcd.setCursor(0, 0);
    lcd.print("FAULT STATUS");

    lcd.setCursor(0, 1);
    lcd.print(getFaultName(faultCode));
  }

  lcdPage = !lcdPage;
}
void uploadThingSpeak() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("ThingSpeak: Wi-Fi disconnected");
    return;
  }

  ThingSpeak.setField(1, voltage);
  ThingSpeak.setField(2, current);
  ThingSpeak.setField(3, faultCode);
  ThingSpeak.setField(4, loadsRunning ? 1 : 0);

  ThingSpeak.setField(
      5, digitalRead(FAULT_SWITCH_1));

  ThingSpeak.setField(
      6, digitalRead(FAULT_SWITCH_2));

  ThingSpeak.setField(
      7, digitalRead(FAULT_SWITCH_3));

  ThingSpeak.setField(
      8, digitalRead(FAULT_SWITCH_4));

  int httpCode =
      ThingSpeak.writeFields(CHANNEL_ID, WRITE_API_KEY);

  if (httpCode == 200) {
    Serial.println("ThingSpeak upload successful");
  } else {
    Serial.print("ThingSpeak upload failed: ");
    Serial.println(httpCode);
  }
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to Wi-Fi");

  unsigned long startTime = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - startTime < 20000) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Wi-Fi connected");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Wi-Fi unavailable; retry enabled");
  }
}
void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  analogSetPinAttenuation(CURRENT_SENSOR_PIN, ADC_11db);
  analogSetPinAttenuation(VOLTAGE_SENSOR_PIN, ADC_11db);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(MOTOR_PIN, OUTPUT);
  pinMode(LIGHT_PIN, OUTPUT);
  setLoad(FAN_PIN, false);
  setLoad(MOTOR_PIN, false);
  setLoad(LIGHT_PIN, false);
  pinMode(FAULT_SWITCH_1, INPUT_PULLDOWN);
  pinMode(FAULT_SWITCH_2, INPUT_PULLDOWN);
  pinMode(FAULT_SWITCH_3, INPUT_PULLDOWN);
  pinMode(FAULT_SWITCH_4, INPUT_PULLDOWN);

  lcd.begin(16, 2);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Protection Unit");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");

  gsm.begin(9600, SERIAL_8N1, GSM_RX_PIN, GSM_TX_PIN);
  connectWiFi();
  ThingSpeak.begin(thingspeakClient);
  voltage = readVoltage();
  current = readCurrent();
  faultCode = readFaultCode();

  controlLoads();

  previousFaultCode = faultCode;

  if (faultCode != 0) {
    smsPending = true;
  }

  Serial.println("Protection system initialized");
}
void loop() {
  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED &&
      now - lastWiFiRetryTime >= WIFI_RETRY_INTERVAL_MS) {

    lastWiFiRetryTime = now;

    Serial.println("Retrying Wi-Fi connection");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }

  if (now - lastSensorTime >= SENSOR_INTERVAL_MS) {
    lastSensorTime = now;

    voltage = readVoltage();
    current = readCurrent();

    faultCode = readFaultCode();

    controlLoads();

    Serial.print("Voltage: ");
    Serial.print(voltage, 2);
    Serial.print(" V | Current: ");
    Serial.print(current, 2);
    Serial.print(" A | Fault: ");
    Serial.print(getFaultName(faultCode));
    Serial.print(" | Loads: ");
    Serial.println(loadsRunning ? "ON" : "OFF");

    if (faultCode != 0 &&
        faultCode != previousFaultCode) {
      smsPending = true;
    }
    if (faultCode == 0) {
      smsPending = false;
    } else if (smsPending &&
               now - lastSMSAttemptTime >= SMS_RETRY_INTERVAL_MS) {

      lastSMSAttemptTime = now;

      if (sendSMS(getFaultMessage())) {
        smsPending = false;
      }
    }

    previousFaultCode = faultCode;
  }

  if (now - lastLCDTime >= LCD_INTERVAL_MS) {
    lastLCDTime = now;
    updateLCD();
  }

  if (now - lastThingSpeakTime >= THINGSPEAK_INTERVAL_MS) {
    lastThingSpeakTime = now;
    uploadThingSpeak();
  }
}
