#ifndef VITARPS5_CONSOLE_TYPES_H
#define VITARPS5_CONSOLE_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

// PlayStation console types
typedef enum {
  PS_CONSOLE_UNKNOWN = 0,
  PS_CONSOLE_PS4,
  PS_CONSOLE_PS4_PRO,
  PS_CONSOLE_PS5,
  PS_CONSOLE_PS5_DIGITAL
} PSConsoleType;

// Console discovery states (matches vitaki-fork/chiaki discovery states)
typedef enum {
  CONSOLE_DISCOVERY_STATE_UNKNOWN = 0,  // State cannot be determined
  CONSOLE_DISCOVERY_STATE_READY =
      1,  // Console is awake and ready for connection
  CONSOLE_DISCOVERY_STATE_STANDBY = 2  // Console is in rest/sleep mode
} ConsoleDiscoveryState;

#ifdef __cplusplus
}
#endif

#endif  // VITARPS5_CONSOLE_TYPES_H