# ZeroOS: an interactive login while pi still has the image's default password
# asks for a new one first (see /usr/lib/zeroos/first-password). Only for uid
# 1000 (pi, or pi renamed by Raspberry Pi Imager): another user's passwd would
# never change pi's password, so the question would never go away.
if [ -e /var/lib/zeroos/default-password ] && [ -z "${ZEROOS_PASSWORD_SET:-}" ] &&
   [ "$(id -u)" = 1000 ] && [ -t 0 ]; then
    case $- in
        # Ctrl+C or Ctrl+Z at the prompt ends the login rather than skipping
        # the rest of this file and leaving a shell open.
        *i*) trap 'exit 1' INT QUIT TSTP
             /usr/lib/zeroos/first-password login || exit 1
             trap - INT QUIT TSTP ;;
    esac
fi
