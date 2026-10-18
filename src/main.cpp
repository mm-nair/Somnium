#include <Arduino.h>

#include <Somnium.h>
#include <SomniumApp.h>

void setup() {
  const bool cameraOk = Somnium::begin();
  Somnium::App::Runtime::begin(cameraOk);
}

void loop() { Somnium::App::Runtime::tick(); }
