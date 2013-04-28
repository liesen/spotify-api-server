var express = require('express');
var async = require('async');
var ffi = require('ffi');
var ref = require('ref');
var libspotify = require('./libspotify');

var app = module.exports = express();

app.use(express.bodyParser());

function loadPlaylist(playlist, callback) {
  if (libspotify.sp_playlist_is_loaded(playlist)) {
    return callback(null, playlist);
  }

  var callbacks = new libspotify.sp_playlist_callbacks;
  callbacks['ref.buffer'].fill(0);
  callbacks.playlist_state_changed = ffi.Callback('void', [libspotify.sp_playlistPtr, 'pointer'], function (playlist, userdata) {
    if (libspotify.sp_playlist_is_loaded(playlist)) {
      libspotify.sp_playlist_remove_callbacks(playlist, userdata, userdata);
      callback(null, playlist);
    }
  });

  var error = libspotify.sp_playlist_add_callbacks(playlist, callbacks.ref(), callbacks.ref());

  if (error !== libspotify.CONSTANTS.sp_error.SP_ERROR_OK) {
    return callback(new Error(libspotify.sp_error_message(error)));
  }
}

app.param('playlistUri', function (req, res, next, uri) {
  var session = app.get('session');
  var link = libspotify.sp_link_create_from_string(uri);

  if (!link) {
    return next(new Error('Unknown playlist URI: ' + uri));
  }

  var playlist = libspotify.sp_playlist_create(session, link);
  libspotify.sp_link_release(link);

  async.waterfall([
    function (callback) {
      loadPlaylist(playlist, callback);
    },
    function (playlist, callback) {
      req.playlist = playlist;
      res.on('finish', function () {
        libspotify.sp_playlist_release(playlist);
      });
      callback(null);
    }
  ], next);
});

var TracksArray = require('ref-array')(libspotify.sp_trackPtr);

function playlistAsJson(playlist) {
  var owner = libspotify.sp_playlist_owner(playlist);
  var username = libspotify.sp_user_display_name(owner);
  libspotify.sp_user_release(owner);

  var numTracks = libspotify.sp_playlist_num_tracks(playlist);
  var tracks = [];

  for (var i = 0; i < numTracks; i++) {
    var track = libspotify.sp_playlist_track(playlist, i);
    tracks.push(getTrackUri(track));
  }

  return {
    owner: username,
    title: libspotify.sp_playlist_name(playlist),
    description: libspotify.sp_playlist_get_description(playlist),
    subscriberCount: libspotify.sp_playlist_num_subscribers(playlist),
    tracks: tracks
  };
}

app.post('/playlists/:playlistUri/add', function (req, res, next) {
  var session = app.get('session');
  var str = 'spotify:track:67N4STz5lAYuxOgR66oN06';

  async.waterfall([
    function (callback) {
      libspotify.sp_link_create_from_string.async(str, callback);
    },
    function (link, callback) {
      async.waterfall([
        function (callback) {
          libspotify.sp_link_as_track.async(link, callback);
        },
        function (track, callback) {
          var tracks = new TracksArray(1);
          tracks[0] = track;
          console.log('track', track);
          console.log('track.name', libspotify.sp_track_name(track));
          console.log('tracks', tracks);
          console.log('tracks[0]', tracks[0]);
          libspotify.sp_playlist_add_tracks.async(req.playlist, tracks.ref(), 1, 0, session, callback);
        }
      ], function (err, ok) {
        console.log('done', err, ok);
        libspotify.sp_link_release.async(link, function (e) {
          next(err || e);
        });
      });
    }
  ], function (err) {
    console.log('fail to get link');
    next(err);
  });
});

app.post('/playlists', function (req, res, next) {
  var session = app.get('session');
  var title = req.param('title');
  var pc = libspotify.sp_session_playlistcontainer(session);
  var playlist = libspotify.sp_playlistcontainer_add_new_playlist(pc, title);

  if (playlist == null) {
    return next(new Error('Unable to create playlist'));
  }

  loadPlaylist(playlist, function (err, playlist) {
    if (err) {
      return next(err);
    }

    res.json(playlistAsJson(playlist));
  });
});

function getTrackUri(track) {
  var uri = new Buffer(37);
  uri.type = ref.refType(ref.types.char);
  var link = libspotify.sp_link_create_from_track(track, 0);
  var len = libspotify.sp_link_as_string(link, uri, uri.length);
  libspotify.sp_link_release(link);
  return uri.readCString(0);
}

app.get('/playlists/:playlistUri', function (req, res, next) {
  res.json(playlistAsJson(req.playlist));
});
