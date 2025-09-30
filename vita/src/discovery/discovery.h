// discovery.h
#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef enum { PS5_STATE_UNKNOWN, PS5_STATE_READY, PS5_STATE_STANDBY } Ps5State;

int build_discovery_probe(char *out, size_t cap);  // returns bytes
bool parse_discovery_response(const char *buf, size_t len, Ps5State *state);