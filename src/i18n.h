#pragma once
#include <stdint.h>

enum class S : uint8_t {
  TitleAprs, TitleAir, TitleDrive, TitleTrail, TitleStation, TitleSetup,
  Stations, Aircraft, Nearest, Ago, NoPosition, WaitingGps, ConnectingWifi, ScanLiveMap, Tagline, Free,
  ConnectTo, ThenOpen, Distance, Time, Average, NoData, LastHeard, Packets, SetCallsign, Ground, Within,
  NoAircraft, NoStations, Rec, Heading, PosGps, PosFollow, PosHome, OpenSource, Settings, Speed,
  TitleMsg, TitleWx, TitleSat, NoMessages, MsgHint, AprsIsOff, AprsIsConnecting, Unverified, MsgSending, MsgDelivered,
  MsgFailed, NoWeather, Wind, Pressure, Humidity, Rain, NextPass, Live, MaxElevation, NoSatData, In,
  Count,
};

const char* tr(S key, uint8_t lang);
