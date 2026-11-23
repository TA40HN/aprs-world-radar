// State shared between the UI loop (core 1) and the network task (core 0). Guard every access with Lock.
#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "model.h"
#include "settings.h"
#include "trail.h"

struct Shared {
  SemaphoreHandle_t mutex = nullptr;
  // Network task → UI
  StationList stations;
  AircraftList aircraft;
  FollowedStation me;
  uint32_t meFetchedMs = 0;
  bool ipValid = false;
  double ipLat = 0, ipLon = 0;
  NetStatus net;
  uint32_t dataVersion = 0;
  bool portalRequested = false;
  // UI → network task
  Position pos;
  // Demo drive: the UI asks for a road loop around (demoLat, demoLon); the network task fills route* once (OSRM).
  static constexpr int kRouteMax = 300;
  bool demoRouteWanted = false;
  double demoLat = 0, demoLon = 0;
  float routeLat[kRouteMax], routeLon[kRouteMax];
  int routeCount = 0;  // set last; the arrays are read-only once it is non-zero
  bool routeFailed = false;
};

extern Shared g;
extern Settings cfg;  // loaded once at boot; changed only by the portal, which restarts the board

struct Lock {
  Lock() { xSemaphoreTake(g.mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(g.mutex); }
};

// net.cpp
void netBegin();
int64_t epochNow();  // Unix seconds, 0 until NTP or GPS set the clock

// store.cpp
void loadSettings(Settings& s);
void saveSettings(const Settings& s);
void storeBegin();
void trailFlush(Trail& t, int64_t epochMs);  // appends the pending fixes to the session file

// portal.cpp
void portalBegin();
void portalStartAp();
void portalLoop();
bool portalApActive();
const char* portalSsid();
