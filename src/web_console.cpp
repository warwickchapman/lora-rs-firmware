#include "web_console.h"

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Updater.h>
#include <cstring>

#include "build_info.h"
#include "config_store.h"
#include "logger.h"
#include "sensor_manager.h"
#include "state_machine.h"
#include "web_console_internal.h"
#include "web_console_ui_assets.h"

#ifndef LRS_ENABLE_MDNS
#define LRS_ENABLE_MDNS 1
#endif

using namespace webconsole_internal;

bool WebConsole::begin(ConfigStore *config,
                       NodeStateMachine *sm,
                       SensorManager *sensors,
                       std::function<void(bool, bool)> onApply,
                       std::function<void()> onAutomationsSaved) {
  config_ = config;
  sm_ = sm;
  sensors_ = sensors;
  on_apply_ = onApply;
  on_automations_saved_ = onAutomationsSaved;
  server_.collectHeaders("Cookie", "User-Agent");
  initStatusCaches();

  routes();
  server_.begin();
  return true;
}

void WebConsole::tick() {
  server_.handleClient();
  tickStatusLiveSse();
}

void WebConsole::beginRequestLog(const char *path, bool api, bool poll, bool heapDiag) {
  request_log_.active = true;
  request_log_.api = api;
  request_log_.poll = poll;
  request_log_.heap_diag = heapDiag;
  request_log_.started_ms = millis();
  request_log_.status = 0;
  request_log_.path = path;
  const IPAddress remote = server_.client().remoteIP();
  request_log_.client_ip[0] = remote[0];
  request_log_.client_ip[1] = remote[1];
  request_log_.client_ip[2] = remote[2];
  request_log_.client_ip[3] = remote[3];
}

void WebConsole::finishRequestLog() {
  if (!request_log_.active) return;

  const uint32_t endMs = millis();
  const uint32_t durMs = endMs - request_log_.started_ms;
  const int status = request_log_.status;
  const char *path = request_log_.path ? request_log_.path : "(unknown)";
  char ip[16];
  snprintf(ip,
           sizeof(ip),
           "%u.%u.%u.%u",
           static_cast<unsigned>(request_log_.client_ip[0]),
           static_cast<unsigned>(request_log_.client_ip[1]),
           static_cast<unsigned>(request_log_.client_ip[2]),
           static_cast<unsigned>(request_log_.client_ip[3]));
  const uint32_t freeHeap = lrslog::heapFree();
  const uint8_t heapFrag = lrslog::heapFragPercent();
  const uint32_t maxBlock = lrslog::heapMaxFreeBlock();
  const lrslog::Category cat = request_log_.api ? lrslog::Category::API : lrslog::Category::WEB;
  const lrslog::Level level = request_log_.poll ? lrslog::Level::DEBUG : lrslog::Level::INFO;

  if (request_log_.heap_diag) {
    lrslog::logf(level,
                 cat,
                 "event=request method=%s path=%s status=%d dur_ms=%lu ip=%s heap_free=%lu heap_frag=%u max_free_block=%lu",
                 httpMethodText(server_.method()),
                 path,
                 status,
                 static_cast<unsigned long>(durMs),
                 ip,
                 static_cast<unsigned long>(freeHeap),
                 static_cast<unsigned>(heapFrag),
                 static_cast<unsigned long>(maxBlock));
    if (freeHeap < kLowHeapWarnThresholdBytes &&
        (last_low_heap_warn_ms_ == 0 || (endMs - last_low_heap_warn_ms_) >= kLowHeapWarnMinIntervalMs)) {
      last_low_heap_warn_ms_ = endMs;
      LRS_LOGW(API,
               "event=low_heap path=%s heap_free=%lu heap_frag=%u max_free_block=%lu dur_ms=%lu",
               path,
               static_cast<unsigned long>(freeHeap),
               static_cast<unsigned>(heapFrag),
               static_cast<unsigned long>(maxBlock),
               static_cast<unsigned long>(durMs));
    }
  } else {
    lrslog::logf(level,
                 cat,
                 "event=request method=%s path=%s status=%d dur_ms=%lu ip=%s",
                 httpMethodText(server_.method()),
                 path,
                 status,
                 static_cast<unsigned long>(durMs),
                 ip);
  }

  if (durMs >= kWebRequestPressureDurMs) {
    // Local proxy for "recent slow-phase pressure" used by SSE pacing.
    // We don't currently have app-level slow-phase state exposed to WebConsole.
    last_web_pressure_ms_ = endMs;
  }

  request_log_ = RequestLogState{};
}

void WebConsole::markResponseStatus(int status) {
  if (request_log_.active) request_log_.status = status;
}

void WebConsole::sendTracked(int code, const char *contentType, const char *body) {
  markResponseStatus(code);
  server_.send(code, contentType, body);
}

void WebConsole::sendTracked(int code, const char *contentType, const String &body) {
  markResponseStatus(code);
  server_.send(code, contentType, body);
}

void WebConsole::initStatusCaches() {
  // Keep cache allocation lazy on low-RAM boards; reserve() here can OOM during boot.
  status_live_cache_.built_ms = 0;
  status_static_cache_.built_ms = 0;
  status_lite_cache_.built_ms = 0;
}

bool WebConsole::apiHeapHealthy(uint32_t minFreeBytes, uint32_t minMaxBlockBytes) const {
  const uint32_t freeHeap = lrslog::heapFree();
  const uint32_t maxBlock = lrslog::heapMaxFreeBlock();
  return freeHeap >= minFreeBytes && (minMaxBlockBytes == 0 || maxBlock >= minMaxBlockBytes);
}

bool WebConsole::tryServeCachedJson(const char *path,
                                    uint32_t minFreeBytes,
                                    uint32_t minMaxBlockBytes,
                                    uint32_t ttlMs,
                                    JsonResponseCache &cache) {
  const uint32_t now = millis();
  if (cache.body.length() > 0 && ttlMs > 0 && (now - cache.built_ms) < ttlMs) {
    sendTracked(200, "application/json", cache.body);
    return true;
  }

  if (apiHeapHealthy(minFreeBytes, minMaxBlockBytes)) {
    return false;
  }

  if (cache.body.length() > 0) {
    LRS_LOGW(API,
             "event=api_cached_stale path=%s heap_free=%lu heap_frag=%u max_free_block=%lu age_ms=%lu",
             path ? path : server_.uri().c_str(),
             static_cast<unsigned long>(lrslog::heapFree()),
             static_cast<unsigned>(lrslog::heapFragPercent()),
             static_cast<unsigned long>(lrslog::heapMaxFreeBlock()),
             static_cast<unsigned long>(now - cache.built_ms));
    sendTracked(200, "application/json", cache.body);
    return true;
  }

  return rejectApiIfLowHeap(path, minFreeBytes, minMaxBlockBytes);
}

void WebConsole::setUiNoStoreHeaders() {
  server_.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server_.sendHeader("Pragma", "no-cache");
  server_.sendHeader("Expires", "0");
}

void WebConsole::closeStatusLiveSse() {
  if (status_live_sse_client_) {
    status_live_sse_client_.flush();
    status_live_sse_client_.stop();
  }
  status_live_sse_client_ = WiFiClient();
  status_live_sse_active_ = false;
  status_live_sse_last_push_ms_ = 0;
  status_live_sse_last_keepalive_ms_ = 0;
  status_live_sse_last_sent_cache_ms_ = 0;
  status_live_sse_last_interval_ms_ = 0;
}

uint32_t WebConsole::computeStatusLiveSseIntervalMs() const {
  const uint32_t now = millis();
  const uint32_t freeHeap = lrslog::heapFree();
  const uint32_t maxBlock = lrslog::heapMaxFreeBlock();
  const uint32_t ratioPct = (freeHeap == 0U) ? 0U : ((maxBlock * 100U) / freeHeap);
  const bool recentPressure =
      (last_web_pressure_ms_ != 0U) && (static_cast<uint32_t>(now - last_web_pressure_ms_) < kStatusLiveSsePressureWindowMs);

  if (maxBlock < 1200U || ratioPct < 30U || freeHeap < 3000U) return kStatusLiveSsePushSevereMs;
  if (maxBlock < 1700U || ratioPct < 40U || (recentPressure && maxBlock < 2600U)) return kStatusLiveSsePushPressureMs;
  if (maxBlock < 2400U || ratioPct < 55U || recentPressure) return kStatusLiveSsePushWarnMs;
  return kStatusLiveSsePushHealthyMs;
}

bool WebConsole::rejectApiIfLowHeap(const char *path, uint32_t minFreeBytes, uint32_t minMaxBlockBytes) {
  const uint32_t freeHeap = lrslog::heapFree();
  const uint32_t maxBlock = lrslog::heapMaxFreeBlock();
  if (freeHeap >= minFreeBytes && (minMaxBlockBytes == 0 || maxBlock >= minMaxBlockBytes)) {
    return false;
  }
  LRS_LOGW(API,
           "event=api_low_heap_reject path=%s heap_free=%lu heap_frag=%u max_free_block=%lu need_free=%lu need_block=%lu",
           path ? path : server_.uri().c_str(),
           static_cast<unsigned long>(freeHeap),
           static_cast<unsigned>(lrslog::heapFragPercent()),
           static_cast<unsigned long>(maxBlock),
           static_cast<unsigned long>(minFreeBytes),
           static_cast<unsigned long>(minMaxBlockBytes));
  char body[160];
  snprintf(body,
           sizeof(body),
           "{\"ok\":false,\"error\":\"low_heap\",\"heap_free\":%lu,\"heap_frag\":%u,\"max_free_block\":%lu}",
           static_cast<unsigned long>(freeHeap),
           static_cast<unsigned>(lrslog::heapFragPercent()),
           static_cast<unsigned long>(maxBlock));
  sendTracked(503, "application/json", body);
  return true;
}

bool WebConsole::hasSession() const {
  if (session_token_.length() == 0) return false;
  if (session_expires_ms_ == 0) return false;
  return static_cast<int32_t>(session_expires_ms_ - millis()) > 0;
}

void WebConsole::clearSession() {
  session_token_ = "";
  session_expires_ms_ = 0;
}

String WebConsole::cookieValue(const String &name) const {
  const String raw = server_.header("Cookie");
  if (raw.length() == 0) return "";
  const String needle = name + "=";
  int p = raw.indexOf(needle);
  if (p < 0) return "";
  p += needle.length();
  int e = raw.indexOf(';', p);
  if (e < 0) e = raw.length();
  String v = raw.substring(p, e);
  v.trim();
  return v;
}

String WebConsole::randomToken() const {
  char out[33];
  for (size_t i = 0; i < 16; i++) {
    const uint8_t b = static_cast<uint8_t>(::random(0, 256));
    snprintf(out + (i * 2), 3, "%02x", b);
  }
  out[32] = '\0';
  return String(out);
}

void WebConsole::startSession() {
  session_token_ = randomToken();
  session_expires_ms_ = millis() + (30UL * 60UL * 1000UL);
  server_.sendHeader("Set-Cookie", "lrs_session=" + session_token_ + "; Path=/; HttpOnly; SameSite=Lax");
}

uint32_t WebConsole::sessionRemainingS() const {
  if (!hasSession()) return 0;
  return static_cast<uint32_t>((session_expires_ms_ - millis()) / 1000UL);
}

bool WebConsole::requireAuth(bool api) {
  if (locked_until_ms_ != 0 && static_cast<int32_t>(locked_until_ms_ - millis()) > 0) {
    if (api) {
      sendTracked(429, "application/json", "{\"error\":\"too_many_failed_logins\"}");
    } else {
      sendTracked(429, "text/plain", "Too many failed logins. Try again shortly.");
    }
    LRS_LOGW(API, "event=auth_locked ip=%s", server_.client().remoteIP().toString().c_str());
    return false;
  }

  if (!hasSession()) {
    if (api) {
      sendTracked(401, "application/json", "{\"error\":\"auth_required\"}");
    } else {
      server_.sendHeader("Location", "/login?expired=1");
      sendTracked(302, "text/plain", "redirect");
    }
    return false;
  }

  const String cookie = cookieValue("lrs_session");
  if (cookie.length() == 0 || cookie != session_token_) {
    if (api) {
      sendTracked(401, "application/json", "{\"error\":\"auth_required\"}");
    } else {
      server_.sendHeader("Location", "/login?expired=1");
      sendTracked(302, "text/plain", "redirect");
    }
    LRS_LOGW(API,
             "event=auth_cookie_invalid ip=%s has_cookie=%u",
             server_.client().remoteIP().toString().c_str(),
             static_cast<unsigned>(cookie.length() ? 1U : 0U));
    return false;
  }
  session_expires_ms_ = millis() + (30UL * 60UL * 1000UL);
  failed_auth_ = 0;
  return true;
}

bool WebConsole::isSoftApActive() const { return softApActiveNow(); }

bool WebConsole::needsFleetSetupPrompt() const {
  if (config_ == nullptr) return false;
  const auto &cfg = config_->settings();
  return isDefaultDeploymentKey(cfg.fleet_passphrase) && !cfg.fleet_setup_prompt_dismissed;
}

void WebConsole::handleCaptiveProbe() {
  if (!isSoftApActive()) {
    sendTracked(204, "text/plain", "");
    return;
  }
  server_.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server_.sendHeader("Pragma", "no-cache");
  server_.sendHeader("Location", "http://192.168.4.1/");
  sendTracked(302, "text/plain", "Redirecting to LRS console");
}
