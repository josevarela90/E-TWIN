#pragma once
#include "etwin/Radio.h"
#include "etwin/BootCounter.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace etwin {
// Todos los metodos salvo callback se llaman desde el mismo loop principal.
class SlaveRadio {
 public:
  explicit SlaveRadio(Node node):node_(node){}
  bool begin() {
    boot_=newBootCounter();
    if(!boot_ || !startStation(node_)) { Serial.println("ERROR seguridad: contador NVS / MAC"); return false; }
    queue_=xQueueCreate(10,sizeof(ReceivedFrame));
    if(!queue_) return false;
    instance()=this;
    if(esp_now_init()!=ESP_OK || esp_now_set_pmk(keys::PMK)!=ESP_OK || !addMasterPeer(node_)) {
      Serial.println("ERROR iniciando ESP-NOW"); return false;
    }
    if(esp_now_register_recv_cb(receive)!=ESP_OK) return false;
    channel_=cfg::FALLBACK_CHANNEL; lastHop_=millis(); ready_=true;
    Serial.println("ESP-NOW: buscando beacon del maestro..."); return true;
  }
  void update() {
    if(!ready_) return;
    ReceivedFrame f;
    while(xQueueReceive(queue_,&f,0)==pdTRUE) {
      if(!sameMac(f.mac,MASTER_MAC) || millis()-f.receivedMs>1500) continue;
      BeaconPacket beacon{};
      if(decode(f.data,f.length,Kind::Beacon,Node::Master,beacon,keys::BEACON_AUTH_KEY)
          && beacon.channel>=1 && beacon.channel<=cfg::MAX_RADIO_CHANNEL
          && (!beaconSeen_ || newerFrame(beacon.h.bootId,beacon.h.sequence,masterBoot_,masterSequence_))) {
        if(beacon.channel!=channel_) {
          if(esp_wifi_set_channel(beacon.channel,WIFI_SECOND_CHAN_NONE)!=ESP_OK) continue;
          channel_=beacon.channel;
        }
        if(!beaconSeen_ || !linked() || masterBoot_!=beacon.h.bootId)
          Serial.printf("Maestro encontrado: canal %u\n",channel_);
        if (beacon.h.bootId != masterBoot_ || beacon.h.session != session_) {
          lastAckSequence_=0; lastAck_=0; // maestro reinicio o nueva sesion
        }
        masterBoot_=beacon.h.bootId; masterSequence_=beacon.h.sequence; session_=beacon.h.session;
        beaconSeen_=true; lastBeacon_=f.receivedMs; lastHop_=millis();
        continue;
      }
      AckPacket ack{};
      if(decode(f.data,f.length,Kind::Ack,Node::Master,ack,authenticationKey(Node::Master,Kind::Ack,node_))
          && ack.h.session==session_ && ack.h.bootId==masterBoot_
          && ack.acknowledgedBoot==boot_ && ack.acknowledgedSequence==sequence_
          && (!lastAckSequence_ || newerSequence(ack.h.sequence,lastAckSequence_))) {
        lastAck_=f.receivedMs; lastAckSequence_=ack.h.sequence;
      }
    }
    const uint32_t now=millis();
    if(!linked() && now-lastHop_>=700) {
      const uint8_t next=channel_%cfg::MAX_RADIO_CHANNEL+1;
      if(esp_wifi_set_channel(next,WIFI_SECOND_CHAN_NONE)==ESP_OK) channel_=next;
      lastHop_=now;
    }
  }
  bool linked() const { return ready_ && beaconSeen_ && millis()-lastBeacon_<3000; }
  bool ready() const { return ready_; }
  uint8_t channel() const { return channel_; }
  uint32_t lastAckMs() const { return lastAck_; }
  bool send(EnergyPacket& p) { return sendPacket(p,Kind::Energy); }
  bool send(VibrationPacket& p) { return sendPacket(p,Kind::Vibration); }
 private:
  static SlaveRadio*& instance() { static SlaveRadio* p=nullptr; return p; }
  static void receive(const uint8_t* mac,const uint8_t* data,int len) {
    auto* self=instance();
    if(!self || !self->queue_ || !mac || !data || len<=0 || len>112) return;
    ReceivedFrame f{}; memcpy(f.mac,mac,6); f.length=len; f.receivedMs=millis();
    memcpy(f.data,data,len); xQueueSend(self->queue_,&f,0);
  }
  template<class T> bool sendPacket(T& p,Kind kind) {
    if(!linked() || !session_) return false;
    if (!seal(p,kind,node_,++sequence_,boot_,millis(),session_,authenticationKey(node_,kind))) return false;
    return esp_now_send(MASTER_MAC,reinterpret_cast<const uint8_t*>(&p),sizeof(p))==ESP_OK;
  }
  Node node_; QueueHandle_t queue_=nullptr;
  bool ready_=false,beaconSeen_=false;
  uint8_t channel_=cfg::FALLBACK_CHANNEL;
  uint32_t boot_=0,masterBoot_=0,masterSequence_=0,session_=0,sequence_=0,lastBeacon_=0,lastHop_=0,lastAck_=0,lastAckSequence_=0;
};
} // namespace etwin
