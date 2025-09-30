#include <stdio.h>

#include "../core/error_codes.h"

// VitaRPS5 error code to string mapping
const char* vitarps5_error_string(VitaRPS5ErrorCode error) {
  switch (error) {
    case VITARPS5_ERR_SUCCESS:
      return "Success";
    case VITARPS5_ERR_UNKNOWN:
      return "Unknown error";
    case VITARPS5_ERR_MEMORY:
      return "Memory allocation failed";
    case VITARPS5_ERR_OVERFLOW:
      return "Buffer overflow";
    case VITARPS5_ERR_INVALID_DATA:
      return "Invalid data";
    case VITARPS5_ERR_INVALID_PARAM:
      return "Invalid parameter";
    case VITARPS5_ERR_INVALID_STATE:
      return "Invalid state";
    case VITARPS5_ERR_THREAD:
      return "Thread error";
    case VITARPS5_ERR_TIMEOUT:
      return "Operation timed out";
    case VITARPS5_ERR_CANCELED:
      return "Operation canceled";
    case VITARPS5_ERR_IN_PROGRESS:
      return "Operation in progress";
    case VITARPS5_ERR_BUF_TOO_SMALL:
      return "Buffer too small";

    // Network errors
    case VITARPS5_ERR_NETWORK:
      return "Network error";
    case VITARPS5_ERR_PARSE_ADDR:
      return "Failed to parse address";
    case VITARPS5_ERR_CONNECTION_REFUSED:
      return "Connection refused";
    case VITARPS5_ERR_HOST_DOWN:
      return "Host is down";
    case VITARPS5_ERR_HOST_UNREACH:
      return "Host unreachable";
    case VITARPS5_ERR_DISCONNECTED:
      return "Disconnected";
    case VITARPS5_ERR_SOCKET_FAILED:
      return "Socket creation failed";
    case VITARPS5_ERR_BIND_FAILED:
      return "Socket bind failed";
    case VITARPS5_ERR_CONNECT_FAILED:
      return "Connection failed";

    // PS5-specific errors
    case VITARPS5_ERR_VERSION_MISMATCH:
      return "Version mismatch";
    case VITARPS5_ERR_AUTH_FAILED:
      return "Authentication failed";
    case VITARPS5_ERR_INVALID_RESPONSE:
      return "Invalid response";
    case VITARPS5_ERR_CRYPTO:
      return "Cryptography error";
    case VITARPS5_ERR_REGISTRATION_FAILED:
      return "Registration failed";
    case VITARPS5_ERR_CONSOLE_NOT_REGISTERED:
      return "Console not registered";
    case VITARPS5_ERR_PSN_ACCOUNT_REQUIRED:
      return "PSN account required";
    case VITARPS5_ERR_INVALID_CREDENTIALS:
      return "Invalid credentials";

    // Service availability errors
    case VITARPS5_ERR_SERVICE_NOT_READY:
      return "PS5 Remote Play service not ready";
    case VITARPS5_ERR_CONSOLE_IN_STANDBY:
      return "Console is in standby mode";
    case VITARPS5_ERR_CONSOLE_OFFLINE:
      return "Console is offline";
    case VITARPS5_ERR_REMOTE_PLAY_DISABLED:
      return "Remote Play disabled";
    case VITARPS5_ERR_WAKE_FAILED:
      return "Wake failed";

    // Session errors
    case VITARPS5_ERR_SESSION_CREATE_FAILED:
      return "Session creation failed";
    case VITARPS5_ERR_SESSION_NOT_FOUND:
      return "Session not found";
    case VITARPS5_ERR_SESSION_ALREADY_EXISTS:
      return "Session already exists";

    // Video/Audio errors
    case VITARPS5_ERR_VIDEO_DECODER:
      return "Video decoder error";
    case VITARPS5_ERR_AUDIO_DECODER:
      return "Audio decoder error";
    case VITARPS5_ERR_RENDERER:
      return "Renderer error";

    // Discovery errors
    case VITARPS5_ERR_DISCOVERY_FAILED:
      return "Discovery failed";
    case VITARPS5_ERR_DISCOVERY_TIMEOUT:
      return "Discovery timeout";

    default:
      return "Unknown error code";
  }
}

// Detailed error descriptions
const char* vitarps5_error_description(VitaRPS5ErrorCode error) {
  switch (error) {
    case VITARPS5_ERR_CONNECTION_REFUSED:
      return "PS5 refused the connection. Remote Play service may not be "
             "running.";
    case VITARPS5_ERR_SERVICE_NOT_READY:
      return "PS5 Remote Play service is not ready. The console may need more "
             "time to start up.";
    case VITARPS5_ERR_CONSOLE_IN_STANDBY:
      return "PS5 is in rest mode. Wake the console before connecting.";
    case VITARPS5_ERR_AUTH_FAILED:
      return "Authentication failed. Registration may be invalid or expired.";
    case VITARPS5_ERR_CONSOLE_NOT_REGISTERED:
      return "This PS5 is not registered with your Vita. Please register "
             "first.";
    case VITARPS5_ERR_PSN_ACCOUNT_REQUIRED:
      return "A valid PSN account is required for Remote Play.";
    case VITARPS5_ERR_REMOTE_PLAY_DISABLED:
      return "Remote Play is disabled on the PS5. Enable it in console "
             "settings.";
    case VITARPS5_ERR_VERSION_MISMATCH:
      return "Protocol version mismatch. PS5 firmware may be incompatible.";
    case VITARPS5_ERR_WAKE_FAILED:
      return "Failed to wake PS5. Ensure wake-on-LAN is enabled in PS5 "
             "settings.";
    default:
      return vitarps5_error_string(error);
  }
}

// User-friendly troubleshooting hints
const char* vitarps5_error_hint(VitaRPS5ErrorCode error) {
  switch (error) {
    case VITARPS5_ERR_CONNECTION_REFUSED:
      return "Try: 1) Wake the PS5 first, 2) Wait 20 seconds after wake, 3) "
             "Check network connection";
    case VITARPS5_ERR_SERVICE_NOT_READY:
      return "Wait 15-20 seconds after waking PS5 for services to start";
    case VITARPS5_ERR_CONSOLE_IN_STANDBY:
      return "Use the Wake button to wake your PS5 from rest mode";
    case VITARPS5_ERR_AUTH_FAILED:
      return "Try re-registering your Vita with the PS5";
    case VITARPS5_ERR_CONSOLE_NOT_REGISTERED:
      return "Register this Vita with your PS5 using the registration PIN";
    case VITARPS5_ERR_PSN_ACCOUNT_REQUIRED:
      return "Sign into PSN on your Vita or set a PSN ID in Profile settings";
    case VITARPS5_ERR_REMOTE_PLAY_DISABLED:
      return "Go to PS5 Settings > System > Remote Play and enable it";
    case VITARPS5_ERR_HOST_UNREACH:
      return "Check that PS5 IP address is correct and both devices are on "
             "same network";
    case VITARPS5_ERR_WAKE_FAILED:
      return "Enable wake-on-LAN in PS5 Power Saving settings";
    default:
      return "";
  }
}

// Note: chiaki_error_string() is defined in
// vitaki-fork/src/chiaki/chiaki_common.c

// Console state strings
const char* console_state_string(ConsoleState state) {
  switch (state) {
    case CONSOLE_STATE_READY:
      return "Ready";
    case CONSOLE_STATE_STANDBY:
      return "Standby";
    case CONSOLE_STATE_UNKNOWN:
    default:
      return "Unknown";
  }
}

// Note: chiaki_to_vitarps5_error is implemented in chiaki_integration.h
