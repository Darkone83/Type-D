#include <FS.h>
#include <ESPAsyncWebServer.h>
#include <FFat.h>
#include "fileman.h"
#include "imagedisplay.h"

// --- Internal state ---
static AsyncWebServer* _server = nullptr;

// --- HTML page strings ---
static const char* _pageHeader =
    "<!DOCTYPE html><html><head>"
    "<title>File Manager</title>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=480'>"
"<style>"
"html, body {"
"    height: 100%;"
"    margin: 0;"
"    padding: 0;"
"}"
"body {"
"    min-height: 100vh;"
"    display: flex;"
"    flex-direction: column;"
"    justify-content: center;"
"    align-items: center;"
"    background:#141414;"
"    color:#EEE;"
"    font-family:sans-serif;"
"}"
"h1, h2 {color:#4eec27;}"
".centered {"
"    width: 100%;"
"    display: flex;"
"    flex-direction: column;"
"    align-items: center;"
"    justify-content: center;"
"}"
".section {"
"    background:#232323;"
"    padding:16px 18px;"
"    margin:22px auto;"
"    border-radius:14px;"
"    display:inline-block;"
"}"
".file-list {margin:10px 0; display:inline-block; text-align:left;}"
".qbtn {margin:6px 9px 6px 0; padding:10px 20px; background:#444; border:none; color:#fff; border-radius:8px; font-size:1.1em; cursor:pointer; display:inline-block;}"
".qbtn:hover {background:#299a2c;}"
"label {font-weight:600;}"
"input[type=file],button {margin:.7em 0; padding:.5em 1.2em; font-size:1.1em; border-radius:5px; border:1px solid #555;}"
"</style>"
"</head><body><div class='centered'>";

static const char* _pageFooter =
    "<div style='font-style:italic;color:#444;' id='lostmsg'></div>"
    "<script>"
    "const lost=["
    "\"Congratulations, you've reached the center of nowhere!\","
    "\"If you’re reading this, you may be in need of an adult.\","
    "\"Lost? Don’t worry—maps are overrated anyway.\","
    "\"Welcome to the end of the internet. Please turn around.\","
    "\"If you found this page, you’re probably beyond help!\""
    "];"
    "document.getElementById('lostmsg').innerText=lost[Math.floor(Math.random()*lost.length)];"
    "</script></div></body></html>";

// --- Forward declarations ---
String buildFileManagerPage();
String listBootImageSection();
String listGallerySection();
String buildResourceManagerPage();
void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
void handleDelete(AsyncWebServerRequest *request);
void serveFile(AsyncWebServerRequest *request);
void handleDisplayRandom(AsyncWebServerRequest *request);
void handleDisplayRandomJpg(AsyncWebServerRequest *request);
void handleDisplayRandomGif(AsyncWebServerRequest *request);
void handleSelectImage(AsyncWebServerRequest *request);
String getRandomGalleryImagePath();
String getRandomJpgImagePath();
String getRandomGifImagePath();

// --- Upload state ---
File uploadFile;
String uploadTargetPath;

// --- Setup routes and handlers ---
void FileMan::begin(AsyncWebServer& server) {
    _server = &server;

    // Main UI
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", buildFileManagerPage());
    });

    // Resource Manager page [ADD]
    server.on("/resource", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", buildResourceManagerPage());
    });

    // Serve FFat files
    server.on("/sd/boot", HTTP_GET, serveFile);
    server.on("/sd/jpg", HTTP_GET, serveFile);
    server.on("/sd/gif", HTTP_GET, serveFile);
    server.on("/sd/resource", HTTP_GET, serveFile);

    // Upload handlers
    server.on("/upload_boot", HTTP_POST, 
        [](AsyncWebServerRequest *request){},
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            handleUpload(request, filename, index, data, len, final);
            if(final)
                request->send(200, "text/html", "<b>Upload complete.</b><br>Redirecting...<script>setTimeout(()=>{location.href='/'} ,500);</script>");
        }
    );
    server.on("/upload_jpg", HTTP_POST, 
        [](AsyncWebServerRequest *request){},
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            handleUpload(request, filename, index, data, len, final);
            if(final)
                request->send(200, "text/html", "<b>Upload complete.</b><br>Redirecting...<script>setTimeout(()=>{location.href='/'} ,500);</script>");
        }
    );
    server.on("/upload_gif", HTTP_POST, 
        [](AsyncWebServerRequest *request){},
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            handleUpload(request, filename, index, data, len, final);
            if(final)
                request->send(200, "text/html", "<b>Upload complete.</b><br>Redirecting...<script>setTimeout(()=>{location.href='/'} ,500);</script>");
        }
    );
    server.on("/upload_resource", HTTP_POST, 
        [](AsyncWebServerRequest *request){},
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            handleUpload(request, filename, index, data, len, final);
            if(final)
                request->send(200, "text/html", "<b>Upload complete.</b><br>Redirecting...<script>setTimeout(()=>{location.href='/resource'} ,500);</script>");
        }
    );

    // Delete handlers
    server.on("/delete_boot", HTTP_POST, handleDelete);
    server.on("/delete_gallery", HTTP_POST, handleDelete);
    server.on("/delete_resource", HTTP_POST, handleDelete);

    // Display random image(s)
    server.on("/display_random", HTTP_POST, handleDisplayRandom);
    server.on("/display_random_jpg", HTTP_POST, handleDisplayRandomJpg);
    server.on("/display_random_gif", HTTP_POST, handleDisplayRandomGif);

    // Select image (from gallery)
    server.on("/select_image", HTTP_POST, handleSelectImage);
}

// --- HTML page builder ---
String buildFileManagerPage() {
    String html = _pageHeader;

    html += "<div class='section'>";
    html += "<div style='width:100%;text-align:center;margin-bottom:1em'>"
            "<img src=\"/resource/TD.jpg\" alt=\"Type D\" style=\"width:128px;height:auto;display:block;margin:0 auto;\">"
            "</div>";
    html += "<h1>File Manager</h1>";

    // --- Insert FFat Space Usage ---
    size_t total = FFat.totalBytes();
    size_t used  = FFat.usedBytes();
    size_t free  = total > used ? total - used : 0;
    html += "<div style='font-size:1.1em; margin:12px 0;'>";
    html += "Space Used: " + String(used / 1024) + " KB / " + String(total / 1024) + " KB";
    html += " &mdash; Free: " + String(free / 1024) + " KB";
    html += "</div>";
    html += "</div>";

    html += listBootImageSection();
    html += listGallerySection();
    html += _pageFooter;
    return html;
}

String listBootImageSection() {
    String html = "<div class='section'><h2>Change Boot Image or Animation</h2>";
    File root = FFat.open("/boot");
    bool hasBootImg = false;
    if (root) {
        File f = root.openNextFile();
        while (f) {
            String fn = f.name();
            if (fn.endsWith("boot.jpg") || fn.endsWith("boot.gif")) {
                html += "<div>" + fn;
                html += "<form method='POST' action='/delete_boot' style='display:inline;'><input type='hidden' name='file' value='" + fn + "'>";
                html += "<button class='qbtn' type='submit'>Delete</button></form></div>";
                hasBootImg = true;
            }
            f = root.openNextFile();
        }
        root.close();
    }
    if (!hasBootImg)
        html += "<div>No boot image present.</div>";
    html += "<form method='POST' enctype='multipart/form-data' action='/upload_boot'>";
    html += "<input type='file' name='upload' accept='.jpg,.gif' required><button class='qbtn' type='submit'>Upload</button>";
    html += "</form></div>";
    return html;
}

String listGallerySection() {
    String html = "<div class='section'><h2>Manage Images</h2>";

    // JPGs
    html += "<div class='file-list'><strong>JPGs:</strong><br>";
    File jpg = FFat.open("/jpg");
    bool hasJpg = false;
    if (jpg) {
        File f = jpg.openNextFile();
        while (f) {
            String fn = f.name();
            if (fn.endsWith(".jpg")) {
                html += fn + " ";
                html += "<form style='display:inline;' method='POST' action='/delete_gallery'>";
                html += "<input type='hidden' name='file' value='" + fn + "'>";
                html += "<input type='hidden' name='folder' value='/jpg'>";
                html += "<button class='qbtn' type='submit'>Delete</button></form>";
                html += "<form style='display:inline;' method='POST' action='/select_image'>";
                html += "<input type='hidden' name='file' value='" + fn + "'>";
                html += "<input type='hidden' name='folder' value='/jpg'>";
                html += "<button class='qbtn' type='submit'>Select</button></form><br>";
                hasJpg = true;
            }
            f = jpg.openNextFile();
        }
        jpg.close();
    }
    if (!hasJpg) html += "No jpg files found.";
    html += "<form method='POST' enctype='multipart/form-data' action='/upload_jpg'>";
    html += "<input type='file' name='upload' accept='.jpg' multiple required><button class='qbtn' type='submit'>Upload</button></form></div>";

    // GIFs
    html += "<div class='file-list'><strong>GIFs:</strong><br>";
    File gif = FFat.open("/gif");
    bool hasGif = false;
    if (gif) {
        File f = gif.openNextFile();
        while (f) {
            String fn = f.name();
            if (fn.endsWith(".gif")) {
                html += fn + " ";
                html += "<form style='display:inline;' method='POST' action='/delete_gallery'>";
                html += "<input type='hidden' name='file' value='" + fn + "'>";
                html += "<input type='hidden' name='folder' value='/gif'>";
                html += "<button class='qbtn' type='submit'>Delete</button></form>";
                html += "<form style='display:inline;' method='POST' action='/select_image'>";
                html += "<input type='hidden' name='file' value='" + fn + "'>";
                html += "<input type='hidden' name='folder' value='/gif'>";
                html += "<button class='qbtn' type='submit'>Select</button></form><br>";
                hasGif = true;
            }
            f = gif.openNextFile();
        }
        gif.close();
    }
    if (!hasGif) html += "No gif files found.";
    html += "<form method='POST' enctype='multipart/form-data' action='/upload_gif'>";
    html += "<input type='file' name='upload' accept='.gif' multiple required><button class='qbtn' type='submit'>Upload</button></form></div>";

    html += "<div style='margin:10px 0;'>";
    html += "<form method='POST' action='/display_random_jpg' style='display:inline;'><button class='qbtn' type='submit'>Random JPG</button></form> ";
    html += "<form method='POST' action='/display_random_gif' style='display:inline;'><button class='qbtn' type='submit'>Random GIF</button></form>";
    html += "<form method='POST' action='/display_random' style='display:inline;'><button class='qbtn' type='submit'>Random Image</button></form>";
    html += "</div>";

    html += "</div>";
    return html;
}

// --- Resource Manager page builder [ADD] ---
String buildResourceManagerPage() {
    String html = _pageHeader;
    html += "<div class='section'><h1>Resource Manager</h1>";

    // Space info
    size_t total = FFat.totalBytes();
    size_t used  = FFat.usedBytes();
    size_t free  = total > used ? total - used : 0;
    html += "<div style='font-size:1.1em; margin:12px 0;'>";
    html += "FFat Used: " + String(used / 1024) + " KB / " + String(total / 1024) + " KB";
    html += " &mdash; Free: " + String(free / 1024) + " KB";
    html += "</div>";

    // List files in /resource
    html += "<div class='file-list'><strong>Manage Resource Files</strong><br>";
    File res = FFat.open("/resource");
    bool hasResource = false;
    if (res) {
        File f = res.openNextFile();
        while (f) {
            String fn = f.name();
            html += fn + " ";
            html += "<form style='display:inline;' method='POST' action='/delete_resource'>";
            html += "<input type='hidden' name='file' value='" + fn + "'>";
            html += "<input type='hidden' name='folder' value='/resource'>";
            html += "<button class='qbtn' type='submit'>Delete</button></form>";
            html += "<a class='qbtn' href='/sd/resource?file=" + fn + "' target='_blank'>Download</a><br>";
            hasResource = true;
            f = res.openNextFile();
        }
        res.close();
    }
    if (!hasResource) html += "No resource files found.";
    html += "<form method='POST' enctype='multipart/form-data' action='/upload_resource'>";
    html += "<input type='file' name='upload' multiple required><button class='qbtn' type='submit'>Upload</button></form></div>";

    html += "<div style='margin:18px 0;'><a class='qbtn' href='/'>Back to File Manager</a></div>";
    html += "</div>";
    html += _pageFooter;
    return html;
}

// --- Serve FFat files for preview/download ---
void serveFile(AsyncWebServerRequest *request) {
    String type = request->url();
    String file = request->arg("file");
    String path;
    if (type == "/sd/boot") path = "/boot/" + file;
    else if (type == "/sd/jpg") path = "/jpg/" + file;
    else if (type == "/sd/gif") path = "/gif/" + file;
    else if (type == "/sd/resource") path = "/resource/" + file;
    else {
        request->send(404, "text/plain", "Invalid file type");
        return;
    }
    File f = FFat.open(path);
    if (!f) {
        request->send(404, "text/plain", "File not found");
        return;
    }
    String contentType = file.endsWith(".gif") ? "image/gif" : (file.endsWith(".jpg") ? "image/jpeg" : "application/octet-stream");
    AsyncWebServerResponse *response = request->beginResponse(f, contentType, false);
    request->send(response);
    f.close();
}

// --- Handle upload (called both as request and upload handler) ---
void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    String url = request->url();
    String folder = "";
    String forceName = "";

    if (url == "/upload_boot") {
        folder = "/boot";
        forceName = filename.endsWith(".gif") ? "boot.gif" : "boot.jpg";
    } else if (url == "/upload_jpg") {
        folder = "/jpg";
    } else if (url == "/upload_gif") {
        folder = "/gif";
    } else if (url == "/upload_resource") {
        folder = "/resource";
    } else {
        return;
    }

    if (index == 0) {
        String targetPath = folder + "/";
        targetPath += (forceName.length() ? forceName : filename);
        uploadTargetPath = targetPath;
        int lastSlash = targetPath.lastIndexOf('/');
        if (lastSlash > 0) {
            String dir = targetPath.substring(0, lastSlash);
            if (!FFat.exists(dir.c_str())) {
                FFat.mkdir(dir.c_str());
            }
        }
        uploadFile = FFat.open(targetPath, FILE_WRITE);
        Serial.printf("[FileMan] Starting upload: %s\n", targetPath.c_str());
    }
    if (uploadFile) {
        uploadFile.write(data, len);
        yeild();
    }
    if (final) {
        if (uploadFile) uploadFile.close();
        Serial.printf("[FileMan] Upload complete: %s\n", uploadTargetPath.c_str());
    }
}

// --- Handle file delete (PATCHED for Serial debug & file/dir check) ---
void handleDelete(AsyncWebServerRequest *request) {
    String folder = request->arg("folder");
    String file = request->arg("file");
    String path = folder.length() > 0 ? folder + "/" + file : "/boot/" + file;

    if (FFat.exists(path.c_str())) {
        FFat.remove(path.c_str());
        Serial.printf("[FileMan] Deleted: %s\n", path.c_str());
    } else {
        Serial.printf("[FileMan] File not found for delete: %s\n", path.c_str());
    }
    String redirect = request->url().indexOf("resource") > 0 ? "/resource" : "/";
    request->redirect(redirect);
}

// --- Display random image helpers/handlers (trivial stubs for this example) ---
void handleDisplayRandom(AsyncWebServerRequest *request) {
    ImageDisplay::displayRandomImage();
    request->redirect("/");
}
void handleDisplayRandomJpg(AsyncWebServerRequest *request) {
    ImageDisplay::displayRandomJpg();
    request->redirect("/");
}
void handleDisplayRandomGif(AsyncWebServerRequest *request) {
    ImageDisplay::displayRandomGif();
    request->redirect("/");
}

void handleSelectImage(AsyncWebServerRequest *request) {
    String folder = request->arg("folder");
    String file = request->arg("file");
    String path = folder + "/" + file;
    ImageDisplay::displayImage(path);
    request->redirect("/");
}
