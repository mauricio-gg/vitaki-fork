#include "http_client.h"

#include <psp2/net/http.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../network/network_manager.h"
#include "logger.h"

// PS Vita Network Error Codes
#define SCE_NET_ERROR_EBUSY 0x80412102  // Already initialized/busy
#define SCE_NETCTL_ERROR_NOT_INITIALIZED 0x80412301

// HTTP client state
static bool http_client_initialized = false;
static bool http_modules_available =
    false;  // Track if HTTP is actually working
static int http_template_id = -1;
static int http_connection_id = -1;
static bool netctl_initialized_by_us = false;
static bool net_initialized_by_us = false;
static bool http_module_loaded_by_us = false;

// Internal buffer size for responses
#define HTTP_RESPONSE_BUFFER_SIZE (64 * 1024)  // 64KB buffer
#define HTTP_HEADER_BUFFER_SIZE 1024

// Static network memory buffer (PS Vita requires static allocation)
#define NET_MEMORY_SIZE (1024 * 1024)  // 1MB for network operations
static uint8_t net_memory_buffer[NET_MEMORY_SIZE] __attribute__((aligned(64)));

// Internal functions
static VitaRPS5Result init_http_modules(void);
static void cleanup_http_modules(void);
static VitaRPS5Result perform_request(const HTTPRequest* request,
                                      HTTPResponse* response);
static char* read_response_body(int request_id, size_t* response_size);

VitaRPS5Result http_client_init(void) {
  if (http_client_initialized) {
    return VITARPS5_SUCCESS;
  }

  log_info("Initializing HTTP client");

  VitaRPS5Result result = init_http_modules();
  if (result != VITARPS5_SUCCESS) {
    log_error("Failed to initialize HTTP modules: %s",
              vitarps5_result_string(result));

    // Mark as initialized but disabled to prevent further init attempts
    http_client_initialized = true;
    http_modules_available = false;
    log_error("HTTP client initialized in disabled mode");
    return result;  // Return error so caller knows HTTP is unavailable
  }

  // Only proceed with HTTP functions if modules are available
  if (!http_modules_available) {
    log_error("HTTP modules not available - cannot create HTTP client");
    http_client_initialized = true;  // Mark as initialized but disabled
    return VITARPS5_ERROR_NETWORK;
  }

  // Create HTTP template with User-Agent
  log_debug("Creating HTTP template");
  http_template_id =
      sceHttpCreateTemplate("VitaRPS5/0.5.637", SCE_HTTP_VERSION_1_1, SCE_TRUE);
  if (http_template_id < 0) {
    log_error("Failed to create HTTP template: 0x%08X", http_template_id);
    cleanup_http_modules();
    return VITARPS5_ERROR_NETWORK;
  }
  log_debug("HTTP template created successfully (ID: %d)", http_template_id);

  // Create HTTP connection with a generic URL that should work
  log_debug("Creating HTTP connection");
  http_connection_id = sceHttpCreateConnectionWithURL(
      http_template_id, "http://www.example.com",
      SCE_FALSE);  // Use HTTP instead of HTTPS initially
  if (http_connection_id < 0) {
    log_error("Failed to create HTTP connection: 0x%08X", http_connection_id);
    sceHttpDeleteTemplate(http_template_id);
    http_template_id = -1;
    cleanup_http_modules();
    return VITARPS5_ERROR_NETWORK;
  }
  log_debug("HTTP connection created successfully (ID: %d)",
            http_connection_id);

  http_client_initialized = true;
  log_info("HTTP client initialized successfully");
  return VITARPS5_SUCCESS;
}

void http_client_cleanup(void) {
  if (!http_client_initialized) {
    return;
  }

  log_info("Cleaning up HTTP client");

  if (http_connection_id >= 0) {
    sceHttpDeleteConnection(http_connection_id);
    http_connection_id = -1;
  }

  if (http_template_id >= 0) {
    sceHttpDeleteTemplate(http_template_id);
    http_template_id = -1;
  }

  cleanup_http_modules();
  http_client_initialized = false;
  log_info("HTTP client cleanup completed");
}

bool http_client_is_available(void) {
  return http_client_initialized && http_modules_available;
}

VitaRPS5Result http_client_request(const HTTPRequest* request,
                                   HTTPResponse* response) {
  if (!http_client_initialized || !request || !response) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  // Check if HTTP modules are actually available
  if (!http_modules_available) {
    log_error("HTTP client request failed: HTTP modules are not available");
    return VITARPS5_ERROR_NETWORK;
  }

  // Initialize response
  memset(response, 0, sizeof(HTTPResponse));
  response->success = false;

  log_info("HTTP %s request to: %s", request->method ? request->method : "GET",
           request->url);

  // Debug: Log headers if provided
  if (request->headers) {
    log_debug("HTTP headers: %s", request->headers);
  }

  // Debug: Log body if provided
  if (request->body && request->body_size > 0) {
    log_debug("HTTP body (%zu bytes): %s", request->body_size, request->body);
  }

  return perform_request(request, response);
}

VitaRPS5Result http_client_get(const char* url, HTTPResponse* response) {
  if (!url || !response) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  HTTPRequest request = {.url = url,
                         .method = "GET",
                         .headers = NULL,
                         .body = NULL,
                         .body_size = 0,
                         .timeout_ms = 30000,  // 30 second timeout
                         .follow_redirects = true};

  return http_client_request(&request, response);
}

VitaRPS5Result http_client_post_json(const char* url, const char* json_body,
                                     HTTPResponse* response) {
  if (!url || !json_body || !response) {
    return VITARPS5_ERROR_INVALID_PARAM;
  }

  HTTPRequest request = {.url = url,
                         .method = "POST",
                         .headers = "Content-Type: application/json",
                         .body = json_body,
                         .body_size = strlen(json_body),
                         .timeout_ms = 30000,  // 30 second timeout
                         .follow_redirects = true};

  return http_client_request(&request, response);
}

void http_response_free(HTTPResponse* response) {
  if (!response) {
    return;
  }

  if (response->response_body) {
    free(response->response_body);
    response->response_body = NULL;
  }

  if (response->content_type) {
    free(response->content_type);
    response->content_type = NULL;
  }

  response->response_size = 0;
  response->status_code = 0;
  response->success = false;
}

char* http_url_encode(const char* input) {
  if (!input) {
    return NULL;
  }

  size_t input_len = strlen(input);
  // Worst case: every character needs encoding (3x expansion)
  char* encoded = malloc(input_len * 3 + 1);
  if (!encoded) {
    return NULL;
  }

  size_t encoded_pos = 0;
  for (size_t i = 0; i < input_len; i++) {
    unsigned char c = input[i];

    // Characters that don't need encoding
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
        c == '~') {
      encoded[encoded_pos++] = c;
    } else {
      // Encode as %XX
      sprintf(&encoded[encoded_pos], "%%%02X", c);
      encoded_pos += 3;
    }
  }

  encoded[encoded_pos] = '\0';
  return encoded;
}

char* http_build_query_string(const char* keys[], const char* values[],
                              int count) {
  if (!keys || !values || count <= 0) {
    return NULL;
  }

  // Calculate required buffer size
  size_t total_size = 1;  // For null terminator
  for (int i = 0; i < count; i++) {
    if (keys[i] && values[i]) {
      total_size += strlen(keys[i]) * 3;    // Worst case encoding
      total_size += strlen(values[i]) * 3;  // Worst case encoding
      total_size += 2;                      // For '=' and '&'
    }
  }

  char* query_string = malloc(total_size);
  if (!query_string) {
    return NULL;
  }

  query_string[0] = '\0';
  bool first = true;

  for (int i = 0; i < count; i++) {
    if (keys[i] && values[i]) {
      if (!first) {
        strcat(query_string, "&");
      }

      char* encoded_key = http_url_encode(keys[i]);
      char* encoded_value = http_url_encode(values[i]);

      if (encoded_key && encoded_value) {
        strcat(query_string, encoded_key);
        strcat(query_string, "=");
        strcat(query_string, encoded_value);
      }

      free(encoded_key);
      free(encoded_value);
      first = false;
    }
  }

  return query_string;
}

// Internal implementations

static VitaRPS5Result init_http_modules(void) {
  log_info("Initializing PS Vita network and HTTP modules");

  // Step 1: Initialize NetCtl module first
  int netctl_result = sceNetCtlInit();
  if (netctl_result < 0) {
    if (netctl_result == SCE_NET_ERROR_EBUSY) {
      log_debug("NetCtl already initialized by another subsystem - continuing");
      netctl_initialized_by_us = false;
    } else {
      log_error("Failed to initialize NetCtl: 0x%08X", netctl_result);
      return VITARPS5_ERROR_NETWORK;
    }
  } else {
    log_debug("NetCtl initialized successfully");
    netctl_initialized_by_us = true;
  }

  // Step 2: Check network manager state instead of initializing network
  // ourselves
  NetworkState net_state = network_manager_get_state();
  if (net_state != NETWORK_STATE_AVAILABLE) {
    log_info("Network manager reports network unavailable (state: %d)",
             net_state);
    log_info("HTTP client will be disabled - network features unavailable");
    http_modules_available = false;
    return VITARPS5_SUCCESS;  // Not a fatal error - graceful degradation
  }

  log_debug(
      "Network manager reports network available - proceeding with HTTP init");
  net_initialized_by_us = false;  // Network managed by network_manager

  // Step 3: Load HTTP system module before initialization
  log_debug("Loading HTTP system module");
  int module_result = sceSysmoduleLoadModule(SCE_SYSMODULE_HTTP);
  if (module_result < 0 && module_result != SCE_SYSMODULE_LOADED) {
    log_error("Failed to load HTTP system module: 0x%08X", module_result);
    log_info("HTTP client will be disabled - network features unavailable");
    http_modules_available = false;
    return VITARPS5_SUCCESS;  // Not a fatal error - graceful degradation
  } else if (module_result == SCE_SYSMODULE_LOADED) {
    log_debug("HTTP system module already loaded");
    http_module_loaded_by_us = false;
  } else {
    log_debug("HTTP system module loaded successfully");
    http_module_loaded_by_us = true;
  }

  // Step 4: Initialize HTTP module with smaller buffer size
  int http_result = sceHttpInit(32 * 1024);  // Use 32KB instead of 64KB
  if (http_result < 0) {
    log_error("Failed to initialize HTTP: 0x%08X", http_result);

    // HTTP is not available - mark as such to prevent crashes
    log_info("HTTP modules are not available - HTTP client will be disabled");
    log_info(
        "Network features will be unavailable, but app can start normally");
    http_modules_available = false;
    return VITARPS5_SUCCESS;  // Not a fatal error - graceful degradation
  } else {
    log_debug("HTTP module initialized successfully");
    http_modules_available = true;
  }

  log_info("Network and HTTP modules ready for use");
  return VITARPS5_SUCCESS;
}

static void cleanup_http_modules(void) {
  log_debug("Cleaning up HTTP and network modules");
  sceHttpTerm();

  // Unload HTTP system module if we loaded it ourselves
  if (http_module_loaded_by_us) {
    log_debug("Unloading HTTP system module (we loaded it)");
    sceSysmoduleUnloadModule(SCE_SYSMODULE_HTTP);
    http_module_loaded_by_us = false;
  } else {
    log_debug(
        "Not unloading HTTP system module (already loaded by another "
        "subsystem)");
  }

  // Note: We don't call sceNetTerm() as other parts of the app might be using
  // network Only terminate NetCtl if we initialized it ourselves
  if (netctl_initialized_by_us) {
    log_debug("Terminating NetCtl (we initialized it)");
    sceNetCtlTerm();
    netctl_initialized_by_us = false;
  } else {
    log_debug("Not terminating NetCtl (initialized by another subsystem)");
  }
  log_debug("HTTP and network modules cleanup completed");
}

static VitaRPS5Result perform_request(const HTTPRequest* request,
                                      HTTPResponse* response) {
  // Convert method string to VitaSDK constant
  int method = SCE_HTTP_METHOD_GET;  // Default to GET
  if (request->method) {
    if (strcmp(request->method, "POST") == 0) {
      method = SCE_HTTP_METHOD_POST;
    } else if (strcmp(request->method, "PUT") == 0) {
      method = SCE_HTTP_METHOD_PUT;
    } else if (strcmp(request->method, "DELETE") == 0) {
      method = SCE_HTTP_METHOD_DELETE;
    }
    // GET is already set as default
  }

  // Create HTTP request
  int request_id =
      sceHttpCreateRequestWithURL(http_connection_id, method, request->url, 0);
  if (request_id < 0) {
    log_error("Failed to create HTTP request: 0x%08X", request_id);
    return VITARPS5_ERROR_NETWORK;
  }

  // Set headers if provided
  if (request->headers) {
    // Simple header parsing - assumes "Header: Value" format
    char* headers_copy = strdup(request->headers);
    char* header = strtok(headers_copy, "\n");
    while (header) {
      char* colon = strchr(header, ':');
      if (colon) {
        *colon = '\0';
        char* value = colon + 1;
        // Skip leading spaces
        while (*value == ' ') value++;

        int result = sceHttpAddRequestHeader(request_id, header, value,
                                             SCE_HTTP_HEADER_OVERWRITE);
        if (result < 0) {
          log_warning("Failed to add header '%s': 0x%08X", header, result);
        }
      }
      header = strtok(NULL, "\n");
    }
    free(headers_copy);
  }

  // Set request body if provided
  if (request->body && request->body_size > 0) {
    int result =
        sceHttpSendRequest(request_id, request->body, request->body_size);
    if (result < 0) {
      log_error("Failed to send HTTP request with body: 0x%08X", result);
      sceHttpDeleteRequest(request_id);
      return VITARPS5_ERROR_NETWORK;
    }
  } else {
    int result = sceHttpSendRequest(request_id, NULL, 0);
    if (result < 0) {
      log_error("Failed to send HTTP request: 0x%08X", result);
      sceHttpDeleteRequest(request_id);
      return VITARPS5_ERROR_NETWORK;
    }
  }

  // Get response status
  int status_result = sceHttpGetStatusCode(request_id, &response->status_code);
  if (status_result < 0) {
    log_error("Failed to get HTTP status code: 0x%08X", status_result);
    sceHttpDeleteRequest(request_id);
    return VITARPS5_ERROR_NETWORK;
  }

  log_info("HTTP response status: %d", response->status_code);

  // Read response body
  response->response_body =
      read_response_body(request_id, &response->response_size);
  if (!response->response_body) {
    log_warning("Failed to read response body");
  }

  // Get content type (using available VitaSDK function)
  char content_type_buffer[256];
  unsigned int header_size = sizeof(content_type_buffer);
  int ct_result = sceHttpGetAllResponseHeaders(
      request_id, (char**)&content_type_buffer, &header_size);
  if (ct_result >= 0) {
    // Simple parsing - look for Content-Type in headers
    char* ct_start = strstr(content_type_buffer, "Content-Type: ");
    if (ct_start) {
      ct_start += 14;  // Length of "Content-Type: "
      char* ct_end = strstr(ct_start, "\r\n");
      if (ct_end) {
        size_t ct_len = ct_end - ct_start;
        response->content_type = malloc(ct_len + 1);
        if (response->content_type) {
          strncpy(response->content_type, ct_start, ct_len);
          response->content_type[ct_len] = '\0';
        }
      }
    }
  }

  sceHttpDeleteRequest(request_id);

  // Consider 2xx status codes as success
  response->success =
      (response->status_code >= 200 && response->status_code < 300);

  return VITARPS5_SUCCESS;
}

static char* read_response_body(int request_id, size_t* response_size) {
  if (response_size) {
    *response_size = 0;
  }

  // Get content length
  uint64_t content_length = 0;
  int cl_result = sceHttpGetResponseContentLength(request_id, &content_length);

  // Allocate buffer (use content length if available, otherwise default size)
  size_t buffer_size = (cl_result >= 0 && content_length > 0)
                           ? content_length + 1
                           : HTTP_RESPONSE_BUFFER_SIZE;

  char* buffer = malloc(buffer_size);
  if (!buffer) {
    log_error("Failed to allocate response buffer");
    return NULL;
  }

  // Read response data
  size_t total_read = 0;
  int bytes_read;

  do {
    bytes_read = sceHttpReadData(request_id, buffer + total_read,
                                 buffer_size - total_read - 1);
    if (bytes_read > 0) {
      total_read += bytes_read;
    }
  } while (bytes_read > 0 && total_read < buffer_size - 1);

  if (bytes_read < 0) {
    log_error("Failed to read HTTP response data: 0x%08X", bytes_read);
    free(buffer);
    return NULL;
  }

  buffer[total_read] = '\0';

  if (response_size) {
    *response_size = total_read;
  }

  log_debug("Read %zu bytes of response data", total_read);
  return buffer;
}