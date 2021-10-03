// ChoreBoard.ino - Keeping track of who should do the next chore
// links:
//   https://arduino-esp8266.readthedocs.io/en/latest/filesystem.html
//   https://microcontrollerslab.com/esp8266-nodemcu-web-server-using-littlefs-flash-file-system/
//   https://github.com/me-no-dev/ESPAsyncWebServer#request-variables
#define APP_VERSION "v1"
#define APP_NAME    "ChoreBoard"


// BUT (GPIO button) =========================================================================
// Driver for the built-in GPIO0 button

#define BUT_PIN 0

int but_read() {
  return digitalRead(BUT_PIN)==0;
}

void but_setup() {
  pinMode(BUT_PIN, INPUT);
  Serial.printf("but : setup\n");
}

// LED (signal LED) ==========================================================================
// Driver for the built-in LED - used for simple signalling to the user

#define LED_PIN LED_BUILTIN

// Switches the signalling LED on the ESP8266 board off.
void led_off() {
  digitalWrite(LED_PIN, HIGH); // low active
}

// Switches the signalling LED on the ESP8266 board on.
void led_on() {
  digitalWrite(LED_PIN, LOW); // low active
}

// Switches the signalling LED on the ESP8266 board to `on`.
void led_set(int on) {
  digitalWrite(LED_PIN, !on); // low active
}

// Toggles the signalling LED on the ESP8266 board.
void led_tgl() {
  digitalWrite(LED_PIN, !digitalRead(LED_PIN) );
}

// Initializes the LED driver.
// Configures the GPIO block for the LED pin.
void led_setup() {
  pinMode(LED_PIN, OUTPUT);
  led_off();
  Serial.printf("led : setup\n");
}

// Time ===============================================================
// Driver for keeping time using NTP servers

#include <time.h>

#define SVR1 "pool.ntp.org"
#define SVR2 "europe.pool.ntp.org"
#define SVR3 "north-america.pool.ntp.org"

#define TZ   "CET-1CEST,M3.5.0,M10.5.0/3" // Amsterdam

void time_setup() {
  configTime(TZ, SVR1, SVR2, SVR3);
  Serial.printf("time: setup\n");
}

char * time_get() {
  static char buf[4+2+2+1+2+2+2+1];
  time_t      tnow= time(NULL);       // Returns seconds (and writes to the passed pointer, when not NULL) - note `time_t` is just a `long`.
  struct tm * snow= localtime(&tnow); // Returns a struct with time fields (https://www.tutorialspoint.com/c_standard_library/c_function_localtime.htm)

  // In `snow` the `tm_year` field is 1900 based, `tm_month` is 0 based, rest is as expected
  snprintf(buf,sizeof(buf),"%04d%02d%02d%02d%02d%02d", snow->tm_year + 1900, snow->tm_mon + 1, snow->tm_mday, snow->tm_hour, snow->tm_min, snow->tm_sec );
  return buf;
}

// WiFi ==============================================================

#include <ESP8266WiFi.h>

#if 1
  #include "credentials.h" // private file not in repo
#else
  #define CREDENTIALS_SSID "..."
  #define CREDENTIALS_PASS "..."
#endif

void wifi_setup() {
  Serial.printf("wifi: connecting to %s ..",CREDENTIALS_SSID);
  
  //WiFi.persistent(false);
  WiFi.hostname(APP_NAME); // ESP-F1DF77.fritz.box works, but not the hostname
  WiFi.mode(WIFI_STA);
  WiFi.begin(CREDENTIALS_SSID, CREDENTIALS_PASS);

  // Blink LED while we wait for WiFi connection
  while( WiFi.status()!=WL_CONNECTED ) {
    led_tgl();
    Serial.printf(".");
    delay(500);
  }
  led_off();

  // GUI feedback (serial and LED)
  Serial.printf(" connected %s\n",WiFi.localIP().toString().c_str());
}


// File ===============================================================
//https://github.com/earlephilhower/arduino-esp8266littlefs-plugin/releases

#include "LittleFS.h"

void file_dir() {
  Dir root = LittleFS.openDir("/");
  Serial.printf("file: ");
  int n=0;
  while( root.next() ){
    if( n>0 ) Serial.printf(", ");
    Serial.printf("%s", root.fileName() );
    if( root.isFile() ) {
        File f = root.openFile("r");
        Serial.printf( " (%dB)",f.size());
    }
    n++;
  }
  Serial.printf(" [%d]\n",n);
}

void file_begin() {
  int res= LittleFS.begin();
  if( !res ) {
    Serial.printf("file: setup FAILED\n");
    return;
  }

  // To format all space in LittleFS
  // LittleFS.format()

  FSInfo fs_info;
  LittleFS.info(fs_info);
  Serial.printf("file: space %d, used %d\n",fs_info.totalBytes, fs_info.usedBytes);
  file_dir();
}


void file_read() {
  File html = LittleFS.open("/index.html","r");
  if( !html ) {
    Serial.printf("file: read FAILED\n");
    return;
  }

  Serial.println(html.readString());
  html.close();
}


// Webserver ==========================================================

#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
AsyncWebServer http_server(80);

// Replaces placeholders
String http_msg;
String processor(const String& var){
  if( var=="MSG"){
    return http_msg;
  }
  return String();
}

bool http_nameexists( const char * name ) {
  return true; // todo
}

bool http_nameok( const char * name ) {
  while( *name!='\0' ) {
    if( ! isalnum(*name) ) return false;
    name++;
  }
  return true;
}

bool http_ivalok( const char * ival ) {
  while( *ival!='\0' ) {
    if( ! isdigit(*ival) ) return false;
    ival++;
  }
  return true;
}

bool http_deltaok( const char * delta ) {
  if( *delta=='-' ) delta++;
  while( *delta!='\0' ) {
    if( ! isdigit(*delta) ) return false;
    delta++;
  }
  return true;
}

void http_setup() {
  http_server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.printf("http: GET /\n");
    http_msg="";
    request->send(LittleFS, "/index.html", String(), false, processor);
  });
  
  http_server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.printf("http: GET /favicon.ico\n");
    request->send(LittleFS, "/favicon.ico","image/x-icon" );
  });
  
  http_server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.printf("http: GET /style.css\n");
    request->send(LittleFS, "/style.css", "text/css");
  });

  http_server.on("/conf.json", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.printf("http: GET /conf.json\n");
    request->send(LittleFS, "/conf.json", "application/json");
  });

  http_server.on("/log.json", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.printf("http: GET /log.json\n");
    request->send(LittleFS, "/log.json", "application/json");
  });

  http_server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.printf("http: GET /\n");
    if( ! request->hasParam("name") ) { // request without args
      http_msg = "";
      request->send(LittleFS, "/index.html", String(), false, processor);
      return;
    }
    
    AsyncWebParameter *name, *delta;
    long idelta;
    http_msg = "";
  
    if(request->hasParam("name")) {
      name = request->getParam("name");
      if( !http_nameexists(name->value().c_str()) ) http_msg = "name must exist in conf.json";
    }
    if(request->hasParam("delta")) {
      delta = request->getParam("delta");
      if( !http_deltaok(delta->value().c_str()) ) http_msg = "delta must be numeric";
      idelta = delta->value().toInt();
      if( idelta==0 ) http_msg = "delta of 0 ignored";
    } else {
      idelta = +1;
    }

    if( http_msg!="" ) { // There was an error
      Serial.printf("http: %s\n", http_msg.c_str());
      request->send(LittleFS, "/index.html", String(), false, processor);
      return;
    }

    // Append to the log
    File log = LittleFS.open("/log.json", "a");
    if( !log ) {
      http_msg = "appending /log.json failed";
    } else {
      log.printf(",{\"time\":%s, \"name\":\"%s\", \"delta\":%d}\n",time_get(), name->value().c_str(), idelta );
      log.close();
    }
  
    Serial.printf("http: %s %d\n",name->value().c_str(),idelta);
    http_msg = String(idelta>0?"+":"") + idelta + " for " +  name->value();
    request->send(LittleFS, "/index.html", String(), false, processor);

  });
  
  http_server.on("/reset.html", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.printf("http: GET /reset.html\n");
    if( ! request->hasParam("name1") ) { // request without args
      http_msg = "";
      request->send(LittleFS, "/reset.html", String(), false, processor);
      return;
    }
  
    AsyncWebParameter *name1, *ival1, *name2, *ival2, *name3, *ival3, *name4, *ival4, *name5, *ival5;
    http_msg = "";
  
    if(request->hasParam("name1")) {
      name1 = request->getParam("name1");
      if( !http_nameok(name1->value().c_str()) ) http_msg = "name1 must be alpha numeric";
    }
    if(request->hasParam("ival1")) {
      ival1 = request->getParam("ival1");
      if( !http_ivalok(ival1->value().c_str()) ) http_msg = "ival1 must be numeric";
    }
    if( name1->value().length()==0 ) {
      http_msg = "name1 can not be empty";
    }
    
    if(request->hasParam("name2")) {
      name2 = request->getParam("name2");
      if( !http_nameok(name2->value().c_str()) ) http_msg = "name2 must be alpha numeric";
      if( strcmp(name1->value().c_str(),name2->value().c_str())==0 ) http_msg = "name2 may not be the same as name1";
    }
    if(request->hasParam("ival2")) {
      ival2 = request->getParam("ival2");
      if( !http_ivalok(ival2->value().c_str()) ) http_msg = "ival2 must be numeric";
    }
    if( name2->value().length()==0 ) {
      http_msg = "name1 can not be empty";
    }
    int count = 2;
  
    if(request->hasParam("name3")) {
      name3 = request->getParam("name3");
      if( !http_nameok(name3->value().c_str()) ) http_msg = "name3 must be alpha numeric";
    }
    if(request->hasParam("ival3")) {
      ival3 = request->getParam("ival3");
      if( !http_ivalok(ival3->value().c_str()) ) http_msg = "ival3 must be numeric";
      if( strcmp(name1->value().c_str(),name3->value().c_str())==0 ) http_msg = "name3 may not be the same as name1";
      if( strcmp(name2->value().c_str(),name3->value().c_str())==0 ) http_msg = "name3 may not be the same as name2";
    }
    if( name3->value().length()>0 ) count = 3;
  
    if(request->hasParam("name4")) {
      name4 = request->getParam("name4");
      if( !http_nameok(name4->value().c_str()) ) http_msg = "name4 must be alpha numeric";
    }
    if(request->hasParam("ival4")) {
      ival4 = request->getParam("ival4");
      if( !http_ivalok(ival4->value().c_str()) ) http_msg = "ival4 must be numeric";
    }
    if( name4->value().length()>0 ) {
      if( count!=3 ) http_msg = "name4 assigned, but name3 is empty";
      else {
        if( strcmp(name1->value().c_str(),name4->value().c_str())==0 ) http_msg = "name4 may not be the same as name1";
        if( strcmp(name2->value().c_str(),name4->value().c_str())==0 ) http_msg = "name4 may not be the same as name2";
        if( strcmp(name3->value().c_str(),name4->value().c_str())==0 ) http_msg = "name4 may not be the same as name3";
        count = 4;
      }
    }
  
    if(request->hasParam("name5")) {
      name5 = request->getParam("name5");
      if( !http_nameok(name5->value().c_str()) ) http_msg = "name5 must be alpha numeric";
    }
    if(request->hasParam("ival5")) {
      ival5 = request->getParam("ival5");
      if( !http_ivalok(ival5->value().c_str()) ) http_msg = "ival5 must be numeric";
    }
    if( name5->value().length()>0 ) {
      if( count!=4 ) http_msg = "name5 assigned, but name4 is empty";
      else {
        if( strcmp(name1->value().c_str(),name5->value().c_str())==0 ) http_msg = "name5 may not be the same as name1";
        if( strcmp(name2->value().c_str(),name5->value().c_str())==0 ) http_msg = "name5 may not be the same as name2";
        if( strcmp(name3->value().c_str(),name5->value().c_str())==0 ) http_msg = "name5 may not be the same as name3";
        if( strcmp(name4->value().c_str(),name5->value().c_str())==0 ) http_msg = "name5 may not be the same as name4";
        count = 5;
      }
    }
  
    if( !but_read() ) http_msg = "security error: counters reset needs IO0 button on server to be pressed";

    if( http_msg!="" ) { // There was an error
      Serial.printf("http: %s\n", http_msg.c_str());
      request->send(LittleFS, "/reset.html", String(), false, processor);
      return;
    }
  
    // Reset the configuration (person list with initial values)
    File conf = LittleFS.open("/conf.json", "w");
    if( !conf ) {
      http_msg = "writing /conf.json failed";
    } else {
      conf.printf("{\"%s\":%s\n",name1->value().c_str(),ival1->value().c_str() );
      conf.printf(",\"%s\":%s\n",name2->value().c_str(),ival2->value().c_str() );
      if( count>=3 ) conf.printf(",\"%s\":%s\n",name3->value().c_str(),ival3->value().c_str() );
      if( count>=4 ) conf.printf(",\"%s\":%s\n",name4->value().c_str(),ival4->value().c_str() );
      if( count>=5 ) conf.printf(",\"%s\":%s\n",name5->value().c_str(),ival5->value().c_str() );
      conf.printf("}\n");
      conf.close();
    }
    // Reset the log (all transactions)
    File log = LittleFS.open("/log.json", "w");
    if( !log ) {
      http_msg = "writing /log.json failed";
    } else {
      log.printf(" {\"time\":%s, \"name\":\"%s\", \"delta\":0}\n",time_get(), name1->value().c_str() );
      log.close();
    }
    
    if( http_msg!="" ) { // There was an error
      Serial.printf("http: %s\n", http_msg.c_str());
      request->send(LittleFS, "/reset.html", String(), false, processor);
      return;
    }
  
    Serial.printf("http: reset (%d users)\n",count);
    http_msg = "Reset performed";
    request->send(LittleFS, "/reset.html", String(), false, processor);
  } );

  // Start server
  http_server.begin();
  Serial.printf("http: setup\n");
}

// App ================================================================


void setup() {
  delay(1000);
  Serial.begin(115200);
  while( !Serial ) delay(200);
  Serial.printf("\n\n%s - %s\n\n", APP_NAME, APP_VERSION);

  time_setup();
  led_setup();
  but_setup();
  wifi_setup();
  file_begin();
  http_setup();
}

void loop() {
}
