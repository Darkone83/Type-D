#include "diag.h"
#include <FFat.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include "disp_cfg.h"

extern "C" {
#include "esp_psram.h"
}

static const char* resourceFiles[] = {
    "amb.jpg",
    "app.jpg",
    "cpu.jpg",
    "DC.jpg",
    "fan.jpg",
    "TD.jpg",
    "TR.jpg",
    "XBS.jpg"
};

static const char* resourceNames[] = {
    "Ambient Temp icon",
    "App Icon",
    "CPU Icon",
    "Darkone Customs Logo",
    "Fan Icon",
    "Type D Logo",
    "Team Resurgent Logo",
    "XBOX-Scene Logo"
};

// --- Format FFat (Erase) ---
static void handleFormatFS(AsyncWebServerRequest *request) {
    FFat.end(); // Unmount
    bool ok = FFat.format();
    bool remount = FFat.begin();
    String msg = ok && remount ? 
        "<b>File system formatted and remounted!</b>" : 
        "<b>Format or remount failed. Please reboot device.</b>";
    request->send(200, "text/html", msg + "<br><a href='/diag'>Back to Diagnostics</a>");
}

// --- Main Diagnostics Page Handler ---
static void handleDiag(AsyncWebServerRequest *request) {
    // Formatting requested?
    if (request->hasParam("format")) {
        handleFormatFS(request);
        return;
    }

    // --- Start centered container ---
    String html = R"(
    <!DOCTYPE html>
    <html>
    <head>
    <title>Type D Diagnostics</title>
    <meta name="viewport" content="width=480">
    <style>
html, body {
    height: 100%;
    margin: 0;
    padding: 0;
}
body {
    min-height: 100vh;
    display: flex;
    flex-direction: column;
    justify-content: center;
    align-items: center;
    background:#141414;
    color:#EEE;
    font-family:sans-serif;
}
h1, h2 {color:#4eec27;}
.centered {
    width: 100%;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
}
.section {
    background:#232323;
    padding:16px 18px;
    margin:22px auto;
    border-radius:14px;
    display:inline-block;
}
.checklist {margin:0 0 18px 0; text-align:left; display:inline-block;}
.checkitem {margin:2px 0; padding:2px 0;}
.pass {color:#49e24e; font-weight:bold;}
.fail {color:#ed3c3c; font-weight:bold;}
.qbtn {margin:6px 9px 6px 0; padding:10px 20px; background:#444; border:none; color:#fff; border-radius:8px; font-size:1.1em; cursor:pointer; display:inline-block;}
.qbtn:hover {background:#299a2c;}
.footer {margin:36px 0 12px 0; color:#888; font-size:.95em;}
label {font-weight:600;}
input[type=number] {width:60px; margin:0 4px 0 8px; padding:2px 4px;}
</style>

    </head>
    <body>
    <div class='centered'>
    <h1>Type D Diagnostics</h1>
    <div class='section'>
        <h2>System Info</h2>
    )";

    // --- SYSTEM INFO ---
    html += "<div style='text-align:left;display:inline-block;margin:auto;'>";
    html += "<b>Chip:</b> ESP32, Rev " + String(ESP.getChipRevision()) +
            ", Cores: " + String(ESP.getChipCores()) + "<br>";
    html += "<b>Flash size:</b> " + String(ESP.getFlashChipSize() / (1024*1024)) + "MB<br>";
    size_t psram_size = esp_psram_get_size();
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    html += "<b>PSRAM:</b> " + String(psram_size / (1024*1024)) + "MB "
            "(Free: " + String(psram_free / 1024) + " KB)<br>";
    html += "<b>Heap:</b> " + String(ESP.getHeapSize()) + " bytes"
         + " (Free: " + String(ESP.getFreeHeap()) + " bytes)<br>";
    html += "<b>Sketch:</b> " + String(ESP.getSketchSize()) + " bytes"
         + " (Free: " + String(ESP.getFreeSketchSpace()) + " bytes)<br>";
    // FATFS info
    size_t fat_total = FFat.totalBytes();
    size_t fat_used  = FFat.usedBytes();
    size_t fat_free  = fat_total > fat_used ? fat_total - fat_used : 0;
    html += "<b>FFat Used:</b> " + String(fat_used/1024) + " KB / " + String(fat_total/1024) + " KB"
         + " &mdash; Free: " + String(fat_free/1024) + " KB<br>";
    // WiFi info
    String ssid = WiFi.isConnected() ? WiFi.SSID() : "(not connected)";
    String ip = WiFi.isConnected() ? WiFi.localIP().toString() : "(none)";
    html += "<b>WiFi SSID:</b> " + ssid + "<br>";
    html += "<b>IP Address:</b> " + ip + "<br>";
    html += "</div></div>";

    // --- RESOURCE CHECK ---
    html += "<div class='section'><h2>Resource Check</h2>";
    bool anyMissing = false;
    html += "<div class='checklist'>";
    for (int i = 0; i < 8; ++i) {
        String fname = String("/resource/") + resourceFiles[i];
        bool present = false;
        File dir = FFat.open("/resource");
        if (dir && dir.isDirectory()) {
          File f = dir.openNextFile();
          while (f) {
            String fn = String(f.name());
            fn.toLowerCase();
            String cmp = resourceFiles[i];
            cmp.toLowerCase();
            if (fn == cmp) {
              present = true;
              break;
            }
          f = dir.openNextFile();
        }
    }
        html += "<div class='checkitem'>";
        if (present)
            html += "<span class='pass'>&#10004;</span> ";
        else {
            html += "<span class='fail'>&#10008;</span> ";
            anyMissing = true;
        }
        html += String(resourceFiles[i]) + " : <span style='color:#aaa'>" + resourceNames[i] + "</span></div>";
    }
    html += "</div>";

    if (anyMissing) {
        html += "<div style='color:#ed3c3c; font-weight:bold; margin:10px 0 12px 0;'>"
                "One or more resource files are missing!<br>"
                "Please upload missing files via the Resource Manager."
                "</div>";
        html += "<a class='qbtn' style='margin-top:8px;display:inline-block;' href='/resource'>Go to Resource Manager</a>";
    } else {
        html += "<div style='color:#49e24e; font-weight:bold; margin:8px 0 4px 0;'>All required resource files found.</div>";
    }
    html += "</div>";

    // --- QUICK ACCESS COMMANDS ---
    html += "<div class='section'><h2>Function Quick Access</h2>";
    html += "<form style='margin-bottom:9px;display:inline-block;' onsubmit='setBright(event)'>";
    html += "<label>Set Brightness:</label>";
    html += "<input id='brightval' type='number' min='5' max='100' value='80'>%";
    html += "<button class='qbtn' type='submit'>Set</button>";
    html += "</form><br>";

    struct {
        const char* label;
        const char* url;
    } cmds[] = {
        {"Next Image",       "/cmd?c=01"},
        {"Previous Image",   "/cmd?c=02"},
        {"Random Image",     "/cmd?c=03"},
        {"JPG Mode",         "/cmd?c=04&mode=jpg"},
        {"GIF Mode",         "/cmd?c=04&mode=gif"},
        {"Random Mode",      "/cmd?c=04"},
        {"Clear Display",    "/cmd?c=06"},
        {"WiFi Restart",     "/cmd?c=30"},
        {"WiFi Forget",      "/cmd?c=31"},
        {"Reboot",           "/cmd?c=40"},
        {"Display ON",       "/cmd?c=60"},
        {"Display OFF",      "/cmd?c=61"},
    };

    for (auto& cmd : cmds) {
        html += "<button class='qbtn' onclick=\"location.href='" + String(cmd.url) + "';return false;\">" + String(cmd.label) + "</button>";
    }
    // Format FFat
    html += "<button class='qbtn' style='background:#a22;margin-top:12px;' onclick=\"if(confirm('Erase all files?'))location.href='/diag?format=1';return false;\">Format File System</button>";

    html += "<br></div>";

    // --- FOOTER ---
    html += "<div class='footer'>2025 Darkone83 / Darkone Customs / Team Resurgent</div>";

    // --- JS for brightness ---
    html += R"(
    <script>
    function setBright(e){
        e.preventDefault();
        let v = document.getElementById('brightval').value;
        v = Math.max(5, Math.min(100, parseInt(v)));
        location.href = '/cmd?c=20&val=' + v;
    }
    </script>
    )";
    html += "</div></body></html>";

    request->send(200, "text/html", html);
}

namespace Diag {
void begin(AsyncWebServer &server) {
    server.on("/diag", HTTP_GET, handleDiag);
}
void handle() {
    // No periodic tasks needed yet.
}
}
