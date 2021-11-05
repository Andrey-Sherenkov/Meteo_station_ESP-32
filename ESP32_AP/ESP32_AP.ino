/* test
 *  
 *  Create a WiFi access point and provide a web server on it.
 *  ROM byte/%    RAM byte/%
 *  797630 /60%   43552 /13%
*/
#define httpdebug
#define debug
#define i2c_scan
#define serialdata
#define timerenable
#define timerliteenable
#define dnsenable

#include "BMP280ESP.h"
#include "Wire.h"
#include <WiFi.h>
#include <WebServer.h>
#include "FS.h"
#include "SPIFFS.h"
#ifdef dnsenable
  #include <DNSServer.h>
#endif

#define validselect 0x3E
#define analogpin   34     //GPIO34
#define zeroA0 4
#define meteobyuamount 2

/* Set these to your desired credentials. */
const char *ssid = "MeteoStation32";
const char *password = "1234567890";

File fsUploadFile;

#ifdef dnsenable
  const byte DNS_PORT = 53;
  IPAddress apIP(192, 168, 5, 1);
  DNSServer dnsServer;
#endif
//WiFiServer(IPAddress addr, uint16_t port);
WebServer server(80);

/*ESP8266 MAXSIZE RAM copy obgect 3800!!!*/
const uint16_t buflen = 300;

uint32_t longstep = 10000; //300point=50min//(3600000/buflen);   // tour 60min/buflen=3600000/320
uint16_t faststep = 1000 ; //1s//(300000/buflen);    // 5min/buflen=300000/320

//bool dataful=0, longful=0, enableget=0,bmperror=0, buffready[meteobyuamount], bufffirst[meteobyuamount];
//int16_t datavect=0, longvect=0,termikval=0,
int16_t Vbuff=0;
int32_t fastbuff[meteobyuamount], longbuffT=0, longbuffV=0, longbuffcountT=0, longbuffcountV=0;
uint32_t looptime=0, longtime=0, testtime=0;
//String inputstr="",outstr="";

//bool size = uint8_t
struct  TABLDATA{
  uint16_t version     = 11;               //0
  uint16_t packlen ;                       //1
  uint16_t headlen     = 30;               //2
  uint16_t arraylen    = buflen ;           //3
  uint16_t mbamount    = meteobyuamount ;   //4
  int16_t  datavect    = 0 ;               //5
  int16_t  longvect    = 0 ;               //6
  uint16_t ready       = 0 ; //0-ready-longT;1-ready-longV;2-ready-realV;3-ready-realT;4-ready-mb1T;5-ready-mb2T;;;;15-ready-mb12T //7
  uint16_t first       = 0 ;               //8
  uint8_t  datafull    = 0 ; //0-full-real;1-full-long;;;7          //9
  uint8_t  status      = 0 ; //0-bmpInitError;1-bmpReadError;;7-othererror  //9
  int16_t  mbTshift[12]    ;                 //10-11-12-13-14-15-16-17-18-19-20-21
  uint16_t resserved22 = 22;
  uint16_t resserved23 = 23;
  uint16_t resserved24 = 24;
  uint16_t resserved25 = 25;
  uint16_t resserved26 = 26;
  uint16_t resserved27 = 27;
  uint16_t resserved28 = 28;
  uint16_t resserved29 = 29;
  int16_t  longT[buflen] ;
  uint16_t longV[buflen] ;
  uint16_t realV[buflen] ;
  int16_t  realT[buflen] ;
  #if meteobyuamount > 0
  int16_t  mbT[meteobyuamount][buflen]  ;
  #endif
};
TABLDATA bufdata;

BMP280 bmp;

struct  filtrdata{
  uint16_t num;
  int16_t val;
};



void setup() {
  pinMode(2, OUTPUT);
  digitalWrite(2,1);
  bufdata.packlen = bufdata.headlen + (bufdata.arraylen * (bufdata.mbamount + 4));
  //bufdata.first   = 0xFFF0;
  //0-ready-longT;1-ready-longV;2-ready-realV;3-ready-realT;4-ready-mb1T;5-ready-mb2T;;;;15-ready-mb12T
  writedatabuf();       //debug

  #ifdef dnsenable
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(ssid,password);
  #else   
    //WiFi.setOutputPower(20.5); //0Db=76mA;20Db=77mA//пики больше 90мА//реальные пики ~350мА
    //WiFi.softAPConfig(local_ip, gateway, subnet);//работает!
    //softAP(const char* ssid, const char* passphrase = NULL, int channel = 1, int ssid_hidden = 0, int max_connection = 4);
    /* You can remove the password parameter if you want the AP to be open. */
    //WiFi.softAP(ssid, password,1,0,8);
    WiFi.softAP(ssid,password);
  #endif
	 delay(200);
	 Serial.begin(115200);
  Serial.print(F("\nIP="));
  IPAddress myIP = WiFi.softAPIP();
  Serial.println(myIP);

  if(!SPIFFS.begin()){
    Serial.print("noinitFS");
  }
  #ifdef debug
    File dir = SPIFFS.open("/");
    File file = dir.openNextFile();
    while(file){
     Serial.print(file.name());
     Serial.print("\t");
        if(file.isDirectory()){
            Serial.print("dir\n");
           /* if(levels){
                listDir(fs, file.name(), levels -1);
            }*/
        } else {
            Serial.print(file.size());
            Serial.print(" byte\n");
        }
        file = dir.openNextFile();
    }
    Serial.printf("\n");
	   Serial.println(F("\nConfiguring access point..."));
	 #endif
	
 	#ifdef serialdata
  		myIP = WiFi.softAPIP();
  		Serial.print(F("AP IP address: "));
  		Serial.println(myIP);
  		Serial.print(F("AP mac address: "));
  		Serial.println(WiFi.softAPmacAddress());
  		Serial.print("Open http://");
  		Serial.println(myIP);
 	#endif
 	//if (MDNS.begin("meteo")) {
  //  Serial.println("MDNS responder started");
  //}

  //called when the url is not defined here
  //use it to load content from SPIFFS
  server.onNotFound([](){
    if(!handleFileRead(server.uri()))
      server.send(404, "text/plain", "FileNotFound");
  });
  //list directory
  server.on("/list", HTTP_GET, handleFileList);
  server.on("/meteoconf.html", [](){
    server.send(200, "text/html", meteoconf());
  });
  server.on("/meteo-set", [](){
    server.send(200, "text/html", meteo_set());
  });

  server.on("/data.struct", [](){
    #ifdef httpdebug
      testtime=millis();
    #endif
    //void WebServer::sendContent(const char* content, size_t contentLength) {}
    server.sendContent((const char*) &bufdata, sizeof(bufdata));
    #ifdef httpdebug
      Serial.print("time send=");
      Serial.print(millis()-testtime);
      Serial.println(" ms\tstructdata");      //2-3ms
    #endif
  });

  server.begin();
  #ifdef debug
    Serial.println(F("HTTP server started"));
  #endif

  #ifdef dnsenable
    // modify TTL associated  with the domain name (in seconds) default is 60 seconds
    dnsServer.setTTL(300);
    // set which return code will be used for all other domains (e.g. sending
    // ServerFailure instead of NonExistentDomain will reduce number of queries sent by clients)
    // default is DNSReplyCode::NonExistentDomain
    dnsServer.setErrorReplyCode(DNSReplyCode::ServerFailure);
    // start DNS server for a specific domain name
    dnsServer.start(DNS_PORT, "meteo.my", apIP);
  #endif
  
  //Wire.begin(2,14);
  //Wire.begin(4,5);    //sda,scl
  //21,22
  Wire.begin();
  if(!bmp.begin()){
    Serial.println(F("BMP init failed!"));
    bufdata.status |= 1 ;                     //0-bmperror
    #ifdef i2c_scan
      byte error, address;
      int nDevices;
      Serial.println("Scanning...");
      nDevices = 0;
      for(address = 1; address < 127; address++ )
      {
        // The i2c_scanner uses the return value of the Write.endTransmisstion to see if a device did acknowledge to the address.
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
    double T0,P0;
    #ifdef debug
    Serial.println(F("BMP init success!"));
    #endif
    bmp.setOversamplingP(1);  //0=off;1=1;2=2;3=4;4=8;5,6,7=16
    bmp.setOversamplingT(7);  //0=off;1=1;2=2;3=4;4=8;5,6,7=16
    bmp.setFilter(2);         //0=1t;1=8t;2=18t
    bmp.setMode(3);           //0=sleep;1=2=force;3=normal
    bmp.setStandby(1);        //0=0,5ms;1=62,5ms;2=125ms;3=250ms;4=500ms;5=1s;6=2s;7=4s
    while(bmp.startMeasurment() == 0){ ;}
    bmp.getTemperatureAndPressure(T0,P0);

    looptime = millis();
    while(millis()- looptime < 3000){;} //delay to init IR filter
    
    bmp.getTemperatureAndPressure(T0,P0);
    
    bufdata.realT[0]=bufdata.realT[1]=bufdata.longT[0]=bufdata.longT[1]=int(T0*100);
    //0-ready-longT;1-ready-longV;2-ready-realV;3-ready-realT;4-ready-mb1T;5-ready-mb2T;;;;15-ready-mb12T
    bufdata.ready   = 1 | 1<<3  ; 
    #ifdef debug
      Serial.print(F("press = "));Serial.print(P0);
      Serial.print(F("\t\tTemp = "));Serial.println(T0);
    #endif
  }
  bufdata.ready   |= 1<<1 | 1<<2 ; 
  bufdata.realV[0]=bufdata.longV[0]=analogRead(analogpin);
}

void loop() {
  double T,P;
	 server.handleClient();
  #ifdef dnsenable
    dnsServer.processNextRequest();
  #endif

  if(millis() - looptime > faststep){
    looptime=millis();
    digitalWrite(2,!digitalRead(2));     //internal led blink
    char result;
    uint16_t tempvect = bufdata.datavect + 1;
    if(tempvect >= buflen){
      bufdata.datafull |= 1;
      tempvect  = 0;
    }
    if((bufdata.status & 1)==0){    //bmperror
      result = bmp.getTemperatureAndPressure(T,P);
      if(result!=0){
        #ifdef serialdata
          //double A = bmp.altitude(P,P0);
          Serial.print(F("Time = "));Serial.print(millis()); Serial.print(F(" ms\t"));
          Serial.print(F("T = "));Serial.print(T,2); Serial.print(F(" degC\t\t"));
          Serial.print(F("P = "));Serial.print(P,2); Serial.print(F(" Pa\t"));
          //Serial.print(F("A = \t"));Serial.print(A,2); Serial.print(F(" m"));
        #endif
        bufdata.realT[tempvect] = T*100;
        longbuffT += T*100;
        longbuffcountT++;
      }else{
        //отвалился датчик
        bufdata.realT[tempvect]=bufdata.realT[bufdata.datavect];
        bufdata.status |= 1<<1 ;  //error bmp280
      }
      #if meteobyuamount > 0
        for(byte i=0;i<meteobyuamount;i++){
          //Serial.print(F("add to graf"));Serial.println(fastbuff[i]);
          if(bufdata.ready & (1<<(i+4)) ){
            #ifdef debug
              Serial.print(F("ready MB"));
              Serial.print(i);
              Serial.print(F(" = "));
              Serial.print(fastbuff[i]);
            #endif
            if((bufdata.first & (1<<(i+4)) ) == 0){
              #ifdef debug
                Serial.print(F("first MB"));
                Serial.print(i);
              #endif
              bufdata.first |= (1<<(i+4));
              if(bufdata.datafull & 1 ){                   //0-full-real;1-full-long;;;7
                for(int p=0;p<bufdata.arraylen;p++){
                  bufdata.mbT[i][p]=fastbuff[i];
                }
              }else{
                for(int p=0;p<tempvect;p++){
                  bufdata.mbT[i][p]=fastbuff[i];
                }
              }
            }
            bufdata.mbT[i][tempvect]=fastbuff[i];
            bufdata.mbTshift[i]++;
            if(bufdata.mbTshift[i] > bufdata.arraylen){
              bufdata.first ^= (1<<(i+4));
              bufdata.ready ^= (1<<(i+4));
            }
          }
        }
      #endif
    }
    //bufdata.realV[tempvect] = Vbuff;
    bufdata.realV[tempvect]   = analogRead(analogpin);
    longbuffV += bufdata.realV[tempvect];
    longbuffcountV++;
    bufdata.datavect          = tempvect;            //
    #ifdef serialdata
      //Serial.print(F("\tA0 = \t"));Serial.print(float(data[1][datavect]),2);
      Serial.print(F("\tV = "));Serial.print(float(bufdata.realV[bufdata.datavect])*0.027,2); Serial.println(F(" m/s"));
    #endif

    if(millis() - longtime > longstep && (bufdata.datavect > 0 || (bufdata.datafull & 1) == 1)){
      longtime=millis();
      bufdata.longvect++;
      if(bufdata.longvect>=buflen){
          bufdata.datafull |= 1<<1; //0-full-real;1-full-long;;;7
          bufdata.longvect  = 0;
      }
      if(longbuffcountT > 0){ bufdata.longT[bufdata.longvect] = longbuffT / longbuffcountT;}else{bufdata.longT[bufdata.longvect] = T*100;}
      if(longbuffcountV > 0){ bufdata.longV[bufdata.longvect] = longbuffV / longbuffcountV;}else{bufdata.longV[bufdata.longvect] = analogRead(analogpin);}
      longbuffcountT = 0;
      longbuffT      = 0;
      longbuffcountV = 0;
      longbuffV      = 0;
    }
    
  }
}

String getContentType(String filename){
  if(server.hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

bool handleFileRead(String path){
  Serial.println("handleFileRead: " + path);
  if(path.endsWith("/")) path += "index.htm";
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)){
    if(SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    Serial.println("sent: " + sent);
    return true;
  }
  return false;
}

//format bytes
String formatBytes(size_t bytes){
  if (bytes < 1024){
    return String(bytes)+"B";
  } else if(bytes < (1024 * 1024)){
    return String(bytes/1024.0)+"KB";
  } else if(bytes < (1024 * 1024 * 1024)){
    return String(bytes/1024.0/1024.0)+"MB";
  } else {
    return String(bytes/1024.0/1024.0/1024.0)+"GB";
  }
}

void handleFileList() {
  if(!server.hasArg("dir")) {server.send(500, "text/plain", "BAD ARGS"); return;}

  String path = server.arg("dir");
  Serial.println("handleFileList: " + path);
  File dir = SPIFFS.open(path);
  path = String();

  String output = "[ DIR:\t\tFILE:\n";
  //list to serial good 
  File file = dir.openNextFile();
  while(file){
        if(file.isDirectory()){
            Serial.print("  DIR : ");
            Serial.println(file.name());
            output += file.name();
            output += "\t\t dir";
           /* if(levels){
                listDir(fs, file.name(), levels -1);
            }*/
        } else {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("\tSIZE: ");
            Serial.println(file.size());
            output += file.name();
            output += "\t\t";
            output += file.size();
            output += "\n";
        }
        file = dir.openNextFile();
  }
  
  output += "]";
  server.send(200, "text/json", output);
}

String meteoconf(){
  for (uint8_t i=0; i<server.args(); i++){
    if(server.argName(i).startsWith("lenreal")){
      int32_t cn=server.arg(i).toInt();
      if(faststep != (cn)){
  		    faststep = (cn);
        bufdata.datafull &= 0xFE;//B11111110; //0-full-real;1-full-long;;;7
        bufdata.realT[0] = bufdata.realT[bufdata.datavect];
        bufdata.realV[0] = bufdata.realV[bufdata.datavect];
        bufdata.datavect  = 0;
      }
    }
    if(server.argName(i).startsWith("lenlong")){
      int32_t cn=server.arg(i).toInt();
      if(longstep != (cn)){
        longstep = (cn);
        bufdata.datafull &= 0xFD;//B11111101; //0-full-real;1-full-long;;;7
        bufdata.longT[0]  = bufdata.longT[bufdata.longvect];
        bufdata.longV[0]  = bufdata.longV[bufdata.longvect];
        bufdata.longvect  = 0;
        longtime = millis();
      }
    }
    if(server.argName(i).startsWith("reset")){
      if( server.arg(i) == "reset" ){
        faststep = 1000     ;
        longstep = 10000    ;
        bufdata.datafull = 0;
        bufdata.realT[0] = bufdata.realT[bufdata.datavect];
        bufdata.realV[0] = bufdata.realV[bufdata.datavect];
        bufdata.longT[0] = bufdata.longT[bufdata.longvect];
        bufdata.longV[0] = bufdata.longV[bufdata.longvect];
        bufdata.datavect = 0;
        bufdata.longvect = 0;
        longtime = millis() ;
      }
    }
  }

  String message = "<html>";
  message += "<body><head><meta charset=\"utf-8\"> </head><form>";
  message += "<p>step";
  message += "<input class=\"long\" type=\"number\" min=\"0\" name=\"lenreal";
  message += "\" value=\"";
  message += String(uint32_t(faststep));
  message += "\">ms\tlen=";
  message += String(uint32_t(faststep) * bufdata.arraylen / 1000);
  message += "s</p>";
  message += "<p>step";
  message += "<input class=\"long\" type=\"number\" min=\"0\" name=\"lenlong";
  message += "\" value=\"";
  message += String(uint32_t(longstep));
  message += "\">ms\tlen=";
  message += String(uint32_t(longstep) * bufdata.arraylen / 1000);
  message += "s</p>";
  message += "<p><input type=\"submit\" value=\"Set\"></p>";
  message += "</form>";
  message += "<input id=\"buttonreset\" onclick=\"sendcom();\" value=\"RESET\" type=\"button\" >";
  message += "<script type=\"text/javascript\">  ";
  message += "var xhs = new XMLHttpRequest();\n";
  message += "xhs.onload = function (oEvent) {\n";
  message += " if (xhs.readyState == 4 && xhs.status == 200){\n";
  message += "  console.log(\"reqwest\");\n";
  message += "  console.log(xhs.response);\n";
  message += "}\n; }\n;";
  message += "function sendcom(){\n";
  message += "  xhs.open(\"GET\", \"meteoconf.html?reset=reset\", true);\n";
  message += "  xhs.send();}\n";
  message += "</script>\n";
  message += "</body></html>\n";
  //Serial.println(message);
  return message;
}

String meteo_set(){
  for (uint8_t i=0; i<server.args(); i++){
    if(server.argName(i).startsWith("MBT")){
      int32_t cn=server.arg(i).toInt();
      String number = server.argName(i).substring(3,server.argName(i).length());
      int32_t n = number.toInt();
      if(cn>=-10000 && cn<=10000 && n>=0 && n<meteobyuamount){
        fastbuff[n] = cn;
        bufdata.ready |= (1<<(n+4));
        bufdata.mbTshift[n] = 0;
      }
    }
    if(server.argName(i).startsWith("MBV")){
      int32_t cn=server.arg(i).toInt();
      if(cn>=0 && cn<=10000){
        Vbuff = cn;
      }
    }
  }
  return "Ok";
}

void writedatabuf(){
  for(int ixt=0; ixt < bufdata.arraylen;ixt++){
    bufdata.longT[ixt] = ixt;
    bufdata.longV[ixt] = ixt;
    bufdata.realV[ixt] = ixt;
    bufdata.realT[ixt] = ixt;
    #if meteobyuamount > 0
    for(byte n=0; n < bufdata.mbamount ; n++){
      bufdata.mbT[n][ixt] = ixt ;
    }
    #endif
  }
}
