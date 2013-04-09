var libspotify = require('./lib/libspotify');

// Merge libspotify.libspotify with libspotify
for (var key in libspotify.libspotify) {
  libspotify[key] = libspotify.libspotify[key];
}

delete libspotify.libspotify;
module.exports = libspotify;
