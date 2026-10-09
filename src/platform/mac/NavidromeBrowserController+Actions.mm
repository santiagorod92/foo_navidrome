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

@implementation NavidromeBrowserController (Actions)

- (IBAction)starSelection:(id)sender   { [self applyStarred:YES]; }
- (IBAction)unstarSelection:(id)sender { [self applyStarred:NO]; }

- (IBAction)removeBookmarkSelection:(id)sender {
    NSMutableArray<NavidromeNode *> *songs = [NSMutableArray array];
    for (NavidromeNode *n in [self selectedNodes])
        if (n.type == NavidromeNodeTypeSong) [songs addObject:n];
    if (songs.count == 0) { _statusLabel.stringValue = @"Select one or more songs"; return; }

    [_spinner startAnimation:nil];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSString *firstError = nil;
        NSMutableArray<NavidromeNode *> *cleared = [NSMutableArray array];
        for (NavidromeNode *n in songs) {
            NSError *err = nil;
            if ([SubsonicClient.sharedClient deleteBookmarkForSongId:n.nodeId error:&err]) {
                [cleared addObject:n];
            } else {
                NSString *msg = err.localizedDescription ?: @"unknown error";
                NAVIDROME_WARN("UI", "remove bookmark " + NBCStr(n.nodeId) + ": " + NBCStr(msg));
                if (!firstError) firstError = msg;
            }
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_spinner stopAnimation:nil];
            for (NavidromeNode *n in cleared) {
                n.bookmarkPositionMs = 0.0;
                [self->_outlineView reloadItem:n];
            }
            [self invalidateBookmarksCategory];
            self->_statusLabel.stringValue = firstError
                ? [NSString stringWithFormat:@"Error: %@", firstError]
                : [NSString stringWithFormat:@"Removed %lu bookmark(s)", (unsigned long)cleared.count];
        });
    });
}

- (void)invalidateBookmarksCategory {
    for (NavidromeNode *root in _rootNodes) {
        if (root.type != NavidromeNodeTypeCategory ||
            root.categoryKind != NavidromeCategoryBookmarks) continue;
        [root.children removeAllObjects];
        root.childrenLoaded = NO;
        [_outlineView collapseItem:root];
        [_outlineView reloadItem:root reloadChildren:YES];
        return;
    }
}

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

    std::vector<navidrome::BrowserNodePtr> core;
    core.reserve(targets.count);
    for (NavidromeNode *n in targets)
        core.push_back(std::make_shared<navidrome::BrowserNode>([n coreNode]));

    [_spinner startAnimation:nil];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        auto result = navidrome::applyStarredToNodes(NBCBrowserClient(), core, starred);
        if (!result.error.empty())
            NAVIDROME_WARN("UI", std::string(starred ? "star" : "unstar") +
                " failed: " + result.error);
        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_spinner stopAnimation:nil];
            for (NSUInteger i = 0; i < targets.count; i++)
                targets[i].starred = core[i]->starred;
            if (!result.error.empty()) {
                self->_statusLabel.stringValue =
                    [NSString stringWithFormat:@"Error: %s", result.error.c_str()];
            } else {
                self->_statusLabel.stringValue = [NSString stringWithFormat:@"%@ %lu item(s)",
                    starred ? @"Starred" : @"Unstarred", (unsigned long)result.done];
            }
            [self->_outlineView reloadData];
        });
    });
}

- (IBAction)setRatingFromMenu:(NSMenuItem *)item {
    NSInteger rating = item.tag;
    NSMutableArray<NavidromeNode *> *songs = [NSMutableArray array];
    for (NavidromeNode *n in [self selectedNodes])
        if (n.type == NavidromeNodeTypeSong) [songs addObject:n];

    if (songs.count == 0) {
        _statusLabel.stringValue = @"Select one or more songs to rate";
        return;
    }

    std::vector<navidrome::BrowserNodePtr> core;
    core.reserve(songs.count);
    for (NavidromeNode *n in songs)
        core.push_back(std::make_shared<navidrome::BrowserNode>([n coreNode]));

    [_spinner startAnimation:nil];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        auto result = navidrome::applyRatingToNodes(NBCBrowserClient(), core, (int)rating);
        if (!result.error.empty())
            NAVIDROME_WARN("UI", "setRatingFromMenu: rating=" + std::to_string((long)rating) +
                " failed: " + result.error);
        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_spinner stopAnimation:nil];
            for (NSUInteger i = 0; i < songs.count; i++)
                songs[i].rating = core[i]->rating;
            self->_statusLabel.stringValue = !result.error.empty()
                ? [NSString stringWithFormat:@"Error: %s", result.error.c_str()]
                : [NSString stringWithFormat:@"Rated %lu song(s)", (unsigned long)songs.count];
            [self->_outlineView reloadData];
        });
    });
}

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
        if (!ok)
            NAVIDROME_WARN("UI", "sendActivePlaylist \"" + NBCStr(name) + "\" failed: " +
                NBCStr(err.localizedDescription));
        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_spinner stopAnimation:nil];
            if (!ok) {
                self->_statusLabel.stringValue =
                    [NSString stringWithFormat:@"Upload failed: %@",
                     err.localizedDescription ?: @"Unknown error"];
                return;
            }
            self->_statusLabel.stringValue = skippedCount > 0
                ? [NSString stringWithFormat:@"Sent “%@” (%lu tracks, %lu non-Navidrome skipped)",
                   name, (unsigned long)songIds.count, (unsigned long)skippedCount]
                : [NSString stringWithFormat:@"Sent “%@” (%lu tracks)",
                   name, (unsigned long)songIds.count];
            [self invalidatePlaylistsCategory];
            [self refreshServerPlaylists];
        });
    });
}

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
        NSError *err = nil;
        NSMutableArray<NavidromeNode *> *songs = [self collectSelectionSongs:nodesCopy error:&err];
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

- (void)refreshServerPlaylists {
    if (_playlistsLoading || ![SubsonicClient.sharedClient isConfigured]) return;
    _playlistsLoading = YES;
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        NSArray<SubsonicPlaylist *> *lists =
            [SubsonicClient.sharedClient getPlaylistsWithError:&err];
        dispatch_async(dispatch_get_main_queue(), ^{
            self->_playlistsLoading = NO;
            if (!err && lists) self->_serverPlaylists = lists;
        });
    });
}

- (void)menuNeedsUpdate:(NSMenu *)menu {
    if (menu == _alchemyItem.menu) {
        _alchemyItem.hidden = !navidrome::audioMuseSettings().configured();
        return;
    }
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

    [self refreshServerPlaylists];
}

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
        NSError *err = nil;
        NSMutableArray<NavidromeNode *> *songs = [self collectSelectionSongs:nodesCopy error:&err];
        NSMutableArray<NSString *> *ids = [NSMutableArray array];
        for (NavidromeNode *s in songs)
            if (s.nodeId.length) [ids addObject:s.nodeId];

        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_spinner stopAnimation:nil];
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
            if (!ok)
                NAVIDROME_WARN("UI", "addSelectionToServerPlaylist \"" + NBCStr(target.name) +
                    "\" failed: " + NBCStr(one.localizedDescription));
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
            if (!newId)
                NAVIDROME_WARN("UI", "newServerPlaylist \"" + NBCStr(name) + "\" failed: " +
                    NBCStr(one.localizedDescription));
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

- (IBAction)removeFromPlaylist:(id)sender {
    NavidromeNode *playlist = nil;
    NSMutableArray<NSNumber *> *indexes = [NSMutableArray array];

    for (NavidromeNode *n in [self selectedNodes]) {
        if (n.type != NavidromeNodeTypeSong) continue;
        NavidromeNode *parent = [_outlineView parentForItem:n];
        if (!parent || parent.type != NavidromeNodeTypePlaylist) continue;
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
        if (!ok)
            NAVIDROME_WARN("UI", "removeFromPlaylist \"" + NBCStr(playlistName) + "\" failed: " +
                NBCStr(err.localizedDescription));
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
        if (!ok)
            NAVIDROME_WARN("UI", "renamePlaylist -> \"" + NBCStr(name) + "\" failed: " +
                NBCStr(err.localizedDescription));
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
        if (!ok)
            NAVIDROME_WARN("UI", "deletePlaylist \"" + NBCStr(playlistName) + "\" failed: " +
                NBCStr(err.localizedDescription));
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

- (NavidromeNode *)singleSelectedPlaylist {
    NSArray<NavidromeNode *> *sel = [self selectedNodes];
    if (sel.count != 1) return nil;
    return sel[0].type == NavidromeNodeTypePlaylist ? sel[0] : nil;
}

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

- (void)refreshRadioStations {
    if (![SubsonicClient.sharedClient isConfigured]) return;
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        NSArray<SubsonicRadioStation *> *stations =
            [SubsonicClient.sharedClient getRadioStationsWithError:&err];
        dispatch_async(dispatch_get_main_queue(), ^{
            if (!err && stations) self->_radioStations = stations;
        });
    });
}

- (SubsonicRadioStation *)radioStationForId:(NSString *)stationId {
    for (SubsonicRadioStation *s in _radioStations)
        if ([s.stationId isEqualToString:stationId]) return s;
    return nil;
}

- (NavidromeNode *)singleSelectedRadioStation {
    NSArray<NavidromeNode *> *sel = [self selectedNodes];
    if (sel.count != 1) return nil;
    return sel[0].type == NavidromeNodeTypeRadioStation ? sel[0] : nil;
}

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
        if (!result)
            NAVIDROME_WARN("UI", "create radio station \"" + NBCStr(name) + "\": " + NBCStr(err.localizedDescription ?: @"unknown error"));
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
        if (!ok)
            NAVIDROME_WARN("UI", "update radio station " + NBCStr(stationId) + ": " + NBCStr(err.localizedDescription ?: @"unknown error"));
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
        if (!ok)
            NAVIDROME_WARN("UI", "delete radio station " + NBCStr(stationId) + ": " + NBCStr(err.localizedDescription ?: @"unknown error"));
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

- (NavidromeNode *)singleSelectedPodcastChannel {
    NSArray<NavidromeNode *> *sel = [self selectedNodes];
    if (sel.count != 1) return nil;
    return sel[0].type == NavidromeNodeTypePodcastChannel ? sel[0] : nil;
}

- (void)invalidatePodcastsCategory {
    for (NavidromeNode *root in _rootNodes) {
        if (root.type != NavidromeNodeTypeCategory ||
            root.categoryKind != NavidromeCategoryPodcasts) continue;
        [root.children removeAllObjects];
        root.childrenLoaded = NO;
        [_outlineView collapseItem:root];
        [_outlineView reloadItem:root reloadChildren:YES];
        return;
    }
}

- (IBAction)subscribePodcast:(id)sender {
    NSString *url = [self promptForText:@"Subscribe to Podcast"
                                 message:@"Podcast RSS feed URL:"
                            initialValue:@""];
    if (url.length == 0) return;

    _statusLabel.stringValue = @"Subscribing…";
    [_spinner startAnimation:nil];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        [SubsonicClient.sharedClient createPodcastChannelWithURL:url error:&err];
        BOOL ok = err == nil;
        if (!ok) NAVIDROME_WARN("UI", "subscribePodcast \"" + NBCStr(url) + "\" failed: " +
                                 NBCStr(err.localizedDescription));
        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_spinner stopAnimation:nil];
            self->_statusLabel.stringValue = ok
                ? @"Subscribed"
                : [NSString stringWithFormat:@"Failed: %@",
                   err.localizedDescription ?: @"unknown error"];
            if (ok) [self invalidatePodcastsCategory];
        });
    });
}

- (IBAction)unsubscribePodcast:(id)sender {
    NavidromeNode *node = [self singleSelectedPodcastChannel];
    if (!node) { _statusLabel.stringValue = @"Select a single podcast"; return; }

    NSAlert *confirm = [[NSAlert alloc] init];
    confirm.messageText = [NSString stringWithFormat:@"Unsubscribe from “%@”?", node.displayName];
    confirm.informativeText = @"Downloaded episodes are removed from the server.";
    confirm.alertStyle = NSAlertStyleWarning;
    [confirm addButtonWithTitle:@"Unsubscribe"];
    [confirm addButtonWithTitle:@"Cancel"];
    if ([confirm runModal] != NSAlertFirstButtonReturn) return;

    NSString *channelId = node.nodeId;
    NSString *channelName = node.displayName;
    [_spinner startAnimation:nil];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        BOOL ok = [SubsonicClient.sharedClient deletePodcastChannel:channelId error:&err];
        if (!ok) NAVIDROME_WARN("UI", "unsubscribePodcast \"" + NBCStr(channelName) + "\" failed: " +
                                 NBCStr(err.localizedDescription));
        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_spinner stopAnimation:nil];
            self->_statusLabel.stringValue = ok
                ? [NSString stringWithFormat:@"Unsubscribed from “%@”", channelName]
                : [NSString stringWithFormat:@"Failed: %@",
                   err.localizedDescription ?: @"unknown error"];
            if (ok) [self invalidatePodcastsCategory];
        });
    });
}

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

- (NSString *)promptForText:(NSString *)title
                    message:(NSString *)message
               initialValue:(NSString *)initial {
    std::string value = initial.UTF8String ?: "";
    if (!navidrome::promptForText(title.UTF8String ?: "", message.UTF8String ?: "", value)) return nil;
    return @(value.c_str());
}

@end
