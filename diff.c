#include <libspotify/api.h>
#include <apr.h>
#include <svn_diff.h>
#include <svn_pools.h>

#include "constants.h"


struct track_tokens_t {
  sp_track **tracks;
  int num_tracks;
  int index;
};

static void init_track_tokens(struct track_tokens_t *src,
                              int num_tracks) {
  src->tracks = calloc(num_tracks, sizeof(sp_track *));
  src->num_tracks = num_tracks;
  src->index = 0;
}

static void fill_track_tokens_from_playlist(struct track_tokens_t *src,
                                            sp_playlist *playlist) {
  int num_tracks = sp_playlist_num_tracks(playlist);
  init_track_tokens(src, num_tracks);

  for (int i = 0; i < num_tracks; i++) {
    sp_track *track = sp_playlist_track(playlist, i);
    sp_track_add_ref(track);
    src->tracks[i] = track;
  }
}

static void fill_track_tokens_from_tracks(struct track_tokens_t *src,
                                          sp_track **tracks,
                                          int num_tracks) {
  init_track_tokens(src, num_tracks);

  for (int i = 0; i < num_tracks; i++) {
    sp_track *track = tracks[i]; 
    sp_track_add_ref(track);
    src->tracks[i] = track;
  }
}

static int datasource_to_index(svn_diff_datasource_e datasource) {
  switch (datasource) {
    case svn_diff_datasource_original:
      return 0;

    case svn_diff_datasource_modified:
      return 1;

    case svn_diff_datasource_latest:
      return 2;

    case svn_diff_datasource_ancestor:
      return 3;
  }

  return -1;
}

static svn_error_t* datasource_open(void *baton, 
                                    svn_diff_datasource_e datasource) {
  return SVN_NO_ERROR;
}

static svn_error_t* datasource_close(void *baton,
                                     svn_diff_datasource_e datasource) {
  return SVN_NO_ERROR;
}

static svn_error_t* datasource_get_next_token(apr_uint32_t *hash,
                                              void **token,
                                              void *baton,
                                              svn_diff_datasource_e datasource) {
  struct track_tokens_t *srcs = (struct track_tokens_t *) baton;
  struct track_tokens_t *src = &srcs[datasource_to_index(datasource)];
  *token = NULL;

  if (src->index < src->num_tracks)
    *token = src->tracks[src->index++];

  return SVN_NO_ERROR;
}

static svn_error_t *token_compare(void *baton,
                                  void *ltoken,
                                  void *rtoken,
                                  int *result) {
  sp_track *ltrack = (sp_track *) ltoken,
           *rtrack = (sp_track *) rtoken;
  sp_link *llink = sp_link_create_from_track(ltrack, 0), 
          *rlink = sp_link_create_from_track(rtrack, 0);
  char lbuf[kTrackLinkLength],
       rbuf[kTrackLinkLength];
  sp_link_as_string(llink, lbuf, kTrackLinkLength);
  sp_link_as_string(rlink, rbuf, kTrackLinkLength);
  *result = strcmp(lbuf, rbuf);
  return SVN_NO_ERROR;
}

static void token_discard(void *baton, void *token) {
  // Don't discard single tokens/tracks: not all will enter this function
}

static void discard_track_tokens(struct track_tokens_t *src) {
  for (int i = 0; i < src->num_tracks; i++)
    sp_track_release(src->tracks[i]);
}

static void token_discard_all(void *baton) {
  struct track_tokens_t *srcs = (struct track_tokens_t *) baton;
  discard_track_tokens(&srcs[0]);
  discard_track_tokens(&srcs[1]);
}

static const svn_diff_fns_t diff_playlist_search_vtable = {
  datasource_open,
  datasource_close,
  datasource_get_next_token,
  token_compare,
  token_discard,
  token_discard_all
};

svn_error_t *diff_playlist_tracks(svn_diff_t **diff,
                                  sp_playlist *playlist,
                                  sp_track **tracks,
                                  int num_tracks,
                                  apr_pool_t *pool) {
  sp_playlist_add_ref(playlist);
  struct track_tokens_t sources[2];
  fill_track_tokens_from_playlist(&sources[0], playlist);
  fill_track_tokens_from_tracks(&sources[1], tracks, num_tracks);

  // Run through SVN diff
  apr_pool_t *local_pool = svn_pool_create(pool);
  svn_error_t *result = svn_diff_diff(diff, &sources, 
                                      &diff_playlist_search_vtable, local_pool);
  svn_pool_destroy(local_pool);
  sp_playlist_release(playlist);
  return result;
}

struct output_baton_t {
  sp_session *session;
  sp_playlist *playlist;
  sp_track **tracks;
  int num_tracks;
};

static svn_error_t *wrap_spotify_error(sp_error error) {
  return svn_error_quick_wrap(NULL, sp_error_message(error));
}

svn_error_t *output_diff_modified(void *output_baton,
                                  apr_off_t original_start,
                                  apr_off_t original_length,
                                  apr_off_t modified_start,
                                  apr_off_t modified_length,
                                  apr_off_t latest_start,
                                  apr_off_t latest_length) {
  struct output_baton_t *baton = (struct output_baton_t *) output_baton;

  if (original_length > 0) {
    int tracks[original_length];

    for (int i = 0; i < original_length; i++)
      tracks[i] = modified_start + i;

    sp_error remove_tracks_error = sp_playlist_remove_tracks(baton->playlist,
                                                             tracks,
                                                             original_length);

    if (remove_tracks_error != SP_ERROR_OK)
      return wrap_spotify_error(remove_tracks_error);
  }

  if (modified_length > 0) {
    sp_track *tracks[modified_length];

    for (int i = 0; i < modified_length; i++)
      tracks[i] = baton->tracks[modified_start + i];

    sp_error add_tracks_error = sp_playlist_add_tracks(baton->playlist,
                                                       (sp_track *const *) tracks,
                                                       modified_length,
                                                       modified_start,
                                                       baton->session);

    if (add_tracks_error != SP_ERROR_OK)
      return wrap_spotify_error(add_tracks_error);
  }

  return SVN_NO_ERROR;
}

static const svn_diff_output_fns_t output_fns_vtable = {
  .output_diff_modified = &output_diff_modified
};

svn_error_t *diff_playlist_tracks_apply(svn_diff_t *diff,
                                        sp_playlist *playlist,
                                        sp_track **tracks,
                                        int num_tracks,
                                        sp_session *session) {
  sp_playlist_add_ref(playlist);

  struct output_baton_t *baton = malloc(sizeof (struct output_baton_t));
  baton->playlist = playlist;
  baton->tracks = tracks;
  baton->num_tracks = num_tracks;
  baton->session = session;

  svn_error_t *result = svn_diff_output(diff, baton, &output_fns_vtable);
  
  free(baton);
  sp_playlist_release(playlist);
  return result;
}


void append_track(sp_track *track, svn_stringbuf_t *buf) {
  sp_link *link = sp_link_create_from_track(track, 0);
  char link_buf[kTrackLinkLength];
  sp_link_as_string(link, link_buf, kTrackLinkLength);
  svn_stringbuf_appendcstr(buf, link_buf);
  svn_stringbuf_appendcstr(buf, APR_EOL_STR);
}

void append_tracks(sp_track **tracks, int num_tracks, svn_stringbuf_t *buf) {
  for (int i = 0; i < num_tracks; i++)
    append_track(tracks[i], buf);
}

void append_playlist_tracks(sp_playlist *playlist, svn_stringbuf_t *buf) {
  int num_tracks = sp_playlist_num_tracks(playlist);

  for (int i = 0; i < num_tracks; i++)
    append_track(sp_playlist_track(playlist, i), buf);
}

void append_search_tracks(sp_search *search, svn_stringbuf_t *buf) {
  int num_tracks = sp_search_num_tracks(search);

  for (int i = 0; i < num_tracks; i++)
    append_track(sp_search_track(search, i), buf);
}


svn_error_t *diff_output_stdout(
    svn_stream_t *stream_output,
    svn_diff_t *diff,
    sp_playlist *playlist,
    sp_track **tracks,
    int num_tracks,
    apr_pool_t *pool) {
  svn_stringbuf_t *playlist_buf = svn_stringbuf_create("", pool),
                  *tracks_buf = svn_stringbuf_create("", pool);
  append_playlist_tracks(playlist, playlist_buf);
  append_tracks(tracks, num_tracks, tracks_buf);
  svn_string_t *playlist_str = svn_string_create_from_buf(playlist_buf, pool),
               *tracks_str = svn_string_create_from_buf(tracks_buf, pool);
  return svn_diff_mem_string_output_unified(stream_output,
                                            diff,
                                            "playlist",
                                            "tracks",
                                            "utf-8",
                                            playlist_str,
                                            tracks_str,
                                            pool);

}
