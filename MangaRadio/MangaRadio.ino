
#include <NeoPixelBus.h>
#include "Audio.h"
#include "SD.h"
#include "FS.h"
#include "string.h"
extern "C"
{
#include "hal_i2c.h"
#include "tinyScreen128x64.h"
}



#define I2S_BCLK    5
#define I2S_LRC     25
#define I2S_DOUT    26
#define SDA         18
#define SCL         23
#define I2SN (i2s_port_t)0


#define LED2        GPIO_NUM_4

#define PUSH        GPIO_NUM_0
#define ROTARY_A    GPIO_NUM_32
#define ROTARY_B    GPIO_NUM_19

#define SDD GPIO_NUM_34     
#define SD_CS         13
#define SPI_MOSI      15
#define SPI_MISO      2
#define SPI_SCK       14
//////////////////////////////
// NeoPixel led control
/////////////////////////////
#define PixelCount 1
#define PixelPin 22
RgbColor RED(255, 0, 0);
RgbColor GREEN(0, 255, 0);
RgbColor BLUE(0, 0, 255);
RgbColor YELLOW(255, 128, 0);
RgbColor WHITE(255, 255, 255);
RgbColor BLACK(0, 0, 0);
NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> strip(PixelCount, PixelPin);

Audio audio;
File root;
File F;
File file;
int stop;
int Napp;



#define Nrad    0
#define Nplay   1
#define Nwifi   2
int vol = 10;
int pvol;
#define volMAX 21
bool mp3ON;
bool sdON = false;
bool dofwd = false;
uint32_t sampleRate;

TaskHandle_t Tencoder;
int N = 0;
int PN = -1;
char b[40];
uint8_t c[20];

int R;   

int P = 0;
char title[80];
char artist[80];
char *bta;
bool toDisplay = false;
int station;
int previousStation;
int MS;

char* linkS;
/////////////////////////////////////////////////////////////////////////
//  defines how many stations in SPIFFS file "/linkS"
//
////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////////
// IMPROV 
uint8_t data[512];


enum ImprovSerialType : uint8_t {
  TYPE_CURRENT_STATE = 0x01,
  TYPE_ERROR_STATE = 0x02,
  TYPE_RPC = 0x03,
  TYPE_RPC_RESPONSE = 0x04
};


#define IMPROV_SERIAL_VERSION 1 

enum Error : uint8_t {
  ERROR_NONE = 0x00,
  ERROR_INVALID_RPC = 0x01,
  ERROR_UNKNOWN_RPC = 0x02,
  ERROR_UNABLE_TO_CONNECT = 0x03,
  ERROR_NOT_AUTHORIZED = 0x04,
  ERROR_UNKNOWN = 0xFF
};


enum State : uint8_t {
  STATE_STOPPED = 0x00,
  STATE_AWAITING_AUTHORIZATION = 0x01,
  STATE_AUTHORIZED = 0x02,
  STATE_PROVISIONING = 0x03,
  STATE_PROVISIONED = 0x04
};

enum Command : uint8_t {
  UNKNOWN = 0x00,
  WIFI_SETTINGS = 0x01,
  IDENTIFY = 0x02,
  GET_CURRENT_STATE = 0x02,
  GET_DEVICE_INFO = 0x03,
  GET_WIFI_NETWORKS = 0x04,
  BAD_CHECKSUM = 0xFF
};



const uint8_t H[7] = {'I','M','P','R','O','V', IMPROV_SERIAL_VERSION};



//Send Current state
void set_state(State S)
{
  uint8_t checksum;
  for(int i=0;i<6;i++)data[i] = H[i];   // header : IMPROV
  data[6] = IMPROV_SERIAL_VERSION;
  data[7] = TYPE_CURRENT_STATE;
  data[8] = 1;                          // length
  data[9] = S;
  checksum = 0;
  for(int i=0;i<10;i++) checksum += data[i];
  data[10] = checksum;
  Serial.write(data, 11); 
}

//Send Error state
void set_error(Error E)
{
  uint8_t checksum;
  for(int i=0;i<6;i++)data[i] = H[i];    // header : IMPROV
  data[6] = IMPROV_SERIAL_VERSION;
  data[7] = TYPE_ERROR_STATE;
  data[8] = 1;                           // length
  data[9] = E;
  checksum = 0;
  for(int i=0;i<10;i++) checksum += data[i];
  data[10] = checksum;
  Serial.write(data, 11);     
}

//Send RPC Response
void send_response(uint8_t* R)
{
  uint8_t checksum;
  int i, im;
  uint8_t L;
  L = R[1] + 2 ;
  for(int i=0;i<6;i++)data[i] = H[i];    // header : IMPROV
  data[6] = IMPROV_SERIAL_VERSION;
  data[7] = TYPE_RPC_RESPONSE;
  data[8] = L ;                         // length
  for(i=0;i<L;i++)data[9+i] = R[i];
  im = L + 9;
  checksum = 0;
  for(i=0;i<im;i++) checksum += data[i];  
  data[im] = checksum ;  
  Serial.write(data, im+1); 
}

//Build RPC response
uint8_t * build_rpc_response(Command C, char** S, int n)
{
  static uint8_t D[128];
  int i, j;
  uint32_t length;
  uint8_t len;

  D[0] = C;
  if(S == NULL)
  {
    D[1] = 0;
    return D;
  }
  j = 2;
  length = 0;
  for(i=0;i<n;i++)
  {
    len = strlen(S[i]);
    length += len + 1;
    D[j++] = len;
    for(int k=0;k<len;k++)D[j++] = S[i][k];
  }
  D[1] = length ; 
 
  return D;
}
// testing packet integrity (header + checksum)
bool parse_improv_serial_packet(uint8_t* b)
{
  bool R = true;
  int L;
  uint8_t checksum;

//verifying header  
  uint8_t H[] = {'I', 'M', 'P', 'R', 'O', 'V'};
  for(int i=0;i<6;i++)
  {
    if(b[i] != H[i])R = false;
  }
  if(b[6] != IMPROV_SERIAL_VERSION)R = false;
 
  return R;  
}


void wifi(void)
{
   clearBuffer();
   drawStrC(10,"Initializing");
   drawStrC(30, "Wifi");
   drawStrC(40, "credentials");
   sendBuffer();
///////////////////////////////////////////////////////////////////////////////////////////////
// Wifi credentials setup
//    using Improv
//
//
//    SSID and password saved in /ssid and /pwd (SPIFFS files)
///////////////////////////////////////////////////////////////////////////////////////////////

char ssid[80];
char pwd[80];
  String s;
  char buf[255];
  uint8_t R[2];
  const char* sp;
  bool credOK ;
  State state;
  Command com;
  char* S[3];   
  int i, j, n, l;
  uint32_t now;
  File f, ff;
////////////////////////////////////////////////////////////////////////////////////////////////  
//
////////////////////////////////////////////////////////////////////////////////////////////////  
state = STATE_AUTHORIZED;

    
//   Serial.setTimeout(1000);    
    do
    {
// user stop (long push)
    if(P == 2) 
    {
    Napp = -1;
    P = 0;
    return;
    } 
////////////////////////////////////
//get packet from client
////////////////////////////////////
 

//get header   
    for(i=0;i<7;i++)
    {
      Serial.readBytes(buf+i, 1);
      
////////////////////////////////
// user stop (long push)
    if(P == 2) 
    {
    Napp = -1;
    P = 0;
    return;
    }
//////////////////////////////////     
      if(buf[i] != H[i])i--;
    }
    Serial.readBytes(buf+7, 2);
     
    int l = buf[8] +1;           // data length

    Serial.readBytes(buf+9, l);
//get data + checksum

//test packet integrity
   if(!parse_improv_serial_packet((uint8_t*)buf)) 
   {

    set_error(ERROR_INVALID_RPC);
    break;
   }
//
   com = (Command)buf[9];
   switch(com)
   {
    case WIFI_SETTINGS:
    {
//retrieve ssid and password      
      for(i=0;i<buf[11];i++)ssid[i] = buf[12+i];
      ssid[i] = 0;
      for(j=0;j<buf[12+i];j++)pwd[j] = buf[13+i+j];
      pwd[j] = 0;

//SSID and password stored in files
      f = SPIFFS.open("/ssid", "w");
      f.write((uint8_t*)ssid, strlen(ssid)+1);
      f.close();
      f = SPIFFS.open("/pwd", "w");     
      f.write((uint8_t*)pwd,strlen(pwd)+1 );
      f.close();  

// initializing WiFi credentials     
      WiFi.mode(WIFI_STA);
      WiFi.begin(ssid, pwd);
//change state
      state = STATE_PROVISIONING;
      set_state(state);  
      while(WiFi.status() != WL_CONNECTED) delay(500);
      state = STATE_PROVISIONED;
      set_state(state);  
      R[0] = WIFI_SETTINGS;
      R[1] = 0;
      send_response(R);
      break;        
    }
    case GET_CURRENT_STATE:
    {
      set_state(state);
      break;
    }
    case GET_DEVICE_INFO:
    {
      char* info[] = {"Radio", "2022.10.15", "ESP32", "MN Cast (raspiaudio)"};
      uint8_t* d = build_rpc_response(com, info, 4);
      send_response(d);
      break;
    }
    case GET_WIFI_NETWORKS:
    {
 //scan 
    int  nSsid = WiFi.scanNetworks();

    for(i=0;i<nSsid;i++)
    {
      String ST = WiFi.SSID(i);
      S[0] = (char*)ST.c_str();
      sprintf(buf, "%d", WiFi.RSSI(i));
      S[1] = buf;
      S[2] = "YES";
      uint8_t* d = build_rpc_response(GET_WIFI_NETWORKS, S, 3);
      send_response(d);
    }

      uint8_t* d = build_rpc_response(GET_WIFI_NETWORKS, NULL, 0);     
      send_response(d);  
      break;     
    }
    default:
    {
      
    }
   }

    }while(state != STATE_PROVISIONED);  
   
   delay(2000);
   Napp = -1;  
}

int maxStation(void)
{
  File ln = SPIFFS.open("/linkS", FILE_READ);
  uint8_t c;
  int m = 0;
  int t;
  t = ln.size();
  int i = 0;
  do 
  {
    while(c != 0x0a){ln.read(&c, 1); i++;}
    c = 0;
    m++;
  }while(i < t);
  ln.close();
  return m;  
}
char* Rlink(int st)
{
  int i;
  static char b[80];
  File ln = SPIFFS.open("/linkS", FILE_READ);
  i = 0;
  uint8_t c;
  while(i != st)
  {
    while(c != 0x0a)ln.read(&c, 1);
    c = 0;
    i++;
  }
  i = 0;
  do
  {
    ln.read((uint8_t*)&b[i], 1);
    i++;
  }while(b[i-1] != 0x0a);
  b[i-1] = 0;
  ln.close();
  return b;
}
/////////////////////////////////////////////////////////////////////////////////
//  gets station name from SPIFFS file "/namS"
//
/////////////////////////////////////////////////////////////////////////////////
char* Rname(int st)
{
  int i;
  static char b[20];
  File ln = SPIFFS.open("/nameS", FILE_READ);
  i = 0;
  uint8_t c;
  while(i != st)
  {
    while(c != 0x0a)ln.read(&c, 1);
    c = 0;
    i++;
  }
  i = 0;
  do
  {
    ln.read((uint8_t*)&b[i], 1);
    i++;
  }while(b[i-1] != 0x0a);
  b[i-1] = 0;
  ln.close();
  return b;
}
void radios(void)
{
   char ssid[80];
char pwd[80];
String s;
char buf[255];
uint8_t R[2];
const char* sp;
bool credOK ;
State state;
Command com;
char* S[3];   
int i, j, n, l;
uint32_t now;
File f;

   clearBuffer();
   drawBigStrC(24,"Radio");
   sendBuffer();
   delay(2000);
/////////////////////////////////////////////////
// wifi connection
/////////////////////////////////////////////////
  if(!SPIFFS.exists("/ssid"))
   {

    wifi();
    delay(2000);    
   }
   f = SPIFFS.open("/ssid", "r");
   l = f.read((uint8_t*)ssid, 80);
   Serial.println(ssid);
   f.close();
   
   f = SPIFFS.open("/pwd", "r");
   f.read((uint8_t*)pwd, 80);
   Serial.println(pwd);
   f.close();   
      
// connecting WiFi     
      WiFi.mode(WIFI_STA);
      WiFi.begin(ssid, pwd);  
      now = millis();
      while((WiFi.status() != WL_CONNECTED) && ((millis() - now) < 10000))delay(500);
      Serial.println("after?");
      Serial.println(WL_CONNECTED);
      Serial.println(WiFi.status());
      if(WiFi.status() != WL_CONNECTED) 
      { 
      wifi();  
      delay(2000);
      }

// variables de travail
      previousStation = -1;
      MS = maxStation()-1;
 //     Serial.print("max ===>  ");Serial.println(MS);

/////////////////////////////////////////////////////////////
// recovers params (station & vol)
///////////////////////////////////////////////////////////////
        char b[4];
        File ln = SPIFFS.open("/station", "r");
        ln.read((uint8_t*)b, 2);
        b[2] = 0;
        station = atoi(b);
        Serial.print("station "); Serial.println(station);
        ln.close();       
        ln = SPIFFS.open("/volume", "r");
        ln.read((uint8_t*)b, 2);
        b[2] = 0;
        vol = atoi(b);
        ln.close();
        Serial.print("volume init   "); Serial.println(vol);
        pvol = vol;
        audio.setVolume(vol);
        Napp = Nrad;
        return;  
}




void encoder(void* data)
{ 
  int va, vb;
  int32_t ta = 0;
  int32_t tb = 0;
  uint32_t to = 0;
  uint32_t dt;
  int v;
  while(true)
  { 
////////////////////////////////////////////     
// Push
//   <500 ms ==> P = 1
//   >500 ms ==> P = 2   
////////////////////////////////////////////  
    v = gpio_get_level(PUSH);
    if(to == 0)
    {
      if(v == 0) to = millis();
    }
    else
    {
      if(v == 1)
      {
        dt = millis() - to;
        if(dt < 100) P = 0;
        else if((millis() - to) < 500) P = 1 ;
        else  P = 2;
        to = 0;
      }
    }
/////////////////////////////////////////////////
// rotactor
/////////////////////////////////////////////////    
    va = gpio_get_level(ROTARY_A);
    vb = gpio_get_level(ROTARY_B);
    if((va == 1) && (ta == -1))ta = 0;
    if((vb == 1) && (tb == -1))tb = 0;
    if((va == 0) && (ta == 0)) ta = millis();
    if((vb == 0) && (tb == 0)) tb = millis();
    if((ta > 0) && (tb > 0))
    {
      dt = ta - tb;     
      if(ta > tb) N++; else N--;
//      if((ta > tb) && (dt < 20)) N++; else N--;
//      if((ta < tb) && (dt > -20))N--; else N++;
      Serial.println(N);
      ta=tb=-1;
    }
    delay(5);
  }
}


void setup() {  
Serial.begin(115200);
while(!Serial)delay(100);
 
////////////////////////////////////////////////////////////////
// init NeoPixel led handle
///////////////////////////////////////////////////////////////
  strip.Begin();  
///////////////////////////////////////////////////////////////
// init spdif led
///////////////////////////////////////////////////////////////
  gpio_reset_pin(LED2);
  gpio_set_direction(LED2, GPIO_MODE_OUTPUT); 
///////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////
//init rotactor
///////////////////////////////////////////////////////////////

  gpio_reset_pin(PUSH);
  gpio_set_direction(PUSH, GPIO_MODE_INPUT); 
  gpio_set_pull_mode(PUSH, GPIO_PULLUP_ONLY); 

  gpio_reset_pin(ROTARY_A);
  gpio_set_direction(ROTARY_A, GPIO_MODE_INPUT); 
  gpio_set_pull_mode(ROTARY_A, GPIO_PULLUP_ONLY); 

  gpio_reset_pin(ROTARY_B);
  gpio_set_direction(ROTARY_B, GPIO_MODE_INPUT);   
  gpio_set_pull_mode(ROTARY_B, GPIO_PULLUP_ONLY); 
  
//////////////////////////////////////////////////////////////  
// init tiny screen
///////////////////////////////////////////////////////////////
   tinySsd_init(SDA, SCL, 0, 0x3C, 1);
//////////////////////////////////////////////////////////////
   clearBuffer();
   sendBuffer();

    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    if(!SPIFFS.begin())Serial.println("Erreur SPIFFS");



//////////////////////////////////////////////////////////////
// init encoder handler as a core#0 task
/////////////////////////////////////
   xTaskCreatePinnedToCore(encoder, "encoder", 5000, NULL, 5, &Tencoder, 0);



 
///////////////////////////////////////////////////////////////
// Menu
//////////////////////////////////////////////////////////////    
   clearBuffer();
   drawBigStrC(24,"Ros&Co");
   sendBuffer();
   delay(1000);
   clearBuffer();
   drawBigStrC(16,"Manga");
   drawBigStrC(40,"Cast");
   sendBuffer();
   delay(2000);
   Napp = Nrad;
   radios();
   title[0] = 0;
   artist[0] = 0;
}

void loop() {

audio.loop();



///////////////////////////////////////////
//volume
///////////////////////////////////////////  
     if((N - PN) != 0)
     {
     vol = vol + (N - PN);
     if(vol > volMAX) vol = volMAX;
     if(vol < 0) vol = 0;
     printf("%d  %d  %d\n",vol, N, PN);    
     PN = N;
     audio.setVolume(vol); 
     toDisplay = true;
     }
///////////////////////////////////////////////
// to store volume every 2 sec if needed
///////////////////////////////////////////////
 //
  if(((millis() % 2000) < 10) && (pvol != vol))
     {
     char b[4];
     File ln = SPIFFS.open("/volume", "w");
     sprintf(b,"%02d",vol);      
     ln.write((uint8_t*)b, 2);
     ln.close();  
     pvol = vol;    
     Serial.print("volume stored   "); Serial.println(vol); 
     }
    





///////////////////////////////////////////////////////////////
//station change
///////////////////////////////////////////////////////////////
   if(P == 1)
      {
        P = 0;
        station++;
        if(station > MS) station = 0;      
      }

   if(station != previousStation)
   {     
      char b[4];
      Serial.print("STATION2   "); Serial.println(station);
      sprintf(b,"%02d",station);
      File ln = SPIFFS.open("/station", "w");
      ln.write((uint8_t*)b, 2);
      ln.close(); 
            
      i2s_stop(I2SN);
      i2s_zero_dma_buffer(I2SN);
      delay(500);
      i2s_start(I2SN);      
      audio.stopSong();
      
      //delay(100);
      linkS = Rlink(station);
      Serial.print("===>  "); Serial.println(linkS);
      audio.connecttohost(linkS);
      previousStation = station;

      toDisplay = true;
    }
    if(toDisplay == true)
    {
      clearBuffer();
      drawStrC(2, "MN Cast");
      drawHLine(14, 0,128);      
      
      if(strlen(Rname(station)) < 9)
      {      
      drawBigStrC(24, Rname(station));
      }
      else
      {
        char n[32];
        strcpy(n, Rname(station));
        n[16] = 0;
        drawStrC(34,n);
      }
      drawRectangle(56, 14, 6, 100*vol/volMAX);
      drawFrame(56, 14, 6,100);     
 //     drawHLine(23, 0, 128);
 //     drawHLine(50, 0, 128);      
      sendBuffer(); 
      toDisplay = false;
    }
      if(P == 2)
         {
          P = 0;                
          audio.stopSong();
          Napp = -1;       
          }                  
  }

// optional
void audio_info(const char *info){
    Serial.print("info        "); Serial.println(info);
    if(strstr(info, "SampleRate=") > 0) 
    {
    sscanf(info,"SampleRate=%d",&sampleRate);
    printf("==================>>>>>>>>>>%d\n", sampleRate);
    }
}
void audio_id3data(const char *info){  //id3 metadata
    Serial.print("id3data     ");Serial.println(info);
    int st = strncmp(info, "Title: ",7);
    if(st == 0)
    {
      bta = (char*)info + 7;
      strcpy(title, bta);
      toDisplay = true;
    }   
    st = strncmp(info, "Artist: ",8);
    if(st == 0)
    {
      bta = (char*)info + 8;
      strcpy(artist, bta);
      toDisplay = true;
    }   
}
void audio_eof_mp3(const char *info){  //end of file
    Serial.print("eof_mp3     ");Serial.println(info);mp3ON = false;
}
void audio_showstation(const char *info){
    Serial.print("station     ");Serial.println(info);
}
void audio_showstreaminfo(const char *info){
    Serial.print("streaminfo  ");Serial.println(info);
    Serial.println("top");
}
void audio_showstreamtitle(const char *info){
    Serial.print("streamtitle ");Serial.println(info);
}
void audio_bitrate(const char *info){
    Serial.print("bitrate     ");Serial.println(info);
    
}
void audio_commercial(const char *info){  //duration in sec
    Serial.print("commercial  ");Serial.println(info);
}
void audio_icyurl(const char *info){  //homepage
    Serial.print("icyurl      ");Serial.println(info);
}
void audio_lasthost(const char *info){  //stream URL played
    Serial.print("lasthost    ");Serial.println(info);
}
void audio_eof_speech(const char *info){
    Serial.print("eof_speech  ");Serial.println(info);
}
