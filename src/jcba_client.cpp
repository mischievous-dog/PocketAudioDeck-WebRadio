#include "jcba_client.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

String JcbaClient::resolveStreamUrl(const char* stationId) {
  if (!stationId || stationId[0] == '\0') {
    return "";
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  String apiUrl = "https://www.jcbasimul.com/api/select_stream?station=";
  apiUrl += stationId;
  apiUrl += "&channel=0&quality=high&burst=5";

  Serial.printf("[JCBA] Requesting stream for '%s'...\n", stationId);
  if (!https.begin(client, apiUrl)) {
    Serial.println("[JCBA] https.begin failed");
    return "";
  }
  https.setUserAgent("PocketAudioDeck/1.0");
  https.setTimeout(7000);

  int httpCode = https.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[JCBA] GET failed, code: %d\n", httpCode);
    https.end();
    return "";
  }

  String payload = https.getString();
  https.end();

  // Parse location and token from JSON response
  int locIdx = payload.indexOf("\"location\":");
  int tokenIdx = payload.indexOf("\"token\":");
  if (locIdx < 0 || tokenIdx < 0) {
    Serial.println("[JCBA] missing location or token");
    return "";
  }

  int locStart = payload.indexOf('\"', locIdx + 11);
  int locEnd = payload.indexOf('\"', locStart + 1);
  String location = payload.substring(locStart + 1, locEnd);

  int tokStart = payload.indexOf('\"', tokenIdx + 8);
  int tokEnd = payload.indexOf('\"', tokStart + 1);
  String token = payload.substring(tokStart + 1, tokEnd);

  // location is formatted like "wss://os1308.radimo.smen.biz:443/socket?burst=5"
  String host;
  int port = 443;
  int protoEnd = location.indexOf("://");
  if (protoEnd >= 0) {
    int hostStart = protoEnd + 3;
    int hostEnd = location.indexOf(':', hostStart);
    int slashEnd = location.indexOf('/', hostStart);
    if (hostEnd >= 0 && (slashEnd < 0 || hostEnd < slashEnd)) {
      host = location.substring(hostStart, hostEnd);
      int portEnd = (slashEnd >= 0) ? slashEnd : location.length();
      port = location.substring(hostEnd + 1, portEnd).toInt();
    } else if (slashEnd >= 0) {
      host = location.substring(hostStart, slashEnd);
    } else {
      host = location.substring(hostStart);
    }
  }

  if (host.isEmpty() || token.isEmpty()) {
    Serial.println("[JCBA] failed to parse host or token");
    return "";
  }

  // Construct HTTPS audio/ogg stream URL with authorization token
  String streamUrl = "https://" + host + ":" + String(port) + "/" + stationId + "/0/high.ogg?token=" + token;
  Serial.printf("[JCBA] Stream URL resolved: %s\n", streamUrl.c_str());
  return streamUrl;
}
