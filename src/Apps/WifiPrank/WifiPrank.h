#pragma once

#include "../../PocketGame/PocketGame.h"

#include <AsyncUDP.h>
#include <WebServer.h>

class WifiPrankGame final : public pocketgame::IGame {
public:
  const char *id() const override { return "wifi-prank"; }
  const char *title() const override { return "FREE WIFI"; }
  const char *description() const override { return "RICKROLL HOTSPOT"; }
  const char *storageNamespace() const override { return "wifi-prank"; }

  void begin(pocketgame::PocketGameSystem &system) override;
  void loop(pocketgame::PocketGameSystem &system, uint32_t now) override;

private:
  enum class ControlAction : uint8_t { START_SETUP, START_PRANK, FORGET, EXIT };

  static constexpr uint8_t MAX_SSID_BYTES = 33;
  static constexpr uint8_t MAX_PASSWORD_BYTES = 65;
  static constexpr uint8_t MAX_PRANK_URL_BYTES = 193;
  static constexpr uint8_t DNS_PENDING_COUNT = 8;
  static constexpr uint8_t MAX_SCANNED_NETWORKS = 16;

  struct PendingDnsQuery {
    bool used = false;
    uint16_t id = 0;
    IPAddress clientIp;
    uint16_t clientPort = 0;
  };

  pocketgame::PocketGameSystem *system_ = nullptr;
  AsyncUDP dnsProxy_;
  WebServer webServer_{80};
  bool routesConfigured_ = false;
  bool accessPointStarted_ = false;
  bool dnsStarted_ = false;
  bool dnsForwarding_ = false;
  bool naptEnabled_ = false;
  bool upstreamConnecting_ = false;
  uint32_t lastUpstreamAttemptAt_ = 0;
  uint32_t lastRenderAt_ = 0;
  ControlAction selectedAction_ = ControlAction::START_SETUP;
  IPAddress accessPointIp_;
  IPAddress upstreamDns_;
  char upstreamSsid_[MAX_SSID_BYTES] = {};
  char upstreamPassword_[MAX_PASSWORD_BYTES] = {};
  char prankUrl_[MAX_PRANK_URL_BYTES] = {};
  char scannedSsids_[MAX_SCANNED_NETWORKS][MAX_SSID_BYTES] = {};
  int32_t scannedRssi_[MAX_SCANNED_NETWORKS] = {};
  uint8_t scannedNetworkCount_ = 0;
  PendingDnsQuery pendingDns_[DNS_PENDING_COUNT] = {};

  void configureRoutes();
  void handlePortalRequest();
  void handleSaveUpstream();
  void handleForgetUpstream();
  void handleRescanUpstream();
  void sendSetupPage(const String &notice = String());
  void sendPrankPage();

  void loadSettings();
  void saveUpstreamSettings(const String &ssid, const String &password,
                            const String &prankUrl);
  void forgetUpstream();
  bool hasUpstreamSettings() const { return upstreamSsid_[0] != '\0'; }

  void startSetupWifi();
  void startPrankWifi();
  void startAccessPoint(bool withUpstreamRadio);
  void scanUpstreamNetworks();
  void stopPrankWifi();
  void startUpstreamConnection();
  void updateUpstream(uint32_t now);
  void enableRouting();
  void disableRouting();

  void startDnsProxy();
  void stopDnsProxy();
  void handleDnsPacket(AsyncUDPPacket &packet);
  void sendLocalDnsResponse(AsyncUDPPacket &packet);
  void forwardDnsQuery(AsyncUDPPacket &packet);
  void sendForwardedDnsResponse(AsyncUDPPacket &packet);

  void render(pocketgame::PocketGameSystem &system, uint32_t now);
  void selectNextAction();
  void performSelectedAction();
};
