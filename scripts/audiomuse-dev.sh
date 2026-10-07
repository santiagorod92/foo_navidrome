#!/usr/bin/env bash
# audiomuse-dev.sh — drive the local AudioMuse-AI test stack (dev/audiomuse/docker-compose.yml)
# used to test foo_navidrome's AudioMuse features (issue #16) against a real AudioMuse.
#
#   audiomuse-dev.sh up          start the stack, wait for the API, check the host Ollama has the model
#   audiomuse-dev.sh analyze [N] analyse the N newest albums (default NUM_RECENT_ALBUMS from .env)
#   audiomuse-dev.sh status      API health, last/active task, a sample CLAP text search
#   audiomuse-dev.sh search TEXT one CLAP text search (what foo_navidrome's Text Search sends)
#   audiomuse-dev.sh logs        follow flask + worker logs
#   audiomuse-dev.sh down [-v]   stop the stack (-v also deletes the analysis + models)
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
DIR="$REPO/dev/audiomuse"
ENV="$DIR/.env"
compose() { docker compose --project-directory "$DIR" -f "$DIR/docker-compose.yml" "$@"; }

say()  { printf '\033[1;36m==> %s\033[0m\n' "$*"; }
fail() { printf '\033[1;31mERROR\033[0m %s\n' "$*" >&2; exit 1; }

[ -f "$ENV" ] || fail "no $ENV — cp dev/audiomuse/.env.example dev/audiomuse/.env and fill in the Navidrome credentials"
set -a; # shellcheck disable=SC1090
. "$ENV"; set +a
PORT=8000   # fixed in the AudioMuse image (gunicorn --bind 0.0.0.0:8000), host network
API="http://127.0.0.1:$PORT"
TOKEN="${AUDIOMUSE_API_TOKEN:-foo-navidrome-dev-token}"
MODEL="${OLLAMA_MODEL:-qwen3.5:9b}"

# Bearer-authenticated JSON call; prints the body.
api() {  # METHOD PATH [JSON]
  curl -sS -X "$1" -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' \
       ${3:+-d "$3"} "$API$2"
}

# AudioMuse persists its config in Postgres (app_config) on first start, and the
# stored values then win over the environment — editing .env later does nothing.
# Push the LLM settings from .env into the table; restart flask/worker if changed.
sync_llm_config() {
  local url="${OLLAMA_URL:-http://127.0.0.1:11434/api/generate}" changed=0 key val cur
  for kv in "AI_MODEL_PROVIDER=OLLAMA" "OLLAMA_SERVER_URL=$url" "OLLAMA_MODEL_NAME=$MODEL"; do
    key="${kv%%=*}"; val="${kv#*=}"
    cur="$(compose exec -T postgres psql -U audiomuse -d audiomusedb -tAc \
           "select value from app_config where key='$key'" 2>/dev/null || true)"
    [ -z "$cur" ] || [ "$cur" = "$val" ] && continue
    compose exec -T postgres psql -U audiomuse -d audiomusedb -qc \
      "update app_config set value='$val' where key='$key'" >/dev/null
    say "AudioMuse config $key: $cur -> $val"; changed=1
  done
  if [ "$changed" = 1 ]; then compose restart flask worker >/dev/null; wait_api; fi
}

wait_api() {
  say "waiting for AudioMuse-AI on $API ..."
  for _ in $(seq 1 120); do
    curl -sf "$API/api/health" >/dev/null 2>&1 && { say "API up"; return 0; }
    sleep 2
  done
  compose logs --tail 40 flask
  fail "AudioMuse-AI didn't answer /api/health within 4 minutes"
}

case "${1:-}" in
  up)
    compose up -d
    wait_api
    sync_llm_config
    # Instant Playlist needs the chat model on the host's Ollama (see .env.example).
    if [ -z "${OLLAMA_URL:-}" ]; then
      if curl -sf http://127.0.0.1:11434/api/tags | grep -q "\"name\":\"$MODEL\""; then
        say "host Ollama has $MODEL"
      else
        printf '\033[1;33m  !!\033[0m host Ollama lacks %s — Instant Playlist will fail until: ollama pull %s\n' "$MODEL" "$MODEL"
      fi
    fi
    say "ready. foobar2000 > Preferences > Tools > Navidrome > AudioMuse-AI:"
    echo "    Server URL: http://127.0.0.1:$PORT    API token: AUDIOMUSE_API_TOKEN in dev/audiomuse/.env"
    echo "    (macOS VM guest: reach it via the host's LAN IP — needs a ufw allow rule for :8000)"
    echo "    Next: make audiomuse-analyze — searches return nothing until the library is analysed."
    ;;
  analyze)
    n="${2:-${NUM_RECENT_ALBUMS:-10}}"
    say "starting analysis of the $n newest albums"
    api POST /api/analysis/start "{\"num_recent_albums\": $n}"; echo
    echo "    progress: make audiomuse-status  (or the web UI at $API, user ${AUDIOMUSE_USER:-admin})"
    ;;
  status)
    say "health";       curl -sS "$API/api/health"; echo
    say "active tasks"; api GET /api/active_tasks; echo
    say "last task";    api GET /api/last_task | head -c 600; echo
    say "sample text search (\"rock\")"
    api POST /api/clap/search '{"query": "rock", "limit": 3}' | head -c 600; echo
    ;;
  search)
    shift; [ $# -gt 0 ] || fail "usage: audiomuse-dev.sh search TEXT"
    q="$(python3 -c 'import json,sys; print(json.dumps(" ".join(sys.argv[1:])))' "$@")"
    api POST /api/clap/search "{\"query\": $q, \"limit\": 10}"; echo
    ;;
  logs)  compose logs -f --tail 50 flask worker ;;
  down)  shift; compose down "$@" ;;
  *) sed -n '2,11p' "$0"; exit 2 ;;
esac
