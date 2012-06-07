#define DEFAULT_PORT 1337

typedef struct {
  int port;
  char * username;
  unsigned char api_key[9240];
  char * password;
  int valid;
  long api_key_size;
} Config;

char *read_string_key(char *key, json_t *root);
int read_integer_key(char *key, json_t *root);
Config get_config(char * config_file);