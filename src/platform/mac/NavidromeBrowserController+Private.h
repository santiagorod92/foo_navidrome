#pragma once
#import "NavidromeBrowserController.h"
#include "../../core/NavidromeBrowserModel.h"
#include <string>

navidrome::IBrowserClient& NBCBrowserClient();

inline std::string NBCStr(NSString *x) {
    return x ? std::string(x.UTF8String) : std::string();
}

@interface NavidromeBrowserController () <NSSearchFieldDelegate>
{
    NSMutableArray<NavidromeNode *> *_rootNodes;
    long long _treeLoadedAtMs;
    NSString *_reloadNotice;
    BOOL _standalone;
    NSOutlineView *_outlineView;
    NSSearchField *_searchField;
    NSProgressIndicator *_spinner;
    NSTextField *_statusLabel;
    NSMutableArray<NavidromeNode *> *_filteredNodes;
    BOOL _isSearching;
    NSTimer *_searchDebounceTimer;
    NSUInteger _searchGeneration;
    NSMenu *_playlistsMenu;
    NSArray<SubsonicPlaylist *> *_serverPlaylists;
    BOOL _playlistsLoading;
    NSArray<SubsonicRadioStation *> *_radioStations;
    NSMenuItem *_alchemyItem;
}
@property (nonatomic, strong) NSMutableArray<NavidromeNode *> *rootNodes;
@property (nonatomic, assign) long long treeLoadedAtMs;
@property (nonatomic, copy) NSString *reloadNotice;
@property (nonatomic, assign) BOOL standalone;
@property (nonatomic, strong) NSOutlineView  *outlineView;
@property (nonatomic, strong) NSSearchField  *searchField;
@property (nonatomic, strong) NSProgressIndicator *spinner;
@property (nonatomic, strong) NSTextField    *statusLabel;
@property (nonatomic, strong) NSMutableArray<NavidromeNode *> *filteredNodes;
@property (nonatomic, assign) BOOL isSearching;
@property (nonatomic, strong) NSTimer *searchDebounceTimer;
@property (nonatomic, assign) NSUInteger searchGeneration;
@property (nonatomic, strong) NSMenu *playlistsMenu;
@property (nonatomic, strong) NSArray<SubsonicPlaylist *> *serverPlaylists;
@property (nonatomic, assign) BOOL playlistsLoading;
@property (nonatomic, strong) NSArray<SubsonicRadioStation *> *radioStations;
@property (nonatomic, strong) NSMenuItem *alchemyItem;
- (NSArray<NavidromeNode *> *)selectedNodes;
- (NSMutableArray<NavidromeNode *> *)collectSelectionSongs:(NSArray<NavidromeNode *> *)nodes
                                                     error:(NSError **)outError;
@end

@interface NavidromeBrowserController (Actions)
- (IBAction)starSelection:(id)sender;
- (IBAction)unstarSelection:(id)sender;
- (IBAction)removeBookmarkSelection:(id)sender;
- (void)invalidateBookmarksCategory;
- (void)applyStarred:(BOOL)starred;
- (IBAction)setRatingFromMenu:(NSMenuItem *)item;
- (IBAction)sendActivePlaylist:(id)sender;
- (IBAction)downloadSelection:(id)sender;
- (NSString *)downloadFileNameForNode:(NavidromeNode *)node;
- (void)refreshServerPlaylists;
- (void)menuNeedsUpdate:(NSMenu *)menu;
- (void)collectSelectedSongIds:(void (^)(NSArray<NSString *> *ids, NSError *err))done;
- (IBAction)addSelectionToServerPlaylist:(NSMenuItem *)item;
- (IBAction)newServerPlaylist:(id)sender;
- (IBAction)removeFromPlaylist:(id)sender;
- (IBAction)renamePlaylist:(id)sender;
- (IBAction)deletePlaylist:(id)sender;
- (NavidromeNode *)singleSelectedPlaylist;
- (void)invalidatePlaylistNode:(NSString *)playlistId;
- (void)invalidatePlaylistsCategory;
- (void)refreshRadioStations;
- (SubsonicRadioStation *)radioStationForId:(NSString *)stationId;
- (NavidromeNode *)singleSelectedRadioStation;
- (void)invalidateRadioCategory;
- (IBAction)newRadioStation:(id)sender;
- (IBAction)editRadioStation:(id)sender;
- (IBAction)deleteRadioStation:(id)sender;
- (NavidromeNode *)singleSelectedPodcastChannel;
- (void)invalidatePodcastsCategory;
- (IBAction)subscribePodcast:(id)sender;
- (IBAction)unsubscribePodcast:(id)sender;
- (BOOL)promptForRadioStationWithTitle:(NSString *)title name:(NSString **)outName streamURL:(NSString **)outStreamURL homePageURL:(NSString **)outHomePageURL;
- (BOOL)promptForRadioStationWithTitle:(NSString *)title initialName:(NSString *)initialName initialStreamURL:(NSString *)initialStreamURL initialHomePageURL:(NSString *)initialHomePageURL name:(NSString **)outName streamURL:(NSString **)outStreamURL homePageURL:(NSString **)outHomePageURL;
- (NSString *)promptForText:(NSString *)title message:(NSString *)message initialValue:(NSString *)initial;
@end
