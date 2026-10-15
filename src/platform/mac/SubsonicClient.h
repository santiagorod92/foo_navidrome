#pragma once
#import <Foundation/Foundation.h>
#include "../../core/stdafx.h"
#include "../../core/SubsonicTypes.h"
#include "../../core/SubsonicCore.h"

@interface SubsonicPlaylist : NSObject
@property (nonatomic, copy) NSString *playlistId;
@property (nonatomic, copy) NSString *name;
@property (nonatomic, copy) NSString *owner;
@property (nonatomic, assign) NSInteger songCount;
@property (nonatomic, assign) NSTimeInterval duration;
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

@interface SubsonicClient : NSObject

+ (instancetype)sharedClient;

- (BOOL)isConfigured;

- (BOOL)pingWithError:(NSError **)error;

- (navidrome::SubsonicCore &)core;

- (NSArray<SubsonicMusicFolder *> *)getMusicFoldersWithError:(NSError **)error;
- (void)refreshMusicFolders;

- (NSArray<SubsonicPlaylist *> *)getPlaylistsWithError:(NSError **)error;
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

- (NSString *)createPodcastChannelWithURL:(NSString *)url error:(NSError **)error;
- (BOOL)deletePodcastChannel:(NSString *)channelId error:(NSError **)error;

- (BOOL)deleteBookmarkForSongId:(NSString *)songId error:(NSError **)error;

- (NSURL *)coverArtURLForId:(NSString *)coverArtId size:(NSInteger)size;
- (NSURL *)downloadURLForSongId:(NSString *)songId;

- (NSData *)dataForURL:(NSURL *)url error:(NSError **)error;

- (BOOL)downloadURL:(NSURL *)url toPath:(NSString *)path error:(NSError **)error;

@end
