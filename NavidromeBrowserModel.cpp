// Shared browser tree logic — the child-fetch dispatch, the root-node builder
// and the deep song collector, written once over the IBrowserClient seam so the
// Windows and macOS browser views stay identical. SDK-free (see the header).

#include "NavidromeBrowserModel.h"
#include "NavidromePlaylistSync.h"

namespace navidrome {

namespace {

// Push the freshly fetched server-side rating / favorite of these song nodes
// onto any matching playlist entry, so a value changed elsewhere (the Navidrome
// web UI, another client) catches up as soon as the user looks at the album
// here. Costs no extra request — the values arrived with the browse response.
void syncSongNodesToPlaylists(const std::vector<BrowserNodePtr>& nodes) {
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
    else                   syncSongNodesToPlaylists(out);
    return out;
}

void collectSongsDeep(IBrowserClient& client, const BrowserNodePtr& node,
                      std::vector<BrowserNodePtr>& out) {
    if (!node) return;
    if (node->type == BrowserNode::Song || node->type == BrowserNode::Radio) {
        out.push_back(node);
        return;
    }
    if (node->type == BrowserNode::Loading || node->type == BrowserNode::Error) return;

    if (node->childrenLoaded && !node->children.empty()) {
        for (const auto& c : node->children) collectSongsDeep(client, c, out);
        return;
    }

    std::string err;
    for (const auto& c : fetchChildren(client, *node, err))
        collectSongsDeep(client, c, out);
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

} // namespace navidrome
