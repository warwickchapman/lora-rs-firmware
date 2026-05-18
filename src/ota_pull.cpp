#include "ota_pull.h"

#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <SHA256.h>
#include <Updater.h>

#include "logger.h"

namespace {
bool isSha256Hex(const char *hex) {
  if (hex == nullptr || strlen(hex) != 64) return false;
  for (size_t i = 0; i < 64; ++i) {
    const char c = hex[i];
    const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                    (c >= 'A' && c <= 'F');
    if (!ok) return false;
  }
  return true;
}

bool hexDigestEquals(const uint8_t digest[32], const char *hex) {
  if (!isSha256Hex(hex)) return false;
  char out[65];
  for (size_t i = 0; i < 32; ++i) {
    snprintf(out + (i * 2), 3, "%02x", digest[i]);
  }
  out[64] = '\0';
  return strcasecmp(out, hex) == 0;
}
}

bool otaPullFromUrl(const char *url, const char *sha256Hex, String &error) {
  if (url == nullptr || url[0] == '\0') {
    error = "missing_url";
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    error = "wifi_not_connected";
    return false;
  }
  if (sha256Hex == nullptr || sha256Hex[0] == '\0') {
    error = "missing_sha256";
    return false;
  }
  if (!isSha256Hex(sha256Hex)) {
    error = "invalid_sha256";
    return false;
  }

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) {
    error = "http_begin_failed";
    return false;
  }

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    error = String("http_") + code;
    http.end();
    return false;
  }

  const int contentLength = http.getSize();
  if (contentLength <= 0) {
    error = "missing_content_length";
    http.end();
    return false;
  }

  const uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
  if (!Update.begin(maxSketchSpace)) {
    error = "update_begin_failed";
    http.end();
    return false;
  }

  SHA256 hash;
  hash.reset();
  WiFiClient *stream = http.getStreamPtr();
  uint8_t buf[512];
  int remaining = contentLength;
  size_t written = 0;
  uint32_t lastProgressMs = millis();

  while (remaining > 0 && http.connected()) {
    const size_t available = stream->available();
    if (available == 0) {
      if ((millis() - lastProgressMs) > 15000UL) {
        error = "download_timeout";
        Update.end(false);
        http.end();
        return false;
      }
      delay(1);
      continue;
    }

    const size_t want = min(sizeof(buf), min(static_cast<size_t>(remaining), available));
    const int n = stream->readBytes(buf, want);
    if (n <= 0) {
      error = "download_read_failed";
      Update.end(false);
      http.end();
      return false;
    }
    lastProgressMs = millis();
    hash.update(buf, static_cast<size_t>(n));
    if (Update.write(buf, static_cast<size_t>(n)) != static_cast<size_t>(n)) {
      error = "update_write_failed";
      Update.end(false);
      http.end();
      return false;
    }
    written += static_cast<size_t>(n);
    remaining -= n;
    delay(0);
  }

  if (written != static_cast<size_t>(contentLength)) {
    error = "download_incomplete";
    Update.end(false);
    http.end();
    return false;
  }

  uint8_t digest[32];
  hash.finalize(digest, sizeof(digest));
  if (!hexDigestEquals(digest, sha256Hex)) {
    error = "sha256_mismatch";
    Update.end(false);
    http.end();
    return false;
  }

  if (!Update.end(true)) {
    error = "update_end_failed";
    http.end();
    return false;
  }

  http.end();
  LRS_LOGW(SYS, "event=ota_pull_success bytes=%lu", static_cast<unsigned long>(written));
  return true;
}
