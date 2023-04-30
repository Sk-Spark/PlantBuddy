// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

#include <stdlib.h>
#include <stdarg.h>

#include <az_core.h>
#include <az_iot.h>

#include "AzureIoT.h"
#include "Azure_IoT_PnP_Template.h"

#include <az_precondition_internal.h>

// OLed Display
#include "oled_display.h"

// DTH 11 sensore
#include <DHT.h>
#define DHTTYPE DHT11   // DHT 11

// EEPROM for storing pot moisture threshold
#include <EEPROM.h>
#define EEPROM_SIZE 5 // 5 bytes for storing pot moisture threshold
#define EEPROM_ADDR_POT1_MOISTURE_THRESHOLD 0 // 1st byte for storing pot moisture threshold


/* --- Defines --- */
// #define AZURE_PNP_MODEL_ID "dtmi:azureiot:devkit:freertos:Esp32AzureIotKit;1"
#define AZURE_PNP_MODEL_ID "dtmi:azureiot:devkit:freertos:PlantsBuddy;1"

#define SAMPLE_DEVICE_INFORMATION_NAME                 "deviceInformation"
#define SAMPLE_MANUFACTURER_PROPERTY_NAME              "manufacturer"
#define SAMPLE_MODEL_PROPERTY_NAME                     "model"
#define SAMPLE_SOFTWARE_VERSION_PROPERTY_NAME          "swVersion"
#define SAMPLE_OS_NAME_PROPERTY_NAME                   "osName"
#define SAMPLE_PROCESSOR_ARCHITECTURE_PROPERTY_NAME    "processorArchitecture"
#define SAMPLE_PROCESSOR_MANUFACTURER_PROPERTY_NAME    "processorManufacturer"
#define SAMPLE_TOTAL_STORAGE_PROPERTY_NAME             "totalStorage"
#define SAMPLE_TOTAL_MEMORY_PROPERTY_NAME              "totalMemory"

#define SAMPLE_MANUFACTURER_PROPERTY_VALUE             "ESPRESSIF"
#define SAMPLE_MODEL_PROPERTY_VALUE                    "ESP32"
#define SAMPLE_VERSION_PROPERTY_VALUE                  "1.0.0"
#define SAMPLE_OS_NAME_PROPERTY_VALUE                  "FreeRTOS"
#define SAMPLE_ARCHITECTURE_PROPERTY_VALUE             "ESP32 WROVER-B"
#define SAMPLE_PROCESSOR_MANUFACTURER_PROPERTY_VALUE   "ESPRESSIF"
// The next couple properties are in KiloBytes.
#define SAMPLE_TOTAL_STORAGE_PROPERTY_VALUE            4096
#define SAMPLE_TOTAL_MEMORY_PROPERTY_VALUE             8192

//soil moisture values
#define MAX_SOIL_MOISTURE                              1900 // 100% soil moisture
#define MIN_SOIL_MOISTURE                              3440 // 0% soil moisture

#define TELEMETRY_PROP_NAME_TEMPERATURE                "temperature"
#define TELEMETRY_PROP_NAME_SOIL_MOISTURE_POT1         "pot1"
#define TELEMETRY_PROP_NAME_HUMIDITY                   "humidity"

static az_span COMMAND_NAME_TOGGLE_LED_1 = AZ_SPAN_FROM_STR("ToggleLed1");
static az_span COMMAND_NAME_TOGGLE_LED_2 = AZ_SPAN_FROM_STR("ToggleLed2");
static az_span COMMAND_NAME_DISPLAY_TEXT = AZ_SPAN_FROM_STR("DisplayText");
static az_span COMMAND_NAME_THRESHOLD_UPDATE_POT1 = AZ_SPAN_FROM_STR("ThresholdUpdate_Pot1");
#define COMMAND_RESPONSE_CODE_ACCEPTED                 202
#define COMMAND_RESPONSE_CODE_REJECTED                 404

#define WRITABLE_PROPERTY_TELEMETRY_FREQ_SECS          "telemetryFrequencySecs"
#define WRITABLE_PROPERTY_RESPONSE_SUCCESS             "success"

#define DOUBLE_DECIMAL_PLACE_DIGITS 2

/* --- Function Checks and Returns --- */
#define RESULT_OK       0
#define RESULT_ERROR    __LINE__

#define EXIT_IF_TRUE(condition, retcode, message, ...)                              \
  do                                                                                \
  {                                                                                 \
    if (condition)                                                                  \
    {                                                                               \
      LogError(message, ##__VA_ARGS__ );                                            \
      return retcode;                                                               \
    }                                                                               \
  } while (0)

#define EXIT_IF_AZ_FAILED(azresult, retcode, message, ...)                                   \
  EXIT_IF_TRUE(az_result_failed(azresult), retcode, message, ##__VA_ARGS__ )

/* --- Data --- */
#define DATA_BUFFER_SIZE 1024
static uint8_t data_buffer[DATA_BUFFER_SIZE];
static uint32_t telemetry_send_count = 0;

static size_t telemetry_frequency_in_seconds = TELEMETRY_FREQUENCY_IN_SECONDS;
static time_t last_telemetry_send_time = INDEFINITE_TIME;

static bool led1_on = false;
static bool led2_on = false;

/* --- DHT Sensor --- */
#define DHTPIN 4 
DHT dht(DHTPIN, DHTTYPE);

// --- Soil Moisture Sensor Read Interval --- //
static time_t last_soil_moisture_read_time = INDEFINITE_TIME;
uint soil_moisture_read = 0;
int soil_moisture_pot1 = 0;
float soil_moisture_pot1_avg = 0;
bool sendTelemeteryNow = false;

// For Pot 1
#define POT1_PIN 34
#define DEFAULT_POT1_MOISTURE_THRESHOLD 65 // Default Pot1 moisture threshold in %, 0-100
int POT1_MOISTURE_THRESHOLD = 0; 

// --- Pump --- //
#define PUMP1_PIN 18
#define PUMP_RUN_DURATION_IN_MILLISECS 1000*10 // 10 seconds
static bool run_pump1 = false;
static unsigned long pump1_ran_at = 0;


/* --- Function Prototypes --- */
/* Please find the function implementations at the bottom of this file */
static int generate_telemetry_payload(
  uint8_t* payload_buffer, size_t payload_buffer_size, size_t* payload_buffer_length);
static int generate_device_info_payload(
  az_iot_hub_client const* hub_client, uint8_t* payload_buffer,
  size_t payload_buffer_size, size_t* payload_buffer_length);
static int consume_properties_and_generate_response(
  azure_iot_t* azure_iot, az_span properties,
  uint8_t* buffer, size_t buffer_size, size_t* response_length);

static void readSoilMoistureThresholdFromEEPROM();

/* --- Public Functions --- */
void azure_pnp_init()
{
  LogInfo("Initializing Azure IoT PnP Client");
  // --- DHT Sensor ---
  dht.begin();
  pinMode(PUMP1_PIN, OUTPUT);
  pinMode(POT1_PIN, INPUT);
  digitalWrite(PUMP1_PIN, LOW);
  
  // --- OLED Display ----
  setupDisplay();

  // initialize EEPROM with predefined size
  EEPROM.begin(EEPROM_SIZE);
  readSoilMoistureThresholdFromEEPROM();
}

void readSoilMoistureThresholdFromEEPROM(){
  // read the soil moisture threshold from EEPROM
  POT1_MOISTURE_THRESHOLD = EEPROM.read(EEPROM_ADDR_POT1_MOISTURE_THRESHOLD);
  if(POT1_MOISTURE_THRESHOLD<0 || POT1_MOISTURE_THRESHOLD>100){
    POT1_MOISTURE_THRESHOLD = DEFAULT_POT1_MOISTURE_THRESHOLD;
    EEPROM.write(EEPROM_ADDR_POT1_MOISTURE_THRESHOLD, POT1_MOISTURE_THRESHOLD);
    EEPROM.commit();
  }
  LogInfo("Pot1 moisture threshold [From EEPROM]: %d %%", POT1_MOISTURE_THRESHOLD);
} 

const az_span azure_pnp_get_model_id()
{
  return AZ_SPAN_FROM_STR(AZURE_PNP_MODEL_ID);
}

void azure_pnp_set_telemetry_frequency(size_t frequency_in_seconds)
{
  telemetry_frequency_in_seconds = frequency_in_seconds;
  LogInfo("Telemetry frequency set to once every %d seconds.", telemetry_frequency_in_seconds);
}

/* --- Internal Functions --- */
static int map_soil_moisture_value(int moisture_value){
 int percent = map(moisture_value, MAX_SOIL_MOISTURE , MIN_SOIL_MOISTURE, 100, 0);
  if(percent < 0)
    percent = 0;
  else if(percent > 100)
    percent = 100;
  // Serial.printf("percent: %d%\n",percent);
  return percent;
}

static float get_temperature()
{
  return dht.readTemperature();
}

static float get_humidity()
{
  return dht.readHumidity();
}

static int get_soil_moisture_pot1(){
  int moisture_value = analogRead(POT1_PIN);
  LogInfo("pot1 moisture value: %d", moisture_value);
  return moisture_value;
}

static void turn_pump_on(bool value){
  if(value == true){
    digitalWrite(PUMP1_PIN, HIGH);
    LogInfo("Turning Pump ON.");
  }
  else{
    digitalWrite(PUMP1_PIN, LOW);
    LogInfo("Turning Pump OFF.");
  }
}

static bool is_pump_on(){
  bool pin_state = digitalRead(PUMP1_PIN);
  return pin_state;
}

// For turning on and off pump
void water_pump_handler(){
  if(run_pump1 == true){
    if(millis() > (pump1_ran_at + PUMP_RUN_DURATION_IN_MILLISECS)){
      run_pump1 = false;
      turn_pump_on(false);
      LogInfo("Pump turned OFF. [Over run]");
    }
    else if(!is_pump_on()){
      turn_pump_on(true);
      LogInfo("Pump is ON.");
    }
  }
  else if(run_pump1 == false && is_pump_on()){
    turn_pump_on(false);
    LogInfo("Pump turned OFF. [flag OFF]");
  }
}

// For sending telemetry now
void setSendTelemetryNow(){
  sendTelemeteryNow = true;
}

/* Application-specific data section */

int azure_pnp_send_telemetry(azure_iot_t* azure_iot)
{
  _az_PRECONDITION_NOT_NULL(azure_iot);

  time_t now = time(NULL);

  if (now == INDEFINITE_TIME)
  {
    LogError("Failed getting current time for controlling telemetry.");
    return RESULT_ERROR;
  }
  
  if(last_soil_moisture_read_time == INDEFINITE_TIME || difftime(now, last_soil_moisture_read_time) >= SOIL_MOISTURE_READ_FREQUENCY_IN_SECONDS)
  {
    last_soil_moisture_read_time = now;
    // Read soil moisture here.
    soil_moisture_pot1 = get_soil_moisture_pot1();
    ++soil_moisture_read;
    // Calculate average soil moisture
    soil_moisture_pot1_avg += (soil_moisture_pot1 - soil_moisture_pot1_avg ) / soil_moisture_read;
    int soil_moisture_pot1_percent = map_soil_moisture_value(soil_moisture_pot1_avg); 
    LogInfo("Soil Moisture read: %d / %d -> avg: %f -> percent: %d%%",soil_moisture_pot1, soil_moisture_read, soil_moisture_pot1_avg, soil_moisture_pot1_percent); 
       
    // Checking and setting if pump needs to be run
    if( run_pump1 != true && soil_moisture_pot1_percent < (POT1_MOISTURE_THRESHOLD)){
      run_pump1 = true;
      pump1_ran_at = millis();
      LogInfo("[telemetry fun] Truning pump ON.");
    }
    else if( run_pump1 == true && soil_moisture_pot1_percent >= (POT1_MOISTURE_THRESHOLD + 5) ){
      run_pump1 = false;
      LogInfo("[telemetry fun] Truning pump OFF.");
    }
  }
  // Turn pump on and off
  water_pump_handler();

  if ( sendTelemeteryNow || (last_telemetry_send_time != INDEFINITE_TIME && difftime(now, last_telemetry_send_time) >= telemetry_frequency_in_seconds))
  {
    size_t payload_size;

    last_telemetry_send_time = now;
    sendTelemeteryNow = false;

    if (generate_telemetry_payload(data_buffer, DATA_BUFFER_SIZE, &payload_size) != RESULT_OK)
    {
      LogError("Failed generating telemetry payload.");
      return RESULT_ERROR;
    }

    if (azure_iot_send_telemetry(azure_iot, az_span_create(data_buffer, payload_size)) != 0)
    {
      LogError("Failed sending telemetry.");
      return RESULT_ERROR;
    }
  }
  else if(last_telemetry_send_time == INDEFINITE_TIME){
    last_telemetry_send_time = now;
  }

  return RESULT_OK;
}

int azure_pnp_send_device_info(azure_iot_t* azure_iot, uint32_t request_id)
{
  _az_PRECONDITION_NOT_NULL(azure_iot);

  int result;
  size_t length;  
    
  result = generate_device_info_payload(&azure_iot->iot_hub_client, data_buffer, DATA_BUFFER_SIZE, &length);
  EXIT_IF_TRUE(result != RESULT_OK, RESULT_ERROR, "Failed generating telemetry payload.");

  result = azure_iot_send_properties_update(azure_iot, request_id, az_span_create(data_buffer, length));
  EXIT_IF_TRUE(result != RESULT_OK, RESULT_ERROR, "Failed sending reported properties update.");

  return RESULT_OK;
}

int azure_pnp_handle_command_request(azure_iot_t* azure_iot, command_request_t command)
{
  _az_PRECONDITION_NOT_NULL(azure_iot);

  uint16_t response_code;

  if (az_span_is_content_equal(command.command_name, COMMAND_NAME_TOGGLE_LED_1))
  {
    led1_on = !led1_on;
    LogInfo("LED 1 state: %s", (led1_on ? "ON" : "OFF"));
    LogInfo("Command not recognized (%.*s).", az_span_size(command.command_name), az_span_ptr(command.command_name));
    response_code = COMMAND_RESPONSE_CODE_ACCEPTED;
  }
  else if (az_span_is_content_equal(command.command_name, COMMAND_NAME_TOGGLE_LED_2))
  {
    led2_on = !led2_on;
    LogInfo("LED 2 state: %s", (led2_on ? "ON" : "OFF"));
    response_code = COMMAND_RESPONSE_CODE_ACCEPTED;
  }
  else if (az_span_is_content_equal(command.command_name, COMMAND_NAME_DISPLAY_TEXT))
  {
    // The payload comes surrounded by quotes, so to remove them we offset the payload by 1 and its size by 2.
    LogInfo("OLED display: %.*s", az_span_size(command.payload) - 2, az_span_ptr(command.payload) + 1);
    response_code = COMMAND_RESPONSE_CODE_ACCEPTED;
  }
  else if (az_span_is_content_equal(command.command_name, COMMAND_NAME_THRESHOLD_UPDATE_POT1))
  {
    char* parse_string = (char*)az_span_ptr(command.payload);
    parse_string[az_span_size(command.payload)] = '\0';
    // String parse_string = String(parse_string);
    int new_threshold = atoi(parse_string);
    LogInfo("Threshold Update Pot_1: %d, Size:%d", new_threshold, az_span_size(command.payload) );
    //Check for valid threshold value
    if(new_threshold < 0 || new_threshold > 100){
      LogError("Invalid threshold value: %d", new_threshold);
      response_code = COMMAND_RESPONSE_CODE_REJECTED;
      return azure_iot_send_command_response(azure_iot, command.request_id, response_code, AZ_SPAN_LITERAL_FROM_STR("Invalid threshold value") );
    }
    else{
      // POT1_MOISTURE_THRESHOLD = new_threshold;
      LogInfo("New threshold value: %d", new_threshold);
      POT1_MOISTURE_THRESHOLD = new_threshold;
      response_code = COMMAND_RESPONSE_CODE_ACCEPTED;
    }

    // Update threshold value to EEPROM
    EEPROM.put(EEPROM_ADDR_POT1_MOISTURE_THRESHOLD, new_threshold);
    EEPROM.commit();
    response_code = COMMAND_RESPONSE_CODE_ACCEPTED;
    setSendTelemetryNow(); // To send telemetry with new threshold value
  }
  else
  {
    LogError("Command not recognized (%.*s).", az_span_size(command.command_name), az_span_ptr(command.command_name));
    response_code = COMMAND_RESPONSE_CODE_REJECTED;
  }

  return azure_iot_send_command_response(azure_iot, command.request_id, response_code, AZ_SPAN_EMPTY);
}

int azure_pnp_handle_properties_update(azure_iot_t* azure_iot, az_span properties, uint32_t request_id)
{
  _az_PRECONDITION_NOT_NULL(azure_iot);
  _az_PRECONDITION_VALID_SPAN(properties, 1, false);

  int result;
  size_t length;

  result = consume_properties_and_generate_response(azure_iot, properties, data_buffer, DATA_BUFFER_SIZE, &length);
  EXIT_IF_TRUE(result != RESULT_OK, RESULT_ERROR, "Failed generating properties ack payload.");

  result = azure_iot_send_properties_update(azure_iot, request_id, az_span_create(data_buffer, length));
  EXIT_IF_TRUE(result != RESULT_OK, RESULT_ERROR, "Failed sending reported properties update.");

  return RESULT_OK;
}

static int generate_telemetry_payload(uint8_t* payload_buffer, size_t payload_buffer_size, size_t* payload_buffer_length)
{
  az_json_writer jw;
  az_result rc;
  az_span payload_buffer_span = az_span_create(payload_buffer, payload_buffer_size);
  az_span json_span;
  float temperature, humidity, light, pressure, altitude;
  int soil_moisture_pot1_percent;
  int32_t magneticFieldX, magneticFieldY, magneticFieldZ;
  int32_t pitch, roll, accelerationX, accelerationY, accelerationZ;

  humidity = get_humidity();
  temperature = get_temperature();
  soil_moisture_pot1_percent = map_soil_moisture_value(soil_moisture_pot1_avg);

  // Logging the values
  LogInfo("Temp: %f °C", temperature);
  LogInfo("Humidity: %f", humidity);
  LogInfo("pot1 sensor avg value: %.0f", soil_moisture_pot1_avg);
  LogInfo("pot1 moiture: %d%%", soil_moisture_pot1_percent);

  displayToLed(temperature, soil_moisture_pot1_percent);

  rc = az_json_writer_init(&jw, payload_buffer_span, NULL);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed initializing json writer for telemetry.");

  rc = az_json_writer_append_begin_object(&jw);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed setting telemetry json root.");
  // Temperature telemetry
  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(TELEMETRY_PROP_NAME_TEMPERATURE));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding temperature property name to telemetry payload.");
  rc = az_json_writer_append_double(&jw, temperature, DOUBLE_DECIMAL_PLACE_DIGITS);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding temperature property value to telemetry payload. ");
  // Humidity telemetry
  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(TELEMETRY_PROP_NAME_HUMIDITY));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding humidity property name to telemetry payload.");
  rc = az_json_writer_append_double(&jw, humidity, DOUBLE_DECIMAL_PLACE_DIGITS);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding humidity property value to telemetry payload. ");
  // Soil moisture telemetry in percent
  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR("pot1_moisture_percent"));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding soil_moisture_pot1 property name to telemetry payload.");
  rc = az_json_writer_append_int32(&jw, soil_moisture_pot1_percent);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding soil_moisture_pot1 property value to telemetry payload. ");
  // Soil moisture telemetry raw sensor value
  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR("pot1_moisture_sensore_value"));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding soil_moisture_pot1 property name to telemetry payload.");
  rc = az_json_writer_append_int32(&jw, soil_moisture_pot1_avg);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding soil_moisture_pot1 property value to telemetry payload. ");
  // Thresholds telemetry
  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR("threshold1"));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding thrshold for pot1 property name to telemetry payload.");
  rc = az_json_writer_append_int32(&jw, POT1_MOISTURE_THRESHOLD);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding thrshold for pot1 property value to telemetry payload. ");
  // Pump1 telemetry
  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR("pump1"));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding pump1 property name to telemetry payload.");
  rc = az_json_writer_append_int32(&jw, run_pump1);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding pump1 property value to telemetry payload. ");

  rc = az_json_writer_append_end_object(&jw);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed closing telemetry json payload.");

  payload_buffer_span = az_json_writer_get_bytes_used_in_destination(&jw);

  // char msg[200] = {0};
  // snprintf(msg, sizeof(msg)-1, "{\"temp\": %d, \"humidity\": %d, \"pump\": %d, \"pot1\": { \"moisture\":%d, \"threshold\": %d}}", temperature, humidity, run_pump1, soil_moisture_pot1, POT1_MOISTURE_THRESHOLD);
  // payload_buffer_span = az_span_copy(payload_buffer_span, az_span_create_from_str(msg) );
  // payload_buffer_span = az_span_copy_u8(payload_buffer_span, '\0');

  if ((payload_buffer_size - az_span_size(payload_buffer_span)) < 1)
  {
    LogError("Insufficient space for telemetry payload null terminator.");
    return RESULT_ERROR;
  }

  payload_buffer[az_span_size(payload_buffer_span)] = null_terminator;
  *payload_buffer_length = az_span_size(payload_buffer_span);

  // Resetting the soil moisture read count and average
  soil_moisture_pot1_avg = 0;
  soil_moisture_read = 0;
 
  return RESULT_OK;
}

static int generate_device_info_payload(az_iot_hub_client const* hub_client, uint8_t* payload_buffer, size_t payload_buffer_size, size_t* payload_buffer_length)
{
  az_json_writer jw;
  az_result rc;
  az_span payload_buffer_span = az_span_create(payload_buffer, payload_buffer_size);
  az_span json_span;

  rc = az_json_writer_init(&jw, payload_buffer_span, NULL);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed initializing json writer for telemetry.");

  rc = az_json_writer_append_begin_object(&jw);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed setting telemetry json root.");
  
  rc = az_iot_hub_client_properties_writer_begin_component(
    hub_client, &jw, AZ_SPAN_FROM_STR(SAMPLE_DEVICE_INFORMATION_NAME));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed writting component name.");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_MANUFACTURER_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_MANUFACTURER_PROPERTY_NAME to payload.");
  rc = az_json_writer_append_string(&jw, AZ_SPAN_FROM_STR(SAMPLE_MANUFACTURER_PROPERTY_VALUE));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_MANUFACTURER_PROPERTY_VALUE to payload. ");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_MODEL_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_MODEL_PROPERTY_NAME to payload.");
  rc = az_json_writer_append_string(&jw, AZ_SPAN_FROM_STR(SAMPLE_MODEL_PROPERTY_VALUE));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_MODEL_PROPERTY_VALUE to payload. ");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_SOFTWARE_VERSION_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_SOFTWARE_VERSION_PROPERTY_NAME to payload.");
  rc = az_json_writer_append_string(&jw, AZ_SPAN_FROM_STR(SAMPLE_VERSION_PROPERTY_VALUE));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_VERSION_PROPERTY_VALUE to payload. ");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_OS_NAME_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_OS_NAME_PROPERTY_NAME to payload.");
  rc = az_json_writer_append_string(&jw, AZ_SPAN_FROM_STR(SAMPLE_OS_NAME_PROPERTY_VALUE));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_OS_NAME_PROPERTY_VALUE to payload. ");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_PROCESSOR_ARCHITECTURE_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_PROCESSOR_ARCHITECTURE_PROPERTY_NAME to payload.");
  rc = az_json_writer_append_string(&jw, AZ_SPAN_FROM_STR(SAMPLE_ARCHITECTURE_PROPERTY_VALUE));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_ARCHITECTURE_PROPERTY_VALUE to payload. ");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_PROCESSOR_MANUFACTURER_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_PROCESSOR_MANUFACTURER_PROPERTY_NAME to payload.");
  rc = az_json_writer_append_string(&jw, AZ_SPAN_FROM_STR(SAMPLE_PROCESSOR_MANUFACTURER_PROPERTY_VALUE));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_PROCESSOR_MANUFACTURER_PROPERTY_VALUE to payload. ");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_TOTAL_STORAGE_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_TOTAL_STORAGE_PROPERTY_NAME to payload.");
  rc = az_json_writer_append_double(&jw, SAMPLE_TOTAL_STORAGE_PROPERTY_VALUE, DOUBLE_DECIMAL_PLACE_DIGITS);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_TOTAL_STORAGE_PROPERTY_VALUE to payload. ");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_TOTAL_MEMORY_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_TOTAL_MEMORY_PROPERTY_NAME to payload.");
  rc = az_json_writer_append_double(&jw, SAMPLE_TOTAL_MEMORY_PROPERTY_VALUE, DOUBLE_DECIMAL_PLACE_DIGITS);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding SAMPLE_TOTAL_MEMORY_PROPERTY_VALUE to payload. ");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR("telemeteryFrequency"));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding Telemetery Frequency to payload.");
  rc = az_json_writer_append_double(&jw, TELEMETRY_FREQUENCY_IN_SECONDS, DOUBLE_DECIMAL_PLACE_DIGITS);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding TELEMETRY_FREQUENCY_IN_SECONDS to payload. ");

  rc = az_iot_hub_client_properties_writer_end_component(hub_client, &jw);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed closing component object.");

  rc = az_json_writer_append_end_object(&jw);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed closing telemetry json payload.");

  payload_buffer_span = az_json_writer_get_bytes_used_in_destination(&jw);

  if ((payload_buffer_size - az_span_size(payload_buffer_span)) < 1)
  {
    LogError("Insufficient space for telemetry payload null terminator.");
    return RESULT_ERROR;
  }

  payload_buffer[az_span_size(payload_buffer_span)] = null_terminator;
  *payload_buffer_length = az_span_size(payload_buffer_span);
 
  return RESULT_OK;
}

static int generate_properties_update_response(
  azure_iot_t* azure_iot,
  az_span component_name, int32_t frequency, int32_t version,
  uint8_t* buffer, size_t buffer_size, size_t* response_length)
{
  az_result azrc;
  az_json_writer jw;
  az_span response = az_span_create(buffer, buffer_size);

  azrc = az_json_writer_init(&jw, response, NULL);
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed initializing json writer for properties update response.");

  azrc = az_json_writer_append_begin_object(&jw);
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed opening json in properties update response.");

  // This Azure PnP Template does not have a named component,
  // so az_iot_hub_client_properties_writer_begin_component is not needed.

  azrc = az_iot_hub_client_properties_writer_begin_response_status(
    &azure_iot->iot_hub_client,
    &jw,
    AZ_SPAN_FROM_STR(WRITABLE_PROPERTY_TELEMETRY_FREQ_SECS),
    (int32_t)AZ_IOT_STATUS_OK,
    version,
    AZ_SPAN_FROM_STR(WRITABLE_PROPERTY_RESPONSE_SUCCESS));
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed appending status to properties update response.");

  azrc = az_json_writer_append_int32(&jw, frequency);
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed appending frequency value to properties update response.");

  azrc = az_iot_hub_client_properties_writer_end_response_status(&azure_iot->iot_hub_client, &jw);
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed closing status section in properties update response.");

  // This Azure PnP Template does not have a named component,
  // so az_iot_hub_client_properties_writer_end_component is not needed.

  azrc = az_json_writer_append_end_object(&jw);
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed closing json in properties update response.");

  *response_length = az_span_size(az_json_writer_get_bytes_used_in_destination(&jw));

  return RESULT_OK;
}

static int consume_properties_and_generate_response(
  azure_iot_t* azure_iot, az_span properties,
  uint8_t* buffer, size_t buffer_size, size_t* response_length)
{
  int result;
  az_json_reader jr;
  az_span component_name;
  int32_t version = 0;

  az_result azrc = az_json_reader_init(&jr, properties, NULL);
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed initializing json reader for properties update.");

  const az_iot_hub_client_properties_message_type message_type =
    AZ_IOT_HUB_CLIENT_PROPERTIES_MESSAGE_TYPE_WRITABLE_UPDATED;

  azrc = az_iot_hub_client_properties_get_properties_version(
    &azure_iot->iot_hub_client, &jr, message_type, &version);
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed writable properties version.");

  azrc = az_json_reader_init(&jr, properties, NULL);
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed re-initializing json reader for properties update.");

  while (az_result_succeeded(
    azrc = az_iot_hub_client_properties_get_next_component_property(
      &azure_iot->iot_hub_client, &jr, message_type,
      AZ_IOT_HUB_CLIENT_PROPERTY_WRITABLE, &component_name)))
  {
    if (az_json_token_is_text_equal(&jr.token, AZ_SPAN_FROM_STR(WRITABLE_PROPERTY_TELEMETRY_FREQ_SECS)))
    {
      int32_t value;
      azrc = az_json_reader_next_token(&jr);
      EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed getting writable properties next token.");

      azrc = az_json_token_get_int32(&jr.token, &value);
      EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed getting writable properties int32_t value.");

      azure_pnp_set_telemetry_frequency((size_t)value);

      result = generate_properties_update_response(
        azure_iot, component_name, value, version, buffer, buffer_size, response_length);
      EXIT_IF_TRUE(result != RESULT_OK, RESULT_ERROR, "generate_properties_update_response failed.");
    }
    else
    {
      LogError("Unexpected property received (%.*s).",
        az_span_size(jr.token.slice), az_span_ptr(jr.token.slice));
    }

    azrc = az_json_reader_next_token(&jr);
    EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed moving to next json token of writable properties.");

    azrc = az_json_reader_skip_children(&jr);
    EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed skipping children of writable properties.");

    azrc = az_json_reader_next_token(&jr);
    EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed moving to next json token of writable properties (again).");
  }

  return RESULT_OK;
}
