var ffi = require('ffi');
var libspotify = require('./libspotify');
var ref = require('ref');
var util = require('util');

var timeoutPtr = ref.alloc('int');
var timeoutId;

function process_events(session) {
  util.debug('process_events');
  clearTimeout(timeoutId);

  // Very important that this uses async
  do {
    libspotify.sp_session_process_events.async(session, timeoutPtr, function (err, error) {});
  } while (timeoutPtr.deref() === 0);

  timeoutId = setTimeout(process_events, timeoutPtr.deref(), session);
}

// Web app
var app = require('./app');
var server;

var session_config = new libspotify.sp_session_config;
session_config['ref.buffer'].fill(0);
session_config.api_version = 12;
session_config.cache_location = ".cache";
session_config.tracefile = ".tracefile";
session_config.user_agent = "Node.js";

var session_callbacks = new libspotify.sp_session_callbacks;
session_callbacks['ref.buffer'].fill(0);

session_callbacks.logged_in = ffi.Callback('void', [libspotify.sp_sessionPtr, 'int'], function (session, error) {
  util.debug('logged_in: ' + libspotify.CONSTANTS.sp_error[error]);
  app.set('session', session);
  server = app.listen(3000);

  process.on('SIGINT', function () {
    libspotify.sp_session_logout.async(session, function () {
    });
  });
});

session_callbacks.logged_out = ffi.Callback('void', [libspotify.sp_sessionPtr], function (session) {
  util.debug('logged_out');
  clearTimeout(timeoutId);
  timeoutId.unref();
  server.close();
  process.exit();
});

session_callbacks.log_message = ffi.Callback('void', [libspotify.sp_sessionPtr, 'string'], function (session, message) {
  util.debug('log_message: ' + message.trim());
});

session_callbacks.message_to_user = ffi.Callback('void', [libspotify.sp_sessionPtr, 'string'], function (session, message) {
  util.debug('message_to_user: ' + message.trim());
});

session_callbacks.notify_main_thread = ffi.Callback('void', [libspotify.sp_sessionPtr], function (session, error) {
  util.debug('notify_main_thread');
  setImmediate(process_events, session);
});

// logged_in
// logged_out
session_callbacks.metadata_updated = ffi.Callback('void', [libspotify.sp_sessionPtr], function (session) { util.debug('metadata_updated'); });
session_callbacks.connection_error = ffi.Callback('void', [libspotify.sp_sessionPtr, 'int'], function (session, error) { util.debug('connection_error'); });
// message_to_user
// notify_main_thread
session_callbacks.music_delivery = ffi.Callback('int', [libspotify.sp_sessionPtr, 'pointer', 'pointer', 'int'], function (session, audioformat, frames, numFrames) { util.debug('music_delivery'); });
session_callbacks.play_token_lost = ffi.Callback('void', [libspotify.sp_sessionPtr], function (session) { util.debug('play_token_lost'); });
// log_message
session_callbacks.end_of_track = ffi.Callback('void', [libspotify.sp_sessionPtr], function (session) { util.debug('end_of_track'); });
session_callbacks.streaming_error = ffi.Callback('void', [libspotify.sp_sessionPtr, 'int'], function (session, error) { util.debug('streaming_error'); });
session_callbacks.userinfo_updated = ffi.Callback('void', [libspotify.sp_sessionPtr], function (session) { util.debug('userinfo_updated'); });
session_callbacks.start_playback = ffi.Callback('void', [libspotify.sp_sessionPtr], function (session) { util.debug('start_playback'); });
session_callbacks.stop_playback = ffi.Callback('void', [libspotify.sp_sessionPtr], function (session) { util.debug('stop_playback'); });
session_callbacks.get_audio_buffer_stats = ffi.Callback('void', [libspotify.sp_sessionPtr, 'pointer'], function (session, stats) { util.debug('get_audio_buffer_stats'); });
session_callbacks.offline_status_updated = ffi.Callback('void', [libspotify.sp_sessionPtr], function (session) { util.debug('offline_status_updated'); });
session_callbacks.offline_error = ffi.Callback('void', [libspotify.sp_sessionPtr, 'int'], function (session, error) { util.debug('offline_error'); });
session_callbacks.credentials_blob_updated = ffi.Callback('void', [libspotify.sp_sessionPtr, 'string'], function (session, blob) { util.debug('credentials_blob_updated'); });
session_callbacks.connectionstate_updated = ffi.Callback('void', [libspotify.sp_sessionPtr], function (session) { util.debug('connectionstate_updated'); });
session_callbacks.scrobble_error = ffi.Callback('void', [libspotify.sp_sessionPtr, 'int'], function (session, error) { util.debug('scrobble_error'); });
session_callbacks.private_session_mode_changed = ffi.Callback('void', [libspotify.sp_sessionPtr, 'bool'], function (session, is_private) { util.debug('private_session_mode_changed'); });

session_config.callbacks = session_callbacks.ref();

var argv = require('optimist').
  usage('Usage: $0 -A [path to application key] -u [username] -p [password]').
  demand(['A', 'u', 'p']).
  argv;

var application_key = require('fs').readFileSync(argv.A);
session_config.application_key = application_key;
session_config.application_key_size = +application_key.length;
var sessionPtrPtr = ref.alloc(ref.refType(libspotify.sp_sessionPtr));
var create_error = libspotify.sp_session_create(session_config.ref(), sessionPtrPtr);
util.debug('session_create: ' + libspotify.CONSTANTS.sp_error[create_error]);
var login_error = libspotify.sp_session_login(sessionPtrPtr.deref(), argv.u, argv.p, 0, null);

if (login_error !== libspotify.CONSTANTS.sp_error.SP_ERROR_OK) {
  throw new Error(libspotify.CONSTANTS.sp_error[login_error]);
}

// Keep a reference to session_callbacks so that they won't get GC'd
process.on('exit', function () {
  session_callbacks;
});
