#ifndef VITARPS5_UI_REGISTRATION_H
#define VITARPS5_UI_REGISTRATION_H

/**
 * Console Registration UI for VitaRPS5
 *
 * Provides user interface for PlayStation console registration process,
 * including PIN entry and progress tracking.
 * Updated to use clean data model from CONNECTION_DEBUG_PLAN.md
 */

#include <psp2/ctrl.h>
#include <stdbool.h>
#include <stdint.h>

#include "../core/console_registration.h"
#include "../core/vitarps5.h"

#ifdef __cplusplus
extern "C" {
#endif

// Registration UI states
typedef enum {
  REGISTRATION_UI_STATE_IDLE = 0,
  REGISTRATION_UI_STATE_PIN_ENTRY,
  REGISTRATION_UI_STATE_REGISTERING,
  REGISTRATION_UI_STATE_SUCCESS,
  REGISTRATION_UI_STATE_ERROR
} RegistrationUIState;

// Registration UI configuration - simplified for clean implementation
typedef struct {
  char console_name[64];
  char console_ip[16];
  uint64_t psn_account_id;         // Keep for backward compatibility
  char psn_account_id_base64[16];  // Original base64 PSN ID (direct from
                                   // profile)
} RegistrationUIConfig;

// PIN entry state
typedef struct {
  uint32_t pin_digits[8];  // Individual PIN digits (0-9)
  uint32_t current_digit;  // Current digit being edited (0-7)
  uint32_t complete_pin;   // Complete PIN as number
  bool pin_complete;       // All digits entered
} PinEntryState;

// Registration progress information
typedef struct {
  RegistrationUIState ui_state;
  const char* status_message;
  float progress_percentage;
  uint32_t elapsed_time_ms;
  bool has_error;
  const char* error_message;
} RegistrationProgress;

// Core Registration UI API

/**
 * Initialize registration UI subsystem
 */
VitaRPS5Result ui_registration_init(void);

/**
 * Cleanup registration UI subsystem
 */
void ui_registration_cleanup(void);

/**
 * Start console registration process
 */
VitaRPS5Result ui_registration_start(const RegistrationUIConfig* config);

/**
 * Cancel ongoing registration
 */
VitaRPS5Result ui_registration_cancel(void);

/**
 * Update registration UI (call regularly)
 */
VitaRPS5Result ui_registration_update(void);

/**
 * Render registration UI
 */
void ui_registration_render(void);

/**
 * Handle input during registration
 */
void ui_registration_handle_input(SceCtrlData* pad, SceCtrlData* prev_pad);

/**
 * Get current registration UI state
 */
RegistrationUIState ui_registration_get_state(void);

/**
 * Check if registration UI is active
 */
bool ui_registration_is_active(void);

/**
 * Get registration progress information
 */
VitaRPS5Result ui_registration_get_progress(RegistrationProgress* progress);

// PIN Entry Functions

/**
 * Get current PIN entry state
 */
VitaRPS5Result ui_registration_get_pin_state(PinEntryState* pin_state);

/**
 * Set PIN digit at specific position
 */
VitaRPS5Result ui_registration_set_pin_digit(uint32_t position, uint32_t digit);

/**
 * Clear all PIN digits
 */
void ui_registration_clear_pin(void);

/**
 * Submit completed PIN
 */
VitaRPS5Result ui_registration_submit_pin(void);

// UI State Management

/**
 * Set registration UI state
 */
void ui_registration_set_state(RegistrationUIState state);

/**
 * Set error message
 */
void ui_registration_set_error(const char* error_message);

/**
 * Set status message
 */
void ui_registration_set_status(const char* status_message);

// Utility Functions

/**
 * Format PIN for display (adds spacing)
 */
void ui_registration_format_pin_display(const PinEntryState* pin_state,
                                        char* display_text);

/**
 * Check if PIN entry is complete
 */
bool ui_registration_is_pin_complete(const PinEntryState* pin_state);

/**
 * Convert PIN entry to number
 */
uint32_t ui_registration_pin_to_number(const PinEntryState* pin_state);

/**
 * Validate PIN entry
 */
bool ui_registration_is_pin_valid(const PinEntryState* pin_state);

#ifdef __cplusplus
}
#endif

#endif  // VITARPS5_UI_REGISTRATION_H