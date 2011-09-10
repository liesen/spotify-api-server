#include <assert.h>
#include <jansson.h>
#include <libspotify/api.h>
#include <string.h>
#include <stdbool.h>

#include "constants.h"

json_t *track_to_json(sp_track *track, json_t *object) {
  char uri[kTrackLinkLength];
  sp_link *link = sp_link_create_from_track(track, 0);
  sp_link_as_string(link, uri, kTrackLinkLength);
  sp_link_release(link);

  json_object_set_new(object, "uri", json_string_nocheck(uri)); 

  if (!sp_track_is_loaded(track))
    return object;

  const char *name = sp_track_name(track);
  json_object_set_new(object, "title", json_string_nocheck(name)); 
  return object;
}

json_t *playlist_to_json_set_collaborative(sp_playlist *playlist,
                                           json_t *object) {
  bool collaborative = sp_playlist_is_collaborative(playlist);
  json_object_set_new(object, "collaborative",
                      collaborative ? json_true() : json_false());
  return object;
}

json_t *playlist_to_json(sp_playlist *playlist, json_t *object, sp_session *session) {
  assert(sp_playlist_is_loaded(playlist));

  // Owner
  sp_user *owner = sp_playlist_owner(playlist);
  const char *username = sp_user_display_name(owner);
  sp_user_release(owner);
  json_object_set_new_nocheck(object, "creator",
                              json_string_nocheck(username));

  // URI
  size_t playlist_uri_len = strlen("spotify:user:") + strlen(username) + 
                            strlen(":playlist:") +
                            strlen("284on3DVWeAxWkgVuzZKGt") + 1;
  char *playlist_uri = malloc(playlist_uri_len);

  if (playlist_uri == NULL)
    return NULL;

  sp_link *playlist_link = sp_link_create_from_playlist(playlist);

  if (playlist_link == NULL)  // Shouldn't happen; playlist is loaded (?)
    return object;

  sp_link_as_string(playlist_link, playlist_uri, playlist_uri_len);
  sp_link_release(playlist_link);
  json_object_set_new(object, "uri", 
                      json_string_nocheck(playlist_uri));
  free(playlist_uri);

  // Title
  const char *title = sp_playlist_name(playlist);
  json_object_set_new(object, "title",
                      json_string_nocheck(title));

  // Collaborative
  playlist_to_json_set_collaborative(playlist, object);

  // Description
  const char *description = sp_playlist_get_description(playlist);

  if (description != NULL) {
    json_object_set_new(object, "description",
                        json_string_nocheck(description));
  }

  // Number of subscribers
  int num_subscribers = sp_playlist_num_subscribers(playlist);
  json_object_set_new(object, "subscriberCount",
                      json_integer(num_subscribers));

  // Tracks
  json_t *tracks_object = json_array();
  json_object_set_new(object, "tracks", tracks_object);
  char track_uri[kTrackLinkLength];

  for (int i = 0; i < sp_playlist_num_tracks(playlist); i++) {
    json_t *track_object = json_object();
    
    sp_track *track = sp_playlist_track(playlist, i);
    sp_link *track_link = sp_link_create_from_track(track, 0);
    sp_link_as_string(track_link, track_uri, kTrackLinkLength);
    
    // URI Track
    sp_link_release(track_link);
    json_object_set_new_nocheck(track_object, "uri", 
                                json_string_nocheck(track_uri));

    // Title Track 
    const char *title = sp_track_name(track);
    if (title != NULL) {
      json_object_set_new_nocheck(track_object, "title", 
                                  json_string_nocheck(title));
    }
    

    json_t *album_object = json_object();
    json_object_set_new(track_object, "album", album_object);

    // Album
    sp_album *album = sp_track_album(track);

    const char *album_title = sp_album_name(album);
    json_object_set_new_nocheck(album_object, "title", 
                                json_string_nocheck(album_title));


    const byte *album_cover_id = sp_album_cover(album);
    sp_image *image = sp_image_create(session, album_cover_id);

    if (image != NULL) {
      sp_link *image_link = sp_link_create_from_image(image); 
      char image_uri[kImageLinkLength];
      sp_link_as_string(image_link, image_uri, sizeof(image_uri) );

      sp_link_release(image_link);
      json_object_set_new_nocheck(album_object, "image", 
                                  json_string_nocheck(image_uri));
    }

    // Artists
    json_t *artists_object = json_array();
    json_object_set_new(track_object, "artists", artists_object);
    int num_artists = sp_track_num_artists(track);

    for (int i = 0; i < num_artists; i++) {

      json_t *artist_object = json_object();
      sp_artist *artist = sp_track_artist(track, i);
      const char *artist_title = sp_album_name(artist);

      json_object_set_new_nocheck(artist_object, "title", 
                                  json_string_nocheck(artist_title));

      json_array_append(artists_object, artist_object);
    }

    json_array_append(tracks_object, track_object);

  }

  return object;
}

bool json_to_track(json_t *json, sp_track **track) {
  if (!json_is_string(json))
    return false;

  sp_link *link = sp_link_create_from_string(json_string_value(json));

  if (link == NULL)
    return false;

  if (sp_link_type(link) != SP_LINKTYPE_TRACK) {
    sp_link_release(link);
    return false;
  }

  *track = sp_link_as_track(link);
  return *track != NULL;
}

int json_to_tracks(json_t *json, sp_track **tracks, int num_tracks) {
  if (!json_is_array(json))
    return 0;

  int num_valid_tracks = 0;

  for (int i = 0; i < num_tracks; i++) {
    json_t *item = json_array_get(json, i);

    if (json_to_track(item, &tracks[num_valid_tracks]))
      num_valid_tracks++;
  }

  return num_valid_tracks;
}
