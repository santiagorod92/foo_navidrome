#pragma once

namespace navidrome {

// The searcher script has no version of its own — it reports foo_navidrome's
// COMPONENT_VERSION (see version.txt / version_generated.h), threaded through
// at write time via config.componentVersion (buildEsLyricConfigJs). That way
// ESLyric's displayed searcher version always matches the installed build
// instead of a literal that has to be bumped by hand on every release.
inline constexpr char kEsLyricScriptSource[] = R"SCRIPT(import { config } from '../lib/foo_navidrome/config.js';

export function getConfig(cfg) {
  cfg.name = 'Navidrome (foo_navidrome)';
  cfg.version = config.componentVersion || '0.0.0';
  cfg.author = 'foo_navidrome';
  cfg.useRawMeta = false;
}

const unsupportedServers = Object.create(null);
let debugLogged = false;

function decodeOnce(value) {
  try { return decodeURIComponent(value); } catch (_) { return value; }
}

function queryValue(path, name) {
  const queryIndex = path.indexOf('?');
  if (queryIndex < 0) return '';
  const pairs = path.substring(queryIndex + 1).split('&');
  for (const pair of pairs) {
    const equals = pair.indexOf('=');
    if (equals >= 0 && pair.substring(0, equals) === name)
      return decodeOnce(pair.substring(equals + 1));
  }
  return '';
}

function parseSongId(rawPath) {
  if (!rawPath) return '';
  const prefix = 'navidrome://track/';
  if (rawPath.startsWith(prefix)) {
    const end = rawPath.indexOf('?', prefix.length);
    return decodeOnce(rawPath.substring(prefix.length,
      end < 0 ? rawPath.length : end));
  }
  if (rawPath.indexOf('/rest/stream.view') >= 0) return queryValue(rawPath, 'id');
  return '';
}

function endpointUrl(endpoint, parameters) {
  let url = config.serverUrl;
  while (url.endsWith('/')) url = url.substring(0, url.length - 1);
  url += '/rest/' + endpoint + '?u=' + encodeURIComponent(config.username);
  url += '&t=' + encodeURIComponent(config.token);
  url += '&s=' + encodeURIComponent(config.salt);
  url += '&v=1.16.1&c=foo_navidrome&f=json';
  if (parameters) url += '&' + parameters;
  return url;
}

function responseRoot(body) {
  try {
    const parsed = JSON.parse(body);
    const root = parsed && parsed['subsonic-response'];
    return root && root.status !== 'failed' ? root : null;
  } catch (_) {
    return null;
  }
}

function asArray(value) {
  if (!value) return [];
  return Array.isArray(value) ? value : [value];
}

function lrcTime(milliseconds) {
  const value = Math.max(0, Math.floor(milliseconds));
  const minutes = Math.floor(value / 60000);
  const seconds = Math.floor(value / 1000) % 60;
  const centiseconds = Math.floor(value / 10) % 100;
  const pad2 = number => (number < 10 ? '0' : '') + number;
  return '[' + minutes + ':' + pad2(seconds) + '.' + pad2(centiseconds) + ']';
}

function lyricText(entry) {
  const lines = asArray(entry && entry.line);
  if (!lines.length) return '';
  if (!entry.synced)
    return lines.map(line => line && line.value !== undefined ? String(line.value) : '').join('\n');
  const offset = Number(entry.offset || 0);
  const result = [];
  for (const line of lines) {
    if (!line || line.start === undefined || line.value === undefined) continue;
    result.push(lrcTime(Number(line.start) - offset) + String(line.value));
  }
  return result.join('\n');
}

function addCandidate(meta, man, text) {
  if (!text) return;
  const lyric = man.createLyric();
  lyric.title = meta.title;
  lyric.artist = meta.artist;
  lyric.album = meta.album;
  lyric.lyricText = text;
  man.addLyric(lyric);
}

function requestLegacy(meta, man) {
  if (man.checkAbort()) return;
  const artist = meta.rawArtist || meta.artist || '';
  const title = meta.rawTitle || meta.title || '';
  const parameters = 'artist=' + encodeURIComponent(artist) +
    '&title=' + encodeURIComponent(title);
  request({ url: endpointUrl('getLyrics.view', parameters), timeout: 5000,
    headers: config.headers }, (err, res, body) => {
      if (err || !res || res.statusCode !== 200 || man.checkAbort()) return;
      const root = responseRoot(body);
      const value = root && root.lyrics && root.lyrics.value;
      if (value !== undefined && String(value).length)
        addCandidate(meta, man, String(value));
    });
}

export function getLyrics(meta, man) {
  if (!config.serverUrl || !config.username || !config.token || man.checkAbort()) return;
  const rawPath = meta.rawPath || meta.path || '';
  if (config.debug && !debugLogged) {
    const safePath = rawPath.startsWith('navidrome://') ? rawPath : rawPath.split('?')[0];
    console.log('[foo_navidrome] meta.rawPath = ' + safePath);
    debugLogged = true;
  }
  const songId = parseSongId(rawPath);
  if (!songId) return;

  if (unsupportedServers[config.serverUrl]) {
    requestLegacy(meta, man);
    return;
  }

  request({ url: endpointUrl('getLyricsBySongId.view',
    'id=' + encodeURIComponent(songId)), timeout: 5000,
    headers: config.headers }, (err, res, body) => {
      if (err || !res || man.checkAbort()) return;
      if (res.statusCode === 404) {
        unsupportedServers[config.serverUrl] = true;
        requestLegacy(meta, man);
        return;
      }
      if (res.statusCode !== 200) return;
      const root = responseRoot(body);
      const entries = asArray(root && root.lyricsList && root.lyricsList.structuredLyrics);
      entries.sort((left, right) => Number(Boolean(right.synced)) - Number(Boolean(left.synced)));
      for (const entry of entries) {
        if (man.checkAbort()) return;
        addCandidate(meta, man, lyricText(entry));
      }
    });
}
)SCRIPT";

} // namespace navidrome
