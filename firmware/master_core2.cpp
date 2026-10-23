#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "config.h"
#include "etwin/Radio.h"
#include "etwin/Analysis.h"
#include "etwin/ThingSpeakCA.h"
#include "etwin/BootCounter.h"

using namespace etwin;
namespace {
constexpr uint16_t BG=0x0863, CARD=0x10A5, WHITE=0xEF7D, MUTED=0x9D75;
constexpr uint16_t CYAN=0x3EFD, GREEN=0x4EAB, AMBER=0xFCA0, RED=0xF986;
struct Snapshot {
  Sample<EnergyPacket> energy;
  Sample<VibrationPacket> vibration;
  Baseline baseline;
  bool capturing=false;
  uint16_t captureCount=0;
};
struct CloudView {
  bool wifi=false,mqtt=false;
  uint32_t lastSentMs=0, sent=0;
  int lastError=0;
  char status[40]="Iniciando";
};
Snapshot localState, sharedState;
CloudView cloudView;
portMUX_TYPE stateMux=portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE cloudMux=portMUX_INITIALIZER_UNLOCKED;
QueueHandle_t rxQueue=nullptr;
volatile uint32_t droppedFrames=0;
bool radioReady=false, spriteReady=false;
uint32_t bootId=0, sessionId=0, beaconSequence=0, ackSequence=0, rejectedAuth=0, rejectedReplay=0;
uint32_t lastBeaconMs=0,lastPaintMs=0,lastLogMs=0;
uint8_t page=0;
uint32_t clearArmedAt=0;
M5Canvas screen(&M5.Display);
RunningStats referencePower,referenceVibration;
uint32_t lastReferenceMs=0,refEnergySeq=0,refVibrationSeq=0;
uint32_t refEnergyBoot=0,refVibrationBoot=0;
bool buttonBHeld=false,buttonCHeld=false;
char notice[64]="Deslice izquierda/derecha para ver 4 vistas";
uint32_t noticeAt=0;

void message(const char* text) {
  snprintf(notice,sizeof(notice),"%s",text); noticeAt=millis(); Serial.println(text);
}
void shareState() {
  portENTER_CRITICAL(&stateMux); sharedState=localState; portEXIT_CRITICAL(&stateMux);
}
Snapshot copyState() {
  Snapshot copy; portENTER_CRITICAL(&stateMux); copy=sharedState; portEXIT_CRITICAL(&stateMux); return copy;
}
CloudView copyCloud() {
  CloudView copy; portENTER_CRITICAL(&cloudMux); copy=cloudView; portEXIT_CRITICAL(&cloudMux); return copy;
}
void cloudStatus(const char* status,bool wifi,bool mqtt,int error=0) {
  portENTER_CRITICAL(&cloudMux);
  cloudView.wifi=wifi; cloudView.mqtt=mqtt; cloudView.lastError=error;
  snprintf(cloudView.status,sizeof(cloudView.status),"%s",status);
  portEXIT_CRITICAL(&cloudMux);
}
void received(const uint8_t* mac,const uint8_t* data,int length) {
  if(!rxQueue || !mac || !data || length<=0 || length>80) return;
  if(!sameMac(mac,ENERGY_MAC) && !sameMac(mac,VIBRATION_MAC)) return;
  ReceivedFrame f{}; memcpy(f.mac,mac,6); f.length=length; f.receivedMs=millis();
  memcpy(f.data,data,length);
  if(xQueueSend(rxQueue,&f,0)!=pdTRUE) ++droppedFrames;
}
void sendAck(const Header& h,const uint8_t* mac) {
  AckPacket ack{}; ack.acknowledgedBoot=h.bootId; ack.acknowledgedSequence=h.sequence;
  if (!seal(ack,Kind::Ack,Node::Master,++ackSequence,bootId,millis(),sessionId,
            authenticationKey(Node::Master,Kind::Ack, sameMac(mac,ENERGY_MAC)?Node::Energy:Node::Vibration))) return;
  esp_now_send(mac,reinterpret_cast<const uint8_t*>(&ack),sizeof(ack));
}
template<class T> bool accept(Sample<T>& s,const T& p,uint32_t arrival) {
  // Same-boot duplicate/reordered packets do not refresh freshness.
  if(s.seen && !newerFrame(p.h.bootId,p.h.sequence,s.packet.h.bootId,s.packet.h.sequence)) return false;
  s.packet=p; s.receivedMs=arrival; s.seen=true; return true;
}
void drainRadio() {
  if(!rxQueue) return;
  ReceivedFrame f;
  while(xQueueReceive(rxQueue,&f,0)==pdTRUE) {
    EnergyPacket e{}; VibrationPacket v{};
    if (sameMac(f.mac, ENERGY_MAC)) {
      if (!decode(f.data,f.length,Kind::Energy,Node::Energy,e,keys::ENERGY_AUTH_KEY) || e.h.session!=sessionId) { __atomic_fetch_add(&rejectedAuth,1,__ATOMIC_RELAXED); continue; }
      if (accept(localState.energy,e,f.receivedMs)) sendAck(e.h,f.mac); else __atomic_fetch_add(&rejectedReplay,1,__ATOMIC_RELAXED);
    } else if (sameMac(f.mac, VIBRATION_MAC)) {
      if (!decode(f.data,f.length,Kind::Vibration,Node::Vibration,v,keys::VIBRATION_AUTH_KEY) || v.h.session!=sessionId) { __atomic_fetch_add(&rejectedAuth,1,__ATOMIC_RELAXED); continue; }
      if (accept(localState.vibration,v,f.receivedMs)) sendAck(v.h,f.mac); else __atomic_fetch_add(&rejectedReplay,1,__ATOMIC_RELAXED);
    }
  }
}
void sendBeacon() {
  const uint32_t now=millis();
  if(!radioReady || now-lastBeaconMs<300) return;
  lastBeaconMs=now;
  uint8_t ch=0; wifi_second_chan_t secondary;
  if(esp_wifi_get_channel(&ch,&secondary)!=ESP_OK || ch<1) return;
  BeaconPacket p{}; p.channel=ch;
  if (!seal(p,Kind::Beacon,Node::Master,++beaconSequence,bootId,now,sessionId,keys::BEACON_AUTH_KEY)) return;
  esp_now_send(BROADCAST_MAC,reinterpret_cast<const uint8_t*>(&p),sizeof(p));
}
void loadBaseline() {
  Preferences prefs;
  if(prefs.begin("etwin",true)) {
    Baseline b{};
    if(prefs.getBytesLength("baseline")==sizeof(b)) {
      prefs.getBytes("baseline",&b,sizeof(b));
      if(baselineValid(b)) localState.baseline=b;
    }
    prefs.end();
  }
}
bool storeBaseline(const Baseline& b) {
  Preferences prefs; if(!prefs.begin("etwin",false)) return false;
  const bool ok=prefs.putBytes("baseline",&b,sizeof(b))==sizeof(b); prefs.end(); return ok;
}
void cancelCapture(const char* reason) {
  localState.capturing=false; localState.captureCount=0;
  referencePower={}; referenceVibration={}; message(reason);
}
void startCapture() {
  const auto a=analyze(localState.energy,localState.vibration,localState.baseline,millis());
  if(!a.energyValid || !a.vibrationValid || !a.running) {
    message("Referencia: requiere 2 nodos y maquina activa"); return;
  }
  referencePower={}; referenceVibration={};
  localState.capturing=true; localState.captureCount=0;
  lastReferenceMs=millis();
  refEnergySeq=localState.energy.packet.h.sequence;
  refVibrationSeq=localState.vibration.packet.h.sequence;
  refEnergyBoot=localState.energy.packet.h.bootId;
  refVibrationBoot=localState.vibration.packet.h.bootId;
  message("Referencia: 10 pares; mantener carga estable");
}
void updateReference() {
  if(!localState.capturing) return;
  const uint32_t now=millis();
  const auto a=analyze(localState.energy,localState.vibration,localState.baseline,now);
  if(!a.energyValid || !a.vibrationValid || !a.running ||
      refEnergyBoot!=localState.energy.packet.h.bootId || refVibrationBoot!=localState.vibration.packet.h.bootId) {
    cancelCapture("Referencia cancelada: dato perdido/reinicio"); return;
  }
  if(now-lastReferenceMs<cfg::BASELINE_SAMPLE_INTERVAL_MS) return;
  if(refEnergySeq==localState.energy.packet.h.sequence || refVibrationSeq==localState.vibration.packet.h.sequence) return;
  lastReferenceMs=now;
  refEnergySeq=localState.energy.packet.h.sequence; refVibrationSeq=localState.vibration.packet.h.sequence;
  referencePower.add(localState.energy.packet.power); referenceVibration.add(localState.vibration.packet.rms);
  localState.captureCount=referencePower.n;
  if(referencePower.n<cfg::BASELINE_SAMPLES) return;
  if(referencePower.cv()>cfg::BASELINE_MAX_POWER_CV || referenceVibration.cv()>cfg::BASELINE_MAX_VIBRATION_CV) {
    cancelCapture("Referencia rechazada: carga/vibracion inestable"); return;
  }
  Baseline next; next.power=referencePower.mean; next.vibration=referenceVibration.mean; next.samples=referencePower.n;
  const time_t epoch=time(nullptr); next.capturedEpoch=epoch>1700000000 ? static_cast<uint32_t>(epoch):0;
  if(!baselineValid(next)) { cancelCapture("Referencia insuficiente: vibracion muy baja"); return; }
  localState.baseline=next; localState.capturing=false;
  if(storeBaseline(next)) message("Referencia guardada; comparar misma carga");
  else message("Referencia activa; fallo guardando en memoria");
}
void clearReference() {
  cancelCapture("Referencia desactivada");
  localState.baseline=Baseline{};
  Preferences prefs; bool erased=false;
  if(prefs.begin("etwin",false)) {
    erased=!prefs.isKey("baseline") || prefs.remove("baseline"); prefs.end();
  }
  message(erased?"Referencia borrada de memoria":"Error NVS: referencia podria volver al reiniciar");
}
String urlEncode(const String& text) {
  static const char hex[]="0123456789ABCDEF"; String out; out.reserve(text.length()*2);
  for(size_t i=0;i<text.length();++i) {
    const uint8_t c=text[i];
    if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~') out+=static_cast<char>(c);
    else { out+='%'; out+=hex[c>>4]; out+=hex[c&15]; }
  } return out;
}
String optionalFloat(float value,int decimals) { return isfinite(value) ? String(value,decimals):String("na"); }
String cloudPayload(const Snapshot& s,uint32_t publication) {
  const uint32_t now=millis();
  const auto a=analyze(s.energy,s.vibration,s.baseline,now,s.capturing);
  String payload; payload.reserve(700);
  if(a.energyValid) {
    const auto& e=s.energy.packet;
    payload="field1="+String(e.voltage,1)+"&field2="+String(e.current,3)+"&field3="+String(e.power,1)
      +"&field4="+String(e.energyKWh,3)+"&field5="+String(e.frequency,1)+"&field6="+String(e.powerFactor,3)+"&";
  }
  if(a.vibrationValid) payload+="field7="+String(s.vibration.packet.rms,4)+"&field8="+String(s.vibration.packet.peak,4)+"&";
  String status="ver=2;state="+String(stateCode(a.state))+";e="+String(a.energyValid?1:0)+";v="+String(a.vibrationValid?1:0)
    +";ea="+(s.energy.seen ? String(now-s.energy.receivedMs):String("-1"))
    +";va="+(s.vibration.seen ? String(now-s.vibration.receivedMs):String("-1"))
    +";alarm="+String(a.alarms)+";ref="+String(a.referenceValid?1:0)
    +";dp="+optionalFloat(a.powerDeltaPercent,1)+";vr="+optionalFloat(a.vibrationRatio,2)
    +";boot="+String(bootId)+";seq="+String(publication)
    +";vf="+String(s.vibration.seen?s.vibration.packet.h.flags:0);
  // ThingSpeak permite 255 bytes maximos en status. Sin secretos ni escritura remota.
  status+=";run="+String(a.running?1:0)+";ot=1;rc="+String(cfg::BASELINE_SAMPLES)
    +";ct="+String(s.captureCount);
  const auto optional=[&status](const String& part) {
    if (status.length()+part.length()<=250) status+=part;
  };
  if (a.vibrationValid) {
    const auto& v=s.vibration.packet;
    optional(";vx="+String(v.rmsX,3)); optional(";vy="+String(v.rmsY,3));
    optional(";vz="+String(v.rmsZ,3)); optional(";cf="+String(v.crest,2));
    optional(";fs="+String(v.sampleRateHz,1)); optional(";ns="+String(v.samples));
    optional(";mi="+String(v.missed));
  }
  optional(";rj="+String(__atomic_load_n(&rejectedAuth,__ATOMIC_RELAXED)));
  optional(";rp="+String(__atomic_load_n(&rejectedReplay,__ATOMIC_RELAXED)));
  // Nunca incluir una etiqueta truncada o status que sobrepase el limite.
  if (status.length()>255) status=status.substring(0,255);
  payload+="status="+urlEncode(status);
  return payload;
}
void cloudTask(void*) {
  WiFiClientSecure tls;
  tls.setCACert(MQTT_ROOT_CA); tls.setHandshakeTimeout(6); tls.setTimeout(5000);
  PubSubClient mqtt(tls);
  mqtt.setServer(cfg::MQTT_HOST,cfg::MQTT_PORT); mqtt.setBufferSize(1536);
  mqtt.setKeepAlive(30); mqtt.setSocketTimeout(3);
  const bool wifiConfigured=cfg::WIFI_SSID[0]!=0;
  const bool mqttConfigured=cfg::THINGSPEAK_CHANNEL_ID>0 && cfg::MQTT_CLIENT_ID[0] && cfg::MQTT_USERNAME[0] && cfg::MQTT_PASSWORD[0];
  const String topic="channels/"+String(cfg::THINGSPEAK_CHANNEL_ID)+"/publish";
  uint32_t lastWifiTry=millis()-60000,wifiStart=0,lastMqttTry=millis()-15000,lastPublish=millis()-cfg::CLOUD_INTERVAL_MS;
  uint32_t publication=0; bool connecting=false,ntpStarted=false,wasConnected=false;
  for(;;) {
    uint32_t now=millis();
    if(!wifiConfigured) {
      cloudStatus("Configurar WiFi en config.h",false,false); vTaskDelay(pdMS_TO_TICKS(1000)); continue;
    }
    if(WiFi.status()!=WL_CONNECTED) {
      if(wasConnected) { mqtt.disconnect(); tls.stop(); wasConnected=false; connecting=false; lastWifiTry=now-60000; }
      if(!connecting && now-lastWifiTry>=60000) {
        lastWifiTry=now; wifiStart=now; connecting=true;
        WiFi.begin(cfg::WIFI_SSID,cfg::WIFI_PASSWORD);
        cloudStatus("Conectando WiFi...",false,false);
      }
      if(connecting && now-wifiStart>=15000) {
        WiFi.disconnect(false,false); connecting=false;
        esp_wifi_set_channel(cfg::FALLBACK_CHANNEL,WIFI_SECOND_CHAN_NONE);
        cloudStatus("WiFi sin enlace; monitoreo local",false,false);
      }
      vTaskDelay(pdMS_TO_TICKS(100)); continue;
    }
    wasConnected=true; connecting=false;
    if(!ntpStarted) { configTime(0,0,"pool.ntp.org","time.google.com"); ntpStarted=true; }
    if(!mqttConfigured) {
      cloudStatus("Configurar MQTT en config.h",true,false); vTaskDelay(pdMS_TO_TICKS(1000)); continue;
    }
    // Validacion de certificados requiere reloj UTC; nunca usar setInsecure().
    if(time(nullptr)<1700000000) {
      cloudStatus("Esperando hora NTP para TLS",true,false); vTaskDelay(pdMS_TO_TICKS(500)); continue;
    }
    if(!mqtt.connected()) {
      if(now-lastMqttTry>=15000) {
        lastMqttTry=now; cloudStatus("Conectando MQTT TLS...",true,false);
        const bool ok=mqtt.connect(cfg::MQTT_CLIENT_ID,cfg::MQTT_USERNAME,cfg::MQTT_PASSWORD);
        if(!ok) { cloudStatus("MQTT fallo; revisar credenciales/TLS",true,false,mqtt.state()); tls.stop(); }
        else cloudStatus("MQTT conectado",true,true);
      }
      vTaskDelay(pdMS_TO_TICKS(100)); continue;
    }
    if(!mqtt.loop()) { cloudStatus("MQTT desconectado; reintentando",true,false,mqtt.state()); continue; }
    now=millis();
    if(now-lastPublish>=cfg::CLOUD_INTERVAL_MS) {
      // Tomar instantanea DESPUES de conectar, nunca publicar cache de antes del TLS.
      const Snapshot snapshot=copyState();
      const String payload=cloudPayload(snapshot,++publication);
      const bool sent=mqtt.publish(topic.c_str(),payload.c_str(),false);
      lastPublish=now; // limite de frecuencia tambien ante error de envio
      if(sent) {
        portENTER_CRITICAL(&cloudMux);
        cloudView.lastSentMs=now; ++cloudView.sent;
        portEXIT_CRITICAL(&cloudMux);
        cloudStatus("MQTT enviado (QoS0)",true,true);
        Serial.printf("ThingSpeak: envio %lu, %u bytes; QoS0 sin confirmacion de guardado\n",
                      static_cast<unsigned long>(publication),static_cast<unsigned>(payload.length()));
      } else {
        cloudStatus("Error publicando MQTT",true,mqtt.connected(),mqtt.state());
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// --- Core2 320 x 240: 4 vistas SCADA con deslizamiento tactil ----------
void textAt(int x,int y,const String& value,uint16_t color=WHITE,uint8_t size=1) {
  screen.setTextSize(size); screen.setTextColor(color);
  screen.setCursor(x,y); screen.print(value);
}
String number(float v,uint8_t decimals) { return isfinite(v)?String(v,static_cast<unsigned int>(decimals)):String("--"); }
String ageLabel(bool seen,uint32_t t,uint32_t now) { return seen?String((now-t)/1000)+"s":String("--"); }
void cardAt(int x,int y,int w,int h,const String& title,const String& value,const String& unit,uint16_t c=WHITE) {
  screen.fillRoundRect(x,y,w,h,6,CARD);
  textAt(x+7,y+5,title,MUTED);
  textAt(x+7,y+19,value,c,2);
  if(unit.length()) textAt(x+w-6-unit.length()*6,y+32,unit,MUTED);
}
void drawProcess(const Analysis& a,uint32_t now) {
  // Diagrama de flujo informativo: NO manda ordenes ni representa RPM reales.
  screen.fillRoundRect(6,49,103,50,5,CARD);
  textAt(13,55,"NODO 1 | PZEM",CYAN);
  textAt(13,69,a.energyValid?number(localState.energy.packet.voltage,1)+" V":"SIN DATOS",WHITE);
  textAt(13,82,a.energyValid?number(localState.energy.packet.power,0)+" W":"--",CYAN);
  screen.fillRoundRect(6,112,103,50,5,CARD);
  textAt(13,118,"NODO 2 | IMU",AMBER);
  textAt(13,132,a.vibrationValid?number(localState.vibration.packet.rms,3)+" RMS":"SIN DATOS",WHITE);
  textAt(13,145,a.vibrationValid?number(localState.vibration.packet.peak,2)+" pico":"--",AMBER);
  // Flujo desde sensores hacia motor. Puntos animados cuando hay dato actual.
  screen.drawLine(109,74,133,74,a.energyValid?CYAN:MUTED);
  screen.drawLine(133,74,133,120,a.energyValid?CYAN:MUTED);
  screen.drawLine(109,137,133,137,a.vibrationValid?AMBER:MUTED);
  screen.drawLine(133,120,145,120,MUTED);
  if(a.energyValid) screen.fillCircle(109+(now/130)%25,74,2,CYAN);
  if(a.vibrationValid) screen.fillCircle(109+(now/170)%25,137,2,AMBER);
  const int cx=181, cy=119;
  screen.fillRoundRect(144,91,75,58,9,CARD);
  screen.drawCircle(cx,cy,24,(a.state==MachineState::Alarm?RED:GREEN));
  screen.drawCircle(cx,cy,18,MUTED);
  const float angle=a.running?float(now%2600)*6.2831853f/2600.0f:0.0f;
  for(int j=0;j<3;++j) {
    const float t=angle+j*2.0943951f;
    screen.drawLine(cx,cy,cx+int(cosf(t)*15),cy+int(sinf(t)*15),a.running?CYAN:MUTED);
  }
  screen.fillCircle(cx,cy,4,WHITE);
  textAt(154,159,a.running?"ACTIVA":"REPOSO",a.running?GREEN:MUTED);
  screen.drawLine(219,118,237,118,a.energyValid?GREEN:MUTED);
  screen.fillRoundRect(236,95,78,47,5,CARD);
  textAt(243,101,"M5 CORE2",CYAN);
  textAt(243,115,"DATOS OT",WHITE);
  textAt(243,128,radioReady?"HMAC":"RADIO OFF",radioReady?GREEN:RED);
  screen.drawLine(275,142,275,167,MUTED);
  textAt(241,170,"MQTT / TLS",MUTED);
  textAt(8,187,a.energyValid?"FP "+number(localState.energy.packet.powerFactor,2)+" | "+number(localState.energy.packet.frequency,1)+" Hz":"SIN MEDICION ELECTRICA",MUTED);
  textAt(8,199,"E:"+ageLabel(localState.energy.seen,localState.energy.receivedMs,now)
      +" V:"+ageLabel(localState.vibration.seen,localState.vibration.receivedMs,now)
      +" | ref:"+String(localState.capturing?localState.captureCount:(a.referenceValid?cfg::BASELINE_SAMPLES:0))
      +"/"+String(cfg::BASELINE_SAMPLES),MUTED);
}
void drawElectric(const Analysis& a) {
  const auto& e=localState.energy.packet;
  const bool ok=a.energyValid;
  cardAt(8,49,148,48,"VOLTAJE",ok?number(e.voltage,1):"--","V",CYAN);
  cardAt(164,49,148,48,"CORRIENTE",ok?number(e.current,3):"--","A");
  cardAt(8,103,148,48,"POTENCIA",ok?number(e.power,0):"--","W",CYAN);
  cardAt(164,103,148,48,"ENERGIA",ok?number(e.energyKWh,3):"--","kWh");
  cardAt(8,157,148,48,"FRECUENCIA",ok?number(e.frequency,1):"--","Hz");
  cardAt(164,157,148,48,"FACTOR POTENCIA",ok?number(e.powerFactor,2):"--","FP");
  textAt(8,210,"S="+number(a.apparentVA,1)+" VA   P vs ref="+number(a.powerDeltaPercent,1)+" %",MUTED);
}
void drawVibration(const Analysis& a) {
  const auto& v=localState.vibration.packet;
  const bool ok=a.vibrationValid;
  cardAt(8,49,148,48,"RMS VECTORIAL",ok?number(v.rms,3):"--","m/s2",AMBER);
  cardAt(164,49,148,48,"PICO VECTORIAL",ok?number(v.peak,3):"--","m/s2",AMBER);
  cardAt(8,102,96,48,"RMS X",ok?number(v.rmsX,3):"--","m/s2");
  cardAt(112,102,96,48,"RMS Y",ok?number(v.rmsY,3):"--","m/s2");
  cardAt(216,102,96,48,"RMS Z",ok?number(v.rmsZ,3):"--","m/s2");
  textAt(10,162,"FACTOR CRESTA: "+(ok?number(v.crest,2):"--"),WHITE);
  textAt(10,178,"FS: "+(ok?number(v.sampleRateHz,1):"--")+" Hz | N: "+(ok?String(v.samples):"--"),WHITE);
  textAt(10,194,"RMS / REF: "+number(a.vibrationRatio,2)+" x",AMBER);
  textAt(10,209,"BANDA 6-75 Hz | 4o orden HP+LP",MUTED);
}
void drawDiagnostic(const Analysis& a,uint32_t now) {
  const CloudView c=copyCloud();
  uint8_t channel=0; wifi_second_chan_t secondary;
  esp_wifi_get_channel(&channel,&secondary);
  textAt(8,51,"ESP-NOW cifrado AES-CCMP + HMAC",GREEN);
  textAt(8,68,"Nodo 1 "+String(a.energyValid?"OK":"NO VALIDO")+" | "+ageLabel(localState.energy.seen,localState.energy.receivedMs,now));
  textAt(8,83,"Nodo 2 "+String(a.vibrationValid?"OK":"NO VALIDO")+" | "+ageLabel(localState.vibration.seen,localState.vibration.receivedMs,now));
  textAt(8,98,"Rechazos MAC/HMAC: "+String(__atomic_load_n(&rejectedAuth,__ATOMIC_RELAXED))+"  replay: "+String(__atomic_load_n(&rejectedReplay,__ATOMIC_RELAXED)),AMBER);
  textAt(8,113,"Canal "+String(channel)+" | Perdidas cola "+String(droppedFrames));
  textAt(8,128,"WiFi: "+String(c.wifi?"OK":"OFF")+"  MQTT/TLS: "+String(c.mqtt?"OK":"OFF"),c.mqtt?GREEN:AMBER);
  textAt(8,143,c.status,c.mqtt?GREEN:AMBER);
  textAt(8,158,"Nube: "+String(c.sent)+" envios, QoS0, 20s");
  textAt(8,173,"Referencia: "+String(localState.capturing?localState.captureCount:(a.referenceValid?cfg::BASELINE_SAMPLES:0))
      +"/"+String(cfg::BASELINE_SAMPLES)+" | E "+number(localState.baseline.power,0)+"W");
  textAt(8,188,"Referencia vib: "+number(localState.baseline.vibration,3)+" m/s2");
  textAt(8,203,"Alarmas: "+String(a.alarms)+" | boot: "+String(bootId),a.alarms&~SETUP?AMBER:MUTED);
}
void paint() {
  if(!spriteReady) return;
  const uint32_t now=millis();
  const auto a=analyze(localState.energy,localState.vibration,localState.baseline,now,localState.capturing);
  screen.fillSprite(BG);
  textAt(8,7,"E-TWIN",CYAN,2);
  textAt(150,10,"DIGITAL TWIN / OT",MUTED);
  const uint16_t color=a.state==MachineState::Alarm?RED:a.state==MachineState::Warn?AMBER:a.running?GREEN:MUTED;
  screen.fillRect(0,29,320,15,CARD);
  textAt(8,32,String(stateLabel(a.state)),color);
  if(page==0) drawProcess(a,now);
  else if(page==1) drawElectric(a);
  else if(page==2) drawVibration(a);
  else drawDiagnostic(a,now);
  // Barra de navegacion tactil inferior y puntos de pagina.
  screen.drawFastHLine(0,223,320,CARD);
  for (int j=0;j<4;++j) {
    screen.fillRoundRect(j*80+2,227,76,11,3,j==page?0x2188:CARD);
    const char* label[]={"PROCESO","ENERGIA","VIBRACION","RED / OT"};
    textAt(j*80+9,228,label[j],j==page?WHITE:MUTED);
  }
  // Avisos prioritarios mostrados en serial y al pie en periodos cortos.
  if(now-noticeAt<2600 && page!=0) { screen.fillRect(0,208,320,15,BG); textAt(8,211,notice,AMBER); }
  screen.pushSprite(0,0);
}
void changePage(int shift) { page=(page+4+shift)%4; }
void controls() {
  if(M5.BtnA.wasPressed()) changePage(1);
  if(!M5.BtnB.isPressed()) buttonBHeld=false;
  if(M5.BtnB.pressedFor(2000) && !buttonBHeld) {
    buttonBHeld=true;
    if(localState.capturing) cancelCapture("Referencia cancelada"); else startCapture();
  }
  if(M5.BtnC.wasPressed()) page=3;
  if(!M5.BtnC.isPressed()) buttonCHeld=false;
  if(M5.BtnC.pressedFor(4000) && !buttonCHeld) {
    buttonCHeld=true;
    const uint32_t now=millis();
    if(clearArmedAt && now-clearArmedAt<10000) {clearReference();clearArmedAt=0;}
    else {clearArmedAt=now;message("BORRAR: mantenga C 4s otra vez (<10s)");}
  }
  // M5Unified: distancia relativa a inicio del gesto.
  auto t=M5.Touch.getDetail();
  if(t.wasFlicked() || t.wasDragged()) {
    if(abs(t.distanceX())>=55 && abs(t.distanceX())>abs(t.distanceY())*1.35f)
      changePage(t.distanceX()<0?1:-1);
  } else if(t.wasClicked() && t.y>=223) {
    page=static_cast<uint8_t>(constrain(t.x/80,0,3));
  }
}
} // namespace

void setup() {
  auto m5cfg=M5.config();
  m5cfg.serial_baudrate=115200;
  m5cfg.internal_imu=false; m5cfg.internal_mic=false; m5cfg.internal_spk=false;
  m5cfg.external_imu=false; m5cfg.external_rtc=false; m5cfg.external_display_value=0;
  M5.begin(m5cfg);
  if(M5.Display.width()<M5.Display.height()) M5.Display.setRotation((M5.Display.getRotation()+1)%4);
  M5.Display.setBrightness(150); M5.Display.setTextWrap(false);
  screen.setColorDepth(16); screen.setTextWrap(false);
  spriteReady=screen.createSprite(320,240)!=nullptr;
  if(!spriteReady) { M5.Display.fillScreen(BG); M5.Display.setCursor(8,8); M5.Display.print("Error de memoria TFT; ver Serial"); }
  Serial.println("\nE-TWIN / Maestro M5 Core2 / 2.0 OT");
  bootId=newBootCounter(); sessionId=esp_random(); if (!sessionId) sessionId=esp_random()|1;
  loadBaseline(); shareState();
  rxQueue=xQueueCreate(24,sizeof(ReceivedFrame));
  radioReady=bootId && sessionId && startStation(Node::Master) && rxQueue;
  if(radioReady) radioReady=esp_now_init()==ESP_OK && esp_now_set_pmk(keys::PMK)==ESP_OK
    && addPeer(Node::Energy) && addPeer(Node::Vibration) && addBroadcastPeer()
    && esp_now_register_recv_cb(received)==ESP_OK;
  if(!radioReady) message("ERROR radio/MAC; revisar monitor serie");
  else message("Listo; esperando los dos nodos");
  // Solo cloudTask manipula cliente TLS/MQTT; recepcion y TFT siguen en loop.
  if (!radioReady) cloudStatus("Bloqueado: radio OT no disponible",false,false);
  else if(xTaskCreatePinnedToCore(cloudTask,"etwin-cloud",12288,nullptr,1,nullptr,0)!=pdPASS)
    cloudStatus("Error creando tarea de nube",false,false);
  paint();
}
void loop() {
  M5.update(); drainRadio(); sendBeacon(); controls(); updateReference(); shareState();
  const uint32_t now=millis();
  if(now-lastPaintMs>=160) { lastPaintMs=now; paint(); }
  if(now-lastLogMs>=2000) {
    lastLogMs=now;
    const auto a=analyze(localState.energy,localState.vibration,localState.baseline,now,localState.capturing);
    Serial.printf("Estado=%s e=%d v=%d P=%.1f RMS=%.4f alarm=%u\n",stateCode(a.state),a.energyValid,a.vibrationValid,
                  a.energyValid?localState.energy.packet.power:NAN,a.vibrationValid?localState.vibration.packet.rms:NAN,a.alarms);
  }
  delay(2);
}
