#include <apr.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/thread.h>
#include <getopt.h>
#include <libspotify/api.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <svn_diff.h>
#include <sys/stat.h>
#include <syslog.h>

#include "server.h"

// Application keys are 321 bytes, from what I've seen... but ramp it up
// to be on the safe side
#define MAX_APPLICATION_KEY_SIZE 1024

extern const unsigned char g_appkey[];
extern const size_t g_appkey_size;

// Read application key from file (binary), placing the results into the
// session configuration. Fails silently, without warning, if something
// goes wrong: libspotify will fail later, too.
void read_application_key(char *path, sp_session_config *session_config) {
  struct stat st;

  if (stat(path, &st) != 0) {
    return;
  }

  int appkey_size = st.st_size;

  if (appkey_size > MAX_APPLICATION_KEY_SIZE) {
    // Application key file looks awfully large
    appkey_size = MAX_APPLICATION_KEY_SIZE;
  }

  size_t size = sizeof(unsigned char);
  unsigned char *appkey = malloc(appkey_size * size);

  if (appkey == NULL) {
    return;
  }

  FILE *file = fopen(path, "rb");

  if (!file) {
    free(appkey);
    return;
  }

  session_config->application_key_size = fread(appkey, size, appkey_size, file);
  session_config->application_key = appkey;
  fclose(file);
}

int main(int argc, char **argv) {
  // Open syslog
  openlog("spotify-api-server", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL1);

  // Initialize program state
  struct state *state = malloc(sizeof(struct state));

  // Web server defaults
  state->http_host = strdup("127.0.0.1");
  state->http_port = 1337;

  // Initialize libev w/ pthreads
  evthread_use_pthreads();

  state->event_base = event_base_new();
  state->async = event_new(state->event_base, -1, 0, &process_events, state);
  state->timer = evtimer_new(state->event_base, &process_events, state);
  state->sigint = evsignal_new(state->event_base, SIGINT, &sigint_handler, state);
  state->exit_status = EXIT_FAILURE;

  // Initialize APR
  apr_status_t rv = apr_initialize();

  if (rv != APR_SUCCESS) {
    syslog(LOG_CRIT, "Unable to initialize APR");
  } else {
    apr_pool_create(&state->pool, NULL);

    // Initialize libspotify
    sp_session_callbacks session_callbacks = {
      .logged_in = &logged_in,
      .logged_out = &logged_out,
      .notify_main_thread = &notify_main_thread
    };

    sp_session_config session_config = {
      .api_version = SPOTIFY_API_VERSION,
      .application_key_size = 0,
      .cache_location = ".cache",  // Cache location is required
      .callbacks = &session_callbacks,
      .user_agent = "spotify-api-server",
      .userdata = state,
    };

    // Parse command line arguments
    char *username = NULL;
    char *password = NULL;
    char *credentials_blob = NULL;
    bool remember_me = false;
    bool relogin = false;
    struct option opts[] = {
      // Login configuration
      {"username", required_argument, NULL, 'u'},
      {"password", required_argument, NULL, 'p'},
      {"remember-me", no_argument, &remember_me, 1},
      {"credentials", required_argument, NULL, 'c'},
      {"relogin", no_argument, &relogin, 1},

      {"credentials-path", required_argument, NULL, 'k'},

      // Application key file (binary) path
      {"application-key", required_argument, NULL, 'A'},

      // Session configuration
      {"cache-location", required_argument, NULL, 'C'},
      {"compress-playlists", no_argument,
       &session_config.compress_playlists, 1},
      {"dont-save_metadata_for_playlists", no_argument,
       &session_config.dont_save_metadata_for_playlists, 1},
      {"initially-unload_playlists", no_argument,
        &session_config.initially_unload_playlists, 1},
      {"settings-location", required_argument, NULL, 'S'},
      {"tracefile", required_argument, NULL, 'T'},
      {"user-agent", required_argument, NULL, 'U'},

      // HTTP options
      {"host", required_argument, NULL, 'H'},
      {"port", required_argument, NULL, 'P'},

      {NULL, 0, NULL, 0}
    };
    const char optstring[] = "u:p:c:k:A:C:S:T:U:H:P:";

    for (int c; (c = getopt_long(argc, argv, optstring, opts, NULL)) != -1; ) {
      switch (c) {
        case 'u':
          username = strdup(optarg);
          break;

        case 'p':
          password = strdup(optarg);
          break;

        case 'c':
          credentials_blob = strdup(optarg);
          break;

        case 'k':
          state->credentials_blob_filename = strdup(optarg);
          session_callbacks.credentials_blob_updated = &credentials_blob_updated;
          break;

        case 'A':
          read_application_key(optarg, &session_config);
          break;

        case 'C':
          session_config.cache_location = strdup(optarg);
          break;

        case 'S':
          session_config.settings_location = strdup(optarg);
          break;

        case 'T':
          session_config.tracefile = strdup(optarg);
          break;

        case 'U':
          session_config.user_agent = strdup(optarg);
          break;

        case 'H':
          state->http_host = strdup(optarg);
          break;

        case 'P':
          state->http_port = atoi(optarg);
          break;
      }
    }

    if (session_config.application_key_size == 0) {
      fprintf(stderr, "You didn't specify a path to your application key (use"
                      " -A/--application-key).\n");
    } else {
      sp_session *session;
      sp_error session_create_error = sp_session_create(&session_config,
                                                        &session);

      if (session_create_error != SP_ERROR_OK) {
        syslog(LOG_CRIT, "Error creating Spotify session: %s",
               sp_error_message(session_create_error));
      } else {
        // Log in to Spotify
        if (relogin) {
          sp_session_relogin(session);
        } else {
          sp_session_login(session, username, password, remember_me,
                           credentials_blob);
        }

        event_base_dispatch(state->event_base);
      }
    }

    // Free whatever was set by command line args
    // TODO(liesen): free session_config settings too?
    if (username != NULL) free(username);
    if (password != NULL) free(password);
    if (credentials_blob != NULL) free(credentials_blob);
  }

  event_free(state->async);
  event_free(state->timer);
  event_free(state->sigint);
  if (state->http != NULL) evhttp_free(state->http);
  free(state->http_host);
  event_base_free(state->event_base);
  int exit_status = state->exit_status;
  free(state);
  return exit_status;
}
