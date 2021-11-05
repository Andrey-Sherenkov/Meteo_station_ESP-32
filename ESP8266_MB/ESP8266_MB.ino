/* Create a WiFi access point and provide a web server on it.
MB 1 2732*/
//#define httpdebug
//#define debug
//#define i2c_scan
//#define serialdata
#include "BMP280ESP.h"
#include "Wire.h"
//#include <WiFiClient.h> 
#include <ESP8266WiFi.h>

#define zeroA0 4
#define meteobyuamount 1
#define byunum 0

/* Set these to your desired credentials. */
const char *ssid = "MeteoStation32";
const char *password = "1234567890";
//const char* host = "data.sparkfun.com";
char host[] = "192.168.5.1";

bool bmperror=0;
uint16_t faststep = 1000;    // 5min/buflen=300000/320
double P0;
unsigned long looptime=0;
IPAddress lip;

BMP280 bmp;

struct  filtrdata{
  uint16_t num;
  int16_t val;
};


void setup() {
  Serial.begin(115200);
	 //delay(100);
  //Wire.begin(2,14);
  Wire.begin(4,5);    //sda,scl
  if(!bmp.begin()){
    Serial.println(F("BMP init failed!"));
    bmperror=1;
    #ifdef i2c_scan
      byte error, address;
      int nDevices;
      Serial.println("Scanning...");
      nDevices = 0;
      for(address = 1; address < 127; address++ )
      {
        // The i2c_scanner uses the return value of
        // the Write.endTransmisstion to see if
        // a device did acknowledge to the address.
        Wire.beginTransmission(address);
        error = Wire.endTransmission();
        if (error == 0)
        {
          Serial.print("I2C device found at address 0x");
          if (address<16)
            Serial.print("0");
          Serial.print(address,HEX);
          Serial.println("  !");
          nDevices++;
        }
        else if (error==4)
        {
          Serial.print("Unknow error at address 0x");
          if (address<16)
            Serial.print("0");
          Serial.println(address,HEX);
        }    
      }
      if (nDevices == 0)
        Serial.println("No I2C devices found\n");
      else
        Serial.println("done\n");
   #endif
  } else {
    double T0;
    #ifdef debug
    Serial.println(F("BMP init success!"));
    #endif
    bmp.setOversamplingP(1);  //0=off;1=1;2=2;3=4;4=8;5,6,7=16
    bmp.setOversamplingT(7);  //0=off;1=1;2=2;3=4;4=8;5,6,7=16
    bmp.setFilter(2);         //0=1t;1=8t;2=18t
    bmp.setMode(3);           //0=sleep;1=2=force;3=normal
    bmp.setStandby(1);        //0=0,5ms;1=62,5ms;2=125ms;3=250ms;4=500ms;5=1s;6=2s;7=4s
    while(bmp.startMeasurment() ==0){ ;}
    bmp.getTemperatureAndPressure(T0,P0);

    
    while(millis()-looptime<3000){
     /* while(Serial.available() > 0) {
        if(addchar(Serial.read())){
          parcestr(outstr);           // отработка радиоканала
        }
      }*/
    }
  }
  
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  looptime=millis();
  /* Explicitly set the ESP8266 to be a WiFi-client, otherwise, it by default,
     would try to act as both a client and an access-point and could cause
     network-issues with your other WiFi-devices on your WiFi-network. */
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  //WiFi.begin(ssid);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(millis() - looptime); //ms:4356,502,2502,4502,2502

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: local: ");
  lip=WiFi.localIP();
  Serial.println(lip);
  lip[3]=1;
  Serial.print("remote: ");
  Serial.println(lip);
}

void loop() {
  double T,P;
  if(millis() - looptime > faststep){
    looptime=millis();
    char result;
    if(bmperror==0){
      result = bmp.getTemperatureAndPressure(T,P);
      
      if(result!=0)
      {
        #ifdef serialdata
          double A = bmp.altitude(P,P0);
          Serial.print(F("T = \t"));Serial.print(T,2); Serial.print(F(" degC\t"));
          Serial.print(F("P = \t"));Serial.print(P,2); Serial.print(F(" Pa\t"));
          Serial.print(F("A = \t"));Serial.print(A,2); Serial.print(F(" m"));
        #endif
        if(WiFi.status() != WL_CONNECTED) {
          Serial.println("not-conect\nREBOOT");
          delay(200);
          ESP.restart();
        }
        // Use WiFiClient class to create TCP connections
        WiFiClient client;
        const int httpPort = 80;
        if (!client.connect(lip, httpPort)) {
          Serial.println("connection failed");
          return;
        }

        String url = "/meteo-set";
        url += "?MBT";
        url += byunum;
        url += "=";
        url += int(T*100);
      /*
        // We now create a URI for the request
        String url = "/input/";
        url += streamId;
        url += "?private_key=";
        url += privateKey;
        url += "&value=";
        url += value;
      */
        //Serial.print("Requesting URL: ");
        //Serial.println(url);
      
        // This will send the request to the server
        client.print(String("GET ") + url + " HTTP/1.1\r\n" + "Host: " + host + "\r\n" + "Connection: close\r\n\r\n");
        
        unsigned long timeout = millis();
        while (client.available() == 0) {
          if (millis() - timeout > 500) {
            Serial.println(">>> Client Timeout !");
            client.stop();
            return;
          }
        }
        ///millis= 118290 req time= 6  loop time= 13
        Serial.print("millis= ");
        Serial.print(millis());
        Serial.print("\treq time= ");
        Serial.print(millis() - timeout );
        Serial.print("\t\tloop time= ");
        Serial.println(millis() - looptime );
        // Read all the lines of the reply from server and print them to Serial
        //while (client.available()) {
        //  String line = client.readStringUntil('\r');
        //  Serial.print(line);
        //}
        
      }
    }
    
  }
}
