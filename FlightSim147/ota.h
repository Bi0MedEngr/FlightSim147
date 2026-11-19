// ota.h — Wi-Fi firmware updater (own access point, upload .bin from any phone browser)
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>

namespace ota {

static const char *AP_SSID = "FlightSim147";
static WebServer server(80);
static String lastError;

static const char PAGE[] PROGMEM = R"HTML(<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1"><title>FlightSim147 update</title>
<style>body{font-family:system-ui,sans-serif;background:#0d1b2a;color:#e0e6ee;padding:24px;max-width:520px;margin:auto}
button,input{font-size:1.05rem}button{padding:12px 24px;border:0;border-radius:8px;background:#4caf50;color:#fff}
progress{width:100%;height:22px;margin:16px 0}.e{color:#ff8a80}</style></head><body>
<h2>FlightSim147 updater</h2>
<p>Choose the <b>-ota.bin</b> file for this board, then tap Upload.</p>
<input type="file" id="f" accept=".bin"><br><br><button onclick="go()">Upload</button>
<progress id="p" value="0" max="100"></progress><div id="s"></div>
<script>
function go(){var f=document.getElementById('f').files[0],s=document.getElementById('s');
if(!f){s.textContent='Pick a file first.';return}
if(f.size>2000000){s.innerHTML='<span class=e>That file is too big. Use the <b>-ota.bin</b> file, not the full flasher image.</span>';return}
var x=new XMLHttpRequest(),d=new FormData();d.append('fw',f,f.name);
x.upload.onprogress=function(e){if(e.lengthComputable)document.getElementById('p').value=e.loaded*100/e.total};
x.onload=function(){s.innerHTML=x.status==200?'Done! Rebooting...':'<span class=e>'+x.responseText+'</span>'};
x.onerror=function(){s.textContent='Connection lost. If the board rebooted, the update probably worked.'};
x.open('POST','/update');x.send(d);s.textContent='Uploading...'}
</script></body></html>)HTML";

static void begin() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", PAGE); });
  server.on("/update", HTTP_POST,
    [] {
      bool ok = !Update.hasError() && lastError.length() == 0;
      server.sendHeader("Connection", "close");
      server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : ("Update failed: " + lastError));
      if (ok) { delay(500); ESP.restart(); }
    },
    [] {
      HTTPUpload &up = server.upload();
      if (up.status == UPLOAD_FILE_START) {
        lastError = "";
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) lastError = Update.errorString();
      } else if (up.status == UPLOAD_FILE_WRITE) {
        if (lastError.length() == 0 && Update.write(up.buf, up.currentSize) != up.currentSize)
          lastError = Update.errorString();
      } else if (up.status == UPLOAD_FILE_END) {
        if (lastError.length() == 0 && !Update.end(true)) lastError = Update.errorString();
      } else if (up.status == UPLOAD_FILE_ABORTED) {
        Update.abort(); lastError = "upload aborted";
      }
    });
  server.begin();
}

static void loop() { server.handleClient(); }

static size_t progress() { return Update.progress(); }

} // namespace ota
