#include "power.h"

#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "alarms.h"
#include "config.h"
#include "display.h"
#include "net.h"
#include "orientation.h"
#include "speaker.h"
#include "touch.h"

WakeReason wakeReason() {
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_UNDEFINED: return WakeReason::PowerOn;
    case ESP_SLEEP_WAKEUP_EXT1: return WakeReason::Motion;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: return WakeReason::Touch;
    case ESP_SLEEP_WAKEUP_TIMER: return WakeReason::Timer;
    default: return WakeReason::Other;
  }
}

const char* wakeReasonName(WakeReason r) {
  switch (r) {
    case WakeReason::PowerOn: return "power on";
    case WakeReason::Motion: return "motion";
    case WakeReason::Touch: return "touch";
    case WakeReason::Timer: return "timer (alarm)";
    default: return "other";
  }
}

bool usbHostConnected() {
#if ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT
  return HWCDC::isPlugged();
#else
  return false;
#endif
}

void goToSleep() {
  Serial.println("[sleep] deep sleep: tilt the cube or touch the strip to wake");
  // Wake ALARM_PREWAKE_S before the next alarm, to re-sync the time and ring.
  time_t wakeAt = schedule.sleepWakeAt();
  int64_t wakeIn = wakeAt ? (int64_t)(wakeAt - nowEpoch()) : 0;
  if (wakeAt) {
    if (wakeIn < 10) wakeIn = 10;
    Serial.printf("[sleep] timer wake at %s (alarm at %s)\n", hhmm(nowEpoch() + wakeIn).c_str(),
                  hhmm(schedule.nextAlarm()).c_str());
  }
  Serial.flush();
  display.off();
  net.shutdown();
  speaker.shutdown();

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  if (wakeAt) esp_sleep_enable_timer_wakeup((uint64_t)wakeIn * 1000000ULL);
  // RTC peripherals stay powered for the INT1 pull-down and the touch sensor.
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

  if (orientation.present()) {
    orientation.armMotionWake();
    gpio_num_t pin = (gpio_num_t)PIN_ACCEL_INT;
    rtc_gpio_init(pin);
    rtc_gpio_set_direction(pin, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_dis(pin);
    rtc_gpio_pulldown_en(pin);  // INT1 not wired: stays low, no false wakes
    esp_sleep_enable_ext1_wakeup(1ULL << PIN_ACCEL_INT, ESP_EXT1_WAKEUP_ANY_HIGH);
  }

  // Wake when the reading rises by the same margin that counts as a touch.
  uint32_t delta = touch.baseline() * (TOUCH_ON_FACTOR - 1.0f);
  touchSleepWakeUpEnable(PIN_TOUCH, delta > 0 ? delta : 1);

  esp_deep_sleep_start();
}
