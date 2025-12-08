#include "display_manager.h"

// Global display instance
EInkDisplay_VisionMasterE290 display;

// Draw small version text (e.g. "v1.0.3") bottom-right
static void drawVersionBadge() {
  String versionStr = "v";
  versionStr += FW_VERSION;     // e.g. FW_VERSION "1.0.3" -> "v1.0.3"

  display.setTextSize(1);

  int screenWidth  = display.width();
  int screenHeight = display.height();

  int charWidth    = 6;         // approx built-in font width at textSize=1
  int lineWidthPx  = versionStr.length() * charWidth;

  int marginX = 4;
  int marginY = 8;

  int x = screenWidth  - lineWidthPx - marginX;
  int y = screenHeight - marginY;  // bottom area

  display.setCursor(x, y);
  display.println(versionStr);
}

void displayInit() {
  display.begin();
  display.setRotation(1); // landscape
  display.clearMemory();
  display.update();
}

// Generic status screen (small text, top-left) + version badge
void displayStatus(const String &msg) {
  Serial.println("[DISPLAY] " + msg);
  display.clearMemory();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.println(msg);

  // Version badge in corner
  drawVersionBadge();

  display.update();
}

void displayError(const String& msg) {
  displayStatus("Error:\n" + msg);
}

// Wrap + center multiline text.
// Uses display.width() so it respects rotation.
static void printWrappedCentered(const String &text,
                                 int maxCharsPerLine,
                                 int &cursorY,
                                 int lineHeight,
                                 uint8_t textSize) {
  display.setTextSize(textSize);

  int len = text.length();
  int start = 0;
  int screenWidthPx = display.width();

  while (start < len) {
    int end = start + maxCharsPerLine;
    if (end > len) end = len;

    int breakPos = end;

    if (end < len) {
      breakPos = text.lastIndexOf(' ', end);
      if (breakPos <= start) {
        breakPos = end;
      }
    }

    String line = text.substring(start, breakPos);
    line.trim();

    if (line.length() > 0) {
      int lineLen    = line.length();
      int charWidth  = 6 * textSize;  // approx built-in font width
      int lineWidthPx = lineLen * charWidth;

      int startX = 0;
      if (lineWidthPx < screenWidthPx) {
        startX = (screenWidthPx - lineWidthPx) / 2;
      }

      display.setCursor(startX, cursorY);
      display.println(line);
      cursorY += lineHeight;
    }

    start = breakPos;
    while (start < len && text[start] == ' ') start++;
  }
}

// Main formatted quote renderer in landscape
void displayQuote(const String& text,
                  const String& author,
                  const String& tagsLine) {
  Serial.println("[DISPLAY] Rendering formatted quote...");
  display.clearMemory();

  int screenWidth  = display.width();
  int screenHeight = display.height();

  int y = 6;  // top padding

  // 1) Quote text (big, centered, wrapped)
  const int maxQuoteChars   = 24;   // for textSize=2
  const int quoteLineHeight = 20;
  printWrappedCentered(text, maxQuoteChars, y, quoteLineHeight, 2);

  // Extra spacing before author
  y += 4;

  // 2) Author (small, centered)
  if (author.length() > 0) {
    String authorLine = "- " + author;

    const int authorTextSize   = 1;
    const int authorLineHeight = 12;
    const int maxAuthorChars   = 36;

    if (authorLine.length() > maxAuthorChars) {
      authorLine = authorLine.substring(0, maxAuthorChars);
    }

    display.setTextSize(authorTextSize);

    int lineLen     = authorLine.length();
    int charWidth   = 6 * authorTextSize;
    int lineWidthPx = lineLen * charWidth;
    int startX      = 0;
    if (lineWidthPx < screenWidth) {
      startX = (screenWidth - lineWidthPx) / 2;
    }

    display.setCursor(startX, y);
    display.println(authorLine);
    y += authorLineHeight;
  }

  // 3) Tags line (bottom, small, centered)
  if (tagsLine.length() > 0) {
    const int tagsTextSize = 1;
    display.setTextSize(tagsTextSize);

    String t = tagsLine;
    const int maxTagChars = 45;
    if (t.length() > maxTagChars) {
      t = t.substring(0, maxTagChars - 3) + "...";
    }

    int lineLen     = t.length();
    int charWidth   = 6 * tagsTextSize;
    int lineWidthPx = lineLen * charWidth;
    int yTags       = screenHeight - 14;

    int startX = 0;
    if (lineWidthPx < screenWidth) {
      startX = (screenWidth - lineWidthPx) / 2;
    }

    display.setCursor(startX, yTags);
    display.println(t);
  }

  // Version badge bottom-right
  drawVersionBadge();

  display.update();
}
