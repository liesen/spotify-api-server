# spotify-api-server

Implementation of parts of the [Spotify playlist API](https://github.com/spotify/playlist-api).

Hopefully this will allow for more services around Spotify as it makes editing playlists much easier than using libspotify.

It's a web server (listens at port 1337 by default) that talks to Spotify using libspotify. JSON is assumed as input and output.

> spotify-api-server is an *experiment* with C, libspotify, evented I/O (libevent) and GPL.


## Supported API methods

### Playlists

    GET /user/{username}/playlists -> {playlists:[<playlist>]}
    GET /user/{username}/starred -> <playlist>

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

1. Make sure you have the required libraries:
 * [libspotify](http://developer.spotify.com/en/libspotify/)
 * subversion (libsvn-dev) and its dependency, libapr
 * [libevent2](http://monkey.org/~provos/libevent/)
 * [jansson](http://www.digip.org/jansson/) 2.x
1. Run `make`.

## How to run

Necessary requirements:

* A *Spotify premium account*.
* An application key file in binary format. Get it [from Spotify](https://developer.spotify.com/technologies/libspotify/keys/).

Start the server:

    ./server --application-key <path to appkey> --username <username> --password <password>`

Read the source for more command line arguments, like setting the cache location (`-C`), which port to listen on (`-P`) or how to login without using a password (`-k`).
