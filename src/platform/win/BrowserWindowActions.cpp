#include "stdafx.h"
#include "BrowserWindow.h"
#include "BrowserPrompts.h"
#include "SubsonicClientWin.h"
#include "WinUi.h"
#include "../../core/NavidromeBrowserEnqueue.h"
#include "../../core/NavidromeLibraryPlatform.h"
#include "../../core/NavidromeDebugLog.h"
#include <SDK/playlist.h>
#include <SDK/metadb.h>
#include <SDK/playback_control.h>
#include <algorithm>
#include <string>
#include <thread>
#include <vector>

using navidrome::win::u8ToWide;
using navidrome::win::wToU8;
static inline void dbgLog(const std::string& msg) { NAVIDROME_LOG("UI", msg); }

void BrowserWindow::OnStar(UINT, int, HWND)   { dbgLog("OnStar fired");   applyStarred(true); }
void BrowserWindow::OnUnstar(UINT, int, HWND) { dbgLog("OnUnstar fired"); applyStarred(false); }

void BrowserWindow::applyStarred(bool starred) {
    std::vector<std::shared_ptr<NavidromeNode>> targets;
    for (auto& n : selectedNodes()) {
        if (n->type == NavidromeNode::Song ||
            n->type == NavidromeNode::Album ||
            n->type == NavidromeNode::Artist)
            targets.push_back(n);
    }
    if (targets.empty()) { setStatus("Select a song, album or artist first"); return; }

    std::thread([this, targets, starred]() {
        auto result = navidrome::applyStarredToNodes(navidrome::libraryClient(), targets, starred);
        if (!result.error.empty())
            NAVIDROME_WARN("UI", std::string(starred ? "star" : "unstar") +
                " failed: " + result.error);
        fb2k::inMainThread([this, targets, starred, result]() {
            if (!IsWindow()) return;
            for (auto& n : targets) refreshLabel(n);
            setStatus(result.error.empty()
                ? (starred ? "Starred " : "Unstarred ") + std::to_string(result.done) + " item(s)"
                : "Error: " + result.error);
        });
    }).detach();
}

void BrowserWindow::OnRemoveBookmark(UINT, int, HWND) { applyRemoveBookmark(); }

void BrowserWindow::applyRemoveBookmark() {
    std::vector<std::shared_ptr<NavidromeNode>> songs;
    for (auto& n : selectedNodes())
        if (n->type == NavidromeNode::Song) songs.push_back(n);
    if (songs.empty()) { setStatus("Select one or more songs"); return; }

    std::thread([this, songs]() {
        std::string err;
        std::size_t done = 0;
        for (auto& n : songs) {
            std::string one;
            if (navidrome::SubsonicClientWin::get().deleteBookmark(n->id, one)) {
                n->bookmarkPositionMs = 0.0;
                ++done;
            } else {
                NAVIDROME_WARN("UI", "remove bookmark " + n->id + ": " + one);
                if (err.empty()) err = one;
            }
        }
        fb2k::inMainThread([this, songs, done, err]() {
            if (!IsWindow()) return;
            for (auto& n : songs) refreshLabel(n);
            invalidateBookmarksCategory();
            setStatus(err.empty()
                ? "Removed " + std::to_string(done) + " bookmark(s)"
                : "Error: " + err);
        });
    }).detach();
}

void BrowserWindow::OnRate(UINT, int id, HWND) {
    int stars = id - IDC_RATE_0;
    dbgLog("OnRate fired: id=" + std::to_string(id) + " stars=" + std::to_string(stars));
    if (stars < 0 || stars > 5) { dbgLog("OnRate: stars out of range, aborting"); return; }

    std::vector<std::shared_ptr<NavidromeNode>> songs;
    for (auto& n : selectedNodes())
        if (n->type == NavidromeNode::Song) songs.push_back(n);
    dbgLog("OnRate: selected song count=" + std::to_string(songs.size()));
    if (songs.empty()) { dbgLog("OnRate: no song-type nodes selected, aborting"); setStatus("Select one or more songs to rate"); return; }

    std::thread([this, songs, stars]() {
        auto result = navidrome::applyRatingToNodes(navidrome::libraryClient(), songs, stars);
        if (!result.error.empty())
            NAVIDROME_WARN("UI", "OnRate: stars=" + std::to_string(stars) +
                " failed: " + result.error);
        fb2k::inMainThread([this, songs, result]() {
            if (!IsWindow()) return;
            for (auto& n : songs) refreshLabel(n);
            setStatus(result.error.empty()
                ? "Rated " + std::to_string(songs.size()) + " song(s)"
                : "Error: " + result.error);
        });
    }).detach();
}

void BrowserWindow::OnSendActivePlaylist(UINT, int, HWND) {
    auto pm = playlist_manager::get();
    t_size pl = pm->get_active_playlist();
    if (pl == pfc_infinite) { setStatus("No active playlist"); return; }

    pfc::string8 pfcName;
    pm->playlist_get_name(pl, pfcName);
    metadb_handle_list items;
    pm->playlist_get_all_items(pl, items);

    std::vector<std::string> songIds;
    std::size_t skipped = 0;
    for (t_size i = 0; i < items.get_count(); ++i) {
        std::string id = navidrome::trackIdFromURI(items[i]->get_path());
        if (id.empty()) { ++skipped; continue; }
        songIds.push_back(id);
    }
    if (songIds.empty()) {
        setStatus("No Navidrome tracks in the active playlist");
        return;
    }

    std::string name = pfcName.is_empty() ? "foobar2000" : pfcName.c_str();
    setStatus("Uploading playlist…");

    std::thread([this, name, songIds, skipped]() {
        std::string err;
        navidrome::SubsonicClientWin::get().createPlaylist(name, songIds, err);
        bool ok = err.empty();
        if (!ok) NAVIDROME_WARN("UI", "OnSendActivePlaylist \"" + name + "\" failed: " + err);
        fb2k::inMainThread([this, name, songIds, skipped, ok, err]() {
            if (!IsWindow()) return;
            if (!ok) {
                setStatus("Upload failed: " + err);
                return;
            }
            std::string msg = "Sent \"" + name + "\" (" +
                              std::to_string(songIds.size()) + " tracks";
            if (skipped > 0)
                msg += ", " + std::to_string(skipped) + " non-Navidrome skipped";
            setStatus(msg + ")");
            invalidatePlaylistsCategory();
            refreshServerPlaylists();
        });
    }).detach();
}

void BrowserWindow::OnDownload(UINT, int, HWND) {
    auto selected = selectedNodes();
    if (selected.empty()) { setStatus("Select at least one item"); return; }

    std::wstring destDir;
    if (!navidrome::win::pickFolder(*this, destDir)) return;

    setStatus("Resolving tracks…");
    std::thread([this, destDir, selected]() {
        auto songs = navidrome::collectSelectionSongs(navidrome::libraryClient(), selected);

        std::size_t done = 0, failed = 0;
        for (std::size_t i = 0; i < songs.size(); ++i) {
            const std::size_t position = i + 1, total = songs.size();
            fb2k::inMainThread([this, position, total]() {
                if (IsWindow())
                    setStatus("Downloading " + std::to_string(position) + "/" +
                              std::to_string(total) + "…");
            });

            auto& s = songs[i];
            std::string name;
            if (s->track > 0) {
                char buf[8];
                snprintf(buf, sizeof(buf), "%02d. ", s->track);
                name += buf;
            }
            if (!s->subtitle.empty()) name += s->subtitle + " - ";
            name += s->displayName.empty() ? "untitled" : s->displayName;
            name = navidrome::sanitizeFileName(name);
            if (!s->suffix.empty()) name += "." + s->suffix;

            std::string err;
            std::string url = navidrome::SubsonicClientWin::get().downloadURL(s->id);
            if (navidrome::SubsonicClientWin::get()
                    .httpDownloadToFile(url, destDir + L"\\" + u8ToWide(name), err))
                ++done;
            else
                ++failed;
        }

        fb2k::inMainThread([this, done, failed]() {
            if (!IsWindow()) return;
            setStatus(failed == 0
                ? "Downloaded " + std::to_string(done) + " track(s)"
                : "Downloaded " + std::to_string(done) + ", " +
                  std::to_string(failed) + " failed");
        });
    }).detach();
}

std::vector<std::string> BrowserWindow::collectSongIdsDeep(
        const std::vector<std::shared_ptr<NavidromeNode>>& nodes) {
    return navidrome::collectSongIdsDeep(navidrome::libraryClient(), nodes);
}

std::shared_ptr<NavidromeNode> BrowserWindow::singleSelectedPlaylist() {
    auto sel = selectedNodes();
    if (sel.size() != 1 || sel[0]->type != NavidromeNode::Playlist) return nullptr;
    return sel[0];
}

void BrowserWindow::reloadNodeChildren(const std::shared_ptr<NavidromeNode>& node) {
    if (!node || !NodeItem(node)) return;

    m_tree.Expand(NodeItem(node), TVE_COLLAPSE);
    HTREEITEM child = m_tree.GetChildItem(NodeItem(node));
    while (child) {
        HTREEITEM next = m_tree.GetNextSiblingItem(child);
        m_nodeMap.erase(child);
        m_tree.DeleteItem(child);
        child = next;
    }
    node->children.clear();
    node->childrenLoaded = false;
    node->isLoading      = false;

    TVITEM it    = {};
    it.mask      = TVIF_CHILDREN;
    it.hItem     = NodeItem(node);
    it.cChildren = 1;
    m_tree.SetItem(&it);
}

void BrowserWindow::invalidatePlaylistNode(const std::string& playlistId) {
    for (auto& root : m_rootNodes) {
        if (root->type != NavidromeNode::Category ||
            root->category != NavidromeNode::CatPlaylists) continue;
        for (auto& pl : root->children) {
            if (pl->id != playlistId) continue;
            reloadNodeChildren(pl);
            return;
        }
    }
}

void BrowserWindow::invalidatePlaylistsCategory() {
    for (auto& root : m_rootNodes) {
        if (root->type == NavidromeNode::Category &&
            root->category == NavidromeNode::CatPlaylists) {
            reloadNodeChildren(root);
            return;
        }
    }
}

void BrowserWindow::invalidateBookmarksCategory() {
    for (auto& root : m_rootNodes) {
        if (root->type == NavidromeNode::Category &&
            root->category == NavidromeNode::CatBookmarks) {
            reloadNodeChildren(root);
            return;
        }
    }
}

void BrowserWindow::OnAddToServerPlaylist(UINT, int id, HWND) {
    const std::size_t idx = static_cast<std::size_t>(id - IDC_PLAYLIST_FIRST);
    if (idx >= m_serverPlaylists.size()) return;
    const std::string playlistId = m_serverPlaylists[idx].id;
    const std::string name       = m_serverPlaylists[idx].name;

    auto selected = selectedNodes();
    if (selected.empty()) { setStatus("Select at least one item"); return; }

    setStatus("Resolving tracks…");
    std::thread([this, playlistId, name, selected]() {
        auto ids = collectSongIdsDeep(selected);
        if (ids.empty()) {
            fb2k::inMainThread([this]() {
                if (IsWindow()) setStatus("No tracks in the selection");
            });
            return;
        }
        std::string err;
        bool ok = navidrome::SubsonicClientWin::get().addToPlaylist(playlistId, ids, err);
        if (!ok) NAVIDROME_WARN("UI", "OnAddToServerPlaylist \"" + name + "\" failed: " + err);
        fb2k::inMainThread([this, playlistId, name, ids, ok, err]() {
            if (!IsWindow()) return;
            setStatus(ok ? "Added " + std::to_string(ids.size()) + " track(s) to \"" + name + "\""
                         : "Failed: " + (err.empty() ? "unknown error" : err));
            if (ok) { invalidatePlaylistNode(playlistId); refreshServerPlaylists(); }
        });
    }).detach();
}

void BrowserWindow::OnNewServerPlaylist(UINT, int, HWND) {
    auto selected = selectedNodes();
    if (selected.empty()) { setStatus("Select at least one item"); return; }

    std::wstring name;
    if (!navidrome::win::promptText(*this, L"New Navidrome playlist",
                               L"Name for the new playlist:", L"", name))
        return;
    std::string nameU8 = wToU8(name);
    if (nameU8.empty()) return;

    setStatus("Resolving tracks…");
    std::thread([this, nameU8, selected]() {
        auto ids = collectSongIdsDeep(selected);
        std::string err;
        std::string newId =
            navidrome::SubsonicClientWin::get().createPlaylist(nameU8, ids, err);
        bool ok = err.empty();
        if (!ok) NAVIDROME_WARN("UI", "OnNewServerPlaylist \"" + nameU8 + "\" failed: " + err);
        fb2k::inMainThread([this, nameU8, ids, ok, err]() {
            if (!IsWindow()) return;
            setStatus(ok ? "Created \"" + nameU8 + "\" (" +
                           std::to_string(ids.size()) + " track(s))"
                         : "Failed: " + err);
            if (ok) { invalidatePlaylistsCategory(); refreshServerPlaylists(); }
        });
    }).detach();
}

void BrowserWindow::OnRemoveFromPlaylist(UINT, int, HWND) {
    std::shared_ptr<NavidromeNode> playlist;
    std::vector<int> indexes;

    for (auto& n : selectedNodes()) {
        if (n->type != NavidromeNode::Song || !NodeItem(n)) continue;
        auto parent = nodeForItem(m_tree.GetParentItem(NodeItem(n)));
        if (!parent || parent->type != NavidromeNode::Playlist) continue;
        if (playlist && playlist->id != parent->id) continue;
        playlist = parent;
        for (std::size_t i = 0; i < parent->children.size(); ++i) {
            if (parent->children[i] == n) { indexes.push_back(static_cast<int>(i)); break; }
        }
    }

    if (!playlist || indexes.empty()) {
        setStatus("Select tracks inside a server playlist first");
        return;
    }

    const std::string playlistId = playlist->id;
    const std::string name       = playlist->displayName;
    setStatus("Removing…");
    std::thread([this, playlistId, name, indexes]() {
        std::string err;
        bool ok = navidrome::SubsonicClientWin::get()
                      .removeFromPlaylist(playlistId, indexes, err);
        if (!ok) NAVIDROME_WARN("UI", "OnRemoveFromPlaylist \"" + name + "\" failed: " + err);
        fb2k::inMainThread([this, playlistId, name, indexes, ok, err]() {
            if (!IsWindow()) return;
            setStatus(ok ? "Removed " + std::to_string(indexes.size()) +
                           " track(s) from \"" + name + "\""
                         : "Failed: " + (err.empty() ? "unknown error" : err));
            if (ok) invalidatePlaylistNode(playlistId);
        });
    }).detach();
}

void BrowserWindow::OnRenamePlaylist(UINT, int, HWND) {
    auto playlist = singleSelectedPlaylist();
    if (!playlist) { setStatus("Select a single server playlist"); return; }

    std::wstring name;
    if (!navidrome::win::promptText(*this, L"Rename playlist", L"New name:",
                               u8ToWide(playlist->displayName), name))
        return;
    std::string nameU8 = wToU8(name);
    if (nameU8.empty() || nameU8 == playlist->displayName) return;

    const std::string playlistId = playlist->id;
    std::thread([this, playlist, playlistId, nameU8]() {
        std::string err;
        bool ok = navidrome::SubsonicClientWin::get()
                      .renamePlaylist(playlistId, nameU8, err);
        if (!ok) NAVIDROME_WARN("UI", "OnRenamePlaylist -> \"" + nameU8 + "\" failed: " + err);
        fb2k::inMainThread([this, playlist, nameU8, ok, err]() {
            if (!IsWindow()) return;
            if (ok) {
                playlist->displayName = nameU8;
                refreshLabel(playlist);
                setStatus("Renamed to \"" + nameU8 + "\"");
                refreshServerPlaylists();
            } else {
                setStatus("Failed: " + (err.empty() ? "unknown error" : err));
            }
        });
    }).detach();
}

void BrowserWindow::OnDeletePlaylist(UINT, int, HWND) {
    auto playlist = singleSelectedPlaylist();
    if (!playlist) { setStatus("Select a single server playlist"); return; }

    std::wstring prompt = L"Delete \"" + u8ToWide(playlist->displayName) +
                          L"\" from the server?\r\n\r\n"
                          L"The playlist is removed for every client. "
                          L"The tracks themselves are not touched.";
    if (MessageBoxW(prompt.c_str(), L"Delete playlist",
                    MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES)
        return;

    const std::string playlistId = playlist->id;
    const std::string name       = playlist->displayName;
    std::thread([this, playlistId, name]() {
        std::string err;
        bool ok = navidrome::SubsonicClientWin::get().deletePlaylist(playlistId, err);
        if (!ok) NAVIDROME_WARN("UI", "OnDeletePlaylist \"" + name + "\" failed: " + err);
        fb2k::inMainThread([this, name, ok, err]() {
            if (!IsWindow()) return;
            setStatus(ok ? "Deleted \"" + name + "\""
                         : "Failed: " + (err.empty() ? "unknown error" : err));
            if (ok) { invalidatePlaylistsCategory(); refreshServerPlaylists(); }
        });
    }).detach();
}

std::shared_ptr<NavidromeNode> BrowserWindow::singleSelectedRadioStation() {
    auto sel = selectedNodes();
    if (sel.size() != 1 || sel[0]->type != NavidromeNode::Radio) return nullptr;
    return sel[0];
}

void BrowserWindow::invalidateRadioCategory() {
    for (auto& root : m_rootNodes) {
        if (root->type == NavidromeNode::Category &&
            root->category == NavidromeNode::CatRadio) {
            reloadNodeChildren(root);
            return;
        }
    }
}

void BrowserWindow::OnNewRadioStation(UINT, int, HWND) {
    std::wstring name, streamUrl, homePageUrl;
    if (!navidrome::win::promptRadioStation(*this, L"New Radio Station", L"", L"", L"",
                                       name, streamUrl, homePageUrl))
        return;
    std::string nameU8 = wToU8(name), urlU8 = wToU8(streamUrl), homeU8 = wToU8(homePageUrl);
    if (nameU8.empty() || urlU8.empty()) {
        setStatus("Name and stream URL are required");
        return;
    }

    setStatus("Creating radio station…");
    std::thread([this, nameU8, urlU8, homeU8]() {
        std::string err;
        std::string result = navidrome::SubsonicClientWin::get()
                                  .createRadioStation(urlU8, nameU8, homeU8, err);
        bool ok = err.empty();
        if (!ok) NAVIDROME_WARN("UI", "create radio station \"" + nameU8 + "\": " + err);
        fb2k::inMainThread([this, nameU8, ok, err]() {
            if (!IsWindow()) return;
            setStatus(ok ? "Created \"" + nameU8 + "\""
                         : "Failed: " + (err.empty() ? "unknown error" : err));
            if (ok) { invalidateRadioCategory(); refreshRadioStations(); }
        });
    }).detach();
}

void BrowserWindow::OnEditRadioStation(UINT, int, HWND) {
    auto node = singleSelectedRadioStation();
    if (!node) { setStatus("Select a single radio station"); return; }

    std::string currentUrl = radioStationURL(node->id);
    std::wstring name, streamUrl, homePageUrl;
    if (!navidrome::win::promptRadioStation(*this, L"Edit Radio Station",
                                       u8ToWide(node->displayName),
                                       u8ToWide(currentUrl),
                                       u8ToWide(node->subtitle),
                                       name, streamUrl, homePageUrl))
        return;
    std::string nameU8 = wToU8(name), urlU8 = wToU8(streamUrl), homeU8 = wToU8(homePageUrl);
    if (nameU8.empty() || urlU8.empty()) {
        setStatus("Name and stream URL are required");
        return;
    }

    const std::string stationId = node->id;
    std::thread([this, stationId, nameU8, urlU8, homeU8]() {
        std::string err;
        bool ok = navidrome::SubsonicClientWin::get()
                      .updateRadioStation(stationId, urlU8, nameU8, homeU8, err);
        if (!ok) NAVIDROME_WARN("UI", "update radio station " + stationId + ": " + err);
        fb2k::inMainThread([this, nameU8, ok, err]() {
            if (!IsWindow()) return;
            setStatus(ok ? "Updated \"" + nameU8 + "\""
                         : "Failed: " + (err.empty() ? "unknown error" : err));
            if (ok) { invalidateRadioCategory(); refreshRadioStations(); }
        });
    }).detach();
}

void BrowserWindow::OnDeleteRadioStation(UINT, int, HWND) {
    auto node = singleSelectedRadioStation();
    if (!node) { setStatus("Select a single radio station"); return; }

    std::wstring prompt = L"Delete \"" + u8ToWide(node->displayName) +
                          L"\" from the server?\r\n\r\n"
                          L"The station is removed for every client.";
    if (MessageBoxW(prompt.c_str(), L"Delete radio station",
                    MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES)
        return;

    const std::string stationId = node->id;
    const std::string name      = node->displayName;
    std::thread([this, stationId, name]() {
        std::string err;
        bool ok = navidrome::SubsonicClientWin::get().deleteRadioStation(stationId, err);
        if (!ok) NAVIDROME_WARN("UI", "delete radio station " + stationId + ": " + err);
        fb2k::inMainThread([this, name, ok, err]() {
            if (!IsWindow()) return;
            setStatus(ok ? "Deleted \"" + name + "\""
                         : "Failed: " + (err.empty() ? "unknown error" : err));
            if (ok) { invalidateRadioCategory(); refreshRadioStations(); }
        });
    }).detach();
}

std::shared_ptr<NavidromeNode> BrowserWindow::singleSelectedPodcastChannel() {
    auto sel = selectedNodes();
    if (sel.size() != 1 || sel[0]->type != NavidromeNode::PodcastChannel) return nullptr;
    return sel[0];
}

void BrowserWindow::invalidatePodcastsCategory() {
    for (auto& root : m_rootNodes) {
        if (root->type == NavidromeNode::Category &&
            root->category == NavidromeNode::CatPodcasts) {
            reloadNodeChildren(root);
            return;
        }
    }
}

void BrowserWindow::OnSubscribePodcast(UINT, int, HWND) {
    std::wstring url;
    if (!navidrome::win::promptText(*this, L"Subscribe to Podcast",
                               L"Podcast RSS feed URL:", L"", url))
        return;
    std::string urlU8 = wToU8(url);
    if (urlU8.empty()) { setStatus("A feed URL is required"); return; }

    setStatus("Subscribing…");
    std::thread([this, urlU8]() {
        std::string err;
        navidrome::SubsonicClientWin::get().createPodcastChannel(urlU8, err);
        bool ok = err.empty();
        if (!ok) NAVIDROME_WARN("UI", "OnSubscribePodcast \"" + urlU8 + "\" failed: " + err);
        fb2k::inMainThread([this, ok, err]() {
            if (!IsWindow()) return;
            setStatus(ok ? "Subscribed" : "Failed: " + (err.empty() ? "unknown error" : err));
            if (ok) invalidatePodcastsCategory();
        });
    }).detach();
}

void BrowserWindow::OnUnsubscribePodcast(UINT, int, HWND) {
    auto node = singleSelectedPodcastChannel();
    if (!node) { setStatus("Select a single podcast"); return; }

    std::wstring prompt = L"Unsubscribe from \"" + u8ToWide(node->displayName) +
                          L"\"?\r\n\r\nDownloaded episodes are removed from the server.";
    if (MessageBoxW(prompt.c_str(), L"Unsubscribe from podcast",
                    MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES)
        return;

    const std::string channelId = node->id;
    const std::string name      = node->displayName;
    std::thread([this, channelId, name]() {
        std::string err;
        bool ok = navidrome::SubsonicClientWin::get().deletePodcastChannel(channelId, err);
        if (!ok) NAVIDROME_WARN("UI", "OnUnsubscribePodcast \"" + name + "\" failed: " + err);
        fb2k::inMainThread([this, name, ok, err]() {
            if (!IsWindow()) return;
            setStatus(ok ? "Unsubscribed from \"" + name + "\""
                         : "Failed: " + (err.empty() ? "unknown error" : err));
            if (ok) invalidatePodcastsCategory();
        });
    }).detach();
}

void BrowserWindow::OnRefresh(UINT, int, HWND) {
    m_search.SetWindowText(L"");
    loadArtists();
}

void BrowserWindow::OnSearchChanged(UINT, int, HWND) {
    SetTimer(kSearchDebounceTimer, kSearchDebounceMs, nullptr);
}

void BrowserWindow::OnTimer(UINT_PTR id) {
    if (id != kSearchDebounceTimer) return;
    KillTimer(kSearchDebounceTimer);

    wchar_t buf[256] = {};
    m_search.GetWindowText(buf, 256);
    std::string query = wToU8(buf);

    ++m_searchGeneration;

    if (query.size() < 2) {
        if (m_isSearching) restoreBrowseTree();
        return;
    }

    setStatus("Searching\u2026");
    std::uint64_t generation = m_searchGeneration;
    std::thread([this, query, generation]() {
        std::string err;
        auto results = navidrome::SubsonicClientWin::get().search(query, err);
        auto* payload = new LoadedPayload{};
        payload->error      = err;
        payload->generation = generation;
        for (auto& s : results.songs) {
            auto n = navidrome::makeSongNode(s);
            payload->nodes.push_back(n);
        }
        syncBrowserNodesToPlaylists(payload->nodes);
        if (!PostMessage(WM_NAVIDROME_SEARCH, reinterpret_cast<WPARAM>(payload), 0))
            delete payload;
    }).detach();
}

void BrowserWindow::enqueueNodes(std::vector<std::shared_ptr<NavidromeNode>> songs,
                                 bool play, bool clearFirst) {
    std::string status;
    navidrome::enqueueBrowserNodes(
        songs, play, clearFirst,
        [this](const std::string& id) { return radioStationURL(id); },
        status);
    setStatus(status);
}
