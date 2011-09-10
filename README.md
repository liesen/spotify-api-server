# spotify-api-server

Implementation of parts of the [Spotify playlist API](https://github.com/spotify/playlist-api).

Hopefully this will allow for more services around Spotify as it makes editing playlists much easier than using libspotify.

It's a web server (listens at port 1337 by default) that talks to Spotify using libspotify. JSON is assumed as input and output.

> spotify-api-server is an *experiment* with C, libspotify, evented I/O (libevent) and GPL.


## Supported API methods

### Playlists

    GET /playlist/{id} -> <playlist>
    GET /playlist/{id}/collaborative -> {collaborative:<boolean>}
    GET /playlist/{id}/subscribers -> [<string>]

    POST /playlist <- {title:<string>} -> <playlist>
    POST /playlist/{id}/add?index <- [<track URI>] -> <playlist>
    POST /playlist/{id}/remove?index&count -> <playlist>
    POST /playlist/{id}/collaborative?enabled=<boolean> -> <playlist>
    POST /playlist/{id}/patch <- [<track URI>] -> <playlist>

`patch` replaces all tracks in a playlist with as few `add`s and `remove`s as possible by first performing a *diff* between the playlist and the new tracks and then applying the changes.

### Inboxes

    POST /user/{user}/inbox <- {message:<string>, tracks:[<track URI>]}

`message` is optional.

## How to build

1. Make sure you have the required libraries
  * libspotify(http://developer.spotify.com/en/libspotify/) > 9
  * subversion (`libsvn-dev`) and its dependency, `libapr`
  * [libevent](http://monkey.org/~provos/libevent/)
  * [jansson](http://www.digip.org/jansson/) > 2.0

1. Update `account.c` with your credentials. A *Spotify premium account is necessary*.

1. Copy `appkey.c` into the directory and run `make`.

