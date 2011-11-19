#ifndef DIFF_H_
#define DIFF_H_

svn_error_t *diff_playlist_tracks(svn_diff_t **,
                                  sp_playlist *,
                                  sp_track **tracks,
                                  int num_tracks,
                                  apr_pool_t *);

svn_error_t *diff_playlist_tracks_apply(svn_diff_t *,
                                        sp_playlist *,
                                        sp_track **tracks,
                                        int num_tracks,
                                        sp_session *);

svn_error_t *diff_output_stdout(svn_stream_t *stream_output,
                                svn_diff_t *diff,
                                sp_playlist *playlist,
                                sp_track **tracks,
                                int num_tracks,
                                apr_pool_t *pool);

#endif 
