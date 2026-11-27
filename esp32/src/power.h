// Deep sleep on the off face, and wake on motion (MMA8452 INT1) or touch.
#pragma once

#include <Arduino.h>

enum class WakeReason { PowerOn, Motion, Touch, Timer, Other };

WakeReason wakeReason();
const char* wakeReasonName(WakeReason r);

// True while a computer is connected to the native USB port.
bool usbHostConnected();

// Screen off, WiFi off, amp off, arm the wake sources (and a timer before the
// next alarm), deep sleep. Does not return:
// the cube restarts from setup() when it wakes.
[[noreturn]] void goToSleep();
