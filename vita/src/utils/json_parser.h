#ifndef VITARPS5_JSON_PARSER_H
#define VITARPS5_JSON_PARSER_H

#include <stdbool.h>
#include <stdint.h>

#include "../core/vitarps5.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file json_parser.h
 * @brief Simple JSON parser for PSN API responses
 *
 * This module provides basic JSON parsing functionality for handling PSN API
 * responses. It's designed to be lightweight and focused on the specific
 * JSON structures we need to parse.
 */

// JSON value types
typedef enum {
  JSON_TYPE_NULL = 0,
  JSON_TYPE_BOOL,
  JSON_TYPE_NUMBER,
  JSON_TYPE_STRING,
  JSON_TYPE_OBJECT,
  JSON_TYPE_ARRAY
} JSONType;

// JSON value structure
typedef struct JSONValue {
  JSONType type;
  union {
    bool bool_value;
    double number_value;
    char* string_value;
    struct {
      struct JSONValue** values;
      char** keys;
      int count;
    } object_value;
    struct {
      struct JSONValue** values;
      int count;
    } array_value;
  };
} JSONValue;

/**
 * Parse JSON string into JSONValue structure
 *
 * @param json_string Input JSON string
 * @return Parsed JSONValue (caller must call json_value_free), NULL on error
 */
JSONValue* json_parse(const char* json_string);

/**
 * Free JSONValue and all its nested values
 */
void json_value_free(JSONValue* value);

/**
 * Get string value from JSON object by key
 *
 * @param object JSON object
 * @param key Key to look up
 * @return String value or NULL if not found/wrong type
 */
const char* json_object_get_string(const JSONValue* object, const char* key);

/**
 * Get number value from JSON object by key
 *
 * @param object JSON object
 * @param key Key to look up
 * @param out_value Output value
 * @return true if found and is number, false otherwise
 */
bool json_object_get_number(const JSONValue* object, const char* key,
                            double* out_value);

/**
 * Get boolean value from JSON object by key
 *
 * @param object JSON object
 * @param key Key to look up
 * @param out_value Output value
 * @return true if found and is boolean, false otherwise
 */
bool json_object_get_bool(const JSONValue* object, const char* key,
                          bool* out_value);

/**
 * Get object value from JSON object by key
 *
 * @param object JSON object
 * @param key Key to look up
 * @return JSON object value or NULL if not found/wrong type
 */
const JSONValue* json_object_get_object(const JSONValue* object,
                                        const char* key);

/**
 * Get array value from JSON object by key
 *
 * @param object JSON object
 * @param key Key to look up
 * @return JSON array value or NULL if not found/wrong type
 */
const JSONValue* json_object_get_array(const JSONValue* object,
                                       const char* key);

/**
 * Get array length
 */
int json_array_get_length(const JSONValue* array);

/**
 * Get array element by index
 */
const JSONValue* json_array_get_element(const JSONValue* array, int index);

/**
 * Create JSON string from key-value pairs (simple object creation)
 *
 * @param keys Array of string keys
 * @param values Array of string values
 * @param count Number of key-value pairs
 * @return JSON string (caller must free), NULL on error
 */
char* json_create_object(const char* keys[], const char* values[], int count);

/**
 * Escape string for JSON
 *
 * @param input Input string
 * @return Escaped string (caller must free), NULL on error
 */
char* json_escape_string(const char* input);

#ifdef __cplusplus
}
#endif

#endif  // VITARPS5_JSON_PARSER_H