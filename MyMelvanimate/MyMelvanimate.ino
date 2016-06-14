/*-------------------------------------------------------------------------------------------------------

              ESP8266 & Arduino IDE
              Animation software to control WS2812 - several requirements see 



  Sticilface - Beerware licence

--------------------------------------------------------------------------------------------------------*/

//#define DEBUG_ESP_PORT Serial

#include <FS.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoOTA.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <NeoPixelBus.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#define MQTT_MAX_PACKET_SIZE 256 //  this overrides the default packet size for pubsubclient packet.. otherwise it is 128 bytes, too small.  
#include <PubSubClient.h>
#include <Adafruit_GFX.h>
#include <ESPmanager.h>
#include <Melvanimate.h>


//  these are default effect... comment them out here and in setup to remove.  Thats it. 
#include "effects/SwitchEffect.h"
#include "effects/SimpleEffect.h"
#include "effects/DMXEffect.h"
#include "effects/AdalightEffect.h"
#include "effects/UDPEffect.h"
#include "effects/RainbowChase.h"
#include "effects/Shapes.h"
#include "effects/ColorBlend.h"
#include "effects/BeatsTest.h"
#include "effects/EQvisualiser.h"
#include "effects/White.h"

const uint16_t defaultpixelcount =  20;
//const char* devicename = "nodemcu";  
const char* http_username = "admin";
const char* http_password = "admin";


AsyncWebServer HTTP(80);
Melvanimate lights(HTTP, defaultpixelcount);  //  METHOD defaults to use RX pin, GPIO3, using DMA method... to change see mybus.h within Melvanimate
ESPmanager manager(HTTP, SPIFFS ); 

using namespace helperfunc; // used for things like dim. 

// WEB HANDLER IMPLEMENTATION
class SPIFFSEditor: public AsyncWebHandler {
  private:
    String _username;
    String _password;
    bool _uploadAuthenticated;
  public:
    SPIFFSEditor(String username=String(), String password=String()):_username(username),_password(password),_uploadAuthenticated(false){}
    bool canHandle(AsyncWebServerRequest *request){
      if(request->method() == HTTP_GET && request->url() == "/edit" && (SPIFFS.exists("/edit.htm") || SPIFFS.exists("/edit.htm.gz")))
        return true;
      else if(request->method() == HTTP_GET && request->url() == "/list")
        return true;
      else if(request->method() == HTTP_GET && (request->url().endsWith("/") || SPIFFS.exists(request->url()) || (!request->hasParam("download") && SPIFFS.exists(request->url()+".gz"))))
        return true;
      else if(request->method() == HTTP_POST && request->url() == "/edit")
        return true;
      else if(request->method() == HTTP_DELETE && request->url() == "/edit")
        return true;
      else if(request->method() == HTTP_PUT && request->url() == "/edit")
        return true;
      return false;
    }

    void handleRequest(AsyncWebServerRequest *request){
      if(_username.length() && (request->method() != HTTP_GET || request->url() == "/edit" || request->url() == "/list") && !request->authenticate(_username.c_str(),_password.c_str()))
        return request->requestAuthentication();

      if(request->method() == HTTP_GET && request->url() == "/edit"){
        request->send(SPIFFS, "/edit.htm");
      } else if(request->method() == HTTP_GET && request->url() == "/list"){
        if(request->hasParam("dir")){
          String path = request->getParam("dir")->value();
          Dir dir = SPIFFS.openDir(path);
          path = String();
          String output = "[";
          while(dir.next()){
            File entry = dir.openFile("r");
            if (output != "[") output += ',';
            bool isDir = false;
            output += "{\"type\":\"";
            output += (isDir)?"dir":"file";
            output += "\",\"name\":\"";
            output += String(entry.name()).substring(1);
            output += "\"}";
            entry.close();
          }
          output += "]";
          request->send(200, "text/json", output);
          output = String();
        }
        else
          request->send(400);
      } else if(request->method() == HTTP_GET){
        String path = request->url();
        if(path.endsWith("/"))
          path += "index.htm";
        request->send(SPIFFS, path, String(), request->hasParam("download"));
      } else if(request->method() == HTTP_DELETE){
        if(request->hasParam("path", true)){
          ESP.wdtDisable(); SPIFFS.remove(request->getParam("path", true)->value()); ESP.wdtEnable(10);
          request->send(200, "", "DELETE: "+request->getParam("path", true)->value());
        } else
          request->send(404);
      } else if(request->method() == HTTP_POST){
        if(request->hasParam("data", true, true) && SPIFFS.exists(request->getParam("data", true, true)->value()))
          request->send(200, "", "UPLOADED: "+request->getParam("data", true, true)->value());
        else
          request->send(500);
      } else if(request->method() == HTTP_PUT){
        if(request->hasParam("path", true)){
          String filename = request->getParam("path", true)->value();
          if(SPIFFS.exists(filename)){
            request->send(200);
          } else {
            File f = SPIFFS.open(filename, "w");
            if(f){
              f.write(0x00);
              f.close();
              request->send(200, "", "CREATE: "+filename);
            } else {
              request->send(500);
            }
          }
        } else
          request->send(400);
      }
    }

    void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
      if(!index){
        if(!_username.length() || request->authenticate(_username.c_str(),_password.c_str()))
          _uploadAuthenticated = true;
        request->_tempFile = SPIFFS.open(filename, "w");
      }
      if(_uploadAuthenticated && request->_tempFile && len){
        ESP.wdtDisable(); request->_tempFile.write(data,len); ESP.wdtEnable(10);
      }
      if(_uploadAuthenticated && final)
        if(request->_tempFile) request->_tempFile.close();
    }
};

void setup()
{

  Serial.begin(115200);
  Serial.println("");
  Serial.println("Melvanimate - WS2812 control");



  SPIFFS.begin(); 

  manager.begin();

//  Add effects to the manager.
  lights.Add("Off",          new SwitchEffect( offFn), true);        //  **  Last true indicates this is the default effect... ie... off...
  lights.Add("SimpleColor",  new SimpleEffect(SimpleColorFn));       
  lights.Add("RainbowChase", new RainbowChase); 
  lights.Add("Shapes",       new Shapes); 
  lights.Add("Adalight",     new AdalightEffect(Serial, 115200));   //  default serial device and baud. 
  lights.Add("UDP",          new UDPEffect);                        
  lights.Add("DMX",          new DMXEffect );                       // need to test - requires custom libs included
  lights.Add("ColorBlend",   new ColorBlend ); 
  lights.Add("EQ",           new EQvisualiser); 
  lights.Add("Beats",        new BeatsTest ); 
  lights.Add("White",        new White ); //   just gives a brightness to work with rgbw LEDS. 

  
  lights.begin();

    HTTP.addHandler(new SPIFFSEditor(http_username,http_password)); // add last to avoid default handler...
  
  //Serial.printf( "[setup] manager.deviceName() = %s\n" , manager.deviceName() ); 
  
  lights.deviceName( manager.deviceName()  );  
  
  lights.Start("Off");


  HTTP.begin();

  Serial.print(F("Free Heap: "));
  Serial.println(ESP.getFreeHeap());
  Serial.print("Ready IP: ");
  Serial.println(WiFi.localIP());
}


void loop()
{


  lights.loop();
  manager.handle();


}



/*-----------------------------------------------
*                      offFn
*------------------------------------------------*/

void offFn(effectState &state, EffectHandler* ptr)
{

  if (ptr) {

    //  cast pointer to the class defined in the Add... allows you to access any functions within it..  
    SwitchEffect& effect = *static_cast<SwitchEffect*>(ptr);

    switch (state) {

    case PRE_EFFECT: {


      // have to be careful of number of pixels.. < 300 generally OK. 
      lights.createAnimator();
      effect.SetTimeout(1000); //  set speed through the effect
      lights.autoWait(); //  halts progress through states untill animator has finished..

      if (animator) {

        AnimEaseFunction easing = NeoEase::QuadraticInOut;

        for (uint16_t pixel = 0; pixel < strip->PixelCount(); pixel++) {

          RgbColor originalColor = myPixelColor(strip->GetPixelColor(pixel));

          AnimUpdateCallback animUpdate = [ = ](const AnimationParam & param) {
            //float progress = easing(param.progress);
            float progress = param.progress;
            RgbColor updatedColor = RgbColor::LinearBlend(originalColor, RgbColor(0), progress);
            strip->SetPixelColor(pixel, updatedColor);
          };

          animator->StartAnimation(pixel, 1000, animUpdate);

        }
      }

    }

    break;
    case RUN_EFFECT: {
      strip->ClearTo(0);
      lights.deleteAnimator();  //  Not needed once off. 
    }
    break;
    case POST_EFFECT: {

    }
    }
  }
}


/*-----------------------------------------------
*
*                 SimpleColorFn
*
*------------------------------------------------*/

void SimpleColorFn(effectState &state, EffectHandler* ptr)
{

  if (ptr) {

    SimpleEffect& effect = *static_cast<SimpleEffect*>(ptr);

    //  gets the next colour
    //  dim is located in helperfunc;
    RgbColor newColor = dim( effect.color(), effect.brightness() );

    switch (state) {

    case PRE_EFFECT: {

      // creates animator, default size is number of pixels. 
      lights.createAnimator(); 

      effect.SetTimeout(2000); //  set speed through the effect

      lights.autoWait(); //  halts progress through states until animator has finished animating

      if (animator) {

        AnimEaseFunction easing = NeoEase::QuadraticInOut;

        for (uint16_t pixel = 0; pixel < strip->PixelCount(); pixel++) {

          RgbColor originalColor = myPixelColor(strip->GetPixelColor(pixel));

          AnimUpdateCallback animUpdate = [ = ](const AnimationParam & param) {

            //float progress = easing(param.progress);
            float progress = param.progress;
            RgbColor updatedColor = RgbColor::LinearBlend(originalColor, newColor, progress);
            strip->SetPixelColor(pixel, updatedColor);
          };

          animator->StartAnimation(pixel, 1000, animUpdate);

        }
      }

      break;
    }

    case RUN_EFFECT: {
      if (strip) {
        strip->ClearTo(newColor);
      }

      lights.deleteAnimator();

      break;
    }
    case POST_EFFECT: {

      break;
    }
    case EFFECT_REFRESH: {
      state = PRE_EFFECT;
      break;
    }

    }
  }
}





