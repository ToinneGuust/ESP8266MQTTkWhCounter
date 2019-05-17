bool loadConfig() {
  Serial.println("LoadConfig");
  File configFile = SPIFFS.open(configurationFile, "r");
  Serial.println("1");
  if (!configFile) {
    Serial.println("2");
    saveConfig();
    Serial.println("3");
    if (configuration.debug) Serial.println("Failed to open config file");
    Serial.println("4");
    return false;
  }
  Serial.println("5");
  size_t size = configFile.size();
  if (size > 1024) {
    if (configuration.debug) Serial.println("Config file size is too large");
    return false;
  }
  Serial.println("6");
  // Allocate a buffer to store contents of the file.
  std::unique_ptr<char[]> buf(new char[size]);

  // We don't use String here because ArduinoJson library requires the input
  // buffer to be mutable. If you don't use ArduinoJson, you may as well
  // use configFile.readString instead.
  configFile.readBytes(buf.get(), size);
  Serial.println("7");
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(buf.get());

  if (!json.success()) {
    Serial.println("7.1");
    char mqttMessage[200];
    json.printTo(mqttMessage);
    Serial.println(mqttMessage);
    if (configuration.debug) Serial.println("Failed to parse config file");
    return false;
  }
  Serial.println("8");
  //load config to struct
  if (configuration.debug) Serial.println("loading config");

  configuration.fanStartTemp = json["fanStartTemp"];
  configuration.fanStopTemp = json["fanStopTemp"];
  configuration.debug  = json["debug"].as<bool>();
  if (configuration.debug) Serial.println("Configuration file loaded");

  printJsonConfig(json);

  return true;
}

bool saveConfig() {
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();

  //get config from struct
  json["fanStartTemp"] = configuration.fanStartTemp;
  json["fanStopTemp"] = configuration.fanStopTemp;
  json["debug"] = configuration.debug;

  File configFile = SPIFFS.open(configurationFile, "w");
  if (!configFile) {
    if (configuration.debug) Serial.println("Failed to open config file for writing");
    return false;
  }

  json.printTo(configFile);
  if (configuration.debug) json.printTo(Serial);
  if (configuration.debug) Serial.println("");
  if (configuration.debug) Serial.println("Configuration file saved");

  printJsonConfig(json);

  return true;
}

void printJsonConfig(JsonObject& json)
{
  char mqttMessage[200];

  json.printTo(mqttMessage);
  if (configuration.debug) Serial.println(mqttMessage);
  sendMQTTMessage(configTopic, mqttMessage, false);
}
