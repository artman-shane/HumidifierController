#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_SSD1306.h>

#define FANCTRL D7    // Digital control for the fan relay
#define DEHUMCTRL D6  // Digital control for the dehumidifier relay

// The pressure (1015) is used to determine altitude. Use the vaule in Hpa of your area.
#define SEA_LEVEL_PRESSURE 1095  

// Instantiate a new instance of a BME280 sensor
Adafruit_BME280 bme;

// Instantiate a new instance of the LCD display (128x64)
Adafruit_SSD1306 lcd(128, 64, &Wire, -1, 400000UL, 100000UL);

float temperature, humidity, pressure, altitude;
const String ver = "2.2";

/*
 ******************************
   Humidity Settings
 ******************************
*/

// Humidity Level to begin dehumidification
// Found a problem when the BME280 was co-located with the ESP8266 NodeMCU.
// The heat was causing +10deg and -15-20% Humidity. Needed to locate outside the case.
const int onHumidity = 52;  
const int offHumidity = 45;


/*
   Short-Cycling Correction

   Used to provide timing delay controls for start and stopping of dehumidification.
   Previously we had conditions that would cause short cycling (On/Off/On/Off) in seconds
   This is not healthy to the compressor.

   Delay 60 seonds before we dedehumidifyOn after a reboot/reset/power on. This will help
   to prevent power outages from creating short cycles.

   Extend the runtime of dehumidification for a minimum of 5 min
   This will be regardless of the humidity level after the trigger to start

   Delay the restart for 10 min regardless of the humidity level.

*/
unsigned long currentMillis = 0;  // Current time in milliseconds
unsigned long previousMillis = 0; // Time in milliseconds when beginning dehumidification

const unsigned long delaySystemRestartTime = 600000;  // 600000 = 10min
const unsigned long extendSystemRunTime = 300000;     // 300000 = 5 min
const unsigned long delayInitialStartTime = 60000;    // 60000 = 1 min


bool dehumidifyOn = false;    // Dehumidify
bool delayStart = false;      // Delay Starting
bool extendRun = false;        // Extend Running

String strMsg = ""; // Used for display writing

unsigned int runCount = 0; // Number of times the system has cycled
unsigned int runDuration = 0; // ms

unsigned float runHours = 0; // hrs
unsigned float runDays = 0; // days
unsigned int dayCount = 0; // system runtime days
unsigned int dayMillis = 0; // system runtime milliseconds counter to determine days.


unsigned int initialPass = 1;

void setup() {
  Serial.begin(9600);
  delay(100);

  //  dht.begin();

  pinMode(FANCTRL, OUTPUT);
  pinMode(DEHUMCTRL, OUTPUT);
  digitalWrite(FANCTRL, LOW);   // Initialize to OFF
  digitalWrite(DEHUMCTRL, LOW); // Initialize to OFF


  // 0x3C was the I2C ID for my unit. I found this with I2C scanner.
  lcd.begin(SSD1306_SWITCHCAPVCC, 0x3c, true, true);


  lcd.clearDisplay();
  lcd.display();
  lcd.setTextColor(WHITE);
  lcd.setRotation(0);
  lcd.setTextWrap(true);


  // Clear the display and reset coordinates back to top left.
  lcd.clearDisplay();
  lcd.setCursor(0, 0);
  lcd.display();

}
void loop() {
  currentMillis = millis();

  if (!bme.begin(0x76, &Wire)) {
    strMsg = "There was a problem with the BME280 sensor. Please check the wiring...";
    Serial.println(strMsg);
    lcd.println(strMsg);
    lcd.display();
    return;
  } else {
    // Reading temperature or humidity takes about 250 milliseconds!
    // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
    float h = bme.readHumidity();
    // Read temperature as Celsius (the default)
    float t = bme.readTemperature();
    // Read temperature as Fahrenheit (isFahrenheit = true)
    float f = bme.readTemperature() * 9 / 5 + 32;
    // Read pressure
    float p = bme.readPressure() * 0.01108;
    // Read Altitude
    float a = bme.readAltitude(SEA_LEVEL_PRESSURE);

    // Check if any reads failed and exit early (to try again).
    if (isnan(h) || isnan(t) || isnan(f)) {
      strMsg = "Failed to read from BME sensor!";
      Serial.println(strMsg);
      lcd.println(strMsg);
      lcd.display();
      return;
    }

    // Compute heat index "feels-like" in Fahrenheit (the default)
    //  float hif = dht.computeHeatIndex(f, h);
    // Compute heat index in Celsius (isFahreheit = false)
    //  float hic = dht.computeHeatIndex(t, h, false);

    Serial.print("Temp: ");
    Serial.print(f);
    Serial.println("F");
    Serial.print("Humidity: ");
    Serial.print(h);
    Serial.println("%");
    Serial.print("Run Count: ");
    Serial.println(runCount);
    Serial.print("Hours running: ");
    Serial.println(runDuration / 3600000);
    Serial.print("Last Reset: ");
    Serial.print(millis() / 3600000);
    Serial.println(" hours");
    Serial.print("P: ");
    Serial.print(p);
    Serial.print("  A: ");
    Serial.print(a);
    Serial.println("\n\n\n");


    lcd.clearDisplay();
    lcd.setCursor(0, 0);
    lcd.display();
    lcd.print("T:");
    lcd.print(f);
    lcd.print("F");
    lcd.print(" H:");
    lcd.print(h);
    lcd.println("%");
    lcd.print("V:");
    lcd.print(ver);
    lcd.print(" Rst: ");
    lcd.print(millis() / 3600000);
    lcd.println("hrs ago");
    lcd.print(" On:");
    lcd.print(onHumidity);
    lcd.print(" Of:");
    lcd.println(offHumidity);
    lcd.print("RC: ");
    lcd.print(runCount);
    lcd.print(" RT: ");
    lcd.println(runDuration / 3600000); // In hours
    lcd.print("P: ");
    lcd.print(p);
    lcd.print("  A: ");
    lcd.print(a);
    /*

       Handle "delayStart" variable here

    */
    if ((currentMillis - previousMillis) >= delaySystemRestartTime || (currentMillis > delayInitialStartTime && runCount == 0)) {
      if (delayStart) {
        Serial.println("Delay Start disabled");
        lcd.println("Delay Start Disabled");
        delayStart = false;
      }
    } else if (!dehumidifyOn && !extendRun) {
      Serial.println("Delay Start - Preventing Short Cycling");
      lcd.println("Delay Start Active");
      delayStart = true;
    }

    /*

       Handle "extendRun" variable here

    */
    if (currentMillis - previousMillis < extendSystemRunTime) {
      // This is to prevent fast start/stop. Note that this will be 4 min for first cycle if dehumidification was required
      // at the restart.

      // Only call if no longer require dehumidification and extendSystemRunTime is NOT exhausted
      if (!dehumidifyOn) {
        Serial.println("5 min extended run time forced");
        lcd.println("Extended run time active");
      }

      // Only update variable if not already set and extendSystemRunTime is NOT exhuasted
      if (!extendRun) {
        extendRun = true;
      }
    } else if (extendRun) {
      // Only occurs when extendRun active and extendSystemRunTime has been exhausted
      Serial.println("Extended run time expired\nDehumidification can stop (5 min delay reached) when desired humidity level is achieved"); // This is to prevent stop/starts
      lcd.println("Extended run time expired");
      extendRun = false;
    }

    /*

       Handle "dehumidifyOn" variable here

    */
    if (h >= onHumidity && !dehumidifyOn) {
      dehumidifyOn = true;
      previousMillis = millis();
      runCount++;
      Serial.println("High Humidty, begin dehumidifying and delay stopping for 5 min");
      lcd.println("High Humidity deected");
      Serial.print("System has now operated ");
      Serial.print(runCount);
      Serial.println(" times");
    } else if (h <= offHumidity && dehumidifyOn) {
      Serial.print("Desired Humidity Reached: ");
      Serial.print(h);
      Serial.println("%\nStopping dehumidification...\n\n");
      runDuration += (millis() - previousMillis);
      dehumidifyOn = false;
      Serial.print("The system has run for ");
      Serial.print(runDuration / 1000);
      Serial.println(" seconds");
    }

// Work in progress for counting beyond 49days of runtime
//      if (cMillis < previousMillis) {
//        // The millis has rolled... (49 days)
//        runDuration +=  4294967295 - previousMillis + cMillis;
//        if (runDuration >= 86400000) {
//          runDays++;
//        }
//        }
//      } else {
//        runDuration += (cMillis - previousMillis);
//      }
      


    /*

       Handle relays for fan and dehumidification

    */
    if ((dehumidifyOn && !delayStart) || (extendRun && !delayStart)) {
      if (digitalRead(FANCTRL) == LOW) {
        Serial.println("Turning on Fan");
        lcd.println("Turning on fan");
        digitalWrite(FANCTRL, HIGH);
      } else {
        Serial.println("Fan running");
      }
      if (digitalRead(DEHUMCTRL) == LOW) {
        Serial.println("Turning on Dehumidifier");
        lcd.println("Turning on dehumidifier");
        digitalWrite(DEHUMCTRL, HIGH);
      } else {
        Serial.println("Dehumidifier running");
      }
      Serial.println("\nDedehumidifying...\n");
      lcd.print("\nDedehumidifying...");
    } else {
      if (digitalRead(FANCTRL) == HIGH) {
        Serial.println("Turning off Fan...");
        lcd.println("Turning off fan...");
        digitalWrite(FANCTRL, LOW);
      }
      if (digitalRead(DEHUMCTRL) == HIGH) {
        Serial.println("Turning off Dehumidifier...");
        lcd.println("Turning off dehumidifier...");
        digitalWrite(DEHUMCTRL, LOW);
      }
    }
    lcd.display();
    delay(5000);
  }

// Part of the run duration over 49days
//    runtimeCounters(millis());

}

void toggleRelay(int _realy) {
  digitalWrite(_realy, !digitalRead(_realy));
}

unsigned int runtimeCounters(cMillis) {
  /* Test for the start of application
   * If the currentMillis - dayMillis is >= 86400000 then reset the day millis and increment the dayCount by one.
   */ 
  if (dayMillis > cMillis) {
    temp = 4294967295 - dayMillis + cMillis;
  } else {
    if (cMillis - dayMillis >= 86400000) 
    {
      dayCount++;
      if (dayMillis >= 86400000) {
        dayMillis = 86400000;
      } else {
        dayMillis = cMillis - (cMillis - dayMillis - 86400000);
      }
    }
  }
}
