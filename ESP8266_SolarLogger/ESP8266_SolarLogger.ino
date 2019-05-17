//If connection errors on MQTT try reducing the spiffs size to 1M
//create credentials.h in libraries directory to be used as a global file with credentials across sketches
//Example:
//
//#define mySSID "MY_WIFI"
//#define mySSIDPASSWORD "MY_WIFI_PASSWORD"
//
//#define myMQTTSERVER "MQTT_SERVER_ADDRESSQ"
//#define myMQTTPORT 1883
//#define myMQTTUSER "MQTT_USER"
//#define myMQTTPASSWORD "MQTT_PASSWORD"


#include "FS.h"
#include <OneWire.h>
#include <ArduinoJson.h> //Use version 5.x
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <DallasTemperature.h>
#include <credentials.h> // File defining all credentials e.g: mySSID, mySSIDPASSWORD
//If the MQTT port is defined as 8883 then load WiFiClientSecure to support MQTT + TLS
#if myMQTTPORT==8883
  #include <WiFiClientSecure.h>
#endif



const char* configurationFile = "/config.cfg";
const char* measurementsFile = "/measurements.cfg";

const char* ssid = mySSID;
const char* pass = mySSIDPASSWORD;

char mqtt_server[40] = myMQTTSERVER;
int mqtt_server_port = myMQTTPORT;
char mqtt_clientName[40] = "Solar";
char mqtt_username[40] = myMQTTUSER;
char mqtt_password[40] = myMQTTPASSWORD;


//If the MQTT port is defined as 8883 then load WiFiClientSecure to support MQTT + TLS

#if myMQTTPORT==8883
  WiFiClientSecure wifiClient;
#else
  WiFiClient wifiClient;
#endif
//WiFiClientSecure wifiClient;
PubSubClient client(mqtt_server, mqtt_server_port , wifiClient);

char configTopic[] = "storage/solar/config";
char logTopic[] = "log/storage/solar";

char configfanstarttemp[] = "storage/solar/config/fanstarttemp";
char configfanstoptemp[] = "storage/solar/config/fanstoptemp";
char configdebug[] = "storage/solar/config/debug";

struct Configuration
{
  byte fanStartTemp;
  byte fanStopTemp;
  bool debug;
};

struct Measurements
{
  unsigned long GSCMeter;
  float maxSolarOutput;
  float dailySolarProduction;
};

Configuration configuration;
Measurements savedMeasurements;

unsigned long savedMeasurementsMillis = 0;
unsigned long savedMeasurementsInterval = 60000;

OneWire oneWireInverterTemp(D4);
DallasTemperature sensorInverterTemp(&oneWireInverterTemp);

struct Sensor
{
  char* topic;
  float oldValue = 0;
  float value = 0;
  unsigned long lastMessageSend = 0;
  unsigned long messageInterval = 10000;
};

Sensor inverterTemp, solarOutput, maxSolarOutput, dailySolarProduction, GSCMeter, pulseLength, pingpong;

const byte fanpin = D7;
const byte ledpin = D6;
boolean ledpinState = false;
const byte S0PinNumber = D5;

bool loadConfig();
bool loadMeasurements();
bool saveConfig();
void setup_wifi();
void sendMQTTMessage(char* topic, char* message, bool persistent);

unsigned long lastSendConfigurationMillis = 0;

//S0 Pulse variables
const unsigned long minimumPulseLength = 30;
const unsigned long maximumPulseLength = 70;
unsigned long risingFlankMillis = 0;
unsigned long fallingFlankMillis = 0;
float lastSuccesfullPulse = 0;

//Interrupt Variables
const byte minimumInteruptDifference = 360;          //Minimum ms between interrupts prevents accidental double counts 360 ms is good up to 10.000W
volatile unsigned long lastInterruptMillis[2];  //Program variable DO NOT MODIFY
//volatile unsigned short S0Counter = 0;               //Program variable DO NOT MODIFY
volatile long volatileProd = 0;                //Program variable DO NOT MODIFY

//Power variables
const long millisinHour = 3600000;

//Const variables
const unsigned long currentPowerMilisReset = 600000;
const unsigned long DailyProdMilisReset = 7200000;

void ICACHE_RAM_ATTR S0interrupt()
{
  //The interrupt first falls then rises within +/- 50ms
  if (digitalRead(S0PinNumber) == LOW)
  {
    fallingFlankMillis = millis();
  }
  else
  {
    risingFlankMillis = millis();
    if (fallingFlankMillis - lastInterruptMillis[0] > minimumInteruptDifference && (risingFlankMillis - fallingFlankMillis) >= minimumPulseLength && (risingFlankMillis - fallingFlankMillis) <= maximumPulseLength)
    {
      volatileProd++;
      lastInterruptMillis[1] = lastInterruptMillis[0];
      lastInterruptMillis[0] = fallingFlankMillis;
      lastSuccesfullPulse = risingFlankMillis - fallingFlankMillis;
    }
  }
}

void setup() {

  Serial.begin(115200);
  delay(1000);
  pinMode(S0PinNumber,INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(S0PinNumber), S0interrupt, CHANGE);
  pinMode(fanpin, OUTPUT);
  pinMode(ledpin, OUTPUT);
  pinMode(D4, OUTPUT);
  digitalWrite(D4, HIGH);
  DeviceAddress deviceAddress;
  sensorInverterTemp.begin();
  sensorInverterTemp.getAddress(deviceAddress, 0);
  sensorInverterTemp.setResolution(deviceAddress, 12);
  setup_wifi();
  SPIFFS.begin();
  loadConfig();
  loadMeasurements();
  GSCMeter.value = savedMeasurements.GSCMeter;
  maxSolarOutput.value = savedMeasurements.maxSolarOutput;
  dailySolarProduction.value = savedMeasurements.dailySolarProduction;
  configuration.debug = true;

  inverterTemp.topic = "storage/solar/inverterTemp";
  solarOutput.topic = "storage/solar/solarOutput";
  maxSolarOutput.topic = "storage/solar/maxSolarOutput";
  dailySolarProduction.topic = "storage/solar/dailySolarProduction";
  GSCMeter.topic = "storage/solar/GSCMeter";
  pulseLength.topic = "storage/solar/pulseLength";
  pingpong.topic = "storage/solar/pingpong";

  if (!isValidConfig(configuration))
  {
    Serial.println("Invalid configuration, formatting and recreating default config" );
    SPIFFS.format();
    configuration.fanStartTemp = 26;
    configuration.fanStopTemp = 25;
    configuration.debug = true;
    saveConfig();
  }
  //sendMQTTMessage("pushbullet/storage/solar", "SolarLogger booted v0.1", false);
  saveConfig();
  sendConfiguration();
}

void loop() {
  if (client.connected())
  {
    client.loop();
  }
  measureTemps();
  recalculatePowerVariables();
  setFan();
  if (pingpong.value == 0) {
    pingpong.value = 1;
  } else {
    pingpong.value = 0;
  }

  if (GSCMeter.value - savedMeasurements.GSCMeter >= 10 || maxSolarOutput.value > savedMeasurements.maxSolarOutput || millis() - savedMeasurementsMillis > savedMeasurementsInterval)
  {
    if (configuration.debug) Serial.println("Saving measurements to SPIFFS");
    savedMeasurements.GSCMeter = GSCMeter.value;
    savedMeasurements.maxSolarOutput = maxSolarOutput.value;
    savedMeasurements.dailySolarProduction = dailySolarProduction.value;
    saveMeasurements();
    savedMeasurementsMillis = millis();
  }


  SendWirelessData();
  digitalWrite(ledpin,ledpinState);
  ledpinState = !ledpinState;
}

void measureTemps()
{
  sensorInverterTemp.requestTemperatures();
  digitalWrite(D4, HIGH);
  delay2(750);

  float tempValue;

  tempValue = sensorInverterTemp.getTempCByIndex(0);
  if (tempValue != -100 && tempValue != -127) inverterTemp.value = tempValue;
  delay2(0);

  digitalWrite(D4, HIGH);
}

void setFan()
{
  if (inverterTemp.value >= configuration.fanStartTemp)
  {
    digitalWrite(fanpin, HIGH);
  }

  if (inverterTemp.value <= configuration.fanStopTemp)
  {
    digitalWrite(fanpin, LOW);
  }
}

void SendWirelessData()
{
  sendSensor(inverterTemp);
  sendSensor(solarOutput);
  sendSensor(maxSolarOutput);
  sendSensor(dailySolarProduction);
  sendSensor(GSCMeter);
  sendSensor(pulseLength);
  sendSensor(pingpong);
}






char *ftoa(char *a, double f, int precision)
{
  long p[] = {0, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000};

  char *ret = a;
  long heiltal = (long)f;
  itoa(heiltal, a, 10);
  while (*a != '\0') a++;
  *a++ = '.';
  long desimal = abs((long)((f - heiltal) * p[precision]));
  itoa(desimal, a, 10);
  return ret;
}

//Do some maintenance while sleeping
void delay2(unsigned long duration)
{
  unsigned long startMillis = millis();
  do
  {
    yield();
    client.loop();
  } while ((millis() - startMillis) < duration);
}

bool isValidConfig(Configuration c)
{
  //These values cannot be 0
  if (c.fanStartTemp <= 0 || c.fanStopTemp <= 0)
  {
    if (configuration.debug) Serial.print("ConfigValidation;");
    if (configuration.debug) Serial.println("Invalid configuration detected reason: fanStartTemp <= 0 || fanStopTemp <= 0");
    Serial.println(c.fanStartTemp);
    Serial.println(c.fanStopTemp);
    return false;
  }

  //start temperature must be smaller then stop temp must be smaller then full speed temp
  if (!(c.fanStartTemp > c.fanStopTemp))
  {
    if (configuration.debug) Serial.print("ConfigValidation;");
    if (configuration.debug) Serial.println("Invalid configuration detected reason: fanStartTemp > fanStopTemp");
    Serial.println(c.fanStartTemp);
    Serial.println(c.fanStopTemp);
    return false;
  }

  if (configuration.debug) Serial.println("ConfigValidation;Config Valid");
  return true;
}

bool areConfigsEqual(Configuration c1, Configuration c2)
{
  if (c1.fanStartTemp != c2.fanStartTemp) return false;
  if (c1.fanStopTemp != c2.fanStopTemp)return false;
  if (c1.debug != c2.debug) return false;
  return true;
}

void sendConfiguration()
{
  //Only send configuration if the last one was send more then one second ago (To mittigate configuration trigger loop)
  if (millis() - lastSendConfigurationMillis > 1000)
  {
    char buf[10];
    sendMQTTMessage(configfanstarttemp, ftoa(buf, configuration.fanStartTemp, 2), false);
    sendMQTTMessage(configfanstoptemp, ftoa(buf, configuration.fanStopTemp, 2), false);
    sendMQTTMessage(configdebug, ftoa(buf, configuration.debug, 2), false);
    lastSendConfigurationMillis = millis();
  }
}

void recalculatePowerVariables()
{
  unsigned long tempInterruptMillis[2];
  tempInterruptMillis[0] = lastInterruptMillis[0];
  tempInterruptMillis[1] = lastInterruptMillis[1];

  //store volatile in permanent variable
  while (volatileProd > 0)
  {
    dailySolarProduction.value++;
    GSCMeter.value++;
    volatileProd--;
  }
  pulseLength.value = lastSuccesfullPulse;


  //Calculate Currentpower
  if (millis() - tempInterruptMillis[0] > currentPowerMilisReset || tempInterruptMillis[1] == 0)
  {
    solarOutput.value = 0.00;
  }
  else
  {
    solarOutput.value = (float)millisinHour / (float)(tempInterruptMillis[0] - tempInterruptMillis[1]);
    if (solarOutput.value > maxSolarOutput.value)
    {
      maxSolarOutput.value = solarOutput.value;
    }
  }

  //Reset Dailyprod and maxPower if needed
  if (millis() - tempInterruptMillis[0] > DailyProdMilisReset && dailySolarProduction.value != 0.00)
  {
    Serial.println("resetting daily values");
    dailySolarProduction.value = 0.00;
    maxSolarOutput.value = 0.00;
    savedMeasurements.maxSolarOutput = 0.00;
    savedMeasurements.dailySolarProduction = 0.00;
  }
}
