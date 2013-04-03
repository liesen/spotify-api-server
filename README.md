# spotify-api-server

Implementation of parts of the [Spotify playlist API](https://github.com/spotify/playlist-api)
([mirror](https://github.com/liesen/playlist-api)).

Hopefully this will allow for more services around Spotify as it makes editing
playlists much easier than using libspotify.

It's a web server (listens at port 1337 by default) that talks to Spotify using
libspotify. JSON is assumed as input and output.

> spotify-api-server is an *experiment* with C, libspotify, evented I/O (libevent)
and GPL.

## Supported API methods

### Playlists

    GET /user/{username}/playlists -> {playlists:[<playlist>]}
    GET /user/{username}/starred -> <playlist>

    GET /playlist/{uri} -> <playlist>
    GET /playlist/{uri}/collaborative -> {collaborative:<boolean>}
    GET /playlist/{uri}/subscribers -> [<string>]

    POST /playlist <- {title:<string>} -> <playlist>
    POST /playlist/{uri}/add?index <- [<track URI>] -> <playlist>
    POST /playlist/{uri}/remove?index&count -> <playlist>
    POST /playlist/{uri}/collaborative?enabled=<boolean> -> <playlist>
    POST /playlist/{uri}/patch <- [<track URI>] -> <playlist>

    DELETE /playlist/{uri}/delete -> <playlist>


`patch` replaces all tracks in a playlist with as few `add`s and `remove`s as
possible by first performing a *diff* between the playlist and the new
tracks and then applying the changes.

URIs need to be in their fully qualified form, e.g.
`spotify:user:%ce%bb:playlist:0PkJWxqU7Xt0fbvgVlJlkU` (user part is optional)
and `spotify:track:1XlDNpWy8dyEljyRd0RC2J`.

### Inboxes

    POST /user/{username}/inbox <- {message:<string>, tracks:[<track URI>]}

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

Read the source for more command line arguments, like setting the cache location
(`-C`), which port to listen on (`-P`).

### Using credentials to log in

First get a credentials file from Spotify

    ./server -A spotify_appkey.key -u username -p password -k .credentials

Then start the server using the contents of the credentials file

    cat .credentials | xargs ./server -A spotify_appkey.key -u username -c
