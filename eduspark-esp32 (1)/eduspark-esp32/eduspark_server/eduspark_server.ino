#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <SD.h>
#include <ctype.h>
#include <DNSServer.h>
#include <driver/gpio.h>

const char *ssid = "Off-learn_AP";
const char *password = "12345678";

WebServer server(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;

#define SD_SCK 5
#define SD_MISO 15
#define SD_MOSI 2
#define SD_CS 16

SPIClass spi(VSPI);

const char *CONTENT_ROOT = "/content";
const size_t MAX_FOLDER_COUNT = 20;
const size_t MAX_FILES_PER_FOLDER = 50;
const size_t MAX_PDF_FILES_PER_FOLDER = 50;
const size_t MAX_WORD_FILES_PER_FOLDER = 50;
const size_t MAX_AUDIO_FILES_PER_FOLDER = 50;
const size_t MAX_WEB_FILES_PER_FOLDER = 50;
const size_t MAX_PRESENTATION_FILES_PER_FOLDER = 50;
const size_t MAX_VIDEO_FILES_PER_FOLDER = 50;
const size_t MAX_FOLDER_NAME_LEN = 32;
const size_t MAX_FILE_NAME_LEN = 64;
const unsigned long FOLDERS_CACHE_TTL_MS = 15000; // 15s cache
const unsigned long FOLDERS_SCAN_BUDGET_MS = 1500;
const uint32_t SD_SPI_FREQUENCY_HZ = 400000;
const uint32_t SD_WRITE_SPI_FREQUENCY_HZ = 100000;
const unsigned long SD_RETRY_DELAY_MS = 20;
const uint8_t SD_IO_SETTLE_DELAY_MS = 20;
const size_t SD_UPLOAD_FLUSH_INTERVAL_BYTES = 8192;
const uint8_t SD_UPLOAD_CHUNK_DELAY_MS = 5;
const uint8_t SD_INIT_MAX_RETRIES = 5;
const size_t MAX_UPLOAD_SIZE_BYTES = 3 * 1024 * 1024;
const size_t MAX_UPLOAD_CHUNK_BYTES = 4096;
const size_t SD_WRITE_SLICE_BYTES = 256;
const unsigned long RAW_CHUNK_READ_TIMEOUT_MS = 5000;

bool sdReady = false;
volatile bool sdWriteInProgress = false;
// Helper to update sdReady with logging
void setSdReady(bool v) {
  if (sdReady == v) return;
  sdReady = v;
  Serial.printf("sdReady -> %s\n", sdReady ? "true" : "false");
}

// Re-apply GPIO compensation for MOSI (GPIO 2) and MISO (GPIO 15).
// Must be called AFTER every spi.begin() or SD.begin() because those
// reconfigure the pins and reset pull mode / drive strength.
void applyMosiCompensation() {
  gpio_set_pull_mode(GPIO_NUM_2, GPIO_FLOATING);
  gpio_set_drive_capability(GPIO_NUM_2, GPIO_DRIVE_CAP_3);  // 40 mA
  gpio_set_pull_mode(GPIO_NUM_15, GPIO_PULLUP_ONLY);
}

// Bit-bang 80+ clock cycles with CS HIGH (guaranteed).
// Unlike spi.transfer(), this does NOT assert CS via hardware,
// so the SD card truly ignores these clocks and resets its state machine.
// Only used during recovery — not during normal operation.
void bitBangIdleClocks() {
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  pinMode(SD_SCK, OUTPUT);
  for (int i = 0; i < 80; i++) {
    digitalWrite(SD_SCK, HIGH);
    delayMicroseconds(5);
    digitalWrite(SD_SCK, LOW);
    delayMicroseconds(5);
  }
  digitalWrite(SD_SCK, LOW);
}
String foldersCacheJson = "[]";
unsigned long foldersCacheAt = 0;
String libraryCacheJson = "[]";
unsigned long libraryCacheAt = 0;
const unsigned long LIBRARY_CACHE_TTL_MS = 10000;
int totalFileCountCache = -1;

struct UploadContext {
  File file;
  bool active = false;
  bool failed = false;
  size_t written = 0;
  size_t flushPending = 0;
  String safeFileName;
  String tempPath;
  String finalPath;
  String error;
};

UploadContext uploadCtx;

struct ChunkUploadContext {
  File file;
  bool active = false;
  bool failed = false;
  size_t written = 0;
  size_t offset = 0;
  size_t totalSize = 0;
  bool hasTotalSize = false;
  size_t lastChunkSize = 0;
  bool startedNewFile = false;
  String folderName;
  String safeFileName;
  String tempPath;
  String finalPath;
  String error;
};

ChunkUploadContext chunkCtx;

enum FileCategory {
  FILE_CAT_INVALID = 0,
  FILE_CAT_PDF,
  FILE_CAT_WORD,
  FILE_CAT_AUDIO,
  FILE_CAT_WEB,
  FILE_CAT_PRESENTATION,
  FILE_CAT_VIDEO
};

void handleRoot();
void handleFolders();
void handleCreateFolder();
void handleList();
void handleUpload();
void handleUploadResponse();
void handleUploadChunk();
void handleUploadChunkResponse();
void handleUploadRawChunk();
void handleListFiles();
void handleDashboardData();
void handleDownload();
void handleDelete();
void handleDeleteFolder();
void closeChunkFile();

bool ensureSdReady();
bool ensureContentRoot();
bool tryReinitializeSd();
File openFileWithRetry(const String &path, const char *mode);
bool openFileForAppendWithRetry(const String &path, File &file);
bool openFileForWriteWithRetry(const String &path, File &file);
bool openFileForReadWithRetry(const String &path, File &file);
bool mkdirWithRetry(const String &path);
bool pathExistsWithRetry(const String &path);
bool removeFileWithRetry(const String &path);
bool removeDirWithRetry(const String &path);
bool removePathRecursive(const String &path);
bool verifyFileAccessible(const String &path, size_t *sizeOut = nullptr);
bool writeUploadChunkBuffered(const uint8_t *data, size_t length, String &error);
bool writeFileBuffered(File &file, const uint8_t *data, size_t length, String &error);
bool writeFileAtOffset(const String &path, size_t offset, const uint8_t *data, size_t length, String &error);
void stabilizeSdAfterUpload();
void recoverSdCard();
bool requireFacultyIfProvided();
bool facultyWriteAllowed(String &error);
String getRole();
String urlDecodeComponent(const String &value);
bool isValidFolderName(const String &name, String &trimmed);
bool isValidFileName(const String &fileName, String &safeName);
String trimCopy(const String &value);
String getFolderPath(const String &folderName);
bool folderExists(const String &folderName);
bool findFolderCaseInsensitive(const String &folderName, String &matchedName);
bool resolveFolderName(const String &rawName, String &resolvedName);
size_t countFolders();
size_t countFilesInFolder(const String &folderName);
FileCategory categoryFromFileName(const String &fileName);
size_t countCategoryInFolder(const String &folderName, FileCategory category);
bool folderHasCategory(const String &folderName, FileCategory category);
String categoryLabel(FileCategory category);
String getMimeType(const String &path);
String jsonEscape(const String &value);
String joinPath(const String &left, const String &right);
void sendJson(int code, const String &body);
void sendJsonStatus(int code, const char *status, const String &message);
void sendJsonArray(int code, const String &json);
String extractParam(const char *name);
void cleanupUploadTempFile();
File openStaticFile(String path);

void setup() {
  Serial.begin(115200);

  WiFi.softAP(ssid, password);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  // Wait for bootloader to fully release GPIO 2 before touching SPI.
  // GPIO 2 is sampled during boot — if the SD card is driving MISO while
  // the bootloader holds GPIO 2 low, the SPI bus starts in a bad state.
  delay(500);

  spi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  spi.setFrequency(SD_SPI_FREQUENCY_HZ);
  applyMosiCompensation();

  // Try initializing SD with retries for stability
  setSdReady(false);
  for (uint8_t attempt = 0; attempt < SD_INIT_MAX_RETRIES; attempt++) {
    spi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    spi.setFrequency(SD_SPI_FREQUENCY_HZ);
    applyMosiCompensation();
    delay(50);
    bool ok = SD.begin(SD_CS, spi);
    if (!ok) {
      delay(100);
      ok = SD.begin(SD_CS);
    }
    if (ok) {
      applyMosiCompensation();
      setSdReady(true);
      break;
    }
    Serial.printf("SD mount attempt %u failed\n", attempt + 1);
    delay(SD_RETRY_DELAY_MS * (attempt + 1));
  }
  if (!sdReady) {
    Serial.println("SD mount failed after retries");
  } else {
    // Ensure content root exists at startup only
    if (!ensureContentRoot()) {
      setSdReady(false);
      Serial.println("Failed to create content root");
    }
  }

  server.enableCORS(true);

  const char *headerKeys[] = { "X-User-Role", "X-Role", "Content-Type" };
  server.collectHeaders(headerKeys, 3);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/folders", HTTP_GET, handleFolders);
  server.on("/createFolder", HTTP_POST, handleCreateFolder);
  server.on("/list", HTTP_GET, handleList);
  server.on("/upload", HTTP_POST, handleUploadResponse, handleUpload);
  server.on("/list-files", HTTP_GET, handleListFiles);
  server.on("/dashboard-data", HTTP_GET, handleDashboardData);
  server.on("/download", HTTP_GET, handleDownload);
  server.on("/delete", HTTP_POST, handleDelete);
  server.on("/deleteFolder", HTTP_POST, handleDeleteFolder);

  server.onNotFound([]() {
    // Captive portal redirect: if Host doesn't match our IP, redirect.
    if (!server.hostHeader().equals(WiFi.softAPIP().toString())) {
      server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
      server.send(302, "text/plain", ""); // Empty content quits early
      return;
    }

    // Map incoming requests to /public while protecting API routes.
    String orig = server.uri();

    // Keep original uri and path-only forms
    int q = orig.indexOf('?');
    String pathOnly = (q >= 0) ? orig.substring(0, q) : orig;
    if (pathOnly.length() == 0) pathOnly = "/";
    if (!pathOnly.startsWith("/")) pathOnly = "/" + pathOnly;

    // Silently ignore favicon.ico — browsers always request it, no need to touch SD
    if (pathOnly == "/favicon.ico") {
      server.send(204, "text/plain", "");
      return;
    }

    // Protect API routes: match exact path or path with query
    const char *protectedPaths[] = {"/upload", "/download", "/createFolder", "/list", "/folders", "/list-files", "/dashboard-data", "/delete", "/deleteFolder"};
    for (size_t i = 0; i < (sizeof(protectedPaths) / sizeof(protectedPaths[0])); ++i) {
      String p = protectedPaths[i];
      if (pathOnly == p || orig.startsWith(String(p) + "?")) {
        sendJsonStatus(404, "error", "Route not found");
        return;
      }
    }

    // Attempt to serve a static file — single attempt, no retries, no diagnostics.
    // Heavy retry/diagnostic logic was causing SD I/O exhaustion during rapid page loads.
    if (!sdReady || sdWriteInProgress) {
      // During uploads, refuse all SD reads to prevent SPI bus contention.
      if (sdWriteInProgress) {
        server.send(503, "text/plain", "Server busy");
        return;
      }
      sendJsonStatus(500, "error", "SD card not available");
      return;
    }

    File f = openStaticFile(pathOnly);
    if (f && !f.isDirectory()) {
      String servedPath = pathOnly;
      if (!servedPath.startsWith("/public/")) servedPath = "/public" + servedPath;
      
      if (servedPath.endsWith(".html") || servedPath.endsWith(".js") || servedPath.endsWith(".css") || servedPath.endsWith(".svg")) {
        server.sendHeader("Cache-Control", "public, max-age=60"); // Cache for 1 min
      } else {
        server.sendHeader("Cache-Control", "no-store");
      }
      server.streamFile(f, getMimeType(servedPath));
      f.close();
      delay(SD_IO_SETTLE_DELAY_MS);
      yield();
      return;
    }
    if (f) f.close();

    Serial.printf("onNotFound: 404 path='%s'\n", pathOnly.c_str());
    sendJsonStatus(404, "error", "Route not found");
  });

  // Debug endpoint to inspect SD root and /public contents
  server.on("/sd-debug", HTTP_GET, []() {
    String out = "SD debug:\n";
    out += String("sdReady=") + (sdReady ? "true" : "false") + "\n";
    String paths[] = {"/library.html", "/favicon.ico", "/public/library.html", "/public/favicon.ico", "/public/dashboard.html"};
    for (size_t i = 0; i < (sizeof(paths) / sizeof(paths[0])); ++i) {
      const String p = paths[i];
      bool ex = SD.exists(p);
      out += String(p) + " => " + (ex ? "exists" : "missing") + "\n";
    }

    // List files under /public
    out += "\n/public listing:\n";
    File pub = openFileWithRetry("/public", FILE_READ);
    if (!pub || !pub.isDirectory()) {
      if (pub) pub.close();
      out += "(no /public directory)\n";
    } else {
      File e = pub.openNextFile();
      while (e) {
        String name = e.name();
        out += (e.isDirectory() ? "[dir] " : "[file] ") + name + "\n";
        e.close();
        e = pub.openNextFile();
        yield();
      }
      pub.close();
    }

    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/plain", out);
  });

  server.begin();
  Serial.println("Server started");
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
}

void handleRoot() {
  if (sdWriteInProgress) {
    server.send(503, "text/plain", "Server busy");
    return;
  }
  if (!ensureSdReady()) {
    return;
  }

  File file = openStaticFile("/index.html");
  if ((!file || file.isDirectory()) && pathExistsWithRetry("/public/dashboard.html")) {
    if (file) {
      file.close();
      delay(SD_IO_SETTLE_DELAY_MS);
    }
    file = openStaticFile("/dashboard.html");
  }
  if (!file || file.isDirectory()) {
    if (file) {
      file.close();
      delay(SD_IO_SETTLE_DELAY_MS);
    }
    sendJsonStatus(404, "error", "index.html not found");
    return;
  }

  server.sendHeader("Cache-Control", "no-store");
  server.streamFile(file, "text/html");
  file.close();
  delay(SD_IO_SETTLE_DELAY_MS);
}

File openStaticFile(String path) {
  if (path.length() == 0 || path == "/") {
    path = "/index.html";
  }

  if (!path.startsWith("/")) {
    path = "/" + path;
  }

  // Try /public/ path FIRST — that's where static files actually live.
  // This avoids wasting SD operations on the root path that never has files.
  String publicPath = path;
  if (!publicPath.startsWith("/public/")) {
    publicPath = "/public" + path;
  }

  File file = openFileWithRetry(publicPath, FILE_READ);
  if (file && !file.isDirectory()) {
    return file;
  }
  if (file) {
    file.close();
    delay(SD_IO_SETTLE_DELAY_MS);
  }

  // Fallback: try direct path (rare — only if file exists at SD root)
  if (path != publicPath) {
    file = openFileWithRetry(path, FILE_READ);
    if (file && !file.isDirectory()) {
      return file;
    }
    if (file) file.close();
  }

  return File();
}

void handleFolders() {
  if (!sdReady) {
    // If SD not ready, try one last check to ensure content root. 
    // This helps if SD card was slow to mount at boot.
    if (ensureContentRoot()) {
      setSdReady(true);
    } else {
      sendJsonArray(200, "[]");
      return;
    }
  }

  unsigned long scanStartedAt = millis();
  if ((scanStartedAt - foldersCacheAt) < FOLDERS_CACHE_TTL_MS) {
    sendJsonArray(200, foldersCacheJson);
    return;
  }

  File root = openFileWithRetry(CONTENT_ROOT, FILE_READ);
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    // Fallback: If CONTENT_ROOT is missing or broken, at least return 'library' virtual folder
    // but try creating it once first to recover.
    if (mkdirWithRetry(CONTENT_ROOT)) {
        delay(10);
        root = openFileWithRetry(CONTENT_ROOT, FILE_READ);
    }
    
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        foldersCacheJson = "[\"library\"]";
        foldersCacheAt = scanStartedAt;
        sendJsonArray(200, foldersCacheJson);
        return;
    }
  }

  Serial.println("handleFolders: Scanning " + String(CONTENT_ROOT));
  String json = "[";
  json.reserve(1024);
  // Insert a virtual aggregated "library" folder at the front so UI can request all files
  json += "\"library\"";
  bool first = false;
  // If content root has no folders, we'll still return at least the virtual library
  File entry = root.openNextFile();
  int countTotal = 1; // Start at 1 for the virtual "library" folder
  while (entry) {
    if (entry.isDirectory()) {
      String name = entry.name();
      int slashIndex = name.lastIndexOf('/');
      if (slashIndex >= 0) {
        name = name.substring(slashIndex + 1);
      }

      // Skip the system "public" folder so it doesn't show as a subject
      if (!name.equalsIgnoreCase("public")) {
        if (json.length() > 1) json += ",";
        json += "\"" + jsonEscape(name) + "\"";
        countTotal++;
      }
    }

    entry.close();
    entry = root.openNextFile();
    yield();
  }

  root.close();
  json += "]";

  foldersCacheJson = json;
  foldersCacheAt = millis();
  Serial.printf("handleFolders: Finished, found %d items\n", countTotal);
  sendJsonArray(200, foldersCacheJson);
}

void handleCreateFolder() {
  if (!ensureSdReady()) return;

  if (!requireFacultyIfProvided()) {
    return;
  }

  String rawName = urlDecodeComponent(extractParam("name"));
  String folderName;
  if (!isValidFolderName(rawName, folderName)) {
    sendJsonStatus(400, "error", "Invalid folder name");
    return;
  }

  String existingFolder;
  if (folderExists(folderName) || findFolderCaseInsensitive(folderName, existingFolder)) {
    sendJsonStatus(400, "error", "Folder already exists");
    return;
  }

  if (countFolders() >= MAX_FOLDER_COUNT) {
    sendJsonStatus(400, "error", "Maximum folder limit reached");
    return;
  }

  String folderPath = getFolderPath(folderName);
  if (!mkdirWithRetry(folderPath)) {
    sendJsonStatus(400, "error", "Failed to create folder");
    return;
  }
  delay(SD_IO_SETTLE_DELAY_MS);

  // Verify creation instead of reinitializing SD.
  if (!folderExists(folderName)) {
    sendJsonStatus(500, "error", "Folder created but not accessible");
    return;
  }

  foldersCacheAt = 0;
  sendJsonStatus(200, "success", "Folder created");
}

void handleList() {
  if (!ensureSdReady()) return;

  String folderName;
  if (!resolveFolderName(extractParam("folder"), folderName)) {
    sendJsonStatus(400, "error", "Invalid or missing folder");
    return;
  }

  String folderPath = getFolderPath(folderName);
  File dir = openFileWithRetry(folderPath, FILE_READ);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    sendJsonStatus(404, "error", "Folder not found");
    return;
  }

  String json = "[";
  bool first = true;

  File entry = dir.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      String name = entry.name();
      int slashIndex = name.lastIndexOf('/');
      if (slashIndex >= 0) {
        name = name.substring(slashIndex + 1);
      }

      if (!name.endsWith(".part")) {
        if (!first) {
          json += ",";
        }
        json += "\"" + jsonEscape(name) + "\"";
        first = false;
      }
    }

    entry.close();
    entry = dir.openNextFile();
    yield();
  }

  dir.close();
  json += "]";
  sendJsonArray(200, json);
}

void handleUpload() {
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    cleanupUploadTempFile();
    uploadCtx = UploadContext();
    uploadCtx.active = true;
    sdWriteInProgress = true;  // Lock out all other SD access

    // Set low speed for writes on GPIO 2 (MOSI)
    spi.setFrequency(SD_WRITE_SPI_FREQUENCY_HZ);
    applyMosiCompensation();
    delay(50);
    yield();

    Serial.printf("Upload START (Low Speed): filename='%s' name='%s'\n", upload.filename.c_str(), upload.name.c_str());

    if (!sdReady) {
      uploadCtx.failed = true;
      uploadCtx.error = "SD card not available";
      sdWriteInProgress = false;
      spi.setFrequency(SD_SPI_FREQUENCY_HZ); // Restore speed
      return;
    }

    String authError;
    if (!facultyWriteAllowed(authError)) {
      uploadCtx.failed = true;
      uploadCtx.error = authError;
      sdWriteInProgress = false;
      spi.setFrequency(SD_SPI_FREQUENCY_HZ); // Restore speed
      return;
    }

    // Try upload.filename first, fall back to URL param "file"
    String rawFileName = upload.filename;
    if (rawFileName.length() == 0 || !isValidFileName(rawFileName, rawFileName)) {
      rawFileName = extractParam("file");
    }
    String safeFileName;
    if (!isValidFileName(rawFileName, safeFileName)) {
      Serial.printf("Upload: invalid filename='%s'\n", rawFileName.c_str());
      uploadCtx.failed = true;
      uploadCtx.error = "Invalid file name or file type";
      sdWriteInProgress = false;
      spi.setFrequency(SD_SPI_FREQUENCY_HZ); // Restore speed
      return;
    }
    Serial.printf("Upload: resolved filename='%s'\n", safeFileName.c_str());

    if (upload.totalSize > 0 && upload.totalSize > MAX_UPLOAD_SIZE_BYTES) {
      uploadCtx.failed = true;
      uploadCtx.error = "File too large. Max allowed size is 3MB";
      sdWriteInProgress = false;
      spi.setFrequency(SD_SPI_FREQUENCY_HZ); // Restore speed
      return;
    }

    String tempPath = joinPath(CONTENT_ROOT, "__upload_" + String(millis()) + ".part");
    if (pathExistsWithRetry(tempPath)) {
      removeFileWithRetry(tempPath);
      delay(SD_IO_SETTLE_DELAY_MS);
    }

    // Aggressive file-open retry loop. If write fails, it often crashes the card, 
    // so we call recoverSdCard() during the retry loop if needed.
    for (uint8_t openAttempt = 0; openAttempt < 5; openAttempt++) {
      applyMosiCompensation();
      delay(50 * (openAttempt + 1));
      Serial.printf("Upload: file open attempt %u for '%s'\n", openAttempt + 1, tempPath.c_str());
      uploadCtx.file = SD.open(tempPath, FILE_WRITE);
      if (uploadCtx.file) {
        Serial.printf("Upload: file open succeeded on attempt %u\n", openAttempt + 1);
        break;
      }
      
      // If second attempt fails, try a full hardware reset/remount
      if (openAttempt == 1) {
          Serial.println("Upload: SD appears frozen, attempting recovery mid-upload...");
          recoverSdCard();
          spi.setFrequency(SD_WRITE_SPI_FREQUENCY_HZ); // Stick to low speed for write
          applyMosiCompensation();
      }
      Serial.printf("Upload: file open attempt %u failed\n", openAttempt + 1);
    }

    if (!uploadCtx.file) {
      uploadCtx.failed = true;
      uploadCtx.error = "Failed to open upload file";
      Serial.println("Upload: Failed to open temp file for writing after all retries");
      sdWriteInProgress = false;
      spi.setFrequency(SD_SPI_FREQUENCY_HZ); // Restore speed
      return;
    }

    Serial.printf("Upload: opened temp file '%s' (handle valid=%d)\n", tempPath.c_str(), (bool)uploadCtx.file);

    uploadCtx.safeFileName = safeFileName;
    uploadCtx.tempPath = tempPath;
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadCtx.failed) {
      return;
    }

    if (upload.currentSize == 0 || upload.currentSize > MAX_UPLOAD_CHUNK_BYTES) {
      uploadCtx.failed = true;
      uploadCtx.error = "Invalid upload chunk";
      cleanupUploadTempFile();
      spi.setFrequency(SD_SPI_FREQUENCY_HZ); // Restore speed
      return;
    }

    if ((uploadCtx.written + upload.currentSize) > MAX_UPLOAD_SIZE_BYTES) {
      uploadCtx.failed = true;
      uploadCtx.error = "File too large. Max allowed size is 3MB";
      cleanupUploadTempFile();
      spi.setFrequency(SD_SPI_FREQUENCY_HZ); // Restore speed
      return;
    }

    // Use sliced writes (512 bytes) for maximum stability on GPIO 2 (MOSI)
    bool writeOk = false;
    for (uint8_t attempt = 0; attempt < 3; attempt++) {
        if (!uploadCtx.file) {
            // First failure might have crashed the card. Try to recover it.
            Serial.println("Upload WRITE: Card possibly crashed. Recovering...");
            recoverSdCard();
            spi.setFrequency(SD_WRITE_SPI_FREQUENCY_HZ); // Back to low speed
            applyMosiCompensation();
            delay(100);
            
            yield();
            uploadCtx.file = SD.open(uploadCtx.tempPath, FILE_APPEND);
        }
        
        String writeError;
        if (writeFileBuffered(uploadCtx.file, upload.buf, upload.currentSize, writeError)) {
            writeOk = true;
            break;
        }
        
        Serial.printf("Upload WRITE failed (attempt %u): %s\n", attempt + 1, writeError.c_str());
        if (uploadCtx.file) {
            uploadCtx.file.close();
        }
        uploadCtx.file = File();
        delay(100);
        yield();
    }

    if (!writeOk) {
      Serial.printf("Upload WRITE: all retries failed at totalWritten=%u\n", (unsigned)uploadCtx.written);
      uploadCtx.failed = true;
      uploadCtx.error = "SD card write failed";
      cleanupUploadTempFile();
      spi.setFrequency(SD_SPI_FREQUENCY_HZ); // Restore speed
      return;
    }

    uploadCtx.written += upload.currentSize;
    uploadCtx.flushPending += upload.currentSize;

    if (uploadCtx.flushPending >= SD_UPLOAD_FLUSH_INTERVAL_BYTES) {
      uploadCtx.file.flush();
      uploadCtx.flushPending = 0;
      delay(SD_UPLOAD_CHUNK_DELAY_MS);
    }

    yield();

    // Log progress every ~50KB instead of every chunk
    if ((uploadCtx.written % 50000) < upload.currentSize) {
      Serial.printf("Upload progress: %uKB written\n", (unsigned)(uploadCtx.written / 1024));
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    Serial.printf("Upload END: written=%u failed=%s\n", (unsigned)uploadCtx.written, uploadCtx.failed ? "true" : "false");
    if (uploadCtx.file) {
      uploadCtx.file.flush();
      delay(100); 
      uploadCtx.file.close();
      uploadCtx.file = File();
      // MANDATORY: Give card time to finish any internal block writes/housekeeping
      // before we try to modify the FAT directory during finalized rename. 
      delay(500); 
      yield();
    }

    if (uploadCtx.failed) {
      cleanupUploadTempFile();
      return;
    }

    if (uploadCtx.written == 0) {
      uploadCtx.failed = true;
      uploadCtx.error = "Uploaded file is empty";
      cleanupUploadTempFile();
      return;
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    uploadCtx.failed = true;
    uploadCtx.error = "Upload aborted";
    cleanupUploadTempFile();
    sdWriteInProgress = false;
  }
}

void handleUploadResponse() {
  if (uploadCtx.failed) {
    sdWriteInProgress = false;
    recoverSdCard();
    spi.setFrequency(SD_SPI_FREQUENCY_HZ); // Restore speed for UI
    sendJsonStatus(400, "error", uploadCtx.error.length() ? uploadCtx.error : "Upload failed");
    cleanupUploadTempFile();
    uploadCtx = UploadContext();
    return;
  }

  // Ensure low speed for the finalization/metadata operations (folder checks, rename)
  spi.setFrequency(SD_WRITE_SPI_FREQUENCY_HZ);
  applyMosiCompensation();
  delay(100);

  if (!uploadCtx.active || uploadCtx.finalPath.isEmpty()) {
    String authError;
    if (!facultyWriteAllowed(authError)) {
      sendJsonStatus(403, "error", authError);
      cleanupUploadTempFile();
      uploadCtx = UploadContext();
      return;
    }
    // Read folder param, validate, and create if missing
    String rawFolder = extractParam("folder");
    String decoded = urlDecodeComponent(rawFolder);
    String folderName;
    if (!isValidFolderName(decoded, folderName)) {
      sendJsonStatus(400, "error", "Invalid or missing folder");
      cleanupUploadTempFile();
      uploadCtx = UploadContext();
      return;
    }

    String folderPath = getFolderPath(folderName);
    if (!folderExists(folderName)) {
      delay(10);
      if (folderExists(folderName)) {
        folderPath = getFolderPath(folderName);
      } else {
      // Enforce global folder cap when auto-creating from uploads
      if (countFolders() >= MAX_FOLDER_COUNT) {
        sendJsonStatus(400, "error", "Folder limit reached");
        cleanupUploadTempFile();
        uploadCtx = UploadContext();
        return;
      }
      String matched;
      if (findFolderCaseInsensitive(folderName, matched)) {
        folderName = matched;
        folderPath = getFolderPath(folderName);
      } else {
        Serial.printf("Folder '%s' does not exist - creating...\n", folderName.c_str());
          if (!mkdirWithRetry(folderPath)) {
            sendJsonStatus(500, "error", "Failed to create folder");
            cleanupUploadTempFile();
            uploadCtx = UploadContext();
            return;
          }
        }
        delay(SD_IO_SETTLE_DELAY_MS);

        // Verify folder exists rather than reinitializing SD
        if (!folderExists(folderName)) {
          sendJsonStatus(500, "error", "Folder created but not accessible");
          cleanupUploadTempFile();
          uploadCtx = UploadContext();
          return;
        }
        Serial.printf("Folder '%s' created or matched existing\n", folderName.c_str());
      }
    }

    if (uploadCtx.safeFileName.isEmpty()) {
      sendJsonStatus(400, "error", "No upload received");
      cleanupUploadTempFile();
      uploadCtx = UploadContext();
      return;
    }

    Serial.printf("Finalizing upload - folder='%s' file='%s' size=%u\n", folderName.c_str(), uploadCtx.safeFileName.c_str(), uploadCtx.written);

    if (countFilesInFolder(folderName) >= MAX_FILES_PER_FOLDER) {
      sendJsonStatus(400, "error", "Maximum 50 files allowed");
      cleanupUploadTempFile();
      uploadCtx = UploadContext();
      return;
    }

    FileCategory category = categoryFromFileName(uploadCtx.safeFileName);
    if (category == FILE_CAT_INVALID) {
      sendJsonStatus(400, "error", "Invalid file type");
      cleanupUploadTempFile();
      uploadCtx = UploadContext();
      return;
    }

    if (countCategoryInFolder(folderName, category) >= 50) {
      sendJsonStatus(400, "error", "Category limit reached");
      cleanupUploadTempFile();
      uploadCtx = UploadContext();
      return;
    }

    String finalPath = joinPath(getFolderPath(folderName), uploadCtx.safeFileName);
    Serial.printf("Finalizing upload into: %s\n", finalPath.c_str());
    if (SD.exists(finalPath)) {
      sendJsonStatus(400, "error", "File already exists");
      cleanupUploadTempFile();
      uploadCtx = UploadContext();
      return;
    }

    // Robust rename with recovery attempt
    bool renameOk = false;
    for (uint8_t renameAttempt = 0; renameAttempt < 3 && !renameOk; renameAttempt++) {
        if (SD.rename(uploadCtx.tempPath, finalPath)) {
            renameOk = true;
            break;
        }
        Serial.printf("SD.rename: failed attempt %u, recovering...\n", renameAttempt + 1);
        recoverSdCard();
        spi.setFrequency(SD_WRITE_SPI_FREQUENCY_HZ); 
        applyMosiCompensation();
        delay(200);
    }

    if (!renameOk) {
      sendJsonStatus(400, "error", "Failed to finalize upload (rename error)");
      cleanupUploadTempFile();
      uploadCtx = UploadContext();
      spi.setFrequency(SD_SPI_FREQUENCY_HZ);
      return;
    }
    delay(50);

    size_t finalSize = 0;
    if (!verifyFileAccessible(finalPath, &finalSize)) {
      sendJsonStatus(500, "error", "Uploaded file is not accessible after finalize");
      cleanupUploadTempFile();
      uploadCtx = UploadContext();
      return;
    }
    if (finalSize != uploadCtx.written) {
      sendJsonStatus(500, "error", "Final file size mismatch");
      cleanupUploadTempFile();
      uploadCtx = UploadContext();
      return;
    }

    uploadCtx.finalPath = finalPath;
  }

  if (uploadCtx.finalPath.isEmpty()) {
    sendJsonStatus(400, "error", "No upload received");
    cleanupUploadTempFile();
    uploadCtx = UploadContext();
    return;
  }

  stabilizeSdAfterUpload();
  sdWriteInProgress = false;
  spi.setFrequency(SD_SPI_FREQUENCY_HZ); // Restore normal speed
  libraryCacheAt = 0;
  foldersCacheAt = 0; // Ensure new folders appear immediately
  totalFileCountCache = -1;
  sendJsonStatus(200, "success", "Upload completed");
  uploadCtx = UploadContext();
}

void handleUploadChunk() {
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    Serial.println("Upload API called");
    Serial.printf("Upload request URI: %s\n", server.uri().c_str());
    Serial.printf("Upload params: folder='%s' file='%s' offset='%s' totalSize='%s'\n",
                  extractParam("folder").c_str(),
                  extractParam("file").c_str(),
                  extractParam("offset").c_str(),
                  extractParam("totalSize").c_str());

    chunkCtx = ChunkUploadContext();
    chunkCtx.active = true;

    if (!sdReady) {
      chunkCtx.failed = true;
      chunkCtx.error = "SD card not available";
      return;
    }

    String authError;
    if (!facultyWriteAllowed(authError)) {
      chunkCtx.failed = true;
      chunkCtx.error = authError;
      return;
    }

    String rawOffset = extractParam("offset");
    long rawOffsetValue = rawOffset.length() > 0 ? rawOffset.toInt() : -1;
    if (rawOffsetValue < 0) {
      chunkCtx.failed = true;
      chunkCtx.error = "Invalid chunk metadata";
      return;
    }

    String folderName;
    if (!resolveFolderName(extractParam("folder"), folderName)) {
      chunkCtx.failed = true;
      chunkCtx.error = "Invalid chunk metadata";
      return;
    }

    String safeFileName;
    if (!isValidFileName(extractParam("file"), safeFileName)) {
      chunkCtx.failed = true;
      chunkCtx.error = "Invalid chunk metadata";
      return;
    }

    if (!folderExists(folderName)) {
      chunkCtx.failed = true;
      chunkCtx.error = "Selected folder does not exist";
      return;
    }

    size_t offset = static_cast<size_t>(rawOffsetValue);
    String finalPath = joinPath(getFolderPath(folderName), safeFileName);
    long rawTotalSize = extractParam("totalSize").toInt();
    bool hasTotalSize = rawTotalSize > 0;
    size_t totalSize = hasTotalSize ? static_cast<size_t>(rawTotalSize) : 0;
    if (hasTotalSize && totalSize > MAX_UPLOAD_SIZE_BYTES) {
      chunkCtx.failed = true;
      chunkCtx.error = "File too large. Max allowed size is 3MB";
      return;
    }

    String partName = trimCopy(upload.name);
    if (partName.length() > 0 && partName != "chunk") {
      chunkCtx.failed = true;
      chunkCtx.error = "Invalid multipart field";
      return;
    }

    // Record metadata only; each chunk request opens, writes, flushes, and closes its file handle.
    chunkCtx.folderName = folderName;
    chunkCtx.safeFileName = safeFileName;
    chunkCtx.offset = offset;
    chunkCtx.totalSize = totalSize;
    chunkCtx.hasTotalSize = hasTotalSize;
    chunkCtx.startedNewFile = (offset == 0);
    chunkCtx.tempPath = finalPath;
    chunkCtx.finalPath = finalPath;
    // Maintain cumulative written size across the upload session.
    if (offset == 0) {
      chunkCtx.written = 0;
      // Always create new file for offset 0; remove existing if present.
      Serial.printf("Chunk start: offset=0 → creating file %s\n", finalPath.c_str());
      if (pathExistsWithRetry(finalPath)) {
        Serial.printf("Existing file found at %s - removing before create\n", finalPath.c_str());
        if (!removeFileWithRetry(finalPath)) {
          chunkCtx.failed = true;
          chunkCtx.error = "Failed to reset upload file";
          return;
        }
      }
      chunkCtx.file = openFileWithRetry(finalPath, FILE_WRITE);
      if (!chunkCtx.file) {
        chunkCtx.failed = true;
        chunkCtx.error = "Failed to open chunk file";
        Serial.printf("Failed to open chunk file: %s\n", finalPath.c_str());
        return;
      }
    } else {
      // For resumed chunks ensure existence and expected size
      if (!pathExistsWithRetry(finalPath)) {
        chunkCtx.failed = true;
        chunkCtx.error = "Missing upload session";
        Serial.printf("Chunk start: missing file path=%s offset=%u\n", finalPath.c_str(), (unsigned)offset);
        return;
      }
      // Extra settle time for FAT to commit after previous chunk close
      delay(SD_IO_SETTLE_DELAY_MS * 2);
      File tempRead = openFileWithRetry(finalPath, FILE_READ);
      size_t existingSize = tempRead ? static_cast<size_t>(tempRead.size()) : 0;
      if (tempRead) {
        tempRead.close();
        delay(SD_IO_SETTLE_DELAY_MS);
      }
      // Retry size check if mismatch (handles slow FAT commit on ESP32)
      if (existingSize != offset) {
        delay(SD_IO_SETTLE_DELAY_MS * 3);
        File retryRead = openFileWithRetry(finalPath, FILE_READ);
        existingSize = retryRead ? static_cast<size_t>(retryRead.size()) : 0;
        if (retryRead) {
          retryRead.close();
          delay(SD_IO_SETTLE_DELAY_MS);
        }
      }
      Serial.printf("Chunk append: offset=%u → appending to %s (existing size=%u)\n", (unsigned)offset, finalPath.c_str(), (unsigned)existingSize);
      if (existingSize > offset) {
        // Duplicate/overlap chunk - skip write, report actual size
        Serial.printf("Duplicate chunk: client offset=%u file size=%u - skipping write\n", (unsigned)offset, (unsigned)existingSize);
        chunkCtx.offset = existingSize;
        chunkCtx.written = existingSize;
        // Leave chunkCtx.file invalid so UPLOAD_FILE_WRITE is skipped
      } else if (existingSize < offset) {
        chunkCtx.failed = true;
        chunkCtx.error = "Offset mismatch";
        Serial.printf("Offset mismatch: expected=%u actual=%u path=%s\n", (unsigned)offset, (unsigned)existingSize, finalPath.c_str());
        return;
      } else {
        // Exact match - normal append
        chunkCtx.written = existingSize;
        chunkCtx.file = openFileWithRetry(finalPath, FILE_APPEND);
        if (!chunkCtx.file) {
          chunkCtx.failed = true;
          chunkCtx.error = "Failed to open chunk file";
          Serial.printf("Failed to open chunk file: %s\n", finalPath.c_str());
          return;
        }
      }
    }

    Serial.printf("Chunk upload metadata - folder='%s' file='%s' offset=%u total=%u\n", chunkCtx.folderName.c_str(), chunkCtx.safeFileName.c_str(), (unsigned)chunkCtx.offset, (unsigned)chunkCtx.totalSize);

  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (chunkCtx.failed || chunkCtx.tempPath.length() == 0 || !chunkCtx.file) {
      return;
    }

    if (upload.currentSize == 0 || upload.currentSize > MAX_UPLOAD_CHUNK_BYTES) {
      chunkCtx.failed = true;
      chunkCtx.error = "Invalid upload chunk";
      closeChunkFile();
      return;
    }

    // Record last chunk size for finalization checks
    chunkCtx.lastChunkSize += static_cast<size_t>(upload.currentSize);

    // Verify that writing this data will not exceed declared total
    if (chunkCtx.hasTotalSize && (chunkCtx.offset + upload.currentSize) > chunkCtx.totalSize) {
      chunkCtx.failed = true;
      chunkCtx.error = "Chunk exceeds file size";
      closeChunkFile();
      return;
    }

    // Write data directly — do NOT call file.size() as ESP32 SD returns garbage
    // on freshly opened FILE_WRITE handles (seen as 1073516604 in serial log).
    // Trust chunkCtx.offset which was verified at UPLOAD_FILE_START.
    size_t written = chunkCtx.file.write(upload.buf, upload.currentSize);
    if (written != upload.currentSize) {
      // Retry once
      delay(SD_IO_SETTLE_DELAY_MS);
      written = chunkCtx.file.write(upload.buf, upload.currentSize);
    }
    if (written != upload.currentSize) {
      chunkCtx.failed = true;
      chunkCtx.error = "Failed to write chunk data";
      Serial.printf("Write failed: requested=%u written=%u path=%s\n", (unsigned)upload.currentSize, (unsigned)written, chunkCtx.tempPath.c_str());
      closeChunkFile();
      return;
    }

    chunkCtx.file.flush();
    delay(SD_IO_SETTLE_DELAY_MS);

    // Update offset by bytes written (no file.size() call needed)
    chunkCtx.offset += upload.currentSize;
    chunkCtx.written = chunkCtx.offset;
    yield();
    delay(SD_UPLOAD_CHUNK_DELAY_MS);

  } else if (upload.status == UPLOAD_FILE_END) {
    closeChunkFile();
    delay(SD_IO_SETTLE_DELAY_MS);
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    closeChunkFile();
    chunkCtx.failed = true;
    chunkCtx.error = "Upload aborted";
    Serial.printf("Chunk upload aborted for path=%s\n", chunkCtx.tempPath.c_str());
  }
}

void handleUploadChunkResponse() {
  if (chunkCtx.failed) {
    closeChunkFile();
    recoverSdCard();
    sendJsonStatus(400, "error", chunkCtx.error.length() ? chunkCtx.error : "Chunk upload failed");
    chunkCtx = ChunkUploadContext();
    return;
  }

  if (!ensureSdReady()) {
    chunkCtx = ChunkUploadContext();
    return;
  }

  if (!chunkCtx.active || chunkCtx.tempPath.length() == 0) {
    recoverSdCard();
    sendJsonStatus(400, "error", "Invalid chunk state");
    chunkCtx = ChunkUploadContext();
    return;
  }

  if (!verifyFileAccessible(chunkCtx.tempPath)) {
    recoverSdCard();
    sendJsonStatus(400, "error", "Chunk file missing");
    chunkCtx = ChunkUploadContext();
    return;
  }

  if (chunkCtx.hasTotalSize) {
    size_t currentFileSize = chunkCtx.written;
    if (currentFileSize >= chunkCtx.totalSize) {
      File assembled = openFileWithRetry(chunkCtx.finalPath, FILE_READ);
      size_t assembledSize = assembled ? static_cast<size_t>(assembled.size()) : 0;
      if (assembled) {
        assembled.close();
        delay(SD_IO_SETTLE_DELAY_MS);
      }
      Serial.printf("Assembled size=%u expected=%u\n", (unsigned)assembledSize, (unsigned)chunkCtx.totalSize);
      if (assembledSize != chunkCtx.totalSize) {
        recoverSdCard();
        sendJsonStatus(400, "error", "Final file size mismatch");
        chunkCtx = ChunkUploadContext();
        return;
      }
      if (!verifyFileAccessible(chunkCtx.finalPath)) {
        recoverSdCard();
        sendJsonStatus(500, "error", "Uploaded file is not accessible after write");
        chunkCtx = ChunkUploadContext();
        return;
      }
      Serial.printf("Upload complete: file='%s' totalSize=%u\n", chunkCtx.safeFileName.c_str(), (unsigned)assembledSize);
      libraryCacheAt = 0;
      totalFileCountCache = -1;
      stabilizeSdAfterUpload();
    }
  }

  // Return current written offset so client can track/recover
  String resp = "{\"status\":\"ok\",\"written\":" + String(chunkCtx.written) + "}";
  sendJson(200, resp);
  chunkCtx = ChunkUploadContext();
}

void handleUploadRawChunk() {
    // Support both multipart upload events and raw octet-stream bodies.
    // First, handle raw body mode where the POST contains the chunk in server.arg("plain").
    if (server.hasArg("plain")) {
      // Raw octet-stream body mode. Use offset-based writes to assemble file.
      String body = server.arg("plain");
      if (body.length() == 0) {
        sendJsonStatus(400, "error", "Empty body");
        return;
      }

      if (!ensureSdReady()) {
        return;
      }

      String folderName;
      if (!resolveFolderName(extractParam("folder"), folderName)) {
        sendJsonStatus(400, "error", "Invalid or missing folder parameter");
        return;
      }

      String safeName;
      if (!isValidFileName(extractParam("file"), safeName)) {
        sendJsonStatus(400, "error", "Invalid filename");
        return;
      }

      String folderPath = getFolderPath(folderName);
      if (!folderExists(folderName)) {
        sendJsonStatus(400, "error", "Selected folder does not exist");
        return;
      }

      String path = joinPath(folderPath, safeName);

      // Require an explicit offset param for raw chunk uploads
      String rawOffset = extractParam("offset");
      if (rawOffset.length() == 0) {
        sendJsonStatus(400, "error", "Missing offset");
        return;
      }
      long rawOffsetValue = rawOffset.toInt();
      if (rawOffsetValue < 0) {
        sendJsonStatus(400, "error", "Invalid offset");
        return;
      }

      long rawTotalSize = extractParam("totalSize").toInt();
      bool hasRawTotalSize = rawTotalSize > 0;
      size_t totalSize = hasRawTotalSize ? static_cast<size_t>(rawTotalSize) : 0;
      if (hasRawTotalSize && totalSize > MAX_UPLOAD_SIZE_BYTES) {
        sendJsonStatus(400, "error", "File too large. Max allowed size is 3MB");
        return;
      }

      // If offset == 0, start a fresh file (remove existing)
      if (rawOffsetValue == 0 && pathExistsWithRetry(path) && !removeFileWithRetry(path)) {
        sendJsonStatus(500, "error", "Failed to reset upload file");
        return;
      }

      // If offset > 0 ensure file exists and size matches offset
      if (rawOffsetValue > 0 && !pathExistsWithRetry(path)) {
        sendJsonStatus(400, "error", "Missing upload session");
        return;
      }

      // Use read-handle to determine existing size safely before opening for write
      File r = openFileWithRetry(path, FILE_READ);
      size_t existing = r ? static_cast<size_t>(r.size()) : 0;
      if (r) {
        r.close();
        delay(SD_IO_SETTLE_DELAY_MS);
      }

      if (rawOffsetValue > 0) {
        if (existing < static_cast<size_t>(rawOffsetValue)) {
          sendJsonStatus(400, "error", "Offset mismatch");
          return;
        } else if (existing > static_cast<size_t>(rawOffsetValue)) {
          // Duplicate chunk already present; skip writing and return success.
          Serial.printf("Raw duplicate chunk: expected offset=%u actual=%u path=%s - skipping write\n", (unsigned)rawOffsetValue, (unsigned)existing, path.c_str());
          sendJson(200, "{\"status\":\"ok\"}");
          return;
        }
      }

      String writeError;
      if (!writeFileAtOffset(path, static_cast<size_t>(rawOffsetValue), (const uint8_t*)body.c_str(), body.length(), writeError)) {
        Serial.printf("Raw buffered write failed: %s path=%s\n", writeError.c_str(), path.c_str());
        sendJsonStatus(500, "error", writeError.length() ? writeError : "Failed to write chunk");
        return;
      }

      File chk = openFileWithRetry(path, FILE_READ);
      size_t after = chk ? static_cast<size_t>(chk.size()) : 0;
      if (chk) {
        chk.close();
        delay(SD_IO_SETTLE_DELAY_MS);
      }
      size_t expectedAfter = static_cast<size_t>(rawOffsetValue) + body.length();
      Serial.printf("Raw bytes written=%u expectedAfter=%u fileSizeAfter=%u path=%s\n", (unsigned)body.length(), (unsigned)expectedAfter, (unsigned)after, path.c_str());
      if (after != expectedAfter) {
        sendJsonStatus(500, "error", "Post-write size mismatch");
        return;
      }

      if (hasRawTotalSize && expectedAfter == totalSize) {
        File finalCheck = openFileWithRetry(path, FILE_READ);
        size_t finalSize = finalCheck ? static_cast<size_t>(finalCheck.size()) : 0;
        if (finalCheck) {
          finalCheck.close();
          delay(SD_IO_SETTLE_DELAY_MS);
        }
        if (finalSize != totalSize) {
          sendJsonStatus(400, "error", "Final file size mismatch");
          return;
        }
      }

      libraryCacheAt = 0;
      totalFileCountCache = -1;
      sendJson(200, "{\"status\":\"ok\"}");
      return;
    }

    // Otherwise fall back to multipart upload events
    HTTPUpload& upload = server.upload();
    static File uploadFile;
    static String uploadPath;

    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("Raw Upload Start: %s\n", upload.filename.c_str());

      if (!ensureSdReady()) {
        Serial.println("SD card is not ready.");
        return; // ensureSdReady() already sent response
      }

      String folderParam = urlDecodeComponent(extractParam("folder"));
      String folderName;
      if (!isValidFolderName(folderParam, folderName)) {
        sendJsonStatus(400, "error", "Invalid or missing folder parameter");
        return;
      }

      // Ensure folder exists, create if not
      String folderPath = getFolderPath(folderName);
      if (!pathExistsWithRetry(folderPath)) {
        delay(10);
        if (folderExists(folderName)) {
          folderPath = getFolderPath(folderName);
        } else {
        if (!mkdirWithRetry(folderPath)) {
          sendJsonStatus(500, "error", "Failed to create folder on SD");
          return;
        }
        delay(SD_IO_SETTLE_DELAY_MS);
        }
      }

      String safeName;
      if (!isValidFileName(upload.filename, safeName)) {
        sendJsonStatus(400, "error", "Invalid filename");
        return;
      }

      uploadPath = joinPath(folderPath, safeName);

      // If a file with the same name exists, remove it to start fresh
      if (pathExistsWithRetry(uploadPath)) {
        removeFileWithRetry(uploadPath);
      }

      uploadFile = openFileWithRetry(uploadPath, FILE_WRITE);
      if (!uploadFile) {
        Serial.println("File open failed!");
        sendJsonStatus(500, "error", "Failed to create file on SD card.");
        return;
      }
      Serial.printf("Opened for writing: %s\n", uploadPath.c_str());
    }
    else if (upload.status == UPLOAD_FILE_WRITE) {
      if (!uploadFile) {
        Serial.println("File not open for writing!");
        sendJsonStatus(500, "error", "Server write file not open");
        return;
      }
      if (upload.buf == NULL || upload.currentSize == 0) {
        Serial.println("Received empty or null chunk.");
        return;
      }

      // Enforce max chunk size
      if (upload.currentSize > MAX_UPLOAD_CHUNK_BYTES) {
        Serial.printf("Chunk too large: %u bytes\n", (unsigned)upload.currentSize);
        sendJsonStatus(400, "error", "Chunk too large");
        if (uploadFile) {
          uploadFile.flush();
          delay(SD_IO_SETTLE_DELAY_MS);
          uploadFile.close();
          delay(SD_IO_SETTLE_DELAY_MS);
          uploadFile = File();
        }
        return;
      }

      String writeError;
      if (!writeFileBuffered(uploadFile, (const uint8_t*)upload.buf, upload.currentSize, writeError)) {
        Serial.printf("Write failed: %s\n", writeError.c_str());
        if (uploadFile) {
          uploadFile.flush();
          delay(SD_IO_SETTLE_DELAY_MS);
          uploadFile.close();
          delay(SD_IO_SETTLE_DELAY_MS);
          uploadFile = File();
        }
        sendJsonStatus(500, "error", writeError.length() ? writeError : "Failed to write chunk");
        return;
      }
      // Give the card a moment after the write
      delay(20);
    }
    else if (upload.status == UPLOAD_FILE_END) {
      if (uploadFile) {
        uploadFile.flush();
        delay(SD_IO_SETTLE_DELAY_MS);
        uploadFile.close();
        delay(SD_IO_SETTLE_DELAY_MS);
        Serial.printf("Raw Upload Complete: %s (%u bytes)\n", upload.filename.c_str(), upload.totalSize);
        uploadFile = File();
        if (!verifyFileAccessible(uploadPath)) {
          sendJsonStatus(500, "error", "Uploaded file is not accessible after write");
          uploadPath = "";
          return;
        }
        // Return success JSON
        libraryCacheAt = 0;
        totalFileCountCache = -1;
        sendJson(200, "{\"status\":\"success\"}");
      } else {
        Serial.println("Upload end received but no file is open.");
        sendJsonStatus(500, "error", "No file open");
      }
      uploadPath = "";
    }
    else if (upload.status == UPLOAD_FILE_ABORTED) {
      Serial.println("Upload aborted");
      if (uploadFile) {
        uploadFile.flush();
        delay(SD_IO_SETTLE_DELAY_MS);
        uploadFile.close();
        if (uploadPath.length() > 0 && pathExistsWithRetry(uploadPath)) {
          removeFileWithRetry(uploadPath);
          Serial.println("Partial upload file removed.");
        }
        uploadFile = File();
      }
      uploadPath = "";
    }
}

  // New: download handler implementation
  void handleDownload() {
    if (!ensureSdReady()) {
    return;
    }

    String folderName;
    if (!resolveFolderName(extractParam("folder"), folderName)) {
    sendJsonStatus(400, "error", "Invalid or missing folder");
    return;
    }

    String safeFileName;
    if (!isValidFileName(extractParam("file"), safeFileName)) {
    sendJsonStatus(400, "error", "Invalid or missing file");
    return;
    }

    String path = joinPath(getFolderPath(folderName), safeFileName);
    if (!pathExistsWithRetry(path)) {
    sendJsonStatus(404, "error", "File not found");
    return;
    }

    File f = openFileWithRetry(path, FILE_READ);
    if (!f) {
    sendJsonStatus(500, "error", "Failed to open file");
    return;
    }

    server.sendHeader("Cache-Control", "no-store");
    server.sendHeader("Content-Disposition", (String("attachment; filename=") + safeFileName).c_str());
    server.streamFile(f, "application/octet-stream");
    f.close();
  }

  // New: simple API that reuses handleList behavior but exposed at /list-files
  void handleListFiles() {
    if (!ensureSdReady()) return;

    String rawFolder = extractParam("folder");
    String folder = urlDecodeComponent(rawFolder);
    folder.trim();
    if (folder.equalsIgnoreCase("library") || folder.equalsIgnoreCase("all")) {
      unsigned long now = millis();
      if ((now - libraryCacheAt) < LIBRARY_CACHE_TTL_MS && libraryCacheJson.length() > 2) {
        // Serve from cache — no SD scan needed
        server.sendHeader("Cache-Control", "no-store");
        server.setContentLength(libraryCacheJson.length());
        server.send(200, "application/json", "");
        server.sendContent(libraryCacheJson);
        return;
      }

      // Aggregate files from all subject subfolders and return objects {folder,file}.
      // Stream directly via chunked transfer so we never hold the full JSON in RAM.
      File root = openFileWithRetry(CONTENT_ROOT, FILE_READ);
      if (!root || !root.isDirectory()) {
        if (root) root.close();
        libraryCacheJson = "[]";
        libraryCacheAt = millis();
        totalFileCountCache = 0;
        sendJsonArray(200, libraryCacheJson);
        return;
      }

      // --- Build into cache String one entry at a time (no bulk concat) ---
      // Reserve reasonable space; realloc in smaller steps to avoid OOM.
      libraryCacheJson = "";
      libraryCacheJson.reserve(512);
      libraryCacheJson = "[";
      bool firstEntry = true;
      int count = 0;

      server.sendHeader("Cache-Control", "no-store");
      server.setContentLength(CONTENT_LENGTH_UNKNOWN);
      server.send(200, "application/json", "");
      server.sendContent("[");

      File entry = root.openNextFile();
      while (entry) {
        if (entry.isDirectory()) {
          String name = entry.name();
          int slashIndex = name.lastIndexOf('/');
          String subpath = name;
          if (slashIndex >= 0) subpath = name.substring(slashIndex + 1);

          File sub = openFileWithRetry(joinPath(String(CONTENT_ROOT), subpath), FILE_READ);
          if (sub && sub.isDirectory()) {
            File e2 = sub.openNextFile();
            while (e2) {
              if (!e2.isDirectory()) {
                String fname = e2.name();
                int sidx = fname.lastIndexOf('/');
                if (sidx >= 0) fname = fname.substring(sidx + 1);
                if (!fname.endsWith(".part")) {
                  // Build this entry as a small local string
                  String item = (firstEntry ? "" : ",");
                  item += "{\"folder\":\"";
                  item += jsonEscape(subpath);
                  item += "\",\"file\":\"";
                  item += jsonEscape(fname);
                  item += "\",\"size\":" + String(e2.size());
                  item += "}";
                  firstEntry = false;
                  count++;

                  // Stream to client immediately (no buffering)
                  server.sendContent(item);

                  // Accumulate into cache
                  libraryCacheJson += item;
                }
              }
              e2.close();
              e2 = sub.openNextFile();
              yield();
            }
            sub.close();
          }
        }
        entry.close();
        entry = root.openNextFile();
        yield();
      }

      root.close();
      server.sendContent("]");
      server.sendContent(""); // signal end of chunked transfer

      libraryCacheJson += "]";
      totalFileCountCache = count;
      libraryCacheAt = millis();
      return;
    }

    // Default behavior: list files from a single folder
    handleList();
  }

  // New: recursive file counting helper
  int countFilesRecursive(File dir) {
    if (!dir || !dir.isDirectory()) return 0;
    int total = 0;
    File entry = dir.openNextFile();
    while (entry) {
    if (entry.isDirectory()) {
      String name = entry.name();
      int slashIndex = name.lastIndexOf('/');
      String subpath = name;
      if (slashIndex >= 0) subpath = name.substring(slashIndex + 1);
      File sub = openFileWithRetry(joinPath(String(CONTENT_ROOT), subpath), FILE_READ);
      if (sub) {
      total += countFilesRecursive(sub);
      sub.close();
      }
    } else {
      String name = entry.name();
      if (!name.endsWith(".part")) total++;
    }
    entry.close();
    entry = dir.openNextFile();
    yield();
    }
    return total;
  }

  // New: dashboard-data handler
  void handleDashboardData() {
  if (!ensureSdReady()) return;

  unsigned long now = millis();
  if ((now - libraryCacheAt) < LIBRARY_CACHE_TTL_MS && totalFileCountCache >= 0) {
    String body = "{\"totalFiles\":" + String(totalFileCountCache) + "}";
    sendJson(200, body);
    return;
  }

  // If cache is invalid or missing, do the slow scan once.
  File root = openFileWithRetry(CONTENT_ROOT, FILE_READ);
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    sendJson(200, "{\"totalFiles\":0}");
    return;
  }

  int total = countFilesRecursive(root);
  root.close();

  // Update file count cache as well
  totalFileCountCache = total;
  // If we're updating totalFileCount then maybe we shouldn't update libraryCacheAt?
  // Let's just update the count cache for this specific call.

  String body = "{\"totalFiles\":" + String(total) + "}";
  sendJson(200, body);
}
void handleDelete() {
  if (!ensureSdReady()) {
    return;
  }

  if (!requireFacultyIfProvided()) {
    return;
  }

  String folderName;
  if (!resolveFolderName(extractParam("folder"), folderName)) {
    sendJsonStatus(400, "error", "Invalid or missing folder");
    return;
  }

  String safeFileName;
  if (!isValidFileName(extractParam("file"), safeFileName)) {
    sendJsonStatus(400, "error", "Invalid or missing file");
    return;
  }

  String filePath = joinPath(getFolderPath(folderName), safeFileName);
  if (!pathExistsWithRetry(filePath)) {
    sendJsonStatus(404, "error", "File not found");
    return;
  }

  if (!removeFileWithRetry(filePath)) {
    sendJsonStatus(400, "error", "Failed to delete file");
    return;
  }

  libraryCacheAt = 0;
  totalFileCountCache = -1;
  sendJsonStatus(200, "success", "File deleted");
}

void handleDeleteFolder() {
  if (!ensureSdReady()) {
    return;
  }

  if (!requireFacultyIfProvided()) {
    return;
  }

  String folderName;
  if (!resolveFolderName(extractParam("folder"), folderName)) {
    sendJsonStatus(400, "error", "Invalid or missing folder");
    return;
  }

  String folderPath = getFolderPath(folderName);
  if (folderPath == CONTENT_ROOT || folderPath.length() <= String(CONTENT_ROOT).length()) {
    sendJsonStatus(400, "error", "Cannot delete content root");
    return;
  }

  if (!folderPath.startsWith(String(CONTENT_ROOT) + "/")) {
    sendJsonStatus(400, "error", "Invalid folder path");
    return;
  }

  if (!folderExists(folderName)) {
    sendJsonStatus(404, "error", "Folder not found");
    return;
  }

  if (!removePathRecursive(folderPath)) {
    sendJsonStatus(500, "error", "Failed to delete folder");
    return;
  }

  foldersCacheAt = 0;
  foldersCacheJson = "[]";
  sendJsonStatus(200, "success", "Folder deleted");
}

bool ensureSdReady() {
  // If SD was initialized at startup, accept that as ready; avoid probing SD repeatedly.
  if (sdReady) return true;
  sendJsonStatus(500, "error", "SD card not available");
  return false;
}

bool ensureContentRoot() {
  // Robust check for the content root. Do not reinitialize SD here.
  Serial.println("Checking /content...");

  // Small settle before probing to avoid transient false negatives.
  delay(10);

  // Try a couple times before giving up (helps with slightly unstable cards).
  const uint8_t attempts = 3;
  for (uint8_t a = 0; a < attempts; a++) {
    if (pathExistsWithRetry(CONTENT_ROOT)) {
      File root = openFileWithRetry(CONTENT_ROOT, FILE_READ);
      bool ok = root && root.isDirectory();
      if (root) root.close();
      if (ok) {
        Serial.println("/content: Exists and is directory");
        return true;
      }
      Serial.println("/content: Exists but not a directory");
      return false;
    }

    if (a == 0) {
      Serial.println("/content: Not found, attempting to create...");
    } else {
      Serial.printf("/content: Not found (attempt %u), retrying...\n", a + 1);
    }

    // Try to create the content root directory.
    if (mkdirWithRetry(CONTENT_ROOT)) {
      delay(SD_IO_SETTLE_DELAY_MS);
      if (pathExistsWithRetry(CONTENT_ROOT)) {
        Serial.println("/content: Creation successful");
        return true;
      }
      Serial.println("/content: Created but not visible yet");
    } else {
      Serial.println("/content: mkdir() failed");
    }

    delay(SD_IO_SETTLE_DELAY_MS);
  }

  Serial.println("/content: ensureContentRoot() failed after retries");
  return false;
}

bool tryReinitializeSd() {
  // Runtime reinitialization is disabled. Return current mount state only.
  return sdReady;
}

File openFileWithRetry(const String &path, const char *mode) {
  applyMosiCompensation();
  File file = SD.open(path, mode);
  if (!file) {
    delay(50);
    applyMosiCompensation();
    file = SD.open(path, mode);
  }
  return file;
}

bool pathExistsWithRetry(const String &path) {
  applyMosiCompensation();
  if (SD.exists(path)) {
    return true;
  }
  delay(SD_IO_SETTLE_DELAY_MS);
  applyMosiCompensation();
  return SD.exists(path);
}

bool openFileForAppendWithRetry(const String &path, File &file) {
  file = openFileWithRetry(path, FILE_APPEND);
  return static_cast<bool>(file);
}

bool openFileForWriteWithRetry(const String &path, File &file) {
  file = openFileWithRetry(path, FILE_WRITE);
  return static_cast<bool>(file);
}

bool openFileForReadWithRetry(const String &path, File &file) {
  file = openFileWithRetry(path, FILE_READ);
  return static_cast<bool>(file);
}

bool mkdirWithRetry(const String &path) {
  if (pathExistsWithRetry(path)) {
    return true;
  }
  
  applyMosiCompensation();
  if (SD.mkdir(path)) {
    delay(SD_IO_SETTLE_DELAY_MS);
    return true;
  }
  
  Serial.printf("mkdir: failed for %s, attempting recovery...\n", path.c_str());
  recoverSdCard();
  // Ensure we stay at write-safe speed if this is called during upload
  if (sdWriteInProgress) spi.setFrequency(SD_WRITE_SPI_FREQUENCY_HZ);
  applyMosiCompensation();
  delay(100);

  bool ok = SD.mkdir(path);
  if (ok) {
    delay(SD_IO_SETTLE_DELAY_MS);
  }
  return ok;
}

bool removeFileWithRetry(const String &path) {
  if (!pathExistsWithRetry(path)) {
    return true;
  }
  applyMosiCompensation();
  if (SD.remove(path)) {
    delay(SD_IO_SETTLE_DELAY_MS);
    return true;
  }
  
  Serial.printf("removeFile: failed for %s, recovering...\n", path.c_str());
  recoverSdCard();
  if (sdWriteInProgress) spi.setFrequency(SD_WRITE_SPI_FREQUENCY_HZ);
  applyMosiCompensation();
  delay(100);

  bool ok = SD.remove(path);
  if (ok) {
    delay(SD_IO_SETTLE_DELAY_MS);
  }
  return ok;
}

bool removeDirWithRetry(const String &path) {
  if (!pathExistsWithRetry(path)) {
    return true;
  }
  if (SD.rmdir(path)) {
    delay(SD_IO_SETTLE_DELAY_MS);
    return true;
  }
  delay(SD_IO_SETTLE_DELAY_MS);
  if (!pathExistsWithRetry(path)) {
    return true;
  }
  bool ok = SD.rmdir(path);
  if (ok) {
    delay(SD_IO_SETTLE_DELAY_MS);
  }
  return ok;
}

bool removePathRecursive(const String &path) {
  File dir = openFileWithRetry(path, FILE_READ);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
      delay(SD_IO_SETTLE_DELAY_MS);
    }
    return false;
  }

  File entry = dir.openNextFile();
  while (entry) {
    String entryPath = entry.name();
    bool isDir = entry.isDirectory();
    entry.close();
    delay(SD_IO_SETTLE_DELAY_MS);

    bool ok = isDir ? removePathRecursive(entryPath) : removeFileWithRetry(entryPath);
    if (!ok) {
      dir.close();
      delay(SD_IO_SETTLE_DELAY_MS);
      return false;
    }

    entry = dir.openNextFile();
    yield();
  }

  dir.close();
  delay(SD_IO_SETTLE_DELAY_MS);
  return removeDirWithRetry(path);
}

bool verifyFileAccessible(const String &path, size_t *sizeOut) {
  File file = openFileWithRetry(path, FILE_READ);
  if (!file || file.isDirectory()) {
    if (file) {
      file.close();
    }
    return false;
  }
  if (sizeOut != nullptr) {
    *sizeOut = static_cast<size_t>(file.size());
  }
  file.close();
  delay(SD_IO_SETTLE_DELAY_MS);
  return true;
}

bool writeFileAtOffset(const String &path, size_t offset, const uint8_t *data, size_t length, String &error) {
  if (offset == 0 && pathExistsWithRetry(path) && !removeFileWithRetry(path)) {
    error = "Failed to reset upload file";
    return false;
  }

  size_t beforeSize = 0;
  if (offset > 0) {
    File checkFile = openFileWithRetry(path, FILE_READ);
    beforeSize = checkFile ? static_cast<size_t>(checkFile.size()) : 0;
    if (checkFile) {
      checkFile.close();
      delay(SD_IO_SETTLE_DELAY_MS);
    }

    if (beforeSize != offset) {
      error = "Offset mismatch before append";
      return false;
    }
  }

  const char *mode = (offset == 0) ? FILE_WRITE : FILE_APPEND;
  File file = openFileWithRetry(path, mode);
  if (!file) {
    error = "File open failed";
    return false;
  }

  bool ok = writeFileBuffered(file, data, length, error);
  file.flush();
  delay(SD_IO_SETTLE_DELAY_MS);
  file.close();
  delay(SD_IO_SETTLE_DELAY_MS);

  if (!ok) {
    return false;
  }

  size_t afterSize = 0;
  size_t expectedSize = offset + length;
  if (!verifyFileAccessible(path, &afterSize) || afterSize != expectedSize) {
    delay(SD_IO_SETTLE_DELAY_MS * 2);
    if (!verifyFileAccessible(path, &afterSize) || afterSize != expectedSize) {
      error = "Post-write size mismatch";
      return false;
    }
  }

  return true;
}

bool writeUploadChunkBuffered(const uint8_t *data, size_t length, String &error) {
  size_t offset = 0;

  while (offset < length) {
    size_t remaining = length - offset;
    size_t sliceLen = (remaining > SD_WRITE_SLICE_BYTES) ? SD_WRITE_SLICE_BYTES : remaining;
    const uint8_t *slice = data + offset;

    size_t written = 0;
    // Try up to 3 times with escalating delays
    for (uint8_t attempt = 0; attempt < 3; attempt++) {
      written = uploadCtx.file.write(slice, sliceLen);
      if (written == sliceLen) break;
      Serial.printf("writeUploadChunkBuffered: retry %u requested=%u wrote=%u\n", attempt + 1, (unsigned)sliceLen, (unsigned)written);
      uploadCtx.file.flush();
      delay(SD_IO_SETTLE_DELAY_MS * (attempt + 1) * 2);
      yield();
    }
    if (written == sliceLen) {
      offset += written;
      yield();
      delay(SD_UPLOAD_CHUNK_DELAY_MS);
      continue;
    }

    error = (written > 0 && written < sliceLen) ? "SD write mismatch" : "Failed to write upload chunk";
    Serial.printf("writeUploadChunkBuffered: FAILED requested=%u wrote=%u\n", (unsigned)sliceLen, (unsigned)written);
    return false;
  }

  Serial.printf("writeUploadChunkBuffered: Wrote %u bytes for %s\n", (unsigned)length, uploadCtx.safeFileName.c_str());
  return true;
}

// Buffered write helper for arbitrary File references. Ensures full write in SD_WRITE_SLICE_BYTES slices.
bool writeFileBuffered(File &file, const uint8_t *data, size_t length, String &error) {
  if (!file) {
    error = "Invalid file handle";
    return false;
  }

  size_t offset = 0;
  while (offset < length) {
    size_t remaining = length - offset;
    size_t sliceLen = (remaining > SD_WRITE_SLICE_BYTES) ? SD_WRITE_SLICE_BYTES : remaining;
    const uint8_t *slice = data + offset;

    size_t written = file.write(slice, sliceLen);
    if (written != sliceLen) {
      delay(SD_IO_SETTLE_DELAY_MS);
      written = file.write(slice, sliceLen);
    }
    if (written == sliceLen) {
      offset += written;
      yield();
      delay(2); // Wait for card internal supply to stabilize
      continue;
    }

    error = (written > 0 && written < sliceLen) ? "SD write mismatch" : "Failed to write upload chunk";
    return false;
  }

  return true;
}

void stabilizeSdAfterUpload() {
  // Gentle settle after a successful upload — do NOT re-mount.
  // Re-mounting after success destabilizes the card for the next write.
  yield();
  delay(SD_IO_SETTLE_DELAY_MS * 4);
}

void recoverSdCard() {
  // Full re-mount to recover from FAT cache corruption after a failed write.
  // Only call this after upload failures, never after success.
  Serial.println("recoverSdCard: starting recovery");
  yield();
  delay(50);

  // 1. Close any leftover file handles
  if (uploadCtx.file) {
    uploadCtx.file.close();
    uploadCtx.file = File();
  }
  if (chunkCtx.file) {
    chunkCtx.file.close();
    chunkCtx.file = File();
  }
  delay(50);

  // 2. End SD library session and SPI
  SD.end();
  spi.end();   // Release SPI peripheral so we can bit-bang
  delay(100);

  // 3. Bit-bang 80 idle clocks with CS HIGH (guaranteed by direct GPIO control).
  //    Unlike spi.transfer(), this does NOT auto-assert CS via hardware.
  bitBangIdleClocks();

  // 4. Wait for card's internal error recovery (1+ second for write-abort recovery)
  delay(1500);

  // 5. Re-initialize SPI and SD
  spi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  spi.setFrequency(SD_SPI_FREQUENCY_HZ);
  applyMosiCompensation();
  delay(100);

  bool ok = SD.begin(SD_CS, spi);
  if (ok) applyMosiCompensation();
  if (!ok) {
    Serial.println("recoverSdCard: first attempt failed, retrying...");
    delay(1000);
    spi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    spi.setFrequency(SD_SPI_FREQUENCY_HZ);
    applyMosiCompensation();
    ok = SD.begin(SD_CS, spi);
    if (ok) applyMosiCompensation();
  }
  if (!ok) {
    Serial.println("recoverSdCard: second attempt failed, trying default SPI...");
    delay(1000);
    ok = SD.begin(SD_CS);
    if (ok) applyMosiCompensation();
  }
  setSdReady(ok);
  if (ok) {
    Serial.println("SD recovered after re-mount");
  } else {
    Serial.println("WARNING: SD re-mount failed");
  }
}

bool requireFacultyIfProvided() {
  String role = getRole();
  if (role == "faculty") {
    return true;
  }

  sendJsonStatus(403, "error", "Only faculty can perform this action");
  return false;
}

bool facultyWriteAllowed(String &error) {
  String role = getRole();
  if (role == "faculty") {
    return true;
  }

  error = "Only faculty can perform this action";
  return false;
}

String getRole() {
  String role = trimCopy(extractParam("role"));
  if (role.length() == 0 && server.hasHeader("X-User-Role")) {
    role = trimCopy(server.header("X-User-Role"));
  }
  if (role.length() == 0 && server.hasHeader("X-Role")) {
    role = trimCopy(server.header("X-Role"));
  }
  role.toLowerCase();
  return role;
}

String urlDecodeComponent(const String &value) {
  String decoded;
  decoded.reserve(value.length());

  for (size_t i = 0; i < value.length(); i++) {
    char c = value.charAt(i);

    if (c == '+') {
      decoded += ' ';
      continue;
    }

    if (c == '%' && (i + 2) < value.length()) {
      char hi = value.charAt(i + 1);
      char lo = value.charAt(i + 2);
      if (isxdigit(static_cast<unsigned char>(hi)) && isxdigit(static_cast<unsigned char>(lo))) {
        uint8_t hiVal = (hi >= 'a') ? (hi - 'a' + 10) : (hi >= 'A') ? (hi - 'A' + 10) : (hi - '0');
        uint8_t loVal = (lo >= 'a') ? (lo - 'a' + 10) : (lo >= 'A') ? (lo - 'A' + 10) : (lo - '0');
        decoded += static_cast<char>((hiVal << 4) | loVal);
        i += 2;
        continue;
      }
    }

    decoded += c;
  }

  return decoded;
}

bool isValidFolderName(const String &name, String &trimmed) {
  trimmed = trimCopy(name);
  if (trimmed.isEmpty() || trimmed.length() > MAX_FOLDER_NAME_LEN) {
    return false;
  }
  if (trimmed == "." || trimmed == "..") {
    return false;
  }

  for (size_t i = 0; i < trimmed.length(); i++) {
    char c = trimmed.charAt(i);
    // Added '.' to allowed characters for more flexible subject names
    bool safe = isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '_' || c == '-' || c == '.';
    if (!safe) {
      return false;
    }
  }

  return true;
}

bool isValidFileName(const String &fileName, String &safeName) {
  safeName = fileName;
  safeName.replace("\\", "/");

  int slashIndex = safeName.lastIndexOf('/');
  if (slashIndex >= 0) {
    safeName = safeName.substring(slashIndex + 1);
  }

  safeName = trimCopy(safeName);
  if (safeName.isEmpty() || safeName.length() > MAX_FILE_NAME_LEN) {
    return false;
  }
  if (safeName.startsWith(".")) {
    return false;
  }

  for (size_t i = 0; i < safeName.length(); i++) {
    char c = safeName.charAt(i);
    bool safe = isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '_' || c == '-' || c == '.';
    if (!safe) {
      return false;
    }
  }

  return categoryFromFileName(safeName) != FILE_CAT_INVALID;
}

String trimCopy(const String &value) {
  String trimmed = value;
  trimmed.trim();
  return trimmed;
}

String getFolderPath(const String &folderName) {
  return joinPath(CONTENT_ROOT, folderName);
}

bool folderExists(const String &folderName) {
  String folderPath = getFolderPath(folderName);
  if (!pathExistsWithRetry(folderPath)) {
    delay(10);
    if (!pathExistsWithRetry(folderPath)) {
      return false;
    }
  }

  File dir = openFileWithRetry(folderPath, FILE_READ);
  bool ok = dir && dir.isDirectory();
  if (dir) {
    dir.close();
    delay(SD_IO_SETTLE_DELAY_MS);
  }
  return ok;
}

bool findFolderCaseInsensitive(const String &folderName, String &matchedName) {
  matchedName = "";

  File root = openFileWithRetry(CONTENT_ROOT, FILE_READ);
  if (!root || !root.isDirectory()) {
    if (root) {
      root.close();
    }
    return false;
  }

  File entry = root.openNextFile();
  while (entry) {
    if (entry.isDirectory()) {
      String name = entry.name();
      int slashIndex = name.lastIndexOf('/');
      if (slashIndex >= 0) {
        name = name.substring(slashIndex + 1);
      }

      if (name.equalsIgnoreCase(folderName)) {
        matchedName = name;
        entry.close();
        root.close();
        delay(SD_IO_SETTLE_DELAY_MS);
        return true;
      }
    }

    entry.close();
    entry = root.openNextFile();
    yield();
  }

  root.close();
  delay(SD_IO_SETTLE_DELAY_MS);
  return false;
}

bool resolveFolderName(const String &rawName, String &resolvedName) {
  String decoded = urlDecodeComponent(rawName);
  String normalized;
  if (!isValidFolderName(decoded, normalized)) {
    return false;
  }

  if (folderExists(normalized)) {
    resolvedName = normalized;
    return true;
  }

  return findFolderCaseInsensitive(normalized, resolvedName);
}

size_t countFolders() {
  File root = openFileWithRetry(CONTENT_ROOT, FILE_READ);
  if (!root || !root.isDirectory()) {
    if (root) {
      root.close();
    }
    return 0;
  }

  size_t count = 0;
  File entry = root.openNextFile();
  while (entry) {
    if (entry.isDirectory()) {
      count++;
    }
    entry.close();
    entry = root.openNextFile();
    yield();
  }
  root.close();
  return count;
}

size_t countFilesInFolder(const String &folderName) {
  File dir = openFileWithRetry(getFolderPath(folderName), FILE_READ);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return 0;
  }

  size_t count = 0;
  File entry = dir.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      String name = entry.name();
      if (!name.endsWith(".part")) {
        count++;
      }
    }
    entry.close();
    entry = dir.openNextFile();
    yield();
  }
  dir.close();
  return count;
}

FileCategory categoryFromFileName(const String &fileName) {
  String lower = fileName;
  lower.toLowerCase();

  if (lower.endsWith(".pdf")) {
    return FILE_CAT_PDF;
  }
  if (lower.endsWith(".doc") || lower.endsWith(".docx")) {
    return FILE_CAT_WORD;
  }
  if (lower.endsWith(".mp3") || lower.endsWith(".wav")) {
    return FILE_CAT_AUDIO;
  }
  if (lower.endsWith(".html") || lower.endsWith(".htm")) {
    return FILE_CAT_WEB;
  }
  if (lower.endsWith(".pptx") || lower.endsWith(".ppt")) {
    return FILE_CAT_PRESENTATION;
  }
  if (lower.endsWith(".mp4")) {
    return FILE_CAT_VIDEO;
  }
  return FILE_CAT_INVALID;
}

size_t countCategoryInFolder(const String &folderName, FileCategory category) {
  if (category == FILE_CAT_INVALID) {
    return 0;
  }

  File dir = openFileWithRetry(getFolderPath(folderName), FILE_READ);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return 0;
  }

  size_t count = 0;
  File entry = dir.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      String name = entry.name();
      int slashIndex = name.lastIndexOf('/');
      if (slashIndex >= 0) {
        name = name.substring(slashIndex + 1);
      }

      if (!name.endsWith(".part") && categoryFromFileName(name) == category) {
        count++;
      }
    }
    entry.close();
    entry = dir.openNextFile();
    yield();
  }
  dir.close();
  return count;
}

bool folderHasCategory(const String &folderName, FileCategory category) {
  File dir = openFileWithRetry(getFolderPath(folderName), FILE_READ);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return false;
  }

  bool found = false;
  File entry = dir.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      String name = entry.name();
      int slashIndex = name.lastIndexOf('/');
      if (slashIndex >= 0) {
        name = name.substring(slashIndex + 1);
      }

      if (!name.endsWith(".part") && categoryFromFileName(name) == category) {
        found = true;
        entry.close();
        break;
      }
    }

    entry.close();
    entry = dir.openNextFile();
    yield();
  }

  dir.close();
  return found;
}

String categoryLabel(FileCategory category) {
  switch (category) {
    case FILE_CAT_PDF:
      return "PDF";
    case FILE_CAT_WORD:
      return "Word";
    default:
      return "file";
  }
}

// Simple content-type helper (kept for compatibility)
String getContentType(String filename) {
  if (filename.endsWith(".html")) return "text/html";
  if (filename.endsWith(".css")) return "text/css";
  if (filename.endsWith(".js")) return "application/javascript";
  if (filename.endsWith(".pdf")) return "application/pdf";
  return "text/plain";
}

String getMimeType(const String &path) {
  String lower = path;
  lower.toLowerCase();

  if (lower.endsWith(".html")) return "text/html";
  if (lower.endsWith(".js")) return "application/javascript";
  if (lower.endsWith(".css")) return "text/css";
  if (lower.endsWith(".json")) return "application/json";
  if (lower.endsWith(".png")) return "image/png";
  if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) return "image/jpeg";
  if (lower.endsWith(".pdf")) return "application/pdf";
  if (lower.endsWith(".doc")) return "application/msword";
  if (lower.endsWith(".docx")) return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
  if (lower.endsWith(".mp3")) return "audio/mpeg";
  if (lower.endsWith(".wav")) return "audio/wav";
  if (lower.endsWith(".mp4")) return "video/mp4";
  if (lower.endsWith(".pptx")) return "application/vnd.openxmlformats-officedocument.presentationml.presentation";
  return "application/octet-stream";
}

String jsonEscape(const String &value) {
  String escaped;
  escaped.reserve(value.length() + 8);

  for (size_t i = 0; i < value.length(); i++) {
    char c = value.charAt(i);
    switch (c) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped += c;
        break;
    }
  }

  return escaped;
}

String joinPath(const String &left, const String &right) {
  if (left.endsWith("/")) {
    return left + right;
  }
  return left + "/" + right;
}

void sendJson(int code, const String &body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}

void sendJsonStatus(int code, const char *status, const String &message) {
  sendJson(code, "{\"status\":\"" + String(status) + "\",\"message\":\"" + jsonEscape(message) + "\"}");
}

void sendJsonArray(int code, const String &json) {
  sendJson(code, json);
}

String extractParam(const char *name) {
  if (server.hasArg(name)) {
    return server.arg(name);
  }
  return "";
}

void cleanupUploadTempFile() {
  if (uploadCtx.file) {
    uploadCtx.file.close();
    uploadCtx.file = File();
    delay(SD_IO_SETTLE_DELAY_MS);
  }

  if (uploadCtx.tempPath.length() == 0) {
    return;
  }

  // Do not attempt to reinitialize SD here; only remove if currently available.
  if (sdReady && pathExistsWithRetry(uploadCtx.tempPath)) {
    removeFileWithRetry(uploadCtx.tempPath);
  }
}

// Close chunk write file if open
void closeChunkFile() {
  if (chunkCtx.file) {
    Serial.printf("Closing chunk file: %s\n", chunkCtx.tempPath.c_str());
    chunkCtx.file.flush();
    delay(SD_IO_SETTLE_DELAY_MS);
    chunkCtx.file.close();
    chunkCtx.file = File();
    delay(5);
  }
}
  