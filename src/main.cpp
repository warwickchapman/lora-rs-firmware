#include <Arduino.h>

#include "app.h"

App app;

void setup() {
  Serial.begin(115200);
  delay(200);
  app.begin();
}

void loop() {
  app.tick();
}
