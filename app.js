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

  if (link.isNull()) {
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
      callback(null);
    }
  ], function (err) {
    res.on('finish', function () {
      libspotify.sp_playlist_release(playlist);
      playlist = null;
    });
    next(err);
  });
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

  var link = libspotify.sp_link_create_from_string(str);
  var track = libspotify.sp_link_as_track(link);
  libspotify.sp_link_release(link);
  var tracks = new TracksArray(1);
  tracks[0] = track;
  console.log('track', track);
  console.log('track.name', libspotify.sp_track_name(track));
  console.log('tracks', tracks);
  console.log('tracks[0]', tracks[0]);
  var e = libspotify.sp_playlist_add_tracks(req.playlist, tracks, 1, 0, session);
  console.log('e', e);
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

app.get('/playlists', function (req, res, next) {
  var session = app.get('session');
  var pc = libspotify.sp_session_playlistcontainer(session);
  playlists(pc, req, res, next);
});

app.get('/users/:username/playlists', function (req, res, next) {
  var session = app.get('session');
  console.log(req.param('username'));
  libspotify.sp_session_publishedcontainer_for_user_create.async(session, req.param('username'), function (err, pc) {
    res.on('finish', function () {
      libspotify.sp_playlistcontainer_release(pc);
    });
    playlists(pc, req, res, next);
  });
});

function playlists(pc, req, res, next) {
  if (!libspotify.sp_playlistcontainer_is_loaded(pc)) {
    return next(new Error('Playlist container not loaded'));
  }

  // Get number of bytes needed to represent the username in a URI
  var owner = libspotify.sp_playlistcontainer_owner(pc);
  var username = libspotify.sp_user_canonical_name(owner);
  var usernameLen = Buffer.byteLength(encodeURIComponent(username));

  // Alloc buffer for playlist URIs
  var uri = new Buffer('spotify:user:'.length + usernameLen + ':playlist:1v59E2WpkirAgZx4X8bYnj'.length + 17);
  uri.type = ref.refType(ref.types.char);

  var numPlaylists = libspotify.sp_playlistcontainer_num_playlists(pc);
  var playlists = [];

  for (var i = 0; i < numPlaylists; i++) {
    if (libspotify.sp_playlistcontainer_playlist_type(pc, i) !==
        libspotify.CONSTANTS.sp_playlist_type.SP_PLAYLIST_TYPE_PLAYLIST) {
      continue;
    }

    var playlist = libspotify.sp_playlistcontainer_playlist(pc, i);
    var link = libspotify.sp_link_create_from_playlist(playlist);
    var len = libspotify.sp_link_as_string(link, uri, uri.length);
    libspotify.sp_link_release(link);
    playlists.push(ref.readCString(uri.slice(0, len), 0));
  }

  res.json({playlists: playlists});
}

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
