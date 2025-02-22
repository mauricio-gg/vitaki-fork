#pragma once
#include <chiaki/common.h>
#include <chiaki/session.h>

#include "host.h"

#define CFG_VERSION 1
#define CFG_FILENAME "ux0:data/vita-chiaki/chiaki.toml"

/// Action to perform after terminating a session
typedef enum vita_chiaki_disconnect_action_t {
  DISCONNECT_ACTION_ASK,      // Let the user decide each time
  DISCONNECT_ACTION_REST,     // Put the console into Rest Mode
  DISCONNECT_ACTION_NOTHING,  // Just leave the console running
} VitaChiakiDisconnectAction;

/// Settings for the app
typedef struct vita_chiaki_config_t {
  int cfg_version;
  // We use a global PSN Account ID so users only have to enter it once
  char* psn_account_id;
  /// Whether discovery is enabled by default
  bool auto_discovery;
  VitaChiakiDisconnectAction disconnect_action;
  ChiakiVideoResolutionPreset resolution;
  ChiakiVideoFPSPreset fps;
  size_t num_manual_hosts;
  VitaChiakiHost* manual_hosts[MAX_NUM_HOSTS];
  size_t num_registered_hosts;
  VitaChiakiHost* registered_hosts[MAX_NUM_HOSTS];
  // TODO: Logfile path
  // TODO: Loglevel
  // controller map id // TODO should probably replace with fully customizable map
  int controller_map_id;
  bool circle_btn_select;
} VitaChiakiConfig;

void config_parse(VitaChiakiConfig* cfg);
void config_free(VitaChiakiConfig* cfg);
void config_serialize(VitaChiakiConfig* cfg);
