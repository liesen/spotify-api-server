#ifndef JSON_H_
#define JSON_H_

json_t *playlist_to_json(sp_playlist *, json_t *);

json_t *playlist_to_json_set_collaborative(sp_playlist *, json_t *);

#endif
