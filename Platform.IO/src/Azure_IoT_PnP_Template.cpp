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

// EEPROM for storing persistent settings
#include <EEPROM.h>
#define EEPROM_SIZE 30
#define EEPROM_ADDR_LAST_WATERED_TIME 5       // 4 bytes (address 5-8)
#define EEPROM_ADDR_PUMP_DURATION 10          // 4 bytes (address 10-13), stored in milliseconds
#define EEPROM_ADDR_SCHEDULE_HOUR 18          // 1 byte (0-23, 0xFF = disabled)
#define EEPROM_ADDR_SCHEDULE_MINUTE 19        // 1 byte (0-59)
#define EEPROM_ADDR_LAST_SCHEDULE_DAY 20      // 1 byte (1-31, day of month when schedule last ran)
#define EEPROM_ADDR_SCHEDULE_INTERVAL 21      // 1 byte (1-30, days between runs, default 1)
#define EEPROM_ADDR_LAST_SCHEDULE_EPOCH 22    // 4 bytes (address 22-25), epoch time of last schedule trigger
#define EEPROM_ADDR_ADDITIONAL_SCHEDULE_HOUR 26     // 1 byte (0-23, 0xFF = disabled)
#define EEPROM_ADDR_ADDITIONAL_SCHEDULE_MINUTE 27   // 1 byte (0-59)
#define EEPROM_ADDR_LAST_ADDITIONAL_SCHEDULE_DAY 28  // 1 byte (1-31, day of month when additional schedule last ran)

/* --- Defines --- */
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
#define SAMPLE_TOTAL_STORAGE_PROPERTY_VALUE            4096
#define SAMPLE_TOTAL_MEMORY_PROPERTY_VALUE             8192

static az_span COMMAND_NAME_TOGGLE_LED_1 = AZ_SPAN_FROM_STR("ToggleLed1");
static az_span COMMAND_NAME_TOGGLE_LED_2 = AZ_SPAN_FROM_STR("ToggleLed2");
static az_span COMMAND_NAME_DISPLAY_TEXT = AZ_SPAN_FROM_STR("DisplayText");
static az_span COMMAND_NAME_SET_PUMP_DURATION = AZ_SPAN_FROM_STR("SetPumpDuration");
static az_span COMMAND_NAME_TRIGGER_PUMP = AZ_SPAN_FROM_STR("TriggerPump");
static az_span COMMAND_NAME_SET_PUMP_SCHEDULE = AZ_SPAN_FROM_STR("SetPumpSchedule");
static az_span COMMAND_NAME_SET_PUMP_SCHEDULE_INTERVAL = AZ_SPAN_FROM_STR("SetPumpScheduleInterval");
static az_span COMMAND_NAME_SET_ADDITIONAL_PUMP_SCHEDULE = AZ_SPAN_FROM_STR("SetAdditionalPumpSchedule");
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

bool sendTelemeteryNow = false;

// --- Pump --- //
#define PUMP1_PIN 18                                    // GPIO pin connected to the pump relay/MOSFET
#define DEFAULT_PUMP_RUN_DURATION_SECS 30               // Default pump run duration per watering cycle (seconds)
static unsigned long pump_run_duration_ms = DEFAULT_PUMP_RUN_DURATION_SECS * 1000UL;
static bool water_pot1 = false;
static unsigned long pump1_ran_at = 0;
static volatile bool triggerPumpRequested = false;

// --- Pump Schedule --- //
static uint8_t schedule_hour = 6;       // 0xFF = disabled, 0-23 = enabled
static uint8_t schedule_minute = 0;        // 0-59
static int last_schedule_trigger_day = -1; // Day of month when schedule last triggered (prevents re-trigger)
static uint8_t schedule_interval_days = 1; // 1-30, run every N days (1 = daily)
static unsigned long last_schedule_epoch = 0; // Epoch time of last schedule trigger
// --- Additional Pump Schedule --- //
static uint8_t additional_schedule_hour = 0xFF;      // 0xFF = disabled, 0-23 = enabled
static uint8_t additional_schedule_minute = 0;       // 0-59
static int last_additional_schedule_trigger_day = -1; // Day of month when additional schedule last triggered

/* --- Function Prototypes --- */
static int generate_telemetry_payload(
  uint8_t* payload_buffer, size_t payload_buffer_size, size_t* payload_buffer_length);
static int generate_device_info_payload(
  az_iot_hub_client const* hub_client, uint8_t* payload_buffer,
  size_t payload_buffer_size, size_t* payload_buffer_length);
static int consume_properties_and_generate_response(
  azure_iot_t* azure_iot, az_span properties,
  uint8_t* buffer, size_t buffer_size, size_t* response_length);
static void readPumpSettingsFromEEPROM();
static void readPumpScheduleFromEEPROM();

/* --- Public Functions --- */
void azure_pnp_init()
{
  LogInfo("Initializing Azure IoT PnP Client");
  pinMode(PUMP1_PIN, OUTPUT);
  digitalWrite(PUMP1_PIN, LOW);

  setupDisplay();

  EEPROM.begin(EEPROM_SIZE);
  readPumpSettingsFromEEPROM();
  readPumpScheduleFromEEPROM();
}

static void readPumpSettingsFromEEPROM(){
  unsigned long stored_duration_ms;
  EEPROM.get(EEPROM_ADDR_PUMP_DURATION, stored_duration_ms);
  if(stored_duration_ms >= 1000UL && stored_duration_ms <= 300000UL){
    pump_run_duration_ms = stored_duration_ms;
  } else {
    pump_run_duration_ms = DEFAULT_PUMP_RUN_DURATION_SECS * 1000UL;
    EEPROM.put(EEPROM_ADDR_PUMP_DURATION, pump_run_duration_ms);
    EEPROM.commit();
  }
  LogInfo("Pump run duration [From EEPROM]: %lu ms (%lu secs)", pump_run_duration_ms, pump_run_duration_ms / 1000);
}

static void readPumpScheduleFromEEPROM(){
  uint8_t stored_hour = EEPROM.read(EEPROM_ADDR_SCHEDULE_HOUR);
  uint8_t stored_minute = EEPROM.read(EEPROM_ADDR_SCHEDULE_MINUTE);
  if(stored_hour <= 23 && stored_minute <= 59){
    schedule_hour = stored_hour;
    schedule_minute = stored_minute;
    LogInfo("Pump schedule [From EEPROM]: %02d:%02d", schedule_hour, schedule_minute);
  } else {
    schedule_hour = 0xFF; // disabled
    schedule_minute = 0;
    LogInfo("Pump schedule [From EEPROM]: disabled");
  }
  // Restore last trigger day to prevent re-triggering after reboot
  uint8_t stored_day = EEPROM.read(EEPROM_ADDR_LAST_SCHEDULE_DAY);
  if (stored_day >= 1 && stored_day <= 31) {
    last_schedule_trigger_day = stored_day;
    LogInfo("Last schedule trigger day [From EEPROM]: %d", last_schedule_trigger_day);
  }
  // Restore schedule interval
  uint8_t stored_interval = EEPROM.read(EEPROM_ADDR_SCHEDULE_INTERVAL);
  if (stored_interval >= 1 && stored_interval <= 30) {
    schedule_interval_days = stored_interval;
  } else {
    schedule_interval_days = 1;
    EEPROM.write(EEPROM_ADDR_SCHEDULE_INTERVAL, schedule_interval_days);
    EEPROM.commit();
  }
  LogInfo("Pump schedule interval [From EEPROM]: every %d day(s)", schedule_interval_days);
  // Restore last schedule epoch
  unsigned long stored_epoch;
  EEPROM.get(EEPROM_ADDR_LAST_SCHEDULE_EPOCH, stored_epoch);
  if (stored_epoch > 1510592825UL) { // sanity: after Nov 2017
    last_schedule_epoch = stored_epoch;
    LogInfo("Last schedule epoch [From EEPROM]: %lu", last_schedule_epoch);
  }
  // Restore additional schedule
  uint8_t stored_hour2 = EEPROM.read(EEPROM_ADDR_ADDITIONAL_SCHEDULE_HOUR);
  uint8_t stored_minute2 = EEPROM.read(EEPROM_ADDR_ADDITIONAL_SCHEDULE_MINUTE);
  if (stored_hour2 <= 23 && stored_minute2 <= 59) {
    additional_schedule_hour = stored_hour2;
    additional_schedule_minute = stored_minute2;
    LogInfo("Additional pump schedule [From EEPROM]: %02d:%02d", additional_schedule_hour, additional_schedule_minute);
  } else {
    additional_schedule_hour = 0xFF; // disabled
    additional_schedule_minute = 0;
    LogInfo("Additional pump schedule [From EEPROM]: disabled");
  }
  uint8_t stored_day2 = EEPROM.read(EEPROM_ADDR_LAST_ADDITIONAL_SCHEDULE_DAY);
  if (stored_day2 >= 1 && stored_day2 <= 31) {
    last_additional_schedule_trigger_day = stored_day2;
    LogInfo("Last additional schedule trigger day [From EEPROM]: %d", last_additional_schedule_trigger_day);
  }
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

static void turn_pump_on(bool value){
  if(value){
    digitalWrite(PUMP1_PIN, HIGH);
    LogInfo("Turning Pump ON.");
  }
  else{
    digitalWrite(PUMP1_PIN, LOW);
    LogInfo("Turning Pump OFF.");
  }
}

static bool is_pump_on(){
  return digitalRead(PUMP1_PIN);
}

static void writeLastRunTime(unsigned long lastRunTime) {
    EEPROM.put(EEPROM_ADDR_LAST_WATERED_TIME, lastRunTime);
    EEPROM.commit();
}

static unsigned long readLastRunTime() {
    unsigned long lastRunTime;
    EEPROM.get(EEPROM_ADDR_LAST_WATERED_TIME, lastRunTime);
    return lastRunTime;
}

void setSendTelemetryNow(){
  sendTelemeteryNow = true;
}

static void checkPumpSchedule() {
  if (schedule_hour == 0xFF && additional_schedule_hour == 0xFF) return; // Both schedules disabled

  time_t now = time(NULL);
  struct tm* timeinfo = localtime(&now);
  if (timeinfo == NULL) return;

  int today = timeinfo->tm_mday;
  int current_minutes = timeinfo->tm_hour * 60 + timeinfo->tm_min;

  // Determine if today is a valid watering day (interval check)
  // Skip interval check if any schedule already triggered today (we're on a valid day)
  bool any_triggered_today = (today == last_schedule_trigger_day) || (today == last_additional_schedule_trigger_day);
  if (!any_triggered_today && last_schedule_epoch > 0 && schedule_interval_days > 1) {
    unsigned long secs_since_last = (unsigned long)now - last_schedule_epoch;
    unsigned long required_secs = (unsigned long)(schedule_interval_days - 1) * 86400UL;
    if (secs_since_last < required_secs) return; // Not enough days elapsed
  }

  // Check schedule 1
  if (schedule_hour != 0xFF && today != last_schedule_trigger_day) {
    int schedule_minutes = schedule_hour * 60 + schedule_minute;
    if (current_minutes >= schedule_minutes) {
      last_schedule_trigger_day = today;
      last_schedule_epoch = (unsigned long)now;
      EEPROM.write(EEPROM_ADDR_LAST_SCHEDULE_DAY, (uint8_t)today);
      EEPROM.put(EEPROM_ADDR_LAST_SCHEDULE_EPOCH, last_schedule_epoch);
      EEPROM.commit();
      triggerPumpRequested = true;
      LogInfo("Scheduled pump trigger (schedule 1) at %02d:%02d (current %02d:%02d), interval: every %d day(s).",
        schedule_hour, schedule_minute, timeinfo->tm_hour, timeinfo->tm_min, schedule_interval_days);
    }
  }

  // Check additional schedule
  if (additional_schedule_hour != 0xFF && today != last_additional_schedule_trigger_day) {
    int additional_schedule_minutes = additional_schedule_hour * 60 + additional_schedule_minute;
    if (current_minutes >= additional_schedule_minutes) {
      last_additional_schedule_trigger_day = today;
      // Only update epoch if schedule 1 hasn't already updated it today
      if (today != last_schedule_trigger_day) {
        last_schedule_epoch = (unsigned long)now;
        EEPROM.put(EEPROM_ADDR_LAST_SCHEDULE_EPOCH, last_schedule_epoch);
      }
      EEPROM.write(EEPROM_ADDR_LAST_ADDITIONAL_SCHEDULE_DAY, (uint8_t)today);
      EEPROM.commit();
      triggerPumpRequested = true;
      LogInfo("Scheduled pump trigger (additional) at %02d:%02d (current %02d:%02d), interval: every %d day(s).",
        additional_schedule_hour, additional_schedule_minute, timeinfo->tm_hour, timeinfo->tm_min, schedule_interval_days);
    }
  }
}

void water_pump_handler(){
  // Check if scheduled time has been reached
  checkPumpSchedule();

  // Handle deferred TriggerPump command from IoT Central
  if (triggerPumpRequested) {
    triggerPumpRequested = false;
    water_pot1 = true;
    pump1_ran_at = millis();
    writeLastRunTime((unsigned long)time(NULL));
    delay(10);
    turn_pump_on(true);
    setSendTelemetryNow();
    LogInfo("Pump started via TriggerPump command.");
    return;
  }

  // Handle pump stop after duration
  if(water_pot1){
    if(millis() > (pump1_ran_at + pump_run_duration_ms) && is_pump_on()){
      turn_pump_on(false);
      water_pot1 = false;
      setSendTelemetryNow();
      LogInfo("Pump turned OFF. [Duration complete]");
    }
  }
  else if(is_pump_on()){
    turn_pump_on(false);
    setSendTelemetryNow();
    LogInfo("Pump turned OFF.");
  }
}

/* --- Telemetry and Device Info --- */

int azure_pnp_send_telemetry(azure_iot_t* azure_iot)
{
  _az_PRECONDITION_NOT_NULL(azure_iot);

  time_t now = time(NULL);

  if (now == INDEFINITE_TIME)
  {
    LogError("Failed getting current time for controlling telemetry.");
    return RESULT_ERROR;
  }

  // Run pump handler every cycle
  water_pump_handler();

  if (sendTelemeteryNow || (last_telemetry_send_time != INDEFINITE_TIME && difftime(now, last_telemetry_send_time) >= telemetry_frequency_in_seconds))
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

    LogInfo("Telemetry sent.");
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
  EXIT_IF_TRUE(result != RESULT_OK, RESULT_ERROR, "Failed generating device info payload.");

  result = azure_iot_send_properties_update(azure_iot, request_id, az_span_create(data_buffer, length));
  EXIT_IF_TRUE(result != RESULT_OK, RESULT_ERROR, "Failed sending reported properties update.");

  return RESULT_OK;
}

/* --- Command Handling --- */

int azure_pnp_handle_command_request(azure_iot_t* azure_iot, command_request_t command)
{
  _az_PRECONDITION_NOT_NULL(azure_iot);

  uint16_t response_code;

  if (az_span_is_content_equal(command.command_name, COMMAND_NAME_TOGGLE_LED_1))
  {
    led1_on = !led1_on;
    LogInfo("LED 1 state: %s", (led1_on ? "ON" : "OFF"));
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
    LogInfo("OLED display: %.*s", az_span_size(command.payload) - 2, az_span_ptr(command.payload) + 1);
    response_code = COMMAND_RESPONSE_CODE_ACCEPTED;
  }
  else if (az_span_is_content_equal(command.command_name, COMMAND_NAME_SET_PUMP_DURATION))
  {
    char* parse_string = (char*)az_span_ptr(command.payload);
    parse_string[az_span_size(command.payload)] = '\0';
    int new_duration = atoi(parse_string);
    LogInfo("SetPumpDuration: %d seconds", new_duration);
    if (new_duration < 1 || new_duration > 300) {
      LogError("Invalid pump duration: %d (must be 1-300)", new_duration);
      response_code = COMMAND_RESPONSE_CODE_REJECTED;
      return azure_iot_send_command_response(azure_iot, command.request_id, response_code, AZ_SPAN_LITERAL_FROM_STR("Invalid duration (1-300 secs)"));
    }
    pump_run_duration_ms = (unsigned long)new_duration * 1000UL;
    EEPROM.put(EEPROM_ADDR_PUMP_DURATION, pump_run_duration_ms);
    EEPROM.commit();
    delay(10);
    LogInfo("Pump duration updated to %d secs", new_duration);
    response_code = COMMAND_RESPONSE_CODE_ACCEPTED;
    setSendTelemetryNow();
  }
  else if (az_span_is_content_equal(command.command_name, COMMAND_NAME_TRIGGER_PUMP))
  {
    triggerPumpRequested = true;
    LogInfo("Pump trigger requested.");
    response_code = COMMAND_RESPONSE_CODE_ACCEPTED;
  }
  else if (az_span_is_content_equal(command.command_name, COMMAND_NAME_SET_PUMP_SCHEDULE))
  {
    // SetPumpSchedule: payload is HHMM format (e.g., 630 = 6:30 AM, 1830 = 6:30 PM, -1 = disable)
    char* parse_string = (char*)az_span_ptr(command.payload);
    parse_string[az_span_size(command.payload)] = '\0';
    int value = atoi(parse_string);
    if (value < 0) {
      // Disable schedule
      schedule_hour = 0xFF;
      schedule_minute = 0;
      EEPROM.write(EEPROM_ADDR_SCHEDULE_HOUR, 0xFF);
      EEPROM.write(EEPROM_ADDR_SCHEDULE_MINUTE, 0);
      EEPROM.commit();
      delay(10);
      LogInfo("Pump schedule disabled.");
      response_code = COMMAND_RESPONSE_CODE_ACCEPTED;
    } else {
      int hour = value / 100;
      int minute = value % 100;
      if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        LogError("Invalid schedule: %d (use HHMM, e.g. 630 for 6:30)", value);
        response_code = COMMAND_RESPONSE_CODE_REJECTED;
        return azure_iot_send_command_response(azure_iot, command.request_id, response_code, AZ_SPAN_LITERAL_FROM_STR("Invalid time (HHMM format)"));
      }
      schedule_hour = (uint8_t)hour;
      schedule_minute = (uint8_t)minute;
      EEPROM.write(EEPROM_ADDR_SCHEDULE_HOUR, schedule_hour);
      EEPROM.write(EEPROM_ADDR_SCHEDULE_MINUTE, schedule_minute);
      EEPROM.commit();
      delay(10);
      LogInfo("Pump schedule set to %02d:%02d", schedule_hour, schedule_minute);
      response_code = COMMAND_RESPONSE_CODE_ACCEPTED;
    }
    setSendTelemetryNow();
  }
  else if (az_span_is_content_equal(command.command_name, COMMAND_NAME_SET_PUMP_SCHEDULE_INTERVAL))
  {
    char* parse_string = (char*)az_span_ptr(command.payload);
    parse_string[az_span_size(command.payload)] = '\0';
    int interval = atoi(parse_string);
    if (interval < 1 || interval > 30) {
      LogError("Invalid schedule interval: %d (must be 1-30 days)", interval);
      response_code = COMMAND_RESPONSE_CODE_REJECTED;
      return azure_iot_send_command_response(azure_iot, command.request_id, response_code, AZ_SPAN_LITERAL_FROM_STR("Invalid interval (1-30 days)"));
    }
    schedule_interval_days = (uint8_t)interval;
    EEPROM.write(EEPROM_ADDR_SCHEDULE_INTERVAL, schedule_interval_days);
    EEPROM.commit();
    delay(10);
    LogInfo("Pump schedule interval set to every %d day(s)", schedule_interval_days);
    response_code = COMMAND_RESPONSE_CODE_ACCEPTED;
    setSendTelemetryNow();
  }
  else if (az_span_is_content_equal(command.command_name, COMMAND_NAME_SET_ADDITIONAL_PUMP_SCHEDULE))
  {
    // SetAdditionalPumpSchedule: payload is HHMM format (e.g., 1830 = 6:30 PM, -1 = disable)
    char* parse_string = (char*)az_span_ptr(command.payload);
    parse_string[az_span_size(command.payload)] = '\0';
    int value = atoi(parse_string);
    if (value < 0) {
      // Disable additional schedule
      additional_schedule_hour = 0xFF;
      additional_schedule_minute = 0;
      EEPROM.write(EEPROM_ADDR_ADDITIONAL_SCHEDULE_HOUR, 0xFF);
      EEPROM.write(EEPROM_ADDR_ADDITIONAL_SCHEDULE_MINUTE, 0);
      EEPROM.commit();
      delay(10);
      LogInfo("Additional pump schedule disabled.");
      response_code = COMMAND_RESPONSE_CODE_ACCEPTED;
    } else {
      int hour = value / 100;
      int minute = value % 100;
      if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        LogError("Invalid additional schedule: %d (use HHMM, e.g. 1830 for 18:30)", value);
        response_code = COMMAND_RESPONSE_CODE_REJECTED;
        return azure_iot_send_command_response(azure_iot, command.request_id, response_code, AZ_SPAN_LITERAL_FROM_STR("Invalid time (HHMM format)"));
      }
      additional_schedule_hour = (uint8_t)hour;
      additional_schedule_minute = (uint8_t)minute;
      EEPROM.write(EEPROM_ADDR_ADDITIONAL_SCHEDULE_HOUR, additional_schedule_hour);
      EEPROM.write(EEPROM_ADDR_ADDITIONAL_SCHEDULE_MINUTE, additional_schedule_minute);
      EEPROM.commit();
      delay(10);
      LogInfo("Additional pump schedule set to %02d:%02d", additional_schedule_hour, additional_schedule_minute);
      response_code = COMMAND_RESPONSE_CODE_ACCEPTED;
    }
    setSendTelemetryNow();
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

/* --- Payload generation functions --- */

static int generate_telemetry_payload(uint8_t* payload_buffer, size_t payload_buffer_size, size_t* payload_buffer_length)
{
  az_json_writer jw;
  az_result rc;
  az_span payload_buffer_span = az_span_create(payload_buffer, payload_buffer_size);

  rc = az_json_writer_init(&jw, payload_buffer_span, NULL);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed initializing json writer for telemetry.");

  rc = az_json_writer_append_begin_object(&jw);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed setting telemetry json root.");

  // Pump status
  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR("pump1"));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding pump1 property name.");
  rc = az_json_writer_append_int32(&jw, is_pump_on() ? 1 : 0);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding pump1 property value.");

  // Pump duration (seconds)
  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR("pumpRunDurationSecs"));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding pumpRunDurationSecs.");
  rc = az_json_writer_append_int32(&jw, pump_run_duration_ms / 1000);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding pumpRunDurationSecs value.");

  // Pump schedule
  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR("pumpSchedule"));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding pumpSchedule.");
  if (schedule_hour == 0xFF) {
    rc = az_json_writer_append_string(&jw, AZ_SPAN_FROM_STR("Disabled"));
  } else {
    char schedule_buf[6];
    snprintf(schedule_buf, sizeof(schedule_buf), "%02d:%02d", schedule_hour, schedule_minute);
    rc = az_json_writer_append_string(&jw, az_span_create((uint8_t*)schedule_buf, strlen(schedule_buf)));
  }
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding pumpSchedule value.");

  // Additional pump schedule
  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR("additionalPumpSchedule"));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding additionalPumpSchedule.");
  if (additional_schedule_hour == 0xFF) {
    rc = az_json_writer_append_string(&jw, AZ_SPAN_FROM_STR("Disabled"));
  } else {
    char additional_schedule_buf[6];
    snprintf(additional_schedule_buf, sizeof(additional_schedule_buf), "%02d:%02d", additional_schedule_hour, additional_schedule_minute);
    rc = az_json_writer_append_string(&jw, az_span_create((uint8_t*)additional_schedule_buf, strlen(additional_schedule_buf)));
  }
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding additionalPumpSchedule value.");

  // Pump schedule interval (days)
  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR("scheduleIntervalDays"));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding scheduleIntervalDays.");
  rc = az_json_writer_append_int32(&jw, schedule_interval_days);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding scheduleIntervalDays value.");

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

static int generate_device_info_payload(az_iot_hub_client const* hub_client, uint8_t* payload_buffer, size_t payload_buffer_size, size_t* payload_buffer_length)
{
  az_json_writer jw;
  az_result rc;
  az_span payload_buffer_span = az_span_create(payload_buffer, payload_buffer_size);

  rc = az_json_writer_init(&jw, payload_buffer_span, NULL);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed initializing json writer for device info.");

  rc = az_json_writer_append_begin_object(&jw);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed setting device info json root.");
  
  rc = az_iot_hub_client_properties_writer_begin_component(
    hub_client, &jw, AZ_SPAN_FROM_STR(SAMPLE_DEVICE_INFORMATION_NAME));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed writing component name.");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_MANUFACTURER_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding manufacturer.");
  rc = az_json_writer_append_string(&jw, AZ_SPAN_FROM_STR(SAMPLE_MANUFACTURER_PROPERTY_VALUE));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding manufacturer value.");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_MODEL_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding model.");
  rc = az_json_writer_append_string(&jw, AZ_SPAN_FROM_STR(SAMPLE_MODEL_PROPERTY_VALUE));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding model value.");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_SOFTWARE_VERSION_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding swVersion.");
  rc = az_json_writer_append_string(&jw, AZ_SPAN_FROM_STR(SAMPLE_VERSION_PROPERTY_VALUE));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding swVersion value.");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_OS_NAME_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding osName.");
  rc = az_json_writer_append_string(&jw, AZ_SPAN_FROM_STR(SAMPLE_OS_NAME_PROPERTY_VALUE));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding osName value.");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_PROCESSOR_ARCHITECTURE_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding processorArchitecture.");
  rc = az_json_writer_append_string(&jw, AZ_SPAN_FROM_STR(SAMPLE_ARCHITECTURE_PROPERTY_VALUE));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding processorArchitecture value.");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_PROCESSOR_MANUFACTURER_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding processorManufacturer.");
  rc = az_json_writer_append_string(&jw, AZ_SPAN_FROM_STR(SAMPLE_PROCESSOR_MANUFACTURER_PROPERTY_VALUE));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding processorManufacturer value.");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_TOTAL_STORAGE_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding totalStorage.");
  rc = az_json_writer_append_double(&jw, SAMPLE_TOTAL_STORAGE_PROPERTY_VALUE, DOUBLE_DECIMAL_PLACE_DIGITS);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding totalStorage value.");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_TOTAL_MEMORY_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding totalMemory.");
  rc = az_json_writer_append_double(&jw, SAMPLE_TOTAL_MEMORY_PROPERTY_VALUE, DOUBLE_DECIMAL_PLACE_DIGITS);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding totalMemory value.");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR("telemeteryFrequencyMins"));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding telemeteryFrequency.");
  rc = az_json_writer_append_int32(&jw, TELEMETRY_FREQUENCY_IN_SECONDS / 60);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding telemeteryFrequency value.");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR("pumpRunDurationSecs"));
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding pumpRunDurationSecs.");
  rc = az_json_writer_append_int32(&jw, pump_run_duration_ms / 1000);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed adding pumpRunDurationSecs value.");

  rc = az_iot_hub_client_properties_writer_end_component(hub_client, &jw);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed closing component object.");

  rc = az_json_writer_append_end_object(&jw);
  EXIT_IF_AZ_FAILED(rc, RESULT_ERROR, "Failed closing device info json payload.");

  payload_buffer_span = az_json_writer_get_bytes_used_in_destination(&jw);

  if ((payload_buffer_size - az_span_size(payload_buffer_span)) < 1)
  {
    LogError("Insufficient space for device info payload null terminator.");
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

  azrc = az_iot_hub_client_properties_writer_begin_response_status(
    &azure_iot->iot_hub_client,
    &jw,
    AZ_SPAN_FROM_STR(WRITABLE_PROPERTY_TELEMETRY_FREQ_SECS),
    (int32_t)AZ_IOT_STATUS_OK,
    version,
    AZ_SPAN_FROM_STR(WRITABLE_PROPERTY_RESPONSE_SUCCESS));
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed appending status to properties update response.");

  azrc = az_json_writer_append_int32(&jw, frequency);
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed appending frequency value.");

  azrc = az_iot_hub_client_properties_writer_end_response_status(&azure_iot->iot_hub_client, &jw);
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed closing status section.");

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
