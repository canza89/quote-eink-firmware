#ifndef FIREBASE_CLIENT_H
#define FIREBASE_CLIENT_H

#include <Arduino.h>

// Ensure Firebase auth token is valid; logs in again if needed.
bool ensureFirebaseAuth();

// Fetch a random quote for the current logged-in user and display it.
void fetchAndDisplayQuote();

#endif
