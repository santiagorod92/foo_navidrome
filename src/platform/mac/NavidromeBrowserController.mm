#import "NavidromeBrowserController+Private.h"
#import "MacSubsonicBrowserClient.h"
#include "../../core/SubsonicTypes.h"
#include "../../core/NavidromeBrowserModel.h"
#include "../../core/NavidromeBrowserEnqueue.h"
#include "../../core/NavidromeAudioMuse.h"
#include "../../core/NavidromeDebugLog.h"
#include <SDK/playlist.h>
#include <SDK/metadb.h>
#include <SDK/playable_location.h>
#include <SDK/playback_control.h>

navidrome::IBrowserClient& NBCBrowserClient() {
    static std::unique_ptr<navidrome::IBrowserClient> inst = navidrome::makeMacBrowserClient();
    return *inst;
}

@implementation NavidromeNode

+ (instancetype)songNode:(SubsonicSong *)s {
    NavidromeNode *n = [NavidromeNode new];
    n.type         = NavidromeNodeTypeSong;
    n.nodeId       = s.songId;
    n.displayName  = s.title;
    n.subtitle     = s.artist;
    n.albumName    = s.album;
    n.albumId      = s.albumId;
    n.trackNumber  = s.track;
    n.year         = s.year;
    n.duration     = s.duration;
    n.coverArtId   = s.coverArtId;
    n.suffix       = s.suffix;
    n.starred      = s.starred;
    n.rating       = s.rating;
    n.children     = [NSMutableArray array];
    n.childrenLoaded = YES;
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
                        self.type == NavidromeNodeTypeError ||
                        [self isAllSongs]; }

- (BOOL)isAllSongs { return self.type == NavidromeNodeTypeCategory &&
                            self.categoryKind == NavidromeCategoryAllSongs; }

+ (instancetype)wrapCoreNode:(const navidrome::BrowserNode &)c {
    NavidromeNode *n = [NavidromeNode new];
    n.type        = (NavidromeNodeType)(int)c.type;
    n.categoryKind = (NavidromeCategoryKind)(int)c.category;
    n.nodeId      = c.id.empty()         ? nil : @(c.id.c_str());
    n.displayName = @(c.displayName.c_str());
    n.subtitle    = c.subtitle.empty()   ? nil : @(c.subtitle.c_str());
    n.albumName   = c.album.empty()      ? nil : @(c.album.c_str());
    n.albumId     = c.albumId.empty()    ? nil : @(c.albumId.c_str());
    n.libraryId   = c.libraryId.empty()  ? nil : @(c.libraryId.c_str());
    n.coverArtId  = c.coverArtId.empty() ? nil : @(c.coverArtId.c_str());
    n.suffix      = c.suffix.empty()     ? nil : @(c.suffix.c_str());
    n.trackNumber = c.track;
    n.year        = c.year;
    n.duration    = c.duration;
    n.starred     = c.starred;
    n.rating      = c.rating;
    n.bookmarkPositionMs = c.bookmarkPositionMs;
    n.infoText    = c.infoText.empty()   ? nil : @(c.infoText.c_str());
    n.childrenLoaded = c.childrenLoaded;
    n.children    = [NSMutableArray array];
    return n;
}

- (navidrome::BrowserNode)coreNode {
    navidrome::BrowserNode c;
    c.type       = (navidrome::BrowserNode::Type)(int)self.type;
    c.category   = (navidrome::BrowserNode::CategoryKind)(int)self.categoryKind;
    c.id         = NBCStr(self.nodeId);
    c.displayName = NBCStr(self.displayName);
    c.subtitle   = NBCStr(self.subtitle);
    c.album      = NBCStr(self.albumName);
    c.albumId    = NBCStr(self.albumId);
    c.libraryId  = NBCStr(self.libraryId);
    c.coverArtId = NBCStr(self.coverArtId);
    c.suffix     = NBCStr(self.suffix);
    c.track      = (int)self.trackNumber;
    c.year       = (int)self.year;
    c.duration   = self.duration;
    c.starred    = self.starred ? true : false;
    c.rating     = (int)self.rating;
    c.bookmarkPositionMs = self.bookmarkPositionMs;
    c.infoText   = NBCStr(self.infoText);
    c.childrenLoaded = self.childrenLoaded ? true : false;
    return c;
}

@end

static NSMutableArray<NavidromeNode *> *
NBCWrapList(const std::vector<navidrome::BrowserNodePtr> &nodes) {
    NSMutableArray<NavidromeNode *> *out = [NSMutableArray arrayWithCapacity:nodes.size()];
    for (const auto &p : nodes)
        if (p) [out addObject:[NavidromeNode wrapCoreNode:*p]];
    return out;
}

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

NSNotificationName const NavidromeBrowserSectionsDidChangeNotification =
    @"NavidromeBrowserSectionsDidChangeNotification";

@implementation NavidromeBrowserController

- (instancetype)init {
    self = [super initWithNibName:nil bundle:nil];
    if (self) {
        _rootNodes     = [NSMutableArray array];
        _filteredNodes = [NSMutableArray array];
        [NSNotificationCenter.defaultCenter addObserver:self
                                               selector:@selector(browserSectionsChanged:)
                                                   name:NavidromeBrowserSectionsDidChangeNotification
                                                 object:nil];
    }
    return self;
}

- (void)dealloc {
    [NSNotificationCenter.defaultCenter removeObserver:self];
}

- (void)browserSectionsChanged:(NSNotification *)note {
    if (!self.isViewLoaded) return;
    NAVIDROME_LOG("UI", "browser sections changed, reloading tree");
    [self refresh:nil];
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

    _searchField = [NSSearchField new];
    _searchField.translatesAutoresizingMaskIntoConstraints = NO;
    _searchField.placeholderString = @"Search artists, albums, songs…";
    _searchField.target = self;
    _searchField.action = @selector(searchChanged:);
    _searchField.delegate = self;
    [content addSubview:_searchField];

    _spinner = [[NSProgressIndicator alloc] init];
    _spinner.translatesAutoresizingMaskIntoConstraints = NO;
    _spinner.style = NSProgressIndicatorStyleSpinning;
    _spinner.controlSize = NSControlSizeSmall;
    [_spinner setDisplayedWhenStopped:NO];
    [content addSubview:_spinner];

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

    NSMenu *rowMenu = [[NSMenu alloc] init];
    NSMenuItem *playItem = [rowMenu addItemWithTitle:@"Play Now"
                                              action:@selector(playNow:)
                                       keyEquivalent:@""];
    playItem.target = self;
    NSMenuItem *addItem = [rowMenu addItemWithTitle:@"Add to Playlist"
                                             action:@selector(addToPlaylist:)
                                      keyEquivalent:@""];
    addItem.target = self;
    NSMenuItem *similarItem = [rowMenu addItemWithTitle:@"Instant Mix"
                                                 action:@selector(playSimilarSelection:)
                                          keyEquivalent:@""];
    similarItem.target = self;
    _alchemyItem = [rowMenu addItemWithTitle:@"Song Alchemy (AudioMuse-AI)"
                                      action:@selector(alchemySelection:)
                               keyEquivalent:@""];
    _alchemyItem.target = self;
    rowMenu.delegate = self;
    NSMenuItem *artistInfoItem = [rowMenu addItemWithTitle:@"Artist Info"
                                                     action:@selector(showArtistInfo:)
                                              keyEquivalent:@""];
    artistInfoItem.target = self;

    [rowMenu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *starItem = [rowMenu addItemWithTitle:@"Star"
                                              action:@selector(starSelection:)
                                       keyEquivalent:@""];
    starItem.target = self;
    NSMenuItem *unstarItem = [rowMenu addItemWithTitle:@"Unstar"
                                                action:@selector(unstarSelection:)
                                         keyEquivalent:@""];
    unstarItem.target = self;
    NSMenuItem *removeBookmarkItem = [rowMenu addItemWithTitle:@"Remove Bookmark"
                                                        action:@selector(removeBookmarkSelection:)
                                                 keyEquivalent:@""];
    removeBookmarkItem.target = self;

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
    NSMenuItem *subscribePodcastItem = [rowMenu addItemWithTitle:@"Subscribe to Podcast…"
                                                           action:@selector(subscribePodcast:)
                                                    keyEquivalent:@""];
    subscribePodcastItem.target = self;
    NSMenuItem *unsubscribePodcastItem = [rowMenu addItemWithTitle:@"Unsubscribe from Podcast…"
                                                             action:@selector(unsubscribePodcast:)
                                                      keyEquivalent:@""];
    unsubscribePodcastItem.target = self;

    [rowMenu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *uploadItem = [rowMenu addItemWithTitle:@"Send Active Playlist to Navidrome"
                                                action:@selector(sendActivePlaylist:)
                                         keyEquivalent:@""];
    uploadItem.target = self;

    NSMenuItem *randomMixItem = [rowMenu addItemWithTitle:@"Random Mix"
                                                    action:@selector(playRandomMix:)
                                             keyEquivalent:@""];
    randomMixItem.target = self;

    NSMenuItem *downloadItem = [rowMenu addItemWithTitle:@"Download Original Files…"
                                                  action:@selector(downloadSelection:)
                                           keyEquivalent:@""];
    downloadItem.target = self;

    _outlineView.menu = rowMenu;

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

    _statusLabel = [NSTextField labelWithString:@""];
    _statusLabel.translatesAutoresizingMaskIntoConstraints = NO;
    _statusLabel.textColor = [NSColor secondaryLabelColor];
    _statusLabel.font = [NSFont systemFontOfSize:11];
    [content addSubview:_statusLabel];

    NSButton *addBtn = [NSButton buttonWithTitle:@"Add to Playlist"
                                          target:self
                                          action:@selector(addToPlaylist:)];
    addBtn.translatesAutoresizingMaskIntoConstraints = NO;

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

    CGFloat pad = 10;
    [NSLayoutConstraint activateConstraints:@[
        [_searchField.topAnchor constraintEqualToAnchor:content.topAnchor constant:pad],
        [_searchField.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:pad],
        [_searchField.trailingAnchor constraintEqualToAnchor:_spinner.leadingAnchor constant:-pad],

        [_spinner.centerYAnchor constraintEqualToAnchor:_searchField.centerYAnchor],
        [_spinner.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-pad],

        [scrollView.topAnchor constraintEqualToAnchor:_searchField.bottomAnchor constant:pad],
        [scrollView.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:pad],
        [scrollView.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-pad],
        [scrollView.bottomAnchor constraintEqualToAnchor:addBtn.topAnchor constant:-pad],

        [addBtn.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-pad],
        [addBtn.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-pad],

        [playBtn.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-pad],
        [playBtn.trailingAnchor constraintEqualToAnchor:addBtn.leadingAnchor constant:-pad],

        [refreshBtn.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-pad],
        [refreshBtn.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:pad],

        [_statusLabel.centerYAnchor constraintEqualToAnchor:addBtn.centerYAnchor],
        [_statusLabel.leadingAnchor constraintEqualToAnchor:refreshBtn.trailingAnchor constant:pad],
        [_statusLabel.trailingAnchor constraintEqualToAnchor:playBtn.leadingAnchor constant:-pad],
    ]];
}

- (NSArray<NavidromeNode *> *)buildCategoryNodes {
    return NBCWrapList(navidrome::buildCategoryNodes());
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
    [self refreshServerPlaylists];
    [self refreshRadioStations];

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        std::string err;
        NSMutableArray<NavidromeNode *> *roots =
            NBCWrapList(navidrome::buildRootNodes(NBCBrowserClient(), err));
        std::string errCopy = err;
        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_spinner stopAnimation:nil];
            if (!errCopy.empty()) {
                self->_reloadNotice = nil;
                self->_statusLabel.stringValue = [NSString stringWithFormat:@"Error: %s", errCopy.c_str()];
                return;
            }
            self->_treeLoadedAtMs = navidrome::browserNowMs();
            NSUInteger artists = 0, libraries = 0;
            for (NavidromeNode *n in roots) {
                if (n.type == NavidromeNodeTypeArtist)  ++artists;
                if (n.type == NavidromeNodeTypeLibrary) ++libraries;
            }
            [self->_rootNodes addObjectsFromArray:roots];
            NSString *notice = self->_reloadNotice;
            self->_reloadNotice = nil;
            self->_statusLabel.stringValue = notice ? notice : libraries
                ? [NSString stringWithFormat:@"%lu libraries", (unsigned long)libraries]
                : [NSString stringWithFormat:@"%lu artists", (unsigned long)artists];
            [self->_outlineView reloadData];
        });
    });
}

- (NSMutableArray<NavidromeNode *> *)fetchChildrenOf:(NavidromeNode *)node
                                               error:(NSError **)outError {
    std::string err;
    navidrome::BrowserNode core = [node coreNode];
    auto kids = navidrome::fetchChildren(NBCBrowserClient(), core, err);
    if (!err.empty()) {
        if (outError)
            *outError = [NSError errorWithDomain:@"Navidrome"
                                            code:-1
                                        userInfo:@{ NSLocalizedDescriptionKey: @(err.c_str()) }];
        return [NSMutableArray array];
    }
    return NBCWrapList(kids);
}

static void syncSongNodesToPlaylists(NSArray<NavidromeNode *> *nodes) {
    std::vector<navidrome::BrowserNodePtr> core;
    core.reserve(nodes.count);
    for (NavidromeNode *n in nodes)
        core.push_back(std::make_shared<navidrome::BrowserNode>([n coreNode]));
    navidrome::syncBrowserNodesToPlaylists(core);
}

- (void)loadChildrenOfNode:(NavidromeNode *)node inOutlineView:(NSOutlineView *)ov {
    if (node.childrenLoaded || node.isLoading) return;
    node.isLoading = YES;

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

- (void)searchChanged:(id)sender {
    [_searchDebounceTimer invalidate];
    [self dispatchSearch];
}

- (void)controlTextDidChange:(NSNotification *)note {
    if (note.object != _searchField) return;
    [_searchDebounceTimer invalidate];
    __weak typeof(self) weakSelf = self;
    _searchDebounceTimer = [NSTimer scheduledTimerWithTimeInterval:0.3
                                                             repeats:NO
                                                               block:^(NSTimer *timer) {
        [weakSelf dispatchSearch];
    }];
}

- (void)dispatchSearch {
    _searchDebounceTimer = nil;
    NSString *query = [_searchField stringValue];
    NSUInteger generation = ++_searchGeneration;

    if (query.length < 2) {
        _isSearching = NO;
        [_filteredNodes removeAllObjects];
        [_outlineView reloadData];
        _statusLabel.stringValue = @"";
        return;
    }

    _isSearching = YES;
    [_spinner startAnimation:nil];
    _statusLabel.stringValue = @"Searching…";

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        NSDictionary *results = [SubsonicClient.sharedClient search:query error:&err];
        dispatch_async(dispatch_get_main_queue(), ^{
            if (generation != self->_searchGeneration) return;

            [self->_spinner stopAnimation:nil];
            [self->_filteredNodes removeAllObjects];

            if (err || !results) {
                self->_statusLabel.stringValue = [NSString stringWithFormat:@"Search error: %@", err.localizedDescription];
                [self->_outlineView reloadData];
                return;
            }

            NSArray<SubsonicSong *> *songs = results[@"songs"];
            for (SubsonicSong *s in songs)
                [self->_filteredNodes addObject:[NavidromeNode songNode:s]];
            syncSongNodesToPlaylists(self->_filteredNodes);

            self->_statusLabel.stringValue = [NSString stringWithFormat:@"%lu songs found", (unsigned long)songs.count];

            [self->_outlineView reloadData];
        });
    });
}

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

- (void)addNodesToPlaylist:(NSArray<NavidromeNode *> *)nodes play:(BOOL)play {
    [self addNodesToPlaylist:nodes play:play closeWhenDone:NO];
}

- (void)addNodesToPlaylist:(NSArray<NavidromeNode *> *)nodes
                      play:(BOOL)play
             closeWhenDone:(BOOL)closeWhenDone {
    [self addNodesToPlaylist:nodes play:play closeWhenDone:closeWhenDone clearFirst:NO];
}

- (void)addNodesToPlaylist:(NSArray<NavidromeNode *> *)nodes
                      play:(BOOL)play
             closeWhenDone:(BOOL)closeWhenDone
                clearFirst:(BOOL)clearFirst {
    if (nodes.count == 0) {
        _statusLabel.stringValue = @"Select at least one item first";
        return;
    }

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

    NSArray *nodesCopy = [nodes copy];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        NSMutableArray<NavidromeNode *> *songs = [self collectSelectionSongs:nodesCopy error:&err];
        if (err) NAVIDROME_WARN("UI", "addNodesToPlaylist: " + NBCStr(err.localizedDescription));
        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_spinner stopAnimation:nil];
            const bool stale = navidrome::browserTreeIsStale(self->_treeLoadedAtMs,
                                                             navidrome::browserNowMs());
            const bool gotSongs = !err && songs.count > 0;
            const std::string errText = err ? NBCStr(err.localizedDescription) : std::string();
            const bool reload =
                navidrome::shouldReloadAfterEmptyCollect(gotSongs, err != nil, stale);
            const std::string problem = navidrome::queueProblemMessage(gotSongs, errText, reload);
            if (!problem.empty()) navidrome::showBrowserQueueError(problem);
            if (reload) {
                NAVIDROME_WARN("UI", "addNodesToPlaylist: nothing to queue, reloading the browse tree");
                self.reloadNotice = @"Couldn't load tracks — list reloaded, select again";
                [self refresh:nil];
            } else if (!gotSongs) {
                NAVIDROME_WARN("UI", "addNodesToPlaylist: the selection resolved to no tracks");
                self->_statusLabel.stringValue = @"No tracks found — try Refresh";
            } else {
                [self enqueueNodes:songs play:play clearFirst:clearFirst];
                if (closeWhenDone) [self closeStandaloneWindow];
            }
        });
    });
}

- (void)commitSelectionFromKeyboard {
    [self addNodesToPlaylist:[self selectedNodes] play:YES closeWhenDone:YES clearFirst:YES];
}

- (void)closeStandaloneWindow {
    if (self.standalone) [self.view.window close];
}

static BOOL isArtistSubCategoryNode(NavidromeNode *n) {
    return n.type == NavidromeNodeTypeCategory &&
           (n.categoryKind == NavidromeCategoryArtistTopSongs ||
            n.categoryKind == NavidromeCategoryArtistSimilarArtists);
}

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
        if (isArtistSubCategoryNode(child)) continue;
        [self collectSongsDeep:child into:songs error:outError];
        if (outError && *outError) return;
    }
}

- (NSMutableArray<NavidromeNode *> *)collectSelectionSongs:(NSArray<NavidromeNode *> *)nodes
                                                     error:(NSError **)outError {
    NSMutableArray<NavidromeNode *> *out = [NSMutableArray array];
    NSMutableSet<NSString *> *fromEarlierNodes = [NSMutableSet set];
    for (NavidromeNode *node in nodes) {
        NSMutableArray<NavidromeNode *> *part = [NSMutableArray array];
        [self collectSongsDeep:node into:part error:outError];
        if (outError && *outError) break;
        for (NavidromeNode *s in part)
            if (s.nodeId.length == 0 || ![fromEarlierNodes containsObject:s.nodeId])
                [out addObject:s];
        for (NavidromeNode *s in part)
            if (s.nodeId.length) [fromEarlierNodes addObject:s.nodeId];
    }
    return out;
}

- (void)enqueueNodes:(NSArray<NavidromeNode *> *)songNodes play:(BOOL)play {
    [self enqueueNodes:songNodes play:play clearFirst:NO];
}

- (void)enqueueNodes:(NSArray<NavidromeNode *> *)songNodes
                play:(BOOL)play
          clearFirst:(BOOL)clearFirst {
    __unsafe_unretained NavidromeBrowserController *weakSelf = self;
    std::vector<navidrome::BrowserNodePtr> nodes;
    nodes.reserve(songNodes.count);
    for (NavidromeNode *n in songNodes)
        nodes.push_back(std::make_shared<navidrome::BrowserNode>([n coreNode]));

    std::string status;
    navidrome::enqueueBrowserNodes(
        nodes, play, clearFirst,
        [weakSelf](const std::string &radioId) -> std::string {
            NSString *url = [weakSelf radioStationForId:@(radioId.c_str())].streamUrl;
            return url ? std::string(url.UTF8String) : std::string();
        },
        status);
    _statusLabel.stringValue = @(status.c_str());
}

- (IBAction)addToPlaylist:(id)sender {
    [self addNodesToPlaylist:[self selectedNodes] play:NO];
}

- (IBAction)playNow:(id)sender {
    [self addNodesToPlaylist:[self selectedNodes] play:YES];
}

- (IBAction)playSimilarSelection:(id)sender {
    NavidromeNode *node = [self selectedNodes].firstObject;
    navidrome::BrowserNode core = node ? [node coreNode] : navidrome::BrowserNode{};
    if (!node || !navidrome::isSimilarEligible(core)) {
        _statusLabel.stringValue = @"Instant Mix needs an artist, album, or song";
        return;
    }
    navidrome::startInstantMix(std::make_shared<navidrome::BrowserNode>(core));
}

- (IBAction)alchemySelection:(id)sender {
    std::vector<navidrome::BrowserNodePtr> core;
    for (NavidromeNode *n in [self selectedNodes])
        core.push_back(std::make_shared<navidrome::BrowserNode>([n coreNode]));
    std::string label;
    auto seeds = navidrome::audiomuse::seedsFromNodes(core, label);
    if (seeds.empty()) {
        _statusLabel.stringValue = @"Song Alchemy needs songs or artists";
        return;
    }
    navidrome::audioMuseAlchemy(std::move(seeds), std::move(label));
}

- (IBAction)showArtistInfo:(id)sender {
    NavidromeNode *node = [self selectedNodes].firstObject;
    if (!node || node.type != NavidromeNodeTypeArtist) {
        _statusLabel.stringValue = @"Artist Info needs an artist";
        return;
    }

    [_spinner startAnimation:nil];
    _statusLabel.stringValue = @"Fetching artist info…";
    std::string artistId = NBCStr(node.nodeId);
    NSString *artistName = node.displayName;
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        std::string err;
        auto info = NBCBrowserClient().getArtistInfo(artistId, err);
        NSString *text = @(navidrome::formatArtistBiography(info).c_str());
        NSString *lastFmUrl = info.lastFmUrl.empty() ? nil : @(info.lastFmUrl.c_str());
        std::string errCopy = err;
        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_spinner stopAnimation:nil];
            if (!errCopy.empty()) {
                self->_statusLabel.stringValue = [NSString stringWithFormat:@"Error: %s", errCopy.c_str()];
                return;
            }
            self->_statusLabel.stringValue = @"";
            NSAlert *alert = [[NSAlert alloc] init];
            alert.messageText = artistName;
            alert.informativeText = text;
            [alert addButtonWithTitle:@"OK"];
            if (lastFmUrl) [alert addButtonWithTitle:@"Open on Last.fm"];
            if ([alert runModal] == NSAlertSecondButtonReturn && lastFmUrl) {
                [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:lastFmUrl]];
            }
        });
    });
}

- (IBAction)playRandomMix:(id)sender {
    [_spinner startAnimation:nil];
    _statusLabel.stringValue = @"Fetching random mix…";
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        std::string err;
        auto nodes = navidrome::fetchRandomMix(NBCBrowserClient(), 100, err);
        NSMutableArray<NavidromeNode *> *songNodes = NBCWrapList(nodes);
        std::string errCopy = err;
        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_spinner stopAnimation:nil];
            if (!errCopy.empty()) {
                self->_statusLabel.stringValue = [NSString stringWithFormat:@"Error: %s", errCopy.c_str()];
                return;
            }
            if (songNodes.count == 0) {
                self->_statusLabel.stringValue = @"No tracks found";
                return;
            }
            [self enqueueNodes:songNodes play:YES clearFirst:NO];
        });
    });
}

- (IBAction)refresh:(id)sender {
    [_searchDebounceTimer invalidate];
    _searchDebounceTimer = nil;
    ++_searchGeneration;
    _isSearching = NO;
    _searchField.stringValue = @"";
    [self loadArtists];
}

- (void)doubleClicked:(id)sender {
    NSInteger row = [_outlineView clickedRow];
    if (row < 0) return;
    NavidromeNode *node = [_outlineView itemAtRow:row];
    if (!node) return;

    if (node.type == NavidromeNodeTypeSong || node.type == NavidromeNodeTypeRadioStation ||
        node.isAllSongs) {
        [self addNodesToPlaylist:@[node] play:YES];
    } else {
        if ([_outlineView isItemExpanded:node])
            [_outlineView collapseItem:node];
        else
            [_outlineView expandItem:node];
    }
}

- (NSInteger)outlineView:(NSOutlineView *)ov numberOfChildrenOfItem:(id)item {
    if (item == nil) {
        return (NSInteger)(_isSearching ? _filteredNodes.count : _rootNodes.count);
    }
    NavidromeNode *node = (NavidromeNode *)item;
    if (node.isLeaf) return 0;
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
        return [NavidromeNode loadingNode];
    }
    return node.children[(NSUInteger)index];
}

- (BOOL)outlineView:(NSOutlineView *)ov isItemExpandable:(id)item {
    NavidromeNode *node = (NavidromeNode *)item;
    return !node.isLeaf;
}

- (NSView *)outlineView:(NSOutlineView *)ov
     viewForTableColumn:(NSTableColumn *)tableColumn
                   item:(id)item {
    NavidromeNode *node = (NavidromeNode *)item;

    NSTextField *cell = [ov makeViewWithIdentifier:tableColumn.identifier owner:self];
    if (!cell) {
        cell = [NSTextField labelWithString:@""];
        cell.identifier = tableColumn.identifier;
    }

    if (node.type == NavidromeNodeTypeLoading || node.type == NavidromeNodeTypeError) {
        cell.textColor = [NSColor secondaryLabelColor];
        cell.stringValue = [tableColumn.identifier isEqualToString:@"name"] ? node.displayName : @"";
        return cell;
    }

    cell.textColor = [NSColor labelColor];

    navidrome::NodeDisplay d = navidrome::nodeDisplay([node coreNode]);

    if ([tableColumn.identifier isEqualToString:@"name"]) {
        cell.stringValue = @(d.name.c_str());
    } else if ([tableColumn.identifier isEqualToString:@"sub"]) {
        NSString *sub = @(d.subtitle.c_str());
        NSString *stars = @(d.ratingStars.c_str());
        if (stars.length)
            sub = sub.length ? [NSString stringWithFormat:@"%@  %@", sub, stars] : stars;
        if (!d.bookmarkText.empty()) {
            NSString *bm = @(d.bookmarkText.c_str());
            sub = sub.length ? [NSString stringWithFormat:@"%@  %@", sub, bm] : bm;
        }
        if (!d.infoText.empty()) {
            NSString *info = @(d.infoText.c_str());
            sub = sub.length ? [NSString stringWithFormat:@"%@  %@", sub, info] : info;
        }
        cell.stringValue = sub;
        cell.textColor = [NSColor secondaryLabelColor];
    } else if ([tableColumn.identifier isEqualToString:@"dur"]) {
        cell.stringValue = @(d.durationText.c_str());
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
        owner.vc.standalone = YES;

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

bool navidrome::promptForText(const char* title, const char* label, std::string& inOut) {
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = @(title);
    alert.informativeText = @(label);
    [alert addButtonWithTitle:@"OK"];
    [alert addButtonWithTitle:@"Cancel"];

    NSTextField *input = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 320, 24)];
    input.stringValue = @(inOut.c_str()) ?: @"";
    alert.accessoryView = input;
    [alert layout];
    [alert.window setInitialFirstResponder:input];

    if ([alert runModal] != NSAlertFirstButtonReturn) return false;
    [input validateEditing];
    NSString *trimmed = [input.stringValue stringByTrimmingCharactersInSet:
                         [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    inOut = trimmed.UTF8String ?: "";
    return true;
}
