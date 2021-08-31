/* bad
 *  
 *  Create a WiFi access point and provide a web server on it.
MB 1 2732*/
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
/* 
*/
#define validselect 0x3E
#define zeroA0 4
#define meteobyuamount 1

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

/*MAXSIZE RAM obgect 3800!!!*/
const uint16_t buflen = 320;
const uint8_t minscale = 20;
uint32_t longstep = (3600000/buflen);   // tour/buflen=3600000/320
uint16_t faststep = (300000/buflen);    // 5min/buflen=300000/320
double P0;
bool dataful=0, longful=0, enableget=0,bmperror=0, buffready[meteobyuamount], bufffirst[meteobyuamount];
int16_t datavect=0, longvect=0,termikval=0, Vbuff=0;
int32_t data[2+meteobyuamount][buflen], datal[2][buflen],fastbuff[meteobyuamount];
uint32_t looptime=0,longtime=0,testtime=0;
String inputstr="",outstr="";


BMP280 bmp;

struct  filtrdata{
  uint16_t num;
  int16_t val;
};



void setup() {
  
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
	delay(1000);
	Serial.begin(115200);
  Serial.print(F("\nIP="));
  IPAddress myIP = WiFi.softAPIP();
  Serial.println(myIP);
	//Serial.println();
  //if(!SPIFFS.begin()){
  if(!SPIFFS.begin()){
    Serial.print("noinitFS");
  }
  #ifdef debug
    File dir = SPIFFS.open("/");
    while (dir.openNextFile()) {    
      String fileName = dir.name();
      size_t fileSize = dir.size();
      Serial.printf("FS File: %s, size: %s\n", fileName.c_str(), formatBytes(fileSize).c_str());
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
	//if (MDNS.begin("meteo.byu")) {
  //  Serial.println("MDNS responder started");
  //}

  //list directory
  server.on("/list", HTTP_GET, handleFileList);

  //called when the url is not defined here
  //use it to load content from SPIFFS
  server.onNotFound([](){
    if(!handleFileRead(server.uri()))
      server.send(404, "text/plain", "FileNotFound");
  });
  server.on("/meteoconf.html", [](){
    server.send(200, "text/html", meteoconf());
  });
  server.on("/meteo-set", [](){
    server.send(200, "text/html", meteo_set());
  });
  
  #ifdef timerenable
  server.on("/timer.html", [](){
    server.send(200, "text/html", timerconf());
  });
  #endif
  #ifdef timerliteenable
  server.on("/timerlite.html", [](){
    server.send(200, "text/html", timerliteconf());
  });
  #endif

  server.on("/real.txt", [](){
    #ifdef httpdebug
      testtime=millis();
    #endif
    server.send(200, "text/html", getrealraw());
    #ifdef httpdebug
      Serial.print("time send=");
      Serial.print(millis()-testtime);
      Serial.println("\treal.txt");
    #endif
  });
  server.on("/long.txt", [](){
    #ifdef httpdebug
      testtime=millis();
    #endif
    server.send(200, "text/html", getlongraw());
    #ifdef httpdebug
      Serial.print("time send=");
      Serial.print(millis()-testtime);
      Serial.println("\tlong.txt");
    #endif
  });
  server.on("/fullraw.txt", [](){
    #ifdef httpdebug
      testtime=millis();
    #endif
    server.send(200, "text/html", getfullraw());
    #ifdef httpdebug
      Serial.print("time send=");
      Serial.print(millis()-testtime);  //no-passwrd  50-270ms; max=1,7-5s
      Serial.println("\tfullraw.txt");
    #endif
  });
  server.on("/data.bin", [](){
    #ifdef httpdebug
      testtime=millis();
    #endif
    server.send(200, "application/octet-stream", getdatabin());
    #ifdef httpdebug
      Serial.print("time send=");
      Serial.print(millis()-testtime);
      Serial.println("\tdata.bin");
    #endif
  });

  
  server.begin();
  #ifdef debug
    Serial.println(F("HTTP server started"));
  #endif

  #ifdef dnsenable
    // modify TTL associated  with the domain name (in seconds)
    // default is 60 seconds
    dnsServer.setTTL(300);
    // set which return code will be used for all other domains (e.g. sending
    // ServerFailure instead of NonExistentDomain will reduce number of queries
    // sent by clients)
    // default is DNSReplyCode::NonExistentDomain
    dnsServer.setErrorReplyCode(DNSReplyCode::ServerFailure);
    // start DNS server for a specific domain name
    dnsServer.start(DNS_PORT, "meteo.my", apIP);
  #endif
  //Wire.begin(2,14);
  //Wire.begin(4,5);    //sda,scl
  Wire.begin();
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

    
    while(millis()-looptime<1000){
      while(Serial.available() > 0) {
        if(addchar(Serial.read())){
          parcestr(outstr);           // отработка радиоканала
        }
      }
    }
    
    bmp.getTemperatureAndPressure(T0,P0);
    //Serial.print(F("press "));Serial.println(P0);
    //Serial.print(F("Temp "));Serial.println(T0);
    data[0][0]=data[0][1]=datal[0][0]=datal[0][1]=int(T0*100);
    //Serial.print(F("Temp "));Serial.println(data[0][0]);
  }
  
  for(byte i=0;i<meteobyuamount;i++){
     //Serial.print(F("add to graf"));Serial.println(fastbuff[i]);
     data[i+2][datavect]=0;
  }
  
  data[1][0]=datal[1][0]=analogRead(A0);
}

void loop() {
  double T,P;
	server.handleClient();
  #ifdef dnsenable
    dnsServer.processNextRequest();
  #endif
  // данные с радиоканала
 /* while(Serial.available() > 0) {
    if(addchar(Serial.read())){
      parcestr(outstr);
    }
  }*/
  if(millis() - looptime > faststep){
    looptime=millis();
    char result;
    datavect++;
  	if(datavect>=buflen){
  		dataful=1;
  		datavect=0;
  	}
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
        data[0][datavect]=T*100;
        for(byte i=0;i<meteobyuamount;i++){
          //Serial.print(F("add to graf"));Serial.println(fastbuff[i]);
          if(buffready[i]){
            if(bufffirst[i]==0){
              bufffirst[i]=1;
              if(dataful){
                for(int p=0;p<buflen;p++){
                  data[i+2][p]=fastbuff[i];
                }
              }else{
                for(int p=0;p<datavect;p++){
                  data[i+2][p]=fastbuff[i];
                }
              }
            }
            data[i+2][datavect]=fastbuff[i];
          }else{
            data[i+2][datavect]=0;
          }
        }
      }
    }
    data[1][datavect]=Vbuff;
    #ifdef serialdata
      //Serial.print(F("\tA0 = \t"));Serial.print(float(data[1][datavect]),2);
      Serial.print(F("\tV = \t"));Serial.print(float(data[1][datavect])*0.027,2); Serial.println(F(" m/s"));
    #endif
    

    if(millis() - longtime > longstep && (datavect > 0 || dataful == 1)){
      longtime=millis();
      longvect++;
      if(longvect>=buflen){
          longful=1;
          longvect=0;
          //Serial.print("longful ");Serial.println(longful);
      }
    }
    datal[0][longvect]=T*100;
    datal[1][longvect]=data[1][datavect];
  }
}


bool addchar(char input){   //NL&CR   10 13
  if(input==13){  //Carriage return
    ;
  }else{
    if(input==10){  //Line feed   linux
      outstr=inputstr;
      inputstr="";
      return 1;
    }else{
      inputstr+=input;
    }
  }
  return 0;
}

void parcestr(String instring){
  String  number;
  uint8_t arryind[7],n ;
  int32_t TempData[7] ;
  char line[64];
  //Serial.print("parce ");
  if (instring.startsWith("MB ") ) {
        //Serial.print("MB_");Serial.println(instring);
        arryind[0] = instring.indexOf(" ");
        for ( n=1;n<7;n++){
          if (instring.indexOf(" ",arryind[n-1]) > 0 ) {
            arryind[n] = instring.indexOf(" ", arryind[n-1] + 1);
            number = instring.substring(arryind[n-1],arryind[n]);
            number.toCharArray(line,number.length()+1);
            TempData[n-1] = atoi(line);
            //Serial.print("parce ");Serial.print(number);Serial.print("_");Serial.print(line);Serial.print("_");Serial.println(TempData[n-1]);
          } else break;
        }
        //Serial.print(TempData[0]);Serial.print("_");Serial.println(TempData[1]);
        if(TempData[0] >0 && TempData[0] <= meteobyuamount){
          //Serial.print("add data ");Serial.print(TempData[0]);Serial.print("_");Serial.println(TempData[1]);
          fastbuff[TempData[0]-1]=TempData[1];
        }
  }
  #ifdef debug
  else if (instring.startsWith("get") ) {
    arryind[0] = instring.indexOf(" ");
        for ( n=1;n<7;n++){
          if (instring.indexOf(" ",arryind[n-1]) > 0 ) {
            arryind[n] = instring.indexOf(" ", arryind[n-1] + 1);
            number = instring.substring(arryind[n-1],arryind[n]);
            number.toCharArray(line,number.length()+1);
            TempData[n-1] = atoi(line);
            //Serial.print("parce ");Serial.print(number);Serial.print("_");Serial.print(line);Serial.print("_");Serial.println(TempData[n-1]);
          } else break;
        }
        //Serial.print(TempData[0]);Serial.print("_");Serial.println(TempData[1]);
        if(TempData[0] >=0 && TempData[0] <= meteobyuamount){
          switch (TempData[0]) {
            case 3:    // your hand is nowhere near the sensor
              Serial.println(getfullraw());
              break;
          }
          //Serial.println(TempData[1]);
        }
  }
  #endif
}

String meteo_set(){
  for (uint8_t i=0; i<server.args(); i++){
    if(server.argName(i).startsWith("MBT")){
      int32_t cn=server.arg(i).toInt();
      String number = server.argName(i).substring(3,server.argName(i).length());
      int32_t n = number.toInt();
      if(cn>=-10000 && cn<=10000 && n>=0 && n<meteobyuamount){
        fastbuff[n] = cn;
        buffready[n] = 1;
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

String getdatabin(){
  int16_t select=0;
  String out="";
  byte over=0x80;
  //размер буфера, 13бит~8192max
  out+=char(((buflen>>7)&0x3f)|0x80);
  out+=char((buflen&0x7f)|0x80);
  // номер активного элемента 5мин и бит полного буфера
  if(dataful){
    over=0xC0;
  }else{
    over=0x80;
  }
  out+=char(((datavect>>7)&0x3f)|over);
  out+=char((datavect&0x7f)|0x80);
  // номер активного элемента час и бит полного буфера
  if(longful){
    over=0xC0;
  }else{
    over=0x80;
  }
  out+=char(((longvect>>7)&0x3f)|over);
  out+=char((longvect&0x7f)|0x80);

  for (uint8_t i=0; i<server.args(); i++){
    if(server.argName(i).startsWith("sel")){
      select=(((server.arg(i).charAt(0)&B00111111)<<6|(server.arg(i).charAt(1)&B00111111))&validselect); //validselect-допустимые режимы
    }
  }
  // reserved, treal, vreal, tlong, vlong, te1,2,3
  out+=char(((select<<7)&0x7F)|0x80);                   //reserved
  out+=char((select&0x7F)|0x80);
  /*
  switch(select){
    case 1:out+=char((0xA7)); //real
    break;
    case 2:out+=char((0x99)); //long
    break;
    case 3:out+=char((0xBf)|); //full
    break;
  }
  */
  
  if((select>>1)&1){
    for(int i=0;i<buflen;i++){    //tr
       out += char(((data[0][i]>>7)&0x3F)|0x80|((data[0][i]>>9)&0x40));
       out += char((data[0][i]&0x7F)|0x80);
    }
  }
  if((select>>2)&1){
    for(int i=0;i<buflen;i++){    //vr
       out += char(((data[1][i]>>7)&0x3F)|0x80|((data[1][i]>>9)&0x40));
       out += char((data[1][i]&0x7F)|0x80);
    }
  }
  if((select>>3)&1){
    for(int i=0;i<buflen;i++){    //tl
       out += char(((datal[0][i]>>7)&0x3F)|0x80|((datal[0][i]>>9)&0x40));
       out += char((datal[0][i]&0x7F)|0x80);
    }
  }
  if((select>>4)&1){
    for(int i=0;i<buflen;i++){    //vl
       out += char(((datal[1][i]>>7)&0x3F)|0x80|((datal[1][i]>>9)&0x40));
       out += char((datal[1][i]&0x7F)|0x80);
    }
  }
  if((select>>5)&1){
    for(int i=0;i<buflen;i++){    //te
       out += char(((data[2][i]>>7)&0x3F)|0x80|((data[2][i]>>9)&0x40));
       out += char((data[2][i]&0x7F)|0x80);
    }
  }
  return out;
}

String getrealraw(){
	int graflen,grafvect;
	String out="";
	grafvect=datavect;
	if(dataful){graflen=buflen;}else{graflen=datavect+1;}
	for(byte nl=0;nl<(2+meteobyuamount);nl++){
		for(int i=0;i<graflen;i++){
		   if(i<=grafvect){
			 out += String(data[nl][grafvect-i])+' ';
		   }else{
			 if(dataful){
				 out += String(data[nl][buflen+grafvect-i])+' '; 
			  }
		   } 
		}
		out += ';';
	}
  return out;
}

String getlongraw(){
	int graflen,grafvect;
	String out="";
	grafvect=longvect;
	if(longful){graflen=buflen;}else{graflen=longvect+1;}
    for(byte nl=0;nl<2;nl++){
		for(int i=0;i<graflen;i++){
		  if(i<=grafvect){
			 out += String(datal[nl][grafvect-i])+' ';
		  }else{
			 if(longful){
				out += String(datal[nl][buflen+grafvect-i])+' '; 
			 }
		  } 
		}
	  out += ';';
	}
  return out;
}

String getfullraw(){
  int graflen,grafvect;
  String out="";
  byte nl=0;
  //treal
    grafvect=datavect;
    if(dataful){graflen=buflen;}else{graflen=datavect+1;}
    for(int i=0;i<graflen;i++){
       if(i<=grafvect){
         out += String(data[nl][grafvect-i])+' ';
       }else{
         if(dataful){
             out += String(data[nl][buflen+grafvect-i])+' '; 
          }
       } 
    }
	out += ';';
	//vreal
	nl=1;
	for(int i=0;i<graflen;i++){
       if(i<=grafvect){
         out += String(data[nl][grafvect-i])+' ';
       }else{
         if(dataful){
             out += String(data[nl][buflen+grafvect-i])+' '; 
          }
       } 
    }
	out += ';';
	//tlong
    nl=0;
    grafvect=longvect;
    if(longful){graflen=buflen;}else{graflen=longvect+1;}
    for(int i=0;i<graflen;i++){
      if(i<=grafvect){
         out += String(datal[nl][grafvect-i])+' ';
      }else{
         if(longful){
            out += String(datal[nl][buflen+grafvect-i])+' '; 
         }
      } 
    }
  out += ';';
  //vlong
  nl=1;
    for(int i=0;i<graflen;i++){
      if(i<=grafvect){
         out += String(datal[nl][grafvect-i])+' ';
      }else{
         if(longful){
            out += String(datal[nl][buflen+grafvect-i])+' '; 
         }
      } 
    }
  out += ';';
  //te-real
  grafvect=datavect;
  if(dataful){graflen=buflen;}else{graflen=datavect+1;}
  for(byte n=0;n< meteobyuamount;n++){
    nl=n+2;
    for(int i=0;i<graflen;i++){
       if(i<=grafvect){
         out += String(data[nl][grafvect-i])+' ';
       }else{
         if(dataful){
             out += String(data[nl][buflen+grafvect-i])+' '; 
          }
       } 
    }
    out += ';';
  }
  return out;
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

  String output = "[";
  
  File file = dir.openNextFile();
  while(file){
        if(file.isDirectory()){
            Serial.print("  DIR : ");
            Serial.println(file.name());
           /* if(levels){
                listDir(fs, file.name(), levels -1);
            }*/
        } else {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("\tSIZE: ");
            Serial.println(file.size());
        }
        file = dir.openNextFile();
  }
  
  output += "]";
  server.send(200, "text/json", output);
}

#ifdef timerenable
#define comand_amount 16
#define servoamount 8
#define maxpos 1200
struct  {
     struct  {
       unsigned int time;
       unsigned int duration;
       unsigned int angle:12;
       byte status:2;
       byte type:3;
       byte num:3;
     }  timer[comand_amount] ;
     struct {
        byte time:4;
        byte power:4;
     } led[2] ;
     unsigned int beginangle[servoamount];
     unsigned int speed;
     int deltaspeed;
     byte tone[2] ;
     unsigned long UID;
     struct  {
       uint8_t key[16]; // = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
       uint16_t freq;   //420+freq/1000
     } radio ;
} Data; 

String timerconf(){
  for (uint8_t i=0; i<server.args(); i++){
    if(server.argName(i).startsWith("beginangle")){
      int32_t cn=server.arg(i).toInt();
      String number = server.argName(i).substring(10,server.argName(i).length());
      int n = number.toInt();
      if(cn>=0 && cn<=maxpos && n<servoamount){
        Data.beginangle[n] = cn;
      }
    }

    if(server.argName(i).startsWith("start")){
      int32_t cn=server.arg(i).toInt();
      String number = server.argName(i).substring(5,server.argName(i).length());
      int n = number.toInt();
      if(cn>=0 && cn<=1300000 && n< comand_amount){
        Data.timer[n].time = cn/20;
      }
    }
    if(server.argName(i).startsWith("len")){
      int32_t cn=server.arg(i).toInt();
      String number = server.argName(i).substring(3,server.argName(i).length());
      int n = number.toInt();
      if(cn>=0 && cn<=1300000 && n< comand_amount){
        Data.timer[n].duration = cn/20;
      }
    }
    if(server.argName(i).startsWith("angle")){
      int32_t cn=server.arg(i).toInt();
      String number = server.argName(i).substring(5,server.argName(i).length());
      int n = number.toInt();
      if(cn>=0 && cn<=maxpos && n< comand_amount){
        Data.timer[n].angle = cn;
      }
    }
    if(server.argName(i).startsWith("num")){
      int32_t cn=server.arg(i).toInt();
      String number = server.argName(i).substring(3,server.argName(i).length());
      int n = number.toInt();
      if(cn>=0 && cn<=servoamount && n< comand_amount){
        Data.timer[n].num = cn;
      }
    }
    if(server.argName(i).startsWith("type")){
      int32_t cn=server.arg(i).toInt();
      String number = server.argName(i).substring(4,server.argName(i).length());
      int n = number.toInt();
      if(cn>=0 && cn<=8 && n< comand_amount){
        Data.timer[n].type = cn;
      }
    }
  }
  
  String message = "<html>";
  message += "<style type=\"text/css\"> input.long{width:65px;} input.pos{width:50px;} input.short{width:25px;}</style><meta charset=\"utf-8\">";
  message += "<body>";
  message += "<form>Начальное положение";
  message += "<p>";
  for(int n=0;n<servoamount;n++){
    message += "<input class=\"pos\" name=\"beginangle";
    message += n;
    message += "\" pattern=\"[0-9]{1,4}\" value=\"";
    message += Data.beginangle[n];
    message += "\">";
  }
  message += "</p>";
  message += "<p>Команды</p>";
  message += "<p>старт,длительность,положение,номер сервы,номер кривой</p>";
  for(int n=0;n<comand_amount;n++){
    message += "<p>";
    message += "<input class=\"long\" name=\"start";
    message += n;
    message += "\" pattern=\"[0-9]{1,6}\" value=\"";
    message += Data.timer[n].time*20;
    message += "\">";
    message += "<input class=\"long\" name=\"len";
    message += n;
    message += "\" pattern=\"[0-9]{1,6}\" value=\"";
    message += Data.timer[n].duration*20;
    message += "\">";
    message += "<input class=\"pos\" name=\"angle";
    message += n;
    message += "\" pattern=\"[0-9]{1,4}\" value=\"";
    message += Data.timer[n].angle;
    message += "\">";
    message += "<input class=\"short\" name=\"num";
    message += n;
    message += "\" pattern=\"[0-7]\" value=\"";
    message += Data.timer[n].num;
    message += "\">";
    message += "<input class=\"short\" name=\"type";
    message += n;
    message += "\" pattern=\"[0-7]\" value=\"";
    message += Data.timer[n].type;
    message += "\">";
    message += "</p>";
  }
  
  message += "<p><input type=\"submit\" value=\"Set\"></p>";
  message += "</form>";
  
  message += "</body></html>";
  return message;
}
#endif

#ifdef timerliteenable

#define comand_lite_amount 4
uint16_t timerlite[comand_lite_amount];
const  String  S_TimerDataLite = "TL ";

String timerliteconf(){
  for (uint8_t i=0; i<server.args(); i++){
    if(server.argName(i).startsWith("start")){
      int32_t cn=server.arg(i).toInt();
      String number = server.argName(i).substring(5,server.argName(i).length());
      int32_t n = number.toInt();
      if(cn>=0 && cn<=1300000 && n< comand_lite_amount){
        timerlite[n] = cn/20;
      }
      //Serial.println("n: " + String(n)+"\tval: "+String(cn));
    }
    //else{
    //  Serial.println("argName not start");
    //}
  }
  char datastr[comand_lite_amount*3+S_TimerDataLite.length()+4];
  for(byte v=0;v<S_TimerDataLite.length();v++){
    datastr[v]=byte(S_TimerDataLite.charAt(v));
  }
  for(byte v=0;v<comand_lite_amount;v++){
    //data[v]=(instr[v*3+S_TimerDataLite.length()]&0x3F) | (int(instr[v*3+1+S_TimerDataLite.length()]&0x3F)<<6)| (int(instr[v*3+2+S_TimerDataLite.length()]&0x3F)<<12);
    datastr[v*3+S_TimerDataLite.length()]  =(timerlite[v]&0x3F)|0x40;
    datastr[v*3+S_TimerDataLite.length()+1]=((timerlite[v]>>6)&0x3F)|0x40;
    datastr[v*3+S_TimerDataLite.length()+2]=((timerlite[v]>>12)&0x3F)|0x40;
  }
  uint8_t CRC=0;
  for (uint8_t c = S_TimerDataLite.length() ;c <comand_lite_amount*3+S_TimerDataLite.length();c++){ 
    CRC = CRC^datastr[c]; 
  }
  datastr[comand_lite_amount*3+S_TimerDataLite.length()]  =(CRC & 0x3F)   |0x40;
  datastr[comand_lite_amount*3+S_TimerDataLite.length()+1]=((CRC<<6)&0x3F)|0x40;
  datastr[comand_lite_amount*3+S_TimerDataLite.length()+2]=13;
  datastr[comand_lite_amount*3+S_TimerDataLite.length()+3]=10;
  datastr[comand_lite_amount*3+S_TimerDataLite.length()+4]=0;
  Serial.println(datastr);
  
  String message = "<html>";
  message += "<style type=\"text/css\"> input.long{width:85px;} input.pos{width:50px;} input.short{width:25px;}</style><meta charset=\"utf-8\">";
  message += "<body><form>";

  message += "<p>Команды</p>";
  //message += "<p>старт</p>";
  for(int n=0;n<comand_lite_amount;n++){
    message += "<p>";
    message += "<input class=\"long\" type=\"number\" min=\"0\" max=\"1310700\" step=\"20\" name=\"start";
    message += n;
    message += "\" value=\"";
    message += String(uint32_t(timerlite[n])*20);
    message += "\">ms";
    message += "</p>";
    //Serial.println("timerlite[n]: "+String(uint32_t(timerlite[n])*20));
  }
  
  message += "<p><input type=\"submit\" value=\"Set\"></p>";
  message += "</form>";
  message += "</body></html>";
  //Serial.println(message);
  return message;
}
#endif

String meteoconf(){
  for (uint8_t i=0; i<server.args(); i++){
    if(server.argName(i).startsWith("lenreal")){
      int32_t cn=server.arg(i).toInt();
		faststep = (cn*1000/buflen);
    }
    if(server.argName(i).startsWith("lenlong")){
      int32_t cn=server.arg(i).toInt();
      longstep = (cn*1000/buflen); 
    }
  }

  String message = "<html>";
  //message += "<style type=\"text/css\"> input.long{width:85px;} input.pos{width:50px;} input.short{width:25px;}</style><meta charset=\"utf-8\">";
  message += "<body><form>";

  //message += "<p>Команды</p>";
  //message += "<p>старт</p>";
    message += "<p>";
    message += "<input class=\"long\" type=\"number\" min=\"0\" name=\"lenreal";
    message += "\" value=\"";
    message += String(uint32_t(faststep)*buflen/1000);
    message += "\">s";
    message += "</p>";
    message += "<p>";
    message += "<input class=\"long\" type=\"number\" min=\"0\" name=\"lenlong";
    message += "\" value=\"";
    message += String(uint32_t(longstep)*buflen/1000);
    message += "\">s";
    message += "</p>";
    //Serial.println("timerlite[n]: "+String(uint32_t(timerlite[n])*20));

  
  message += "<p><input type=\"submit\" value=\"Set\"></p>";
  message += "</form>";
  message += "</body></html>";
  //Serial.println(message);
  return message;
}
