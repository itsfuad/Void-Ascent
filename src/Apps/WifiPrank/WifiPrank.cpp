#include "WifiPrank.h"

#include <Adafruit_GFX.h>
#include <Arduino.h>
#include <WiFi.h>
#include <string.h>

namespace {

static constexpr int16_t SCREEN_W = pocketgame::config::SCREEN_WIDTH;
static constexpr uint32_t UPSTREAM_CONNECT_TIMEOUT_MS = 15000;
static constexpr uint32_t UPSTREAM_RETRY_INTERVAL_MS = 30000;
static constexpr uint16_t DNS_HEADER_BYTES = 12;
static constexpr size_t DNS_PACKET_BYTES = 512;

static constexpr const char *AP_SSID = "FreeWifi";
static constexpr const char *CHECK_HOST = "connectivitycheck.gstatic.com";
static constexpr const char *DEFAULT_PRANK_URL =
    "https://www.youtube.com/watch?v=dQw4w9WgXcQ";
static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_SUBNET(255, 255, 255, 0);
static const IPAddress AP_DHCP_START(192, 168, 4, 2);
static const IPAddress FALLBACK_DNS(1, 1, 1, 1);

static constexpr uint16_t colour565(uint8_t red, uint8_t green, uint8_t blue) {
  return ((uint16_t)(red & 0xF8) << 8) | ((uint16_t)(green & 0xFC) << 3) |
         ((uint16_t)blue >> 3);
}

static constexpr uint16_t C_BLACK = colour565(3, 7, 14);
static constexpr uint16_t C_NAVY = colour565(7, 18, 36);
static constexpr uint16_t C_PANEL = colour565(13, 29, 48);
static constexpr uint16_t C_PANEL_2 = colour565(20, 42, 67);
static constexpr uint16_t C_BORDER = colour565(48, 83, 115);
static constexpr uint16_t C_TEXT = colour565(239, 246, 247);
static constexpr uint16_t C_MUTED = colour565(119, 149, 171);
static constexpr uint16_t C_CYAN = colour565(48, 200, 231);
static constexpr uint16_t C_GOLD = colour565(255, 198, 68);
static constexpr uint16_t C_RED = colour565(229, 66, 72);
static constexpr uint16_t C_GREEN = colour565(62, 202, 133);

static int16_t textWidth(const char *text, uint8_t size) {
  return (int16_t)(strlen(text) * 6 * size);
}

static void textLeft(GFXcanvas16 &frame, const char *text, int16_t x, int16_t y,
                     uint8_t size, uint16_t colour) {
  frame.setTextWrap(false);
  frame.setTextSize(size);
  frame.setTextColor(colour);
  frame.setCursor(x, y);
  frame.print(text);
}

static void centered(GFXcanvas16 &frame, const char *text, int16_t y,
                     uint8_t size, uint16_t colour) {
  textLeft(frame, text, (SCREEN_W - textWidth(text, size)) / 2, y, size,
           colour);
}

static String htmlEscape(const String &value) {
  String escaped;
  escaped.reserve(value.length() + 12);
  for (uint16_t index = 0; index < value.length(); ++index) {
    switch (value[index]) {
    case '&':
      escaped += "&amp;";
      break;
    case '<':
      escaped += "&lt;";
      break;
    case '>':
      escaped += "&gt;";
      break;
    case '"':
      escaped += "&quot;";
      break;
    case '\'':
      escaped += "&#39;";
      break;
    default:
      escaped += value[index];
      break;
    }
  }
  return escaped;
}

static uint16_t readU16(const uint8_t *data) {
  return ((uint16_t)data[0] << 8) | data[1];
}

static void writeU16(uint8_t *data, uint16_t value) {
  data[0] = (uint8_t)(value >> 8);
  data[1] = (uint8_t)value;
}

static bool isZeroIp(const IPAddress &ip) {
  return ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0;
}

static bool queryHost(const uint8_t *packet, size_t length, String &host,
                      size_t &questionEnd) {
  if (length < DNS_HEADER_BYTES || readU16(packet + 4) != 1) {
    return false;
  }

  size_t cursor = DNS_HEADER_BYTES;
  host = "";
  while (cursor < length) {
    const uint8_t labelLength = packet[cursor++];
    if (labelLength == 0) {
      break;
    }
    if ((labelLength & 0xC0) != 0 || cursor + labelLength > length) {
      return false;
    }
    if (host.length() > 0) {
      host += '.';
    }
    for (uint8_t index = 0; index < labelLength; ++index) {
      char character = (char)packet[cursor + index];
      if (character >= 'A' && character <= 'Z') {
        character = (char)(character - 'A' + 'a');
      }
      host += character;
    }
    cursor += labelLength;
  }

  questionEnd = cursor + 4;
  return cursor <= length && questionEnd <= length;
}

} // namespace

void WifiPrankGame::configureRoutes() {
  if (routesConfigured_) {
    return;
  }

  webServer_.on("/generate_204", HTTP_GET, [this]() { handlePortalRequest(); });
  webServer_.on("/hotspot-detect.html", HTTP_GET,
                [this]() { handlePortalRequest(); });
  webServer_.on("/connecttest.txt", HTTP_GET,
                [this]() { handlePortalRequest(); });
  webServer_.on("/ncsi.txt", HTTP_GET, [this]() { handlePortalRequest(); });
  webServer_.on("/", HTTP_GET, [this]() { handlePortalRequest(); });
  webServer_.on("/setup", HTTP_GET, [this]() { sendSetupPage(); });
  webServer_.on("/prank", HTTP_GET, [this]() { sendPrankPage(); });
  webServer_.on("/save-upstream", HTTP_POST,
                [this]() { handleSaveUpstream(); });
  webServer_.on("/rescan-upstream", HTTP_POST,
                [this]() { handleRescanUpstream(); });
  webServer_.on("/forget-upstream", HTTP_POST,
                [this]() { handleForgetUpstream(); });
  webServer_.onNotFound([this]() { handlePortalRequest(); });
  routesConfigured_ = true;
}

void WifiPrankGame::loadSettings() {
  memset(upstreamSsid_, 0, sizeof(upstreamSsid_));
  memset(upstreamPassword_, 0, sizeof(upstreamPassword_));
  system_->storage().getBytes("up_ssid", upstreamSsid_, sizeof(upstreamSsid_));
  system_->storage().getBytes("up_pass", upstreamPassword_,
                              sizeof(upstreamPassword_));
  if (system_->storage().getBytes("prank_url", prankUrl_, sizeof(prankUrl_)) !=
          sizeof(prankUrl_) ||
      prankUrl_[0] == '\0') {
    strncpy(prankUrl_, DEFAULT_PRANK_URL, sizeof(prankUrl_) - 1);
  }
}

void WifiPrankGame::saveUpstreamSettings(const String &ssid,
                                         const String &password,
                                         const String &prankUrl) {
  ssid.toCharArray(upstreamSsid_, sizeof(upstreamSsid_));
  password.toCharArray(upstreamPassword_, sizeof(upstreamPassword_));
  prankUrl.toCharArray(prankUrl_, sizeof(prankUrl_));
  system_->storage().putBytes("up_ssid", upstreamSsid_, sizeof(upstreamSsid_));
  system_->storage().putBytes("up_pass", upstreamPassword_,
                              sizeof(upstreamPassword_));
  system_->storage().putBytes("prank_url", prankUrl_, sizeof(prankUrl_));
}

void WifiPrankGame::forgetUpstream() {
  memset(upstreamSsid_, 0, sizeof(upstreamSsid_));
  memset(upstreamPassword_, 0, sizeof(upstreamPassword_));
  system_->storage().remove("up_ssid");
  system_->storage().remove("up_pass");
  upstreamConnecting_ = false;
  WiFi.disconnect(false, false);
  disableRouting();
}

void WifiPrankGame::handlePortalRequest() {
  if (!naptEnabled_ || WiFi.status() != WL_CONNECTED) {
    sendSetupPage();
    return;
  }
  sendPrankPage();
}

void WifiPrankGame::sendPrankPage() {
  String url = htmlEscape(String(prankUrl_));
  String page;
  page.reserve(1800);
  page += F("<!doctype html><html><head><meta name='viewport' "
            "content='width=device-width,initial-scale=1'><meta "
            "http-equiv='refresh' content='0;url=");
  page += url;
  page += F("'><title>FreeWifi</"
            "title><style>body{font-family:sans-serif;max-width:34rem;margin:"
            "5rem auto;padding:0 "
            "1rem;text-align:center;background:#101827;color:#eef6f7}a{color:#"
            "30c8e7}</style></head><body><h1>FreeWifi</h1><p>Opening the "
            "prank...</p><p><a href='");
  page += url;
  page += F("'>Continue</a></p></body></html>");
  webServer_.send(200, "text/html", page);
}

void WifiPrankGame::handleSaveUpstream() {
  String ssid = webServer_.arg("manual_ssid");
  if (ssid.length() == 0) {
    ssid = webServer_.arg("ssid");
  }
  if (ssid.length() == 0) {
    sendSetupPage("Choose an upstream Wi-Fi network first.");
    return;
  }

  const String prankUrl = webServer_.arg("prank_url");
  if (!prankUrl.startsWith("http://") && !prankUrl.startsWith("https://")) {
    sendSetupPage("Prank URL must start with http:// or https://.");
    return;
  }
  saveUpstreamSettings(ssid, webServer_.arg("password"), prankUrl);
  selectedAction_ = ControlAction::START_PRANK;
  startUpstreamConnection();

  sendSetupPage("Saved. Connecting to upstream Wi-Fi...");
}

void WifiPrankGame::handleForgetUpstream() {
  forgetUpstream();
  selectedAction_ = ControlAction::START_SETUP;
  sendSetupPage("Upstream Wi-Fi forgotten.");
}

void WifiPrankGame::handleRescanUpstream() {
  scanUpstreamNetworks();
  sendSetupPage(
      scannedNetworkCount_ > 0
          ? "Network list refreshed."
          : "No Wi-Fi networks found. Try again closer to the router.");
}

void WifiPrankGame::sendSetupPage(const String &notice) {
  String page;
  page.reserve(4300);
  page += F("<!doctype html><html><head><meta name='viewport' "
            "content='width=device-width,initial-scale=1'><title>FreeWifi "
            "setup</"
            "title><style>body{font-family:sans-serif;max-width:34rem;margin:"
            "2rem auto;padding:0 "
            "1rem;background:#101827;color:#eef6f7}input,select,button{font:"
            "inherit;width:100%;padding:.7rem;margin:.35rem 0 "
            "1rem;box-sizing:border-box}button{background:#30c8e7;border:0;"
            "color:#06101c;font-weight:bold}.danger{background:#e54248;color:"
            "white}section{background:#142a43;padding:1rem;border-radius:12px;"
            "margin:1rem 0}.muted{color:#7795ab}details{margin:0 0 "
            "1rem}summary{cursor:pointer}</style></head><body><h1>FreeWifi "
            "setup</h1>");
  if (notice.length() > 0) {
    page += F("<p><strong>");
    page += htmlEscape(notice);
    page += F("</strong></p>");
  }
  page += F(
      "<p class='muted'>Choose the Wi-Fi that will provide internet access to "
      "prank clients.</p><section><h2>Upstream Wi-Fi</h2><form method='post' "
      "action='/save-upstream'><label>Available networks<select "
      "name='ssid'><option value=''>Choose a Wi-Fi network</option>");
  for (uint8_t index = 0; index < scannedNetworkCount_; ++index) {
    const String ssid(scannedSsids_[index]);
    page += F("<option value='");
    page += htmlEscape(ssid);
    page += F("'");
    if (ssid == upstreamSsid_) {
      page += F(" selected");
    }
    page += F(">");
    page += htmlEscape(ssid);
    page += F(" (");
    page += String(scannedRssi_[index]);
    page += F(" dBm)</option>");
  }
  page += F("</select></label><details><summary>Hidden or unavailable "
            "network?</summary><label>SSID<input name='manual_ssid' "
            "maxlength='32' placeholder='Enter SSID "
            "manually'></label></details><label>Password<input name='password' "
            "type='password' maxlength='63' placeholder='Leave blank for open "
            "Wi-Fi'></label><label>Prank URL<input name='prank_url' type='url' "
            "maxlength='192' value='");
  page += htmlEscape(String(prankUrl_));
  page += F("' required></label><button>Save and connect</button></form><form "
            "method='post' action='/rescan-upstream'><button>Rescan Wi-Fi "
            "networks</button></form></section><section><h2>Forget "
            "upstream</h2><p class='muted'>This removes the saved upstream "
            "SSID and password. The prank URL is kept.</p><form method='post' "
            "action='/forget-upstream'><button class='danger'>Forget upstream "
            "Wi-Fi</button></form></section><p class='muted'>Warning: this "
            "setup page is served over the open FreeWifi network. Use a "
            "test/guest upstream network.</p></body></html>");
  webServer_.send(200, "text/html", page);
}

void WifiPrankGame::startAccessPoint(bool withUpstreamRadio) {
  if (accessPointStarted_) {
    return;
  }

  WiFi.mode(withUpstreamRadio ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_IP, AP_SUBNET, AP_DHCP_START, AP_IP);
  accessPointStarted_ = WiFi.softAP(AP_SSID);
  if (!accessPointStarted_) {
    Serial.println("WIFI PRANK: access point start failed.");
    return;
  }

  accessPointIp_ = WiFi.softAPIP();
  startDnsProxy();
  webServer_.begin();

  Serial.print("WIFI PRANK: ");
  Serial.print(AP_SSID);
  Serial.print(" at ");
  Serial.println(accessPointIp_);
}

void WifiPrankGame::startSetupWifi() {
  startAccessPoint(true);
  if (accessPointStarted_) {
    scanUpstreamNetworks();
  }
}

void WifiPrankGame::scanUpstreamNetworks() {
  scannedNetworkCount_ = 0;
  const int16_t networkCount = WiFi.scanNetworks();
  if (networkCount < 0) {
    Serial.println("WIFI PRANK: upstream scan failed.");
    return;
  }

  for (int16_t index = 0;
       index < networkCount && scannedNetworkCount_ < MAX_SCANNED_NETWORKS;
       ++index) {
    const String ssid = WiFi.SSID((uint8_t)index);
    if (ssid.length() == 0 || ssid.length() >= MAX_SSID_BYTES) {
      continue;
    }

    bool duplicate = false;
    for (uint8_t saved = 0; saved < scannedNetworkCount_; ++saved) {
      if (ssid == scannedSsids_[saved]) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) {
      continue;
    }

    ssid.toCharArray(scannedSsids_[scannedNetworkCount_], MAX_SSID_BYTES);
    scannedRssi_[scannedNetworkCount_] = WiFi.RSSI((uint8_t)index);
    ++scannedNetworkCount_;
  }
  WiFi.scanDelete();

  Serial.print("WIFI PRANK: found ");
  Serial.print(scannedNetworkCount_);
  Serial.println(" upstream networks.");
}

void WifiPrankGame::startPrankWifi() {
  if (!hasUpstreamSettings()) {
    return;
  }

  startAccessPoint(true);
  startUpstreamConnection();
}

void WifiPrankGame::stopPrankWifi() {
  disableRouting();
  stopDnsProxy();
  if (accessPointStarted_) {
    webServer_.stop();
    WiFi.softAPdisconnect(true);
  }
  WiFi.disconnect(false, false);
  WiFi.mode(WIFI_OFF);
  accessPointStarted_ = false;
  upstreamConnecting_ = false;
}

void WifiPrankGame::startUpstreamConnection() {
  if (!accessPointStarted_ || !hasUpstreamSettings()) {
    return;
  }

  WiFi.mode(WIFI_AP_STA);
  disableRouting();
  dnsForwarding_ = false;
  WiFi.disconnect(false, false);
  const char *password =
      upstreamPassword_[0] == '\0' ? nullptr : upstreamPassword_;
  WiFi.begin(upstreamSsid_, password);
  upstreamConnecting_ = true;
  lastUpstreamAttemptAt_ = millis();
  Serial.print("WIFI PRANK: connecting upstream ");
  Serial.println(upstreamSsid_);
}

void WifiPrankGame::updateUpstream(uint32_t now) {
  if (!accessPointStarted_ || !hasUpstreamSettings()) {
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    upstreamConnecting_ = false;
    if (!naptEnabled_) {
      enableRouting();
    }
    return;
  }

  if (naptEnabled_) {
    disableRouting();
  }

  if (upstreamConnecting_ &&
      now - lastUpstreamAttemptAt_ >= UPSTREAM_CONNECT_TIMEOUT_MS) {
    upstreamConnecting_ = false;
  }
  if (!upstreamConnecting_ &&
      now - lastUpstreamAttemptAt_ >= UPSTREAM_RETRY_INTERVAL_MS) {
    startUpstreamConnection();
  }
}

void WifiPrankGame::enableRouting() {
  upstreamDns_ = WiFi.dnsIP(0);
  if (isZeroIp(upstreamDns_)) {
    upstreamDns_ = FALLBACK_DNS;
  }
  naptEnabled_ = WiFi.AP.enableNAPT(true);
  dnsForwarding_ = naptEnabled_;
  if (naptEnabled_) {
    Serial.print("WIFI PRANK: routed through ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WIFI PRANK: NAPT enable failed.");
  }
}

void WifiPrankGame::disableRouting() {
  if (naptEnabled_) {
    WiFi.AP.enableNAPT(false);
  }
  naptEnabled_ = false;
  dnsForwarding_ = false;
}

void WifiPrankGame::startDnsProxy() {
  if (dnsStarted_) {
    return;
  }
  memset(pendingDns_, 0, sizeof(pendingDns_));
  dnsProxy_.onPacket(
      [this](AsyncUDPPacket &packet) { handleDnsPacket(packet); });
  dnsStarted_ = dnsProxy_.listen(53);
  if (!dnsStarted_) {
    Serial.println("WIFI PRANK: DNS proxy start failed.");
  }
}

void WifiPrankGame::stopDnsProxy() {
  if (!dnsStarted_) {
    return;
  }
  dnsProxy_.close();
  dnsStarted_ = false;
  memset(pendingDns_, 0, sizeof(pendingDns_));
}

void WifiPrankGame::handleDnsPacket(AsyncUDPPacket &packet) {
  if (packet.length() < DNS_HEADER_BYTES ||
      packet.length() > DNS_PACKET_BYTES) {
    return;
  }

  const bool fromUpstream = dnsForwarding_ &&
                            packet.remoteIP() == upstreamDns_ &&
                            packet.remotePort() == 53;
  if (fromUpstream) {
    sendForwardedDnsResponse(packet);
    return;
  }

  String host;
  size_t questionEnd = 0;
  if (!queryHost(packet.data(), packet.length(), host, questionEnd)) {
    return;
  }

  if (!dnsForwarding_ || host == CHECK_HOST) {
    sendLocalDnsResponse(packet);
    return;
  }
  forwardDnsQuery(packet);
}

void WifiPrankGame::sendLocalDnsResponse(AsyncUDPPacket &packet) {
  uint8_t response[DNS_PACKET_BYTES];
  const size_t questionLength = packet.length();
  memcpy(response, packet.data(), questionLength);

  uint16_t flags = readU16(response + 2);
  flags |= 0x8000;
  flags &= (uint16_t)~0x0003;
  writeU16(response + 2, flags);
  writeU16(response + 6, 0);

  String host;
  size_t questionEnd = 0;
  if (!queryHost(response, questionLength, host, questionEnd)) {
    return;
  }

  const uint16_t queryType = readU16(response + questionEnd - 4);
  size_t responseLength = questionEnd;
  if (queryType == 1 || queryType == 255) {
    writeU16(response + 6, 1);
    response[responseLength++] = 0xC0;
    response[responseLength++] = 0x0C;
    writeU16(response + responseLength, 1);
    responseLength += 2;
    writeU16(response + responseLength, 1);
    responseLength += 2;
    response[responseLength++] = 0;
    response[responseLength++] = 0;
    response[responseLength++] = 0;
    response[responseLength++] = 30;
    writeU16(response + responseLength, 4);
    responseLength += 2;
    response[responseLength++] = AP_IP[0];
    response[responseLength++] = AP_IP[1];
    response[responseLength++] = AP_IP[2];
    response[responseLength++] = AP_IP[3];
  }

  AsyncUDPMessage message(responseLength);
  message.write(response, responseLength);
  packet.send(message);
}

void WifiPrankGame::forwardDnsQuery(AsyncUDPPacket &packet) {
  int8_t pendingIndex = -1;
  for (uint8_t index = 0; index < DNS_PENDING_COUNT; ++index) {
    if (!pendingDns_[index].used ||
        pendingDns_[index].id == readU16(packet.data())) {
      pendingIndex = (int8_t)index;
      break;
    }
  }
  if (pendingIndex < 0) {
    return;
  }

  PendingDnsQuery &pending = pendingDns_[pendingIndex];
  pending.used = true;
  pending.id = readU16(packet.data());
  pending.clientIp = packet.remoteIP();
  pending.clientPort = packet.remotePort();

  AsyncUDPMessage message(packet.length());
  message.write(packet.data(), packet.length());
  dnsProxy_.sendTo(message, upstreamDns_, 53, TCPIP_ADAPTER_IF_STA);
}

void WifiPrankGame::sendForwardedDnsResponse(AsyncUDPPacket &packet) {
  const uint16_t id = readU16(packet.data());
  for (uint8_t index = 0; index < DNS_PENDING_COUNT; ++index) {
    PendingDnsQuery &pending = pendingDns_[index];
    if (!pending.used || pending.id != id) {
      continue;
    }

    AsyncUDPMessage message(packet.length());
    message.write(packet.data(), packet.length());
    dnsProxy_.sendTo(message, pending.clientIp, pending.clientPort,
                     TCPIP_ADAPTER_IF_AP);
    pending.used = false;
    return;
  }
}

void WifiPrankGame::render(pocketgame::PocketGameSystem &system, uint32_t now) {
  lastRenderAt_ = now;
  GFXcanvas16 &frame = system.display().canvas();
  frame.fillScreen(C_BLACK);
  frame.fillRect(0, 0, SCREEN_W, 58, C_NAVY);
  frame.drawFastHLine(0, 57, SCREEN_W, C_CYAN);
  centered(frame, "FREE WIFI", 15, 2, C_TEXT);
  centered(frame, accessPointStarted_ ? "SERVICE ON" : "SERVICE OFF", 41, 1,
           accessPointStarted_ ? C_GREEN : C_MUTED);

  frame.fillRoundRect(11, 76, 150, 165, 12, C_PANEL);
  frame.drawRoundRect(11, 76, 150, 165, 12, naptEnabled_ ? C_GREEN : C_BORDER);
  centered(frame, accessPointStarted_ ? "ON" : "OFF", 98, 2,
           accessPointStarted_ ? C_GREEN : C_MUTED);
  centered(frame, "SSID: FreeWifi", 132, 1, C_TEXT);
  centered(frame,
           naptEnabled_
               ? "UPSTREAM READY"
               : (upstreamConnecting_ ? "CONNECTING..." : "SETUP PORTAL READY"),
           153, 1, naptEnabled_ ? C_GREEN : C_GOLD);
  centered(frame, hasUpstreamSettings() ? "PRANK PAGE" : "SETUP PAGE", 180, 2,
           C_CYAN);
  frame.fillRoundRect(20, 205, 132, 25, 6, C_PANEL_2);
  const char *action = "START SETUP";
  switch (selectedAction_) {
  case ControlAction::START_SETUP:
    action = accessPointStarted_ ? "SETUP ACTIVE" : "START SETUP";
    break;
  case ControlAction::START_PRANK:
    action = naptEnabled_ ? "STOP PRANK" : "START PRANK";
    break;
  case ControlAction::FORGET:
    action = "FORGET UPSTREAM";
    break;
  case ControlAction::EXIT:
    action = "EXIT APP";
    break;
  }
  centered(frame, action, 212, 1, C_TEXT);
  centered(frame, "CLICK NEXT  HOLD SELECT", 265, 1, C_MUTED);
  system.display().present();
}

void WifiPrankGame::selectNextAction() {
  if (!hasUpstreamSettings()) {
    selectedAction_ = selectedAction_ == ControlAction::START_SETUP
                          ? ControlAction::EXIT
                          : ControlAction::START_SETUP;
    return;
  }

  switch (selectedAction_) {
  case ControlAction::START_PRANK:
    selectedAction_ = ControlAction::FORGET;
    break;
  case ControlAction::FORGET:
    selectedAction_ = ControlAction::EXIT;
    break;
  default:
    selectedAction_ = ControlAction::START_PRANK;
    break;
  }
}

void WifiPrankGame::performSelectedAction() {
  switch (selectedAction_) {
  case ControlAction::START_SETUP:
    startSetupWifi();
    break;
  case ControlAction::START_PRANK:
    if (naptEnabled_) {
      stopPrankWifi();
    } else {
      startPrankWifi();
    }
    break;
  case ControlAction::FORGET:
    forgetUpstream();
    selectedAction_ = ControlAction::START_SETUP;
    break;
  case ControlAction::EXIT:
    stopPrankWifi();
    system_->requestLauncher();
    break;
  }
}

void WifiPrankGame::begin(pocketgame::PocketGameSystem &system) {
  system_ = &system;
  stopPrankWifi();
  loadSettings();
  selectedAction_ = hasUpstreamSettings() ? ControlAction::START_PRANK
                                          : ControlAction::START_SETUP;
  configureRoutes();
  render(system, millis());
  Serial.println("WIFI PRANK controller ready. Wi-Fi is OFF.");
}

void WifiPrankGame::loop(pocketgame::PocketGameSystem &system, uint32_t now) {
  updateUpstream(now);
  if (accessPointStarted_) {
    webServer_.handleClient();
  }

  const pocketgame::ControlEvents &events = system.controls().events();
  if (events.cycle) {
    selectNextAction();
    render(system, now);
  }
  if (events.select) {
    performSelectedAction();
    if (system.activeGame() != this) {
      return;
    }
    render(system, now);
  }

  if (now - lastRenderAt_ >= 500) {
    render(system, now);
  }

  delay(1);
}
