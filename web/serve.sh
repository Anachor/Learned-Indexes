#!/usr/bin/env bash
# Manage the results server as a systemd user service.
#
#   web/serve.sh --install [--port N]   write the unit, enable and start it (default port 8289)
#   web/serve.sh --up                   start it
#   web/serve.sh --down                 stop it
#   web/serve.sh --restart              restart it, e.g. after editing serve.py
#   web/serve.sh --status               show whether it is running, and its recent log
#
# --install writes ~/.config/systemd/user/results-server.service with this
# checkout's path and the port, and enables lingering so the server keeps
# running after logout and starts at boot. Re-running it replaces the unit, so
# it is also how to change the port or pick up a moved repo.

set -euo pipefail

NAME=results-server
PORT=8289
ACTION=

usage() {
    sed -n '4,8p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//' >&2
    exit 2
}

action() {
    if [ -n "$ACTION" ]; then
        echo "choose one of --install, --up, --down, --restart, --status" >&2
        exit 2
    fi
    ACTION="$1"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --install|--up|--down|--restart|--status)
            action "${1#--}"
            shift
            ;;
        -p|--port)
            [ $# -ge 2 ] || usage
            PORT="$2"
            shift 2
            ;;
        --port=*)
            PORT="${1#--port=}"
            shift
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo "unknown argument: $1" >&2
            usage
            ;;
    esac
done

[ -n "$ACTION" ] || usage

WEB="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UNITS="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
UNIT="$UNITS/$NAME.service"

installed() {
    if [ ! -e "$UNIT" ]; then
        echo "$NAME is not installed - run: $0 --install [--port N]" >&2
        exit 1
    fi
}

# The port the installed unit serves on, read back from its ExecStart line.
port() {
    sed -n 's/^ExecStart=.*--port \([0-9]*\).*/\1/p' "$UNIT"
}

# Wait briefly, then report whether the service came up.
check() {
    sleep 1
    if systemctl --user is-active --quiet "$NAME.service"; then
        echo "$NAME running on http://localhost:$(port)/"
    else
        echo "$NAME failed to start - see: $0 --status" >&2
        exit 1
    fi
}

install() {
    if ! [[ "$PORT" =~ ^[0-9]+$ ]] || [ "$PORT" -lt 1 ] || [ "$PORT" -gt 65535 ]; then
        echo "invalid port: $PORT" >&2
        exit 2
    fi

    # Take down any earlier install first. The unit may be a symlink into the
    # repo (from an older, linked install), so remove it rather than write
    # through it.
    systemctl --user disable --now "$NAME.service" >/dev/null 2>&1 || true
    rm -f "$UNIT"
    mkdir -p "$UNITS"

    cat > "$UNIT" <<EOF
[Unit]
Description=Experiment results server ($WEB/serve.py)
After=network-online.target

[Service]
ExecStart=$(command -v python3) -u $WEB/serve.py --port $PORT
Restart=on-failure
RestartSec=3

[Install]
WantedBy=default.target
EOF

    systemctl --user daemon-reload
    systemctl --user enable --now "$NAME.service"

    if ! loginctl enable-linger "$USER" 2>/dev/null; then
        echo "note: could not enable lingering - the server will stop when you log out." >&2
        echo "      ask an admin to run: sudo loginctl enable-linger $USER" >&2
    fi
    check
}

if [ "$ACTION" != install ] && [ "$PORT" != 8289 ]; then
    echo "--port only applies to --install; to change the port, re-run --install" >&2
    exit 2
fi

case "$ACTION" in
    install)
        install
        ;;
    up)
        installed
        systemctl --user start "$NAME.service"
        check
        ;;
    down)
        installed
        systemctl --user stop "$NAME.service"
        echo "$NAME stopped"
        ;;
    restart)
        installed
        systemctl --user restart "$NAME.service"
        check
        ;;
    status)
        installed
        systemctl --user status "$NAME.service" --no-pager --lines=15 || true
        ;;
esac
