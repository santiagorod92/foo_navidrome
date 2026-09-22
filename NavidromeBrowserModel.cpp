// Shared browser tree logic — the child-fetch dispatch, the root-node builder
// and the deep song collector, written once over the IBrowserClient seam so the
// Windows and macOS browser views stay identical. SDK-free (see the header).

#include "NavidromeBrowserModel.h"
#include "NavidromePlaylistSync.h"

namespace navidrome {

void syncBrowserNodesToPlaylists(const std::vector<BrowserNodePtr>& nodes) {
    std::vector<RatingUpdate> updates;
    for (const auto& n : nodes) {
        if (!n || n->type != BrowserNode::Song || n->id.empty()) continue;
        RatingUpdate u;
        u.songId  = n->id;
        u.rating  = n->rating;
        u.starred = n->starred;
        updates.push_back(std::move(u));
    }
    syncRatingsToPlaylists(std::move(updates));
}

namespace {

BrowserNodePtr makeLibraryArtistNode(const Artist& a, const std::string& libraryId) {
    auto n = makeArtistNode(a);
    n->libraryId = libraryId;   // pin this artist's album fetch to the library
    return n;
}

} // namespace

std::vector<BrowserNodePtr> buildRootNodes(IBrowserClient& client, std::string& outError) {
    outError.clear();
    std::vector<BrowserNodePtr> out;

    // Multi-library server -> group the tree by library: one Library node per
    // library, each lazily expanding to its own artists. Single-library server
    // (or a one-library scope) -> flat artist list. The grouping decision is
    // the client's (groupingLibraryIds), independent of the "only selected
    // libraries" filter — see the Decisions note in CLAUDE.md.
    auto groupIds = client.groupingLibraryIds();
    if (groupIds.size() >= 2) {
        auto folders = client.musicFolders();   // warmed by groupingLibraryIds()
        for (auto& n : buildCategoryNodes()) out.push_back(n);
        for (const auto& id : groupIds) {
            std::string name = id;
            for (const auto& f : folders)
                if (f.id == id) { name = f.name; break; }
            out.push_back(makeLibraryNode(id, name));
        }
        return out;
    }

    auto artists = client.getArtists(outError);
    if (outError.empty())
        for (auto& n : buildCategoryNodes()) out.push_back(n);
    for (const auto& a : artists)
        out.push_back(makeArtistNode(a));
    return out;
}

std::vector<BrowserNodePtr> fetchChildren(IBrowserClient& client,
                                          const BrowserNode& node,
                                          std::string& outError) {
    outError.clear();
    std::vector<BrowserNodePtr> out;

    auto addSong = [&out](const Song& s, double bookmarkPositionMs = 0.0) {
        out.push_back(makeSongNode(s, bookmarkPositionMs));
    };

    switch (node.type) {
        case BrowserNode::Library:
            for (const auto& a : client.getArtistsForLibrary(node.id, outError))
                out.push_back(makeLibraryArtistNode(a, node.id));
            break;
        case BrowserNode::Artist:
            out.push_back(makeArtistSubNode(BrowserNode::CatArtistTopSongs, "Top Songs",
                                            node.id, node.displayName));
            out.push_back(makeArtistSubNode(BrowserNode::CatArtistSimilarArtists, "Similar Artists",
                                            node.id, node.displayName));
            for (const auto& a : client.getAlbumsForArtist(node.id, node.libraryId, outError))
                out.push_back(makeAlbumNode(a));
            break;
        case BrowserNode::Album:
            for (const auto& s : client.getSongsForAlbum(node.id, outError)) addSong(s);
            break;
        case BrowserNode::Playlist:
            for (const auto& s : client.getPlaylistSongs(node.id, outError)) addSong(s);
            break;
        case BrowserNode::Genre:
            // getSongsByGenre is paged; 500 covers all but the largest genres
            // and keeps a single request per expansion.
            for (const auto& s : client.getSongsForGenre(node.id, 500, outError)) addSong(s);
            break;
        case BrowserNode::PodcastChannel:
            for (const auto& e : client.getPodcastEpisodes(node.id, outError))
                out.push_back(makePodcastEpisodeNode(e));
            break;
        case BrowserNode::Category:
            switch (node.category) {
                case BrowserNode::CatStarred:
                    for (const auto& s : client.getStarredSongs(outError)) addSong(s);
                    break;
                case BrowserNode::CatGenres:
                    for (const auto& g : client.getGenres(outError))
                        out.push_back(makeGenreNode(g));
                    break;
                case BrowserNode::CatPlaylists:
                    for (const auto& p : client.getPlaylists(outError))
                        out.push_back(makePlaylistNode(p));
                    break;
                case BrowserNode::CatBookmarks:
                    for (const auto& b : client.getBookmarks(outError))
                        addSong(b.song, b.positionMs);
                    break;
                case BrowserNode::CatRadio:
                    for (const auto& s : client.getRadioStations(outError))
                        out.push_back(makeRadioNode(s));
                    break;
                case BrowserNode::CatPodcasts:
                    for (const auto& c : client.getPodcastChannels(outError))
                        out.push_back(makePodcastChannelNode(c));
                    break;
                case BrowserNode::CatNowPlaying:
                    for (const auto& e : client.getNowPlaying(outError)) {
                        auto n = makeSongNode(e.song);
                        n->infoText = e.username + " · " + std::to_string(e.minutesAgo) + "m ago";
                        out.push_back(n);
                    }
                    break;
                case BrowserNode::CatArtistTopSongs:
                    // node.subtitle carries the artist name (see makeArtistSubNode) —
                    // getTopSongs.view keys off the name, not the id.
                    for (const auto& s : client.getTopSongs(node.subtitle, 50, outError)) addSong(s);
                    break;
                case BrowserNode::CatArtistSimilarArtists: {
                    auto info = client.getArtistInfo(node.id, outError);
                    for (const auto& a : info.similarArtists) out.push_back(makeArtistNode(a));
                    break;
                }
                default:   // the four getAlbumList2-backed smart lists
                    for (const auto& a : client.getAlbumList(
                            albumListTypeForCategory(node.category), 100, outError))
                        out.push_back(makeAlbumNode(a));
                    break;
            }
            break;
        default:
            break;
    }

    if (!outError.empty()) out.clear();
    else                   syncBrowserNodesToPlaylists(out);
    return out;
}

// An artist's "Top Songs"/"Similar Artists" children are extra browsing
// entry points, not part of the artist's own discography — a deep walk that
// started at the Artist (Play/Add "whole artist") must skip them, or it picks
// up duplicate top tracks and drags in unrelated artists' entire catalogs.
// Selecting either node directly still works: this only filters them out of
// their *parent*'s recursion, and the function has no other special case for
// BrowserNode::Category, so a direct call on one of these nodes falls through
// to the normal fetch-then-recurse path below.
namespace {
bool isArtistSubCategory(const BrowserNodePtr& n) {
    return n->type == BrowserNode::Category &&
           (n->category == BrowserNode::CatArtistTopSongs ||
            n->category == BrowserNode::CatArtistSimilarArtists);
}
} // namespace

void collectSongsDeep(IBrowserClient& client, const BrowserNodePtr& node,
                      std::vector<BrowserNodePtr>& out) {
    if (!node) return;
    if (node->type == BrowserNode::Song || node->type == BrowserNode::Radio) {
        out.push_back(node);
        return;
    }
    if (node->type == BrowserNode::Loading || node->type == BrowserNode::Error) return;

    if (node->childrenLoaded && !node->children.empty()) {
        for (const auto& c : node->children)
            if (!isArtistSubCategory(c)) collectSongsDeep(client, c, out);
        return;
    }

    std::string err;
    for (const auto& c : fetchChildren(client, *node, err))
        if (!isArtistSubCategory(c)) collectSongsDeep(client, c, out);
}

std::vector<std::string> collectSongIdsDeep(IBrowserClient& client,
                                            const std::vector<BrowserNodePtr>& nodes) {
    std::vector<BrowserNodePtr> songs;
    for (const auto& n : nodes) collectSongsDeep(client, n, songs);

    std::vector<std::string> ids;
    ids.reserve(songs.size());
    for (const auto& s : songs)
        if (!s->id.empty()) ids.push_back(s->id);
    return ids;
}

StarRatingResult applyStarredToNodes(IBrowserClient& client,
                                     const std::vector<BrowserNodePtr>& targets,
                                     bool starred) {
    StarRatingResult result;
    for (const auto& n : targets) {
        StarKind kind = StarKind::Song;
        if (n->type == BrowserNode::Album)  kind = StarKind::Album;
        if (n->type == BrowserNode::Artist) kind = StarKind::Artist;

        std::string one;
        if (client.setStarred(starred, n->id, kind, one)) {
            n->starred = starred;
            ++result.done;
        } else if (result.error.empty()) {
            result.error = one;
        }
    }
    syncBrowserNodesToPlaylists(targets);
    return result;
}

StarRatingResult applyRatingToNodes(IBrowserClient& client,
                                    const std::vector<BrowserNodePtr>& targets,
                                    int stars) {
    StarRatingResult result;
    for (const auto& n : targets) {
        std::string one;
        if (client.setRating(stars, n->id, one)) {
            n->rating = stars;
            ++result.done;
        } else if (result.error.empty()) {
            result.error = one;
        }
    }
    syncBrowserNodesToPlaylists(targets);
    return result;
}

namespace {
std::vector<BrowserNodePtr> songsToNodes(const std::vector<Song>& songs) {
    std::vector<BrowserNodePtr> nodes;
    nodes.reserve(songs.size());
    for (const auto& s : songs) nodes.push_back(makeSongNode(s));
    return nodes;
}
} // namespace

std::vector<BrowserNodePtr> fetchSimilarSongs(IBrowserClient& client,
                                              const std::string& itemId, int count,
                                              std::string& outError) {
    return songsToNodes(client.getSimilarSongs(itemId, count, outError));
}

std::vector<BrowserNodePtr> fetchRandomMix(IBrowserClient& client, int count,
                                           std::string& outError) {
    return songsToNodes(client.getRandomSongs(count, outError));
}

} // namespace navidrome
