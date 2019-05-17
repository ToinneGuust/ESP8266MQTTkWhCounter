bool loadMeasurements() {
  File mFile = SPIFFS.open(measurementsFile, "r");
  if (!mFile) {
    saveMeasurements();
    if (configuration.debug) Serial.println("Failed to open config file");
    return false;
  }

  size_t size = mFile.size();
  if (size > 1024) {
    if (configuration.debug) Serial.println("Measurements file size is too large");
    return false;
  }

  // Allocate a buffer to store contents of the file.
  std::unique_ptr<char[]> buf(new char[size]);

  // We don't use String here because ArduinoJson library requires the input
  // buffer to be mutable. If you don't use ArduinoJson, you may as well
  // use configFile.readString instead.
  mFile.readBytes(buf.get(), size);

  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(buf.get());

  if (!json.success()) {
    if (configuration.debug) Serial.println("Failed to parse measurements file");
    return false;
  }

  //load config to struct
  if (configuration.debug) Serial.println("loading measurements");

  savedMeasurements.GSCMeter = json["GSCMeter"];
  savedMeasurements.maxSolarOutput = json["maxSolarOutput"];
  savedMeasurements.dailySolarProduction = json["dailySolarProduction"];
  
  if (configuration.debug) Serial.println("Measurements file loaded");

  printJsonConfig(json);

  return true;
}

bool saveMeasurements() {
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();

  //get config from struct
  json["GSCMeter"] = savedMeasurements.GSCMeter;
  json["maxSolarOutput"] = savedMeasurements.maxSolarOutput;
  json["dailySolarProduction"] = savedMeasurements.dailySolarProduction;

  File mFile = SPIFFS.open(measurementsFile, "w");
  if (!mFile) {
    if (configuration.debug) Serial.println("Failed to open measurements file for writing");
    return false;
  }

  json.printTo(mFile);
  if (configuration.debug) json.printTo(Serial);
  if (configuration.debug) Serial.println("");
  if (configuration.debug) Serial.println("Measurements file saved");

  return true;
}
