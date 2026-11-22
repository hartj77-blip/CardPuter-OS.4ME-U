# Security

ZeroOS runs SSH, Tailscale, Syncthing and an update service on a device that
goes on your network, so security reports are welcome.

## Reporting a problem

Please don't open a public issue for a security problem. Report it privately
through GitHub instead: on this repository's **Security** tab, choose
**Report a vulnerability**. Include what you found, how to reproduce it, and
which ZeroOS version (Settings > About this device, or `cat /etc/zeroos-release`).

You should hear back within a week. Once a fix is released, the report can be
made public, with credit if you'd like it.

## Supported versions

Only the newest release gets fixes. Use the **Update** app to stay current.

## Things that are by design

- Every image starts with the password `raspberry` for the `pi` user. The
  first login asks for a new one, and SSH commands without a terminal are
  refused until it is changed.
- Updates are only fetched from the addresses set in
  `/etc/zeroos/update.conf`; see [docs/UPDATE.md](docs/UPDATE.md).
