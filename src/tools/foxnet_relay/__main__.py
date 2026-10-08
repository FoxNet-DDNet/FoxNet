#!/usr/bin/env python3
"""Two-peer, durable command relay polled by local FoxNet instances."""

import hmac
import ipaddress
import json
import os
import sqlite3
import sys
import threading
import logging
import time
import uuid
from contextlib import contextmanager
from pathlib import Path
from urllib.error import URLError
from urllib.request import ProxyHandler, Request, build_opener

from relay_http import RelayApp, jsonify, request

NODE = os.environ.get("FOXNET_NODE_ID", "")
ADMIN_TOKEN = os.environ.get("FOXNET_API_TOKEN", "")
INSTANCE_TOKEN = os.environ.get("FOXNET_INSTANCE_TOKEN", "")
PEER_TOKEN = os.environ.get("FOXNET_PEER_TOKEN", "")
PEER_URL = os.environ.get("FOXNET_PEER_URL", "").rstrip("/")
PEER_ALLOWED_IPS = os.environ.get("FOXNET_PEER_ALLOWED_IPS", "")
LOCAL_ALLOWED_IPS = os.environ.get("FOXNET_LOCAL_ALLOWED_IPS", "127.0.0.1,::1")
DB_FILE = Path(os.environ.get("FOXNET_DB", "foxnet-relay.sqlite3"))
_next_peer_attempt = 0.0
_peer_failures = 0
_last_created_ns = 0
_created_lock = threading.Lock()


def next_created():
    global _last_created_ns
    with _created_lock:
        _last_created_ns = max(time.time_ns(), _last_created_ns + 1)
        return _last_created_ns


app = RelayApp(__name__)
app.config["MAX_CONTENT_LENGTH"] = 256 * 1024


@contextmanager
def database():
    connection = sqlite3.connect(DB_FILE, timeout=10)
    connection.row_factory = sqlite3.Row
    try:
        yield connection
        connection.commit()
    except Exception:
        connection.rollback()
        raise
    finally:
        connection.close()


def initialize():
    if not NODE or not ADMIN_TOKEN or not INSTANCE_TOKEN or not PEER_TOKEN or not PEER_URL or not PEER_ALLOWED_IPS:
        raise ValueError("FOXNET_NODE_ID, FOXNET_API_TOKEN, FOXNET_INSTANCE_TOKEN, FOXNET_PEER_TOKEN, FOXNET_PEER_URL and FOXNET_PEER_ALLOWED_IPS are required")
    for entry in PEER_ALLOWED_IPS.split(",") + LOCAL_ALLOWED_IPS.split(","):
        ipaddress.ip_address(entry.strip())
    DB_FILE.parent.mkdir(parents=True, exist_ok=True)
    with database() as db:
        db.execute("PRAGMA journal_mode=WAL")
        db.executescript("""
            CREATE TABLE IF NOT EXISTS commands (
                id TEXT PRIMARY KEY, origin TEXT NOT NULL, command TEXT NOT NULL,
                created INTEGER NOT NULL, expires INTEGER NOT NULL,
                retry_uncertain INTEGER NOT NULL, replay_on_start INTEGER NOT NULL,
                peer_ack INTEGER NOT NULL, scope TEXT NOT NULL DEFAULT 'global',
                logged INTEGER NOT NULL DEFAULT 1);
            CREATE TABLE IF NOT EXISTS deliveries (
                command_id TEXT NOT NULL, instance_id TEXT NOT NULL,
                state TEXT NOT NULL DEFAULT 'pending', attempts INTEGER NOT NULL DEFAULT 0,
                next_attempt INTEGER NOT NULL DEFAULT 0, error TEXT NOT NULL DEFAULT '',
                PRIMARY KEY(command_id, instance_id));
            CREATE INDEX IF NOT EXISTS delivery_due ON deliveries(state, next_attempt);
            CREATE TABLE IF NOT EXISTS instances (
                id TEXT PRIMARY KEY, last_seen INTEGER NOT NULL,
                reset_id TEXT NOT NULL DEFAULT '');
            CREATE TABLE IF NOT EXISTS ban_state (
                kind TEXT NOT NULL, target TEXT NOT NULL, command TEXT NOT NULL,
                expires INTEGER NOT NULL, version TEXT NOT NULL, event_id TEXT NOT NULL,
                PRIMARY KEY(kind,target));
            CREATE TABLE IF NOT EXISTS ban_clear (
                id INTEGER PRIMARY KEY CHECK(id=1), version TEXT NOT NULL);
        """)
        columns = {row[1] for row in db.execute("PRAGMA table_info(commands)")}
        if "scope" not in columns:
            db.execute("ALTER TABLE commands ADD COLUMN scope TEXT NOT NULL DEFAULT 'global'")
        if "logged" not in columns:
            db.execute("ALTER TABLE commands ADD COLUMN logged INTEGER NOT NULL DEFAULT 1")
        instance_columns = {row[1] for row in db.execute("PRAGMA table_info(instances)")}
        if "reset_id" not in instance_columns:
            db.execute("ALTER TABLE instances ADD COLUMN reset_id TEXT NOT NULL DEFAULT ''")
        if (db.execute("SELECT 1 FROM ban_state LIMIT 1").fetchone() is None
                and db.execute("SELECT 1 FROM ban_clear LIMIT 1").fetchone() is None):
            old_events = db.execute("""SELECT id,origin,command,created,expires,
                                       retry_uncertain,replay_on_start,scope FROM commands
                                       WHERE replay_on_start=1 ORDER BY created,origin,id""").fetchall()
            for row in old_events:
                apply_ban_change(db, dict(row))
        global _last_created_ns
        _last_created_ns = db.execute(
            "SELECT COALESCE(MAX(created), 0) FROM commands WHERE origin=?",
            (NODE,)).fetchone()[0]


def authenticated(token):
    return bool(token) and hmac.compare_digest(
        request.headers.get("Authorization", ""), "Bearer " + token)


def allowed(ips):
    try:
        remote = ipaddress.ip_address(request.remote_addr)
        return any(remote == ipaddress.ip_address(entry.strip()) for entry in ips.split(","))
    except ValueError:
        return False


def local_authenticated():
    return allowed(LOCAL_ALLOWED_IPS) and authenticated(ADMIN_TOKEN)


def instance_authenticated():
    return allowed(LOCAL_ALLOWED_IPS) and authenticated(INSTANCE_TOKEN)


def peer_authenticated():
    return allowed(PEER_ALLOWED_IPS) and authenticated(PEER_TOKEN)


def open_peer(peer_request, timeout):
    # Peer traffic must use the configured private link directly, never a
    # system HTTP proxy that could receive the bearer token.
    return build_opener(ProxyHandler({})).open(peer_request, timeout=timeout)


def valid_command(value):
    return (isinstance(value, str) and value and value == value.strip()
            and not any(c in value for c in "\r\n\0")
            and len(value.encode("utf-8")) <= 900)


def delivery_expiry(command, now, ttl, replay):
    expires = now + ttl
    if replay:
        parts = command.split(" ", 4)
        index = 2 if parts[0] == "ban_timestamp" else 3 if parts[0] == "ban_range_timestamp" else -1
        if index >= 0 and len(parts) > index:
            try:
                timestamp = int(parts[index])
                if timestamp > 0:
                    expires = min(expires, max(now + 60, timestamp + 60))
            except ValueError:
                pass
    return expires


def ban_change(command):
    parts = command.split(" ")
    if len(parts) >= 3 and parts[0] == "ban_timestamp":
        return "addr", parts[1], command
    if len(parts) >= 4 and parts[0] == "ban_range_timestamp":
        return "range", parts[1] + " " + parts[2], command
    if len(parts) == 2 and parts[0] == "unban":
        return "addr", parts[1], ""
    if len(parts) == 3 and parts[0] == "unban_range":
        return "range", parts[1] + " " + parts[2], ""
    if command == "unban_all":
        return "all", "", ""
    return None


def apply_ban_change(db, event):
    if not event["replay_on_start"]:
        return True
    change = ban_change(event["command"])
    if change is None:
        return False
    kind, target, command = change
    version = f'{event["created"]:012d}:{event["origin"]}:{event["id"]}'
    cleared = db.execute("SELECT version FROM ban_clear WHERE id=1").fetchone()
    if cleared and version <= cleared["version"]:
        return False
    if kind == "all":
        db.execute("INSERT OR REPLACE INTO ban_clear(id,version) VALUES(1,?)", (version,))
        previous = [row[0] for row in db.execute(
            "SELECT event_id FROM ban_state WHERE version<=?", (version,))]
        db.execute("DELETE FROM ban_state WHERE version<=?", (version,))
        db.execute("UPDATE instances SET reset_id=?", (event["id"],))
    else:
        old = db.execute("SELECT version,event_id FROM ban_state WHERE kind=? AND target=?",
                         (kind, target)).fetchone()
        if old and version <= old["version"]:
            return False
        previous = [old["event_id"]] if old else []
        db.execute("""INSERT OR REPLACE INTO ban_state
                      (kind,target,command,expires,version,event_id) VALUES(?,?,?,?,?,?)""",
                   (kind, target, command, event["expires"], version, event["id"]))
    db.executemany("""UPDATE deliveries SET state='superseded'
                      WHERE command_id=? AND state IN ('pending','awaiting_ack')""",
                   ((event_id,) for event_id in previous))
    return kind != "all"


def insert_events(events):
    with database() as db:
        instances = [row[0] for row in db.execute("SELECT id FROM instances")]
        for event in events:
            result = db.execute("""INSERT OR IGNORE INTO commands
                (id,origin,command,created,expires,retry_uncertain,replay_on_start,peer_ack,scope,logged)
                VALUES(?,?,?,?,?,?,?,?,?,0)""",
                                (event["id"], event["origin"], event["command"],
                                 event["created"], event["expires"], event["retry_uncertain"],
                                 event["replay_on_start"],
                                 int(event["origin"] != NODE or event["scope"] == "local"),
                                 event["scope"]))
            if result.rowcount:
                if apply_ban_change(db, event):
                    db.executemany("INSERT INTO deliveries(command_id,instance_id) VALUES(?,?)",
                                   ((event["id"], name) for name in instances))


@app.route("/api/commands", methods=["POST"])
def submit():
    if not local_authenticated():
        return jsonify(error="unauthorized"), 401
    body = request.get_json(silent=True)
    if not isinstance(body, dict):
        return jsonify(error="JSON object required"), 400
    cmd, ttl = body.get("command"), body.get("ttl_seconds", 3600)
    retry = body.get("retry_uncertain", False)
    scope = body.get("scope", "global")
    event_id = body.get("id") or str(uuid.uuid4())
    try:
        uuid.UUID(event_id)
    except (ValueError, TypeError):
        return jsonify(error="invalid id"), 400
    if (not valid_command(cmd) or type(ttl) is not int or not 1 <= ttl <= 31536000
            or type(retry) is not bool or scope not in ("local", "global")):
        return jsonify(error="invalid command, ttl_seconds, retry_uncertain or scope"), 400
    now = int(time.time())
    event = dict(id=event_id, origin=NODE, command=cmd, created=next_created(),
                 expires=now + ttl, retry_uncertain=int(retry), replay_on_start=0,
                 scope=scope)
    with database() as db:
        existing = db.execute("SELECT command,origin,scope FROM commands WHERE id=?", (event_id,)).fetchone()
    if existing is not None:
        if existing["command"] != cmd or existing["origin"] != NODE or existing["scope"] != scope:
            return jsonify(error="id already used for another command"), 409
        return jsonify(id=event_id, accepted=True, duplicate=True), 200
    insert_events([event])
    return jsonify(id=event["id"], accepted=True), 202


@app.route("/api/commands/batch", methods=["POST"])
def submit_batch():
    if not instance_authenticated():
        return jsonify(error="unauthorized"), 401
    body = request.get_json(silent=True)
    items = body.get("commands") if isinstance(body, dict) else None
    if not isinstance(items, list) or not 1 <= len(items) <= 100:
        return jsonify(error="batch must contain 1 to 100 commands"), 400
    now = int(time.time())
    events = []
    for item in items:
        if not isinstance(item, dict):
            return jsonify(error="invalid command"), 400
        event_id = item.get("id")
        try:
            uuid.UUID(event_id)
        except (ValueError, TypeError):
            return jsonify(error="invalid id"), 400
        command = item.get("command")
        ttl = item.get("ttl_seconds", 3600)
        retry = item.get("retry_uncertain", False)
        replay = item.get("replay_on_start", False)
        scope = item.get("scope", "global")
        if (not valid_command(command) or type(ttl) is not int
                or not 1 <= ttl <= 3153600000 or type(retry) is not bool
                or type(replay) is not bool or scope not in ("local", "global")
                or (scope == "local" and replay)
                or (replay and ban_change(command) is None)):
            return jsonify(error="invalid command, ttl_seconds or retry flags"), 400
        events.append(dict(id=event_id, origin=NODE, command=command, created=next_created(),
                           expires=delivery_expiry(command, now, ttl, replay),
                           retry_uncertain=int(retry),
                           replay_on_start=int(replay), scope=scope))
    with database() as db:
        for event in events:
            existing = db.execute("SELECT command,origin,scope FROM commands WHERE id=?",
                                  (event["id"],)).fetchone()
            if existing and (existing["command"] != event["command"]
                             or existing["origin"] != NODE
                             or existing["scope"] != event["scope"]):
                return jsonify(error="id already used for another command"), 409
    insert_events(events)
    return jsonify(accepted=len(events), ids=[event["id"] for event in events]), 202


@app.route("/api/commands/<event_id>", methods=["GET"])
def status(event_id):
    if not local_authenticated():
        return jsonify(error="unauthorized"), 401
    with database() as db:
        event = db.execute("SELECT origin,peer_ack,scope FROM commands WHERE id=?", (event_id,)).fetchone()
        if event is None:
            return jsonify(error="unknown command"), 404
        rows = db.execute("SELECT instance_id,state,attempts,error FROM deliveries WHERE command_id=?",
                          (event_id,)).fetchall()
    remote = None
    if event["origin"] == NODE and event["peer_ack"] and event["scope"] == "global":
        peer_request = Request(PEER_URL + "/api/peer/commands/" + event_id,
                               headers={"Authorization": "Bearer " + PEER_TOKEN})
        try:
            with open_peer(peer_request, timeout=2) as response:
                remote = json.load(response)["instances"]
        except (URLError, TimeoutError, OSError, ValueError, KeyError):
            pass
    return jsonify(id=event_id, origin=event["origin"], scope=event["scope"],
                   peer_ack=bool(event["peer_ack"]),
                   local_instances=[dict(row) for row in rows], peer_instances=remote)


@app.route("/api/peer/commands/<event_id>", methods=["GET"])
def peer_status(event_id):
    if not peer_authenticated():
        return jsonify(error="unauthorized"), 401
    with database() as db:
        rows = db.execute("SELECT instance_id,state,attempts,error FROM deliveries WHERE command_id=?",
                          (event_id,)).fetchall()
    return jsonify(instances=[dict(row) for row in rows])


@app.route("/api/peer/commands", methods=["POST"])
def receive_peer():
    if not peer_authenticated():
        return jsonify(error="unauthorized"), 401
    body = request.get_json(silent=True)
    events = body.get("commands") if isinstance(body, dict) else None
    if not isinstance(events, list) or len(events) > 100:
        return jsonify(error="invalid batch"), 400
    for event in events:
        if not isinstance(event, dict) or set(event) != {
                "id", "origin", "command", "created", "expires", "retry_uncertain",
                "replay_on_start", "scope"}:
            return jsonify(error="invalid event"), 400
        try:
            uuid.UUID(event["id"])
        except (ValueError, TypeError):
            return jsonify(error="invalid event id"), 400
        if (not isinstance(event["origin"], str) or event["origin"] == NODE
                or not valid_command(event["command"]) or type(event["created"]) is not int
                or type(event["expires"]) is not int or event["expires"] <= (event["created"] // 1000000000 if event["created"] >= 1000000000000 else event["created"])
                or type(event["retry_uncertain"]) is not int
                or event["retry_uncertain"] not in (0, 1)
                or type(event["replay_on_start"]) is not int
                or event["replay_on_start"] not in (0, 1)
                or event["scope"] != "global"
                or (event["replay_on_start"] and ban_change(event["command"]) is None)):
            return jsonify(error="invalid event"), 400
    insert_events(events)
    return jsonify(accepted=len(events))


@app.route("/")
def health():
    return jsonify(status="ok")


@app.route("/api/instances/<instance_id>/poll", methods=["POST"])
def poll_instance(instance_id):
    if not instance_authenticated():
        return jsonify(error="unauthorized"), 401
    if not instance_id.isdigit() or not 1 <= int(instance_id) <= 65535:
        return jsonify(error="invalid instance port"), 400
    body = request.get_json(silent=True)
    if not isinstance(body, dict) or type(body.get("started", False)) is not bool:
        return jsonify(error="invalid poll"), 400
    started_at = body.get("started_at", 0)
    if type(started_at) is not int or started_at < 0:
        return jsonify(error="invalid start time"), 400
    acks = body.get("acks", [])
    if not isinstance(acks, list) or len(acks) > 100:
        return jsonify(error="invalid acknowledgments"), 400
    try:
        for event_id in acks:
            uuid.UUID(event_id)
    except (ValueError, TypeError):
        return jsonify(error="invalid acknowledgment id"), 400
    reset_ack = body.get("reset_ack", "")
    if not isinstance(reset_ack, str):
        return jsonify(error="invalid reset acknowledgment"), 400
    if reset_ack:
        try:
            uuid.UUID(reset_ack)
        except ValueError:
            return jsonify(error="invalid reset acknowledgment"), 400

    now = int(time.time())
    with database() as db:
        known = db.execute("SELECT 1 FROM instances WHERE id=?", (instance_id,)).fetchone()
        db.execute("""INSERT INTO instances(id,last_seen) VALUES(?,?)
                      ON CONFLICT(id) DO UPDATE SET last_seen=excluded.last_seen""",
                   (instance_id, now))
        if reset_ack:
            db.execute("UPDATE instances SET reset_id='' WHERE id=? AND reset_id=?",
                       (instance_id, reset_ack))
        reset_id = db.execute("SELECT reset_id FROM instances WHERE id=?",
                              (instance_id,)).fetchone()[0]
        reset_bans = bool(body.get("started", False) or known is None or reset_id)
        if reset_bans:
            if started_at:
                db.execute("""INSERT OR IGNORE INTO deliveries(command_id,instance_id)
                              SELECT id,? FROM commands
                              WHERE replay_on_start=0 AND
                              (created>=? OR (created<1000000000000 AND created>=?))
                              AND expires>?""",
                           (instance_id, started_at * 1000000000, started_at, now))
            db.execute("""UPDATE deliveries SET state='superseded'
                          WHERE instance_id=? AND state IN ('pending','awaiting_ack')
                          AND command_id IN (SELECT id FROM commands WHERE replay_on_start=1)""",
                       (instance_id,))
            db.execute("""INSERT OR IGNORE INTO deliveries(command_id,instance_id)
                          SELECT event_id,? FROM ban_state
                          WHERE command!='' AND expires>?""",
                       (instance_id, now))
            db.execute("""UPDATE deliveries SET state='pending', attempts=0,
                          next_attempt=0, error='' WHERE instance_id=?
                          AND command_id IN (SELECT event_id FROM ban_state
                                             WHERE command!='' AND expires>?)""",
                       (instance_id, now))
        db.executemany("""UPDATE deliveries SET state='delivered',error=''
                          WHERE command_id=? AND instance_id=?""",
                       ((event_id, instance_id) for event_id in acks))
        db.execute("""UPDATE deliveries SET state='pending'
                      WHERE instance_id=? AND state='awaiting_ack' AND next_attempt<=?
                      AND command_id IN (SELECT id FROM commands WHERE retry_uncertain=1)""",
                   (instance_id, now))
        db.execute("""UPDATE deliveries SET state='uncertain',
                      error='instance did not acknowledge command'
                      WHERE instance_id=? AND state='awaiting_ack' AND next_attempt<=?
                      AND command_id IN (SELECT id FROM commands WHERE retry_uncertain=0)""",
                   (instance_id, now))
        db.execute("""UPDATE deliveries SET state='expired'
                      WHERE instance_id=? AND state IN ('pending','awaiting_ack')
                      AND command_id IN (SELECT id FROM commands WHERE expires<=?)""",
                   (instance_id, now))
        rows = db.execute("""SELECT c.id,c.command FROM deliveries d
                             JOIN commands c ON c.id=d.command_id
                             WHERE d.instance_id=? AND d.state='pending'
                             AND d.next_attempt<=? AND c.expires>?
                             ORDER BY c.rowid LIMIT 100""",
                          (instance_id, now, now)).fetchall()
        db.executemany("""UPDATE deliveries SET state='awaiting_ack',
                          attempts=attempts+1,next_attempt=?
                          WHERE command_id=? AND instance_id=?""",
                       ((now + 5, row["id"], instance_id) for row in rows))
    return jsonify(commands=[dict(row) for row in rows],
                   reset_bans=reset_bans, reset_id=reset_id)


def replicate():
    global _next_peer_attempt, _peer_failures
    if time.monotonic() < _next_peer_attempt:
        return
    with database() as db:
        rows = db.execute("""SELECT id,origin,command,created,expires,retry_uncertain,replay_on_start,scope
                             FROM commands WHERE origin=? AND peer_ack=0 AND scope='global'
                             ORDER BY rowid LIMIT 100""", (NODE,)).fetchall()
    if not rows:
        return
    body = json.dumps({"commands": [dict(row) for row in rows]}).encode()
    req = Request(PEER_URL + "/api/peer/commands", data=body, method="POST",
                  headers={"Authorization": "Bearer " + PEER_TOKEN,
                           "Content-Type": "application/json"})
    try:
        with open_peer(req, timeout=5) as response:
            if response.status != 200:
                return
    except (URLError, TimeoutError, OSError):
        _peer_failures += 1
        _next_peer_attempt = time.monotonic() + min(60, 2 ** min(_peer_failures, 6))
        return
    _peer_failures = 0
    _next_peer_attempt = 0.0
    with database() as db:
        db.executemany("UPDATE commands SET peer_ack=1 WHERE id=?", ((row["id"],) for row in rows))


def prune_expired():
    cutoff = int(time.time()) - 7 * 86400
    with database() as db:
        db.execute("""DELETE FROM deliveries WHERE command_id IN
                      (SELECT id FROM commands WHERE expires<? AND peer_ack=1 AND logged=1)""",
                   (cutoff,))
        db.execute("DELETE FROM commands WHERE expires<? AND peer_ack=1 AND logged=1", (cutoff,))


def command_for_log(command):
    name = command.split(" ", 1)[0].lower()
    if name.startswith("auth_") or any(word in name for word in ("password", "token", "secret")):
        return name + " <redacted>"
    return "".join(char if char.isprintable() else " " for char in command)


def log_commands():
    while True:
        try:
            with database() as db:
                row = db.execute("""SELECT id,origin,scope,command FROM commands
                                    WHERE logged=0 ORDER BY rowid LIMIT 1""").fetchone()
                if row:
                    db.execute("UPDATE commands SET logged=1 WHERE id=?", (row["id"],))
            if row:
                # The database transaction is closed before writing to the console.
                # A paused console must not block HTTP requests or replication.
                app.logger.info("command %s origin=%s scope=%s: %s",
                                row["id"], row["origin"], row["scope"],
                                command_for_log(row["command"]))
            else:
                time.sleep(0.2)
        except Exception:
            app.logger.exception("relay command logger failed")
            time.sleep(1)


def worker():
    last_prune = 0.0
    while True:
        try:
            replicate()
            if time.monotonic() - last_prune >= 3600:
                prune_expired()
                last_prune = time.monotonic()
        except Exception:
            app.logger.exception("relay worker failed")
        time.sleep(0.25)


def load_config(path):
    mapping = {
        "node_id": "FOXNET_NODE_ID",
        "admin_token": "FOXNET_API_TOKEN",
        "instance_token": "FOXNET_INSTANCE_TOKEN",
        "peer_token": "FOXNET_PEER_TOKEN",
        "peer_url": "FOXNET_PEER_URL",
        "peer_allowed_ips": "FOXNET_PEER_ALLOWED_IPS",
        "local_allowed_ips": "FOXNET_LOCAL_ALLOWED_IPS",
        "database": "FOXNET_DB",
    }
    values = {"listen_host": "0.0.0.0", "listen_port": "666"}
    for raw in Path(path).read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise ValueError(f"invalid config line: {line}")
        key, value = (part.strip() for part in line.split("=", 1))
        if key in mapping:
            os.environ[mapping[key]] = value
        elif key in values:
            values[key] = value
        else:
            raise ValueError(f"unknown config key: {key}")
    port = int(values["listen_port"])
    if not 1 <= port <= 65535:
        raise ValueError("listen_port must be between 1 and 65535")
    return values["listen_host"], port


if __name__ == "__main__":
    if len(sys.argv) == 1:
        config_path = Path(sys.argv[0]).resolve().with_name("relay.conf")
    elif len(sys.argv) == 3 and sys.argv[1] == "--config":
        config_path = Path(sys.argv[2])
    else:
        raise SystemExit("Usage: python foxnet-relay.pyz [--config relay.conf]")
    if not config_path.is_file():
        raise SystemExit(f"Missing {config_path}; copy relay.conf.example to relay.conf and fill it in")
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    host, port = load_config(config_path)
    # The module's settings are read at import time, before load_config. Reload
    # them explicitly after parsing the config file.
    NODE = os.environ.get("FOXNET_NODE_ID", "")
    ADMIN_TOKEN = os.environ.get("FOXNET_API_TOKEN", "")
    INSTANCE_TOKEN = os.environ.get("FOXNET_INSTANCE_TOKEN", "")
    PEER_TOKEN = os.environ.get("FOXNET_PEER_TOKEN", "")
    PEER_URL = os.environ.get("FOXNET_PEER_URL", "").rstrip("/")
    PEER_ALLOWED_IPS = os.environ.get("FOXNET_PEER_ALLOWED_IPS", "")
    LOCAL_ALLOWED_IPS = os.environ.get("FOXNET_LOCAL_ALLOWED_IPS", "127.0.0.1,::1")
    DB_FILE = Path(os.environ.get("FOXNET_DB", "foxnet-relay.sqlite3"))
    initialize()
    threading.Thread(target=worker, daemon=True).start()
    threading.Thread(target=log_commands, daemon=True).start()
    app.serve(host, port)


