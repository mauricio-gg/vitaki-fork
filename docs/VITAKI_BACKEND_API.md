# Vitaki-Fork Backend API Audit

## Purpose
Document the existing vitaki-fork backend API that our UI should call directly (no wrapper/stub layers).

## Core Backend Files to Use

### 1. host.c - Console Registration & Management
**Functions we need**:
- `int host_register(VitaChiakiHost* host, int pin)` - Register console with PIN
- `int host_wakeup(VitaChiakiHost* host)` - Wake console from standby
- `VitaChiakiHost* find_host_by_*()` - Find host by various criteria

**Structures**:
```c
typedef struct {
    ChiakiDiscoveryHost* discovery_state;  // Discovery info
    ChiakiRegisteredHost* registered_state; // Registration credentials
    char* hostname;                         // IP address
    ChiakiTarget target;                    // PS4/PS5
    uint8_t server_mac[6];                  // MAC address
    HostType type;                          // DISCOVERED | REGISTERED
} VitaChiakiHost;
```

### 2. discovery.c - Console Discovery
**Functions**:
- Discovery happens automatically via `context.discovery`
- Callback: `discovery_cb()` updates `context.hosts[]`
- UI reads from: `context.hosts[i]->discovery_state`

### 3. config.c - Settings & Persistence  
**Functions**:
- `int config_load(VitaChiakiConfig* config)` - Load saved config
- `int config_save(VitaChiakiConfig* config)` - Save config
- `void config_serialize(VitaChiakiConfig* config)` - Write to file

**Structure**:
```c
typedef struct {
    char psn_account_id[13];           // Base64 PSN ID
    VitaChiakiHost* registered_hosts[MAX_NUM_HOSTS];
    int num_registered_hosts;
    // ... video/audio settings
} VitaChiakiConfig;
```

### 4. context.c - Global State
**Global variable**:
```c
extern VitaChiakiContext context;

typedef struct {
    ChiakiLog log;
    ChiakiDiscoveryService discovery;
    bool discovery_enabled;
    VitaChiakiHost* hosts[MAX_NUM_HOSTS];
    VitaChiakiHost* active_host;
    VitaChiakiStream stream;           // Active streaming session
    VitaChiakiConfig config;
    uint8_t num_hosts;
} VitaChiakiContext;
```

### 5. Streaming - Uses ChiakiSession directly
**From context.stream**:
```c
typedef struct {
    ChiakiSession session;             // Chiaki streaming session
    bool session_init;
    bool is_streaming;
    ChiakiControllerState controller_state;
} VitaChiakiStream;
```

**Functions in existing codebase** (check host.c):
- Session init/start probably in host.c or inline

## UI Integration Plan

### Registration Flow (UI → Vitaki):
```c
// ui_registration.c
#include "../host.h"
#include "../context.h"

void ui_handle_pin_entry(const char* ip, const char* pin) {
    // Find host from discovery
    VitaChiakiHost* host = NULL;
    for (int i = 0; i < context.num_hosts; i++) {
        if (strcmp(context.hosts[i]->hostname, ip) == 0) {
            host = context.hosts[i];
            break;
        }
    }
    
    if (host) {
        // Call vitaki's registration directly
        int result = host_register(host, atoi(pin));
        if (result == 0) {
            // Success! Credentials now in host->registered_state
            config_serialize(&context.config);  // Save
        }
    }
}
```

### Streaming Flow (UI → Vitaki):
```c
// ui_streaming.c  
#include "../context.h"
#include "../host.h"

void ui_start_streaming(const char* ip) {
    // Set active host
    context.active_host = find_host_by_ip(ip);
    
    // Initialize Chiaki session (check host.c for exact function)
    // Likely: chiaki_session_init(&context.stream.session, ...)
    // Then: chiaki_session_start(&context.stream.session)
    
    context.stream.is_streaming = true;
}
```

### Discovery (Vitaki → UI):
```c
// UI just reads from context.hosts[]
for (int i = 0; i < context.num_hosts; i++) {
    VitaChiakiHost* host = context.hosts[i];
    if (host->discovery_state) {
        // Display: host->discovery_state->host_name
        // Display: host->discovery_state->state (READY/STANDBY)
        // Status: host->type & REGISTERED
    }
}
```

## Next Steps
1. Read host.c to find exact streaming init functions
2. Check if ChiakiSession needs wrapper or can be used directly
3. Document callback registration for discovery updates

