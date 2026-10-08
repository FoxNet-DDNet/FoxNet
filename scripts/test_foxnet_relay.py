"""End-to-end test for a built foxnet-relay.pyz (Python stdlib only)."""

import json
import sqlite3
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
import uuid
from pathlib import Path


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def request(port, path, token=None, body=None):
    data = None if body is None else json.dumps(body).encode()
    headers = {"Content-Type": "application/json"}
    if token:
        headers["Authorization"] = "Bearer " + token
    req = urllib.request.Request(f"http://127.0.0.1:{port}{path}", data=data,
                                 headers=headers, method="POST" if body is not None else "GET")
    with urllib.request.urlopen(req, timeout=3) as response:
        return response.status, json.load(response)


def until(condition, timeout=8):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            result = condition()
            if result:
                return result
        except (ConnectionError, TimeoutError, urllib.error.URLError):
            pass
        time.sleep(0.1)
    raise AssertionError("timed out waiting for relay")


def main():
    archive = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as root:
        ports = [free_port(), free_port()]
        processes = []
        try:
            for index in range(2):
                config_dir = Path(root) / f"vps{index}"
                config_dir.mkdir()
                config = config_dir / "relay.conf"
                config.write_text(
                    f"node_id=vps{index}\nadmin_token=admin{index}\n"
                    f"instance_token=instance{index}\npeer_token=peer\n"
                    f"peer_url=http://127.0.0.1:{ports[1-index]}\n"
                    f"peer_allowed_ips=127.0.0.1\nlocal_allowed_ips=127.0.0.1\n"
                    f"listen_host=127.0.0.1\nlisten_port={ports[index]}\n"
                    f"database={Path(root) / f'relay{index}.sqlite3'}\n", encoding="utf-8")
                if index == 0:
                    local_archive = config_dir / "foxnet-relay.pyz"
                    shutil.copy2(archive, local_archive)
                    command = [sys.executable, str(local_archive)]
                else:
                    command = [sys.executable, str(archive), "--config", str(config)]
                processes.append(subprocess.Popen(
                    command,
                    stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True))
            for port in ports:
                until(lambda port=port: request(port, "/")[1].get("status") == "ok")
            # Child stderr is an unread pipe. Request logging must not fill it
            # and stall HTTP responses before headers are sent.
            for _ in range(256):
                assert request(ports[0], "/")[1]["status"] == "ok"

            event_id = str(uuid.uuid4())
            expiry = int(time.time()) + 600
            _, accepted = request(ports[0], "/api/commands/batch", "instance0", {
                "commands": [{"id": event_id,
                              "command": f"ban_timestamp 192.0.2.10 {expiry} test",
                              "ttl_seconds": 2000000000, "retry_uncertain": True,
                              "replay_on_start": True, "scope": "global"}]})
            assert accepted["accepted"] == 1
            _, duplicate = request(ports[0], "/api/commands/batch", "instance0", {
                "commands": [{"id": event_id,
                              "command": f"ban_timestamp 192.0.2.10 {expiry} test",
                              "ttl_seconds": 2000000000, "retry_uncertain": True,
                              "replay_on_start": True, "scope": "global"}]})
            assert duplicate["accepted"] == 1
            until(lambda: request(ports[1], f"/api/commands/{event_id}", "admin1")[1])
            _, polled = request(ports[1], "/api/instances/8304/poll", "instance1",
                                {"started": True, "started_at": int(time.time()), "acks": []})
            assert polled["reset_bans"] is True
            assert any(item["id"] == event_id for item in polled["commands"])
            request(ports[1], "/api/instances/8304/poll", "instance1",
                    {"started": False, "acks": [event_id]})
            _, state = request(ports[0], f"/api/commands/{event_id}", "admin0")
            assert state["peer_ack"] is True
            assert any(item["state"] == "delivered" for item in state["peer_instances"])

            clear_id = str(uuid.uuid4())
            request(ports[0], "/api/commands/batch", "instance0", {
                "commands": [{"id": clear_id, "command": "unban_all",
                              "ttl_seconds": 2000000000, "retry_uncertain": True,
                              "replay_on_start": True, "scope": "global"}]})
            until(lambda: request(ports[0], f"/api/commands/{clear_id}", "admin0")[1]
                  .get("peer_ack"))
            _, cleared = request(ports[1], "/api/instances/8304/poll", "instance1",
                                 {"started": False, "acks": []})
            assert cleared["reset_bans"] is True and cleared["reset_id"] == clear_id
            assert all(item["id"] != event_id for item in cleared["commands"])
            _, acknowledged = request(ports[1], "/api/instances/8304/poll", "instance1",
                                      {"started": False, "reset_ack": clear_id, "acks": []})
            assert acknowledged["reset_bans"] is False
            _, local = request(ports[0], "/api/commands", "admin0",
                               {"command": "echo local-only", "scope": "local"})
            local_id = local["id"]
            request(ports[0], "/api/instances/8303/poll", "instance0",
                    {"started": True, "started_at": int(time.time()) - 2, "acks": []})
            _, state = request(ports[0], f"/api/commands/{local_id}", "admin0")
            assert state["scope"] == "local" and state["peer_ack"] is True
            try:
                request(ports[1], f"/api/commands/{local_id}", "admin1")
                raise AssertionError("local command leaked to peer")
            except urllib.error.HTTPError as error:
                assert error.code == 404
            try:
                request(ports[0], "/api/commands", "wrong", {"command": "echo no"})
                raise AssertionError("bad token accepted")
            except urllib.error.HTTPError as error:
                assert error.code == 401
            # Leave stderr unread while generating enough command lines to fill
            # a pipe. Logging must not hold up the HTTP request threads.
            burst = [{"id": str(uuid.uuid4()), "command": f"echo burst-{i}",
                      "ttl_seconds": 3600, "retry_uncertain": False,
                      "replay_on_start": False, "scope": "local"} for i in range(200)]
            for start in range(0, len(burst), 100):
                request(ports[0], "/api/commands/batch", "instance0",
                        {"commands": burst[start:start + 100]})
            until(lambda: request(ports[0], "/")[1]["status"] == "ok")
            for index in range(2):
                database = Path(root) / f"relay{index}.sqlite3"
                def was_logged():
                    db = sqlite3.connect(database)
                    try:
                        return db.execute("SELECT logged FROM commands WHERE id=?",
                                          (event_id,)).fetchone() == (1,)
                    finally:
                        db.close()
                until(was_logged)
            time.sleep(0.2)
        finally:
            for process in processes:
                process.terminate()
            outputs = []
            for process in processes:
                try:
                    outputs.append(process.communicate(timeout=3)[1])
                except subprocess.TimeoutExpired:
                    process.kill()
                    outputs.append(process.communicate()[1])
        for output in outputs:
            assert output.count(f"command {event_id} ") == 1, output
        print("relay integration passed: replication, snapshot, acknowledgments, scope, auth, once-only logging")


if __name__ == "__main__":
    main()
