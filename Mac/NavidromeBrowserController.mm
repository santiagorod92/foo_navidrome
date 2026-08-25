#import "NavidromeBrowserController.h"
#import "../NavidromeInput.h"
#include "../SubsonicTypes.h"
#include <SDK/playlist.h>
#include <SDK/metadb.h>
#include <SDK/playable_location.h>
#include <SDK/playback_control.h>

// ---------------------------------------------------------------------------
// Helper: format seconds as M:SS
// ---------------------------------------------------------------------------
static NSString *formatDuration(NSTimeInterval secs) {
    int s = (int)secs;
    return [NSString stringWithFormat:@"%d:%02d", s / 60, s % 60];
}

// ---------------------------------------------------------------------------
// NavidromeNode
// ---------------------------------------------------------------------------

@implementation NavidromeNode

+ (instancetype)artistNode:(SubsonicArtist *)a {
    NavidromeNode *n = [NavidromeNode new];
    n.type        = NavidromeNodeTypeArtist;
    n.nodeId      = a.artistId;
    n.displayName = a.name;
    n.coverArtId  = a.coverArtId;
    n.starred     = a.starred;
    n.children    = [NSMutableArray array];
    return n;
}

+ (instancetype)albumNode:(SubsonicAlbum *)a {
    NavidromeNode *n = [NavidromeNode new];
    n.type        = NavidromeNodeTypeAlbum;
    n.nodeId      = a.albumId;
    n.displayName = a.name;
    n.subtitle    = a.artist;
    n.coverArtId  = a.coverArtId;
    n.starred     = a.starred;
    n.children    = [NSMutableArray array];
    return n;
}

+ (instancetype)songNode:(SubsonicSong *)s {
    NavidromeNode *n = [NavidromeNode new];
    n.type         = NavidromeNodeTypeSong;
    n.nodeId       = s.songId;
    n.displayName  = s.title;
    n.subtitle     = s.artist;
    n.albumName    = s.album;
    n.trackNumber  = s.track;
    n.year         = s.year;
    n.duration     = s.duration;
    n.coverArtId   = s.coverArtId;
    n.suffix       = s.suffix;
    n.starred      = s.starred;
    n.rating       = s.rating;
    n.children     = [NSMutableArray array];
    n.childrenLoaded = YES;  // Songs are always leaves
    return n;
}

+ (instancetype)playlistNode:(SubsonicPlaylist *)p {
    NavidromeNode *n = [NavidromeNode new];
    n.type        = NavidromeNodeTypePlaylist;
    n.nodeId      = p.playlistId;
    n.displayName = p.name;
    n.subtitle    = p.songCount == 1 ? @"1 track"
                  : [NSString stringWithFormat:@"%ld tracks", (long)p.songCount];
    n.children    = [NSMutableArray array];
    return n;
}

+ (instancetype)genreNode:(SubsonicGenre *)g {
    NavidromeNode *n = [NavidromeNode new];
    n.type        = NavidromeNodeTypeGenre;
    n.nodeId      = g.name;   // getSongsByGenre keys off the name, not an id
    n.displayName = g.name;
    n.subtitle    = g.songCount == 1 ? @"1 track"
                  : [NSString stringWithFormat:@"%ld tracks", (long)g.songCount];
    n.children    = [NSMutableArray array];
    return n;
}

+ (instancetype)radioStationNode:(SubsonicRadioStation *)station {
    NavidromeNode *n = [NavidromeNode new];
    n.type         = NavidromeNodeTypeRadioStation;
    n.nodeId       = station.stationId;
    n.displayName  = station.name;
    n.subtitle     = station.homePageUrl;
    n.children     = [NSMutableArray array];
    n.childrenLoaded = YES;  // Radio stations are always leaves
    return n;
}

+ (instancetype)categoryNode:(NavidromeCategoryKind)kind title:(NSString *)title {
    NavidromeNode *n = [NavidromeNode new];
    n.type         = NavidromeNodeTypeCategory;
    n.categoryKind = kind;
    n.displayName  = title;
    n.children     = [NSMutableArray array];
    return n;
}

+ (instancetype)loadingNode {
    NavidromeNode *n = [NavidromeNode new];
    n.type        = NavidromeNodeTypeLoading;
    n.displayName = @"Loading…";
    n.children    = [NSMutableArray array];
    n.childrenLoaded = YES;
    return n;
}

+ (instancetype)errorNodeWithMessage:(NSString *)msg {
    NavidromeNode *n = [NavidromeNode new];
    n.type        = NavidromeNodeTypeError;
    n.displayName = msg;
    n.children    = [NSMutableArray array];
    n.childrenLoaded = YES;
    return n;
}

- (BOOL)isLeaf { return self.type == NavidromeNodeTypeSong ||
                        self.type == NavidromeNodeTypeRadioStation ||
                        self.type == NavidromeNodeTypeLoading ||
                        self.type == NavidromeNodeTypeError; }

@end

// ---------------------------------------------------------------------------
// NavidromeBrowserController
// ---------------------------------------------------------------------------

// Outline view subclass that turns Return / Enter into a "commit" action.
// Key equivalents (default buttons) intercept Return before -keyDown:, so the
// Add button no longer claims @"\r" — this is the only Return handler now.
@interface NavidromeCommitOutlineView : NSOutlineView
@property (nonatomic, copy) void (^onCommit)(void);
@end

@implementation NavidromeCommitOutlineView
- (void)keyDown:(NSEvent *)event {
    NSString *chars = event.charactersIgnoringModifiers;
    unichar c = chars.length ? [chars characterAtIndex:0] : 0;
    if ((c == NSCarriageReturnCharacter || c == NSEnterCharacter) && self.onCommit) {
        self.onCommit();
        return;
    }
    [super keyDown:event];
}

// Right-click acts on the clicked row: if it isn't already part of the
// selection, select just that row so the context-menu actions (which operate on
// -selectedNodes) target what the user clicked. Suppress the menu on empty space.
- (NSMenu *)menuForEvent:(NSEvent *)event {
    NSPoint pt = [self convertPoint:event.locationInWindow fromView:nil];
    NSInteger row = [self rowAtPoint:pt];
    if (row < 0) return nil;
    if (![self.selectedRowIndexes containsIndex:(NSUInteger)row])
        [self selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)row]
          byExtendingSelection:NO];
    return [super menuForEvent:event];
}
@end

@interface NavidromeBrowserController ()
// Root artist nodes
@property (nonatomic, strong) NSMutableArray<NavidromeNode *> *rootNodes;
// YES when hosted in the standalone NSWindow (vs. embedded in the prefs page);
// only then does the Enter shortcut close the window after queueing.
@property (nonatomic, assign) BOOL standalone;
// Controls
@property (nonatomic, strong) NSOutlineView  *outlineView;
@property (nonatomic, strong) NSSearchField  *searchField;
@property (nonatomic, strong) NSProgressIndicator *spinner;
@property (nonatomic, strong) NSTextField    *statusLabel;
// Filtered nodes when searching
@property (nonatomic, strong) NSMutableArray<NavidromeNode *> *filteredNodes;
@property (nonatomic, assign) BOOL isSearching;
// "Add to Navidrome Playlist" submenu, populated from _serverPlaylists when the
// menu opens. Fetching the list on demand would block the main thread, so the
// cache is refreshed in the background at load time and after every mutation.
@property (nonatomic, strong) NSMenu *playlistsMenu;
@property (nonatomic, strong) NSArray<SubsonicPlaylist *> *serverPlaylists;
@property (nonatomic, assign) BOOL playlistsLoading;
// Cached radio stations, refreshed alongside serverPlaylists — lets the
// enqueue path resolve a station's streamUrl from just its id without a
// network round-trip.
@property (nonatomic, strong) NSArray<SubsonicRadioStation *> *radioStations;
@end

@implementation NavidromeBrowserController

- (instancetype)init {
    self = [super initWithNibName:nil bundle:nil];
    if (self) {
        _rootNodes     = [NSMutableArray array];
        _filteredNodes = [NSMutableArray array];
    }
    return self;
}

- (void)loadView {
    NSView *content = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 520, 600)];
    content.wantsLayer = YES;
    self.view = content;
    [self buildUI];
    [self loadArtists];
}

- (void)buildUI {
    NSView *content = self.view;

    // ── Search field (top) ──────────────────────────────────────────────
    _searchField = [NSSearchField new];
    _searchField.translatesAutoresizingMaskIntoConstraints = NO;
    _searchField.placeholderString = @"Search artists, albums, songs…";
    _searchField.target = self;
    _searchField.action = @selector(searchChanged:);
    [content addSubview:_searchField];

    // ── Spinner (top-right corner) ───────────────────────────────────────
    _spinner = [[NSProgressIndicator alloc] init];
    _spinner.translatesAutoresizingMaskIntoConstraints = NO;
    _spinner.style = NSProgressIndicatorStyleSpinning;
    _spinner.controlSize = NSControlSizeSmall;
    [_spinner setDisplayedWhenStopped:NO];
    [content addSubview:_spinner];

    // ── Outline view (center) ────────────────────────────────────────────
    NavidromeCommitOutlineView *outline = [[NavidromeCommitOutlineView alloc] init];
    __weak typeof(self) weakSelf = self;
    outline.onCommit = ^{ [weakSelf commitSelectionFromKeyboard]; };
    _outlineView = outline;
    _outlineView.dataSource = self;
    _outlineView.delegate   = self;
    _outlineView.usesAlternatingRowBackgroundColors = YES;
    _outlineView.rowHeight = 20.0;
    _outlineView.allowsMultipleSelection = YES;
    _outlineView.autoresizesOutlineColumn = NO;
    _outlineView.target = self;
    _outlineView.doubleAction = @selector(doubleClicked:);

    // Right-click context menu — mirrors the bottom buttons for a native feel.
    NSMenu *rowMenu = [[NSMenu alloc] init];
    NSMenuItem *playItem = [rowMenu addItemWithTitle:@"Play Now"
                                              action:@selector(playNow:)
                                       keyEquivalent:@""];
    playItem.target = self;
    NSMenuItem *addItem = [rowMenu addItemWithTitle:@"Add to Playlist"
                                             action:@selector(addToPlaylist:)
                                      keyEquivalent:@""];
    addItem.target = self;

    // Server-side favorites + ratings. Both are per-user state on Navidrome, so
    // they show up in its web UI and in every other Subsonic client.
    [rowMenu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *starItem = [rowMenu addItemWithTitle:@"Star"
                                              action:@selector(starSelection:)
                                       keyEquivalent:@""];
    starItem.target = self;
    NSMenuItem *unstarItem = [rowMenu addItemWithTitle:@"Unstar"
                                                action:@selector(unstarSelection:)
                                         keyEquivalent:@""];
    unstarItem.target = self;

    NSMenuItem *ratingItem = [rowMenu addItemWithTitle:@"Rating"
                                                action:nil
                                         keyEquivalent:@""];
    NSMenu *ratingMenu = [[NSMenu alloc] init];
    for (NSInteger stars = 0; stars <= 5; stars++) {
        NSString *title = stars == 0 ? @"None"
                        : [@"" stringByPaddingToLength:(NSUInteger)stars * 1
                                            withString:@"★" startingAtIndex:0];
        NSMenuItem *it = [ratingMenu addItemWithTitle:title
                                               action:@selector(setRatingFromMenu:)
                                        keyEquivalent:@""];
        it.target = self;
        it.tag    = stars;
    }
    [rowMenu setSubmenu:ratingMenu forItem:ratingItem];

    // Server playlists. The submenu is filled in -menuNeedsUpdate: from the
    // cached playlist list, so opening the menu never blocks on the network.
    [rowMenu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *addToPlaylistItem = [rowMenu addItemWithTitle:@"Add to Navidrome Playlist"
                                                       action:nil
                                                keyEquivalent:@""];
    _playlistsMenu = [[NSMenu alloc] init];
    _playlistsMenu.delegate = self;
    [rowMenu setSubmenu:_playlistsMenu forItem:addToPlaylistItem];

    NSMenuItem *removeItem = [rowMenu addItemWithTitle:@"Remove from Playlist"
                                                action:@selector(removeFromPlaylist:)
                                         keyEquivalent:@""];
    removeItem.target = self;
    NSMenuItem *renameItem = [rowMenu addItemWithTitle:@"Rename Playlist…"
                                                action:@selector(renamePlaylist:)
                                         keyEquivalent:@""];
    renameItem.target = self;
    NSMenuItem *deleteItem = [rowMenu addItemWithTitle:@"Delete Playlist…"
                                                action:@selector(deletePlaylist:)
                                         keyEquivalent:@""];
    deleteItem.target = self;

    // Internet radio stations. Unlike playlists, "New" needs no selection.
    [rowMenu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *newRadioItem = [rowMenu addItemWithTitle:@"New Radio Station…"
                                                  action:@selector(newRadioStation:)
                                           keyEquivalent:@""];
    newRadioItem.target = self;
    NSMenuItem *editRadioItem = [rowMenu addItemWithTitle:@"Edit Radio Station…"
                                                   action:@selector(editRadioStation:)
                                            keyEquivalent:@""];
    editRadioItem.target = self;
    NSMenuItem *deleteRadioItem = [rowMenu addItemWithTitle:@"Delete Radio Station…"
                                                     action:@selector(deleteRadioStation:)
                                              keyEquivalent:@""];
    deleteRadioItem.target = self;

    [rowMenu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *uploadItem = [rowMenu addItemWithTitle:@"Send Active Playlist to Navidrome"
                                                action:@selector(sendActivePlaylist:)
                                         keyEquivalent:@""];
    uploadItem.target = self;

    NSMenuItem *downloadItem = [rowMenu addItemWithTitle:@"Download Original Files…"
                                                  action:@selector(downloadSelection:)
                                           keyEquivalent:@""];
    downloadItem.target = self;

    _outlineView.menu = rowMenu;

    // Columns
    NSTableColumn *nameCol = [[NSTableColumn alloc] initWithIdentifier:@"name"];
    nameCol.title = @"Name";
    nameCol.minWidth = 160;
    nameCol.width = 280;
    [_outlineView addTableColumn:nameCol];
    _outlineView.outlineTableColumn = nameCol;

    NSTableColumn *subCol = [[NSTableColumn alloc] initWithIdentifier:@"sub"];
    subCol.title = @"Artist / Album";
    subCol.minWidth = 80;
    subCol.width = 160;
    [_outlineView addTableColumn:subCol];

    NSTableColumn *durCol = [[NSTableColumn alloc] initWithIdentifier:@"dur"];
    durCol.title = @"Duration";
    durCol.minWidth = 50;
    durCol.width = 60;
    [_outlineView addTableColumn:durCol];

    NSScrollView *scrollView = [[NSScrollView alloc] init];
    scrollView.translatesAutoresizingMaskIntoConstraints = NO;
    scrollView.documentView = _outlineView;
    scrollView.hasVerticalScroller = YES;
    scrollView.hasHorizontalScroller = NO;
    scrollView.borderType = NSBezelBorder;
    [content addSubview:scrollView];

    // ── Status label (bottom-left) ───────────────────────────────────────
    _statusLabel = [NSTextField labelWithString:@""];
    _statusLabel.translatesAutoresizingMaskIntoConstraints = NO;
    _statusLabel.textColor = [NSColor secondaryLabelColor];
    _statusLabel.font = [NSFont systemFontOfSize:11];
    [content addSubview:_statusLabel];

    // ── Buttons (bottom-right) ───────────────────────────────────────────
    NSButton *addBtn = [NSButton buttonWithTitle:@"Add to Playlist"
                                          target:self
                                          action:@selector(addToPlaylist:)];
    addBtn.translatesAutoresizingMaskIntoConstraints = NO;
    // Return is handled by the outline view (commit + play + close); don't let
    // the default-button key equivalent steal it.

    NSButton *playBtn = [NSButton buttonWithTitle:@"Play Now"
                                           target:self
                                           action:@selector(playNow:)];
    playBtn.translatesAutoresizingMaskIntoConstraints = NO;

    NSButton *refreshBtn = [NSButton buttonWithTitle:@"Refresh"
                                              target:self
                                              action:@selector(refresh:)];
    refreshBtn.translatesAutoresizingMaskIntoConstraints = NO;

    [content addSubview:addBtn];
    [content addSubview:playBtn];
    [content addSubview:refreshBtn];

    // ── Auto-layout ──────────────────────────────────────────────────────
    CGFloat pad = 10;
    [NSLayoutConstraint activateConstraints:@[
        // Search field
        [_searchField.topAnchor constraintEqualToAnchor:content.topAnchor constant:pad],
        [_searchField.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:pad],
        [_searchField.trailingAnchor constraintEqualToAnchor:_spinner.leadingAnchor constant:-pad],

        // Spinner
        [_spinner.centerYAnchor constraintEqualToAnchor:_searchField.centerYAnchor],
        [_spinner.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-pad],

        // Scroll view
        [scrollView.topAnchor constraintEqualToAnchor:_searchField.bottomAnchor constant:pad],
        [scrollView.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:pad],
        [scrollView.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-pad],
        [scrollView.bottomAnchor constraintEqualToAnchor:addBtn.topAnchor constant:-pad],

        // Bottom row buttons
        [addBtn.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-pad],
        [addBtn.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-pad],

        [playBtn.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-pad],
        [playBtn.trailingAnchor constraintEqualToAnchor:addBtn.leadingAnchor constant:-pad],

        [refreshBtn.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-pad],
        [refreshBtn.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:pad],

        // Status label
        [_statusLabel.centerYAnchor constraintEqualToAnchor:addBtn.centerYAnchor],
        [_statusLabel.leadingAnchor constraintEqualToAnchor:refreshBtn.trailingAnchor constant:pad],
        [_statusLabel.trailingAnchor constraintEqualToAnchor:playBtn.leadingAnchor constant:-pad],
    ]];
}

// ---------------------------------------------------------------------------
// Data loading
// ---------------------------------------------------------------------------

// Smart-list roots, shown above the artist list. Each expands lazily like any
// other node, so opening the browser still costs exactly one getArtists call.
- (NSArray<NavidromeNode *> *)buildCategoryNodes {
    return @[
        [NavidromeNode categoryNode:NavidromeCategoryStarred        title:@"★ Starred"],
        [NavidromeNode categoryNode:NavidromeCategoryRecentlyAdded  title:@"Recently Added"],
        [NavidromeNode categoryNode:NavidromeCategoryMostPlayed     title:@"Most Played"],
        [NavidromeNode categoryNode:NavidromeCategoryRecentlyPlayed title:@"Recently Played"],
        [NavidromeNode categoryNode:NavidromeCategoryRandom         title:@"Random Albums"],
        [NavidromeNode categoryNode:NavidromeCategoryGenres         title:@"Genres"],
        [NavidromeNode categoryNode:NavidromeCategoryPlaylists      title:@"Playlists"],
        [NavidromeNode categoryNode:NavidromeCategoryRadio          title:@"Radio"],
    ];
}

- (void)loadArtists {
    if (![SubsonicClient.sharedClient isConfigured]) {
        _statusLabel.stringValue = @"Not configured — set server in Preferences > Navidrome";
        return;
    }
    [_spinner startAnimation:nil];
    _statusLabel.stringValue = @"Loading artists…";
    [_rootNodes removeAllObjects];
    [_outlineView reloadData];
    // Warm the cache the "Add to Navidrome Playlist" submenu reads from, so the
    // first right-click already lists the server's playlists.
    [self refreshServerPlaylists];
    [self refreshRadioStations];

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        NSArray<SubsonicArtist *> *artists = [SubsonicClient.sharedClient getArtistsWithError:&err];
        dispatch_async(dispatch_get_main_queue(), ^{
            [_spinner stopAnimation:nil];
            if (err || !artists) {
                _statusLabel.stringValue = [NSString stringWithFormat:@"Error: %@", err.localizedDescription ?: @"Unknown"];
                return;
            }
            [_rootNodes addObjectsFromArray:[self buildCategoryNodes]];
            for (SubsonicArtist *a in artists) {
                [_rootNodes addObject:[NavidromeNode artistNode:a]];
            }
            _statusLabel.stringValue = [NSString stringWithFormat:@"%lu artists", (unsigned long)artists.count];
            [_outlineView reloadData];
        });
    });
}

// Synchronous child fetch for any expandable node — background thread only.
// Shared by lazy expansion and the deep song collector so both agree on what a
// category / playlist / artist / album contains.
- (NSMutableArray<NavidromeNode *> *)fetchChildrenOf:(NavidromeNode *)node
                                               error:(NSError **)outError {
    NSMutableArray<NavidromeNode *> *childNodes = [NSMutableArray array];
    SubsonicClient *client = SubsonicClient.sharedClient;

    switch (node.type) {
        case NavidromeNodeTypeArtist: {
            for (SubsonicAlbum *a in [client getAlbumsForArtist:node.nodeId error:outError])
                [childNodes addObject:[NavidromeNode albumNode:a]];
            break;
        }
        case NavidromeNodeTypeAlbum: {
            for (SubsonicSong *s in [client getSongsForAlbum:node.nodeId error:outError])
                [childNodes addObject:[NavidromeNode songNode:s]];
            break;
        }
        case NavidromeNodeTypePlaylist: {
            for (SubsonicSong *s in [client getPlaylistSongs:node.nodeId error:outError])
                [childNodes addObject:[NavidromeNode songNode:s]];
            break;
        }
        case NavidromeNodeTypeGenre: {
            // getSongsByGenre is paged; 500 covers all but the largest genres
            // and keeps a single request per expansion.
            for (SubsonicSong *s in [client getSongsForGenre:node.nodeId
                                                       count:500
                                                       error:outError])
                [childNodes addObject:[NavidromeNode songNode:s]];
            break;
        }
        case NavidromeNodeTypeCategory: {
            if (node.categoryKind == NavidromeCategoryStarred) {
                for (SubsonicSong *s in [client getStarredSongsWithError:outError])
                    [childNodes addObject:[NavidromeNode songNode:s]];
            } else if (node.categoryKind == NavidromeCategoryPlaylists) {
                for (SubsonicPlaylist *p in [client getPlaylistsWithError:outError])
                    [childNodes addObject:[NavidromeNode playlistNode:p]];
            } else if (node.categoryKind == NavidromeCategoryGenres) {
                for (SubsonicGenre *g in [client getGenresWithError:outError])
                    [childNodes addObject:[NavidromeNode genreNode:g]];
            } else if (node.categoryKind == NavidromeCategoryRadio) {
                for (SubsonicRadioStation *s in [client getRadioStationsWithError:outError])
                    [childNodes addObject:[NavidromeNode radioStationNode:s]];
            } else {
                NSString *type = @"newest";
                if (node.categoryKind == NavidromeCategoryMostPlayed)     type = @"frequent";
                if (node.categoryKind == NavidromeCategoryRecentlyPlayed) type = @"recent";
                if (node.categoryKind == NavidromeCategoryRandom)         type = @"random";
                for (SubsonicAlbum *a in [client getAlbumListOfType:type size:100 error:outError])
                    [childNodes addObject:[NavidromeNode albumNode:a]];
            }
            break;
        }
        default:
            break;
    }

    if (outError && *outError) [childNodes removeAllObjects];
    return childNodes;
}

- (void)loadChildrenOfNode:(NavidromeNode *)node inOutlineView:(NSOutlineView *)ov {
    if (node.childrenLoaded || node.isLoading) return;
    node.isLoading = YES;

    // Insert temporary "Loading…" placeholder
    [node.children removeAllObjects];
    [node.children addObject:[NavidromeNode loadingNode]];
    [ov reloadItem:node reloadChildren:YES];

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        NSMutableArray<NavidromeNode *> *childNodes = [self fetchChildrenOf:node error:&err];

        dispatch_async(dispatch_get_main_queue(), ^{
            node.isLoading = NO;
            node.childrenLoaded = YES;
            [node.children removeAllObjects];
            if (err) {
                [node.children addObject:[NavidromeNode errorNodeWithMessage:
                    [NSString stringWithFormat:@"Error: %@", err.localizedDescription]]];
            } else {
                [node.children addObjectsFromArray:childNodes];
            }
            [ov reloadItem:node reloadChildren:YES];
        });
    });
}

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

- (void)searchChanged:(id)sender {
    NSString *query = [_searchField stringValue];
    if (query.length < 2) {
        _isSearching = NO;
        [_filteredNodes removeAllObjects];
        [_outlineView reloadData];
        return;
    }

    _isSearching = YES;
    [_spinner startAnimation:nil];
    _statusLabel.stringValue = @"Searching…";

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        NSDictionary *results = [SubsonicClient.sharedClient search:query error:&err];
        dispatch_async(dispatch_get_main_queue(), ^{
            [_spinner stopAnimation:nil];
            [_filteredNodes removeAllObjects];

            if (err || !results) {
                _statusLabel.stringValue = [NSString stringWithFormat:@"Search error: %@", err.localizedDescription];
                [_outlineView reloadData];
                return;
            }

            // Build flat list of song nodes matching the search
            NSArray<SubsonicSong *> *songs = results[@"songs"];
            for (SubsonicSong *s in songs)
                [_filteredNodes addObject:[NavidromeNode songNode:s]];

            NSUInteger total = [results[@"artists"] count] + [results[@"albums"] count] + songs.count;
            _statusLabel.stringValue = [NSString stringWithFormat:@"%lu songs found", (unsigned long)songs.count];
            (void)total;

            [_outlineView reloadData];
        });
    });
}

// ---------------------------------------------------------------------------
// Adding to playlist
// ---------------------------------------------------------------------------

// Returns all selected playable nodes (anything but the loading/error rows).
- (NSArray<NavidromeNode *> *)selectedNodes {
    NSMutableArray<NavidromeNode *> *nodes = [NSMutableArray array];
    NSIndexSet *selected = [_outlineView selectedRowIndexes];
    [selected enumerateIndexesUsingBlock:^(NSUInteger idx, BOOL *stop) {
        NavidromeNode *node = [_outlineView itemAtRow:idx];
        if (node.type != NavidromeNodeTypeLoading &&
            node.type != NavidromeNodeTypeError) {
            [nodes addObject:node];
        }
    }];
    return nodes;
}

// Entry point for Add/Play actions — handles async deep loading for artists/albums.
- (void)addNodesToPlaylist:(NSArray<NavidromeNode *> *)nodes play:(BOOL)play {
    [self addNodesToPlaylist:nodes play:play closeWhenDone:NO];
}

// closeWhenDone closes the standalone window once tracks are queued — used by
// the Enter shortcut ("queue, play, and dismiss"). No-op when embedded.
- (void)addNodesToPlaylist:(NSArray<NavidromeNode *> *)nodes
                      play:(BOOL)play
             closeWhenDone:(BOOL)closeWhenDone {
    [self addNodesToPlaylist:nodes play:play closeWhenDone:closeWhenDone clearFirst:NO];
}

// clearFirst replaces the active playlist's contents instead of appending —
// used by the Enter shortcut ("select artist/album, Enter = play just this").
- (void)addNodesToPlaylist:(NSArray<NavidromeNode *> *)nodes
                      play:(BOOL)play
             closeWhenDone:(BOOL)closeWhenDone
                clearFirst:(BOOL)clearFirst {
    if (nodes.count == 0) {
        _statusLabel.stringValue = @"Select at least one item first";
        return;
    }

    // Fast path: everything is already a song node
    BOOL allSongs = YES;
    for (NavidromeNode *n in nodes)
        if (n.type != NavidromeNodeTypeSong) { allSongs = NO; break; }
    if (allSongs) {
        [self enqueueNodes:nodes play:play clearFirst:clearFirst];
        if (closeWhenDone) [self closeStandaloneWindow];
        return;
    }

    [_spinner startAnimation:nil];
    _statusLabel.stringValue = @"Loading tracks…";

    // Copy nodes list for use on background thread
    NSArray *nodesCopy = [nodes copy];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSMutableArray<NavidromeNode *> *songs = [NSMutableArray array];
        NSError *err = nil;
        for (NavidromeNode *node in nodesCopy) {
            [self collectSongsDeep:node into:songs error:&err];
            if (err) break;
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            [_spinner stopAnimation:nil];
            if (err) {
                _statusLabel.stringValue = [NSString stringWithFormat:@"Error: %@",
                                            err.localizedDescription];
            } else {
                [self enqueueNodes:songs play:play clearFirst:clearFirst];
                if (closeWhenDone) [self closeStandaloneWindow];
            }
        });
    });
}

// Return / Enter in the tree: replace the active playlist with the selection,
// start playing, and close the window (standalone only).
- (void)commitSelectionFromKeyboard {
    [self addNodesToPlaylist:[self selectedNodes] play:YES closeWhenDone:YES clearFirst:YES];
}

- (void)closeStandaloneWindow {
    if (self.standalone) [self.view.window close];
}

// Synchronous deep song collector — must be called from a background thread.
// Walks any expandable node (artist, album, category, playlist) down to songs,
// reusing already-expanded children and fetching the rest on demand.
- (void)collectSongsDeep:(NavidromeNode *)node
                    into:(NSMutableArray<NavidromeNode *> *)songs
                   error:(NSError **)outError {
    if (node.type == NavidromeNodeTypeSong || node.type == NavidromeNodeTypeRadioStation) {
        [songs addObject:node];
        return;
    }
    if (node.type == NavidromeNodeTypeLoading || node.type == NavidromeNodeTypeError)
        return;

    NSArray<NavidromeNode *> *children;
    if (node.childrenLoaded && node.children.count > 0) {
        children = node.children;
    } else {
        children = [self fetchChildrenOf:node error:outError];
        if (outError && *outError) return;
    }

    for (NavidromeNode *child in children) {
        [self collectSongsDeep:child into:songs error:outError];
        if (outError && *outError) return;
    }
}

- (void)enqueueNodes:(NSArray<NavidromeNode *> *)songNodes play:(BOOL)play {
    [self enqueueNodes:songNodes play:play clearFirst:NO];
}

- (void)enqueueNodes:(NSArray<NavidromeNode *> *)songNodes
                play:(BOOL)play
          clearFirst:(BOOL)clearFirst {
    if (songNodes.count == 0) {
        _statusLabel.stringValue = @"No songs selected";
        return;
    }
    // Build metadb handle list. Each item is identified by a navidrome://
    // URI — our input handler resolves it to the current HTTP stream at
    // decode time, so playlists survive credential / server URL changes.
    metadb_handle_list tracks;
    auto hintList = metadb_io_v2::get()->create_hint_list();

    for (NavidromeNode *node in songNodes) {
        metadb_handle_ptr handle;
        playable_location_impl loc;

        if (node.type == NavidromeNodeTypeRadioStation) {
            // Raw stream URL, not a navidrome:// URI — foobar's stock HTTP
            // input plays a live radio stream natively (incl. Shoutcast/
            // Icecast metadata); there's no server-side transcoding or
            // credential resolution to redirect through for radio.
            NSString *streamUrl = [self radioStationForId:node.nodeId].streamUrl;
            if (streamUrl.length == 0) continue;
            loc.set_path([streamUrl UTF8String]);
            loc.set_subsong(0);
            metadb::get()->handle_create(handle, loc);
            tracks += handle;

            file_info_impl info;
            if (node.displayName.length)
                info.meta_set("title", [node.displayName UTF8String]);
            hintList->add_hint(handle, info, filestats_invalid, true);
            continue;
        }

        NSString *uri = NavidromeMakeTrackURIWithFields(node.nodeId,
                                                        node.displayName,
                                                        node.subtitle,
                                                        node.albumName,
                                                        node.trackNumber,
                                                        node.year,
                                                        node.duration,
                                                        node.coverArtId ?: @"",
                                                        node.suffix ?: @"");
        if (!uri) continue;

        loc.set_path([uri UTF8String]);
        loc.set_subsong(0);
        metadb::get()->handle_create(handle, loc);
        tracks += handle;

        // Provide metadata hints so foobar displays correct info immediately
        file_info_impl info;
        if (node.displayName.length)
            info.meta_set("title", [node.displayName UTF8String]);
        if (node.subtitle.length)
            info.meta_set("artist", [node.subtitle UTF8String]);
        if (node.albumName.length)
            info.meta_set("album", [node.albumName UTF8String]);
        if (node.trackNumber > 0)
            info.meta_set("tracknumber", pfc::format_int(node.trackNumber));
        if (node.year > 0)
            info.meta_set("date", pfc::format_int(node.year));
        if (node.duration > 0)
            info.set_length(node.duration);

        hintList->add_hint(handle, info, filestats_invalid, true);
    }

    hintList->on_done();

    auto tracksCopy = std::make_shared<metadb_handle_list>(tracks);
    bool doPlay = play;
    bool doClearFirst = clearFirst;

    fb2k::inMainThread([tracksCopy, doPlay, doClearFirst] {
        auto pm = playlist_manager::get();
        t_size activePlaylist = pm->get_active_playlist();
        if (activePlaylist == pfc_infinite) {
            pm->create_playlist("Navidrome", ~0, pfc_infinite);
            activePlaylist = pm->get_active_playlist();
        }
        if (doClearFirst) pm->playlist_clear(activePlaylist);
        t_size insertPos = pm->playlist_get_item_count(activePlaylist);
        pm->playlist_add_items(activePlaylist, *tracksCopy, pfc::bit_array_false());

        if (doPlay && tracksCopy->get_count() > 0) {
            // Start playback honoring the user's Playback > Order setting
            // (Shuffle, Random, Default, …). track_command_play asks the active
            // playback order for the starting track; the focus biases in-order
            // modes to the first newly-added track. (playlist_execute_default_action
            // would instead pin that exact track and ignore the order.)
            pm->set_active_playlist(activePlaylist);
            pm->set_playing_playlist(activePlaylist);
            pm->playlist_set_focus_item(activePlaylist, insertPos);
            playback_control::get()->start(playback_control::track_command_play);
        }
    });

    _statusLabel.stringValue = [NSString stringWithFormat:@"Added %lu tracks", (unsigned long)songNodes.count];
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

- (IBAction)addToPlaylist:(id)sender {
    [self addNodesToPlaylist:[self selectedNodes] play:NO];
}

- (IBAction)playNow:(id)sender {
    [self addNodesToPlaylist:[self selectedNodes] play:YES];
}

- (IBAction)refresh:(id)sender {
    _isSearching = NO;
    _searchField.stringValue = @"";
    [self loadArtists];
}

// ---------------------------------------------------------------------------
// Favorites, ratings and playlist upload
// ---------------------------------------------------------------------------

- (IBAction)starSelection:(id)sender   { [self applyStarred:YES]; }
- (IBAction)unstarSelection:(id)sender { [self applyStarred:NO]; }

- (void)applyStarred:(BOOL)starred {
    NSMutableArray<NavidromeNode *> *targets = [NSMutableArray array];
    for (NavidromeNode *n in [self selectedNodes]) {
        if (n.type == NavidromeNodeTypeSong ||
            n.type == NavidromeNodeTypeAlbum ||
            n.type == NavidromeNodeTypeArtist)
            [targets addObject:n];
    }
    if (targets.count == 0) {
        _statusLabel.stringValue = @"Select a song, album or artist first";
        return;
    }

    [_spinner startAnimation:nil];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        NSUInteger done = 0;
        for (NavidromeNode *n in targets) {
            SubsonicStarKind kind = SubsonicStarKindSong;
            if (n.type == NavidromeNodeTypeAlbum)  kind = SubsonicStarKindAlbum;
            if (n.type == NavidromeNodeTypeArtist) kind = SubsonicStarKindArtist;
            NSError *one = nil;
            if ([SubsonicClient.sharedClient setStarred:starred forId:n.nodeId
                                                   kind:kind error:&one]) {
                n.starred = starred;
                done++;
            } else if (!err) {
                err = one;
            }
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            [_spinner stopAnimation:nil];
            if (err) {
                _statusLabel.stringValue =
                    [NSString stringWithFormat:@"Error: %@", err.localizedDescription];
            } else {
                _statusLabel.stringValue = [NSString stringWithFormat:@"%@ %lu item(s)",
                    starred ? @"Starred" : @"Unstarred", (unsigned long)done];
            }
            [_outlineView reloadData];
        });
    });
}

// Ratings are a song-level concept in Subsonic; albums/artists are ignored.
- (IBAction)setRatingFromMenu:(NSMenuItem *)item {
    NSInteger rating = item.tag;
    NSMutableArray<NavidromeNode *> *songs = [NSMutableArray array];
    for (NavidromeNode *n in [self selectedNodes])
        if (n.type == NavidromeNodeTypeSong) [songs addObject:n];

    if (songs.count == 0) {
        _statusLabel.stringValue = @"Select one or more songs to rate";
        return;
    }

    [_spinner startAnimation:nil];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        for (NavidromeNode *n in songs) {
            NSError *one = nil;
            if ([SubsonicClient.sharedClient setRating:rating forSongId:n.nodeId error:&one])
                n.rating = rating;
            else if (!err)
                err = one;
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            [_spinner stopAnimation:nil];
            _statusLabel.stringValue = err
                ? [NSString stringWithFormat:@"Error: %@", err.localizedDescription]
                : [NSString stringWithFormat:@"Rated %lu song(s)", (unsigned long)songs.count];
            [_outlineView reloadData];
        });
    });
}

// Pushes the active foobar2000 playlist to the server under the same name, so
// it shows up on phones / the web UI. Only navidrome:// tracks can be sent —
// local files have no Subsonic id.
- (IBAction)sendActivePlaylist:(id)sender {
    auto pm = playlist_manager::get();
    t_size playlist = pm->get_active_playlist();
    if (playlist == pfc_infinite) {
        _statusLabel.stringValue = @"No active playlist";
        return;
    }

    pfc::string8 pfcName;
    pm->playlist_get_name(playlist, pfcName);
    metadb_handle_list items;
    pm->playlist_get_all_items(playlist, items);

    NSMutableArray<NSString *> *songIds = [NSMutableArray array];
    NSUInteger skipped = 0;
    for (t_size i = 0; i < items.get_count(); i++) {
        std::string id = navidrome::trackIdFromURI(items[i]->get_path());
        if (id.empty()) { skipped++; continue; }
        [songIds addObject:[NSString stringWithUTF8String:id.c_str()]];
    }

    if (songIds.count == 0) {
        _statusLabel.stringValue = @"No Navidrome tracks in the active playlist";
        return;
    }

    NSString *name = [NSString stringWithUTF8String:pfcName.c_str()];
    if (name.length == 0) name = @"foobar2000";
    NSUInteger skippedCount = skipped;

    [_spinner startAnimation:nil];
    _statusLabel.stringValue = @"Uploading playlist…";
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        BOOL ok = [SubsonicClient.sharedClient createPlaylistNamed:name
                                                           songIds:songIds
                                                             error:&err] != nil;
        dispatch_async(dispatch_get_main_queue(), ^{
            [_spinner stopAnimation:nil];
            if (!ok) {
                _statusLabel.stringValue =
                    [NSString stringWithFormat:@"Upload failed: %@",
                     err.localizedDescription ?: @"Unknown error"];
                return;
            }
            _statusLabel.stringValue = skippedCount > 0
                ? [NSString stringWithFormat:@"Sent “%@” (%lu tracks, %lu non-Navidrome skipped)",
                   name, (unsigned long)songIds.count, (unsigned long)skippedCount]
                : [NSString stringWithFormat:@"Sent “%@” (%lu tracks)",
                   name, (unsigned long)songIds.count];
            [self invalidatePlaylistsCategory];
            [self refreshServerPlaylists];
        });
    });
}

// ---------------------------------------------------------------------------
// Download originals
//
// download.view always serves the file as stored on the server — the streaming
// transcode preferences deliberately don't apply here.
// ---------------------------------------------------------------------------

- (IBAction)downloadSelection:(id)sender {
    NSArray<NavidromeNode *> *nodes = [self selectedNodes];
    if (nodes.count == 0) { _statusLabel.stringValue = @"Select at least one item first"; return; }

    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = NO;
    panel.canChooseDirectories = YES;
    panel.canCreateDirectories = YES;
    panel.allowsMultipleSelection = NO;
    panel.prompt = @"Download Here";
    panel.message = @"Choose a folder for the downloaded tracks.";
    if ([panel runModal] != NSModalResponseOK || !panel.URL) return;
    NSString *destDir = panel.URL.path;

    [_spinner startAnimation:nil];
    _statusLabel.stringValue = @"Resolving tracks…";

    NSArray *nodesCopy = [nodes copy];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSMutableArray<NavidromeNode *> *songs = [NSMutableArray array];
        NSError *err = nil;
        for (NavidromeNode *n in nodesCopy) {
            [self collectSongsDeep:n into:songs error:&err];
            if (err) break;
        }
        if (err) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [self->_spinner stopAnimation:nil];
                self->_statusLabel.stringValue =
                    [NSString stringWithFormat:@"Error: %@", err.localizedDescription];
            });
            return;
        }

        NSUInteger done = 0, failed = 0;
        for (NSUInteger i = 0; i < songs.count; i++) {
            NavidromeNode *s = songs[i];
            NSUInteger position = i + 1;
            dispatch_async(dispatch_get_main_queue(), ^{
                self->_statusLabel.stringValue =
                    [NSString stringWithFormat:@"Downloading %lu/%lu…",
                     (unsigned long)position, (unsigned long)songs.count];
            });

            NSURL *url = [SubsonicClient.sharedClient downloadURLForSongId:s.nodeId];
            if (!url) { failed++; continue; }

            NSString *path = [destDir stringByAppendingPathComponent:
                              [self downloadFileNameForNode:s]];
            NSError *one = nil;
            if ([SubsonicClient.sharedClient downloadURL:url toPath:path error:&one]) done++;
            else failed++;
        }

        NSUInteger okCount = done, failCount = failed;
        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_spinner stopAnimation:nil];
            self->_statusLabel.stringValue = failCount == 0
                ? [NSString stringWithFormat:@"Downloaded %lu track(s)", (unsigned long)okCount]
                : [NSString stringWithFormat:@"Downloaded %lu, %lu failed",
                   (unsigned long)okCount, (unsigned long)failCount];
        });
    });
}

// "<track>. <artist> - <title>.<suffix>" with anything illegal replaced. The
// suffix comes from the node when known; download.view keeps the original
// container either way, so a missing suffix just means no extension.
- (NSString *)downloadFileNameForNode:(NavidromeNode *)node {
    NSMutableString *name = [NSMutableString string];
    if (node.trackNumber > 0) [name appendFormat:@"%02ld. ", (long)node.trackNumber];
    if (node.subtitle.length) [name appendFormat:@"%@ - ", node.subtitle];
    [name appendString:node.displayName.length ? node.displayName : @"untitled"];

    std::string clean = navidrome::sanitizeFileName([name UTF8String] ?: "untitled");
    NSString *result = [NSString stringWithUTF8String:clean.c_str()];
    if (node.suffix.length) result = [result stringByAppendingFormat:@".%@", node.suffix];
    return result;
}

// ---------------------------------------------------------------------------
// Server playlist management
//
// Everything here works on song ids, so the selection is first resolved down to
// songs (fetching artist/album/genre children when needed) exactly the way the
// Add-to-playlist actions do.
// ---------------------------------------------------------------------------

// Refresh the cached playlist list used by the "Add to Navidrome Playlist"
// submenu. Cheap enough to re-run after every mutation.
- (void)refreshServerPlaylists {
    if (_playlistsLoading || ![SubsonicClient.sharedClient isConfigured]) return;
    _playlistsLoading = YES;
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        NSArray<SubsonicPlaylist *> *lists =
            [SubsonicClient.sharedClient getPlaylistsWithError:&err];
        dispatch_async(dispatch_get_main_queue(), ^{
            _playlistsLoading = NO;
            if (!err && lists) _serverPlaylists = lists;
        });
    });
}

// NSMenuDelegate — fills the submenu from the cache each time it opens.
- (void)menuNeedsUpdate:(NSMenu *)menu {
    if (menu != _playlistsMenu) return;
    [menu removeAllItems];

    for (NSUInteger i = 0; i < _serverPlaylists.count; i++) {
        NSMenuItem *it = [menu addItemWithTitle:_serverPlaylists[i].name
                                         action:@selector(addSelectionToServerPlaylist:)
                                  keyEquivalent:@""];
        it.target = self;
        it.tag    = (NSInteger)i;
    }
    if (_serverPlaylists.count == 0) {
        NSMenuItem *placeholder = [menu addItemWithTitle:
            (_playlistsLoading ? @"Loading…" : @"No playlists on server")
                                                  action:nil keyEquivalent:@""];
        placeholder.enabled = NO;
    }
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *newItem = [menu addItemWithTitle:@"New Playlist…"
                                          action:@selector(newServerPlaylist:)
                                   keyEquivalent:@""];
    newItem.target = self;

    // A stale cache is only visible once — refresh for the next open.
    [self refreshServerPlaylists];
}

// Resolves the current selection to Subsonic song ids on a background thread.
- (void)collectSelectedSongIds:(void (^)(NSArray<NSString *> *ids, NSError *err))done {
    NSArray<NavidromeNode *> *nodes = [self selectedNodes];
    if (nodes.count == 0) {
        _statusLabel.stringValue = @"Select at least one item first";
        return;
    }
    [_spinner startAnimation:nil];
    _statusLabel.stringValue = @"Resolving tracks…";

    NSArray *nodesCopy = [nodes copy];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSMutableArray<NavidromeNode *> *songs = [NSMutableArray array];
        NSError *err = nil;
        for (NavidromeNode *n in nodesCopy) {
            [self collectSongsDeep:n into:songs error:&err];
            if (err) break;
        }
        NSMutableArray<NSString *> *ids = [NSMutableArray array];
        for (NavidromeNode *s in songs)
            if (s.nodeId.length) [ids addObject:s.nodeId];

        dispatch_async(dispatch_get_main_queue(), ^{
            [_spinner stopAnimation:nil];
            done(ids, err);
        });
    });
}

- (IBAction)addSelectionToServerPlaylist:(NSMenuItem *)item {
    NSUInteger idx = (NSUInteger)item.tag;
    if (idx >= _serverPlaylists.count) return;
    SubsonicPlaylist *target = _serverPlaylists[idx];

    [self collectSelectedSongIds:^(NSArray<NSString *> *ids, NSError *err) {
        if (err)       { self->_statusLabel.stringValue =
                             [NSString stringWithFormat:@"Error: %@", err.localizedDescription]; return; }
        if (!ids.count) { self->_statusLabel.stringValue = @"No tracks in the selection"; return; }

        self->_statusLabel.stringValue = @"Adding to playlist…";
        [self->_spinner startAnimation:nil];
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
            NSError *one = nil;
            BOOL ok = [SubsonicClient.sharedClient addSongs:ids
                                                 toPlaylist:target.playlistId
                                                      error:&one];
            dispatch_async(dispatch_get_main_queue(), ^{
                [self->_spinner stopAnimation:nil];
                self->_statusLabel.stringValue = ok
                    ? [NSString stringWithFormat:@"Added %lu track(s) to “%@”",
                       (unsigned long)ids.count, target.name]
                    : [NSString stringWithFormat:@"Failed: %@",
                       one.localizedDescription ?: @"unknown error"];
                if (ok) {
                    [self invalidatePlaylistNode:target.playlistId];
                    [self refreshServerPlaylists];
                }
            });
        });
    }];
}

- (IBAction)newServerPlaylist:(id)sender {
    [self collectSelectedSongIds:^(NSArray<NSString *> *ids, NSError *err) {
        if (err) {
            self->_statusLabel.stringValue =
                [NSString stringWithFormat:@"Error: %@", err.localizedDescription];
            return;
        }
        NSString *name = [self promptForText:@"New Navidrome playlist"
                                     message:@"Name for the new playlist:"
                                initialValue:@""];
        if (name.length == 0) return;

        self->_statusLabel.stringValue = @"Creating playlist…";
        [self->_spinner startAnimation:nil];
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
            NSError *one = nil;
            NSString *newId = [SubsonicClient.sharedClient createPlaylistNamed:name
                                                                        songIds:ids
                                                                          error:&one];
            dispatch_async(dispatch_get_main_queue(), ^{
                [self->_spinner stopAnimation:nil];
                self->_statusLabel.stringValue = newId
                    ? [NSString stringWithFormat:@"Created “%@” (%lu track(s))",
                       name, (unsigned long)ids.count]
                    : [NSString stringWithFormat:@"Failed: %@",
                       one.localizedDescription ?: @"unknown error"];
                if (newId) {
                    [self invalidatePlaylistsCategory];
                    [self refreshServerPlaylists];
                }
            });
        });
    }];
}

// Only meaningful for song rows sitting directly under a playlist node — that's
// where a track has a position for songIndexToRemove to refer to.
- (IBAction)removeFromPlaylist:(id)sender {
    NavidromeNode *playlist = nil;
    NSMutableArray<NSNumber *> *indexes = [NSMutableArray array];

    for (NavidromeNode *n in [self selectedNodes]) {
        if (n.type != NavidromeNodeTypeSong) continue;
        NavidromeNode *parent = [_outlineView parentForItem:n];
        if (!parent || parent.type != NavidromeNodeTypePlaylist) continue;
        // Mixing playlists in one request isn't expressible — the endpoint takes
        // a single playlistId.
        if (playlist && ![playlist.nodeId isEqualToString:parent.nodeId]) continue;
        playlist = parent;
        NSUInteger idx = [parent.children indexOfObject:n];
        if (idx != NSNotFound) [indexes addObject:@(idx)];
    }

    if (!playlist || indexes.count == 0) {
        _statusLabel.stringValue = @"Select tracks inside a server playlist first";
        return;
    }

    NSString *playlistId = playlist.nodeId;
    NSString *playlistName = playlist.displayName;
    [_spinner startAnimation:nil];
    _statusLabel.stringValue = @"Removing…";
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        BOOL ok = [SubsonicClient.sharedClient removeIndexes:indexes
                                                fromPlaylist:playlistId
                                                       error:&err];
        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_spinner stopAnimation:nil];
            self->_statusLabel.stringValue = ok
                ? [NSString stringWithFormat:@"Removed %lu track(s) from “%@”",
                   (unsigned long)indexes.count, playlistName]
                : [NSString stringWithFormat:@"Failed: %@",
                   err.localizedDescription ?: @"unknown error"];
            if (ok) [self invalidatePlaylistNode:playlistId];
        });
    });
}

- (IBAction)renamePlaylist:(id)sender {
    NavidromeNode *playlist = [self singleSelectedPlaylist];
    if (!playlist) { _statusLabel.stringValue = @"Select a single server playlist"; return; }

    NSString *name = [self promptForText:@"Rename playlist"
                                 message:@"New name:"
                            initialValue:playlist.displayName ?: @""];
    if (name.length == 0 || [name isEqualToString:playlist.displayName]) return;

    NSString *playlistId = playlist.nodeId;
    [_spinner startAnimation:nil];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        BOOL ok = [SubsonicClient.sharedClient renamePlaylist:playlistId
                                                       toName:name
                                                        error:&err];
        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_spinner stopAnimation:nil];
            if (ok) {
                playlist.displayName = name;
                self->_statusLabel.stringValue = [NSString stringWithFormat:@"Renamed to “%@”", name];
                [self->_outlineView reloadData];
                [self refreshServerPlaylists];
            } else {
                self->_statusLabel.stringValue = [NSString stringWithFormat:@"Failed: %@",
                    err.localizedDescription ?: @"unknown error"];
            }
        });
    });
}

- (IBAction)deletePlaylist:(id)sender {
    NavidromeNode *playlist = [self singleSelectedPlaylist];
    if (!playlist) { _statusLabel.stringValue = @"Select a single server playlist"; return; }

    NSAlert *confirm = [[NSAlert alloc] init];
    confirm.messageText = [NSString stringWithFormat:@"Delete “%@” from the server?",
                           playlist.displayName];
    confirm.informativeText = @"The playlist is removed for every client. "
                               "The tracks themselves are not touched.";
    confirm.alertStyle = NSAlertStyleWarning;
    [confirm addButtonWithTitle:@"Delete"];
    [confirm addButtonWithTitle:@"Cancel"];
    if ([confirm runModal] != NSAlertFirstButtonReturn) return;

    NSString *playlistId = playlist.nodeId;
    NSString *playlistName = playlist.displayName;
    [_spinner startAnimation:nil];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        BOOL ok = [SubsonicClient.sharedClient deletePlaylist:playlistId error:&err];
        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_spinner stopAnimation:nil];
            self->_statusLabel.stringValue = ok
                ? [NSString stringWithFormat:@"Deleted “%@”", playlistName]
                : [NSString stringWithFormat:@"Failed: %@",
                   err.localizedDescription ?: @"unknown error"];
            if (ok) {
                [self invalidatePlaylistsCategory];
                [self refreshServerPlaylists];
            }
        });
    });
}

// Exactly one playlist row selected, or nil.
- (NavidromeNode *)singleSelectedPlaylist {
    NSArray<NavidromeNode *> *sel = [self selectedNodes];
    if (sel.count != 1) return nil;
    return sel[0].type == NavidromeNodeTypePlaylist ? sel[0] : nil;
}

// Drop a playlist node's cached children so the next expand refetches them.
- (void)invalidatePlaylistNode:(NSString *)playlistId {
    for (NavidromeNode *root in _rootNodes) {
        if (root.type != NavidromeNodeTypeCategory ||
            root.categoryKind != NavidromeCategoryPlaylists) continue;
        for (NavidromeNode *pl in root.children) {
            if (![pl.nodeId isEqualToString:playlistId]) continue;
            [pl.children removeAllObjects];
            pl.childrenLoaded = NO;
            [_outlineView collapseItem:pl];
            [_outlineView reloadItem:pl reloadChildren:YES];
            return;
        }
    }
}

// Drop the whole Playlists category — used when a playlist appears or vanishes.
- (void)invalidatePlaylistsCategory {
    for (NavidromeNode *root in _rootNodes) {
        if (root.type != NavidromeNodeTypeCategory ||
            root.categoryKind != NavidromeCategoryPlaylists) continue;
        [root.children removeAllObjects];
        root.childrenLoaded = NO;
        [_outlineView collapseItem:root];
        [_outlineView reloadItem:root reloadChildren:YES];
        return;
    }
}

// ---------------------------------------------------------------------------
// Internet radio station management
// ---------------------------------------------------------------------------

// Refresh the cached station list the enqueue path resolves streamUrl from.
// Cheap enough to re-run after every mutation, same as refreshServerPlaylists.
- (void)refreshRadioStations {
    if (![SubsonicClient.sharedClient isConfigured]) return;
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        NSArray<SubsonicRadioStation *> *stations =
            [SubsonicClient.sharedClient getRadioStationsWithError:&err];
        dispatch_async(dispatch_get_main_queue(), ^{
            if (!err && stations) _radioStations = stations;
        });
    });
}

- (SubsonicRadioStation *)radioStationForId:(NSString *)stationId {
    for (SubsonicRadioStation *s in _radioStations)
        if ([s.stationId isEqualToString:stationId]) return s;
    return nil;
}

// Exactly one radio station row selected, or nil.
- (NavidromeNode *)singleSelectedRadioStation {
    NSArray<NavidromeNode *> *sel = [self selectedNodes];
    if (sel.count != 1) return nil;
    return sel[0].type == NavidromeNodeTypeRadioStation ? sel[0] : nil;
}

// Drop the whole Radio category — used when a station appears/vanishes/changes.
- (void)invalidateRadioCategory {
    for (NavidromeNode *root in _rootNodes) {
        if (root.type != NavidromeNodeTypeCategory ||
            root.categoryKind != NavidromeCategoryRadio) continue;
        [root.children removeAllObjects];
        root.childrenLoaded = NO;
        [_outlineView collapseItem:root];
        [_outlineView reloadItem:root reloadChildren:YES];
        return;
    }
}

- (IBAction)newRadioStation:(id)sender {
    // Unlike a new playlist, creating a station needs no selection.
    NSString *name = nil, *streamUrl = nil, *homePageUrl = nil;
    if (![self promptForRadioStationWithTitle:@"New Radio Station"
                                          name:&name
                                     streamURL:&streamUrl
                                   homePageURL:&homePageUrl])
        return;
    if (name.length == 0 || streamUrl.length == 0) {
        _statusLabel.stringValue = @"Name and stream URL are required";
        return;
    }

    _statusLabel.stringValue = @"Creating radio station…";
    [_spinner startAnimation:nil];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        NSString *result = [SubsonicClient.sharedClient createRadioStationWithStreamURL:streamUrl
                                                                                    name:name
                                                                             homePageUrl:homePageUrl
                                                                                   error:&err];
        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_spinner stopAnimation:nil];
            self->_statusLabel.stringValue = result
                ? [NSString stringWithFormat:@"Created “%@”", name]
                : [NSString stringWithFormat:@"Failed: %@",
                   err.localizedDescription ?: @"unknown error"];
            if (result) {
                [self invalidateRadioCategory];
                [self refreshRadioStations];
            }
        });
    });
}

- (IBAction)editRadioStation:(id)sender {
    NavidromeNode *node = [self singleSelectedRadioStation];
    if (!node) { _statusLabel.stringValue = @"Select a single radio station"; return; }
    SubsonicRadioStation *current = [self radioStationForId:node.nodeId];

    NSString *name = nil, *streamUrl = nil, *homePageUrl = nil;
    if (![self promptForRadioStationWithTitle:@"Edit Radio Station"
                            initialName:current.name ?: node.displayName
                       initialStreamURL:current.streamUrl ?: @""
                     initialHomePageURL:current.homePageUrl ?: @""
                                   name:&name
                              streamURL:&streamUrl
                            homePageURL:&homePageUrl])
        return;
    if (name.length == 0 || streamUrl.length == 0) {
        _statusLabel.stringValue = @"Name and stream URL are required";
        return;
    }

    NSString *stationId = node.nodeId;
    [_spinner startAnimation:nil];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        BOOL ok = [SubsonicClient.sharedClient updateRadioStation:stationId
                                                          streamURL:streamUrl
                                                               name:name
                                                        homePageUrl:homePageUrl
                                                              error:&err];
        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_spinner stopAnimation:nil];
            self->_statusLabel.stringValue = ok
                ? [NSString stringWithFormat:@"Updated “%@”", name]
                : [NSString stringWithFormat:@"Failed: %@",
                   err.localizedDescription ?: @"unknown error"];
            if (ok) {
                [self invalidateRadioCategory];
                [self refreshRadioStations];
            }
        });
    });
}

- (IBAction)deleteRadioStation:(id)sender {
    NavidromeNode *node = [self singleSelectedRadioStation];
    if (!node) { _statusLabel.stringValue = @"Select a single radio station"; return; }

    NSAlert *confirm = [[NSAlert alloc] init];
    confirm.messageText = [NSString stringWithFormat:@"Delete “%@” from the server?",
                           node.displayName];
    confirm.informativeText = @"The station is removed for every client.";
    confirm.alertStyle = NSAlertStyleWarning;
    [confirm addButtonWithTitle:@"Delete"];
    [confirm addButtonWithTitle:@"Cancel"];
    if ([confirm runModal] != NSAlertFirstButtonReturn) return;

    NSString *stationId = node.nodeId;
    NSString *stationName = node.displayName;
    [_spinner startAnimation:nil];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        BOOL ok = [SubsonicClient.sharedClient deleteRadioStation:stationId error:&err];
        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_spinner stopAnimation:nil];
            self->_statusLabel.stringValue = ok
                ? [NSString stringWithFormat:@"Deleted “%@”", stationName]
                : [NSString stringWithFormat:@"Failed: %@",
                   err.localizedDescription ?: @"unknown error"];
            if (ok) {
                [self invalidateRadioCategory];
                [self refreshRadioStations];
            }
        });
    });
}

// Modal 3-field prompt (name / stream URL / home page URL). Returns NO if
// cancelled, in which case the out params are left untouched.
- (BOOL)promptForRadioStationWithTitle:(NSString *)title
                                   name:(NSString **)outName
                              streamURL:(NSString **)outStreamURL
                            homePageURL:(NSString **)outHomePageURL {
    return [self promptForRadioStationWithTitle:title
                                     initialName:@""
                                initialStreamURL:@""
                              initialHomePageURL:@""
                                            name:outName
                                       streamURL:outStreamURL
                                     homePageURL:outHomePageURL];
}

- (BOOL)promptForRadioStationWithTitle:(NSString *)title
                            initialName:(NSString *)initialName
                       initialStreamURL:(NSString *)initialStreamURL
                     initialHomePageURL:(NSString *)initialHomePageURL
                                   name:(NSString **)outName
                              streamURL:(NSString **)outStreamURL
                            homePageURL:(NSString **)outHomePageURL {
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = title;
    alert.informativeText = @"Name and stream URL are required. Home page URL is optional.";
    [alert addButtonWithTitle:@"OK"];
    [alert addButtonWithTitle:@"Cancel"];

    CGFloat fieldWidth = 260, rowHeight = 24, rowGap = 6, labelHeight = 16;
    NSView *container = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, fieldWidth, 3 * (rowHeight + labelHeight + rowGap))];

    NSTextField *nameLabel = [NSTextField labelWithString:@"Name:"];
    NSTextField *nameField = [[NSTextField alloc] init];
    nameField.stringValue = initialName ?: @"";

    NSTextField *urlLabel = [NSTextField labelWithString:@"Stream URL:"];
    NSTextField *urlField = [[NSTextField alloc] init];
    urlField.stringValue = initialStreamURL ?: @"";

    NSTextField *homeLabel = [NSTextField labelWithString:@"Home page URL (optional):"];
    NSTextField *homeField = [[NSTextField alloc] init];
    homeField.stringValue = initialHomePageURL ?: @"";

    CGFloat y = 3 * (rowHeight + labelHeight + rowGap) - labelHeight;
    for (NSArray *pair in @[@[nameLabel, nameField], @[urlLabel, urlField], @[homeLabel, homeField]]) {
        NSTextField *label = pair[0];
        NSTextField *field = pair[1];
        label.frame = NSMakeRect(0, y, fieldWidth, labelHeight);
        [container addSubview:label];
        y -= (rowHeight + 2);
        field.frame = NSMakeRect(0, y, fieldWidth, rowHeight);
        [container addSubview:field];
        y -= rowGap;
    }

    alert.accessoryView = container;
    [alert layout];
    [alert.window setInitialFirstResponder:nameField];

    if ([alert runModal] != NSAlertFirstButtonReturn) return NO;

    [nameField validateEditing];
    [urlField validateEditing];
    [homeField validateEditing];

    NSCharacterSet *ws = [NSCharacterSet whitespaceAndNewlineCharacterSet];
    if (outName)        *outName        = [nameField.stringValue stringByTrimmingCharactersInSet:ws];
    if (outStreamURL)   *outStreamURL   = [urlField.stringValue stringByTrimmingCharactersInSet:ws];
    if (outHomePageURL) *outHomePageURL = [homeField.stringValue stringByTrimmingCharactersInSet:ws];
    return YES;
}

// Modal single-line prompt. NSAlert is the only sheet-free way to ask for text
// that works both in the standalone window and inside the prefs page.
- (NSString *)promptForText:(NSString *)title
                    message:(NSString *)message
               initialValue:(NSString *)initial {
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = title;
    alert.informativeText = message;
    [alert addButtonWithTitle:@"OK"];
    [alert addButtonWithTitle:@"Cancel"];

    NSTextField *input = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 260, 24)];
    input.stringValue = initial ?: @"";
    alert.accessoryView = input;
    [alert layout];
    [alert.window setInitialFirstResponder:input];

    if ([alert runModal] != NSAlertFirstButtonReturn) return nil;
    // Flush the field editor into stringValue — clicking OK doesn't necessarily
    // end editing, so without this the last typed characters are lost.
    [input validateEditing];
    return [input.stringValue stringByTrimmingCharactersInSet:
            [NSCharacterSet whitespaceAndNewlineCharacterSet]];
}

- (void)doubleClicked:(id)sender {
    NSInteger row = [_outlineView clickedRow];
    if (row < 0) return;
    NavidromeNode *node = [_outlineView itemAtRow:row];
    if (!node) return;

    if (node.type == NavidromeNodeTypeSong || node.type == NavidromeNodeTypeRadioStation) {
        [self addNodesToPlaylist:@[node] play:YES];
    } else {
        // Toggle expand/collapse
        if ([_outlineView isItemExpanded:node])
            [_outlineView collapseItem:node];
        else
            [_outlineView expandItem:node];
    }
}

// ---------------------------------------------------------------------------
// NSOutlineViewDataSource
// ---------------------------------------------------------------------------

- (NSInteger)outlineView:(NSOutlineView *)ov numberOfChildrenOfItem:(id)item {
    if (item == nil) {
        return (NSInteger)(_isSearching ? _filteredNodes.count : _rootNodes.count);
    }
    NavidromeNode *node = (NavidromeNode *)item;
    if (node.isLeaf) return 0;
    // If not yet loaded, show 1 (will trigger loading when expanded)
    if (!node.childrenLoaded && !node.isLoading) return 1;
    return (NSInteger)node.children.count;
}

- (id)outlineView:(NSOutlineView *)ov child:(NSInteger)index ofItem:(id)item {
    if (item == nil) {
        NSArray *roots = _isSearching ? _filteredNodes : _rootNodes;
        return roots[(NSUInteger)index];
    }
    NavidromeNode *node = (NavidromeNode *)item;
    if (!node.childrenLoaded && !node.isLoading && index == 0) {
        // Return a temporary node while we trigger loading
        return [NavidromeNode loadingNode];
    }
    return node.children[(NSUInteger)index];
}

- (BOOL)outlineView:(NSOutlineView *)ov isItemExpandable:(id)item {
    NavidromeNode *node = (NavidromeNode *)item;
    return !node.isLeaf;
}

// ---------------------------------------------------------------------------
// NSOutlineViewDelegate
// ---------------------------------------------------------------------------

- (NSView *)outlineView:(NSOutlineView *)ov
     viewForTableColumn:(NSTableColumn *)tableColumn
                   item:(id)item {
    NavidromeNode *node = (NavidromeNode *)item;

    NSTextField *cell = [ov makeViewWithIdentifier:tableColumn.identifier owner:self];
    if (!cell) {
        cell = [NSTextField labelWithString:@""];
        cell.identifier = tableColumn.identifier;
    }

    // Style placeholders differently
    if (node.type == NavidromeNodeTypeLoading || node.type == NavidromeNodeTypeError) {
        cell.textColor = [NSColor secondaryLabelColor];
        cell.stringValue = [tableColumn.identifier isEqualToString:@"name"] ? node.displayName : @"";
        return cell;
    }

    cell.textColor = [NSColor labelColor];

    if ([tableColumn.identifier isEqualToString:@"name"]) {
        NSString *name = node.displayName ?: @"";
        if (node.type == NavidromeNodeTypeSong && node.trackNumber > 0)
            name = [NSString stringWithFormat:@"%ld. %@", (long)node.trackNumber, name];
        // Favorites are marked inline; category rows carry their own icon.
        if (node.starred && node.type != NavidromeNodeTypeCategory)
            name = [@"★ " stringByAppendingString:name];
        cell.stringValue = name;
    } else if ([tableColumn.identifier isEqualToString:@"sub"]) {
        NSString *sub = node.subtitle ?: @"";
        if (node.rating > 0) {
            NSString *stars = [@"" stringByPaddingToLength:(NSUInteger)node.rating
                                                withString:@"★" startingAtIndex:0];
            sub = sub.length ? [NSString stringWithFormat:@"%@  %@", sub, stars] : stars;
        }
        cell.stringValue = sub;
        cell.textColor = [NSColor secondaryLabelColor];
    } else if ([tableColumn.identifier isEqualToString:@"dur"]) {
        cell.stringValue = node.duration > 0 ? formatDuration(node.duration) : @"";
        cell.textColor = [NSColor secondaryLabelColor];
        cell.alignment = NSTextAlignmentRight;
    }

    return cell;
}

- (void)outlineViewItemWillExpand:(NSNotification *)notification {
    NavidromeNode *node = notification.userInfo[@"NSObject"];
    if (node && !node.childrenLoaded && !node.isLoading) {
        [self loadChildrenOfNode:node inOutlineView:_outlineView];
    }
}

@end

// ---------------------------------------------------------------------------
// Standalone window wrapper for the File menu and library_viewer.activate().
// Each call creates a fresh browser controller and wraps it in an NSWindow.
// The window+controller pair is retained in a static set until the window
// closes, at which point it's released. Multiple windows can coexist.
// ---------------------------------------------------------------------------

@interface NavidromeBrowserWindowOwner : NSObject <NSWindowDelegate>
@property (nonatomic, strong) NSWindow *window;
@property (nonatomic, strong) NavidromeBrowserController *vc;
@end

static NSMutableSet<NavidromeBrowserWindowOwner *> *gStandaloneOwners = nil;

@implementation NavidromeBrowserWindowOwner
- (void)windowWillClose:(NSNotification *)note {
    [gStandaloneOwners removeObject:self];
}
@end

void NavidromeShowStandaloneBrowser(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        if (!gStandaloneOwners) gStandaloneOwners = [NSMutableSet set];

        NavidromeBrowserWindowOwner *owner = [NavidromeBrowserWindowOwner new];
        owner.vc = [NavidromeBrowserController new];
        owner.vc.standalone = YES;   // enables the Enter = queue+play+close shortcut

        NSWindow *win = [[NSWindow alloc]
                         initWithContentRect:NSMakeRect(0, 0, 520, 600)
                         styleMask:(NSWindowStyleMaskTitled |
                                    NSWindowStyleMaskClosable |
                                    NSWindowStyleMaskMiniaturizable |
                                    NSWindowStyleMaskResizable)
                         backing:NSBackingStoreBuffered
                         defer:NO];
        win.title = @"Navidrome Browser";
        win.minSize = NSMakeSize(360, 300);
        win.releasedWhenClosed = NO;
        win.contentViewController = owner.vc;
        win.delegate = owner;
        [win center];

        owner.window = win;
        [gStandaloneOwners addObject:owner];
        [win makeKeyAndOrderFront:nil];
    });
}
