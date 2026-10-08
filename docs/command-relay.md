# FoxNet command relay

`foxnet-relay` is a separate FoxNet build target. It packages one executable
Python archive using only Python's standard library. Run one relay process on
each VPS. FoxNet instances poll their local relay using the game port as their
identity. The relays exchange global events and keep commands, acknowledgments,
and the current ban snapshot in SQLite. No FIFO, econ port, Flask, Gunicorn, or
ChaiScript ban hook is needed.

## Build

Build just the relay with CMake and Python 3.7 or newer; the relay-only build does not
need the game server, Rust, curl, or SQLite development packages:

```sh
cmake -S src/tools/foxnet_relay -B out/build/relay
cmake --build out/build/relay --target foxnet-relay
```

This produces `foxnet-relay.pyz`, `relay.conf.example`, and
`foxnet-server.cfg.example` in `out/build/relay`. If FoxNet is already
configured, its main build also has a `foxnet-relay` target and produces the
archive and both example configs in `out/build/<preset>`:

```sh
cmake --build out/build/<preset> --target foxnet-relay
```

On Linux it can be run as an executable after `chmod +x`, or with
`python3 foxnet-relay.pyz`. On Windows, run `py -3 foxnet-relay.pyz` in a
terminal. With no arguments, the relay loads `relay.conf` next to the archive;
use `--config <path>` to select another file. Double-clicking the archive also
works on Windows once `relay.conf` is beside it and Python is associated
with `.pyz` files. Python 3.7 or newer is already required for the relay build
and is the only runtime dependency of this archive. The relay can be built and
run without building the game server target.

## Configure both VPSs

Copy the archive and `relay.conf.example` to both VPSs. On each VPS, copy
`relay.conf.example` to `/etc/foxnet-relay.conf`, fill every blank required
value, and set its mode to 0600. Builds may refresh the `.example` files;
they do not touch your filled-in config. The example below is for VPS 1.
VPS 2 swaps its node ID, peer URL, and peer allowlist. Use long random tokens. `peer_token` must match on both VPSs;
`admin_token` and `instance_token` can differ.

```ini
node_id=vps1
admin_token=replace-with-random-admin-token
instance_token=replace-with-random-instance-token
peer_token=replace-with-shared-random-peer-token
peer_url=http://10.0.0.2:666
peer_allowed_ips=10.0.0.2
local_allowed_ips=127.0.0.1
listen_host=0.0.0.0
listen_port=666
database=/var/lib/foxnet-relay/relay.sqlite3
```

The service binds all interfaces in this example so the other VPS can reach it.
Allow port 666 only from the peer VPS in the firewall. The relay checks the
actual source IP and bearer token for each request; it does not trust forwarded
IP headers. Use a private authenticated network, such as WireGuard, for HTTP
between VPSs. For a public network use HTTPS for `peer_url` and terminate HTTPS
at a proxy with a strict firewall. Do not expose plain HTTP with bearer tokens
to the public internet.

A systemd service example:

```ini
[Unit]
Description=FoxNet Relay
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStart=/usr/bin/python3 /opt/foxnet/foxnet-relay.pyz --config /etc/foxnet-relay.conf
WorkingDirectory=/var/lib/foxnet-relay
User=foxnet-relay
Group=foxnet-relay
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
```

Create `/var/lib/foxnet-relay` owned by that service user. Retain its SQLite
file during updates. Copy `foxnet-server.cfg.example` into a config shared by
all FoxNet server instances on the same VPS, then fill the instance token:

```ini
sv_command_relay_url "http://127.0.0.1:666"
sv_command_relay_token replace-with-random-instance-token
```

The `instance_token` must match the local relay config. The relay does not need
per-instance port settings. FoxNet keeps an outgoing SQLite outbox per game
port in its save directory; retain these files too.

## Commands and delivery

`relay_local <command>` runs on all instances on this VPS. `relay_all <command>`
runs on all instances on both VPSs. `relay_status` shows the local pending
outbox. Ordinary `ban`, `ban_range`, `ban_timestamp`, `unban`, `unban_range`, and
`unban_all` publish accepted ban state changes globally from FoxNet's native
ban store, including AntiBot bans. Do not wrap bans in `relay_all`; ordinary ban
commands also update the durable restart snapshot. Other commands typed in one
server's console remain local.

The local admin API accepts `POST /api/commands` with JSON such as
`{"command":"say Hello", "scope":"global"}` and an
`Authorization: Bearer <admin_token>` header. Use `scope:"local"` to keep the
command on this VPS. `GET /api/commands/<id>` reports local and peer delivery.
A peer acknowledgment means the other relay stored the event; individual
instance acknowledgments appear separately. Each relay logs a newly accepted
command once by event ID. Repeated polls and retries of that event stay quiet.
Global events appear once in each VPS relay log. The relay creates its SQLite
database and parent directory on startup; a relative database path is relative
to the process working directory.

Generic commands are not retried when execution is uncertain, to avoid
repeating commands like `say` or `restart`. Native bans and unbans are retried.
When an instance starts, it receives the current ban snapshot. Temporary bans
use absolute expiry timestamps, so delayed delivery does not restart their
full duration. Keep both VPS clocks synchronized so conflicting changes made
while the peer link is down resolve in the intended order.

```sh
python3 scripts/test_foxnet_relay.py out/build/<preset>/foxnet-relay.pyz
```
