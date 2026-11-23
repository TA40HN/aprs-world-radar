// APRS-IS client task (core 0): one plain TCP connection to the APRS-IS rotation with a filter for messages addressed to
// you. Received messages land in g.messages and are acknowledged; queued ones are sent and retried until acked; with
// `beacon` on, the GPS position goes out by SmartBeaconing. A demo or followed position is never beaconed.
#include <WiFi.h>

#include "app_state.h"
#include "aprs_proto.h"

namespace {

constexpr const char* kServer = "rotate.aprs2.net";
constexpr uint16_t kPort = 14580;
constexpr uint32_t kSilenceMs = 90000;  // servers send a "#" keepalive every ~20 s
constexpr uint32_t kRetryMs = 30000;
constexpr int kMaxTries = 4;

WiFiClient client;
char lineBuf[512];
size_t lineLen = 0;
uint32_t lastRxMs = 0, lastConnectMs = 0;
uint16_t nextId = 1;

void sendLine(const char* packet) {
  client.print(packet);
  client.print("\r\n");
}

bool isMine(const char* call) { return aprs::sameCall(call, cfg.loginCall()) || aprs::sameCall(call, cfg.callsign); }

void setStatus(bool connected, bool verified, const char* server = nullptr) {
  Lock l;
  g.aprsis.enabled = cfg.aprsis;
  g.aprsis.connected = connected;
  g.aprsis.verified = verified;
  if (server) snprintf(g.aprsis.server, sizeof g.aprsis.server, "%s", server);
}

void handleLine(const char* line) {
  lastRxMs = millis();
  if (line[0] == '#') {
    if (const char* r = strstr(line, "logresp")) {
      bool verified = strstr(r, " verified") != nullptr;
      const char* sv = strstr(r, "server ");
      char server[16] = "";
      if (sv) snprintf(server, sizeof server, "%.15s", sv + 7);
      setStatus(true, verified, server);
      Serial.printf("[aprsis] %s\n", line);
    }
    return;
  }
  aprs::Parsed p;
  if (!aprs::parseMessage(line, p) || !isMine(p.addressee)) return;
  if (p.ack || p.rej) {
    Lock l;
    for (int i = 0; i < g.messages.count; i++) {
      AprsMessage& m = g.messages.items[i];
      if (m.outgoing && aprs::sameCall(m.to, p.from) && strcmp(m.id, p.id) == 0) m.state = p.ack ? MsgState::Acked : MsgState::Rejected;
    }
    return;
  }
  if (p.id[0]) {  // acknowledge every copy: the sender retries until it hears one
    char pkt[96];
    aprs::ackPacket(pkt, sizeof pkt, p.addressee, p.from, p.id);
    sendLine(pkt);
  }
  Lock l;
  for (int i = 0; i < g.messages.count; i++) {  // a retry of a message already shown
    const AprsMessage& m = g.messages.items[i];
    if (!m.outgoing && p.id[0] && aprs::sameCall(m.from, p.from) && strcmp(m.id, p.id) == 0) return;
  }
  AprsMessage m;
  snprintf(m.from, sizeof m.from, "%s", p.from);
  snprintf(m.to, sizeof m.to, "%s", p.addressee);
  snprintf(m.text, sizeof m.text, "%s", p.text);
  snprintf(m.id, sizeof m.id, "%s", p.id);
  m.epoch = epochNow();
  g.messages.push(m);
  g.msgVersion++;
  Serial.printf("[aprsis] message from %s: %s\n", m.from, m.text);
}

// One due message per call (the task loops every 100 ms): the packet is built under the lock and written after it, so
// a slow socket never stalls the UI.
void sendQueued() {
  uint32_t now = millis();
  char pkt[160];
  bool due = false;
  {
    Lock l;
    for (int i = 0; i < g.messages.count && !due; i++) {
      AprsMessage& m = g.messages.items[i];
      bool waiting = m.state == MsgState::Queued || (m.state == MsgState::Sent && now - m.lastTryMs >= kRetryMs);
      if (!m.outgoing || !waiting) continue;
      if (m.tries >= kMaxTries) {
        m.state = MsgState::Failed;
        continue;
      }
      aprs::messagePacket(pkt, sizeof pkt, cfg.loginCall(), m.to, m.text, m.id);
      m.tries++;
      m.lastTryMs = now;
      m.state = MsgState::Sent;
      due = true;
    }
  }
  if (due) sendLine(pkt);
}

void maybeBeacon(aprs::SmartBeacon& sb) {
  if (!cfg.beacon) return;
  Position p;
  bool demo;
  {
    Lock l;
    p = g.pos;
    demo = g.posDemo;
  }
  if (!p.valid || p.source != PosSource::Gps || demo) return;
  uint32_t now = millis();
  float speed = p.speedKmh < 0 ? 0 : p.speedKmh;
  if (!sb.due(speed, p.course, now)) return;
  char pkt[200];
  aprs::positionPacket(pkt, sizeof pkt, cfg.loginCall(), p.lat, p.lon, p.course, speed, NAN, cfg.symbol, cfg.comment);
  sendLine(pkt);
  sb.mark(p.course, now);
  Lock l;
  g.aprsis.beacons++;
  g.aprsis.lastBeacon = epochNow();
  Serial.printf("[aprsis] beacon %s\n", pkt);
}

void aprsisTask(void*) {
  aprs::SmartBeacon sb;
  for (;;) {
    if (!cfg.aprsis || !cfg.loginCall()[0] || WiFi.status() != WL_CONNECTED) {
      if (client.connected()) client.stop();
      setStatus(false, false);
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    if (!client.connected()) {
      setStatus(false, false);
      if (lastConnectMs && millis() - lastConnectMs < 30000) {
        vTaskDelay(pdMS_TO_TICKS(500));
        continue;
      }
      lastConnectMs = millis();
      if (!client.connect(kServer, kPort, 5000)) {
        Serial.println("[aprsis] connect failed");
        continue;
      }
      // Filter: messages to the login call and, when it differs, to the callsign (g/A/B); nothing else.
      char filter[32];
      int fl = snprintf(filter, sizeof filter, "g/%s", cfg.loginCall());
      if (cfg.callsign[0] && !aprs::sameCall(cfg.loginCall(), cfg.callsign)) snprintf(filter + fl, sizeof filter - fl, "/%s", cfg.callsign);
      char login[160];
      snprintf(login, sizeof login, "user %s pass %d vers APRSWorldRadar " FW_VERSION " filter %s", cfg.loginCall(),
               aprs::passcode(cfg.loginCall()), filter);
      sendLine(login);
      lastRxMs = millis();
      lineLen = 0;
      setStatus(true, false);
    }
    while (client.available()) {
      int ch = client.read();
      if (ch == '\n') {
        lineBuf[lineLen] = 0;
        if (lineLen) handleLine(lineBuf);
        lineLen = 0;
      } else if (ch != '\r' && lineLen < sizeof lineBuf - 1) {
        lineBuf[lineLen++] = (char)ch;
      }
    }
    if (millis() - lastRxMs > kSilenceMs) {
      Serial.println("[aprsis] silent, reconnecting");
      client.stop();
      continue;
    }
    sendQueued();
    maybeBeacon(sb);
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

}  // namespace

void aprsisBegin() { xTaskCreatePinnedToCore(aprsisTask, "aprsis", 4096, nullptr, 1, nullptr, 0); }

bool aprsisSend(const char* to, const char* text) {
  if (!cfg.aprsis || !to[0] || !text[0]) return false;
  AprsMessage m;
  m.outgoing = true;
  m.state = MsgState::Queued;
  snprintf(m.from, sizeof m.from, "%s", cfg.loginCall());
  snprintf(m.to, sizeof m.to, "%s", to);
  for (char* c = m.to; *c; c++) *c = (char)toupper((unsigned char)*c);
  // APRS message text: printable ASCII, no '|', '~' or '{' (the id separator), at most 67 characters.
  size_t n = 0;
  for (const char* c = text; *c && n < sizeof m.text - 1; c++)
    if (*c >= 0x20 && *c <= 0x7e && *c != '|' && *c != '~' && *c != '{') m.text[n++] = *c;
  snprintf(m.id, sizeof m.id, "%u", nextId);
  nextId = nextId >= 999 ? 1 : nextId + 1;
  m.epoch = epochNow();
  Lock l;
  g.messages.push(m);
  return true;
}
