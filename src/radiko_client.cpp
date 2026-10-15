#include "radiko_client.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "mbedtls/base64.h"

extern bool isRadikoPlaybackActive();

RadikoClient& RadikoClient::instance() {
  static RadikoClient sInstance;
  return sInstance;
}

RadikoClient::RadikoClient() {
  initDefaultStations();
}

void RadikoClient::initDefaultStations() {
  stations_ = {
      {"TBS", "TBSラジオ", "JP13"},
      {"QRR", "文化放送", "JP13"},
      {"LFR", "ニッポン放送", "JP13"},
      {"INT", "interfm", "JP13"},
      {"FMT", "TOKYO FM", "JP13"},
      {"FMJ", "J-WAVE", "JP13"},
      {"JORF", "ラジオ日本", "JP13"},
      {"BAYFM78", "BAYFM78", "JP12"},
      {"NACK5", "NACK5", "JP11"},
      {"YFM", "FMヨコハマ", "JP14"},
  };
}

const RadikoStation* RadikoClient::getStation(size_t index) const {
  if (index < stations_.size()) {
    return &stations_[index];
  }
  return nullptr;
}

bool RadikoClient::isAuthenticated() const {
  if (authToken_.isEmpty()) {
    return false;
  }
  if (tokenIssuedMs_ > 0 && millis() - tokenIssuedMs_ > 90UL * 60UL * 1000UL) {
    return false;
  }
  return true;
}

void RadikoClient::invalidateToken() {
  authToken_ = "";
  tokenIssuedMs_ = 0;
}

bool RadikoClient::authenticate() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[Radiko] WiFi not connected, cannot authenticate");
    return false;
  }

  Serial.println("[Radiko] Starting authentication...");
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

  // Part 1: auth1
  http.begin(client, "https://radiko.jp/v2/api/auth1");
  http.addHeader("User-Agent", "esp32/4");
  http.addHeader("Accept", "*/*");
  http.addHeader("X-Radiko-App", "pc_html5");
  http.addHeader("X-Radiko-App-Version", "0.0.1");
  http.addHeader("X-Radiko-User", "dummy_user");
  http.addHeader("X-Radiko-Device", "pc");

  const char* headerKeys[] = {"X-Radiko-AuthToken", "X-Radiko-KeyLength", "X-Radiko-KeyOffset"};
  http.collectHeaders(headerKeys, 3);

  int code = http.GET();
  if (code != HTTP_CODE_OK && code != HTTP_CODE_MOVED_PERMANENTLY) {
    Serial.printf("[Radiko] auth1 failed with HTTP %d\n", code);
    http.end();
    return false;
  }

  authToken_ = http.header("X-Radiko-AuthToken");
  const int keyLength = http.header("X-Radiko-KeyLength").toInt();
  const int keyOffset = http.header("X-Radiko-KeyOffset").toInt();
  http.end();

  if (authToken_.isEmpty() || keyLength <= 0 || keyOffset < 0 || keyOffset + keyLength > 40) {
    Serial.println("[Radiko] auth1 invalid response headers");
    return false;
  }

  // Generate partialKey
  static const char* kAuthKey = "bcd151073c03b352e1ef2fd66c32209da9ca0afa";
  char tokenPart[48] = {0};
  memcpy(tokenPart, kAuthKey + keyOffset, keyLength);
  tokenPart[keyLength] = '\0';

  unsigned char b64Buf[64] = {0};
  size_t outLen = 0;
  mbedtls_base64_encode(b64Buf, sizeof(b64Buf), &outLen,
                        reinterpret_cast<const unsigned char*>(tokenPart), keyLength);
  const String partialKey = reinterpret_cast<const char*>(b64Buf);

  // Part 2: auth2
  http.begin(client, "https://radiko.jp/v2/api/auth2");
  http.addHeader("User-Agent", "esp32/4");
  http.addHeader("Accept", "*/*");
  http.addHeader("X-Radiko-App", "pc_html5");
  http.addHeader("X-Radiko-App-Version", "0.0.1");
  http.addHeader("X-Radiko-User", "dummy_user");
  http.addHeader("X-Radiko-Device", "pc");
  http.addHeader("X-Radiko-AuthToken", authToken_);
  http.addHeader("X-Radiko-Partialkey", partialKey);
  http.addHeader("X-Radiko-Connection", "wifi");

  int code2 = http.GET();
  if (code2 != HTTP_CODE_OK && code2 != HTTP_CODE_MOVED_PERMANENTLY) {
    Serial.printf("[Radiko] auth2 failed with HTTP %d\n", code2);
    http.end();
    return false;
  }

  const String payload = http.getString();
  http.end();

  const int comma = payload.indexOf(',');
  if (comma > 0) {
    areaId_ = payload.substring(0, comma);
  } else {
    areaId_ = "JP13";
  }
  tokenIssuedMs_ = millis();
  if (tokenIssuedMs_ == 0) {
    tokenIssuedMs_ = 1;
  }

  Serial.printf("[Radiko] authenticated: area=%s, token=%s\n", areaId_.c_str(), authToken_.c_str());

  // Fetch station list for current area
  fetchStations();

  return true;
}

bool RadikoClient::fetchStations() {
  if (areaId_.isEmpty() || authToken_.isEmpty()) {
    return false;
  }

  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

  const String url = "https://radiko.jp/v2/station/list/" + areaId_ + ".xml";
  http.begin(client, url);
  http.addHeader("X-Radiko-AuthToken", authToken_);

  const int code = http.GET();
  if (code != HTTP_CODE_OK && code != HTTP_CODE_MOVED_PERMANENTLY) {
    Serial.printf("[Radiko] fetchStations HTTP %d\n", code);
    http.end();
    return false;
  }

  const String xml = http.getString();
  http.end();

  std::vector<RadikoStation> parsed;
  int pos = 0;
  while ((pos = xml.indexOf("<station>", pos)) != -1) {
    const int endStation = xml.indexOf("</station>", pos);
    if (endStation == -1) {
      break;
    }
    const String stationBlock = xml.substring(pos, endStation);

    const int idStart = stationBlock.indexOf("<id>");
    const int idEnd = stationBlock.indexOf("</id>");
    const int nameStart = stationBlock.indexOf("<name>");
    const int nameEnd = stationBlock.indexOf("</name>");

    if (idStart != -1 && idEnd != -1 && nameStart != -1 && nameEnd != -1) {
      RadikoStation st;
      st.id = stationBlock.substring(idStart + 4, idEnd);
      st.name = decodeHtmlEntities(stationBlock.substring(nameStart + 6, nameEnd));
      st.areaId = areaId_;
      parsed.push_back(st);
    }
    pos = endStation + 10;
  }

  if (!parsed.empty()) {
    stations_ = parsed;
    Serial.printf("[Radiko] loaded %u stations for area %s\n",
                  static_cast<unsigned>(stations_.size()), areaId_.c_str());
    return true;
  }

  return false;
}

String RadikoClient::getStreamUrl(const String& stationId) const {
  return "https://f-radiko.smartstream.ne.jp/" + stationId + "/_definst_/simul-stream.stream/playlist.m3u8";
}

String RadikoClient::fetchNowProgramTitle(const String& stationId) {
  if (areaId_.isEmpty()) {
    return "";
  }

  HTTPClient http;
  WiFiClient client;
  const String url = "http://radiko.jp/v3/program/now/" + areaId_ + ".xml";
  http.begin(client, url);

  const int code = http.GET();
  if (code != HTTP_CODE_OK && code != HTTP_CODE_MOVED_PERMANENTLY) {
    http.end();
    return "";
  }

  const String xml = http.getString();
  http.end();

  String target = "<station id=\"" + stationId + "\">";
  int stPos = xml.indexOf(target);
  if (stPos == -1) {
    target = "<station id='" + stationId + "'>";
    stPos = xml.indexOf(target);
  }
  if (stPos == -1) {
    return "";
  }

  int endStation = xml.indexOf("</station>", stPos);
  if (endStation == -1) {
    endStation = xml.length();
  }
  const String block = xml.substring(stPos, endStation);

  const int titleStart = block.indexOf("<title>");
  const int titleEnd = block.indexOf("</title>");
  if (titleStart != -1 && titleEnd != -1) {
    return decodeHtmlEntities(block.substring(titleStart + 7, titleEnd));
  }
  return "";
}

String RadikoClient::decodeHtmlEntities(const String& src) {
  String out = src;
  out.replace("&amp;", "&");
  out.replace("&quot;", "\"");
  out.replace("&#39;", "'");
  out.replace("&lt;", "<");
  out.replace("&gt;", ">");
  out.replace("&#x2F;", "/");
  return out;
}

extern "C" const char* pocket_audio_get_radiko_token() {
  if (isRadikoPlaybackActive()) {
    const auto& tok = RadikoClient::instance().getAuthToken();
    if (!tok.isEmpty()) {
      return tok.c_str();
    }
  }
  return nullptr;
}
