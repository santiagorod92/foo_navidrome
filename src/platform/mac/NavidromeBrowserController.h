#pragma once
#import <Cocoa/Cocoa.h>
#include "SubsonicClient.h"
#include "../../core/NavidromeBrowserModel.h"

typedef NS_ENUM(NSInteger, NavidromeNodeType) {
    NavidromeNodeTypeArtist,
    NavidromeNodeTypeAlbum,
    NavidromeNodeTypeSong,
    NavidromeNodeTypeCategory,
    NavidromeNodeTypePlaylist,
    NavidromeNodeTypeGenre,
    NavidromeNodeTypeRadioStation,
    NavidromeNodeTypeLibrary,
    NavidromeNodeTypePodcastChannel,
    NavidromeNodeTypeLoading,
    NavidromeNodeTypeError,
};

typedef NS_ENUM(NSInteger, NavidromeCategoryKind) {
    NavidromeCategoryStarred,
    NavidromeCategoryRecentlyAdded,
    NavidromeCategoryMostPlayed,
    NavidromeCategoryRecentlyPlayed,
    NavidromeCategoryRandom,
    NavidromeCategoryGenres,
    NavidromeCategoryPlaylists,
    NavidromeCategoryBookmarks,
    NavidromeCategoryRadio,
    NavidromeCategoryPodcasts,
    NavidromeCategoryNowPlaying,
    NavidromeCategoryArtistTopSongs,
    NavidromeCategoryArtistSimilarArtists,
    NavidromeCategoryAllSongs,
};

@interface NavidromeNode : NSObject

@property (nonatomic, assign) NavidromeNodeType type;
@property (nonatomic, copy)   NSString *nodeId;
@property (nonatomic, copy)   NSString *displayName;
@property (nonatomic, copy)   NSString *subtitle;
@property (nonatomic, copy)   NSString *albumName;
@property (nonatomic, copy)   NSString *albumId;
@property (nonatomic, copy)   NSString *libraryId;
@property (nonatomic, assign) NSInteger trackNumber;
@property (nonatomic, assign) NSInteger year;
@property (nonatomic, assign) NSTimeInterval duration;
@property (nonatomic, copy)   NSString *coverArtId;
@property (nonatomic, copy)   NSString *suffix;
@property (nonatomic, assign) NavidromeCategoryKind categoryKind;
@property (nonatomic, assign) BOOL      starred;
@property (nonatomic, assign) NSInteger rating;
@property (nonatomic, assign) NSTimeInterval bookmarkPositionMs;
@property (nonatomic, copy)   NSString *infoText;

@property (nonatomic, assign) BOOL childrenLoaded;
@property (nonatomic, assign) BOOL isLoading;
@property (nonatomic, strong) NSMutableArray<NavidromeNode *> *children;

+ (instancetype)wrapCoreNode:(const navidrome::BrowserNode &)core;
- (navidrome::BrowserNode)coreNode;

+ (instancetype)songNode:(SubsonicSong *)song;
+ (instancetype)loadingNode;
+ (instancetype)errorNodeWithMessage:(NSString *)msg;

- (BOOL)isLeaf;
- (BOOL)isAllSongs;

@end

@interface NavidromeBrowserController : NSViewController
                                      <NSOutlineViewDataSource,
                                       NSOutlineViewDelegate,
                                       NSMenuDelegate>
@end

void NavidromeShowStandaloneBrowser(void);
