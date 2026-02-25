#include "web_console.h"

#include <ArduinoJson.h>
#include "feature_flags.h"
#if LRS_ENABLE_AUTOMATIONS
#include <LittleFS.h>

#include "automation_rules_store.h"
#endif
#include "logger.h"
#include "web_console_internal.h"

using namespace webconsole_internal;

void WebConsole::handleGetAutomationRules() {
#if !LRS_ENABLE_AUTOMATIONS
  sendTracked(404, "application/json", "{\"ok\":false,\"error\":\"disabled\"}");
  return;
#else
  const uint32_t startMs = millis();
  const uint32_t heapBefore = lrslog::heapFree();
  const uint32_t blockBefore = lrslog::heapMaxFreeBlock();
  if (rejectApiIfLowHeap("/api/automation-rules", kApiLightLowHeapRejectFreeBytes, kApiLightLowHeapRejectMaxBlockBytes)) return;

  if (!automation_rules::Store::exists()) {
    const size_t len = automation_rules::Store::defaultJsonLength();
    server_.setContentLength(len);
    markResponseStatus(200);
    server_.send(200, "application/json", "");
    automation_rules::Store::writeDefaultJson(server_.client());
    LRS_LOGI(API,
             "event=automations_api_get source=default status=200 bytes=%lu dur_ms=%lu heap_before=%lu heap_after=%lu block_before=%lu block_after=%lu",
             static_cast<unsigned long>(len),
             static_cast<unsigned long>(millis() - startMs),
             static_cast<unsigned long>(heapBefore),
             static_cast<unsigned long>(lrslog::heapFree()),
             static_cast<unsigned long>(blockBefore),
             static_cast<unsigned long>(lrslog::heapMaxFreeBlock()));
    return;
  }

  File f = LittleFS.open(automation_rules::Store::rulesPath(), "r");
  if (!f) {
    LRS_LOGE(FS, "event=automation_rules_open_failed path=%s mode=r", automation_rules::Store::rulesPath());
    sendTracked(500, "application/json", "{\"ok\":false,\"error\":\"open_failed\"}");
    LRS_LOGW(API,
             "event=automations_api_get status=500 error=open_failed dur_ms=%lu heap_before=%lu heap_after=%lu block_before=%lu block_after=%lu",
             static_cast<unsigned long>(millis() - startMs),
             static_cast<unsigned long>(heapBefore),
             static_cast<unsigned long>(lrslog::heapFree()),
             static_cast<unsigned long>(blockBefore),
             static_cast<unsigned long>(lrslog::heapMaxFreeBlock()));
    return;
  }

  const size_t len = static_cast<size_t>(f.size());
  server_.setContentLength(len);
  markResponseStatus(200);
  server_.send(200, "application/json", "");
  uint8_t buf[192];
  while (f.available()) {
    const size_t n = f.read(buf, sizeof(buf));
    if (n == 0) break;
    server_.client().write(buf, n);
    yield();
  }
  f.close();
  LRS_LOGI(API,
           "event=automations_api_get source=file status=200 bytes=%lu dur_ms=%lu heap_before=%lu heap_after=%lu block_before=%lu block_after=%lu",
           static_cast<unsigned long>(len),
           static_cast<unsigned long>(millis() - startMs),
           static_cast<unsigned long>(heapBefore),
           static_cast<unsigned long>(lrslog::heapFree()),
           static_cast<unsigned long>(blockBefore),
           static_cast<unsigned long>(lrslog::heapMaxFreeBlock()));
#endif
}

void WebConsole::handlePostAutomationRules() {
#if !LRS_ENABLE_AUTOMATIONS
  sendTracked(404, "application/json", "{\"ok\":false,\"error\":\"disabled\"}");
  return;
#else
  const uint32_t startMs = millis();
  const uint32_t heapBefore = lrslog::heapFree();
  const uint32_t blockBefore = lrslog::heapMaxFreeBlock();
  if (rejectApiIfLowHeap("/api/automation-rules", kApiLowHeapRejectFreeBytes, kApiLowHeapRejectMaxBlockBytes)) return;

  const String body = server_.arg("plain");
  automation_rules::SaveResult result = automation_rules::Store::validateAndSave(body);
  if (!result.ok) {
    DynamicJsonDocument doc(256);
    doc["ok"] = false;
    doc["error"] = result.error_code;
    if (result.detail[0] != '\0') doc["detail"] = result.detail;
    String out;
    serializeJson(doc, out);
    sendTracked(result.http_status, "application/json", out);
    LRS_LOGW(API,
             "event=automations_api_post status=%u error=%s body_bytes=%lu dur_ms=%lu heap_before=%lu heap_after=%lu block_before=%lu block_after=%lu",
             static_cast<unsigned>(result.http_status),
             result.error_code ? result.error_code : "invalid",
             static_cast<unsigned long>(body.length()),
             static_cast<unsigned long>(millis() - startMs),
             static_cast<unsigned long>(heapBefore),
             static_cast<unsigned long>(lrslog::heapFree()),
             static_cast<unsigned long>(blockBefore),
             static_cast<unsigned long>(lrslog::heapMaxFreeBlock()));
    return;
  }

  DynamicJsonDocument doc(192);
  doc["ok"] = true;
  doc["saved_bytes"] = static_cast<uint32_t>(result.saved_bytes);
  doc["path"] = automation_rules::Store::rulesPath();
  String out;
  serializeJson(doc, out);
  if (on_automations_saved_) {
    on_automations_saved_();
  }
  sendTracked(200, "application/json", out);
  LRS_LOGI(API,
           "event=automations_api_post status=200 body_bytes=%lu saved_bytes=%lu dur_ms=%lu heap_before=%lu heap_after=%lu block_before=%lu block_after=%lu",
           static_cast<unsigned long>(body.length()),
           static_cast<unsigned long>(result.saved_bytes),
           static_cast<unsigned long>(millis() - startMs),
           static_cast<unsigned long>(heapBefore),
           static_cast<unsigned long>(lrslog::heapFree()),
           static_cast<unsigned long>(blockBefore),
           static_cast<unsigned long>(lrslog::heapMaxFreeBlock()));
#endif
}
