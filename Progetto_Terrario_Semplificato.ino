#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <NTPClient.h>
#include <OneWire.h> 
#include <DallasTemperature.h>
#include <ArduinoJson.h>
#include "FS.h"
#include <WiFiManager.h> 

#define ONE_WIRE_BUS D3
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

#define SX_DOOR D5
#define DX_DOOR D6
#define BUZZER D7

ESP8266WebServer server(80); 
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7200); //fuso orario UTC quando ultimo parametro 0
WiFiManager wifiManager;

//****Alarm definitions****
#define TMAX_ZONE_1 38 //Zona calda
#define TMIN_ZONE_1 20 //Zona calda
#define OPEN_DOOR_MAX_TIME 300//Tempo massimo in secondi apertura porte prima di allarme

float t_zone1;
bool sx_door_state=0,dx_door_state=0,global_status=0;
unsigned long alarm_sx_door_time,alarm_dx_door_time,alarm_temp_zone_1_time;
bool alarm_sx_door=false,alarm_dx_door=false,alarm_temp=false, notified=false;
char sx_door_color[8],dx_door_color[8],temp_color[8],status_color[8];
#define REDCOLOR "#f42a2a"
#define GREENCOLOR "#00f040"

uint16_t counter,open_door_max_time=OPEN_DOOR_MAX_TIME;
uint8_t numberOfDevices,counter_t=0,tmax_zone_1=TMAX_ZONE_1,tmin_zone_1=TMIN_ZONE_1;

void setup() {
  pinMode(SX_DOOR, INPUT);
  pinMode(DX_DOOR, INPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  Serial.begin(115200);
  //wifi_connect();
  network_config();
  if (!SPIFFS.begin()) {
    Serial.println("Failed to mount file system");
    return;
  }
  Serial.println("loading config.json");
  if (!loadConfig()) Serial.println("failed to read config, using default settings");
  sensors.begin();
  // Grab a count of devices on the wire
  numberOfDevices = sensors.getDeviceCount();
  Serial.print("Found ");
  Serial.print(numberOfDevices);
  Serial.println(" DS18B20 sensors");
  temperature_check();
}

void loop() {
  MDNS.update();
  server.handleClient();
  door_check();
  alarm_check();
  delay(100);
  counter++;
  counter_t++;
  if (counter>36000) {
    timeClient.update();
    counter=0;
  }
  if (counter_t>60) {
    temperature_check();
    counter_t=0;
  }
  //Serial.print("Free memory: ");
  //Serial.println(ESP.getFreeHeap());
}

bool loadConfig() {
  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile) {
    Serial.println("Failed to open config file");
    return false;
  }

  size_t size = configFile.size();
  if (size > 1024) {
    Serial.println("Config file size is too large");
    return false;
  }

  // Allocate a buffer to store contents of the file.
  std::unique_ptr<char[]> buf(new char[size]);

  // We don't use String here because ArduinoJson library requires the input
  // buffer to be mutable. If you don't use ArduinoJson, you may as well
  // use configFile.readString instead.
  configFile.readBytes(buf.get(), size);

  StaticJsonDocument<200> doc;
  auto error = deserializeJson(doc, buf.get());
  if (error) {
    Serial.println("Failed to parse config file");
    return false;
  }

  tmax_zone_1 = doc["tmax_zone_1"];
  tmin_zone_1 = doc["tmin_zone_1"];
  open_door_max_time = doc["open_door_max_time"];

  // Real world application would store these values in some variables for
  // later use.

  Serial.print("Loaded Max Temperature Zone 1: ");
  Serial.println(tmax_zone_1);
  Serial.print("Loaded Min Temperature Zone 1: ");
  Serial.println(tmin_zone_1);
  Serial.print("Loaded Open Door Max Time: ");
  Serial.println(open_door_max_time);
  return true;
}

bool saveConfig() {
  StaticJsonDocument<200> doc;
  doc["tmax_zone_1"] = tmax_zone_1;
  doc["tmin_zone_1"] = tmin_zone_1;
  doc["open_door_max_time"] = open_door_max_time;

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("Failed to open config file for writing");
    return false;
  }

  serializeJson(doc, configFile);
  return true;
}

void network_config() {
  WiFi.hostname("terrario");
  wifiManager.autoConnect("Terrario");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected");
    Serial.print("IP: ");  Serial.println(WiFi.localIP());
    Serial.print("hostname: ");  Serial.println(WiFi.hostname());
  }
  //Start mDNS responder
  if (!MDNS.begin("terrario",WiFi.localIP())) {
    Serial.println("Error setting up MDNS responder!");
    while (1) {
      delay(1000);
    }
  }
  server.on("/", handle_OnConnect);
  server.on("/settings",handle_Settings);
  server.on("/action_page", handleForm);
  server.on("/reset-wifi-settings", reset_wifi_settings); //reset wifi settings calling http://terrario.local/reset-wifi-settings
  server.serveStatic("/img", SPIFFS, "/img");
  server.serveStatic("/config.json", SPIFFS, "/config.json");
  server.onNotFound(handle_NotFound);
  server.begin();
  Serial.println("HTTP server started");
  //register service to MDNS-SD
  MDNS.addService("http", "tcp", 80);
  Serial.println("mDNS responder started");
  //NTP client
  timeClient.begin();
  timeClient.update();
  Serial.println("NTP client started");
  MDNS.update();
}

void reset_wifi_settings(){
  
  wifiManager.resetSettings(); //reset wifi settings

  if(SPIFFS.remove("/config.json"))
  { //Remove config file
    Serial.println("Successfully removed settings");
  }else{
    Serial.println("Error removing settings");
  }
  delay(2000); // wait 2 seconds
  ESP.reset(); // and then reboot. After the reboot ESP8266 will be set in AP Mode SSID named "Stazione-Meteo" and address 192.168.4.1
}


void temperature_check() {
 Serial.print(" Requesting temperatures..."); 
 sensors.requestTemperatures(); // Send the command to get temperature readings 
 t_zone1=sensors.getTempCByIndex(0);
 Serial.println("DONE"); 
 Serial.print("Temperature is: "); 
 Serial.println(t_zone1);
 if ((t_zone1 >= tmin_zone_1) && (t_zone1 <= tmax_zone_1)) {
  alarm_temp=false;
  alarm_temp_zone_1_time=0;
  strcpy(temp_color,GREENCOLOR);
 }
 else if (alarm_temp_zone_1_time!=0) {
  if ((timeClient.getEpochTime()- alarm_temp_zone_1_time) > 60) alarm_temp=true;
  strcpy(temp_color,REDCOLOR);
 }
 else if (alarm_temp_zone_1_time==0) {
  alarm_temp_zone_1_time=timeClient.getEpochTime();
  strcpy(temp_color,REDCOLOR);
 }
 
}

void door_check() {
  sx_door_state=digitalRead(SX_DOOR);
  dx_door_state=digitalRead(DX_DOOR);
  if (sx_door_state==1) {
    alarm_sx_door_time=0;
    alarm_sx_door=false;
    strcpy(sx_door_color,GREENCOLOR);
    
  }
  if (sx_door_state==0 && alarm_sx_door_time!=0) {
    strcpy(sx_door_color,REDCOLOR);
    if ((timeClient.getEpochTime()- alarm_sx_door_time) > open_door_max_time) alarm_sx_door=true;
  }
  if (sx_door_state==0 && alarm_sx_door_time==0) alarm_sx_door_time=timeClient.getEpochTime();

  if (dx_door_state==1) {
    alarm_dx_door_time=0;
    alarm_dx_door=false;
    strcpy(dx_door_color,GREENCOLOR);
  }
  if (dx_door_state==0 && alarm_dx_door_time!=0) {
    strcpy(dx_door_color,REDCOLOR);
    if ((timeClient.getEpochTime()- alarm_dx_door_time) > open_door_max_time) alarm_dx_door=true;
  }
  if (dx_door_state==0 && alarm_dx_door_time==0) alarm_dx_door_time=timeClient.getEpochTime();
    
}

void alarm_check() {
  if (alarm_sx_door==true || alarm_dx_door==true || alarm_temp==true) 
    {
      global_status=1;
      strcpy(status_color,REDCOLOR);
      audible_alarm(true);
    }
  else { 
    global_status=0;
    strcpy(status_color,GREENCOLOR);
    audible_alarm(false);
  }
}

void audible_alarm(bool activate) {
  if (activate==true) {
    digitalWrite(LED_BUILTIN,LOW);
    tone(BUZZER, 1000);
  }
  if (activate==false){ 
    digitalWrite(LED_BUILTIN,HIGH);
    noTone(BUZZER);
  }
}

void handle_OnConnect() {
  server.send(200, "text/html", SendHTML()); 
}

void handle_NotFound(){
  server.send(404, "text/plain", "Not found");
}

void handle_Settings() {
  server.send(200, "text/html", settings(tmax_zone_1,tmin_zone_1,open_door_max_time)); 
}

void handleForm() {
 String ptr;
 tmax_zone_1 = server.arg("tmax").toInt(); 
 tmin_zone_1 = server.arg("tmin").toInt(); 
 open_door_max_time =server.arg("open_door_time").toInt(); 
 if (saveConfig()) ptr = "<h1>Configuration saved <a href='/'> Go Back </a><h1>";
 else ptr = "<h1>Problem saving configuration <a href='/'> Go Back </a><h1>";
 server.send(200, "text/html", ptr); //Send web page
 
}

String settings(uint8_t tmax,uint8_t tmin,uint16_t open_door_time) {
  String ptr="<html><title>Settings</title><body><h3>Settings</h3> <form action=\"/action_page\">";
  ptr += "Max Temperature:<br><input type=\"number\" name=\"tmax\" value=\"";
  ptr += tmax;
  ptr += "\"><br>";
  ptr += "Minimum Temperature:<br><input type=\"number\" name=\"tmin\" value=\"";
  ptr += tmin;
  ptr += "\"><br>";
  ptr += "Open door max time:<br><input type=\"number\" name=\"open_door_time\" value=\"";
  ptr += open_door_time;
  ptr += "\"><br>";
  ptr += "<input type=\"submit\" value=\"Submit\"></form><br>";
  ptr += "<form action=/reset-wifi-settings>";
  ptr += "<input type=\"submit\" value=\"Reset WiFi Settings\">";
  ptr += "</body></html>";
  return ptr;
}

String SendHTML(){

  String dx_door_html;
  String sx_door_html;
  String global_status_html;
  if (sx_door_state==1) sx_door_html="Chiusa";
  if (sx_door_state==0) sx_door_html="Aperta";
  if (dx_door_state==1) dx_door_html="Chiusa";
  if (dx_door_state==0) dx_door_html="Aperta";
  if (global_status==0) global_status_html="OK";
  else global_status_html="Anomalo";
 
  String ptr = "<!DOCTYPE html>";
  
  ptr +="<html xmlns=\"http://www.w3.org/1999/xhtml\">";
  ptr +="<head>";
  ptr +="<meta charset=\"utf-8\"/>";
  ptr +="<title>Controllo Terrario</title>";
  ptr +="<meta name=\"viewport\" content=";
  ptr +="\"width=device-width, initial-scale=1.0\" />";
  ptr +="<style>";
  ptr +="";
  ptr +="html {";
  ptr +="font-family: \"Helvetica\", sans-serif;";
  ptr +="display: block;";
  ptr +="margin: 0px auto;";
  ptr +="text-align: center;";
  ptr +="color: #444444;";
  ptr +="}";
  ptr +="body{margin: 0px;}";
  ptr +="h1 {margin: 50px auto 30px;}";
  ptr +=".side-by-side{display: table-cell;vertical-align: middle;position: relative;}";
  ptr +=".text{font-weight: 600;font-size: 15px;width: 170px;}";
  ptr +=".reading{font-weight: 300;font-size: 40px;padding-right: 25px;}";
  ptr +=".temperature .reading{color: ";
  ptr +=temp_color;
  ptr +=";}";
  ptr +=".status .reading{color: ";
  ptr +=status_color;
  ptr +=";}";
  ptr +=".sx_door .reading{color: ";
  ptr +=sx_door_color;
  ptr +=";}";
  ptr +=".dx_door .reading{color: ";
  ptr +=dx_door_color;
  ptr +=";}";
  ptr +=".time .reading{color: #955BA5;}";
  ptr +=".button{ background-color: #00f040;color: white;padding: 15px 32px;font-size: 16px;margin: 4px 2px;border-radius: 8px;}";
  ptr +=".superscript{font-size: 17px;font-weight: 600;position: absolute;top: 10px;}";
  ptr +=".data{padding: 10px;}";
  ptr +=".container{display: table;margin: 0 auto;}";
  ptr +=".icon{width:100px}";
  ptr +="";
  ptr +="</style>";
  ptr +="<script>";
  ptr +="";
  ptr +="setInterval(loadDoc,5000);";
  ptr +="function loadDoc() {";
  ptr +="var xhttp = new XMLHttpRequest();";
  ptr +="xhttp.onreadystatechange = function() {";
  ptr +="if (this.readyState == 4 && this.status == 200) {";
  ptr +="document.body.innerHTML =this.responseText}";
  ptr +="};";
  ptr +="xhttp.open(\"GET\", \"/\", true);";
  ptr +="xhttp.send();";
  ptr +="}";
  ptr +="";
  ptr +="</script>";
  ptr +="</head>";
  ptr +="<body>";
  ptr +="<h1>Terrario</h1>";
  ptr +="<div class=\"container\">";
  ptr +="<div class=\"data status\">";
  ptr +="<div class=\"side-by-side icon\">";
  if (global_status==0) ptr +="<img src=\"img/gecko-green.svg\" alt=\"Status\" height=\"50\" width=\"50\" />";
  else ptr +="<img src=\"img/gecko-red.svg\" alt=\"Status\" height=\"50\" width=\"50\" />";
  ptr +="</div>";
  ptr +="<div class=\"side-by-side text\">";
  ptr +="Stato terrario";
  ptr +="</div>";
  ptr +="<div class=\"side-by-side reading\">";
  ptr +=global_status_html;
  ptr +="</div>";
  ptr +="</div>";
  ptr +="<div class=\"data temperature\">";
  ptr +="<div class=\"side-by-side icon\">";
  if (alarm_temp==0) ptr +="<img src=\"img/temp-green.svg\" alt=\"Temperature\" height=\"54\" width=\"19\" />";
  else ptr +="<img src=\"img/temp-red.svg\" alt=\"Temperature\" height=\"54\" width=\"19\" />";
  ptr +="</div>";
  ptr +="<div class=\"side-by-side text\">";
  ptr +="Temperatura";
  ptr +="</div>";
  ptr +="<div class=\"side-by-side reading\">";
  ptr +=t_zone1;
  ptr +="<span class=\"superscript\">&deg;C</span>";
  ptr +="</div>";
  ptr +="</div>";
  ptr +="<div class=\"data sx_door\">";
  ptr +="<div class=\"side-by-side icon\">";
  if (sx_door_state==1) ptr +="<img src=\"img/sx_door-green.svg\" alt=\"SX Door\" height=\"50\" width=\"100\" />";
  else ptr +="<img src=\"img/sx_door-red.svg\" alt=\"SX Door\" height=\"50\" width=\"100\" />";
  ptr +="</div>";
  ptr +="<div class=\"side-by-side text\">";
  ptr +="Porta SX";
  ptr +="</div>";
  ptr +="<div class=\"side-by-side reading\">";
  ptr +=sx_door_html;
  ptr +="</div>";
  ptr +="</div>";
  ptr +="<div class=\"data dx_door\">";
  ptr +="<div class=\"side-by-side icon\">";
  if (dx_door_state==1) ptr +="<img src=\"img/dx_door-green.svg\" alt=\"DX Door\" height=\"50\" width=\"100\" />";
  else ptr +="<img src=\"img/dx_door-red.svg\" alt=\"DX Door\" height=\"50\" width=\"100\" />";
  ptr +="</div>";
  ptr +="<div class=\"side-by-side text\">";
  ptr +="Porta DX";
  ptr +="</div>";
  ptr +="<div class=\"side-by-side reading\">";
  ptr +=dx_door_html;
  ptr +="</div>";
  ptr +="</div>";
  ptr +="";
  ptr +="<div class=\"data time\">";
  ptr +="<div class=\"side-by-side icon\">";
  ptr +="<img src=\"img/clock.svg\" alt=\"Time\" height=\"40\" width=\"40\" />";
  ptr +="</div>";
  ptr +="<div class=\"side-by-side text\">";
  ptr +="Ora";
  ptr +="</div>";
  ptr +="<div class=\"side-by-side reading\">";
  ptr +=timeClient.getFormattedTime();
  ptr +="</div>";
  ptr +="</div>";
  ptr +="</div>";
  ptr +="<form id=\"F1\" action=\"settings\" name=\"F1\">";
  ptr +="<input class=\"button\" type=\"submit\" value=";
  ptr +="\"Impostazioni\" />";
  ptr +="</form><br />";
  ptr +="</body>";
  ptr +="</html>";
  return ptr;
}
