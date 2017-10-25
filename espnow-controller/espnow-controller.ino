#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <CMMC_SimplePair.h>
#include <CMMC_Utils.h>
#include <CMMC_ESPNow.h>
#include <CMMC_LED.h>
#include <CMMC_BootMode.h>
#include "data_type.h"

#include <SoftwareSerial.h>
#include <Ticker.h>
#define rxPin 14
#define txPin 12

bool serialBusy = false;

SoftwareSerial swSerial(rxPin, txPin, false, 128);
Ticker ticker;

#define LED_PIN 2
#define BUTTON_PIN 0
u8 b = 5;

int mode;

CMMC_SimplePair instance;
CMMC_ESPNow espNow;
CMMC_Utils utils;
CMMC_LED led(LED_PIN, HIGH);
CMMC_BootMode bootMode(&mode, BUTTON_PIN);

uint8_t mmm[6];

void evt_callback(u8 status, u8* sa, const u8* data) {
  if (status == 0) {
    Serial.printf("[CSP_EVENT_SUCCESS] STATUS: %d\r\n", status);
    Serial.printf("WITH KEY: ");
    utils.dump(data, 16);
    Serial.printf("WITH MAC: ");
    utils.dump(sa, 6);
    led.high();
    ESP.reset();
  }
  else {
    Serial.printf("[CSP_EVENT_ERROR] %d: %s\r\n", status, (const char*)data);
  }
}
void setup_hardware() {
  Serial.begin(57600);
  swSerial.begin(57600);
  //  swSerial.enableRx(true);
  Serial.println();
  led.init();
}

void start_config_mode() {
  uint8_t* controller_addr = utils.getESPNowControllerMacAddress();
  utils.printMacAddress(controller_addr);
  instance.begin(MASTER_MODE, evt_callback);
  instance.debug([](const char* s) {
    Serial.println(s);
  });
  instance.set_message(controller_addr, 6);
  instance.start();
}
bool dirty = false;
int counter = 0;

#include <CMMC_RX_Parser.h>
CMMC_RX_Parser parser(&swSerial);

typedef struct __attribute((__packed__)) {
  uint32_t time;
} CMMC_SLEEP_TIME_T;


void setup()
{
  setup_hardware();
  parser.on_command_arrived([](CMMC_SERIAL_PACKET_T * packet) {
    CMMC_SLEEP_TIME_T t;
    if (packet->cmd == CMMC_SLEEP_TIME_CMD) {
      memcpy(&t.time, packet->data, 4);
      //      Serial.printf("time = %lu \r\n", t.time);
      b = t.time;

      if (t.time > 255) {
        b = 254;
      }

    }
  });
  bootMode.init();
  bootMode.check([](int mode) {
    if (mode == BootMode::MODE_CONFIG) {
      led.low();
      start_config_mode();
    }
    else if (mode == BootMode::MODE_RUN) {
      Serial.print("Initializing... Controller..");
      espNow.debug([](const char* s) {
        //        Serial.println(s);
      });
      espNow.init(NOW_MODE_CONTROLLER);


      espNow.on_message_recv([](uint8_t *macaddr, uint8_t *data, uint8_t len) {

        memcpy(mmm, macaddr, 6);
        dirty = true;
        serialBusy = true;
        led.toggle();
        CMMC_SENSOR_T packet;
        CMMC_PACKET_T wrapped;
        memcpy(&packet, data, sizeof(packet));
        wrapped.data = packet;
        wrapped.reserved = 0xff;
        wrapped.version = 2;
        wrapped.sleep = b;
        wrapped.ms = millis();
        wrapped.sum = CMMC::checksum((uint8_t*) &wrapped,
                                     sizeof(wrapped) - sizeof(wrapped.sum));
        Serial.write((byte*)&wrapped, sizeof(wrapped));
        swSerial.write((byte*)&wrapped, sizeof(wrapped));
      });

      espNow.on_message_sent([](uint8_t *macaddr,  uint8_t status) {
        serialBusy = false;
        dirty = false;
      });
    }
    else {
      // unhandled
    }
  }, 2000);
}

void loop()
{
  if (serialBusy == false) {
    parser.process();
    delay(1);
  }
  yield();

  while (dirty) {
    espNow.send(mmm, &b, 1);
    delay(1);
  }
}
