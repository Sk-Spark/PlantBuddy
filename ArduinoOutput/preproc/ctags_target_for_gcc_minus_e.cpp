# 1 "c:\\Users\\shuku\\spark\\PlantBuddy\\PlantBuddy.ino"
// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full
// license information.

# 6 "c:\\Users\\shuku\\spark\\PlantBuddy\\PlantBuddy.ino" 2
# 7 "c:\\Users\\shuku\\spark\\PlantBuddy\\PlantBuddy.ino" 2
# 8 "c:\\Users\\shuku\\spark\\PlantBuddy\\PlantBuddy.ino" 2
# 9 "c:\\Users\\shuku\\spark\\PlantBuddy\\PlantBuddy.ino" 2
# 17 "c:\\Users\\shuku\\spark\\PlantBuddy\\PlantBuddy.ino"
const char* SCOPE_ID = "0ne00764250";
const char* DEVICE_ID = "Esp8266-test-device";
const char* DEVICE_KEY = "L/rxF75eNGYXfhIJl4GPLFrp2HU/nNm17dKk60eyx5E=";

DHT dht(2 /* D4*/, DHT11 /* DHT 11*/);

int moisture_Pin= 0; // Soil Moisture Sensor input at Analog PIN A0
int moisture_value= 0;

void on_event(IOTContext ctx, IOTCallbackInfo* callbackInfo);
# 28 "c:\\Users\\shuku\\spark\\PlantBuddy\\PlantBuddy.ino" 2

void on_event(IOTContext ctx, IOTCallbackInfo* callbackInfo) {
  // ConnectionStatus
  if (strcmp(callbackInfo->eventName, "ConnectionStatus") == 0) {
    do { Serial.printf("  - "); Serial.printf("Is connected ? %s (%d)", callbackInfo->statusCode == 0x40 ? "YES" : "NO", callbackInfo->statusCode); Serial.printf("\r\n"); } while (0)

                                         ;
    isConnected = callbackInfo->statusCode == 0x40;
    return;
  }

  // payload buffer doesn't have a null ending.
  // add null ending in another buffer before print
  AzureIOT::StringBuffer buffer;
  if (callbackInfo->payloadLength > 0) {
    buffer.initialize(callbackInfo->payload, callbackInfo->payloadLength);
  }

  do { Serial.printf("  - "); Serial.printf("- [%s] event was received. Payload => %s\n", callbackInfo->eventName, buffer.getLength() ? *buffer : "EMPTY"); Serial.printf("\r\n"); } while (0)
                                                                              ;

  if (strcmp(callbackInfo->eventName, "Command") == 0) {
    do { Serial.printf("  - "); Serial.printf("- Command name was => %s\r\n", callbackInfo->tag); Serial.printf("\r\n"); } while (0);
  }
}


void setup() {
  Serial.begin(9600);

  connect_wifi("SparkMI", "123456789");
  connect_client(SCOPE_ID, DEVICE_ID, DEVICE_KEY);

  if (context != __null) {
    lastTick = 0; // set timer in the past to enable first telemetry a.s.a.p
  }
   dht.begin();
}

void loop() {

  float h = dht.readHumidity();
  float t = dht.readTemperature();
  moisture_value= analogRead(moisture_Pin);
  // moisture_value= 5675;
  moisture_value= moisture_value/10;

  if (isConnected) {

    unsigned long ms = millis();
    if (ms - lastTick > 10000) { // send telemetry every 10 seconds      

      char msg[64] = {0};
      int pos = 0, errorCode = 0;

      lastTick = ms;
      if (loopId++ % 2 == 0) { // send telemetry
        pos = snprintf(msg, sizeof(msg) - 1, "{\"Temperature\": %f, \"Humidity\":%f}", t, h);
        errorCode = iotc_send_telemetry(context, msg, pos);

        pos = snprintf(msg, sizeof(msg) - 1, "{\"Moisture\":%i}", moisture_value);
        errorCode = iotc_send_telemetry(context, msg, pos);

        // pos = snprintf(msg, sizeof(msg) - 1, "{\"Humidity\":89}", h);
        // errorCode = iotc_send_telemetry(context, msg, pos);

      } else { // send property

      }

      msg[pos] = 0;

      if (errorCode != 0) {
        do { Serial.printf("X - Error at %s:%d\r\n\t", "PlantBuddy.ino", 101); Serial.printf("Sending message has failed with error code %d", errorCode); Serial.printf("\r\n"); } while (0);
      }
    }

    iotc_do_work(context); // do background work for iotc
  } else {
    iotc_free_context(context);
    context = __null;
    connect_client(SCOPE_ID, DEVICE_ID, DEVICE_KEY);
  }
  // yield();
}
