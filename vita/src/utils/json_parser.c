#include "json_parser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "logger.h"

// Parser state
typedef struct {
  const char* json;
  size_t position;
  size_t length;
} JSONParser;

// Internal functions
static JSONValue* parse_value(JSONParser* parser);
static JSONValue* parse_object(JSONParser* parser);
static JSONValue* parse_array(JSONParser* parser);
static JSONValue* parse_string(JSONParser* parser);
static JSONValue* parse_number(JSONParser* parser);
static JSONValue* parse_literal(JSONParser* parser);
static void skip_whitespace(JSONParser* parser);
static bool peek_char(JSONParser* parser, char expected);
static bool consume_char(JSONParser* parser, char expected);
static char* parse_string_content(JSONParser* parser);
static JSONValue* create_json_value(JSONType type);

JSONValue* json_parse(const char* json_string) {
  if (!json_string) {
    log_error("JSON parse: null input string");
    return NULL;
  }

  JSONParser parser = {
      .json = json_string, .position = 0, .length = strlen(json_string)};

  log_debug("Parsing JSON string of length %zu", parser.length);

  JSONValue* result = parse_value(&parser);
  if (!result) {
    log_error("JSON parse failed at position %zu", parser.position);
  }

  return result;
}

void json_value_free(JSONValue* value) {
  if (!value) {
    return;
  }

  switch (value->type) {
    case JSON_TYPE_STRING:
      free(value->string_value);
      break;

    case JSON_TYPE_OBJECT:
      for (int i = 0; i < value->object_value.count; i++) {
        free(value->object_value.keys[i]);
        json_value_free(value->object_value.values[i]);
      }
      free(value->object_value.keys);
      free(value->object_value.values);
      break;

    case JSON_TYPE_ARRAY:
      for (int i = 0; i < value->array_value.count; i++) {
        json_value_free(value->array_value.values[i]);
      }
      free(value->array_value.values);
      break;

    default:
      // No cleanup needed for primitive types
      break;
  }

  free(value);
}

const char* json_object_get_string(const JSONValue* object, const char* key) {
  if (!object || object->type != JSON_TYPE_OBJECT || !key) {
    return NULL;
  }

  for (int i = 0; i < object->object_value.count; i++) {
    if (strcmp(object->object_value.keys[i], key) == 0) {
      JSONValue* value = object->object_value.values[i];
      if (value->type == JSON_TYPE_STRING) {
        return value->string_value;
      }
      break;
    }
  }

  return NULL;
}

bool json_object_get_number(const JSONValue* object, const char* key,
                            double* out_value) {
  if (!object || object->type != JSON_TYPE_OBJECT || !key || !out_value) {
    return false;
  }

  for (int i = 0; i < object->object_value.count; i++) {
    if (strcmp(object->object_value.keys[i], key) == 0) {
      JSONValue* value = object->object_value.values[i];
      if (value->type == JSON_TYPE_NUMBER) {
        *out_value = value->number_value;
        return true;
      }
      break;
    }
  }

  return false;
}

bool json_object_get_bool(const JSONValue* object, const char* key,
                          bool* out_value) {
  if (!object || object->type != JSON_TYPE_OBJECT || !key || !out_value) {
    return false;
  }

  for (int i = 0; i < object->object_value.count; i++) {
    if (strcmp(object->object_value.keys[i], key) == 0) {
      JSONValue* value = object->object_value.values[i];
      if (value->type == JSON_TYPE_BOOL) {
        *out_value = value->bool_value;
        return true;
      }
      break;
    }
  }

  return false;
}

const JSONValue* json_object_get_object(const JSONValue* object,
                                        const char* key) {
  if (!object || object->type != JSON_TYPE_OBJECT || !key) {
    return NULL;
  }

  for (int i = 0; i < object->object_value.count; i++) {
    if (strcmp(object->object_value.keys[i], key) == 0) {
      JSONValue* value = object->object_value.values[i];
      if (value->type == JSON_TYPE_OBJECT) {
        return value;
      }
      break;
    }
  }

  return NULL;
}

const JSONValue* json_object_get_array(const JSONValue* object,
                                       const char* key) {
  if (!object || object->type != JSON_TYPE_OBJECT || !key) {
    return NULL;
  }

  for (int i = 0; i < object->object_value.count; i++) {
    if (strcmp(object->object_value.keys[i], key) == 0) {
      JSONValue* value = object->object_value.values[i];
      if (value->type == JSON_TYPE_ARRAY) {
        return value;
      }
      break;
    }
  }

  return NULL;
}

int json_array_get_length(const JSONValue* array) {
  if (!array || array->type != JSON_TYPE_ARRAY) {
    return -1;
  }

  return array->array_value.count;
}

const JSONValue* json_array_get_element(const JSONValue* array, int index) {
  if (!array || array->type != JSON_TYPE_ARRAY || index < 0 ||
      index >= array->array_value.count) {
    return NULL;
  }

  return array->array_value.values[index];
}

char* json_create_object(const char* keys[], const char* values[], int count) {
  if (!keys || !values || count <= 0) {
    return NULL;
  }

  // Calculate required buffer size
  size_t buffer_size = 3;  // "{}\\0"
  for (int i = 0; i < count; i++) {
    if (keys[i] && values[i]) {
      buffer_size += strlen(keys[i]) * 2 + strlen(values[i]) * 2 +
                     8;  // Quotes, colon, comma, escaping
    }
  }

  char* json = malloc(buffer_size);
  if (!json) {
    return NULL;
  }

  strcpy(json, "{");
  bool first = true;

  for (int i = 0; i < count; i++) {
    if (keys[i] && values[i]) {
      if (!first) {
        strcat(json, ",");
      }

      char* escaped_key = json_escape_string(keys[i]);
      char* escaped_value = json_escape_string(values[i]);

      if (escaped_key && escaped_value) {
        strcat(json, "\"");
        strcat(json, escaped_key);
        strcat(json, "\":\"");
        strcat(json, escaped_value);
        strcat(json, "\"");
      }

      free(escaped_key);
      free(escaped_value);
      first = false;
    }
  }

  strcat(json, "}");
  return json;
}

char* json_escape_string(const char* input) {
  if (!input) {
    return NULL;
  }

  size_t input_len = strlen(input);
  // Worst case: every character needs escaping (2x expansion)
  char* escaped = malloc(input_len * 2 + 1);
  if (!escaped) {
    return NULL;
  }

  size_t escaped_pos = 0;
  for (size_t i = 0; i < input_len; i++) {
    char c = input[i];

    switch (c) {
      case '"':
        escaped[escaped_pos++] = '\\';
        escaped[escaped_pos++] = '"';
        break;
      case '\\':
        escaped[escaped_pos++] = '\\';
        escaped[escaped_pos++] = '\\';
        break;
      case '\b':
        escaped[escaped_pos++] = '\\';
        escaped[escaped_pos++] = 'b';
        break;
      case '\f':
        escaped[escaped_pos++] = '\\';
        escaped[escaped_pos++] = 'f';
        break;
      case '\n':
        escaped[escaped_pos++] = '\\';
        escaped[escaped_pos++] = 'n';
        break;
      case '\r':
        escaped[escaped_pos++] = '\\';
        escaped[escaped_pos++] = 'r';
        break;
      case '\t':
        escaped[escaped_pos++] = '\\';
        escaped[escaped_pos++] = 't';
        break;
      default:
        escaped[escaped_pos++] = c;
        break;
    }
  }

  escaped[escaped_pos] = '\0';
  return escaped;
}

// Internal implementations

static JSONValue* parse_value(JSONParser* parser) {
  skip_whitespace(parser);

  if (parser->position >= parser->length) {
    return NULL;
  }

  char c = parser->json[parser->position];

  switch (c) {
    case '{':
      return parse_object(parser);
    case '[':
      return parse_array(parser);
    case '"':
      return parse_string(parser);
    case 't':
    case 'f':
    case 'n':
      return parse_literal(parser);
    default:
      if (c == '-' || isdigit(c)) {
        return parse_number(parser);
      }
      break;
  }

  log_error("JSON parse: unexpected character '%c' at position %zu", c,
            parser->position);
  return NULL;
}

static JSONValue* parse_object(JSONParser* parser) {
  if (!consume_char(parser, '{')) {
    return NULL;
  }

  JSONValue* object = create_json_value(JSON_TYPE_OBJECT);
  if (!object) {
    return NULL;
  }

  object->object_value.count = 0;
  object->object_value.keys = NULL;
  object->object_value.values = NULL;

  skip_whitespace(parser);

  // Handle empty object
  if (peek_char(parser, '}')) {
    consume_char(parser, '}');
    return object;
  }

  // Parse key-value pairs
  while (parser->position < parser->length) {
    skip_whitespace(parser);

    // Parse key
    if (!peek_char(parser, '"')) {
      log_error("JSON parse: expected string key at position %zu",
                parser->position);
      json_value_free(object);
      return NULL;
    }

    char* key = parse_string_content(parser);
    if (!key) {
      json_value_free(object);
      return NULL;
    }

    skip_whitespace(parser);
    if (!consume_char(parser, ':')) {
      log_error("JSON parse: expected ':' after key at position %zu",
                parser->position);
      free(key);
      json_value_free(object);
      return NULL;
    }

    // Parse value
    JSONValue* value = parse_value(parser);
    if (!value) {
      free(key);
      json_value_free(object);
      return NULL;
    }

    // Add to object
    object->object_value.count++;
    object->object_value.keys = realloc(
        object->object_value.keys, object->object_value.count * sizeof(char*));
    object->object_value.values =
        realloc(object->object_value.values,
                object->object_value.count * sizeof(JSONValue*));

    if (!object->object_value.keys || !object->object_value.values) {
      free(key);
      json_value_free(value);
      json_value_free(object);
      return NULL;
    }

    object->object_value.keys[object->object_value.count - 1] = key;
    object->object_value.values[object->object_value.count - 1] = value;

    skip_whitespace(parser);

    if (peek_char(parser, '}')) {
      break;
    }

    if (!consume_char(parser, ',')) {
      log_error("JSON parse: expected ',' or '}' at position %zu",
                parser->position);
      json_value_free(object);
      return NULL;
    }
  }

  if (!consume_char(parser, '}')) {
    json_value_free(object);
    return NULL;
  }

  return object;
}

static JSONValue* parse_array(JSONParser* parser) {
  if (!consume_char(parser, '[')) {
    return NULL;
  }

  JSONValue* array = create_json_value(JSON_TYPE_ARRAY);
  if (!array) {
    return NULL;
  }

  array->array_value.count = 0;
  array->array_value.values = NULL;

  skip_whitespace(parser);

  // Handle empty array
  if (peek_char(parser, ']')) {
    consume_char(parser, ']');
    return array;
  }

  // Parse array elements
  while (parser->position < parser->length) {
    JSONValue* value = parse_value(parser);
    if (!value) {
      json_value_free(array);
      return NULL;
    }

    // Add to array
    array->array_value.count++;
    array->array_value.values =
        realloc(array->array_value.values,
                array->array_value.count * sizeof(JSONValue*));

    if (!array->array_value.values) {
      json_value_free(value);
      json_value_free(array);
      return NULL;
    }

    array->array_value.values[array->array_value.count - 1] = value;

    skip_whitespace(parser);

    if (peek_char(parser, ']')) {
      break;
    }

    if (!consume_char(parser, ',')) {
      log_error("JSON parse: expected ',' or ']' at position %zu",
                parser->position);
      json_value_free(array);
      return NULL;
    }
  }

  if (!consume_char(parser, ']')) {
    json_value_free(array);
    return NULL;
  }

  return array;
}

static JSONValue* parse_string(JSONParser* parser) {
  char* content = parse_string_content(parser);
  if (!content) {
    return NULL;
  }

  JSONValue* value = create_json_value(JSON_TYPE_STRING);
  if (!value) {
    free(content);
    return NULL;
  }

  value->string_value = content;
  return value;
}

static JSONValue* parse_number(JSONParser* parser) {
  size_t start = parser->position;

  // Skip optional minus
  if (parser->position < parser->length &&
      parser->json[parser->position] == '-') {
    parser->position++;
  }

  // Parse integer part
  if (parser->position >= parser->length ||
      !isdigit((unsigned char)parser->json[parser->position])) {
    return NULL;
  }

  while (parser->position < parser->length &&
         isdigit((unsigned char)parser->json[parser->position])) {
    parser->position++;
  }

  // Parse decimal part
  if (parser->position < parser->length &&
      parser->json[parser->position] == '.') {
    parser->position++;
    while (parser->position < parser->length &&
           isdigit((unsigned char)parser->json[parser->position])) {
      parser->position++;
    }
  }

  // Parse exponent part
  if (parser->position < parser->length &&
      (parser->json[parser->position] == 'e' ||
       parser->json[parser->position] == 'E')) {
    parser->position++;
    if (parser->position < parser->length &&
        (parser->json[parser->position] == '+' ||
         parser->json[parser->position] == '-')) {
      parser->position++;
    }
    while (parser->position < parser->length &&
           isdigit((unsigned char)parser->json[parser->position])) {
      parser->position++;
    }
  }

  // Extract number string and convert
  size_t length = parser->position - start;
  char* number_str = malloc(length + 1);
  if (!number_str) {
    return NULL;
  }

  strncpy(number_str, parser->json + start, length);
  number_str[length] = '\0';

  double number = strtod(number_str, NULL);
  free(number_str);

  JSONValue* value = create_json_value(JSON_TYPE_NUMBER);
  if (!value) {
    return NULL;
  }

  value->number_value = number;
  return value;
}

static JSONValue* parse_literal(JSONParser* parser) {
  if (strncmp(parser->json + parser->position, "true", 4) == 0) {
    parser->position += 4;
    JSONValue* value = create_json_value(JSON_TYPE_BOOL);
    if (value) {
      value->bool_value = true;
    }
    return value;
  }

  if (strncmp(parser->json + parser->position, "false", 5) == 0) {
    parser->position += 5;
    JSONValue* value = create_json_value(JSON_TYPE_BOOL);
    if (value) {
      value->bool_value = false;
    }
    return value;
  }

  if (strncmp(parser->json + parser->position, "null", 4) == 0) {
    parser->position += 4;
    return create_json_value(JSON_TYPE_NULL);
  }

  return NULL;
}

static void skip_whitespace(JSONParser* parser) {
  while (parser->position < parser->length &&
         isspace((unsigned char)parser->json[parser->position])) {
    parser->position++;
  }
}

static bool peek_char(JSONParser* parser, char expected) {
  return (parser->position < parser->length &&
          parser->json[parser->position] == expected);
}

static bool consume_char(JSONParser* parser, char expected) {
  if (peek_char(parser, expected)) {
    parser->position++;
    return true;
  }
  return false;
}

static char* parse_string_content(JSONParser* parser) {
  if (!consume_char(parser, '"')) {
    return NULL;
  }

  size_t start = parser->position;
  size_t content_length = 0;

  // Find string end and calculate unescaped length
  while (parser->position < parser->length) {
    char c = parser->json[parser->position];

    if (c == '"') {
      break;
    }

    if (c == '\\') {
      parser->position++;
      if (parser->position >= parser->length) {
        return NULL;
      }
      // Skip escaped character
      parser->position++;
    } else {
      parser->position++;
    }

    content_length++;
  }

  if (!consume_char(parser, '"')) {
    return NULL;
  }

  // Allocate and unescape string content
  char* content = malloc(content_length + 1);
  if (!content) {
    return NULL;
  }

  size_t content_pos = 0;
  for (size_t i = start; i < parser->position - 1; i++) {
    char c = parser->json[i];

    if (c == '\\' && i + 1 < parser->position - 1) {
      char next = parser->json[i + 1];
      switch (next) {
        case '"':
          content[content_pos++] = '"';
          break;
        case '\\':
          content[content_pos++] = '\\';
          break;
        case '/':
          content[content_pos++] = '/';
          break;
        case 'b':
          content[content_pos++] = '\b';
          break;
        case 'f':
          content[content_pos++] = '\f';
          break;
        case 'n':
          content[content_pos++] = '\n';
          break;
        case 'r':
          content[content_pos++] = '\r';
          break;
        case 't':
          content[content_pos++] = '\t';
          break;
        default:
          content[content_pos++] = next;
          break;
      }
      i++;  // Skip the escaped character
    } else {
      content[content_pos++] = c;
    }
  }

  content[content_pos] = '\0';
  return content;
}

static JSONValue* create_json_value(JSONType type) {
  JSONValue* value = malloc(sizeof(JSONValue));
  if (!value) {
    return NULL;
  }

  memset(value, 0, sizeof(JSONValue));
  value->type = type;
  return value;
}