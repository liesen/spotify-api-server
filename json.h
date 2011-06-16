#ifndef JSON_H_
#define JSON_H_

json_t *playlist_to_json(sp_playlist *, json_t *);

json_t *playlist_to_json_set_collaborative(sp_playlist *, json_t *);

// Read track URI into Spotify track
bool json_to_track(json_t *json, sp_track **track);

// Read tracks from an JSON array of track URIs
int json_to_tracks(json_t *json, sp_track **tracks, int num_tracks);

#endif
