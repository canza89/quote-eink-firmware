#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include <heltec-eink-modules.h>
#include "secrets.h"

// Global display object (defined in display_manager.cpp)
extern EInkDisplay_VisionMasterE290 display;

// Initialize E-Ink display (landscape, cleared)
void displayInit();

// Generic status screen (small text, top-left) with version badge
void displayStatus(const String &msg);

// Show error message in a consistent way
void displayError(const String &msg);

// Render a nicely formatted quote + author + tags
void displayQuote(const String &text,
                  const String &author,
                  const String &tagsLine);

#endif
