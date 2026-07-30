#include "ConfigStore.h"

#include <Preferences.h>

const char* ConfigStore::NS_NAMESPACE = "wifi_cfg";
const char* ConfigStore::KEY_SSID     = "ssid";
const char* ConfigStore::KEY_PASS     = "pass";

// 全域單例實體
ConfigStore Config;

bool ConfigStore::hasCredentials() {
  Preferences prefs;
  prefs.begin(NS_NAMESPACE, true);  // 唯讀開啟
  bool has = prefs.isKey(KEY_SSID) && (prefs.getString(KEY_SSID, "").length() > 0);
  prefs.end();
  return has;
}

String ConfigStore::getSSID() {
  Preferences prefs;
  prefs.begin(NS_NAMESPACE, true);
  String ssid = prefs.getString(KEY_SSID, "");
  prefs.end();
  return ssid;
}

String ConfigStore::getPassword() {
  Preferences prefs;
  prefs.begin(NS_NAMESPACE, true);
  String pass = prefs.getString(KEY_PASS, "");
  prefs.end();
  return pass;
}

void ConfigStore::saveCredentials(const String& ssid, const String& password) {
  Preferences prefs;
  prefs.begin(NS_NAMESPACE, false);  // 讀寫開啟
  prefs.putString(KEY_SSID, ssid);
  prefs.putString(KEY_PASS, password);
  prefs.end();
}

void ConfigStore::clearCredentials() {
  Preferences prefs;
  prefs.begin(NS_NAMESPACE, false);
  prefs.clear();  // 清除整個命名空間
  prefs.end();
}
