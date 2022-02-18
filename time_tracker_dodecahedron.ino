#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>

#include "Wire.h"
#include "I2Cdev.h"
#include "MPU6050.h"
MPU6050 mpu;

///////////////////////////////BASIC SETINGS////////////////////////////
String ssid_list[] =      {"Network_Name_1",  "Network_Name_2", "Network_Name_3"}; //Wifi Network Name
String password_list[] =  {"Network_Key_1",   "Network_Key_2",  "Network_Key_3"};  //Wifi Network Key
const char *authorization = "Basic Y2WG...lGsA=="; //ex: "Basic ABC=="
int16_t epsilon = 6;
const int side_values[12][3] = {
   {-2,    0,  46}, 
   {21,  -38,  21}, 
   {-29, -32,  20}, 
   {-40,  18,  17}, 
   {5,    44,  19}, 
   {43,    7,  21}, 
   {2,     0, -54}, 
   {-42,  -8, -29}, 
   {-4,  -44, -27}, 
   {40,  -19, -25}, 
   {32,   31, -27}, 
   {-19,  38, -27}
  };

//-----------------------------description-----------------char pid
const char *trackers[12][2] = {{"Email check",             "172635855"},//0
                              {"Meeting",                  "172635927"},//1
                              {"Programming",              "172635927"},//2
                              {"Reading",                  "163047428"},//3
                              {"Online training courses",  "163047428"},//4
                              {"STOP",                     "\"\""     },//5
                              {"Bug fix",                  "\"\""     },//6
                              {"UI",                       "\"\""     },//7
                              {"Call",                     "\"\""     },//8
                              {"Music",                    "\"\""     },//9
                              {"Exercise",                 "\"\""     },//10
                              {"Relax",                    "\"\""     }};//11
///////////////////////////////BASIC SETINGS////////////////////////////


///////////////////////////////Do not change////////////////////////////
const char *host = "api.track.toggl.com"; // Domain to Server: google.com NOT https://google.com 
const int httpsPort = 443; //HTTPS PORT (default: 443)
///////////////////////////////Do not change////////////////////////////

///////////////////////////////Variables////////////////////////////

String datarx; //Received data as string
int httpsClientTimeout = 5000; //in millis

int16_t ax, ay, az;
int16_t gx, gy, gz;


int last_60_measurements[60] = {};
//#3 if data is sent, wait till position changed:
int sent_dodecahedron_side = -1;//=0
///////////////////////////////Variables////////////////////////////

void setup() {
  Wire.begin();
  Serial.begin(9600);
  
  //smart wifi connection
  WiFi.mode(WIFI_OFF);
  delay(1000);
  WiFi.mode(WIFI_STA);
  
  String ssid = "";
  while (WiFi.status() != WL_CONNECTED) {
    for (byte x = 0; x < (sizeof(ssid_list) / sizeof(ssid_list[0])); x++) {
      ssid = ssid_list[x];
      String password = password_list[x];

      WiFi.begin(ssid, password);
      Serial.println("");
      Serial.print("Connecting to wifi: ");
      Serial.print(ssid);
      int i = 0;
      while (WiFi.status() != WL_CONNECTED && i<14) {
        //on average, it connects in 7 attempts
        delay(500);
        i++;
        Serial.print(".");
      }
      
      if(WiFi.status() == WL_CONNECTED){
        break;
      }
    }
  }
  Serial.print("Connected to wifi: ");
  Serial.println(ssid);
  
  //mpu init
  mpu.initialize();
  Serial.println(mpu.testConnection() ? "MPU6050 OK" : "MPU6050 FAIL");
  delay(1000);

  //filling the array with -1 value
  for(int i=0; i<60; i++){ last_60_measurements[i]=-1;}

}



void loop() {
  mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
  // (-32768.. 32767) 32768/327 (-100.. 100)
  Serial.print(ax/327); Serial.print('\t');
  Serial.print(ay/327); Serial.print('\t');
  Serial.print(az/327); Serial.print('\t');
  
  //#1Checking of the side of the dodecahedron:
  int dodecahedron_side = get_dodecahedron_side(ax/327, ay/327, az/327);
 
  Serial.print("-> "); Serial.print(dodecahedron_side); Serial.print('\t');

  //#2Get if the dodecahedron is on stable position:
  // Need to check if the dodecahedron on same position for 3 seconds 
  // and then start timer
  //We take data each 50 milliseconds. 3000/50 = 60
  for (int i=0;i<59;i++){
    last_60_measurements[i]=last_60_measurements[i+1];//0=1,1=2..
  }
  last_60_measurements[59] = dodecahedron_side;
  //check if stable
  //#3 if data is sent, wait till position changed: sent_dodecahedron_side
  
  if(checkIfCubeStable(last_60_measurements)){
    Serial.print("Stable position"); Serial.print('\t');
    if(sent_dodecahedron_side!=dodecahedron_side){
      Serial.print("SEND DATA!"); Serial.print('\t');
      sent_dodecahedron_side = dodecahedron_side; 
      //callhttps_start_time_entry(trackers[sent_dodecahedron_side][0], trackers[sent_dodecahedron_side][1]);
      
      if(trackers[sent_dodecahedron_side][0] != "STOP"){
        Serial.print("callhttps_start_time_entry"); Serial.print('\t');
        callhttps_start_time_entry(trackers[sent_dodecahedron_side][0], trackers[sent_dodecahedron_side][1]);
        //callhttps_start_time_entry(trackers[sent_dodecahedron_side][0], trackers[sent_dodecahedron_side][1], trackers[sent_dodecahedron_side][2]);
      }else{
        Serial.println("callhttps_stop_time_entry");
        String timeEntry_id = callhttps_stop_time_entry_p1();
        callhttps_stop_time_entry_p2(timeEntry_id);
      }
    }
    
  }

  
  Serial.println("");
  delay(50);
  
}

//mpu functions
bool checkIfCubeStable(int measurements[]){
  for (int i=0;i<60;i++){
    if(i==59){return true;};
    if(measurements[i] != measurements[i+1]){return false;};
  }
  
}

int get_dodecahedron_side(int16_t ax, int16_t ay, int16_t az){ //return the dedocahedron side 0-11, else -1
  for(int i=0; i<12; i++){
    if((side_values[i][0]-epsilon < ax && ax < side_values[i][0]+epsilon) &&
       (side_values[i][1]-epsilon < ay && ay < side_values[i][1]+epsilon) &&
       (side_values[i][2]-epsilon < az && az < side_values[i][2]+epsilon)){//44 < 50 < 66
        return i;
    } 
  }
  return -2;
};

//wi-fi functions
void callhttps_start_time_entry(const char* description, const char* pid){
  WiFiClientSecure httpsClient;
  httpsClient.setInsecure(); // this is the magical line that makes everything work
  
  httpsClient.setTimeout(httpsClientTimeout);
  delay(1000);
  int retry = 0;
  while ((!httpsClient.connect(host, httpsPort)) && (retry < 15)) {
    delay(100);
    Serial.print(".");
    retry++;
  }
  if (retry == 15) {Serial.println("Connection failed");}
  else {Serial.println("Connected to Server");}
  
  Serial.println("Request_start{");
  String req = String("POST /api/v8/time_entries/start HTTP/1.1\r\n")
        + "Host: api.track.toggl.com\r\n"
        +"Content-Type: application/json\r\n"
        +"Authorization: " + authorization + "\r\n"
        +"Content-Length: " + (77 + strlen(description) + strlen(pid)) + "\r\n\r\n"
        
        +"{\"time_entry\":{\"description\":\"" + description + "\",\"tags\":[],\"pid\":" + pid + ",\"created_with\":\"time_cube\"}}" + "\r\n\r\n";
  
  Serial.println(req);
  httpsClient.print(req);
  Serial.println("}Request_end");
  
  Serial.println("line{");
  while (httpsClient.connected()) {
    String line = httpsClient.readStringUntil('\n');
    Serial.print(line);
    if (line == "\r") {
      break;
    }
  }
  Serial.println("}line");
  
  Serial.println("datarx_start{");
  while (httpsClient.available()) {
    datarx += httpsClient.readStringUntil('\n');
  }
  Serial.println(datarx);
  Serial.println("}datarx_end");
  datarx = "";
}

String callhttps_stop_time_entry_p1(){
  WiFiClientSecure httpsClient;
  httpsClient.setInsecure(); // this is the magical line that makes everything work
  
  httpsClient.setTimeout(httpsClientTimeout);
  delay(1000);
  int retry = 0;
  while ((!httpsClient.connect(host, httpsPort)) && (retry < 15)) {
    delay(100);
    Serial.print(".");
    retry++;
  }
  if (retry == 15) {Serial.println("Connection failed");}
  else {Serial.println("Connected to Server");}
  
  Serial.println("Request_start{");
  
  //Get the information about running time entry. Especcially we need time entry id.
  String req = String("GET /api/v8/time_entries/current HTTP/1.1\r\n")
        +"Host: api.track.toggl.com\r\n"
        +"Authorization: " + authorization +"\r\n\r\n";
  
  Serial.println(req);
  httpsClient.print(req);
  Serial.println("}Request_end");
  
  Serial.println("line{");
  while (httpsClient.connected()) {
    String line = httpsClient.readStringUntil('\n');
    Serial.print(line);
    if (line == "\r") {
      break;
    }
  }
  Serial.println("}line");
  
  Serial.println("datarx_start{");
  while (httpsClient.available()) {
    datarx += httpsClient.readStringUntil('\n');
  }
  Serial.println(datarx);
  Serial.println("}datarx_end");
  String timeEntry_id = datarx.substring(14,24);
  datarx = "";
  Serial.println("timeEntry_id:"+timeEntry_id);
  
  return timeEntry_id;
}

void callhttps_stop_time_entry_p2(String timeEntry_id){
  WiFiClientSecure httpsClient;
  httpsClient.setInsecure(); // this is the magical line that makes everything work
  
  httpsClient.setTimeout(15000);
  delay(1000);
  int retry = 0;
  while ((!httpsClient.connect(host, httpsPort)) && (retry < 15)) {
    delay(100);
    Serial.print(".");
    retry++;
  }
  if (retry == 15) {Serial.println("Connection failed");}
  else {Serial.println("Connected to Server");}
  
  Serial.println("Request_start{");
  //Here we will stop the running time entry by using its id:
  String req = String("PUT /api/v8/time_entries/"+timeEntry_id+"/stop HTTP/1.1\r\n")
        +"Host: api.track.toggl.com\r\n"
        +"Authorization: " + authorization +"\r\n"
         +"Content-Length: " + "0" + "\r\n\r\n";

  Serial.println(req);
  httpsClient.print(req);
  Serial.println("}Request_end");
  
  Serial.println("line{");
  while (httpsClient.connected()) {
    String line = httpsClient.readStringUntil('\n');
    Serial.print(line);
    if (line == "\r") {
      break;
    }
  }
  Serial.println("}line");
  
  Serial.println("datarx_start{");
  while (httpsClient.available()) {
    datarx += httpsClient.readStringUntil('\n');
  }
  Serial.println(datarx);
  Serial.println("}datarx_end");
  datarx = "";
}
