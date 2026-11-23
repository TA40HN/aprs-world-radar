#include "i18n.h"

namespace {
struct Pair { const char* en; const char* tr; };
const Pair kStrings[] = {
  {"APRS RADAR", "APRS RADAR"},
  {"AIR TRAFFIC", "HAVA TRAFİĞİ"},
  {"DRIVE", "SÜRÜŞ"},
  {"TRAIL", "İZ"},
  {"MY STATION", "İSTASYONUM"},
  {"SETUP", "KURULUM"},
  {"stations", "istasyon"},
  {"aircraft", "uçak"},
  {"Nearest", "En yakın"},
  {"ago", "önce"},
  {"No position yet", "Konum yok"},
  {"Waiting for GPS", "GPS bekleniyor"},
  {"Connecting Wi-Fi", "Wi-Fi'ye bağlanıyor"},
  {"Scan for the live map", "Canlı harita için tara"},
  {"Live APRS map of the world", "Dünyanın canlı APRS haritası"},
  {"free", "ücretsiz"},
  {"Join Wi-Fi", "Wi-Fi'ye bağlan"},
  {"then open", "sonra aç"},
  {"Distance", "Mesafe"},
  {"Time", "Süre"},
  {"Avg", "Ort."},
  {"No data", "Veri yok"},
  {"Last heard", "Son duyulma"},
  {"packets", "paket"},
  {"Set your callsign in setup", "Kurulumda çağrı işaretini gir"},
  {"ground", "yerde"},
  {"within", "içinde"},
  {"No aircraft in range", "Menzilde uçak yok"},
  {"No stations in range", "Menzilde istasyon yok"},
  {"REC", "KAYIT"},
  {"Heading", "Yön"},
  {"GPS", "GPS"},
  {"via", "takip"},
  {"home", "ev"},
  {"open source", "açık kaynak"},
  {"Settings", "Ayarlar"},
  {"Speed", "Hız"},
};
}  // namespace

const char* tr(S key, uint8_t lang) {
  const Pair& p = kStrings[(int)key];
  return lang == 1 ? p.tr : p.en;
}
