/* 
 *  Video Time Inserter: http://smopi.news.nstrefa.pl/ wersja v2.0.
 *  Wymagania sprzętowe:
 *  - Arduino UNO,
 *  - odbiornik GPS U-Blox NEO-6M lub zgodny,
 *  - VideoOverlayShield MAX7456.
 *  Moduł główny jest odpowiedzialny za odczytanie z odbiornika GPS aktualnego położenia, daty oraz godziny. Dane te
 *  są następnie umieszczane w obrazie wideo PAL lub NTSC.
 *  
 *  Piotr Smolarz, e-mail: smopi.pl@gmail.com
 *  
 *  
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#define httpString   "--smopi.news.nstrefa.pl--"
#define verString    "--  2015-16 VTI v2.0   --"


#include <Time.h>      //Time      (http://www.pjrc.com/teensy/td_libs_Time.html) by Michael Margolis
#include <TinyGPS++.h> //TinyGPS++ (http://arduiniana.org/libraries/tinygpsplus/) by Mikal Hart
#include <SPI.h>
#include <MAX7456.h>
#include "VTI.h"

// Czas systemowy VTI
VTItime time1;

// CheckCPU clock
#define MAX_CHECKS 5
#define CPU_STEPS 16
volatile bool          checkedClock = false; //Czy częstotliwość zegara jest wyznaczona
volatile unsigned int  checkNo      = 0;     //Numer sprawdzenia częstotliwości zegara
volatile bool          isPPSready   = false; //Czy pojawił się impuls 1PPS
unsigned long          averageClock = 0;     //Średnia częstotliwość zegara
volatile unsigned long msTimeStamp  = 0;
volatile unsigned long microsPPS[MAX_CHECKS];//Mikrosekundy pomiędzy impulsami PPS

//Display mode pin: Date/Time <-> Info
const int displayModePin = 4;

//GPS RXPin = 0, TXPin = 1
const uint32_t GPSBaud        = 9600; //Prędkość komunikacji z GPS
const byte     PPSpin         = 3;    //PPS pin
const unsigned int MAX_INPUT  = 20;   //Wielkośc bufora na dane z GPS
unsigned int input_pos        = 0; 

// The TinyGPS++ object
TinyGPSPlus GPS;

//OSD
const byte osdChipSelect    = 10;
const byte masterOutSlaveIn = MOSI;
const byte masterInSlaveOut = MISO;
const byte slaveClock       = SCK;
const byte osdReset         = 0;
uint8_t    videoFields      = 0;

//OSD object
MAX7456 OSD( osdChipSelect );


//******************************************************//
//SETUP - run one time
//******************************************************//
void setup()
{
  unsigned char system_video_in=NULL;
  TimeElements  tm;

  pinMode(PPSpin, INPUT);                //PPS
  pinMode(displayModePin, INPUT_PULLUP); //Display mode
  
  Serial.begin(GPSBaud); //GPS Serial
  
  // Initialize the SPI connection:
  SPI.begin();
  SPI.setClockDivider( SPI_CLOCK_DIV2 ); // Must be less than 10MHz.
    
  // Initialize the MAX7456 OSD:
  OSD.begin();                           // Use NTSC with default area.
  OSD.setSwitchingTime( 5 );             // Set video croma distortion 
                                         // to a minimum.
  OSD.setCharEncoding( MAX7456_ASCII );  // Only needed if ascii font.
    
  system_video_in=OSD.videoSystem();
  
  if(NULL!=system_video_in)
  {
    OSD.setDefaultSystem(system_video_in);
  }
  else
  {
    OSD.setDefaultSystem(MAX7456_PAL);
  }

  
  OSD.display(); // Activate the text display.

  // Info about VTI version
  OSDfooter();

  //******************************************************//
  //Konfiguracja odbiornika GPS
  //******************************************************//
  OSD.setCursor( 0, 0 );
  OSD.print("Konfiguracja GPS....");
  do
  {
    static int i = 1;

    // Wykonuj jeśli nie w VSYNC
    while (OSD.notInVSync()) 
    {}
    
    OSD.setCursor( 22, 0 );
    OSD.print(i);
    i++;
  } while (!configureGPS());
  
  OSD.setCursor( 22, 0 );
  OSD.print("OK");
  
  
  //******************************************************//
  //Czekam na impuls PPS 
  //******************************************************//
  OSD.setCursor( 0, 1 );
  OSD.print("Czekam na PPS.......");

  attachInterrupt(digitalPinToInterrupt(PPSpin), checkClock, RISING);
  
  do
  {
    static int i = 1;

    delay(1000);

    // Wykonuj jeśli nie w VSYNC
    while (OSD.notInVSync()) 
    {}
    
    OSD.setCursor( 22, 1 );
    OSD.print(i);
    i++;
  } while (!isPPSready);
  
  OSD.setCursor( 22, 1 );
  OSD.print("OK ");

  
  //******************************************************//
  //Kalibracja zegara
  //******************************************************//
  OSD.setCursor(0, 2);
  OSD.print("Kalibracja zegara...");
  
  while(!checkedClock)
  {
    // Wykonuj jeśli nie w VSYNC
    while (OSD.notInVSync()) 
    {}
    
    OSD.setCursor( 22, 2 );
    OSD.print(checkNo);
  }
  
  detachInterrupt(digitalPinToInterrupt(PPSpin));
   
  OSD.setCursor( 22, 2 );
  OSD.print("OK ");

  // Print results
  for(int i = 0; i < MAX_CHECKS; i++)
  {
    OSD.setCursor(1, 3 + i);
    averageClock += microsPPS[i];
    OSD.print(CPU_STEPS * microsPPS[i]);
    OSD.print(" Hz");
  }
  // CPU clock is...
  averageClock = (averageClock / MAX_CHECKS) * CPU_STEPS;

  
  //******************************************************//
  //Czekam na dane z GPS
  //******************************************************//
  OSD.setCursor(0, 8);
  OSD.print("Czekam na dane z GPS");
 
  do {
    updateGPSobj(0);
  } while(!(GPS.location.isValid()));
  
  do {
    updateGPSobj(0);  
  } while(!(GPS.time.isValid() && GPS.date.isValid() && GPS.time.age() < 200));

  
  //System date and time
  tm.Second = GPS.time.second();
  tm.Minute = GPS.time.minute();
  tm.Hour   = GPS.time.hour();
  tm.Wday   = NULL;
  tm.Day    = GPS.date.day();
  tm.Month  = GPS.date.month();
  tm.Year   = GPS.date.year() - 1970;

  //Set date and time
  time1.setDateTime(makeTime(tm), GPS.time.age());
  
  
  //******************************************************//
  //Initialize timer1
  //******************************************************//
  noInterrupts();           // disable all interrupts
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1  = 0;

  OCR1A = (int) (averageClock / 1000); // compare match register 16MHz/1/1000Hz
  TCCR1B |= (1 << WGM12);      // CTC mode
  TCCR1B |= (1 << CS10);       // 1 prescaler
  TIMSK1 |= (1 << OCIE1A);     // enable timer compare interrupt
  interrupts();                // enable all interrupts
  // timer1 initialized

  // Initialize PPS event
  attachInterrupt(digitalPinToInterrupt(PPSpin), PPSevent, RISING);

  // Clear screen
  OSD.clear();
}
//******************************************************//
//END SETUP
//******************************************************//

// Interrupt service routine
// Co 1ms wykonaj aktualizację czasu VTI
ISR(TIMER1_COMPA_vect)      // timer compare interrupt service routine
{
  time1.Update();
}


//******************************************************//
//MAIN LOOP - run over and over
//******************************************************//
void loop()
{
  unsigned long       nowMS;                  //aktualna wartość milisekund
  time_t              nowDateTime;            //aktualna data i godzina (time_t)
  static char         input_line [MAX_INPUT]; //bufor na dane z GPS
  static unsigned int input_pos  = 0;         //ile danych w buforze    
  static boolean      isInfoMode = false;     //Wybór trybu OSD

  // Czy w buforze są dane?
  if(input_pos > 0) 
  {
    for(int i = 0; i < input_pos; i++)
    {
      //Pobierz dane z GPS
      if(GPS.encode(input_line[i]) && GPS.time.isValid())
      {
        // Porównaj czas systemowy z GPS nie częściej niż co 900 ms
        // W przypadku różnicy wyświetl komunikat błędu.
        if( !checkVTItime(900) ) OSDFatalError();
      }
    }
    input_pos = 0;
  }
  //endif

  // Wykonuj jeśli nie w VSYNC
  while (OSD.notInVSync()) 
  {
    // Wykonaj jeśli są dostępne dane z odbiornika GPS i miejsce w buforze
    if (Serial.available() > 0 && input_pos < MAX_INPUT)
    {
      input_line[input_pos] = Serial.read();
      input_pos += 1;
    }
  }
  //end while

  // SWITCH - select display mode
  switch(digitalRead(displayModePin))
  {
    // Date/Time mode
    case HIGH:
      noInterrupts();           // disable all interrupts
        nowMS       = time1.getMillis();
        nowDateTime = time1.getDateTime();
      interrupts();             // enable all interrupts
  
      if (isInfoMode)
      {
        isInfoMode = false;
        OSD.clear();
        osdTime(true, nowDateTime);
        osdDate(true, nowDateTime);
        osdShortInfo();
      }
      osdMillis(nowMS);
      osdTime(false, nowDateTime);
      osdDate(false, nowDateTime);
      osdShortInfo();
    break;

    // Info mode
    case LOW:
      if (!isInfoMode)
      {
        isInfoMode = true;
        OSD.clear();
      }
      osdInfo();
    break;
  }
  // END SWITCH
  
  while (!OSD.notInVSync())
  {
   
  }
  
}
//******************************************************//
//END MAIN LOOP
//******************************************************//


// Display millis
void osdMillis(unsigned long ms)
{ 
  static unsigned int counter = 0;
  char msChars[] = "       ";
  byte posOffset = 4 * (counter % 2); // 0 or 4
  
  msChars[2 + posOffset] = ms       % 10 + '0';      
  msChars[1 + posOffset] = ms / 10  % 10 + '0';
  msChars[0 + posOffset] = ms / 100 % 10 + '0';
  
  OSD.setCursor( 10 , 0 );
  OSD.print( msChars );
  
  counter++;
}


// Display time
void osdTime(boolean mustDisplay, time_t DT)
{ 
  static char t[] = "00:00:00 ";
  static int prevSecond = 0;
  static int prevMinute = 0;
  static int prevHour   = 0;
  
  int curSecond = second(DT);
  int curMinute = minute(DT);
  int curHour   = hour(DT);
  
  boolean updatedTime = false;

  //Time
  if (prevSecond != curSecond)
  {
    prevSecond = curSecond;
    updatedTime = true;
    t[6]  = curSecond / 10 + '0';
    t[7]  = curSecond % 10 + '0';
    
    t[8] = (millis() - msTimeStamp > 1000) ? ' ' : 'P';
    
    if (prevMinute != curMinute)
    {
      prevMinute = curMinute;
      t[3]  = curMinute / 10 + '0';
      t[4]  = curMinute % 10 + '0';

      if(prevHour != curHour)
      {
        prevHour = curHour;
        t[0]  = curHour / 10 + '0';
        t[1]  = curHour % 10 + '0';
      }
    }
  }
  
  if(updatedTime || mustDisplay)
  {
    OSD.setCursor( 0, 0 );
    OSD.print( t );
  }
}


// Display date
void osdDate(boolean mustDisplay, time_t DT)
{ 
  static char d[] = "0000-00-00";
  static int prevDay    = 0;
  
  int curDay    = day(DT);
  boolean updatedDate = false;
  
  // Date
  if(prevDay != curDay)
  {
    prevDay = curDay;
    updatedDate = true;
    
    d[8]  = curDay / 10 + '0';
    d[9]  = curDay % 10 + '0';

    d[5]  = month(DT) / 10 + '0';
    d[6]  = month(DT) % 10 + '0';
  
    d[0]  = year(DT) / 1000 + '0';
    d[1]  = year(DT) / 100 % 10 + '0';
    d[2]  = year(DT) / 10  % 10 + '0';
    d[3]  = year(DT) % 10       + '0';
  }
  
  if (updatedDate || mustDisplay)
  {
    OSD.setCursor( 0, OSD.rows() - 1 );
    OSD.print( d );
  }  
}


// Display info
void osdInfo()
{
  static unsigned long lastUpdated = 0;
  unsigned int tmpHDOP = 0;
  
  if (millis() - lastUpdated > 1000)
  {
    lastUpdated = millis();
    
    OSD.setCursor( 0, 0 );
    OSD.print(GPS.location.lat(), 5);
    (GPS.location.rawLat().negative) ? OSD.print(" S") : OSD.print(" N");
  
    OSD.setCursor( 0, 1 );
    OSD.print(GPS.location.lng(), 5);
    (GPS.location.rawLng().negative) ? OSD.print(" W") : OSD.print(" E");
  
    OSD.setCursor( 0, 3 );
    (GPS.altitude.isValid()) ? OSD.print(GPS.altitude.meters(), 0) : OSD.print("---");
    OSD.print(" m  ");
  
    OSD.setCursor( 0, 5 );
    OSD.print("satellites: ");
    OSD.print(GPS.satellites.value());

    tmpHDOP = GPS.hdop.value();
    OSD.print(" HDOP: ");
    OSD.print(tmpHDOP / 100);
    OSD.print(".");
    OSD.print((tmpHDOP / 10) % 10);
    OSD.print("  ");

    OSD.setCursor( 0, 7 );
    OSD.print("NMEA failed: ");
    OSD.print(GPS.failedChecksum());

    OSD.setCursor( 0, 8 );
    OSD.print("NMEA passed: ");
    OSD.print(GPS.passedChecksum());
  }
  OSDfooter();
  OSD.home();
}


// Short info
void osdShortInfo()
{
  static unsigned long ms = 0;

  if (millis() - ms > 1000)
  {
    OSD.setCursor(OSD.columns() - 2, 0);
    OSD.print(GPS.satellites.value());
    OSD.print(" ");

    ms = millis();
  }
}


// Info about VTI version
void OSDfooter()
{
  OSD.setCursor( 0, OSD.rows() - 2 );
  OSD.print( httpString );
  
  OSD.setCursor( 0, OSD.rows() - 1 );
  OSD.print( verString );
}


// Update GPS object
void updateGPSobj(unsigned long timeout)
{
  unsigned long ms = millis();
     
  while (Serial.available() > 0)
  {
    GPS.encode(Serial.read());
    
    if (millis() - ms > timeout && timeout != 0)
    {
      // Timeout!
      return;
    }
  }
}


// Porównaj czas systemowy z czasem GPS
bool checkVTItime(unsigned long msParam)
{
  static unsigned long ms = millis();
  bool val  = true;
  time_t DT = time1.getDateTime();

  if (millis() - ms > msParam)
  {
    ms = millis();
    val = (GPS.time.hour() == hour(DT) && GPS.time.minute() == minute(DT) && GPS.time.second() == second(DT)) ? true : false;
  }
  return val;
}


// Wyświetl informację o poważnym błędzie
void OSDFatalError()
{
  time_t DT = time1.getDateTime();
  
  //OSD.clear();
  OSD.setCursor(0,2);
  OSD.print("FATAL ERROR-RESTART VTI!");

  OSD.setCursor(0,4);
  OSD.print("VTI time: ");
  OSD.print(hour(DT));OSD.print(minute(DT));OSD.print(second(DT));

  OSD.setCursor(0,5);
  OSD.print("GPS time: ");
  OSD.print(GPS.time.hour());OSD.print(GPS.time.minute());OSD.print(GPS.time.second());
  
  //while(1) {};
}


//Wykonaj gdy impuls PPS
void PPSevent()
{
  msTimeStamp = millis();

  time1.roundDateTime(); //Zaokrąglij aktualną godzinę do pełej sekundy
  
  TCNT1  = 0;            //Wyzeruj rejest w timer1 ATmega328
}


//
void checkClock()
{
  static unsigned long microsTS = 0;
  static unsigned long millisTS = 0;

  isPPSready = true;

  long tmpMS = (millis() - millisTS) - 1000;

  if(abs(tmpMS) < 5)
  {
    microsPPS[checkNo] = micros() - microsTS;
    checkNo += 1;
  }

  microsTS = micros();
  millisTS = millis();

  checkedClock = (checkNo == MAX_CHECKS) ? true : false;
}


//Configure GPS
boolean configureGPS()
{
  int counter = 0;
  boolean gps_set_success=false;
  
   // Portmode:
   uint8_t setPORTMODE[] = { 
   0xB5, 0x62, 0x06, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0xD0, 0x08, 0x00, 0x00, 0x80, 0x25, 0x00, 0x00, 0x07, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA1, 0xAF};
   while(!gps_set_success && counter < 3)
   {    
   sendUBX(setPORTMODE, sizeof(setPORTMODE)/sizeof(uint8_t));
   gps_set_success=getUBX_ACK(setPORTMODE);
   counter += 1;
   }
   if (gps_set_success)
   {
     gps_set_success=false;
     counter = 0;
   }
   else
   {
     return false;
   }
  
   // Switching off NMEA GLL:
   uint8_t setGLL[] = { 
   0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x2B};
   while(!gps_set_success && counter < 3)
   {    
   sendUBX(setGLL, sizeof(setGLL)/sizeof(uint8_t));
   gps_set_success=getUBX_ACK(setGLL);
   counter += 1;
   }
   if (gps_set_success)
   {
     gps_set_success=false;
     counter = 0;
   }
   else
   {
     return false;
   }
   
   // Switching off NMEA GSA:
   uint8_t setGSA[] = { 
   0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x32};
   while(!gps_set_success && counter < 3)
   {  
   sendUBX(setGSA, sizeof(setGSA)/sizeof(uint8_t));
   gps_set_success=getUBX_ACK(setGSA);
   counter += 1;
   }
   if (gps_set_success)
   {
     gps_set_success=false;
     counter = 0;
   }
   else
   {
     return false;
   }
   
   // Switching off NMEA GSV:
   uint8_t setGSV[] = { 
   0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x39};
   while(!gps_set_success && counter < 3)
   {
   sendUBX(setGSV, sizeof(setGSV)/sizeof(uint8_t));
   gps_set_success=getUBX_ACK(setGSV);
   counter += 1;
   }
   if (gps_set_success)
   {
     gps_set_success=false;
     counter = 0;
   }
   else
   {
     return false;
   }
   
   // Switching off NMEA VTG:
   uint8_t setVTG[] = { 
   0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x05, 0x47};
   while(!gps_set_success && counter < 3)
   {
   sendUBX(setVTG, sizeof(setVTG)/sizeof(uint8_t));
   gps_set_success=getUBX_ACK(setVTG);
   counter += 1;
   }
   if (gps_set_success)
   {
     gps_set_success=false;
     counter = 0;
   }
   else
   {
     return false;
   }   
   return true; //Configuration success
}


// Send a byte array of UBX protocol to the GPS
void sendUBX(uint8_t *MSG, uint8_t len) {
  for(int i=0; i<len; i++) {
    Serial.write(MSG[i]);
  }
  Serial.println();
}
 
 
// Calculate expected UBX ACK packet and parse UBX response from GPS
boolean getUBX_ACK(uint8_t *MSG) {
  uint8_t b;
  uint8_t ackByteID = 0;
  uint8_t ackPacket[10];
  unsigned long startTime = millis();
  Serial.print(" * Reading ACK response: ");
 
  // Construct the expected ACK packet    
  ackPacket[0] = 0xB5;  // header
  ackPacket[1] = 0x62;  // header
  ackPacket[2] = 0x05;  // class
  ackPacket[3] = 0x01;  // id
  ackPacket[4] = 0x02;  // length
  ackPacket[5] = 0x00;
  ackPacket[6] = MSG[2];  // ACK class
  ackPacket[7] = MSG[3];  // ACK id
  ackPacket[8] = 0;   // CK_A
  ackPacket[9] = 0;   // CK_B
 
  // Calculate the checksums
  for (uint8_t i=2; i<8; i++) {
    ackPacket[8] = ackPacket[8] + ackPacket[i];
    ackPacket[9] = ackPacket[9] + ackPacket[8];
  }
 
  while (1) {
 
    // Test for success
    if (ackByteID > 9) {
      // All packets in order!
      return true;
    }
 
    // Timeout if no valid response in 3 seconds
    if (millis() - startTime > 3000) { 
      return false;
    }
 
    // Make sure data is available to read
    if (Serial.available()) {
      b = Serial.read();
 
      // Check that bytes arrive in sequence as per expected ACK packet
      if (b == ackPacket[ackByteID]) { 
        ackByteID++;
      } 
      else {
        ackByteID = 0;  // Reset and look again, invalid order
      }
 
    }
  }
}
