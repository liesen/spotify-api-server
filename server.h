#include <apr.h>
#include <event2/event.h>
#include <libspotify/api.h>

// Application state
struct state {
  sp_session *session;

  char *credentials_blob_filename;

  struct event_base *event_base;
  struct event *async;
  struct event *timer;
  struct event *sigint;
  struct timeval next_timeout;

  struct evhttp *http;
  char *http_host;
  int http_port;

  apr_pool_t *pool;

  int exit_status;
};

void credentials_blob_updated(sp_session *session, const char *blob);

void logged_in(sp_session *session, sp_error error);

void logged_out(sp_session *session);

void notify_main_thread(sp_session *session);

void process_events(evutil_socket_t socket, short what, void *userdata);

void sigint_handler(evutil_socket_t socket, short what, void *userdata);
