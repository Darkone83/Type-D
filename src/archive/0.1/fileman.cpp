// fileman.cpp
#include <FS.h>
#include <WebServer.h>
#include <SD_MMC.h>
#include "fileman.h"
#include "imagedisplay.h"

// --- Internal state ---
static WebServer* _server = nullptr;

// --- HTML page strings ---
static const char* _pageHeader =
    "<!DOCTYPE html><html><head>"
    "<title>File Manager</title>"
    "<meta name='viewport' content='width=400'>"
    "<style>body{font-family:sans-serif;text-align:center;}"
    "input[type=file]{margin:10px;}button{margin:5px;padding:5px 15px;}"
    ".file-list{margin:10px 0;display:inline-block;text-align:left;}"
    ".section{margin:30px 0;}</style></head><body>";

static const char* _pageFooter =
    "<div style='margin:32px 0;'>"
    "<form method='POST' action='/display_random'>"
    "<button type='submit' style='font-size:1.2em;padding:8px 28px;'>Display Random Image</button>"
    "</form></div>"
    "<div style='font-size:1.1em;margin-bottom:8px;'>Darkone83 -=- Andr0 -=- Team Resurgent</div>"
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
    "</script></body></html>";

// --- Forward declarations ---
String buildFileManagerPage();
String listBootImageSection();
String listGallerySection();
void handleUpload();
void handleDelete();
void serveFile();
void handleDisplayRandom();
void handleDisplayRandomJpg();
void handleDisplayRandomGif();
void handleSelectImage();
String getRandomGalleryImagePath();
String getRandomJpgImagePath();
String getRandomGifImagePath();

// --- Upload state ---
File uploadFile;
String uploadTargetPath;

// --- Setup routes and handlers ---
void FileMan::begin(WebServer& server) {
    _server = &server;

    // Main UI
    server.on("/", HTTP_GET, []() {
        _server->send(200, "text/html", buildFileManagerPage());
    });

    // Serve SD files
    server.on("/sd/boot", HTTP_GET, serveFile);
    server.on("/sd/jpg", HTTP_GET, serveFile);
    server.on("/sd/gif", HTTP_GET, serveFile);

    // Upload handlers
    server.on("/upload_boot", HTTP_POST, handleUpload, handleUpload);
    server.on("/upload_jpg", HTTP_POST, handleUpload, handleUpload);
    server.on("/upload_gif", HTTP_POST, handleUpload, handleUpload);

    // Delete handlers
    server.on("/delete_boot", HTTP_POST, handleDelete);
    server.on("/delete_gallery", HTTP_POST, handleDelete);

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
    html += "<h1>File Manager</h1>";
    html += listBootImageSection();
    html += listGallerySection();
    html += _pageFooter;
    return html;
}

String listBootImageSection() {
    String html = "<div class='section'><h2>Change Boot Image or Animation</h2>";
    File root = SD_MMC.open("/boot");
    bool hasBootImg = false;
    if (root) {
        File f = root.openNextFile();
        while (f) {
            String fn = f.name();
            if (fn.endsWith("boot.jpg") || fn.endsWith("boot.gif")) {
                html += "<div><img src='/sd/boot?file=" + fn + "' style='max-width:140px;max-height:140px;'><br>" + fn;
                html += "<form method='POST' action='/delete_boot'><input type='hidden' name='file' value='" + fn + "'>";
                html += "<button type='submit'>Delete</button></form></div>";
                hasBootImg = true;
            }
            f = root.openNextFile();
        }
        root.close();
    }
    if (!hasBootImg)
        html += "<div>No boot image present.</div>";
    html += "<form method='POST' enctype='multipart/form-data' action='/upload_boot'>";
    html += "<input type='file' name='upload' accept='.jpg,.gif' required><button type='submit'>Upload</button>";
    html += "</form></div>";
    return html;
}

String listGallerySection() {
    String html = "<div class='section'><h2>Manage Images</h2>";

    // JPGs
    html += "<div class='file-list'><strong>JPGs:</strong><br>";
    File jpg = SD_MMC.open("/jpg");
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
                html += "<button type='submit'>Delete</button></form>";
                html += "<form style='display:inline;' method='POST' action='/select_image'>";
                html += "<input type='hidden' name='file' value='" + fn + "'>";
                html += "<input type='hidden' name='folder' value='/jpg'>";
                html += "<button type='submit'>Select</button></form><br>";
                hasJpg = true;
            }
            f = jpg.openNextFile();
        }
        jpg.close();
    }
    if (!hasJpg) html += "No jpg files found.";
    html += "<form method='POST' enctype='multipart/form-data' action='/upload_jpg'>";
    html += "<input type='file' name='upload' accept='.jpg' multiple required><button type='submit'>Upload</button></form></div>";

    // GIFs
    html += "<div class='file-list'><strong>GIFs:</strong><br>";
    File gif = SD_MMC.open("/gif");
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
                html += "<button type='submit'>Delete</button></form>";
                html += "<form style='display:inline;' method='POST' action='/select_image'>";
                html += "<input type='hidden' name='file' value='" + fn + "'>";
                html += "<input type='hidden' name='folder' value='/gif'>";
                html += "<button type='submit'>Select</button></form><br>";
                hasGif = true;
            }
            f = gif.openNextFile();
        }
        gif.close();
    }
    if (!hasGif) html += "No gif files found.";
    html += "<form method='POST' enctype='multipart/form-data' action='/upload_gif'>";
    html += "<input type='file' name='upload' accept='.gif' multiple required><button type='submit'>Upload</button></form></div>";

    html += "<div style='margin:10px 0;'>";
    html += "<form method='POST' action='/display_random_jpg' style='display:inline;'><button type='submit'>Random JPG</button></form> ";
    html += "<form method='POST' action='/display_random_gif' style='display:inline;'><button type='submit'>Random GIF</button></form>";
    html += "</div>";

    html += "</div>";
    return html;
}

// --- Serve SD files for preview/download ---
void serveFile() {
    String type = _server->uri();
    String file = _server->arg("file");
    String path;
    if (type == "/sd/boot") path = "/boot/" + file;
    else if (type == "/sd/jpg") path = "/jpg/" + file;
    else if (type == "/sd/gif") path = "/gif/" + file;
    else {
        _server->send(404, "text/plain", "Invalid file type");
        return;
    }
    File f = SD_MMC.open(path);
    if (!f) {
        _server->send(404, "text/plain", "File not found");
        return;
    }
    String contentType = file.endsWith(".gif") ? "image/gif" : (file.endsWith(".jpg") ? "image/jpeg" : "application/octet-stream");
    _server->streamFile(f, contentType);
    f.close();
}

// --- Handle upload (called both as request and upload handler) ---
void handleUpload() {
    HTTPUpload& upload = _server->upload();
    String url = _server->uri();
    String folder = "";
    String forceName = "";

    if (url == "/upload_boot") {
        folder = "/boot";
        forceName = upload.filename.endsWith(".gif") ? "boot.gif" : "boot.jpg";
    } else if (url == "/upload_jpg") {
        folder = "/jpg";
    } else if (url == "/upload_gif") {
        folder = "/gif";
    } else {
        return;
    }

    if (upload.status == UPLOAD_FILE_START) {
        String targetPath = folder + "/";
        targetPath += (forceName.length() ? forceName : upload.filename);
        uploadTargetPath = targetPath;
        uploadFile = SD_MMC.open(targetPath, FILE_WRITE);
        Serial.printf("[FileMan] Starting upload: %s\n", targetPath.c_str());
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (uploadFile)
            uploadFile.write(upload.buf, upload.currentSize);
     } else if (upload.status == UPLOAD_FILE_END) {
        if (uploadFile)
            uploadFile.close();
        Serial.printf("[FileMan] Upload complete: %s\n", uploadTargetPath.c_str());
        // Auto-redirect to main page after upload
        _server->send(200, "text/html", "<b>Upload complete.</b><br>Redirecting...<script>setTimeout(()=>{location.href='/'},500);</script>");
    }
}

// --- Handle file delete ---
void handleDelete() {
    String folder = _server->arg("folder");
    String file = _server->arg("file");
    String path = folder.length() > 0 ? folder + "/" + file : "/boot/" + file;
    if (SD_MMC.remove(path)) {
        _server->send(200, "text/html", "<b>File deleted.</b><br><a href='/'>Back</a>");
    } else {
        _server->send(500, "text/html", "<b>Delete failed.</b><br><a href='/'>Back</a>");
    }
}

// --- Display random image on TFT (button handler) ---
void handleDisplayRandom() {
    String imagePath = getRandomGalleryImagePath();
    if (imagePath.length() > 0) {
        ImageDisplay::displayImage(imagePath);
        Serial.printf("[FileMan] Displaying random image: %s\n", imagePath.c_str());
        _server->send(200, "text/html", "<b>Random image displayed on device!</b><br><a href='/'>Back</a>");
    } else {
        _server->send(200, "text/html", "<b>No images found to display!</b><br><a href='/'>Back</a>");
    }
}

void handleDisplayRandomJpg() {
    String imagePath = getRandomJpgImagePath();
    if (imagePath.length() > 0) {
        ImageDisplay::displayImage(imagePath);
        Serial.printf("[FileMan] Displaying random JPG: %s\n", imagePath.c_str());
        _server->send(200, "text/html", "<b>Random JPG displayed on device!</b><br><a href='/'>Back</a>");
    } else {
        _server->send(200, "text/html", "<b>No JPG images found to display!</b><br><a href='/'>Back</a>");
    }
}

void handleDisplayRandomGif() {
    String imagePath = getRandomGifImagePath();
    if (imagePath.length() > 0) {
        ImageDisplay::displayImage(imagePath);
        Serial.printf("[FileMan] Displaying random GIF: %s\n", imagePath.c_str());
        _server->send(200, "text/html", "<b>Random GIF displayed on device!</b><br><a href='/'>Back</a>");
    } else {
        _server->send(200, "text/html", "<b>No GIF images found to display!</b><br><a href='/'>Back</a>");
    }
}

void handleSelectImage() {
    String folder = _server->arg("folder");
    String file = _server->arg("file");
    String imagePath = folder + "/" + file;
    if (SD_MMC.exists(imagePath)) {
        ImageDisplay::displayImage(imagePath);
        Serial.printf("[FileMan] Displaying selected image: %s\n", imagePath.c_str());
        _server->send(200, "text/html", "<b>Selected image displayed on device!</b><br><a href='/'>Back</a>");
    } else {
        _server->send(404, "text/html", "<b>File not found!</b><br><a href='/'>Back</a>");
    }
}

// --- Pick a random gallery image path from /jpg or /gif ---
String getRandomGalleryImagePath() {
    std::vector<String> allImages;
    File jpg = SD_MMC.open("/jpg");
    if (jpg) {
        File f = jpg.openNextFile();
        while (f) {
            String fn = f.name();
            if (fn.endsWith(".jpg")) {
                allImages.push_back("/jpg/" + fn);
            }
            f = jpg.openNextFile();
        }
        jpg.close();
    }
    File gif = SD_MMC.open("/gif");
    if (gif) {
        File f = gif.openNextFile();
        while (f) {
            String fn = f.name();
            if (fn.endsWith(".gif")) {
                allImages.push_back("/gif/" + fn);
            }
            f = gif.openNextFile();
        }
        gif.close();
    }
    if (!allImages.empty()) {
        int r = random(0, allImages.size());
        return allImages[r];
    }
    return "";
}

String getRandomJpgImagePath() {
    std::vector<String> jpgs;
    File jpg = SD_MMC.open("/jpg");
    if (jpg) {
        File f = jpg.openNextFile();
        while (f) {
            String fn = f.name();
            if (fn.endsWith(".jpg")) {
                jpgs.push_back("/jpg/" + fn);
            }
            f = jpg.openNextFile();
        }
        jpg.close();
    }
    if (!jpgs.empty()) {
        int r = random(0, jpgs.size());
        return jpgs[r];
    }
    return "";
}

String getRandomGifImagePath() {
    std::vector<String> gifs;
    File gif = SD_MMC.open("/gif");
    if (gif) {
        File f = gif.openNextFile();
        while (f) {
            String fn = f.name();
            if (fn.endsWith(".gif")) {
                gifs.push_back("/gif/" + fn);
            }
            f = gif.openNextFile();
        }
        gif.close();
    }
    if (!gifs.empty()) {
        int r = random(0, gifs.size());
        return gifs[r];
    }
    return "";
}
