#include <stdio.h>
#include <jansson.h>
#include "config.h"

char *read_string_key(char *key, json_t *root) {
  json_t *value = json_object_get(root, key);
  if(!json_is_string(value)) {
    return NULL;
  }

  return (char *) json_string_value(value);
}

int read_integer_key(char *key, json_t *root) {
  json_t *value = json_object_get(root, key);
  if(!json_is_integer(value)) {
    return -1;
  }

  return (int) json_integer_value(value);
}

Config get_config(char * config_file){
  Config c;
  long key_size;
  json_error_t error;
  json_t *root = json_load_file(config_file, 0, &error);

  if(!root){
    printf(
      "Configuration error: %s at %s:%d\n", 
      error.text,
      config_file,
      error.line
    );
    c.valid = -1;
    return c;
  }

  c.username = read_string_key("username", root);
  if(!c.username) {
    fprintf(stderr, "No username found in configuration file\n");
    c.valid = -1;
    return c;
  }

  c.password = read_string_key("password", root);
  if(!c.password) {
    fprintf(stderr, "No password found in configuration file\n");
    c.valid = -1;
    return c;
  }

  /* Use default port if port wasn't found in config file */
  c.port = read_integer_key("port", root);
  if(c.port == -1) {
    fprintf(
      stderr,
      "No port found in configuration file, using %d\n", 
      DEFAULT_PORT
    );
    c.port = DEFAULT_PORT;
  }

  char * path_to_api_key = read_string_key("api_key", root);
  if(!path_to_api_key){
    c.valid = -1;
    fprintf(stderr, "No api_key found in configuration file\n");
    return c;
  }

  FILE *api_file = fopen(path_to_api_key, "rb");
  if(!api_file){
    fprintf(stderr, "%s could not be read.\n", path_to_api_key);
    c.valid = -1;
    return c;
  }

  fseek(api_file, 0L, SEEK_END);
  key_size = ftell(api_file);
  fseek(api_file, 0L, SEEK_SET);

  if (key_size > 4096) {
    fclose(api_file);
    fprintf(stderr, "Key file is too large (%ld) to be a key file", key_size);
    c.valid = -1;
    return c;
  }

  if (fread(c.api_key, 1, key_size, api_file) != key_size) {
    fclose(api_file);
    fprintf(stderr, "Failed reading %s\n", path_to_api_key);
    c.valid = -1;
    return c;
  }

  c.api_key_size = key_size;
  fclose(api_file);

  c.valid = 1;
  return c;
}