# spotify-api-server

Implementation of parts of the [Spotify playlist API](https://github.com/spotify/playlist-api).

Hopefully this will allow for more services around Spotify as it makes editing playlists much easier than using libspotify.

It's a web server (listens at port 1337 by default) that talks to Spotify using libspotify. JSON is assumed as input and output.

> spotify-api-server is an *experiment* with C, libspotify, evented I/O (libevent) and GPL.


## Supported API methods

    GET /playlist/{id} -> <playlist>
    GET /playlist/{id}/collaborative -> {collaborative:<'true' | 'false'>}

    POST /playlist <- {title:<string>} -> <playlist>
    POST /playlist/{id}/add?index <- [<track URI>] -> <playlist>
    POST /playlist/{id}/remove?index&count -> <playlist>
    POST /playlist/{id}/collaborative?enabled=<'true' | 'false'> -> <playlist>

An extension, `patch`, accepts a (JSON) array of track URIs and replaces all tracks in a playlist with those tracks. It's a bit smarter than `add`/`remove` in that it performs a *diff* between the playlist and the new tracks.

    POST /playlist/{id}/patch <- [<track URI>] -> <playlist>


## How to build

Make sure you have the required libraries

* subversion (`libsvn-dev`, I think) and its dependency `libapr1`
* [libevent](http://monkey.org/~provos/libevent/)
* [jansson](http://www.digip.org/jansson/) > 2.0

Update `account.c` with your credentials. A *Spotify premium account is necessary*.

Copy `appkey.c` into the directory and run `make`.
