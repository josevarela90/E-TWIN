#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include "config.h"
#include "etwin/Protocol.h"
#include "etwin/RadioKeys.h"

namespace etwin {
inline bool startStation(Node node) {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false,false);
  WiFi.setSleep(false);
  uint8_t mac[6]; esp_wifi_get_mac(WIFI_IF_STA,mac);
  Serial.printf("MAC STA: %02X:%02X:%02X:%02X:%02X:%02X\n",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
  if(cfg::CHECK_FACTORY_MAC && !sameMac(mac,macFor(node))) {
    Serial.println("ERROR MAC: verifica equipo y Protocol.h; no se modifica la MAC de fabrica.");
    return false;
  }
  return esp_wifi_set_channel(cfg::FALLBACK_CHANNEL,WIFI_SECOND_CHAN_NONE)==ESP_OK;
}
inline bool addPeer(Node node) {
  esp_now_peer_info_t p{};
  memcpy(p.peer_addr,macFor(node),6); p.channel=0; p.ifidx=WIFI_IF_STA;
  p.encrypt=true;
  // Para enlace esclavo-maestro elegir la clave DEL ESCLAVO en el llamador.
  memcpy(p.lmk,node==Node::Energy ? keys::ENERGY_LMK : keys::VIBRATION_LMK,16);
  return esp_now_add_peer(&p)==ESP_OK;
}
inline bool addMasterPeer(Node slave) {
  esp_now_peer_info_t p{};
  memcpy(p.peer_addr,MASTER_MAC,6); p.channel=0; p.ifidx=WIFI_IF_STA; p.encrypt=true;
  memcpy(p.lmk,slave==Node::Energy?keys::ENERGY_LMK:keys::VIBRATION_LMK,16);
  return esp_now_add_peer(&p)==ESP_OK;
}
inline bool addBroadcastPeer() {
  esp_now_peer_info_t p{};
  memcpy(p.peer_addr,BROADCAST_MAC,6); p.channel=0; p.ifidx=WIFI_IF_STA; p.encrypt=false;
  return esp_now_add_peer(&p)==ESP_OK;
}
struct ReceivedFrame {
  uint8_t mac[6]; uint16_t length; uint32_t receivedMs; uint8_t data[112];
};
} // namespace etwin
