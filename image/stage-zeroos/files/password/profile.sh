# ZeroOS: an interactive login while pi still has the image's default password
# asks for a new one first (see /usr/lib/zeroos/first-password). Only for uid
# 1000 (pi, or pi renamed by Raspberry Pi Imager): another user's passwd would
# never change pi's password, so the question would never go away.
if [ -e /var/lib/zeroos/default-password ] && [ -z "${ZEROOS_PASSWORD_SET:-}" ] &&
   [ "$(id -u)" = 1000 ] && [ -t 0 ]; then
    case $- in
        *i*) /usr/lib/zeroos/first-password login || exit 1 ;;
    esac
fi
