#pragma once
#import <Cocoa/Cocoa.h>
#include "../SubsonicClient.h"
#include "../NavidromeBrowserModel.h"

// ---------------------------------------------------------------------------
// Tree node types for the NSOutlineView
// ---------------------------------------------------------------------------
// Order MUST match navidrome::BrowserNode::Type in NavidromeBrowserModel.h —
// bridged by a plain cast in NavidromeBrowserController.mm.
typedef NS_ENUM(NSInteger, NavidromeNodeType) {
    NavidromeNodeTypeArtist,
    NavidromeNodeTypeAlbum,
    NavidromeNodeTypeSong,
    NavidromeNodeTypeCategory,  // Smart list (Starred, Recently Added, …)
    NavidromeNodeTypePlaylist,  // A playlist stored on the server
    NavidromeNodeTypeGenre,     // A genre from getGenres.view
    NavidromeNodeTypeRadioStation, // A saved internet radio station
    NavidromeNodeTypeLibrary,   // A Navidrome library — shown when 2+ are selected in the filter
    NavidromeNodeTypeLoading,   // Placeholder while loading children
    NavidromeNodeTypeError,     // Placeholder when load fails
};

// Smart-list roots shown above the artist list. Each maps to one Subsonic
// endpoint (see -fetchChildrenOf:). Order MUST match
// navidrome::BrowserNode::CategoryKind in NavidromeBrowserModel.h — the two
// are bridged by a plain cast.
typedef NS_ENUM(NSInteger, NavidromeCategoryKind) {
    NavidromeCategoryStarred,          // getStarred2.view       → songs
    NavidromeCategoryRecentlyAdded,    // getAlbumList2 newest   → albums
    NavidromeCategoryMostPlayed,       // getAlbumList2 frequent → albums
    NavidromeCategoryRecentlyPlayed,   // getAlbumList2 recent   → albums
    NavidromeCategoryRandom,           // getAlbumList2 random   → albums
    NavidromeCategoryGenres,           // getGenres.view         → genres
    NavidromeCategoryPlaylists,        // getPlaylists.view      → playlists
    NavidromeCategoryBookmarks,        // getBookmarks.view      → songs
    NavidromeCategoryRadio,            // getInternetRadioStations.view → stations
};

@interface NavidromeNode : NSObject

@property (nonatomic, assign) NavidromeNodeType type;
@property (nonatomic, copy)   NSString *nodeId;
@property (nonatomic, copy)   NSString *displayName;
@property (nonatomic, copy)   NSString *subtitle;       // artist (for albums/songs)
@property (nonatomic, copy)   NSString *albumName;      // album name (for song nodes)
@property (nonatomic, copy)   NSString *albumId;        // album id (song nodes; startup refresh)
@property (nonatomic, copy)   NSString *libraryId;      // set on artist nodes under a Library node — pins their albums to that library
@property (nonatomic, assign) NSInteger trackNumber;
@property (nonatomic, assign) NSInteger year;
@property (nonatomic, assign) NSTimeInterval duration;
@property (nonatomic, copy)   NSString *coverArtId;
@property (nonatomic, copy)   NSString *suffix;         // codec suffix (mp3/flac/…)
@property (nonatomic, assign) NavidromeCategoryKind categoryKind;  // category nodes only
@property (nonatomic, assign) BOOL      starred;    // server-side favorite
@property (nonatomic, assign) NSInteger rating;     // 0 = unrated, else 1-5
@property (nonatomic, assign) NSTimeInterval bookmarkPositionMs; // > 0 when this song has a saved resume position

// True if children have been loaded (may still be empty)
@property (nonatomic, assign) BOOL childrenLoaded;
// True while async load is in progress
@property (nonatomic, assign) BOOL isLoading;
// Child nodes (albums for artist nodes, songs for album nodes)
@property (nonatomic, strong) NSMutableArray<NavidromeNode *> *children;

// Bridge to the shared C++ tree model (NavidromeBrowserModel.h). The ObjC
// class is the NSOutlineView view-model; browse/fetch/enqueue/label logic runs
// on navidrome::BrowserNode. +wrapCoreNode: builds a view-model from a fetched
// shared node; the model->node mappers all live in NavidromeBrowserModel.h now.
+ (instancetype)wrapCoreNode:(const navidrome::BrowserNode &)core;
- (navidrome::BrowserNode)coreNode;

// The three view-only node kinds the controller still builds directly.
+ (instancetype)songNode:(SubsonicSong *)song;
+ (instancetype)loadingNode;
+ (instancetype)errorNodeWithMessage:(NSString *)msg;

- (BOOL)isLeaf;  // Songs are leaves; artists & albums can expand

@end

// ---------------------------------------------------------------------------
// Browser view controller — embeddable. Used both inside the Media Library
// preferences page (no extra window) and wrapped in a standalone NSWindow
// when invoked from the File menu / library_viewer activate.
// ---------------------------------------------------------------------------

@interface NavidromeBrowserController : NSViewController
                                      <NSOutlineViewDataSource,
                                       NSOutlineViewDelegate,
                                       NSMenuDelegate>
@end

// Wraps a fresh NavidromeBrowserController inside an NSWindow and shows it.
// Retains the window/controller pair internally so they survive until the
// window is closed by the user. Safe to call from any thread.
void NavidromeShowStandaloneBrowser(void);
