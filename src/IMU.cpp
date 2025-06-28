// IMU.cpp
#define RX_PIN 16
#define TX_PIN 1
#include <Arduino.h>
#include <HardwareSerial.h>

#define BAUDRATE 115200

void setUart() {
  Serial.begin(BAUDRATE, SERIAL_8N1, RX_PIN, TX_PIN);
  while (!Serial) {
    delay(10); // Wait for serial port to connect
  }
  Serial.println("UART initialized");
}

void getIMUData() {
  if (Serial.available()) {
    String data = Serial.readStringUntil('\n');
    Serial.print("Received IMU data: ");
    Serial.println(data);
  } else {
    Serial.println("No IMU data available");
  }
}