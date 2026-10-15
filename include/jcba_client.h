#pragma once

#include <Arduino.h>

class JcbaClient {
 public:
  static String resolveStreamUrl(const char* stationId);
};
