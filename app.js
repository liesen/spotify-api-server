var express = require('express');
var ffi = require('ffi');
var libspotify = require('./libspotify');

var app = module.exports = express();

app.param('playlistUri', function (req, res, next, uri) {
  var sessionPtr = app.get('session');
  var linkPtr = libspotify.sp_link_create_from_string(uri);

  if (!linkPtr) {
    return next(new Error('Unknown playlist URI: ' + uri));
  }

  var playlist = libspotify.sp_playlist_create(sessionPtr, linkPtr);
  libspotify.sp_link_release(linkPtr);

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

app.get('/playlists/:playlistUri', function (req, res) {
  var playlist = req.playlist;
  var name = libspotify.sp_playlist_name(playlist);
  res.json({uri: req.params.playlistUri, name: name});
});

