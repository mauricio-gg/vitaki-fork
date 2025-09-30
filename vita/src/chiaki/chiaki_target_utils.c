#include <stdbool.h>

#include "chiaki_log.h"
#include "chiaki_session.h"

// Utility function needed by chiaki_regist_ng.c
uint16_t chiaki_target_get_firmware_version(ChiakiTarget target) {
  switch (target) {
    case CHIAKI_TARGET_PS4_8:
      return 800;
    case CHIAKI_TARGET_PS4_9:
      return 900;
    case CHIAKI_TARGET_PS4_10:
      return 1000;
    case CHIAKI_TARGET_PS5_1:
      return 1200;
    case CHIAKI_TARGET_PS4_UNKNOWN:
    default:
      return 0;
  }
}

// Target utility functions needed by session.c
bool chiaki_target_is_unknown(ChiakiTarget target) {
  return (target == CHIAKI_TARGET_PS4_UNKNOWN ||
          target == CHIAKI_TARGET_PS5_UNKNOWN);
}

bool chiaki_target_is_ps5(ChiakiTarget target) {
  return (target == CHIAKI_TARGET_PS5_1 || target == CHIAKI_TARGET_PS5_UNKNOWN);
}

const char* chiaki_target_string(ChiakiTarget target) {
  switch (target) {
    case CHIAKI_TARGET_PS4_8:
      return "PS4 8.0";
    case CHIAKI_TARGET_PS4_9:
      return "PS4 9.0";
    case CHIAKI_TARGET_PS4_10:
      return "PS4 10.0";
    case CHIAKI_TARGET_PS5_1:
      return "PS5 1.0";
    case CHIAKI_TARGET_PS4_UNKNOWN:
      return "PS4 Unknown";
    case CHIAKI_TARGET_PS5_UNKNOWN:
      return "PS5 Unknown";
    default:
      return "Unknown";
  }
}

// Controller utility function
void chiaki_controller_state_set_idle(ChiakiControllerState* state) {
  if (!state) return;

  // Set all controller inputs to idle/neutral state
  state->buttons = 0;
  state->left_x = 0;
  state->left_y = 0;
  state->right_x = 0;
  state->right_y = 0;
  state->l2_state = 0;
  state->r2_state = 0;

  // Reset touchpad
  state->touch_id_next = 0;
  state->touches[0].active = false;
  state->touches[1].active = false;

  // Reset motion sensors
  state->gyro_x = 0.0f;
  state->gyro_y = 0.0f;
  state->gyro_z = 0.0f;
  state->accel_x = 0.0f;
  state->accel_y = 0.0f;
  state->accel_z = 0.0f;

  // Reset orientation (neutral quaternion)
  state->orient_x = 0.0f;
  state->orient_y = 0.0f;
  state->orient_z = 0.0f;
  state->orient_w = 1.0f;
}

// Stop pipe utility function - removed due to conflict with vitaki
// implementation Using vitaki_stoppipe.c implementation instead

// NOTE: ECDH implementation provided by chiaki_ecdh_vitaki.c