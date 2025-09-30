#ifndef VITARPS5_VITA_SYSTEM_INFO_H
#define VITARPS5_VITA_SYSTEM_INFO_H

#include <psp2/types.h>

#include "../core/vitarps5.h"

// System information structure
typedef struct {
  char model_name[32];        // "PS Vita PCH-1000", "PS Vita PCH-2000", "PS TV"
  char firmware_version[16];  // "3.74" etc.
  char serial_number[32];     // Device serial or identifier
  char user_name[64];         // Vita user name or "Vita User"
  char profile_image_path[256];  // Path to user profile image (if available)
  bool has_profile_image;        // Whether a profile image exists
  int language;                  // System language
  int region;                    // System region
  int enter_button_assign;       // Cross/Circle button assignment
  uint32_t total_memory_mb;      // Total system memory in MB
  uint32_t free_memory_mb;       // Available memory in MB
  int battery_percent;           // Battery percentage (0-100, -1 if N/A)
  bool is_charging;              // Whether device is charging
} VitaSystemInfo;

// Network information structure
typedef struct {
  char ip_address[16];       // Current IP address
  char netmask[16];          // Network mask
  char gateway[16];          // Default gateway
  char dns_primary[16];      // Primary DNS server
  char dns_secondary[16];    // Secondary DNS server
  char connection_name[64];  // WiFi network name or "Ethernet"
  bool is_connected;         // Network connection status
  int connection_type;       // 0=None, 1=WiFi, 2=3G, 3=Ethernet
  int signal_strength;       // WiFi signal strength (0-100, -1 if N/A)
  int nat_type;              // NAT type (1-3, -1 if unknown)
} VitaNetworkInfo;

// System information functions
VitaRPS5Result vita_system_info_init(void);
void vita_system_info_cleanup(void);

VitaRPS5Result vita_system_info_get_system(VitaSystemInfo* info);
VitaRPS5Result vita_system_info_get_network(VitaNetworkInfo* info);

// Utility functions
const char* vita_system_info_get_model_display_name(void);
const char* vita_system_info_get_connection_type_string(int type);
bool vita_system_info_is_pstv(void);

// Real-time updates
VitaRPS5Result vita_system_info_refresh_network(void);
VitaRPS5Result vita_system_info_refresh_memory(void);
VitaRPS5Result vita_system_info_refresh_battery(void);

// Profile image functions
VitaRPS5Result vita_system_info_get_profile_image_path(char* path, size_t size);
bool vita_system_info_has_profile_image(void);

#endif  // VITARPS5_VITA_SYSTEM_INFO_H