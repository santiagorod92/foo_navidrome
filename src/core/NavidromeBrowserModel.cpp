#include "NavidromeBrowserModel.h"
#include "NavidromePlaylistSync.h"

#include <unordered_set>

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
    n->libraryId = libraryId;
    return n;
}
}

std::vector<BrowserNodePtr> buildRootNodes(IBrowserClient& client, std::string& outError) {
    outError.clear();
    std::vector<BrowserNodePtr> out;
    const auto hidden = client.hiddenCategories();

    auto groupIds = client.groupingLibraryIds();
    if (groupIds.size() >= 2) {
        auto folders = client.musicFolders();
        for (auto& n : buildCategoryNodes(hidden)) out.push_back(n);
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
        for (auto& n : buildCategoryNodes(hidden)) out.push_back(n);
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
                    for (const auto& s : client.getTopSongs(node.subtitle, 50, outError)) addSong(s);
                    break;
                case BrowserNode::CatAllSongs:
                    for (const auto& s : client.getAllSongs(outError)) addSong(s);
                    break;
                case BrowserNode::CatArtistSimilarArtists: {
                    auto info = client.getArtistInfo(node.id, outError);
                    for (const auto& a : info.similarArtists) out.push_back(makeArtistNode(a));
                    break;
                }
                default:
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

namespace {

bool isArtistSubCategory(const BrowserNodePtr& n) {
    return n->type == BrowserNode::Category &&
           (n->category == BrowserNode::CatArtistTopSongs ||
            n->category == BrowserNode::CatArtistSimilarArtists);
}
}

void collectSongsDeep(IBrowserClient& client, const BrowserNodePtr& node,
                      std::vector<BrowserNodePtr>& out, std::string* error) {
    if (!node) return;
    if (node->type == BrowserNode::Song || node->type == BrowserNode::Radio) {
        out.push_back(node);
        return;
    }
    if (node->type == BrowserNode::Loading || node->type == BrowserNode::Error) return;

    if (node->childrenLoaded && !node->children.empty()) {
        for (const auto& c : node->children)
            if (!isArtistSubCategory(c)) collectSongsDeep(client, c, out, error);
        return;
    }

    std::string err;
    auto children = fetchChildren(client, *node, err);
    if (!err.empty() && error && error->empty()) *error = err;
    for (const auto& c : children)
        if (!isArtistSubCategory(c)) collectSongsDeep(client, c, out, error);
}

std::vector<BrowserNodePtr> collectSelectionSongs(IBrowserClient& client,
                                                  const std::vector<BrowserNodePtr>& nodes,
                                                  std::string* error) {
    std::vector<BrowserNodePtr> out;
    std::unordered_set<std::string> fromEarlierNodes;
    for (const auto& n : nodes) {
        std::vector<BrowserNodePtr> part;
        collectSongsDeep(client, n, part, error);
        for (const auto& s : part)
            if (s->id.empty() || !fromEarlierNodes.count(s->id)) out.push_back(s);
        for (const auto& s : part)
            if (!s->id.empty()) fromEarlierNodes.insert(s->id);
    }
    return out;
}

std::string queueProblemMessage(bool gotSongs, const std::string& error, bool reloaded) {
    const std::string cause = error.empty() ? "" : "\n\nServer error: " + error;
    if (gotSongs) {
        if (error.empty()) return "";
        return "Some of the selected items couldn't be loaded from the Navidrome server, "
               "so only part of the selection was queued.\n\n"
               "If the server rescanned its library since the list loaded, press Refresh "
               "in the Navidrome Browser and try again." + cause;
    }
    if (reloaded)
        return "Couldn't load the tracks of the selected item from the Navidrome server.\n\n"
               "The server's library probably changed since the list loaded (a rescan can "
               "give artists and albums new ids), so the list was reloaded. Select the item "
               "again and retry. If it keeps failing, check the connection to the server and "
               "press Refresh." + cause;
    if (!error.empty())
        return "Couldn't load the tracks of the selected item from the Navidrome server.\n\n"
               "Press Refresh in the Navidrome Browser and try again." + cause;
    return "The Navidrome server returned no tracks for the selected item.\n\n"
           "If you expected some, the list may be out of date: press Refresh in the "
           "Navidrome Browser and try again.";
}

std::vector<std::string> collectSongIdsDeep(IBrowserClient& client,
                                            const std::vector<BrowserNodePtr>& nodes) {
    std::vector<BrowserNodePtr> songs = collectSelectionSongs(client, nodes);

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
}

std::vector<BrowserNodePtr> fetchSimilarSongs(IBrowserClient& client,
                                              const std::string& itemId, int count,
                                              std::string& outError) {
    return songsToNodes(client.getSimilarSongs(itemId, count, outError));
}

std::vector<BrowserNodePtr> fetchRandomMix(IBrowserClient& client, int count,
                                           std::string& outError) {
    return songsToNodes(client.getRandomSongs(count, outError));
}

bool listLibraryAlbums(IBrowserClient& client, const std::function<bool()>& aborted,
                       const std::function<void(const Album&)>& onAlbum, std::string& outError) {
    std::string err;
    const std::vector<Artist> artists = client.getArtists(err);
    if (artists.empty() && !err.empty()) { outError = err; return false; }
    for (const Artist& artist : artists) {
        if (aborted && aborted()) { outError = "aborted"; return false; }
        std::string aerr;
        for (Album al : client.getAlbumsForArtist(artist.id, std::string(), aerr)) {
            if (al.artist.empty()) al.artist = artist.name;
            if (al.artistId.empty()) al.artistId = artist.id;
            if (al.coverArtId.empty()) al.coverArtId = al.id;
            onAlbum(al);
        }
    }
    return true;
}

std::vector<BrowserNodePtr> collectArtistSongs(IBrowserClient& client, const std::string& artistId,
                                               std::string& outError) {
    std::vector<BrowserNodePtr> nodes;
    for (const Album& al : client.getAlbumsForArtist(artistId, std::string(), outError)) {
        std::string serr;
        for (const Song& s : client.getSongsForAlbum(al.id, serr)) nodes.push_back(makeSongNode(s));
    }
    return nodes;
}

LyricsCache& lyricsCache() {
    static LyricsCache cache;
    return cache;
}

Lyrics lyricsForTrackURI(IBrowserClient& client, const std::string& uri, std::string& outError) {
    const TrackURI t = parseTrackURI(uri);
    if (t.id.empty()) return {};
    Lyrics l;
    if (lyricsCache().get(t.id, l)) return l;
    std::string err;
    l = client.getLyrics(t.id, t.artist, t.title, err);
    if (!err.empty()) {
        outError = err;
        return {};
    }
    lyricsCache().put(t.id, l);
    return l;
}
}
