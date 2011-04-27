# spotify-api-server

Implementation of parts of the [Spotify playlist API](https://github.com/spotify/playlist-api).

Hopefully this will allow for more services around Spotify as it makes editing playlists much easier than using libspotify.

It's a web server (listens at port 1337 by default) that talks to Spotify using libspotify. JSON is assumed as input and output.

> spotify-api-server is an *experiment* with C, libspotify, evented I/O (libev) and GPL.


## Supported API methods

    GET /playlist/{id}
    GET /playlist/{id}/collaborative

    POST /playlist/{id}/add
    POST /playlist/{id}/remove
    POST /playlist/{id}/collaborative

An extension, `patch`, accepts a (JSON) array of track URIs and replaces all tracks in a playlist with those tracks. It's a bit smarter than `add`/`remove` in that it performs a *diff* between the playlist and the new tracks.

    POST /playlist/{id}/patch


## How to build

Make sure you have the required libraries

* `subversion` (`libsvn-dev`, I think) and its dependency `libapr1`

Copy `appkey.c` into the directory and run `make`.
