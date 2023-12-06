#include "Particle.h"
#include <Wire.h>
#include "Module/MAX30105.h"
#include "Module/spo2_algorithm.h"
#include "qrcode.h"
#include "Module/Adafruit_GFX.h"
#include "Module/Adafruit_SSD1306.h"

SYSTEM_THREAD(ENABLED);

MAX30105 particleSensor;

#define MAX_BRIGHTNESS 255

#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
uint16_t irBuffer[100];  // infrared LED sensor data
uint16_t redBuffer[100]; // red LED sensor data
#else
uint32_t irBuffer[100];  // infrared LED sensor data
uint32_t redBuffer[100]; // red LED sensor data
#endif

int32_t bufferLength;  // data length
int32_t spo2;          // SPO2 value
int8_t validSPO2;      // indicator to show if the SPO2 calculation is valid
int32_t heartRate;     // heart rate value
int8_t validHeartRate; // indicator to show if the heart rate calculation is valid

byte pulseLED = 11; // Must be on PWM pin
byte readLED = 13;  // Blinks with each data read

// LED
int red_light_pin = D2;
int green_light_pin = D3;
int blue_light_pin = D4;
int int_time = 1000;

// Connect Status
bool ConnStatus = 0;
unsigned long StartTime;

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
QRCode qrcode;
const char *MESSAGE_CONFIGURE_WIFI[4] = {"Scan QR", "to login", "HR System", ""};

void setup()
{
  // LED Pin Defined
  pinMode(red_light_pin, OUTPUT);
  pinMode(green_light_pin, OUTPUT);
  pinMode(blue_light_pin, OUTPUT);
  RGB_color(256, 0, 0);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  { // Address 0x3D for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;
  }
  delay(1000);

  // const char *lines[4] = { "Scan to", "Join", "Lumifera", "WiFi" };
  drawQrCode("http://0.0.0.0:3000", MESSAGE_CONFIGURE_WIFI);
  delay(3000);
  OLED_Startup_Display(3000);

  Serial.begin(115200); // initialize serial communication at 115200 bits per second:

  pinMode(pulseLED, OUTPUT);
  pinMode(readLED, OUTPUT);

  // Initialize sensor
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) // Use default I2C port, 400kHz speed
  {
    Serial.println(F("MAX30105 was not found. Please check wiring/power."));
    while (1)
      ;
  }

  /*Serial.println(F("Attach sensor to finger with rubber band. Press any key to start conversion"));
  while (Serial.available() == 0) ; //wait until user presses a key
  Serial.read();
  */
  byte ledBrightness = 60; // Options: 0=Off to 255=50mA
  byte sampleAverage = 4;  // Options: 1, 2, 4, 8, 16, 32
  byte ledMode = 2;        // Options: 1 = Red only, 2 = Red + IR, 3 = Red + IR + Green
  byte sampleRate = 100;   // Options: 50, 100, 200, 400, 800, 1000, 1600, 3200
  int pulseWidth = 411;    // Options: 69, 118, 215, 411
  int adcRange = 4096;     // Options: 2048, 4096, 8192, 16384

  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange); // Configure sensor with these settings

  // Particle Cloud Setup

  Particle.variable("hr", (int)heartRate);
  Particle.variable("spo2", (int)spo2);
}

void loop()
{

  bufferLength = 100;   // buffer length of 100 stores 4 seconds of samples running at 25sps
  RGB_color(20, 20, 0); // Yellow
  // read the first 100 samples, and determine the signal range
  for (byte i = 0; i < bufferLength; i++)
  {
    while (particleSensor.available() == false) // do we have new data?
      particleSensor.check();                   // Check the sensor for new data

    redBuffer[i] = particleSensor.getRed();
    irBuffer[i] = particleSensor.getIR();
    particleSensor.nextSample(); // We're finished with this sample so move to next sample

    OLED_Preparing(i); // Show preparing status on OLED
  }

  // calculate heart rate and SpO2 after first 100 samples (first 4 seconds of samples)
  maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);

  String Cloud_List = "";
  bool success;
  success = Particle.publish("hr", Cloud_HR, PRIVATE);

  // Continuously taking samples from MAX30102.  Heart rate and SpO2 are calculated every 1 second
  while (1)
  {
    RGB_color(0, 20, 0);
    // dumping the first 25 sets of samples in the memory and shift the last 75 sets of samples to the top
    for (byte i = 25; i < 100; i++)
    {
      redBuffer[i - 25] = redBuffer[i];
      irBuffer[i - 25] = irBuffer[i];
    }

    // take 25 sets of samples before calculating the heart rate.
    for (byte i = 75; i < 100; i++)
    {
      while (particleSensor.available() == false) // do we have new data?
        particleSensor.check();                   // Check the sensor for new data

      digitalWrite(readLED, !digitalRead(readLED)); // Blink onboard LED with every data read

      redBuffer[i] = particleSensor.getRed();
      irBuffer[i] = particleSensor.getIR();
      particleSensor.nextSample(); // We're finished with this sample so move to next sample
    }
    String Cloud_HR = String(heartRate);
    String Cloud_SPO2 = String(spo2);

    String Send_Data = "{\"HR\":" + Cloud_HR + ",\"SPO2\":" + Cloud_SPO2 + "}";
    Particle.publish("hr", Send_Data, PRIVATE);
    Particle.publish("spo2", Cloud_SPO2, PRIVATE);

    /* State Machine for disconnecting
    unsigned long ms2Sec = 1000000; // Micro Second to Second
    unsigned long periodic_sec = 1800; //30min
    unsigned long periodic_ms = periodic_sec*ms2Sec;
    unsigned long time_count;
    bool success;

    while(1){
      String Cloud_List = "";
      success = Particle.publish("hr", Cloud_HR, PRIVATE); //Send Data
      if (!success){
        ConnStatus = 0; //Change connect status
        StartTime = Micros();
        if (!ConnectStatus)
        {
          while (time_count>periodic_ms):{
            heartRate = getHeartRate();
            Cloud_List.concat(String(heartRate)); // Accumulated Data during off line (Store Data in device)
            time_count = Micros() - StartTime;
          }
          String Send_Data = "{\"HR\": [" + Cloud_List + "]}"; // Send Array Data
          success = Particle.publish("hr", Cloud_HR, PRIVATE);
        }
      }
      else
      {
        Serial.println("Send Data Successful!");
      }
    }
    */

    // Serial_Print_Value(redBuffer[i], irBuffer[i], heartRate, validHeartRate, spo2, validSPO2);
    OLED_Show_Value(heartRate, spo2);
    // After gathering 25 new samples recalculate HR and SP02
    maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);
    RGB_color(0, 0, 20);
    // delay(60000);
    Serial.println(millis());
  }

  Serial.print("\n");
}

void getHeartRate(){}

// Draw QR code for login
void drawQrCode(const char *qrStr, const char *lines[])
{
  uint8_t qrcodeData[qrcode_getBufferSize(3)];
  qrcode_initText(&qrcode, qrcodeData, 3, ECC_LOW, qrStr);

  // Text starting point
  int cursor_start_y = 7;
  int cursor_start_x = 4;
  int font_height = 12;

  // QR Code Starting Point
  int offset_x = 62;
  int offset_y = 2;

  display.clearDisplay();
  for (int y = 0; y < qrcode.size; y++)
  {
    for (int x = 0; x < qrcode.size; x++)
    {
      int newX = offset_x + (x * 2);
      int newY = offset_y + (y * 2);

      if (qrcode_getModule(&qrcode, x, y))
      {
        display.fillRect(newX, newY, 2, 2, 0);
      }
      else
      {
        display.fillRect(newX, newY, 2, 2, 1);
      }
    }
  }
  display.setTextColor(1, 0);
  for (int i = 0; i < 4; i++)
  {
    display.setCursor(cursor_start_x, cursor_start_y + font_height * i);
    display.println(lines[i]);
  }
  display.display();
}

// OLED Startup Display
void OLED_Startup_Display(int Time_Delay)
{
  display.clearDisplay();

  display.setTextSize(3);
  display.setTextColor(WHITE);
  display.setCursor(0, 5);
  // Display static text
  display.println("ECE513");
  display.setTextSize(1.8);
  display.setTextColor(WHITE);
  display.setCursor(0, 35);
  display.println("Final Proj.");
  display.display();
  delay(Time_Delay);

  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("Members:");
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0, 19);
  display.println("Yu-Zheng");
  display.println("Chun-lin");
  display.println("Alonso");
  display.display();
  delay(Time_Delay);
}

void Serial_Print_Value(int redBuffer, int irBuffer, int heartRate, int validHeartRate, int spo2, int validSPO2)
{
  // send samples and calculation result to terminal program through UART
  Serial.print(F("red="));
  Serial.print(redBuffer, DEC);
  Serial.print(F(", ir="));
  Serial.print(irBuffer, DEC);

  Serial.print(F(", HR="));
  Serial.print(heartRate, DEC);

  Serial.print(F(", HRvalid="));
  Serial.print(validHeartRate, DEC);

  Serial.print(F(", SPO2="));
  Serial.print(spo2, DEC);

  Serial.print(F(", SPO2Valid="));
  Serial.println(validSPO2, DEC);
}

void OLED_Preparing(int progress)
{
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("Preparing:");
  display.setTextSize(3);
  display.setCursor(0, 40);
  display.println(progress);
  display.setTextSize(3);
  display.setCursor(50, 40);
  display.println("%");
  display.display();
}

void OLED_Show_Value(int heartRate, int spo2)
{
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("HR:  SPO2:");
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0, 21);
  // Display static text
  display.println(heartRate);
  display.setCursor(63, 21);
  display.println(spo2);
  display.display();
}

void RGB_color(int red_light_value, int green_light_value, int blue_light_value)
{
  analogWrite(red_light_pin, red_light_value);
  analogWrite(green_light_pin, green_light_value);
  analogWrite(blue_light_pin, blue_light_value);
}