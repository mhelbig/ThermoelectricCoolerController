#include <OneWire.h>
#include <DallasTemperature.h>

// Constants:
const float TempSetpoint = 32.5;                 // Target temperature of the cooler in degrees F
const float TempHysteresis = .125;               // +/- Switchpoint hysteresis in degrees F
const float FrostFormingTemp = 40;               // Temperature where frost begins to form
const float FrostMeltedTemp = 43;                // Temperature where frost has melted off during the defrost cycle

const unsigned long EventInterval = 60;          // Send event string every 60 seconds
const unsigned long FrostBuildupLimit = 1440;    // Minutes of frost buildup before defrosting starts (24hr = 1440)
const unsigned long DefrostRecoveryTime = 10;    // Minutes Defrost LED flashes code 12 after defrost mode ends

// Variables:
float tempF;                                     // Current cooler temperature measurement
float CoolingActive = 0;                         // Tracks cooling active for %
float CoolingPeriod = 0;                         // Tracks cooling period for %
int CoolerState =1;                              // Current Cooler operational state, starting in state 1
int LEDblinkCode;                                // 2 digit blink code for the LED
unsigned long FrostBuildup = 0;                  // Accumulated minutes of frost buildup
unsigned long PostDefrostTime = 0;               // Minutes elapsed after defrost cycle completes
unsigned long EventLogTimer=0;                   // Event data Timer (in seconds) for serial output interval

// I/O Ports:
const int BatteryVoltage = 0;
const int Defrost = 7;
const int Fan = 8;
const int Peltier = 9;
const int LED = 10;

// Dallas Digital Thermometer Setup:
#define ONE_WIRE_BUS 11
OneWire oneWire(ONE_WIRE_BUS);                   // Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
DallasTemperature sensors(&oneWire);             // Pass our oneWire reference to Dallas Temperature.
DeviceAddress CoolerThermometer;                 // Arrays to hold device address

void setup()
{                
  // I/O pin setup:
  pinMode(Defrost, OUTPUT);
  pinMode(Fan, OUTPUT);     
  pinMode(Peltier, OUTPUT);     
  pinMode(LED, OUTPUT);     

  // Serial port setup:
  Serial.begin(9600);

  // Temperature Sensor setup:  
  sensors.begin();
  sensors.getAddress(CoolerThermometer,0);
  sensors.setResolution(CoolerThermometer, 12);

  // A/D setup
  analogReference(DEFAULT);

  PrintEventHeader();
  MinuteTick(0);                // Synchronize the minute tick counter
}

void loop()
{
  ReadTemperature();

  switch (CoolerState)
  {
  case 1:  //Cooling mode but not cool enough for frost to form
    SetDefrost(LOW);
    SetFan(HIGH);
    RunCoolingMode();
    LEDblinkCode = int(tempF+0.5);

    if (tempF < FrostFormingTemp)
    {
      CoolerState = 2;    // switch to frost forming mode if cool enough
      FrostBuildup = 0;   // Zero the frost accumulator
      MinuteTick(0);      // Zero the minute counter for the frost buidup timer
    }
    break;

  case 2:  //Cooling mode, frost is forming
    RunCoolingMode();
    FrostBuildup += MinuteTick(1);
    LEDblinkCode = int(tempF+0.5);

    if (tempF > FrostMeltedTemp) CoolerState = 1;  // switch back to no frost mode if cooler warms up again
    else if (FrostBuildup > FrostBuildupLimit) CoolerState = 3; // enough frost has builtup to go into defrost mode
    break;

  case 3:  //Defrosting, waiting for temperature rise to indicate frost is gone
    SetFan(LOW);
    SetDefrost(HIGH);
    SetPeltier(HIGH);
    LEDblinkCode = 11;

    if (tempF > FrostMeltedTemp)      // Temp rise means frost has all melted away
    {
      FrostBuildup = 0;               // Zero the frost accumulator
      MinuteTick(0);                  // Zero the minute counter for the defrost normalizing timer
      CoolerState = 4;                // Switch into state 4    
    }
    break;

  case 4:  //Done defrosting, waiting for temperature to normalize so we can resume blinking temperature on the LED
    LEDblinkCode = 12;
    SetDefrost(LOW);                // Switch back to cooling mode
    SetFan(HIGH);
    RunCoolingMode();

    PostDefrostTime += MinuteTick(1);
    if (PostDefrostTime > DefrostRecoveryTime)
    {
      CoolerState = 1;
      PostDefrostTime = 0;
    }
    break;
  }

  FlashLEDcode(LEDblinkCode);
  PrintEventData();
}

void ReadTemperature(void)
{
/*  if (Serial.available())
  {
    tempF = float(Serial.read());
  }
*/
  
  sensors.requestTemperatures();
  tempF = DallasTemperature::toFahrenheit(sensors.getTempC(CoolerThermometer));

  // Bounds check the temperature and set any temperature related error codes
  if(tempF > 99)
  {
    tempF = 99;
    LEDblinkCode = 19;
  }
  else if(tempF < 20)
  {
    tempF = 20;
    LEDblinkCode = 18;
  }
}

void RunCoolingMode(void)
{
  CoolingPeriod++; 
  if(tempF > TempSetpoint + TempHysteresis )
  {
    SetPeltier(HIGH);
    CoolingActive++;
  }
  if(tempF < TempSetpoint - TempHysteresis ) SetPeltier(LOW);
}

void PrintEventHeader(void)
{
  Serial.println("Minutes,TempF,CoolingPercentage,CoolerState,FrostBuildup,PostDefrostTime");
}

void PrintEventData(void)
{
  if (EventLogTimer < Seconds())
  {
    EventLogTimer += EventInterval;
    Serial.print(Seconds()/60);
    Serial.print(",");
    Serial.print(tempF);
    Serial.print(",");
    if(CoolingPeriod > 0) Serial.print((CoolingActive/CoolingPeriod)*100,0);
    else Serial.print("0");
    Serial.print(",");
    Serial.print(CoolerState);
    Serial.print(",");
    Serial.print(FrostBuildup);
    Serial.print(",");
    Serial.print(PostDefrostTime);
    Serial.println();

    CoolingPeriod = 0;                            // Reset the cooling percentage counters
    CoolingActive = 0;
  }
}

void FlashLEDcode(int Code)
{
  int TensDigit;                                   // Temperature tens digit for flash code
  int OnesDigit;                                   // Temperature ones digit for flash code

  TensDigit = int((Code)/10);
  OnesDigit = int((Code)-(TensDigit*10));

  FlashLED(TensDigit);
  FlashLED(OnesDigit);
  delay(500);
}

void FlashLED(int Count)
{
  int Counter;

  if (Count == 0)                                // Do a tiny little flash for zeros
  {
    SetLED(HIGH);
    delay(5);
    SetLED(LOW);
    delay(295);
  }

  else                                          // Flash normally for all other digits
  {  
    for (Counter=0; Counter < Count; Counter++)
    {
      SetLED(HIGH);
      delay(75);
      SetLED(LOW);
      delay(225);
    }
    delay(650);
  }
}

int MinuteTick(int Command)
{
  static unsigned long RolloverSeconds;

  if (Command == 0)                    // Command of 0 means reset the counter to start a new minute
  {
    RolloverSeconds = Seconds() + 60;
    return (0);
  }
  
  if(RolloverSeconds < Seconds())
  {
    RolloverSeconds += 60;
    return (1);
  }
  
  else return (0);
  
}

unsigned long Seconds(void)
{
  return (millis()/1000);
}

void SetLED(bool State)
{
  if(State == HIGH) digitalWrite(LED, HIGH);
  else digitalWrite(LED,LOW);
}

void SetPeltier(bool State)
{
  if(State == HIGH) digitalWrite(Peltier, HIGH);
  else digitalWrite(Peltier, LOW);
}

void SetDefrost(bool State)
{
  if(State == HIGH) digitalWrite(Defrost, HIGH);
  else digitalWrite(Defrost, LOW);
}

void SetFan(bool State)
{
  if(State == HIGH) digitalWrite(Fan, HIGH);
  else digitalWrite(Fan, LOW);
}
