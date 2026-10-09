#pragma once
#import <Foundation/Foundation.h>
#include "../../core/stdafx.h"
#include "../../core/SubsonicTypes.h"

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
@property (nonatomic, assign) NSTimeInterval duration;
@property (nonatomic, copy) NSString *coverArtId;
@property (nonatomic, copy) NSString *suffix;
@property (nonatomic, assign) BOOL starred;
@property (nonatomic, assign) NSInteger rating;
@end

@interface SubsonicPlaylist : NSObject
@property (nonatomic, copy) NSString *playlistId;
@property (nonatomic, copy) NSString *name;
@property (nonatomic, copy) NSString *owner;
@property (nonatomic, assign) NSInteger songCount;
@property (nonatomic, assign) NSTimeInterval duration;
@end

@interface SubsonicGenre : NSObject
@property (nonatomic, copy) NSString *name;
@property (nonatomic, assign) NSInteger songCount;
@property (nonatomic, assign) NSInteger albumCount;
@end

@interface SubsonicMusicFolder : NSObject
@property (nonatomic, copy) NSString *folderId;
@property (nonatomic, copy) NSString *name;
@end

@interface SubsonicRadioStation : NSObject
@property (nonatomic, copy) NSString *stationId;
@property (nonatomic, copy) NSString *name;
@property (nonatomic, copy) NSString *streamUrl;
@property (nonatomic, copy) NSString *homePageUrl;
@end

@interface SubsonicBookmark : NSObject
@property (nonatomic, strong) SubsonicSong *song;
@property (nonatomic, assign) NSTimeInterval positionMs;
@property (nonatomic, copy) NSString *comment;
@end

@interface SubsonicPodcastEpisode : NSObject
@property (nonatomic, copy) NSString *episodeId;
@property (nonatomic, copy) NSString *streamId;
@property (nonatomic, copy) NSString *channelId;
@property (nonatomic, copy) NSString *title;
@property (nonatomic, copy) NSString *episodeDescription;
@property (nonatomic, copy) NSString *status;
@property (nonatomic, assign) NSTimeInterval duration;
@end

@interface SubsonicPodcastChannel : NSObject
@property (nonatomic, copy) NSString *channelId;
@property (nonatomic, copy) NSString *url;
@property (nonatomic, copy) NSString *title;
@property (nonatomic, copy) NSString *channelDescription;
@property (nonatomic, copy) NSString *status;
@property (nonatomic, copy) NSString *errorMessage;
@end

@interface SubsonicNowPlayingEntry : NSObject
@property (nonatomic, strong) SubsonicSong *song;
@property (nonatomic, copy) NSString *username;
@property (nonatomic, assign) NSInteger minutesAgo;
@end

@interface SubsonicArtistInfo : NSObject
@property (nonatomic, copy) NSString *biography;
@property (nonatomic, copy) NSString *musicBrainzId;
@property (nonatomic, copy) NSString *lastFmUrl;
@property (nonatomic, copy) NSString *smallImageUrl;
@property (nonatomic, copy) NSString *mediumImageUrl;
@property (nonatomic, copy) NSString *largeImageUrl;
@property (nonatomic, copy) NSArray<SubsonicArtist *> *similarArtists;
@end

typedef NS_ENUM(NSInteger, SubsonicStarKind) {
    SubsonicStarKindSong,
    SubsonicStarKindAlbum,
    SubsonicStarKindArtist,
};

@interface SubsonicClient : NSObject

+ (instancetype)sharedClient;

- (BOOL)isConfigured;

- (BOOL)pingWithError:(NSError **)error;
- (BOOL)serverInfo:(navidrome::ServerInfo &)info error:(std::string &)error;

- (navidrome::Error)lastError;

- (NSArray<SubsonicMusicFolder *> *)getMusicFoldersWithError:(NSError **)error;
- (NSArray<SubsonicMusicFolder *> *)cachedMusicFolders;
- (void)refreshMusicFolders;
- (NSArray<NSString *> *)libraryGroupingIds;

- (NSArray<SubsonicArtist *> *)getArtistsWithError:(NSError **)error;
- (NSArray<SubsonicArtist *> *)getArtistsForLibrary:(NSString *)libraryId
                                              error:(NSError **)error;
- (NSArray<SubsonicAlbum *> *)getAlbumsForArtist:(NSString *)artistId
                                            error:(NSError **)error;
- (NSArray<SubsonicAlbum *> *)getAlbumsForArtist:(NSString *)artistId
                                            error:(NSError **)error
                                   scopeLibrary:(NSString *)scopeLibraryId;
- (NSArray<SubsonicSong *> *)getSongsForAlbum:(NSString *)albumId
                                         error:(NSError **)error;

- (NSDictionary *)search:(NSString *)query error:(NSError **)error;

- (NSArray<SubsonicAlbum *> *)getAlbumListOfType:(NSString *)type
                                            size:(NSInteger)size
                                           error:(NSError **)error;

- (NSArray<SubsonicSong *> *)getStarredSongsWithError:(NSError **)error;

- (NSArray<SubsonicGenre *> *)getGenresWithError:(NSError **)error;
- (NSArray<SubsonicSong *> *)getSongsForGenre:(NSString *)genre
                                        count:(NSInteger)count
                                        error:(NSError **)error;

- (NSArray<SubsonicSong *> *)getSimilarSongsForId:(NSString *)itemId
                                             count:(NSInteger)count
                                             error:(NSError **)error;

- (NSArray<SubsonicSong *> *)getRandomSongsWithCount:(NSInteger)count
                                                error:(NSError **)error;

- (NSArray<SubsonicSong *> *)getAllSongsWithError:(NSError **)error;

- (SubsonicArtistInfo *)getArtistInfoForId:(NSString *)artistId
                                      error:(NSError **)error;

- (NSArray<SubsonicSong *> *)getTopSongsForArtist:(NSString *)artistName
                                             count:(NSInteger)count
                                             error:(NSError **)error;

- (navidrome::Lyrics)getLyricsForSongId:(NSString *)songId
                                 artist:(NSString *)artist
                                  title:(NSString *)title
                                  error:(NSError **)error;

- (BOOL)setStarred:(BOOL)starred
             forId:(NSString *)itemId
              kind:(SubsonicStarKind)kind
             error:(NSError **)error;
- (BOOL)setRating:(NSInteger)rating forSongId:(NSString *)songId error:(NSError **)error;

- (SubsonicSong *)getSongWithId:(NSString *)songId error:(NSError **)error;

- (NSArray<SubsonicPlaylist *> *)getPlaylistsWithError:(NSError **)error;
- (NSArray<SubsonicSong *> *)getPlaylistSongs:(NSString *)playlistId error:(NSError **)error;
- (NSString *)createPlaylistNamed:(NSString *)name
                          songIds:(NSArray<NSString *> *)songIds
                            error:(NSError **)error;
- (BOOL)addSongs:(NSArray<NSString *> *)songIds
      toPlaylist:(NSString *)playlistId
           error:(NSError **)error;
- (BOOL)removeIndexes:(NSArray<NSNumber *> *)indexes
         fromPlaylist:(NSString *)playlistId
                error:(NSError **)error;
- (BOOL)renamePlaylist:(NSString *)playlistId
                toName:(NSString *)name
                 error:(NSError **)error;
- (BOOL)deletePlaylist:(NSString *)playlistId error:(NSError **)error;

- (NSArray<SubsonicRadioStation *> *)getRadioStationsWithError:(NSError **)error;
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

- (NSArray<SubsonicPodcastChannel *> *)getPodcastChannelsWithError:(NSError **)error;
- (NSArray<SubsonicPodcastEpisode *> *)getPodcastEpisodesForChannel:(NSString *)channelId
                                                                error:(NSError **)error;
- (NSString *)createPodcastChannelWithURL:(NSString *)url error:(NSError **)error;
- (BOOL)deletePodcastChannel:(NSString *)channelId error:(NSError **)error;

- (NSArray<SubsonicNowPlayingEntry *> *)getNowPlayingWithError:(NSError **)error;

- (NSArray<SubsonicBookmark *> *)getBookmarksWithError:(NSError **)error;
- (BOOL)createBookmarkForSongId:(NSString *)songId
                      positionMs:(NSTimeInterval)positionMs
                         comment:(NSString *)comment
                           error:(NSError **)error;
- (BOOL)deleteBookmarkForSongId:(NSString *)songId error:(NSError **)error;

- (BOOL)startScanWithScanning:(BOOL *)scanning count:(NSInteger *)count error:(NSError **)error;
- (BOOL)getScanStatusWithScanning:(BOOL *)scanning count:(NSInteger *)count error:(NSError **)error;

- (BOOL)scrobbleSongId:(NSString *)songId
            submission:(BOOL)submission
                 error:(NSError **)error;

- (NSString *)streamURLForSongId:(NSString *)songId coverArtId:(NSString *)coverArtId;
- (NSURL *)coverArtURLForId:(NSString *)coverArtId size:(NSInteger)size;
- (NSURL *)downloadURLForSongId:(NSString *)songId;

- (NSData *)dataForURL:(NSURL *)url error:(NSError **)error;

- (BOOL)downloadURL:(NSURL *)url toPath:(NSString *)path error:(NSError **)error;

@end
