#pragma once
#import <Foundation/Foundation.h>

@class SubsonicSong;

// Custom URI scheme used by foo_navidrome to represent a single playable track.
//
// Format: navidrome://track/<songId>?title=...&artist=...&album=...&tracknumber=N
//                                   &date=YYYY&duration=SEC&coverArt=...&suffix=mp3
//                                   &rating=N&starred=1&albumId=...
//
// Metadata is embedded in the URI so playlists render correctly without a network
// round-trip. The actual HTTP stream URL is built at decode time from the current
// Subsonic credentials, so playlists survive credential rotation / server URL
// changes.
//
// rating / starred are the server-side per-user values, snapshotted at enqueue
// time and surfaced as the NAVIDROME_RATING / NAVIDROME_STARRED tags. Both are
// omitted when unset, so URIs saved by older versions parse as unrated/unstarred.
//
// albumId is carried for the startup refresh only — it lets one getAlbum.view
// bring a whole album's playlist entries up to date instead of one request per
// track. The input handler never reads it back; the refresh pass reads it off
// the path with navidrome::queryParamFromURI().

extern NSString *const NavidromeURIScheme;     // @"navidrome"
extern NSString *const NavidromeURIPrefix;     // @"navidrome://track/"

// Builds a navidrome://track/<id>?... URI for a SubsonicSong.
NSString *NavidromeMakeTrackURI(SubsonicSong *song);

// Builds the same URI from discrete fields (used by NavidromeBrowserController,
// whose node objects don't carry a `suffix` field).
NSString *NavidromeMakeTrackURIWithFields(NSString *songId,
                                          NSString *title,
                                          NSString *artist,
                                          NSString *album,
                                          NSInteger track,
                                          NSInteger year,
                                          NSTimeInterval duration,
                                          NSString *coverArtId,
                                          NSString *suffix,
                                          NSInteger rating,
                                          BOOL starred,
                                          NSString *albumId);
