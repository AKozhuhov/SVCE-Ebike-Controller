#include <Arduino.h>
#include <HardwareSerial.h>  // For Serial (USB and VESC)
#include <MPU6050_tockn.h>   // For MPU6050 (I2C)
#include <SD.h>              // For SD Card
#include <VescUart.h>        // For VESC
#include <Wire.h>            // For MPU6050 (I2C)
#include "config.h"          // Constants
#include "helpers.h"         // Helper Objects

// Global objects
MPU6050 mpu6050(Wire);  // 6DOF Accelerometer/Gyro
VescUart vesc;          // VESC
String logFile = "";    // Path to currently in-use log file
State state;

void setup() {
  // Init USB Serial
  Serial.begin(USB_BAUD);

  {  // Init I2C and MPU6050
    Wire.begin();
    mpu6050.begin();
    mpu6050.calcGyroOffsets(DEBUG);
  }

  // Init throttle sense pin
  pinMode(THROTTLE_PIN, INPUT);

  {  // Init SD file IO
    logMsg("\nSD card init...");
    if (!SD.begin(SD_CS_PIN)) {
      logErr("SD Card failed, or not present");
      while (1) {
      }
    }
    logMsg("SD card initialized");
  }

  {  // Find out what the latest log file is, and use the next one
    int num = 0;
    while (logFile == "") {
      if (SD.exists("log" + (String)num + ".csv"))
        num++;
      else
        logFile = "log" + (String)num + ".csv";
    }
    logMsg("Using log file: " + (String)logFile);
  }

  {  // Send header line
    File log = SD.open(logFile, FILE_WRITE);
    if (log) {
      log.println(
          "time, accX, accY, accZ, gyroX, gyroY, gyroZ, rpm, motorPower, "
          "inputPower, motorVoltage, inputVoltage, motorCurrent, inputCurrent, "
          "enabled, targetRPM, targetW, throttleVoltage");
      log.close();
      logMsg("Header Line Written");
    } else
      logErr("Error opening " + (String)logFile);
  }

  {  // Init VESC
    VESC_USART.begin(VESC_BAUD);
    while (!VESC_USART) {
    }
    vesc.setSerialPort(&VESC_USART);
  }
}

void loop() {
  state.time = millis();

  {  // Populate MPU6050 state fields
    mpu6050.update();
    state.accX = mpu6050.getAccX() * 9.80;
    state.accY = mpu6050.getAccY() * 9.80;
    state.accZ = mpu6050.getAccZ() * 9.80;

    state.gyroX = mpu6050.getGyroX() * (PI / 180);
    state.gyroY = mpu6050.getGyroY() * (PI / 180);
    state.gyroZ = mpu6050.getGyroZ() * (PI / 180);
  }

  {  // Populate VESC state fields
    if (vesc.getVescValues()) {
      state.rpm = vesc.data.rpm;
      state.motorVoltage =
          vesc.data.inpVoltage;  // It's the same. BLDC ESCs drive the same
                                 // voltage, just changing pulse times
      state.inputVoltage = vesc.data.inpVoltage;
      state.motorCurrent = vesc.data.avgMotorCurrent;
      state.inputCurrent = vesc.data.avgInputCurrent;
      state.motorPower = state.motorVoltage * state.motorCurrent;
      state.inputPower = state.inputVoltage * state.inputCurrent;
    } else {  // Error fetching new values from VESC, log the error and record
              // null values
      logErr("Couldn't fetch new VESC state values");
      state.rpm = 0.0;
      state.motorVoltage = 0.0;
      state.inputVoltage = 0.0;
      state.motorCurrent = 0.0;
      state.inputCurrent = 0.0;
      state.motorPower = 0.0;
      state.inputPower = 0.0;
    }
  }

  // Read throttle voltage and decide whether or not to enable
  state.throttleVoltage = analogRead(THROTTLE_PIN) * THROTTLE_CONVERSION;
  state.enabled = state.throttleVoltage > THROTTLE_CUTOFF ? true : false;

  {  // Deal with sending goals to ESC
    if (state.enabled) {
      // Compute the desired throttle setting
      state.targetRPM = mapFloat(state.throttleVoltage, THROTTLE_LOW,
                                 THROTTLE_HIGH, 0, MAX_RPM) *
                        25;
      state.targetW = state.targetRPM * 2 * PI / 60;
    } else {  // Disabled, record null goals
      state.targetRPM = 0.0;
      state.targetW = 0.0;
    }

    // TODO(Neil): Do I need to send current? brakeCurrent?
    vesc.setRPM(state.targetRPM);
  }

  {  // Write state info to SD file, log to serial
    File log = SD.open(logFile, FILE_WRITE);
    if (log) {
      String msg = state.generateLogLine();
      logMsg(msg);
      log.println(msg);
      log.close();
    } else
      logErr("Error opening " + (String)logFile);
  }

  // Generate the loop time delay
  delay(1000 / LOOP_SPEED);
}
