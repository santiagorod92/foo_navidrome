#pragma once
#import <Foundation/Foundation.h>
#include "stdafx.h"

// ---------------------------------------------------------------------------
// Data model objects
// ---------------------------------------------------------------------------

@interface SubsonicArtist : NSObject
@property (nonatomic, copy) NSString *artistId;
@property (nonatomic, copy) NSString *name;
@property (nonatomic, assign) NSInteger albumCount;
@property (nonatomic, copy) NSString *coverArtId;
@property (nonatomic, assign) BOOL starred;
@end

@interface SubsonicAlbum : NSObject
@property (nonatomic, copy) NSString *albumId;
@property (nonatomic, copy) NSString *name;
@property (nonatomic, copy) NSString *artist;
@property (nonatomic, copy) NSString *artistId;
@property (nonatomic, assign) NSInteger songCount;
@property (nonatomic, assign) NSInteger year;
@property (nonatomic, copy) NSString *coverArtId;
@property (nonatomic, assign) BOOL starred;
@end

@interface SubsonicSong : NSObject
@property (nonatomic, copy) NSString *songId;
@property (nonatomic, copy) NSString *title;
@property (nonatomic, copy) NSString *artist;
@property (nonatomic, copy) NSString *artistId;
@property (nonatomic, copy) NSString *album;
@property (nonatomic, copy) NSString *albumId;
@property (nonatomic, assign) NSInteger track;
@property (nonatomic, assign) NSInteger year;
@property (nonatomic, assign) NSTimeInterval duration;  // seconds
@property (nonatomic, copy) NSString *coverArtId;
@property (nonatomic, copy) NSString *suffix;           // mp3, flac, etc.
@property (nonatomic, assign) BOOL starred;
@property (nonatomic, assign) NSInteger rating;         // 0 = unrated, else 1-5
@end

@interface SubsonicPlaylist : NSObject
@property (nonatomic, copy) NSString *playlistId;
@property (nonatomic, copy) NSString *name;
@property (nonatomic, copy) NSString *owner;
@property (nonatomic, assign) NSInteger songCount;
@property (nonatomic, assign) NSTimeInterval duration;
@end

// A genre from getGenres.view. Subsonic calls the genre itself "value" in the
// JSON, not "name".
@interface SubsonicGenre : NSObject
@property (nonatomic, copy) NSString *name;
@property (nonatomic, assign) NSInteger songCount;
@property (nonatomic, assign) NSInteger albumCount;
@end

// An internet radio station (getInternetRadioStations.view). Playback uses
// streamUrl directly — no navidrome:// URI, no transcoding.
@interface SubsonicRadioStation : NSObject
@property (nonatomic, copy) NSString *stationId;
@property (nonatomic, copy) NSString *name;
@property (nonatomic, copy) NSString *streamUrl;
@property (nonatomic, copy) NSString *homePageUrl;
@end

// A saved resume position (getBookmarks.view). Subsonic embeds the full song
// object per bookmark and reports position in milliseconds.
@interface SubsonicBookmark : NSObject
@property (nonatomic, strong) SubsonicSong *song;
@property (nonatomic, assign) NSTimeInterval positionMs;
@property (nonatomic, copy) NSString *comment;
@end

// Item kinds accepted by star.view / unstar.view — Subsonic names the query
// parameter differently per kind (id / albumId / artistId).
typedef NS_ENUM(NSInteger, SubsonicStarKind) {
    SubsonicStarKindSong,
    SubsonicStarKindAlbum,
    SubsonicStarKindArtist,
};

// ---------------------------------------------------------------------------
// Subsonic API client (Singleton)
// ---------------------------------------------------------------------------

@interface SubsonicClient : NSObject

+ (instancetype)sharedClient;

// Returns YES if server URL + credentials are configured
- (BOOL)isConfigured;

// Test connection — returns YES on success, sets *error on failure
- (BOOL)pingWithError:(NSError **)error;

// Browse hierarchy
- (NSArray<SubsonicArtist *> *)getArtistsWithError:(NSError **)error;
- (NSArray<SubsonicAlbum *> *)getAlbumsForArtist:(NSString *)artistId
                                            error:(NSError **)error;
- (NSArray<SubsonicSong *> *)getSongsForAlbum:(NSString *)albumId
                                         error:(NSError **)error;

// Search (returns dict with keys "artists", "albums", "songs")
- (NSDictionary *)search:(NSString *)query error:(NSError **)error;

// Smart lists — getAlbumList2.view "type" (newest / frequent / recent /
// random / starred). Backs the browser's category nodes.
- (NSArray<SubsonicAlbum *> *)getAlbumListOfType:(NSString *)type
                                            size:(NSInteger)size
                                           error:(NSError **)error;

// Starred songs (getStarred2.view). Albums/artists from the same response are
// ignored — the Starred node lists tracks.
- (NSArray<SubsonicSong *> *)getStarredSongsWithError:(NSError **)error;

// Genres (getGenres.view) and their tracks (getSongsByGenre.view). Back the
// browser's "Genres" category node.
- (NSArray<SubsonicGenre *> *)getGenresWithError:(NSError **)error;
- (NSArray<SubsonicSong *> *)getSongsForGenre:(NSString *)genre
                                        count:(NSInteger)count
                                        error:(NSError **)error;

// Favorites + ratings. Both are per-user server-side state, so they show up in
// the Navidrome web UI and every other Subsonic client.
- (BOOL)setStarred:(BOOL)starred
             forId:(NSString *)itemId
              kind:(SubsonicStarKind)kind
             error:(NSError **)error;
// rating 1-5; 0 clears the rating.
- (BOOL)setRating:(NSInteger)rating forSongId:(NSString *)songId error:(NSError **)error;

// Single song lookup (getSong.view). Used to refresh the per-user rating of a
// track that is already playing, without re-browsing its album.
- (SubsonicSong *)getSongWithId:(NSString *)songId error:(NSError **)error;

// Server-side playlists
- (NSArray<SubsonicPlaylist *> *)getPlaylistsWithError:(NSError **)error;
- (NSArray<SubsonicSong *> *)getPlaylistSongs:(NSString *)playlistId error:(NSError **)error;
// Creates a new playlist; songs are sent in order. Returns the new playlist's
// id, or nil on failure. songIds may be empty to create an empty playlist.
- (NSString *)createPlaylistNamed:(NSString *)name
                          songIds:(NSArray<NSString *> *)songIds
                            error:(NSError **)error;
// Appends songs to an existing playlist (updatePlaylist.view songIdToAdd).
- (BOOL)addSongs:(NSArray<NSString *> *)songIds
      toPlaylist:(NSString *)playlistId
           error:(NSError **)error;
// Removes entries by their zero-based position in the playlist. Indexes are
// applied highest-first so earlier removals can't shift the later ones.
- (BOOL)removeIndexes:(NSArray<NSNumber *> *)indexes
         fromPlaylist:(NSString *)playlistId
                error:(NSError **)error;
- (BOOL)renamePlaylist:(NSString *)playlistId
                toName:(NSString *)name
                 error:(NSError **)error;
- (BOOL)deletePlaylist:(NSString *)playlistId error:(NSError **)error;

// Internet radio stations (getInternetRadioStations.view + CRUD). Playback
// uses SubsonicRadioStation.streamUrl directly.
- (NSArray<SubsonicRadioStation *> *)getRadioStationsWithError:(NSError **)error;
// Creates a new station. Subsonic's create endpoint doesn't echo the new
// station's id back (unlike createPlaylist.view), so this returns @"" on
// success and nil on failure — check *error, not the returned string.
- (NSString *)createRadioStationWithStreamURL:(NSString *)streamUrl
                                          name:(NSString *)name
                                   homePageUrl:(NSString *)homePageUrl
                                         error:(NSError **)error;
- (BOOL)updateRadioStation:(NSString *)stationId
                  streamURL:(NSString *)streamUrl
                       name:(NSString *)name
                homePageUrl:(NSString *)homePageUrl
                      error:(NSError **)error;
- (BOOL)deleteRadioStation:(NSString *)stationId error:(NSError **)error;

// Saved resume positions (getBookmarks.view). createBookmark is an upsert —
// Subsonic overwrites any existing bookmark for the same song.
- (NSArray<SubsonicBookmark *> *)getBookmarksWithError:(NSError **)error;
- (BOOL)createBookmarkForSongId:(NSString *)songId
                      positionMs:(NSTimeInterval)positionMs
                         comment:(NSString *)comment
                           error:(NSError **)error;
- (BOOL)deleteBookmarkForSongId:(NSString *)songId error:(NSError **)error;

// Scrobble a play to the server: submission=NO marks "now playing",
// submission=YES registers the play (play count, Last.fm / ListenBrainz).
- (BOOL)scrobbleSongId:(NSString *)songId
            submission:(BOOL)submission
                 error:(NSError **)error;

// URL builders — no network required
// Returns the authenticated HTTP stream URL for foobar2000 to play directly.
// coverArtId is embedded as a query param so the art extractor can retrieve it.
// Carries the configured transcoding preferences (format / maxBitRate).
- (NSString *)streamURLForSongId:(NSString *)songId coverArtId:(NSString *)coverArtId;
// Returns cover art URL (size 0 = original)
- (NSURL *)coverArtURLForId:(NSString *)coverArtId size:(NSInteger)size;
// download.view — always the original file, never transcoded.
- (NSURL *)downloadURLForSongId:(NSString *)songId;

// Synchronous GET of arbitrary URL data with the configured custom HTTP headers
// applied (used by the album-art extractor). Returns nil + sets *error on failure.
- (NSData *)dataForURL:(NSURL *)url error:(NSError **)error;

// Synchronous download straight to disk — streams to a temp file rather than
// buffering the body, so a full-quality FLAC doesn't sit in memory.
- (BOOL)downloadURL:(NSURL *)url toPath:(NSString *)path error:(NSError **)error;

@end
