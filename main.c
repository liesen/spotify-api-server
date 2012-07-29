#include <apr.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/thread.h>
#include <libspotify/api.h>
#include <stdbool.h>
#include <stdlib.h>
#include <svn_diff.h>
#include <syslog.h>

#include "server.h"

// Application key
extern const unsigned char g_appkey[];
extern const size_t g_appkey_size;

// Spotify account information
extern const char username[];
extern const char password[];

int main(int argc, char **argv) {
  // Open syslog
  openlog("spotify-api-server", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL1);

  // Initialize program state
  struct state *state = malloc(sizeof(struct state));

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
      .application_key = g_appkey,
      .application_key_size = g_appkey_size,
      .cache_location = ".cache",
      .callbacks = &session_callbacks,
      .compress_playlists = false,
      .dont_save_metadata_for_playlists = false,
      .settings_location = ".settings",
      .user_agent = "sphttpd",
      .userdata = state,
    };

    sp_session *session;
    sp_error session_create_error = sp_session_create(&session_config,
                                                      &session);

    if (session_create_error != SP_ERROR_OK) {
      syslog(LOG_CRIT, "Error creating Spotify session: %s",
             sp_error_message(session_create_error));
    } else {
      // Log in to Spotify
      sp_session_login(session, username, password, false, NULL);

      event_base_dispatch(state->event_base);
    }
  }

  event_free(state->async);
  event_free(state->timer);
  event_free(state->sigint);
  if (state->http != NULL) evhttp_free(state->http);
  event_base_free(state->event_base);
  int exit_status = state->exit_status;
  free(state);
  return exit_status;
}
