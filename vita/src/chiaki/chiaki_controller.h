#ifndef VITARPS5_CHIAKI_CONTROLLER_H
#define VITARPS5_CHIAKI_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// PS5 Controller button mapping (from vitaki-fork)
typedef enum {
  CHIAKI_CONTROLLER_BUTTON_CROSS = 0x01,
  CHIAKI_CONTROLLER_BUTTON_MOON = 0x02,     // Circle
  CHIAKI_CONTROLLER_BUTTON_BOX = 0x04,      // Square
  CHIAKI_CONTROLLER_BUTTON_PYRAMID = 0x08,  // Triangle
  CHIAKI_CONTROLLER_BUTTON_L1 = 0x10,
  CHIAKI_CONTROLLER_BUTTON_R1 = 0x20,
  CHIAKI_CONTROLLER_BUTTON_SHARE = 0x40,    // Select/Share
  CHIAKI_CONTROLLER_BUTTON_OPTIONS = 0x80,  // Start/Options
  CHIAKI_CONTROLLER_BUTTON_L3 = 0x100,
  CHIAKI_CONTROLLER_BUTTON_R3 = 0x200,
  CHIAKI_CONTROLLER_BUTTON_PS = 0x400,
  CHIAKI_CONTROLLER_BUTTON_TOUCHPAD = 0x800,
  CHIAKI_CONTROLLER_BUTTON_DPAD_UP = 0x1000,
  CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN = 0x2000,
  CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT = 0x4000,
  CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT = 0x8000
} ChiakiControllerButton;

// Controller touchpad touch
typedef struct {
  uint8_t id;   // Touch ID (0-1 for dual touch)
  bool active;  // Is this touch point active?
  uint16_t x;   // X coordinate (0-1920)
  uint16_t y;   // Y coordinate (0-943)
} ChiakiControllerTouch;

// Complete controller state structure (based on vitaki-fork)
typedef struct {
  // Digital buttons (bitmask of ChiakiControllerButton values)
  uint32_t buttons;

  // Analog triggers (0x00 to 0xFF)
  uint8_t l2_state;
  uint8_t r2_state;

  // Analog sticks (-32768 to 32767)
  int16_t left_x;
  int16_t left_y;
  int16_t right_x;
  int16_t right_y;

  // Touchpad state
  uint8_t touch_id_next;             // Next available touch ID
  ChiakiControllerTouch touches[2];  // Up to 2 simultaneous touches

  // Motion sensor data (from PS Vita motion sensors)
  float gyro_x;   // Gyroscope X (rad/s)
  float gyro_y;   // Gyroscope Y (rad/s)
  float gyro_z;   // Gyroscope Z (rad/s)
  float accel_x;  // Accelerometer X (g)
  float accel_y;  // Accelerometer Y (g)
  float accel_z;  // Accelerometer Z (g)

  // Orientation quaternion
  float orient_x;  // Quaternion X
  float orient_y;  // Quaternion Y
  float orient_z;  // Quaternion Z
  float orient_w;  // Quaternion W

} ChiakiControllerState;

// Helper functions for controller state
bool chiaki_controller_state_equals(const ChiakiControllerState* a,
                                    const ChiakiControllerState* b);
bool chiaki_controller_state_equals_for_feedback_state(
    const ChiakiControllerState* a, const ChiakiControllerState* b);
bool chiaki_controller_state_equals_for_feedback_history(
    const ChiakiControllerState* a, const ChiakiControllerState* b);

// Initialize controller state to default values
void chiaki_controller_state_init(ChiakiControllerState* state);

// Copy controller state
void chiaki_controller_state_copy(ChiakiControllerState* dest,
                                  const ChiakiControllerState* src);

#ifdef __cplusplus
}
#endif

#endif  // VITARPS5_CHIAKI_CONTROLLER_H