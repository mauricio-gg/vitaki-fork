#ifndef VITARPS5_HTTP_CLIENT_H
#define VITARPS5_HTTP_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

#include "../core/vitarps5.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file http_client.h
 * @brief HTTP client for PSN API requests
 *
 * This module provides HTTP client functionality for making requests to PSN
 * APIs, including authentication flows and account lookups.
 */

// HTTP response structure
typedef struct {
  int status_code;       // HTTP status code (200, 404, etc.)
  char* response_body;   // Response data (caller must free)
  size_t response_size;  // Size of response data
  char* content_type;    // Content-Type header (caller must free)
  bool success;          // True if request completed without errors
} HTTPResponse;

// HTTP request configuration
typedef struct {
  const char* url;        // Request URL
  const char* method;     // HTTP method (GET, POST, etc.)
  const char* headers;    // Additional headers (optional)
  const char* body;       // Request body for POST/PUT (optional)
  size_t body_size;       // Size of request body
  int timeout_ms;         // Request timeout in milliseconds
  bool follow_redirects;  // Whether to follow HTTP redirects
} HTTPRequest;

/**
 * Initialize HTTP client subsystem
 */
VitaRPS5Result http_client_init(void);

/**
 * Cleanup HTTP client subsystem
 */
void http_client_cleanup(void);

/**
 * Check if HTTP client is available
 * @return true if HTTP client is ready for requests, false otherwise
 */
bool http_client_is_available(void);

/**
 * Perform HTTP request
 *
 * @param request Request configuration
 * @param response Output response (caller must call http_response_free)
 * @return VITARPS5_SUCCESS on success, error code on failure
 */
VitaRPS5Result http_client_request(const HTTPRequest* request,
                                   HTTPResponse* response);

/**
 * Perform GET request (convenience function)
 */
VitaRPS5Result http_client_get(const char* url, HTTPResponse* response);

/**
 * Perform POST request with JSON body (convenience function)
 */
VitaRPS5Result http_client_post_json(const char* url, const char* json_body,
                                     HTTPResponse* response);

/**
 * Free HTTPResponse resources
 */
void http_response_free(HTTPResponse* response);

/**
 * URL encode a string (caller must free result)
 */
char* http_url_encode(const char* input);

/**
 * Build query string from key-value pairs (caller must free result)
 */
char* http_build_query_string(const char* keys[], const char* values[],
                              int count);

#ifdef __cplusplus
}
#endif

#endif  // VITARPS5_HTTP_CLIENT_H