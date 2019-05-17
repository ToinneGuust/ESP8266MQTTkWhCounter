void reconnectToMQTT() {
  // Loop until we're reconnected
  if (configuration.debug) Serial.println("Starting reconnect");
  while (!client.connected()) {
    if (configuration.debug) Serial.println("Attempting MQTT connection...");
    // Attempt to connect
    if (configuration.debug) Serial.println(mqtt_server);
    if (configuration.debug) Serial.println(mqtt_server_port);
    if (configuration.debug) Serial.println(mqtt_clientName);
    if (configuration.debug) Serial.println(mqtt_username);
    if (configuration.debug) Serial.println(mqtt_password);
    if (configuration.debug) Serial.println(logTopic);

    if (client.connect(mqtt_clientName, mqtt_username, mqtt_password, logTopic, 1, true, "SolarLogger disconnected from MQTT server")) {
      if (configuration.debug) Serial.println("connected");
      sendMQTTMessage(logTopic, "Living CV fan connected to MQTT server", false);
      client.setCallback(callback);
      subscribe();

    } else {
      if (configuration.debug) Serial.print("failed, rc=");
      if (configuration.debug) Serial.print(client.state());
      if (configuration.debug) Serial.println(" try again in 5 seconds");
      // Wait 1 seconds before retrying
      delay(3000);
      ESP.wdtFeed();
    }
  }
}

void sendSensor(Sensor &sensor)
{

  unsigned long lastMessageSend = 0;
  unsigned long messageInterval = 10000;
  if (sensor.oldValue != sensor.value || millis() - sensor.lastMessageSend >= sensor.messageInterval)
  {
    char buf[10];
    sendMQTTMessage(sensor.topic, ftoa(buf, sensor.value, 2), true);
    sensor.oldValue = sensor.value;
    sensor.lastMessageSend = millis();
    delay2(100);
  }
}

void sendMQTTMessage(char* topic, char* message, bool persistent) {

  if (client.connected()) {
    if (configuration.debug) Serial.print(topic);
    if (configuration.debug) Serial.print(" => ");
    if (configuration.debug) Serial.println(message);
    client.publish(topic, message, persistent);
  } else {
    if (configuration.debug) Serial.println("Reconnecting to MQTT");
    reconnectToMQTT();
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  char payloadcpy[length + 1];
  for (int i = 0; i < length; i++) {
    payloadcpy[i] = payload[i];
  }
  payloadcpy[length + 1] = '\0';

  if (configuration.debug) Serial.print("Message arrived [");
  if (configuration.debug) Serial.print(topic);
  if (configuration.debug) Serial.print("] :");
  if (configuration.debug) Serial.println(payloadcpy);

  Configuration tempConfig = configuration;

  if (strcmp(topic, configfanstarttemp) == 0) tempConfig.fanStartTemp = atoi((char*)payloadcpy);
  if (strcmp(topic, configfanstoptemp) == 0) tempConfig.fanStopTemp = atoi((char*)payloadcpy);
  if (strcmp(topic, configdebug) == 0)
  {
    if (atoi((char*)payloadcpy) == 0) tempConfig.debug = false;
    if (atoi((char*)payloadcpy) == 1) tempConfig.debug = true;
  }

  if (isValidConfig(tempConfig) && !areConfigsEqual(configuration, tempConfig))
  {
    configuration = tempConfig;
    saveConfig();
    sendConfiguration();
  }

}


void subscribe()
{
  client.subscribe(configfanstarttemp);
  client.subscribe(configfanstoptemp);
  client.subscribe(configdebug);
}
