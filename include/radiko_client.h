#pragma once

#include <Arduino.h>
#include <vector>

struct RadikoStation {
  String id;      // e.g. "TBS"
  String name;    // e.g. "TBSラジオ"
  String areaId;  // e.g. "JP13"
};

class RadikoClient {
public:
  static RadikoClient& instance();

  bool begin();
  bool authenticate();
  bool isAuthenticated() const;
  const String& getAuthToken() const { return authToken_; }
  const String& getAreaId() const { return areaId_; }

  bool fetchStations();
  const std::vector<RadikoStation>& getStations() const { return stations_; }
  size_t getStationCount() const { return stations_.size(); }
  const RadikoStation* getStation(size_t index) const;

  String getStreamUrl(const String& stationId) const;
  String fetchNowProgramTitle(const String& stationId);

  void invalidateToken();

private:
  RadikoClient();
  void initDefaultStations();
  static String decodeHtmlEntities(const String& src);

  String authToken_;
  String areaId_;
  uint32_t tokenIssuedMs_ = 0;
  std::vector<RadikoStation> stations_;
};

// Hook function called from Audio.cpp via patch_audio_i2s.py
extern "C" const char* pocket_audio_get_radiko_token();
