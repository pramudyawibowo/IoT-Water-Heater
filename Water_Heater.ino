#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_AHT10.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>

#define pinDS     D5
#define pinPompa  D7
#define pinHeater D6
#define pinLED    D3
#define pinBuzzer D8

const char *ssid = "SSID";
const char *password = "password";
const char *mqtt_server = "168.138.190.252";
String newHostname = "water-heater";
WiFiClient espClient;
PubSubClient client(espClient);
Adafruit_AHT10 aht;
LiquidCrystal_I2C lcd(0x27, 16, 2);
OneWire DS18B20(pinDS);
DallasTemperature ds(&DS18B20);
char buff[33];
unsigned long previousSend = 0, previousSensor = 0, previousPompa = 0, previousBuzzer = 0;
byte heaterChar[] = {0x0E, 0x0A, 0x0A, 0x0E, 0x1F, 0x1F, 0x1F, 0x0E};
byte powerChar[] = {0x00, 0x04, 0x04, 0x0E, 0x15, 0x11, 0x11, 0x0E};

float suhuUdara, kelembapanUdara, suhuUdaraMax, suhuAir, suhuAirMin;
sensors_event_t humidity, temp;
bool heater = false, pompa = false, buzzer = false;
String heaterStatus = "false";
int seconds = 1, buzzerCount = 0;

void initInterface() {
  lcd.init();
  lcd.backlight();
  lcd.createChar(1, powerChar);
  lcd.createChar(2, heaterChar);
}

void printLcd(int col, int row, String text) {
  lcd.setCursor(col, row);
  lcd.print(text);
}

void initDevices() {
  pinMode(pinLED, OUTPUT);
  pinMode(pinHeater, OUTPUT);
  pinMode(pinPompa, OUTPUT);
  pinMode(pinBuzzer, OUTPUT);
  digitalWrite(pinLED, LOW);
  digitalWrite(pinHeater, HIGH);
  digitalWrite(pinPompa, HIGH);
  aht.begin();
  ds.begin();
}

void readSensors() {
  unsigned long currentSensor = millis();
  if (currentSensor - previousSensor >= 500) {
    previousSensor = currentSensor;
    aht.getEvent(&humidity, &temp);
    kelembapanUdara = humidity.relative_humidity;
    suhuUdara = temp.temperature;
    ds.setResolution(12);
    ds.requestTemperatures();
    suhuAir = ds.getTempCByIndex(0);
  }
}

void initWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.hostname(newHostname.c_str());
  WiFi.begin(ssid, password);
  printLcd(0, 0, "   CONNECTING   ");
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
  }
  printLcd(0, 0, "   CONNECTED!   ");
  printLcd(0, 1, "IP:");
  printLcd(3, 1, WiFi.localIP().toString());
  delay(2000);
  lcd.clear();
}

void initMqtt() {
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void reconnect() {
  while (!client.connected()) {
    printLcd(0, 0, "Connecting MQTT ");
    delay(500);
    if (client.connect("WaterHeater")) {
      printLcd(0, 0, " MQTT Connected ");
      client.subscribe("heater/status");
      client.subscribe("heater/suhuHeater");
      client.subscribe("heater/saveHeater");
      client.subscribe("heater/suhuUdara");
      client.subscribe("heater/saveUdara");
      client.subscribe("pompa/status");
    } else {
      printLcd(0, 0, "MQTT Failed    ");
      printLcd(0, 1, String(client.state()));
    }
  }
  delay(500);
}

void callback(char* topic, byte* message, unsigned int length) {
  String messageTemp;
  for (int i = 0; i < length; i++) {
    messageTemp += (char)message[i];
  }
  if (String(topic) == "heater/status") {
    if (messageTemp == "false") {
      heater = false;
    } else if (messageTemp == "true") {
      heater = true;
    }
  }
  if (String(topic) == "heater/suhuHeater") {
    suhuAirMin = messageTemp.toFloat();
  }
  if (String(topic) == "heater/saveHeater") {
    if (messageTemp == "true") {
      printLcd(6, 1, " OK ");
      delay(500);
      EEPROM_writeFloat(0, suhuAirMin);
    }

  }
  if (String(topic) == "heater/suhuUdara") {
    suhuUdaraMax = messageTemp.toFloat();
  }
  if (String(topic) == "heater/saveUdara") {
    if (messageTemp == "true") {
      printLcd(12, 1, " OK ");
      delay(500);
      EEPROM_writeFloat(4, suhuUdaraMax);
    }
  }
  if (String(topic) == "pompa/status") {
    if (messageTemp == "false") {
      pompa = false;
    } else if (messageTemp == "true") {
      pompa = true;
      seconds = 1;
    }
  }
}

void readVariables() {
  EEPROM.begin(8);
  suhuAirMin = EEPROM_readFloat(0);
  suhuUdaraMax = EEPROM_readFloat(4);
}


void setup() {
  // put your setup code here, to run once:
  readVariables();
  initInterface();
  initDevices();
  initWifi();
  initMqtt();
  lcd.clear();
}

void loop() {
  // put your main code here, to run repeatedly:
  if (!client.connected()) {
    reconnect();
    lcd.clear();
  }
  if (!client.loop()) {
    client.connect("WaterHeater");
  }

  readSensors();
  lcd.setCursor(5, 0); lcd.write(2);
  dtostrf(suhuAir, 2, 1, buff);
  printLcd(6, 0, buff);
  lcd.setCursor(10, 0); lcd.print(" *");
  dtostrf(suhuUdara, 2, 1, buff);
  printLcd(12, 0, buff);
  dtostrf(suhuAirMin, 2, 1, buff);
  printLcd(6, 1, buff);
  dtostrf(suhuUdaraMax, 2, 1, buff);
  printLcd(12, 1, buff);

  lcd.setCursor(0, 0); lcd.write(1);
  if (heater) {
    printLcd(1, 0, "ON ");
    heaterStatus = "true";
    if (suhuUdara < suhuUdaraMax) {
      if (suhuAir < suhuAirMin - 2.0) {
        digitalWrite(pinHeater, LOW);
        digitalWrite(pinLED, HIGH);
      } else {
        if (suhuAir == suhuAirMin - 2.0) buzzer = true;
        digitalWrite(pinHeater, HIGH);
        digitalWrite(pinLED, LOW);
      }
    } else {
      digitalWrite(pinHeater, HIGH);
      digitalWrite(pinLED, LOW);
    }
  } else {
    printLcd(1, 0, "OFF");
    heaterStatus = "false";
    digitalWrite(pinHeater, HIGH);
    digitalWrite(pinLED, LOW);
  }

  if (pompa) {
    digitalWrite(pinPompa, LOW);
    unsigned long currentPompa = millis();
    if (seconds == 30) {
      pompa = false;
      seconds = 0;
    }
    if (currentPompa - previousPompa >= 1000) {
      previousPompa = currentPompa;
      seconds++;
      buzzer = true;
    }
  } else {
    digitalWrite(pinPompa, HIGH);
  }

  if (buzzer && buzzerCount < 3) {
    unsigned long currentBuzzer = millis();
    if (currentBuzzer - previousBuzzer >= 250) {
      previousBuzzer = currentBuzzer;
      tone(pinBuzzer, 1500, 200);
      buzzerCount++;
    }
  } else {
    buzzer = false;
    buzzerCount = 0;
  }

  unsigned long currentSend = millis();
  if (currentSend - previousSend >= 1000) {
    previousSend = currentSend;
    char tempSuhu[8];
    dtostrf(suhuUdara, 1, 2, tempSuhu);
    client.publish("udara/suhu", tempSuhu);

    char tempHumidity[8];
    dtostrf(kelembapanUdara, 1, 2, tempHumidity);
    client.publish("udara/kelembapan", tempHumidity);

    char tempHeater[8];
    dtostrf(suhuAir, 1, 2, tempHeater);
    client.publish("heater/suhu", tempHeater);

    char tempHeaterStatus[8];
    heaterStatus.toCharArray(tempHeaterStatus, 8);
    client.publish("heater/statusIn", tempHeaterStatus);
  }
}

void EEPROM_writeFloat(int ee, float value) {
  byte* p = (byte*)(void*)&value;
  for (int i = 0; i < 4; i++) {
    EEPROM.write(ee++, *p++);
    EEPROM.commit();
  }
}

float EEPROM_readFloat(int ee) {
  float value = 0.0;
  byte* p = (byte*)(void*)&value;
  for (int i = 0; i < 4; i++) {
    *p++ = EEPROM.read(ee++);
  }
  return value;
}
