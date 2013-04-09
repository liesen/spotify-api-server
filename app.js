var express = require('express');
var ffi = require('ffi');
var ref = require('ref');
var libspotify = require('./libspotify');

var app = module.exports = express();

app.param('playlistUri', function (req, res, next, uri) {
  var session = app.get('session');
  var link = libspotify.sp_link_create_from_string(uri);

  if (!link) {
    return next(new Error('Unknown playlist URI: ' + uri));
  }

  var playlist = libspotify.sp_playlist_create(session, link);
  libspotify.sp_link_release(link);

  function ok(playlist) {
    req.playlist = playlist;
    res.on('finish', function () {
      libspotify.sp_playlist_release(playlist);
    });
    return next(null);
  }

  if (libspotify.sp_playlist_is_loaded(playlist)) {
    return ok(playlist);
  }

  var callbacks = new libspotify.sp_playlist_callbacks;
  callbacks['ref.buffer'].fill(0);
  callbacks.playlist_state_changed = ffi.Callback('void', [libspotify.sp_playlistPtr, 'pointer'], function (playlist, userdata) {
    if (libspotify.sp_playlist_is_loaded(playlist)) {
      libspotify.sp_playlist_remove_callbacks(playlist, userdata, userdata);
      ok(playlist);
    }
  });

  var error = libspotify.sp_playlist_add_callbacks(playlist, callbacks.ref(), callbacks.ref());

  if (error !== libspotify.CONSTANTS.sp_error.SP_ERROR_OK) {
    return next(new Error(libspotify.sp_error_message(error)));
  }
});

function getTrackUri(track) {
  // TODO(liesen): well this doesn't work...
  var link = libspotify.sp_link_create_from_track(track, 0);
  var uri = new Buffer(37);
  uri.writeCString('spotify:track:40bEw2qJVCnTwQlgNj9yli');
  var len = libspotify.sp_link_as_string(link, uri, uri.length);
  libspotify.sp_link_release(link);
  return uri;
}

app.get('/playlists/:playlistUri', function (req, res) {
  var playlist = req.playlist;
  var name = libspotify.sp_playlist_name(playlist);
  var numTracks = libspotify.sp_playlist_num_tracks(playlist);
  var tracks = [];

  for (var i = 0; i < numTracks; i++) {
    // var track = libspotify.sp_playlist_track(playlist, i);
    // var trackUri = getTrackUri(track);
    // tracks.push(trackUri);
  }

  res.json({
    uri: req.params.playlistUri,
    title: libspotify.sp_playlist_name(playlist),
    description: libspotify.sp_playlist_get_description(playlist),
    subscriberCount: libspotify.sp_playlist_num_subscribers(playlist),
    tracks: tracks
  });
});

