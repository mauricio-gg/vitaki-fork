#include "chiaki_controller.h"

#include <math.h>
#include <string.h>

void chiaki_controller_state_init(ChiakiControllerState* state) {
  if (!state) return;

  memset(state, 0, sizeof(ChiakiControllerState));

  // Initialize orientation quaternion to identity (no rotation)
  state->orient_w = 1.0f;
  state->orient_x = 0.0f;
  state->orient_y = 0.0f;
  state->orient_z = 0.0f;
}

void chiaki_controller_state_copy(ChiakiControllerState* dest,
                                  const ChiakiControllerState* src) {
  if (!dest || !src) return;

  memcpy(dest, src, sizeof(ChiakiControllerState));
}

bool chiaki_controller_state_equals(const ChiakiControllerState* a,
                                    const ChiakiControllerState* b) {
  if (!a || !b) return false;

  return memcmp(a, b, sizeof(ChiakiControllerState)) == 0;
}

bool chiaki_controller_state_equals_for_feedback_state(
    const ChiakiControllerState* a, const ChiakiControllerState* b) {
  if (!a || !b) return false;

  // Compare motion sensor data and analog sticks (continuous data)
  // Use epsilon for floating point comparison
  const float epsilon = 0.001f;

  // Check analog sticks
  if (a->left_x != b->left_x || a->left_y != b->left_y ||
      a->right_x != b->right_x || a->right_y != b->right_y) {
    return false;
  }

  // Check analog triggers
  if (a->l2_state != b->l2_state || a->r2_state != b->r2_state) {
    return false;
  }

  // Check gyroscope (with epsilon)
  if (fabsf(a->gyro_x - b->gyro_x) > epsilon ||
      fabsf(a->gyro_y - b->gyro_y) > epsilon ||
      fabsf(a->gyro_z - b->gyro_z) > epsilon) {
    return false;
  }

  // Check accelerometer (with epsilon)
  if (fabsf(a->accel_x - b->accel_x) > epsilon ||
      fabsf(a->accel_y - b->accel_y) > epsilon ||
      fabsf(a->accel_z - b->accel_z) > epsilon) {
    return false;
  }

  // Check orientation quaternion (with epsilon)
  if (fabsf(a->orient_x - b->orient_x) > epsilon ||
      fabsf(a->orient_y - b->orient_y) > epsilon ||
      fabsf(a->orient_z - b->orient_z) > epsilon ||
      fabsf(a->orient_w - b->orient_w) > epsilon) {
    return false;
  }

  return true;
}

bool chiaki_controller_state_equals_for_feedback_history(
    const ChiakiControllerState* a, const ChiakiControllerState* b) {
  if (!a || !b) return false;

  // Compare discrete events: buttons and touchpad touches

  // Check buttons
  if (a->buttons != b->buttons) {
    return false;
  }

  // Check touchpad touches
  for (int i = 0; i < 2; i++) {
    const ChiakiControllerTouch* touch_a = &a->touches[i];
    const ChiakiControllerTouch* touch_b = &b->touches[i];

    if (touch_a->active != touch_b->active || touch_a->id != touch_b->id ||
        touch_a->x != touch_b->x || touch_a->y != touch_b->y) {
      return false;
    }
  }

  // Check touch ID counter
  if (a->touch_id_next != b->touch_id_next) {
    return false;
  }

  return true;
}