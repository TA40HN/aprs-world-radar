// Every screen, drawn from a UiState snapshot. No Arduino or network code here: sim/ renders the same functions.
#pragma once
#include "canvas.h"
#include "model.h"
#include "settings.h"
#include "trail.h"

struct UiState {
  const Settings* cfg = nullptr;
  const Position* pos = nullptr;
  const StationList* stations = nullptr;
  const AircraftList* aircraft = nullptr;
  const FollowedStation* me = nullptr;
  const Trail* trail = nullptr;
  const NetStatus* net = nullptr;
  uint32_t nowMs = 0;
  int localMin = -1;  // minutes since local midnight, -1 unknown
  ScreenId screen = SCR_APRS;
  int gpsSats = -1;   // -1: no GPS module seen
  float splash = -1;  // 0..1 while the boot animation runs
  char portalSsid[33] = {0};
  char portalIp[16] = {0};
  bool portal = false;
};

void drawFrame(Canvas& c, const UiState& s);
