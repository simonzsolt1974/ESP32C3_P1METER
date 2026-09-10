// ************************************************************************************
// *                        START wifi
// ************************************************************************************
void start_wifi() {
  WiFi.mode(WIFI_STA);
  Serial.println("starting wifi ");
  delay(1000);
  // set the hostname BEFORE WiFi.begin(), otherwise the first (stored
  // credential) connection goes out with the default "espressif" name
  WiFi.setHostname(getChipId(false).c_str());

  /*
   * Intermittent OFFLINE fix.
   *
   * 1. WiFi.setSleep(false): on the ESP32-C3 the default modem-sleep lets
   *    the radio doze between beacons; when the AP changes channel or drops
   *    the beacon the STA link can be lost silently and the driver does not
   *    always recover. Disabling modem sleep removes this trigger. Power
   *    use is irrelevant here, the device is powered by the P1 port.
   *
   * 2. WiFi.setAutoReconnect(true): let the driver re-associate after short
   *    drops. The loop() watchdog in the main sketch handles the cases the
   *    driver cannot recover by itself.
   */
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);

  // we do 10 attemps to connect with the last used credentials
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print("*");
    WiFi.begin();
    event += 1;
    if (event == 10)  break;
// als het verbinden is mislukt gaan we naar het configportal  
  }
  if (event>9) {
     event=0;
     Serial.println("\nWARNING connection failed");
     digitalWrite(led_onb, LED_AAN); // the onboard led on
     start_portal(); // stel het portal in
     }
  Serial.println("\n# connection attempts = " + String(event));  //Serial.println(event);
  event=0; // we kunnen door naar de rest
  start_asyserver();
  ledblink(3, 500);

}
//// *************************************************************************
////                      SERVER STARTEN
// ********************************************************************
//             check if there must come a static ip
// ********************************************************************
//void checkFixed() {
//  // we come here only when wifi connected
//  char GATE_WAY[16] = "";
//  IPAddress gat = WiFi.gatewayIP();
//  sprintf(GATE_WAY, "%d.%d.%d.%d", gat[0], gat[1], gat[2], gat[3]);
//  //DebugPrint("GATE_WAY in checkFixed = nu: "); //DebugPrintln(String(GATE_WAY));
//  //DebugPrint("static_ip in checkFixed = nu: "); //DebugPrintln(String(static_ip));
//
//  if (static_ip[0] != '\0' && static_ip[0] != '0') {
//    //DebugPrintln("we need s static ip  Custom STA IP/GW/Subnet");
//    IPAddress _ip, _gw, _sn(255, 255, 255, 0); // declare
//    _ip.fromString(static_ip);
//    _gw.fromString(GATE_WAY);//  if (ssid != "") {
//    WiFi.config(_ip, _gw, _sn);
//    //DebugPrintln(WiFi.localIP());
//  } // else {
//  //DebugPrintln("trying to get rid of wificonfig");
//  //  WiFi.config(0u, 0u, 0u);
//  //}
//}



void loginBoth(AsyncWebServerRequest *request, String who) {
  //String authFailResponse = "<h2>login failed <a href='/'>click here</a></h2>";
  if (who == "admin" ){
  const char* www_realm = "login as administrator."; 
    if (!request->authenticate("admin", pswd)) return request->requestAuthentication();
  }
  if (who == "both" ){
  const char* www_realm = "login as administrator or user."; 
    if (!request->authenticate("admin", pswd) && !request->authenticate("user", userPwd)) return request->requestAuthentication();
  }
}
