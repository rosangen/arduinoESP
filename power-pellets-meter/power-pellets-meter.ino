#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>       // https://github.com/knolleary/pubsubclient
#include <TimeLib.h>            // https://github.com/PaulStoffregen/Time
#include <EEPROM.h>
#include <Ticker.h>
#include <DallasTemperature.h>  // https://github.com/milesburton/Arduino-Temperature-Control-Library
#include "secrets.h"

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWOD;

#define MQTT_VERSION MQTT_VERSION_3_1_1

// MQTT: ID, server IP, port, username and password
const PROGMEM char* MQTT_CLIENT_ID = "home/sensor1";
const PROGMEM char* MQTT_SERVER_IP = "192.168.2.25";
const PROGMEM uint16_t MQTT_SERVER_PORT = 1883;
const PROGMEM char* MQTT_USER = "homeassistant";
const PROGMEM char* MQTT_PASSWORD = "billprupp";

WiFiClient wifiClient;
PubSubClient client(wifiClient);

// Create an instance of the server
// specify the port to listen on as an argument
WiFiServer server(80);  // se https://github.com/esp8266/Arduino/blob/master/libraries/ESP8266WiFi/examples/WiFiWebServer/WiFiWebServer.ino

const int turnPin     = D5;
const int blinkPin    = D7;
const int one_wirePin = D3;
const int led2        = D8;
const int led3        = D0;

// Pellets
unsigned long turns,timeTurne,sentKgDay;
byte burnerOn = false;
byte burnerUh = false;
unsigned long startTimeDelay = 1;  // minuter mellan varv för att starta brännaren
unsigned long stopTimeDelay  = 9;  // minuter till den tror att brännaren har stannat
int gramTurn = 44;       // Tidigare 42
long gramToday,storageGram,bagHome;
long smallCleanTurn, bigCleanTurn;
//$kw_kg = 3.5;

unsigned long varv,turnStateTime,turnHighTime;
unsigned long burnerStartTime,burnerEndTime,turnBurnerStart,turnBurnerEnd;
byte trunState;


// Blink
Ticker pulse;
volatile byte blinkState = false;
volatile unsigned long blinks,millisBlink;
unsigned long lastBlink,millisLastBlink,millisLastSentBlink,lastSentHouerMillis;
unsigned long lastSentHouer, hourStartBlink,lastSentDay, dayStartBlink, lastSaveTime;
byte ledState = false;
unsigned long kWhcounter;
long kWh;
long detectCounter;
byte printlog = true;

// Other
unsigned long liveTime;
unsigned long mqttReconnetTryeTime;
int sendTempState = 0;
int sensorCount = 0;
float outTemp = 9999,feedTemp,returnTemp;
Ticker led3Blink;

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(one_wirePin);
// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature sensors(&oneWire);
// arrays to hold device address
DeviceAddress sensorAddress;

// NTP Servers:
static const char ntpServerName[] = "pool.ntp.org";
const int timeZone = 1;              // Central European Time

WiFiUDP Udp;
unsigned int localPort = 8888;       // local port to listen for UDP packets

time_t getNtpTime();
void digitalClockDisplay();
void printDigits(int digits);
void sendNTPpacket(IPAddress &address);


/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
  IPAddress ntpServerIP; // NTP server's ip address

  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  Serial.println("Transmit NTP Request");
  // get a random server from the pool
  WiFi.hostByName(ntpServerName, ntpServerIP);
  Serial.print(ntpServerName);
  Serial.print(": ");
  Serial.println(ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 2500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  Serial.println("No NTP Response :-(");
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

void reconnect() {
  long now = millis();
  if ( now > mqttReconnetTryeTime + 15000 ) {
    Serial.print("\nINFO: Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
      Serial.print("\nINFO: connected");
    } else {
      Serial.print("\nERROR: failed, rc=");
      Serial.print(client.state());
      Serial.print(" DEBUG: try again in 15 seconds");
    }
  mqttReconnetTryeTime = now;
  }
}
// function called when a MQTT message arrived
void callback(char* p_topic, byte* p_payload, unsigned int p_length) {

  Serial.println("[");
  Serial.println(p_topic);
  Serial.println("]");
}


void webserver() {
  // Check if a client has connected
  WiFiClient client = server.available();
  if (!client) {
    return;
  }
  int i=0;
  // Wait until the client sends some data
  Serial.print("\nNew client: ");
  while(!client.available()){
    delay(10);
    i++;
    if (i > 1000) {
      Serial.println("Exit!");
      return;
    }
  }
  
  // Read the first line of the request
  String req = client.readStringUntil('\r');
  if (req.length() < 1) return;
  
  Serial.print(req);
  client.flush();
  
  if (req.indexOf("/favicon.ico") != -1){
    // Serial.println("favicon");
    String s = "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\n\r\n\n";
    client.print(s);
    delay(10);
    return;
  }
  if (req.indexOf(WEBSERVER_PASSWORD) == -1) {
    Serial.println("\nInvalid request.");
    String s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>\r\nOk, all system running.</html>\n";
    client.print(s);
    delay(10);
    return;
  }
  if (req.indexOf("/info/") != -1) {
    Serial.print("\nInfo:");
    // printTurnValues();
  }
  else if (req.indexOf("/temp") != -1) {
    client.print("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>\r\n");
    printTemp();
    client.print("ok...");
    client.print("</html>\n");
    delay(20);
    return;
  }
  else if (req.indexOf("/show/storage-g") != -1) {
    Serial.print("\nShow storage g: ");
    Serial.print(storageGram);
    client.print("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>\r\n");
    client.print(storageGram);
    client.print("</html>\n");
    delay(10);
    return;
  }
  else if (req.indexOf("/bagsh/") != -1) {
    Serial.print("\nBags home:");
    long value = req.substring(req.indexOf("/bagsh/")+7, req.indexOf(" HTTP/1") ).toInt() ;
    Serial.print( value );
    bagHome = value;
  }
  else if (req.indexOf("/bagsa/") != -1) {
    Serial.print("\nBags added:");
    long value = req.substring(req.indexOf("/bagsa/")+7, req.indexOf(" HTTP/1") ).toInt() ;
    Serial.print( value );
    bagHome = bagHome + value;
  }
  else if (req.indexOf("/bagsp/") != -1) {
    Serial.print("\nBags poured in storage:");
    long value = req.substring(req.indexOf("/bagsp/")+7, req.indexOf(" HTTP/1") ).toInt() ;
    Serial.print( value );
    storageGram = storageGram + ( value * 16 * 1000 );
    bagHome = bagHome - value;
  }
  else if (req.indexOf("/storage/") != -1) {
    Serial.print("\nSets storage:");
    long value = req.substring(req.indexOf("/storage/")+9, req.indexOf(" HTTP/1") ).toInt() ;
    Serial.print( value );
    storageGram = value * 1000; // Concert to gram :)
  } 
  else if (req.indexOf("/kwh/") != -1) {
    Serial.print("\nSets kWh:");
    long value = req.substring(req.indexOf("/kwh/")+5, req.indexOf(" HTTP/1") ).toInt() ;
    Serial.print( value );
    kWh = value;
  }
  else if (req.indexOf("/sclean/") != -1) {
    Serial.print("\nSmall Clean Performed!");
    smallCleanTurn = turns;
  }
  else if (req.indexOf("/bclean/") != -1) {
    Serial.print("\nBig Clean Performed!");
    smallCleanTurn = turns;
    bigCleanTurn = turns;
  }
  else {
    Serial.print("\nError in request.");
    String s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>\r\nError.</html>\n";
    client.print(s);
    delay(10);
    return;
  }

  client.flush();

  // Prepare the response
  String s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>\r\nok</html>\n";

  // Send the response to the client
  client.print(s);
  delay(10);

  // The client will actually be disconnected 
  // when the function returns and 'client' object is detroyed

  saveTurnValues();
  printTurnValues();
  sendMQTTInfo();
  sendCleanValues();
}

void sendMQTTInfo () {
  char msg[20];
  snprintf (msg, 19, "%ld", storageGram);
  client.publish("home/sensor1/storage", msg);

  memset(msg, 0, sizeof(msg));  // clear
  snprintf (msg, 19, "%ld", bagHome);
  client.publish("home/sensor1/baghome", msg );

  memset(msg, 0, sizeof(msg));  // clear
  snprintf (msg, 19, "%ld", kWh);
  client.publish("home/sensor1/kwh", msg);
}

void printTurnValues(){
  Serial.print("\n\n*** All Pellets values ***");
  Serial.print("\nTurns:");
  Serial.print(turns);
  Serial.print("\ngramToday:");
  Serial.print(gramToday);
  Serial.print("\nstorageGram:");
  Serial.print(storageGram);
  Serial.print("\nsentKgDay:");
  Serial.print(sentKgDay);
  Serial.print("\nbagHome:");
  Serial.print(bagHome);
  Serial.print("\nburnerStartTime:");
  Serial.print(burnerStartTime);
  Serial.print("\nburnerEndTime:");
  Serial.print(burnerEndTime);
  Serial.print("\nturnBurnerStart:");
  Serial.print(turnBurnerStart);
  Serial.print("\nturnBurnerEnd:");
  Serial.print(turnBurnerEnd);
  Serial.print("\nburnerOn:");
  Serial.print(burnerOn);

  Serial.print("\nsmallCleanTurn:");
  Serial.print(smallCleanTurn);
  Serial.print("\nbigCleanTurn:");
  Serial.print(bigCleanTurn);
  Serial.print("\n*** END ***");
}

void printTime(){
  Serial.println();
  Serial.print("Time: ");
  if ( hour() < 10 ) Serial.print("0");
  Serial.print(hour());
  Serial.print(":");
  if ( minute() < 10 ) Serial.print("0");
  Serial.print(minute());
  Serial.print(":");
  if ( second() < 10 ) Serial.print("0");
  Serial.print(second());
}
void save_eeprom(int position, long number) {
  for (uint8_t i =0; i<4; i++) {
    EEPROM.write(position + i, number & 255);
    number = number >> 8;
  }
}

long load_eeprom(int position) {
  long number = 0;
  for (uint8_t i =0; i<4; i++) {
    number = number * 256;
    number += EEPROM.read(position + 3 -i);
  }
  return number;
}

int wattHistory[5];

void detectConsumer(int watt) {
  wattHistory[5] = wattHistory[4];
  wattHistory[4] = wattHistory[3];
  wattHistory[3] = wattHistory[2];
  wattHistory[2] = wattHistory[1];
  wattHistory[1] = wattHistory[0];
  wattHistory[0] = watt;

  // Diskmaskin = 1870  
  // Tvättmaskin = 
  // Torktumlare = 2130;  eller 2300
  
  int detektpower = 2250;  
  int range = 50;
  int diff;
  if (printlog) Serial.print("  Detekt:"); 
  
  
  byte detected = false;
  int i = 1;
  while(i < 6){
    diff = wattHistory[0] - wattHistory[i];  
    if (printlog) Serial.print(diff); 
    if (printlog) Serial.print(" ");

    if ( diff >=  detektpower - range &&  diff <=  detektpower + range ) detected = true;
    if ( diff*-1 >=  detektpower - range &&  diff*-1 <=  detektpower + range ) detected = true;
    i++;
  }

  if ( detected ) {
    Serial.print("\nPOWER ON");

    detectCounter++;
    char msg[20];
    snprintf (msg, 19, "%ld", detectCounter);
    client.publish("home/sensor1/dc", msg);
  }
}



void sendDayMessages(){

  if (!client.connected()) return;        // MQTT connect check

  unsigned long now = millis();
  byte sentOKkg = false;
  byte sentOKwhd = false;
  
  long gramDay = gramToday;
  char msg[20];
  snprintf (msg, 19, "%ld", gramDay);
  Serial.print("\nPublish total gram/day: ");
  Serial.print(msg);
  if ( client.publish("home/sensor1/gramday", msg) ) {
    Serial.println(" > Sent ok");
    sentOKkg = true;
  }

  long whd = ( (long)blinks - (long)dayStartBlink);

  memset(msg, 0, sizeof(msg));  // clear  
  snprintf (msg, 19, "%ld", whd);
  Serial.print(" Publish Wh/Day: ");
  Serial.print(msg);
  if ( client.publish("home/sensor1/whd", msg) ) {
    Serial.println(" > Sent ok.");
    sentOKwhd = true;
  }

  if ( sentOKwhd && sentOKkg ) {

    lastSentDay = day();
    dayStartBlink = blinks;

    gramToday = 0;
    sentKgDay = day();
  }
}
  

void sendBlink(){
  printlog = false;     // debug mode
  blinkState = false;
  unsigned long now = millis();
  // digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  if ( ledState ) {
    analogWrite(LED_BUILTIN, 300);
    ledState = false;
  } else {
    analogWrite(LED_BUILTIN, 0);
    digitalWrite(LED_BUILTIN, HIGH);
    ledState = true;
  }
  
  long numberOfBlink = blinks - lastBlink;
  kWhcounter = kWhcounter + numberOfBlink;

  long pulseLength = (millisBlink - millisLastBlink);
  long watt2 =  3600000 / (long)pulseLength ;
  long watt = watt2 / 10; watt = watt * 10;          //Only send tenths of watt
  
  lastBlink = blinks;
  millisLastBlink = millisBlink;

  
  if ( now - millisLastSentBlink < 4000 ) {  
    if ( printlog ) Serial.print(".");
    return;
  }

  // Inför: Max 11 kW för 16A
  millisLastSentBlink = now;
    
  if ( numberOfBlink > 1 ) Serial.print("\nBlink miss!!!!!!!!!!!!!!!!!!!!!!!!!!");
  if (printlog) {
    Serial.print("\nB:");
    Serial.print(blinks);
    Serial.print(" ms:");
    Serial.print(pulseLength);
    Serial.print(" W:");
    Serial.print(watt2);
    Serial.print(" = ");
    Serial.print(watt);
  }

  char wattChar[20] = {0};
  char pulseChar[20] = {0};
  char json[100] = {0};
  snprintf (wattChar, 19, "%ld", watt);
  snprintf (pulseChar, 19, "%ld", blinks);
  
  strcat(json,"{ \"watt\": ");
  strcat(json,wattChar);
  strcat(json,", \"pulse\": ");
  strcat(json,pulseChar);
  strcat(json," }");        //  json sample:  { "watt": 1230, "pulse": 458013 }
  
  if ( client.publish("home/sensor1/meter",json) ) {
      // Serial.print(" > Sent ok ");
  }

  detectConsumer(watt);  

  if ( lastSentHouer != hour() && now - lastSentHouerMillis > 30000 ) {  // retry max 3000 millis

    lastSentHouerMillis = now;
    long wh = blinks - hourStartBlink;

    Serial.println();
    Serial.print("Sending hour message: w/h: ");
    Serial.print(wh);

    char msg[20];
    snprintf (msg, 19, "%ld", wh);
    Serial.print(" Publish wh: ");
    Serial.print(msg);
          
    if ( client.publish("home/sensor1/wh", msg) ) {
        Serial.print(" > Sent ok. ");

        lastSentHouer = hour();
        hourStartBlink = blinks;
    } else {
        //Serial.println(" no connection retry in 30 sec ");
    }
    
  } else if (lastSentHouer != hour() ) {
    //Serial.println("Resent hour message to soon!");
  }



  if ( kWhcounter >= 1000 ) {

    kWh++;
    kWhcounter = 0;

    char msg[20];
    snprintf (msg, 19, "%ld", kWh);
    Serial.print("\nPublish kWh: ");
    Serial.print(msg);
          
    if ( client.publish("home/sensor1/kwh", msg) ) {
      Serial.println(" > Sent ok.");
    }
  }

  if ( lastSaveTime != ( minute() / 10 ) ) {
    Serial.print("\nSave to eeprom: ");
    saveBlinkValues();
    lastSaveTime = ( minute() / 10 );
  }
  
  int runtime = millis()-now;  
  if ( runtime > 100 ) {
    Serial.print("\nPuls runtime: ");
    Serial.print(runtime);
  }
}

      
void sendTurned() {

  turns++;
  unsigned long nowMill = millis();
  Serial.print("\nTurn: ");
  Serial.print(turns);      
  char msg[20];
  long timeSinsLastTurn = millis() - timeTurne ;
  timeTurne = millis();

  // $kw =          (($used_g/1000) * 4.85)  / ($time /60);
  int burner_watt = (( (float)gramTurn/1000 * 4.85 ) / ( (float)timeSinsLastTurn/1000/60/60 ) * 1000 );   // = kg*4,85 / h = w
  
  if ( burner_watt > 8000 && burner_watt < 30000 &&  burnerOn == false ) {
    if ( client.publish("home/sensor1/burnerOn","1") ) {
      Serial.print("\nBurner on sent!");
      burnerOn = true;

      burnerEndTime = burnerStartTime;            // Save last turn time to calculte burne time and pellets usage
      turnBurnerEnd = turnBurnerStart;

      burnerStartTime = now();
      turnBurnerStart = turns;

      sendBrunerBagDayConsume();

      memset(msg, 0, sizeof(msg));  // clear
      snprintf (msg, 19, "%ld", bagHome);
      client.publish("home/sensor1/baghome", msg );
    }
  }

  if ( burner_watt < 8000 &&  burnerOn == true ) { 
    Serial.print("\nBurner off!");
    sendBurnerOff();
  }
  
  storageGram = storageGram - gramTurn;
  gramToday = gramToday + gramTurn;

  Serial.print(" timeSinsLastTurn: ");
  Serial.print(timeSinsLastTurn);
  Serial.print(" burner_watt: ");
  Serial.print(burner_watt);
  
  snprintf (msg, 19, "%ld", turns);
  if ( client.publish("home/sensor1/turn", msg) ) {
    Serial.print(" > Sent ok");
  }
  
  memset(msg, 0, sizeof(msg));  // clear
  snprintf (msg, 19, "%ld", storageGram);
  if ( client.publish("home/sensor1/storage", msg) ) {
    //Serial.println(" > Sent ok");
  }
 
  memset(msg, 0, sizeof(msg));  // clear
  snprintf (msg, 19, "%ld", gramToday);
  if ( client.publish("home/sensor1/gramtoday", msg) ) {
    //Serial.println(" > Sent ok");
  }

  if ( burner_watt < 30000 ) {
    memset(msg, 0, sizeof(msg));  // clear
    snprintf (msg, 19, "%ld", burner_watt);
    client.publish("home/sensor1/burnerkw", msg);
  }

  if ( turns % 10 == 0 ) {
    Serial.print(" Save to EEPROM ");
    saveTurnValues();
  }

  if ( turns % 100 == 0 ) {
    sendCleanValues();
  }

}

void sendBrunerBagDayConsume() {

  long burnTime = burnerStartTime - burnerEndTime;
  long burnGram = (turnBurnerStart - turnBurnerEnd) * gramTurn;
  long bagDay = ( (float)burnGram / (float)burnTime ) *60*60*24/16;
  
  Serial.print("\nBURNER ON\n burnerEndTime ");
  Serial.print(burnerEndTime);
  Serial.print("\n burnerStartTime ");
  Serial.print(burnerStartTime);
  Serial.print("\n turnBurnerEnd ");
  Serial.print(turnBurnerEnd);
  Serial.print("\n burnTime ");
  Serial.print(burnTime);
  Serial.print("\n burnGram ");
  Serial.print(burnGram);
  Serial.print("\n bagDay ");
  Serial.print(bagDay);

  char msg[20];
  snprintf (msg, 19, "%ld", bagDay);
  Serial.print("\nPublish bagDay: ");
  Serial.print(msg);
  if ( client.publish("home/sensor1/bagday", msg) ) {
    Serial.println(" > Sent ok");
  }
}

void sendBurnerOff(){
  if ( client.publish("home/sensor1/burnerOn","0") ) {
    Serial.println("Burner off sent!");
    saveTurnValues();
    burnerOn = false;
    client.publish("home/sensor1/burnerkw", "0");
    sendCleanValues();
  }
}

void sendCleanValues() {
  int smallCleanKg = (( turns - smallCleanTurn ) * gramTurn ) / 1000;
  int bigCleanKg   = (( turns - bigCleanTurn ) * gramTurn ) / 1000;

  char sClean[20] = {0};
  char bClean[20] = {0};
  char json[100] = {0};
  snprintf (sClean, 19, "%ld", smallCleanKg);
  snprintf (bClean, 19, "%ld", bigCleanKg);
  
  strcat(json,"{ \"s_cleankg\": ");
  strcat(json,sClean);
  strcat(json,", \"b_cleankg\": ");
  strcat(json,bClean);
  strcat(json," }");        //  json sample:  { "s_cleankg": 1230, "b_cleankg": 458013 }
  client.publish("home/sensor1/clean",json);

  Serial.println(" Clean value sent. ");
}


void sendTemp(){
  char topic[50] = {0};
  char topic_node[]= "home/sensor1/";
    
  if ( sendTempState == 0 ) {
    printTime();
    Serial.print(" Req. temps ");
    sensors.setWaitForConversion(false);  // makes it async
    sensors.requestTemperatures(); // Send the command to get temperatures
    outTemp = 9999;
    feedTemp = 0; returnTemp = 0;
    sendTempState++;
  } else if ( sendTempState == 1 ) {
    if (!sensors.getAddress(sensorAddress, sensorCount)) {
      //Serial.print(" All done ");
      //Serial.println(i);
      sendTempState++;
      sensorCount=0;
      return;
    }
    delay(10);
    Serial.print("\n");
    Serial.print(sensorCount);
    Serial.print(":");
    // printAddress(sensorAddress);
    
    int tempCint =((float)sensors.getTempCByIndex(sensorCount))*100;
    char msg[20];
    snprintf (msg, 19, "%ld", tempCint);

    strcat(topic, topic_node);

    for (int a = 0; a < 8; a++){
      char cstr[16];
      itoa(sensorAddress[a], cstr, 16);
      strcat(topic, cstr);
    }

    Serial.print(topic);
    Serial.print("/");
    Serial.print(msg);

    if ( tempCint > -3000 && tempCint < 10000  ) {
      if ( !client.publish(topic, msg) ) {
        Serial.print(" Error! ");
      }
    }

    if ( strcmp(topic, "home/sensor1/28ff248334036") == 0 ) {
      outTemp = (float)sensors.getTempCByIndex(sensorCount);
    }

    if ( strcmp(topic, "home/sensor1/28311665005a") == 0 ) {
      feedTemp = (float)sensors.getTempCByIndex(sensorCount);
    }
    if ( strcmp(topic, "home/sensor1/28bd8334500a5") == 0 ) {
      returnTemp = (float)sensors.getTempCByIndex(sensorCount);
    }
    if ( feedTemp > 0 && returnTemp > 0 ) {
      char msg[20];
      snprintf (msg, 19, "%ld", int((feedTemp - returnTemp)*100) );
      client.publish("home/sensor1/feedreturn", msg);
      feedTemp = 0; returnTemp = 0;
    }

        
    sensorCount++;    
  } else if ( sendTempState == 2 ) {
    //Send data to temperatur.nu then rigth sensor is selected. 
    if ( outTemp != 9999 ) {
      sendToTemperaturDotNu( outTemp );
      outTemp = 9999;
    }
    sendTempState++;
  }
}

void sendToTemperaturDotNu( float celsius ){

  if ( celsius > 70 ) return;
  if ( celsius < -50 ) return;
  
  HTTPClient http;
  
  Serial.print(" Send temp ");

  char url[150] = {0};
  char temp[10] = {0};
  strcat(url, "http://www.temperatur.nu/rapportera.php?hash=");
  strcat(url, TEMPERATUR_NU_HASH ); 
  strcat(url, "&t=" );
  
  dtostrf(celsius, 6, 2, temp);

  while ( temp[0] == ' ' ) {
    for (int i = 0; i < 10; i++){
      temp[i] = temp[i+1];
    }
  }
  strcat(url, temp );
  Serial.println(url);

  http.begin(url);
  delay(10);
  int httpCode = http.GET();
  delay(10);
  if (httpCode > 0) { 
    String payload = http.getString();
    Serial.print(payload);
  }
  http.end();
}

void printTemp(){

  Serial.print("\nReq. temps Sensor ");
  // sensors.requestTemperatures(); // Send the command to get temperatures
  int i = 0;
  while ( i < 20 ) {
    if (!sensors.getAddress(sensorAddress, i)) {
      Serial.print("\nAll done ");
      Serial.println(i);
      break;
    }

    int tempCint =((float)sensors.getTempCByIndex(i))*100;
    char msg[20];
    snprintf (msg, 19, "%ld", tempCint);

    char topic[50] = {0};
    char topic_node[]= "home/sensor1/";
    strcat(topic, topic_node);

    for (int a = 0; a < 8; a++){
      char cstr[16];
      itoa(sensorAddress[a], cstr, 16);
      strcat(topic, cstr);
    }
    Serial.print("\n");
    Serial.print(topic);
    Serial.print(" / ");
    Serial.print(msg);

    // sensors.setResolution(sensorAddress, 11);       //to change resolution

    Serial.print(" ");
    Serial.print(sensors.getTempCByIndex(i));
    Serial.print(" res: ");
    Serial.print(sensors.getResolution(sensorAddress));
    i++;
  }
 }

void saveAllValues() {
  saveBlinkValues();
  saveTurnValues();
  Serial.println("Saved to eeprom");
}

void saveBlinkValues() {    
  save_eeprom (0*4, blinks);
  save_eeprom (1*4, kWhcounter);
  save_eeprom (2*4, kWh);

  save_eeprom (3*4,lastSentHouer);
  save_eeprom (4*4,hourStartBlink);
  save_eeprom (5*4,lastSentDay);
  save_eeprom (6*4,dayStartBlink);
  EEPROM.commit();
}

void saveTurnValues(){
  save_eeprom (10*4, turns);
  save_eeprom (12*4, gramToday);
  save_eeprom (13*4, storageGram);
  save_eeprom (14*4, bagHome);
  save_eeprom (15*4, burnerStartTime);
  save_eeprom (16*4, burnerEndTime);
  save_eeprom (17*4, turnBurnerStart);
  save_eeprom (18*4, turnBurnerEnd);
  save_eeprom (19*4, sentKgDay);
  EEPROM.write(20*4, burnerOn);

  save_eeprom (21*4, smallCleanTurn);  
  save_eeprom (22*4, bigCleanTurn);  
  
  EEPROM.commit();
}

void loadAllValues(){
  blinks          = load_eeprom(0*4);
  kWhcounter      = load_eeprom(1*4);
  kWh             = load_eeprom(2*4);
  lastSentHouer   = load_eeprom(3*4);
  hourStartBlink  = load_eeprom(4*4);
  lastSentDay     = load_eeprom(5*4);
  dayStartBlink   = load_eeprom(6*4);
  lastBlink = blinks;
    
  turns       = load_eeprom(10*4);
  gramToday   = load_eeprom(12*4);
  storageGram = load_eeprom(13*4);
  bagHome     = load_eeprom(14*4);
  burnerStartTime = load_eeprom(15*4);
  burnerEndTime   = load_eeprom(16*4);
  turnBurnerStart = load_eeprom(17*4);
  turnBurnerEnd   = load_eeprom(18*4);
  sentKgDay       = load_eeprom(19*4);
  burnerOn        = EEPROM.read(20*4);
  smallCleanTurn  = load_eeprom (21*4);  
  bigCleanTurn    = load_eeprom (22*4); 

}

// Interrupt functions
void blinkInterrupt() {
  blinks++;
  blinkState = true;
  millisBlink = millis();
  detachInterrupt(digitalPinToInterrupt(blinkPin));
  pulse.attach_ms(200,startPulseCount);
}
void startPulseCount() {
  pulse.detach();
  attachInterrupt(digitalPinToInterrupt(blinkPin), blinkInterrupt, RISING);
}

void ledBlink(){
  digitalWrite( led3 , !digitalRead(led3) );
}


void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("Booting");

  int i = 0;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    i++;
    if ( i > 15 ) ESP.restart();
  }

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);
  ArduinoOTA.setHostname("blink");
  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");
  ArduinoOTA.onStart([]() {
    Serial.println("\nOTA update start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nUpdate End");
    saveAllValues();                   //Save to eeprom
    delay(1000);
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf(" %u%%\r ", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("Wifi OTA Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // init the MQTT connection
  client.setServer(MQTT_SERVER_IP, MQTT_SERVER_PORT);
  client.setCallback(callback);

  // Start the server
  server.begin();
  Serial.println("Web Server started");
  
  Serial.println("Starting UDP");
  Udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(Udp.localPort());
  Serial.println("waiting for sync");
  setSyncProvider(getNtpTime);
  setSyncInterval(300*10);

  i = 0;
  Serial.print("\n\nConnect to MQTT");
  if (!client.connected()) {
    reconnect();
    delay(1000);
    Serial.print(".");
    i++;
    if ( i > 60 ) ESP.restart();
  }

  printTime();
  if ( year() < 2017 ) {
    Serial.print("\n\nGot no time! Rebooting!");
    delay(1000*10);
    ESP.restart();
  }

  sensors.begin();
  Serial.print("\nLocating oneWire devices, ");
  Serial.print(sensors.getDeviceCount(), DEC);
  Serial.println(" devices found.");
  
  // report parasite power requirements
  Serial.print("Parasite power is: "); 
  if (sensors.isParasitePowerMode()) Serial.println("ON");
  else Serial.println("OFF");

 
  EEPROM.begin(100);

  loadAllValues();    //From EEPROM
 
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(led3, OUTPUT);

  pinMode(turnPin, INPUT_PULLUP);
  pinMode(blinkPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(blinkPin), blinkInterrupt, RISING);

  Serial.print("\nBlinks:");
  Serial.print(blinks);
  Serial.print("\nkWhcounter:");
  Serial.print(kWhcounter);
  Serial.print("\nkWh:");
  Serial.print(kWh);
  Serial.print("\nlastSentHouer:");
  Serial.print(lastSentHouer);
  Serial.print("\nhourStartBlink:");
  Serial.print(hourStartBlink);
  Serial.print("\nlastSentDay:");
  Serial.print(lastSentDay);
  Serial.print("\ndayStartBlink:");
  Serial.print(dayStartBlink);

  printTurnValues();
  Serial.println();
  
  if ( burnerOn ) {
    client.publish("home/sensor1/burnerOn","1");
  } else {
    client.publish("home/sensor1/burnerOn","0");
  }
  
  led3Blink.attach(1, ledBlink);
  
}


void loop() {
  ArduinoOTA.handle();

  unsigned long now = millis();

  // MQTT connect check
  if ( !client.connected() ) reconnect();
  client.loop();
  if ( blinkState ) sendBlink();              // Pulse Interrupt 
  
  webserver();

  if ( now - liveTime > 60000 ) {
    sendTemp();    
    if ( sendTempState == 3 ) {
      liveTime = now;
      sendTempState = 0;
    } else {
      liveTime = liveTime + 2000;
    }
  }

  if ( now - timeTurne > stopTimeDelay * 60 * 1000 && burnerOn == true ){
    sendBurnerOff();
  }
 
  byte turnStateRead = digitalRead(turnPin);
  if ( turnStateRead == HIGH && turnStateTime == 0 ) {
    turnStateTime = millis();
  } 
  if (turnStateTime > 0 ) turnHighTime = millis() - turnStateTime;
  if ( trunState == LOW && turnStateRead == HIGH && turnHighTime > 1000 ) {
    trunState = HIGH;
    sendTurned();
  }
  if ( trunState == HIGH && turnStateRead == LOW && turnHighTime > 1000 ) {
    turnStateTime = 0;
    trunState = LOW;
    //Serial.print("\nLOW High time: ");
    //Serial.print(turnHighTime);
  }
  if ( trunState == LOW && turnStateRead == LOW && turnHighTime > 1000 ) {
    turnStateTime = 0;
  }
  
  if ( lastSentDay != day() ) sendDayMessages();
}



