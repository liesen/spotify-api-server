/*
Copyright © 2011 Johan Liesén

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <apr.h>
#include <assert.h>
#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/http_struct.h>
#include <event2/keyvalq_struct.h>
#include <event2/thread.h>
#include <event2/util.h>
#include <jansson.h>
#include <libspotify/api.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <svn_diff.h>
#include <sys/queue.h>
#include <syslog.h>

#include "constants.h"
#include "diff.h"
#include "json.h"
#include "server.h"

#define HTTP_PARTIAL 210
#define HTTP_ERROR 500
#define HTTP_NOTIMPL 501

typedef void (*handle_playlist_fn)(sp_playlist *playlist,
                                   struct evhttp_request *request,
                                   void *userdata);

// State of a request as it's threaded through libspotify callbacks
struct playlist_handler {
  sp_playlist_callbacks *playlist_callbacks;
  struct evhttp_request *request;
  handle_playlist_fn callback;
  void *userdata;
};

typedef void (*handle_playlistcontainer_fn)(sp_playlistcontainer *,
                                            struct evhttp_request *,
                                            void *);

struct playlistcontainer_handler {
  sp_playlistcontainer_callbacks *playlistcontainer_callbacks;
  struct evhttp_request *request;
  handle_playlistcontainer_fn callback;
  void *userdata;
};

static void send_reply(struct evhttp_request *request,
                       int code,
                       const char *message,
                       struct evbuffer *body) {
  evhttp_add_header(evhttp_request_get_output_headers(request),
                    "Content-type", "application/json; charset=UTF-8");
  bool empty_body = body == NULL;

  if (empty_body)
    body = evbuffer_new();

  evhttp_send_reply(request, code, message, body);

  if (empty_body)
    evbuffer_free(body);
}

// Sends JSON to the client (also `free`s the JSON object)
static void send_reply_json(struct evhttp_request *request,
                            int code,
                            const char *message,
                            json_t *json) {
  struct evbuffer *buf = evhttp_request_get_output_buffer(request);
  char *json_str = json_dumps(json, JSON_COMPACT);
  json_decref(json);
  evbuffer_add(buf, json_str, strlen(json_str));
  free(json_str);
  send_reply(request, code, message, buf);
}

// Will wrap an error message in a JSON object before sending it
static void send_error(struct evhttp_request *request,
                       int code,
                       const char *message) {
  json_t *error_object = json_object();
  json_object_set_new(error_object, "message", json_string(message));
  send_reply_json(request, code, message, error_object);
}

static void send_error_sp(struct evhttp_request *request,
                          int code,
                          sp_error error) {
  const char *message = sp_error_message(error);
  send_error(request, code, message);
}

static struct playlist_handler *register_playlist_callbacks(
    sp_playlist *playlist,
    struct evhttp_request *request,
    handle_playlist_fn callback,
    sp_playlist_callbacks *playlist_callbacks,
    void *userdata) {
  struct playlist_handler *handler = malloc(sizeof (struct playlist_handler));
  handler->request = request;
  handler->callback = callback;
  handler->playlist_callbacks = playlist_callbacks;
  handler->userdata = userdata;
  sp_playlist_add_callbacks(playlist, handler->playlist_callbacks, handler);
  return handler;
}

static void playlist_dispatch(sp_playlist *playlist, void *userdata) {
  struct playlist_handler *handler = userdata;
  sp_playlist_remove_callbacks(playlist, handler->playlist_callbacks, handler);
  handler->playlist_callbacks = NULL;
  handler->callback(playlist, handler->request, handler->userdata);
  free(handler);
}

static struct playlistcontainer_handler *register_playlistcontainer_callbacks(
    sp_playlistcontainer *pc,
    struct evhttp_request *request,
    handle_playlistcontainer_fn callback,
    sp_playlistcontainer_callbacks *playlistcontainer_callbacks,
    void *userdata) {
  struct playlistcontainer_handler *handler = malloc(sizeof (struct playlistcontainer_handler));
  handler->request = request;
  handler->callback = callback;
  handler->playlistcontainer_callbacks = playlistcontainer_callbacks;
  handler->userdata = userdata;
  sp_error error = sp_playlistcontainer_add_callbacks(pc, handler->playlistcontainer_callbacks,
                                     handler);
  syslog(LOG_DEBUG, "playlistcontainer_add_callbacks: %d", error);
  return handler;
}

static void playlistcontainer_dispatch(sp_playlistcontainer *pc, void *userdata) {
  struct playlistcontainer_handler *handler = userdata;
  sp_playlistcontainer_remove_callbacks(pc,
                                        handler->playlistcontainer_callbacks,
                                        handler);
  handler->playlistcontainer_callbacks = NULL;
  handler->callback(pc, handler->request, handler->userdata);
  free(handler);
}

static void playlist_dispatch_if_loaded(sp_playlist *playlist, void *userdata) {
  if (sp_playlist_is_loaded(playlist))
    playlist_dispatch(playlist, userdata);
}

static void playlist_dispatch_if_updated(sp_playlist *playlist,
                                         bool done,
                                         void *userdata) {
  if (done)
    playlist_dispatch(playlist, userdata);
}

// Callbacks for when a playlist is loaded
static sp_playlist_callbacks playlist_state_changed_callbacks = {
  .playlist_state_changed = &playlist_dispatch_if_loaded
};

// Callbacks for when a playlist is updated
static sp_playlist_callbacks playlist_update_in_progress_callbacks = {
  .playlist_update_in_progress = &playlist_dispatch_if_updated
};

// Callbacks for when subscribers to a playlist is changed
static sp_playlist_callbacks playlist_subscribers_changed_callbacks = {
  .subscribers_changed = &playlist_dispatch
};

// Callbacks for when a playlist container is loaded
static sp_playlistcontainer_callbacks playlistcontainer_loaded_callbacks = {
  .container_loaded = &playlistcontainer_dispatch
};

// HTTP handlers

// Standard response handler
static void not_implemented(sp_playlist *playlist,
                            struct evhttp_request *request,
                            void *userdata) {
  sp_playlist_release(playlist);
  evhttp_send_error(request, HTTP_NOTIMPL, "Not Implemented");
}

// Responds with an entire playlist
static void get_playlist(sp_playlist *playlist,
                         struct evhttp_request *request,
                         void *userdata) {
  json_t *json = json_object();

  if (playlist_to_json(playlist, json) == NULL) {
    json_decref(json);
    send_error(request, HTTP_ERROR, "");
    return;
  }

  sp_playlist_release(playlist);
  send_reply_json(request, HTTP_OK, "OK", json);
}

static void get_playlist_collaborative(sp_playlist *playlist,
                                       struct evhttp_request *request,
                                       void *userdata) {
  assert(sp_playlist_is_loaded(playlist));
  json_t *json = json_object();
  playlist_to_json_set_collaborative(playlist, json);
  sp_playlist_release(playlist);
  send_reply_json(request, HTTP_OK, "OK", json);
}

static void get_playlist_subscribers_callback(sp_playlist *playlist,
                                              struct evhttp_request *request,
                                              void *userdata) {
  assert(sp_playlist_is_loaded(playlist));
  sp_subscribers *subscribers = sp_playlist_subscribers(playlist);
  json_t *array = json_array();

  for (int i = 0; i < subscribers->count; i++) {
    char *subscriber = subscribers->subscribers[i];
    json_array_append_new(array, json_string(subscriber));
  }

  sp_playlist_subscribers_free(subscribers);
  sp_playlist_release(playlist);
  send_reply_json(request, HTTP_OK, "OK", array);
}

static void get_playlist_subscribers(sp_playlist *playlist,
                                     struct evhttp_request *request,
                                     void *userdata) {
  assert(sp_playlist_is_loaded(playlist));
  sp_session *session = userdata;
  register_playlist_callbacks(playlist, request,
                              &get_playlist_subscribers_callback,
                              &playlist_subscribers_changed_callbacks,
                              userdata);
  sp_playlist_update_subscribers(session, playlist);
}

// Reads JSON from the requests body. Returns NULL on any error.
static json_t *read_request_body_json(struct evhttp_request *request,
                                      json_error_t *error) {
  struct evbuffer *buf = evhttp_request_get_input_buffer(request);
  size_t buflen = evbuffer_get_length(buf);

  if (buflen == 0)
    return NULL;

  // Read body
  char *body = malloc(buflen + 1);

  if (body == NULL)
    return NULL; // TODO(liesen): Handle memory alloc fail

  if (evbuffer_remove(buf, body, buflen) == -1) {
    free(body);
    return NULL;
  }

  body[buflen] = '\0';

  // Parse JSON
  json_t *json = json_loads(body, 0, error);
  free(body);
  return json;
}

static void inbox_post_complete(sp_inbox *inbox, void *userdata) {
  struct evhttp_request *request = userdata;
  sp_error inbox_error = sp_inbox_error(inbox);
  sp_inbox_release(inbox);

  switch (inbox_error) {
    case SP_ERROR_OK:
      send_reply(request, HTTP_OK, "OK", NULL);
      break;

    case SP_ERROR_NO_SUCH_USER:
      send_error_sp(request, HTTP_NOTFOUND, inbox_error);
      break;

    default:
      send_error_sp(request, HTTP_ERROR, inbox_error);
      break;
  }
}

static void get_user_playlists(sp_playlistcontainer *pc,
                               struct evhttp_request *request,
                               void *userdata) {
  json_t *json = json_object();
  json_t *playlists = json_array();
  json_object_set_new(json, "playlists", playlists);
  int status = HTTP_OK;

  for (int i = 0; i < sp_playlistcontainer_num_playlists(pc); i++) {
    if (sp_playlistcontainer_playlist_type(pc, i) != SP_PLAYLIST_TYPE_PLAYLIST)
      continue;

    sp_playlist *playlist = sp_playlistcontainer_playlist(pc, i);

    if (!sp_playlist_is_loaded(playlist)) {
      status = HTTP_PARTIAL;
      continue;
    }

    json_t *playlist_json = json_object();
    playlist_to_json(playlist, playlist_json);
    json_array_append_new(playlists, playlist_json);
  }

  sp_playlistcontainer_release(pc);
  send_reply_json(request, status, status == HTTP_OK ? "OK" : "Partial Content",
                  json);
}

static void put_user_inbox(const char *user,
                           struct evhttp_request *request,
                           void *userdata) {
  json_error_t loads_error;
  json_t *json = read_request_body_json(request, &loads_error);

  if (json == NULL) {
    send_error(request, HTTP_BADREQUEST,
        loads_error.text ? loads_error.text : "Unable to parse JSON");
    return;
  }

  if (!json_is_object(json)) {
    json_decref(json);
    send_error(request, HTTP_BADREQUEST, "Not valid JSON object");
    return;
  }

  json_t *tracks_json = json_object_get(json, "tracks");

  if (!json_is_array(tracks_json)) {
    json_decref(tracks_json);
    send_error(request, HTTP_BADREQUEST, "tracks is not valid JSON array");
    return;
  }

  // Handle empty array
  int num_tracks = json_array_size(tracks_json);

  if (num_tracks == 0) {
    send_reply(request, HTTP_OK, "OK", NULL);
    return;
  }

  sp_track **tracks = calloc(num_tracks, sizeof (sp_track *));
  int num_valid_tracks = json_to_tracks(tracks_json, tracks, num_tracks);

  if (num_valid_tracks == 0) {
    send_error(request, HTTP_BADREQUEST, "No valid tracks");
  } else {
    json_t *message_json = json_object_get(json, "message");
    sp_session *session = userdata;
    sp_inbox *inbox = sp_inbox_post_tracks(session, user, tracks,
        num_valid_tracks,
        json_is_string(message_json) ? json_string_value(message_json) : "",
        &inbox_post_complete, request);

    if (inbox == NULL)
      send_error(request, HTTP_ERROR,
                 "Failed to initialize request to add tracks to user's inbox");
  }

  json_decref(json);
  free(tracks);
}

static void put_playlist(sp_playlist *playlist,
                         struct evhttp_request *request,
                         void *userdata) {
  // TODO(liesen): playlist there so that signatures of all handler methods are
  // the same, but do they have to be?
  assert(playlist == NULL);

  sp_session *session = userdata;
  json_error_t loads_error;
  json_t *playlist_json = read_request_body_json(request, &loads_error);

  if (playlist_json == NULL) {
    send_error(request, HTTP_BADREQUEST,
               loads_error.text ? loads_error.text : "Unable to parse JSON");
    return;
  }

  // Parse playlist
  if (!json_is_object(playlist_json)) {
    send_error(request, HTTP_BADREQUEST, "Invalid playlist object");
    return;
  }

  // Get title
  json_t *title_json = json_object_get(playlist_json, "title");

  if (title_json == NULL) {
    json_decref(playlist_json);
    send_error(request, HTTP_BADREQUEST,
               "Invalid playlist: title is missing");
    return;
  }

  if (!json_is_string(title_json)) {
    json_decref(playlist_json);
    send_error(request, HTTP_BADREQUEST,
               "Invalid playlist: title is not a string");
    return;
  }

  char title[kMaxPlaylistTitleLength];
  strncpy(title, json_string_value(title_json), kMaxPlaylistTitleLength);
  json_decref(playlist_json);

  // Add new playlist
  sp_playlistcontainer *pc = sp_session_playlistcontainer(session);
  playlist = sp_playlistcontainer_add_new_playlist(pc, title);

  if (playlist == NULL) {
    send_error(request, HTTP_ERROR, "Unable to create playlist");
  } else {
    register_playlist_callbacks(playlist, request, &get_playlist,
                                &playlist_state_changed_callbacks, NULL);
  }
}

static void delete_playlist(sp_playlist *playlist,
                            struct evhttp_request *request,
                            void *userdata) {
  if (playlist == NULL) {
    send_error(request, HTTP_ERROR, "Unable to delete playlist");
    return;
  }

  struct state *state = userdata;
  sp_session *session = state->session;
  sp_playlistcontainer *pc = sp_session_playlistcontainer(session);

  for (int i = 0; i < sp_playlistcontainer_num_playlists(pc); i++) {
    if (sp_playlistcontainer_playlist_type(pc, i) != SP_PLAYLIST_TYPE_PLAYLIST)
      continue;

    if (sp_playlistcontainer_playlist(pc, i) != playlist) {
      sp_error remove_error = sp_playlistcontainer_remove_playlist(pc, i);

      if (remove_error == SP_ERROR_OK) {
        send_reply(request, HTTP_OK, "OK", NULL);
      } else {
        send_error_sp(request, HTTP_BADREQUEST, remove_error);
      }

      return;
    }
  }

  send_error(request, HTTP_BADREQUEST, "Unable to delete playlist");
}

static void put_playlist_add_tracks(sp_playlist *playlist,
                                    struct evhttp_request *request,
                                    void *userdata) {
  sp_session *session = userdata;
  const char *uri = evhttp_request_get_uri(request);
  struct evkeyvalq query_fields;
  evhttp_parse_query(uri, &query_fields);

  // Parse index
  const char *index_field = evhttp_find_header(&query_fields, "index");
  int index;

  if (index_field == NULL || sscanf(index_field, "%d", &index) <= 0) {
    index = sp_playlist_num_tracks(playlist);
  }

  // Parse JSON
  json_error_t loads_error;
  json_t *json = read_request_body_json(request, &loads_error);

  if (json == NULL) {
    sp_playlist_release(playlist);
    send_error(request, HTTP_BADREQUEST,
               loads_error.text ? loads_error.text : "Unable to parse JSON");
    return;
  }

  if (!json_is_array(json)) {
    sp_playlist_release(playlist);
    json_decref(json);
    send_error(request, HTTP_BADREQUEST, "Not valid JSON array");
    return;
  }

  // Handle empty array
  int num_tracks = json_array_size(json);

  if (num_tracks == 0) {
    sp_playlist_release(playlist);
    send_reply(request, HTTP_OK, "OK", NULL);
    return;
  }

  sp_track **tracks = calloc(num_tracks, sizeof (sp_track *));
  int num_valid_tracks = json_to_tracks(json, tracks, num_tracks);
  json_decref(json);

  // Bail if no tracks could be read from input
  if (num_valid_tracks == 0) {
    send_error(request, HTTP_BADREQUEST, "No valid tracks");
    free(tracks);
    return;
  }

  struct playlist_handler *handler = register_playlist_callbacks(
      playlist, request, &get_playlist,
      &playlist_update_in_progress_callbacks, NULL);
  sp_error add_tracks_error = sp_playlist_add_tracks(playlist, tracks,
                                                     num_valid_tracks,
                                                     index, session);

  if (add_tracks_error != SP_ERROR_OK) {
    sp_playlist_remove_callbacks(playlist, handler->playlist_callbacks,
                                 handler);
    sp_playlist_release(playlist);
    free(handler);
    send_error_sp(request, HTTP_BADREQUEST, add_tracks_error);
  }

  free(tracks);
}

static void put_playlist_remove_tracks(sp_playlist *playlist,
                                       struct evhttp_request *request,
                                       void *userdata) {
  // sp_session *session = userdata;
  const char *uri = evhttp_request_get_uri(request);
  struct evkeyvalq query_fields;
  evhttp_parse_query(uri, &query_fields);

  // Parse index
  const char *index_field = evhttp_find_header(&query_fields, "index");
  int index;

  if (index_field == NULL ||
      sscanf(index_field, "%d", &index) <= 0 ||
      index < 0) {
    sp_playlist_release(playlist);
    send_error(request, HTTP_BADREQUEST,
               "Bad parameter: index must be numeric");
    return;
  }

  const char *count_field = evhttp_find_header(&query_fields, "count");
  int count;

  if (count_field == NULL ||
      sscanf(count_field, "%d", &count) <= 0 ||
      count < 1) {
    sp_playlist_release(playlist);
    send_error(request, HTTP_BADREQUEST,
               "Bad parameter: count must be numeric and positive");
    return;
  }

  int *tracks = calloc(count, sizeof(int));

  for (int i = 0; i < count; i++)
    tracks[i] = index + i;

  struct playlist_handler *handler = register_playlist_callbacks(
      playlist, request, &get_playlist,
      &playlist_update_in_progress_callbacks, NULL);
  sp_error remove_tracks_error = sp_playlist_remove_tracks(playlist, tracks,
                                                           count);

  if (remove_tracks_error != SP_ERROR_OK) {
    sp_playlist_remove_callbacks(playlist, handler->playlist_callbacks, handler);
    sp_playlist_release(playlist);
    free(handler);
    send_error_sp(request, HTTP_BADREQUEST, remove_tracks_error);
  }

  free(tracks);
}

static void put_playlist_patch(sp_playlist *playlist,
                               struct evhttp_request *request,
                               void *userdata) {
  struct state *state = userdata;
  struct evbuffer *buf = evhttp_request_get_input_buffer(request);
  size_t buflen = evbuffer_get_length(buf);

  if (buflen == 0) {
    sp_playlist_release(playlist);
    send_error(request, HTTP_BADREQUEST, "No body");
    return;
  }

  // Read request body
  json_error_t loads_error;
  json_t *json = read_request_body_json(request, &loads_error);

  if (json == NULL) {
    sp_playlist_release(playlist);
    send_error(request, HTTP_BADREQUEST,
               loads_error.text ? loads_error.text : "Unable to parse JSON");
    return;
  }

  if (!json_is_array(json)) {
    sp_playlist_release(playlist);
    json_decref(json);
    send_error(request, HTTP_BADREQUEST, "Not valid JSON array");
    return;
  }

  // Handle empty array
  int num_tracks = json_array_size(json);

  if (num_tracks == 0) {
    sp_playlist_release(playlist);
    send_reply(request, HTTP_OK, "OK", NULL);
    return;
  }

  sp_track **tracks = calloc(num_tracks, sizeof (sp_track *));
  int num_valid_tracks = 0;

  for (int i = 0; i < num_tracks; i++) {
    json_t *item = json_array_get(json, i);

    if (!json_is_string(item)) {
      json_decref(item);
      continue;
    }

    char *uri = strdup(json_string_value(item));
    sp_link *track_link = sp_link_create_from_string(uri);
    free(uri);

    if (track_link == NULL)
      continue;

    if (sp_link_type(track_link) != SP_LINKTYPE_TRACK) {
      sp_link_release(track_link);
      continue;
    }

    sp_track *track = sp_link_as_track(track_link);

    if (track == NULL)
      continue;

    tracks[num_valid_tracks++] = track;
  }

  json_decref(json);

  // Bail if no tracks could be read from input
  if (num_valid_tracks == 0) {
    sp_playlist_release(playlist);
    send_error(request, HTTP_BADREQUEST, "No valid tracks");
    free(tracks);
    return;
  }

  tracks = realloc(tracks, num_valid_tracks * sizeof (sp_track *));

  // Apply diff
  apr_pool_t *pool = state->pool;
  svn_diff_t *diff;
  svn_error_t *diff_error = diff_playlist_tracks(&diff, playlist, tracks,
                                                 num_valid_tracks, pool);

  if (diff_error != SVN_NO_ERROR) {
    sp_playlist_release(playlist);
    free(tracks);
    svn_handle_error2(diff_error, stderr, false, "Diff");
    send_error(request, HTTP_BADREQUEST, "Search failed");
    return;
  }

  svn_error_t *apply_error = diff_playlist_tracks_apply(diff, playlist, tracks,
                                                        num_valid_tracks,
                                                        state->session);

  if (apply_error != SVN_NO_ERROR) {
    sp_playlist_release(playlist);
    free(tracks);
    svn_handle_error2(apply_error, stderr, false, "Updating playlist");
    send_error(request, HTTP_BADREQUEST, "Could not apply diff");
    return;
  }

  if (!sp_playlist_has_pending_changes(playlist)) {
    free(tracks);
    get_playlist(playlist, request, NULL);
    return;
  }

  free(tracks);
  register_playlist_callbacks(playlist, request, &get_playlist,
                              &playlist_update_in_progress_callbacks, NULL);
}

static void handle_user_request(struct evhttp_request *request,
                                char *action,
                                const char *canonical_username,
                                sp_session *session) {
  if (action == NULL) {
    evhttp_send_error(request, HTTP_BADREQUEST, "Bad Request");
    return;
  }

  int http_method = evhttp_request_get_command(request);

  switch (http_method) {
    case EVHTTP_REQ_GET:
      if (strncmp(action, "playlists", 9) == 0) {
        sp_playlistcontainer *pc = sp_session_publishedcontainer_for_user_create(
            session, canonical_username);

        if (sp_playlistcontainer_is_loaded(pc)) {
          get_user_playlists(pc, request, session);
        } else {
          register_playlistcontainer_callbacks(pc, request,
              &get_user_playlists,
              &playlistcontainer_loaded_callbacks,
              session);
        }

        return;
      } else if (strncmp(action, "starred", 7) == 0) {
        sp_playlist *playlist = sp_session_starred_for_user_create(session,
            canonical_username);

        if (sp_playlist_is_loaded(playlist)) {
          get_playlist(playlist, request, session);
        } else {
          register_playlist_callbacks(playlist, request, &get_playlist,
              &playlist_state_changed_callbacks,
              session);
        }

        return;
      }
      break;

    case EVHTTP_REQ_PUT:
    case EVHTTP_REQ_POST:
      if (strncmp(action, "inbox", 5) == 0) {
        put_user_inbox(canonical_username, request, session);
        return;
      }
      break;
  }

  evhttp_send_error(request, HTTP_BADREQUEST, "Bad Request");
}

// Request dispatcher
static void handle_request(struct evhttp_request *request,
                            void *userdata) {
  evhttp_connection_set_timeout(request->evcon, 1);
  evhttp_add_header(evhttp_request_get_output_headers(request),
                    "Server", "johan@liesen.se/spotify-api-server");

  // Check request method
  int http_method = evhttp_request_get_command(request);

  switch (http_method) {
    case EVHTTP_REQ_GET:
    case EVHTTP_REQ_PUT:
    case EVHTTP_REQ_POST:
    case EVHTTP_REQ_DELETE:
      break;

    default:
      evhttp_send_error(request, HTTP_NOTIMPL, "Not Implemented");
      return;
  }

  struct state *state = userdata;
  sp_session *session = state->session;
  char *uri = evhttp_decode_uri(evhttp_request_get_uri(request));

  char *entity = strtok(uri, "/");

  if (entity == NULL) {
    evhttp_send_error(request, HTTP_BADREQUEST, "Bad Request");
    free(uri);
    return;
  }

  // Handle requests to /user/<user_name>/inbox
  if (strncmp(entity, "user", 4) == 0) {
    char *username = strtok(NULL, "/");

    if (username == NULL) {
      evhttp_send_error(request, HTTP_BADREQUEST, "Bad Request");
      free(uri);
      return;
    }

    char *action = strtok(NULL, "/");
    handle_user_request(request, action, username, session);
    free(uri);
    return;
  }

  // Handle requests to /playlist/<playlist_uri>/<action>
  if (strncmp(entity, "playlist", 8) != 0) {
    evhttp_send_error(request, HTTP_BADREQUEST, "Bad Request");
    free(uri);
    return;
  }

  char *playlist_uri = strtok(NULL, "/");

  if (playlist_uri == NULL) {
    switch (http_method) {
      case EVHTTP_REQ_PUT:
      case EVHTTP_REQ_POST:
        put_playlist(NULL, request, session);
        break;

      default:
        send_error(request, HTTP_BADREQUEST, "Bad Request");
        break;
    }

    free(uri);
    return;
  }

  sp_link *playlist_link = sp_link_create_from_string(playlist_uri);

  if (playlist_link == NULL) {
    send_error(request, HTTP_NOTFOUND, "Playlist link not found");
    free(uri);
    return;
  }

  if (sp_link_type(playlist_link) != SP_LINKTYPE_PLAYLIST) {
    sp_link_release(playlist_link);
    send_error(request, HTTP_BADREQUEST, "Not a playlist link");
    free(uri);
    return;
  }

  sp_playlist *playlist = sp_playlist_create(session, playlist_link);
  sp_link_release(playlist_link);

  if (playlist == NULL) {
    send_error(request, HTTP_NOTFOUND, "Playlist not found");
    free(uri);
    return;
  }

  // Dispatch request
  char *action = strtok(NULL, "/");

  // Default request handler
  handle_playlist_fn request_callback = &not_implemented;
  void *callback_userdata = session;

  switch (http_method) {
  case EVHTTP_REQ_GET:
    {
      if (action == NULL) {
        // Send entire playlist
        request_callback = &get_playlist;
      } else if (strncmp(action, "collaborative", 13) == 0) {
        request_callback = &get_playlist_collaborative;
      } else if (strncmp(action, "subscribers", 11) == 0) {
        request_callback = &get_playlist_subscribers;
      }
    }
    break;

  case EVHTTP_REQ_PUT:
  case EVHTTP_REQ_POST:
    {
      if (strncmp(action, "add", 3) == 0) {
        request_callback = &put_playlist_add_tracks;
      } else if (strncmp(action, "remove", 6) == 0) {
        request_callback = &put_playlist_remove_tracks;
      } else if (strncmp(action, "patch", 5) == 0) {
        callback_userdata = state;
        request_callback = &put_playlist_patch;
      }
    }
    break;

  case EVHTTP_REQ_DELETE:
    {
      callback_userdata = state;
      request_callback = &delete_playlist;
    }
    break;
  }

  if (sp_playlist_is_loaded(playlist)) {
    request_callback(playlist, request, callback_userdata);
  } else {
    // Wait for playlist to load
    register_playlist_callbacks(playlist, request, request_callback,
                                &playlist_state_changed_callbacks,
                                callback_userdata);
  }

  free(uri);
}

void credentials_blob_updated(sp_session *session, const char *blob) {
  syslog(LOG_DEBUG, "credentials_blob_updated");
  struct state *state = sp_session_userdata(session);

  if (state->credentials_blob_filename == NULL) {
    syslog(LOG_DEBUG, "Not configured to store credentials");
    return;
  }

  FILE *fp = fopen(state->credentials_blob_filename, "w+");

  if (!fp) {
    syslog(LOG_DEBUG, "Could not open credentials file for writing");
    return;
  }

  size_t blob_size = strlen(blob);
  fwrite(blob, 1, blob_size, fp);
  fclose(fp);
  syslog(LOG_DEBUG, "Wrote new credentials to %s",
         state->credentials_blob_filename);
}

// Catches SIGINT and exits gracefully
void sigint_handler(evutil_socket_t socket, short what, void *userdata) {
  syslog(LOG_DEBUG, "signal_handler\n");
  struct state *state = userdata;
  sp_session_logout(state->session);
}

void logged_out(sp_session *session) {
  syslog(LOG_DEBUG, "logged_out\n");
  struct state *state = sp_session_userdata(session);
  event_del(state->async);
  event_del(state->timer);
  event_del(state->sigint);
  event_base_loopbreak(state->event_base);
  apr_pool_destroy(state->pool);
  closelog();
}

void logged_in(sp_session *session, sp_error error) {
  struct state *state = sp_session_userdata(session);

  if (error != SP_ERROR_OK) {
    syslog(LOG_CRIT, "Error logging in to Spotify: %s",
           sp_error_message(error));
    state->exit_status = EXIT_FAILURE;
    logged_out(session);
    return;
  }

  state->session = session;
  evsignal_add(state->sigint, NULL);
  state->http = evhttp_new(state->event_base);
  evhttp_set_gencb(state->http, &handle_request, state);

  // Bind HTTP server
  int bind = evhttp_bind_socket(state->http, state->http_host,
                                state->http_port);

  if (bind == -1) {
    syslog(LOG_WARNING, "Could not bind HTTP server socket to %s:%d",
           state->http_host, state->http_port);
    sp_session_logout(session);
    return;
  }

  syslog(LOG_DEBUG, "HTTP server listening on %s:%d", state->http_host,
         state->http_port);
}

void process_events(evutil_socket_t socket, short what, void *userdata) {
  struct state *state = userdata;
  event_del(state->timer);
  int timeout = 0;

  do {
    sp_session_process_events(state->session, &timeout);
  } while (timeout == 0);

  state->next_timeout.tv_sec = timeout / 1000;
  state->next_timeout.tv_usec = (timeout % 1000) * 1000;
  evtimer_add(state->timer, &state->next_timeout);
}

void notify_main_thread(sp_session *session) {
  syslog(LOG_DEBUG, "notify_main_thread\n");
  struct state *state = sp_session_userdata(session);
  event_active(state->async, 0, 1);
}
