# ZeroOS: an interactive login while pi still has the image's default password
# asks for a new one first (see /usr/lib/zeroos/first-password).
if [ -e /var/lib/zeroos/default-password ] && [ -z "${ZEROOS_PASSWORD_SET:-}" ] &&
   [ "$(id -u)" != 0 ] && [ -t 0 ]; then
    case $- in
        *i*) /usr/lib/zeroos/first-password login || exit 1 ;;
    esac
fi
