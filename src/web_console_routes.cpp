#include "web_console.h"

#include "web_console_internal.h"
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>

using namespace webconsole_internal;

void WebConsole::routes() {
  server_.on("/", HTTP_GET, [this]() {
    beginRequestLog("/", false, false, true);
    handleIndex();
    finishRequestLog();
  });
  server_.on("/login", HTTP_GET, [this]() {
    beginRequestLog("/login", false, false, true);
    handleLoginPage();
    finishRequestLog();
  });
  server_.on("/setup", HTTP_GET, [this]() {
    beginRequestLog("/setup", false, false, true);
    handleFleetSetupPage();
    finishRequestLog();
  });
  server_.on("/api/login", HTTP_POST, [this]() {
    beginRequestLog("/api/login", true, false, true);
    handleLoginApi();
    finishRequestLog();
  });
  server_.on("/api/setup/fleet-key", HTTP_POST, [this]() {
    beginRequestLog("/api/setup/fleet-key", true, false, true);
    handleFleetSetupApi();
    finishRequestLog();
  });
  server_.on("/api/setup/commissioning", HTTP_POST, [this]() {
    beginRequestLog("/api/setup/commissioning", true, false, true);
    handleSetupCommissioningApi();
    finishRequestLog();
  });
  server_.on("/api/logout", HTTP_POST, [this]() {
    beginRequestLog("/api/logout", true, false, false);
    handleLogoutApi();
    finishRequestLog();
  });

  server_.on("/generate_204", HTTP_GET,
             [this]() { handleCaptiveProbe(); }); // Android
  server_.on("/gen_204", HTTP_GET,
             [this]() { handleCaptiveProbe(); }); // Android (variant)
  server_.on("/hotspot-detect.html", HTTP_GET,
             [this]() { handleCaptiveProbe(); }); // Apple
  server_.on("/ncsi.txt", HTTP_GET,
             [this]() { handleCaptiveProbe(); }); // Windows
  server_.on("/connecttest.txt", HTTP_GET,
             [this]() { handleCaptiveProbe(); }); // Windows
  server_.on("/fwlink", HTTP_GET,
             [this]() { handleCaptiveProbe(); }); // Windows

  server_.on("/api/status-live", HTTP_GET, [this]() {
    beginRequestLog("/api/status-live", true, true, true);
    if (!requireAuth(true)) {
      finishRequestLog();
      return;
    }
    handleStatusLive();
    finishRequestLog();
  });
  server_.on("/api/status-live/events", HTTP_GET, [this]() {
    beginRequestLog("/api/status-live/events", true, true, true);
    if (!requireAuth(true)) {
      finishRequestLog();
      return;
    }
    handleStatusLiveEvents();
    finishRequestLog();
  });
  server_.on("/api/status-static", HTTP_GET, [this]() {
    beginRequestLog("/api/status-static", true, false, true);
    if (!requireAuth(true)) {
      finishRequestLog();
      return;
    }
    handleStatusStatic();
    finishRequestLog();
  });
  server_.on("/api/status-lite", HTTP_GET, [this]() {
    beginRequestLog("/api/status-lite", true, true, true);
    if (!requireAuth(true)) {
      finishRequestLog();
      return;
    }
    handleStatusLite();
    finishRequestLog();
  });
  server_.on("/api/fleet", HTTP_GET, [this]() {
    beginRequestLog("/api/fleet", true, false, true);
    if (!requireAuth(true)) {
      finishRequestLog();
      return;
    }
    handleFleet();
    finishRequestLog();
  });
#if LRS_ENABLE_AUTOMATIONS
  server_.on("/api/automation-rules", HTTP_GET, [this]() {
    beginRequestLog("/api/automation-rules", true, false, true);
    if (!requireAuth(true)) {
      finishRequestLog();
      return;
    }
    handleGetAutomationRules();
    finishRequestLog();
  });
  server_.on("/api/automation-rules", HTTP_POST, [this]() {
    beginRequestLog("/api/automation-rules", true, false, true);
    if (!requireAuth(true)) {
      finishRequestLog();
      return;
    }
    handlePostAutomationRules();
    finishRequestLog();
  });
#endif
  server_.on("/api/factory", HTTP_GET, [this]() {
    if (!requireAuth(true))
      return;
    handleFactory();
  });
  server_.on("/api/wifi/scan", HTTP_GET, [this]() { handleWifiScan(); });
  server_.on("/api/network/test", HTTP_POST, [this]() { handleTestSta(); });
  server_.on("/api/network/provision-fleet", HTTP_POST, [this]() {
    beginRequestLog("/api/network/provision-fleet", true, false, true);
    handleProvisionFleetWifi();
    finishRequestLog();
  });
  server_.on("/api/provisioning/start", HTTP_POST, [this]() {
    beginRequestLog("/api/provisioning/start", true, false, true);
    handleProvisioningStart();
    finishRequestLog();
  });
  server_.on("/api/provisioning/status", HTTP_GET, [this]() {
    beginRequestLog("/api/provisioning/status", true, true, true);
    handleProvisioningStatus();
    finishRequestLog();
  });
  server_.on("/api/provisioning/provision-all", HTTP_POST, [this]() {
    beginRequestLog("/api/provisioning/provision-all", true, false, true);
    handleProvisioningProvisionAll();
    finishRequestLog();
  });
  server_.on("/api/provisioning/cancel", HTTP_POST, [this]() {
    beginRequestLog("/api/provisioning/cancel", true, false, true);
    handleProvisioningCancel();
    finishRequestLog();
  });
  server_.on("/api/mqtt/test", HTTP_POST, [this]() { handleTestMqtt(); });
  server_.on("/api/logging/udp", HTTP_POST, [this]() {
    beginRequestLog("/api/logging/udp", true, false, true);
    handleUdpLogging();
    finishRequestLog();
  });
  server_.on("/api/settings", HTTP_GET, [this]() {
    if (!requireAuth(true))
      return;
    handleGetSettings();
  });
  server_.on("/api/settings", HTTP_POST, [this]() { handlePostSettings(); });
  server_.on("/api/settings/export", HTTP_GET,
             [this]() { handleExportSettings(); });
  server_.on("/api/settings/import", HTTP_POST,
             [this]() { handleImportSettings(); });
  server_.on(
      "/api/ota", HTTP_POST, [this]() { handleOtaUpload(); },
      [this]() { handleOtaUploadChunk(); });
  server_.on("/api/logs.csv", HTTP_GET, [this]() { handleLogsCsv(); });
  server_.on("/api/logs.txt", HTTP_GET, [this]() { handleLogsText(); });
  server_.on("/api/system/factory-reset", HTTP_POST,
             [this]() { handleFactoryReset(); });
  server_.on("/api/reboot", HTTP_POST, [this]() { handleReboot(); });
  server_.onNotFound([this]() {
    if (server_.method() == HTTP_POST &&
        handleFleetDeviceActionRoute(server_.uri())) {
      return;
    }
    if (isSoftApActive()) {
      handleCaptiveProbe();
      return;
    }
    server_.send(404, "text/plain", "not found");
  });
}
