#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <LittleFS.h>
#include <string.h>

// ESP32-C3 default I2C pins (adjust to match your wiring)
#define I2C_SDA 8
#define I2C_SCL 9

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

#define MAX_FRAMES 16
#define FRAME_DELAY_MS 50
#define MAX_FOLDERS 16
#define EMOTIONS_DIR "/emotions"

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

char frameNames[MAX_FRAMES][64];
int frameCount = 0;

char folderNames[MAX_FOLDERS][64];
int folderCount = 0;

char emotionNames[MAX_FOLDERS][64];
int emotionCount = 0;

uint16_t readLE16(File &f) {
  uint16_t lsb = f.read();
  uint16_t msb = f.read();
  return (msb << 8) | lsb;
}

uint32_t readLE32(File &f) {
  uint32_t b0 = f.read();
  uint32_t b1 = f.read();
  uint32_t b2 = f.read();
  uint32_t b3 = f.read();
  return (b3 << 24) | (b2 << 16) | (b1 << 8) | b0;
}

// Reads a 1-bit, 8-bit paletted, or 24-bit uncompressed BMP from LittleFS and draws it as black/white on the OLED.
void drawBmp(const char *path) {
  File bmpFile = LittleFS.open(path, "r");
  if (!bmpFile) {
    Serial.println(F("Failed to open BMP file"));
    return;
  }

  if (readLE16(bmpFile) != 0x4D42) { // "BM"
    Serial.print(F("Not a BMP file "));
    Serial.println(path);
    bmpFile.close();
    return;
  }

  bmpFile.seek(10);
  uint32_t dataOffset = readLE32(bmpFile);

  bmpFile.seek(14);
  uint32_t headerSize = readLE32(bmpFile);

  bmpFile.seek(18);
  int32_t bmpWidth = (int32_t)readLE32(bmpFile);
  int32_t bmpHeight = (int32_t)readLE32(bmpFile);

  bmpFile.seek(28);
  uint16_t bpp = readLE16(bmpFile);
  uint32_t compression = readLE32(bmpFile);

  if (compression != 0 || (bpp != 1 && bpp != 8 && bpp != 24)) {
    Serial.println(F("Unsupported BMP format (need 1-bit, 8-bit, or 24-bit uncompressed)"));
    bmpFile.close();
    return;
  }

  // For paletted BMPs, read each palette entry's luminance so pixel indices can be thresholded.
  uint8_t paletteLuminance[256];
  if (bpp == 1 || bpp == 8) {
    bmpFile.seek(46);
    uint32_t clrUsed = readLE32(bmpFile);
    uint32_t colorCount = clrUsed != 0 ? clrUsed : (1u << bpp);

    bmpFile.seek(14 + headerSize);
    for (uint32_t i = 0; i < colorCount && i < 256; i++) {
      uint8_t b = bmpFile.read();
      uint8_t g = bmpFile.read();
      uint8_t r = bmpFile.read();
      bmpFile.read(); // reserved
      paletteLuminance[i] = (uint8_t)((r * 299 + g * 587 + b * 114) / 1000);
    }
  }

  bool bottomUp = bmpHeight > 0;
  uint32_t height = bottomUp ? (uint32_t)bmpHeight : (uint32_t)(-bmpHeight);
  uint32_t width = (uint32_t)bmpWidth;
  uint32_t rowSize = ((width * bpp + 31) / 32) * 4;

  uint8_t *rowBuf = (uint8_t *)malloc(rowSize);
  if (!rowBuf) {
    Serial.println(F("Not enough memory to read BMP row"));
    bmpFile.close();
    return;
  }

  display.clearDisplay();

  for (uint32_t row = 0; row < height && row < SCREEN_HEIGHT; row++) {
    uint32_t srcRow = bottomUp ? (height - 1 - row) : row;
    bmpFile.seek(dataOffset + srcRow * rowSize);
    bmpFile.read(rowBuf, rowSize);

    for (uint32_t col = 0; col < width && col < SCREEN_WIDTH; col++) {
      uint16_t luminance;
      if (bpp == 1) {
        uint8_t index = (rowBuf[col / 8] >> (7 - (col % 8))) & 0x01;
        luminance = paletteLuminance[index];
      } else if (bpp == 8) {
        luminance = paletteLuminance[rowBuf[col]];
      } else {
        uint8_t b = rowBuf[col * 3 + 0];
        uint8_t g = rowBuf[col * 3 + 1];
        uint8_t r = rowBuf[col * 3 + 2];
        luminance = (r * 299 + g * 587 + b * 114) / 1000;
      }
      display.drawPixel(col, row, luminance > 128 ? SSD1306_WHITE : SSD1306_BLACK);
    }
  }

  free(rowBuf);
  bmpFile.close();
  display.display();
}

// Builds an alphabetically sorted list of BMP file paths found in folderPath.
void loadFrameList(const char *folderPath) {
  frameCount = 0;

  File dir = LittleFS.open(folderPath);
  if (!dir || !dir.isDirectory()) {
    Serial.println(F("Failed to open image folder"));
    return;
  }

  File file = dir.openNextFile();
  while (file && frameCount < MAX_FRAMES) {
    if (!file.isDirectory()) {
      const char *name = file.name();
      // Some cores return the full path, others just the filename.
      if (strchr(name, '/')) {
        strncpy(frameNames[frameCount], name, sizeof(frameNames[frameCount]) - 1);
      } else {
        snprintf(frameNames[frameCount], sizeof(frameNames[frameCount]), "%s/%s", folderPath, name);
      }
      frameNames[frameCount][sizeof(frameNames[frameCount]) - 1] = '\0';
      frameCount++;
    }
    file = dir.openNextFile();
  }

  for (int i = 1; i < frameCount; i++) {
    char key[64];
    strcpy(key, frameNames[i]);
    int j = i - 1;
    while (j >= 0 && strcmp(frameNames[j], key) > 0) {
      strcpy(frameNames[j + 1], frameNames[j]);
      j--;
    }
    strcpy(frameNames[j + 1], key);
  }
}

// Builds the list of subdirectories directly under parentPath into list, returning how many were found.
int loadSubfolderList(const char *parentPath, char list[][64], int maxCount) {
  int count = 0;

  File parent = LittleFS.open(parentPath);
  if (!parent || !parent.isDirectory()) {
    Serial.println(F("Failed to open folder"));
    return 0;
  }

  bool parentIsRoot = strcmp(parentPath, "/") == 0;
  File entry = parent.openNextFile();
  while (entry && count < maxCount) {
    if (entry.isDirectory()) {
      const char *name = entry.name();
      if (name[0] == '/') {
        strncpy(list[count], name, sizeof(list[count]) - 1);
      } else if (parentIsRoot) {
        snprintf(list[count], sizeof(list[count]), "/%s", name);
      } else {
        snprintf(list[count], sizeof(list[count]), "%s/%s", parentPath, name);
      }
      list[count][sizeof(list[count]) - 1] = '\0';
      count++;
    }
    entry = parent.openNextFile();
  }

  return count;
}

// Builds the list of top-level frame folders found in LittleFS's root (excluding the emotions container).
void loadFolderList() {
  folderCount = loadSubfolderList("/", folderNames, MAX_FOLDERS);

  for (int i = 0; i < folderCount; i++) {
    if (strcmp(folderNames[i], EMOTIONS_DIR) == 0) {
      for (int j = i; j < folderCount - 1; j++) {
        strcpy(folderNames[j], folderNames[j + 1]);
      }
      folderCount--;
      break;
    }
  }
}

// Builds the list of emotion folders found under EMOTIONS_DIR.
void loadEmotionList() {
  emotionCount = loadSubfolderList(EMOTIONS_DIR, emotionNames, MAX_FOLDERS);
}

// Returns a random frame folder from the list built by loadFolderList(), or nullptr if none exist.
const char *getRandomFolder() {
  if (folderCount == 0) {
    return nullptr;
  }
  return folderNames[random(0, folderCount)];
}

// Returns a random emotion folder from the list built by loadEmotionList(), or nullptr if none exist.
const char *getRandomEmotion() {
  if (emotionCount == 0) {
    return nullptr;
  }
  return emotionNames[random(0, emotionCount)];
}

// Loads and draws every BMP frame in folderPath, in alphabetical order, once.
void runFrames(const char *folderPath) {
  loadFrameList(folderPath);

  for (int i = 0; i < frameCount; i++) {
    drawBmp(frameNames[i]);
    delay(FRAME_DELAY_MS);
  }
}

volatile bool drawingInProgress = false;
char drawFramesTaskFolder[64];

void drawFramesTask(void *pvParameters) {
  runFrames((const char *)pvParameters);
  drawingInProgress = false;
  vTaskDelete(NULL);
}

// Returns true while a background frame animation started by drawFrames() is still running.
bool isDrawingFrames() {
  return drawingInProgress;
}

// Kicks off runFrames(folderPath) on a background task. If an animation is already running, this call is skipped.
void drawFrames(const char *folderPath) {
  if (drawingInProgress) {
    Serial.println(F("Skipping drawFrames: previous animation still running"));
    return;
  }

  drawingInProgress = true;
  strncpy(drawFramesTaskFolder, folderPath, sizeof(drawFramesTaskFolder) - 1);
  drawFramesTaskFolder[sizeof(drawFramesTaskFolder) - 1] = '\0';

  xTaskCreatePinnedToCore(drawFramesTask, "DrawFrames", 8192, drawFramesTaskFolder, 1, NULL, 1);
  Serial.print("Started drawFrames task: ");
  Serial.println(drawFramesTaskFolder);
}

void setup() {
  Serial.begin(115200);

  Wire.begin(I2C_SDA, I2C_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    while (true) {
      delay(1000);
    }
  }

  if (!LittleFS.begin(true)) {
    Serial.println(F("LittleFS mount failed"));
    return;
  }

  loadFolderList();
  loadEmotionList();
}

void loop() {
  const char *folder = getRandomFolder();
  if (folder) {
    drawFrames(folder);
    delay(random(100, 6000)); // Random delay between .1 and 6 seconds
  }


  const char *emotion = getRandomEmotion();
  if (emotion) {
    drawFrames(emotion);
    delay(random(100, 6000)); // Random delay between .1 and 6 seconds
  }
}