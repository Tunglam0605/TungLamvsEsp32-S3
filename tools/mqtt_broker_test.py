#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Minimal MQTT 3.1.1 broker and deterministic WCS simulator for Callbox tests.

This is deliberately a LAN test tool, not a production broker.  It implements
the part of MQTT 3.1.1 exercised by the Callbox firmware: CONNECT, SUBSCRIBE,
PUBLISH QoS 0/1, PUBACK, PINGREQ and DISCONNECT.  In particular, every command
published to ``callbox/{id}/cmd`` is QoS 1, tracked until PUBACK, and retried
with the MQTT DUP flag if necessary.

The firmware contract under test is:

* Callbox -> WCS: ``callbox/{id}/event`` (QoS 1)
* WCS -> Callbox: ``callbox/{id}/cmd``   (QoS 1)
* Callbox status: ``callbox/{id}/status`` (QoS 1, retained in real broker)

Run on the laptop and configure the Callbox portal with this laptop IP and
port 1883.  The tool has no TLS or authentication and must never be used as a
production MQTT broker.
"""

from __future__ import annotations

import argparse
import json
import socket
import socketserver
import sys
import threading
import time
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except AttributeError:
    pass


MQTT_QOS = 1
RETRY_INTERVAL_S = 2.0
MAX_DELIVERY_ATTEMPTS = 3


def mqtt_enc_len(value: int) -> bytes:
    """Encode MQTT Remaining Length and reject invalid values."""
    if not 0 <= value <= 268435455:
        raise ValueError("invalid MQTT remaining length")
    encoded = bytearray()
    while True:
        digit = value % 128
        value //= 128
        encoded.append(digit | (0x80 if value else 0))
        if not value:
            return bytes(encoded)


def mqtt_utf8(value: str) -> bytes:
    encoded = value.encode("utf-8")
    if len(encoded) > 65535:
        raise ValueError("MQTT UTF-8 string too long")
    return len(encoded).to_bytes(2, "big") + encoded


def mqtt_read_utf8(packet: bytes, offset: int) -> Tuple[str, int]:
    if offset + 2 > len(packet):
        raise ValueError("truncated MQTT UTF-8 length")
    size = int.from_bytes(packet[offset:offset + 2], "big")
    end = offset + 2 + size
    if end > len(packet):
        raise ValueError("truncated MQTT UTF-8 value")
    return packet[offset + 2:end].decode("utf-8"), end


def mqtt_build_publish(topic: str, payload: str, *, qos: int = 0,
                       packet_id: int = 0, duplicate: bool = False) -> bytes:
    """Build one valid QoS 0 or QoS 1 PUBLISH packet."""
    if qos not in (0, 1):
        raise ValueError("test broker supports QoS 0 and QoS 1 only")
    if qos == 1 and not 1 <= packet_id <= 65535:
        raise ValueError("QoS 1 packet id must be 1..65535")
    body = mqtt_utf8(topic)
    if qos == 1:
        body += packet_id.to_bytes(2, "big")
    body += payload.encode("utf-8")
    flags = (qos << 1) | (0x08 if duplicate else 0)
    return bytes([0x30 | flags]) + mqtt_enc_len(len(body)) + body


def mqtt_build_puback(packet_id: int) -> bytes:
    return b"\x40\x02" + packet_id.to_bytes(2, "big")


def topic_matches(topic_filter: str, topic: str) -> bool:
    """Match a valid subset of MQTT topic filters (+ and terminal #)."""
    wanted = topic_filter.split("/")
    actual = topic.split("/")
    for index, part in enumerate(wanted):
        if part == "#":
            return index == len(wanted) - 1
        if index >= len(actual) or (part != "+" and part != actual[index]):
            return False
    return len(wanted) == len(actual)


@dataclass
class PendingDelivery:
    packet: bytes
    topic: str
    attempts: int
    retry_at: float


@dataclass
class ClientRecord:
    handler: "MqttConnectionHandler"
    client_id: str
    subscriptions: List[Tuple[str, int]]
    next_packet_id: int = 1


class BrokerState:
    """Thread-safe broker state plus WCS transaction state."""

    def __init__(self, auto_accept: bool) -> None:
        self.lock = threading.RLock()
        self.clients: Dict[int, ClientRecord] = {}
        self.inflight: Dict[Tuple[int, int], PendingDelivery] = {}
        self.missions: Dict[Tuple[str, int], Dict[str, object]] = {}
        self.cancel_sequences: Dict[Tuple[str, int], int] = {}
        self.auto_accept = auto_accept

    def register(self, handler: "MqttConnectionHandler", client_id: str) -> None:
        with self.lock:
            self.clients[id(handler)] = ClientRecord(handler, client_id, [])

    def unregister(self, handler: "MqttConnectionHandler") -> None:
        with self.lock:
            self.clients.pop(id(handler), None)
            for key in [key for key in self.inflight if key[0] == id(handler)]:
                self.inflight.pop(key, None)

    def add_subscription(self, handler: "MqttConnectionHandler", topic_filter: str,
                         requested_qos: int) -> int:
        granted_qos = min(max(requested_qos, 0), MQTT_QOS)
        with self.lock:
            record = self.clients.get(id(handler))
            if record is not None:
                record.subscriptions = [pair for pair in record.subscriptions
                                        if pair[0] != topic_filter]
                record.subscriptions.append((topic_filter, granted_qos))
        return granted_qos

    def puback(self, handler: "MqttConnectionHandler", packet_id: int) -> bool:
        with self.lock:
            return self.inflight.pop((id(handler), packet_id), None) is not None

    def _next_packet_id(self, record: ClientRecord) -> int:
        packet_id = record.next_packet_id
        record.next_packet_id = 1 if packet_id == 65535 else packet_id + 1
        return packet_id

    def publish(self, topic: str, payload: Dict[str, object], *, qos: int = MQTT_QOS,
                exclude: Optional["MqttConnectionHandler"] = None) -> int:
        """Route a publication and retain QoS1 command packets until PUBACK."""
        encoded = json.dumps(payload, separators=(",", ":"), ensure_ascii=False)
        deliveries: List[Tuple[ClientRecord, int]] = []
        with self.lock:
            for record in self.clients.values():
                if record.handler is exclude:
                    continue
                matches = [sub_qos for filt, sub_qos in record.subscriptions
                           if topic_matches(filt, topic)]
                if matches:
                    deliveries.append((record, min(qos, max(matches))))
        delivered = 0
        for record, delivery_qos in deliveries:
            try:
                if delivery_qos == 1:
                    with self.lock:
                        packet_id = self._next_packet_id(record)
                    packet = mqtt_build_publish(topic, encoded, qos=1, packet_id=packet_id)
                    record.handler.send_packet(packet)
                    with self.lock:
                        self.inflight[(id(record.handler), packet_id)] = PendingDelivery(
                            packet, topic, 1, time.monotonic() + RETRY_INTERVAL_S)
                else:
                    record.handler.send_packet(mqtt_build_publish(topic, encoded, qos=0))
                delivered += 1
            except OSError:
                self.unregister(record.handler)
        return delivered

    def retry_expired(self) -> None:
        """QoS1 retry: same packet id, DUP flag, bounded retry count."""
        now = time.monotonic()
        with self.lock:
            expired = [(key, item) for key, item in self.inflight.items()
                       if item.retry_at <= now]
        for key, item in expired:
            handler_id, _ = key
            with self.lock:
                record = self.clients.get(handler_id)
                current = self.inflight.get(key)
            if record is None or current is not item:
                continue
            if item.attempts >= MAX_DELIVERY_ATTEMPTS:
                print(f"[QOS1] delivery failed after {item.attempts} attempts: {item.topic}")
                with self.lock:
                    self.inflight.pop(key, None)
                continue
            try:
                resent = bytearray(item.packet)
                resent[0] |= 0x08
                record.handler.send_packet(bytes(resent))
                with self.lock:
                    item.attempts += 1
                    item.retry_at = now + RETRY_INTERVAL_S
                print(f"[QOS1] retry {item.attempts}/{MAX_DELIVERY_ATTEMPTS}: {item.topic}")
            except OSError:
                self.unregister(record.handler)

    def mission(self, callbox_id: str, task: int) -> Dict[str, object]:
        with self.lock:
            return dict(self.missions.get((callbox_id, task),
                         {"state": "idle", "seq": 0, "agv_id": ""}))

    def set_mission(self, callbox_id: str, task: int, state: str, *, seq: Optional[int] = None,
                    agv_id: Optional[str] = None) -> Dict[str, object]:
        with self.lock:
            mission = self.missions.setdefault((callbox_id, task),
                                               {"state": "idle", "seq": 0, "agv_id": ""})
            mission["state"] = state
            if seq is not None:
                mission["seq"] = int(seq)
            if agv_id is not None:
                mission["agv_id"] = agv_id
            return dict(mission)

    def note_cancel(self, callbox_id: str, task: int, seq: int) -> None:
        with self.lock:
            self.cancel_sequences[(callbox_id, task)] = seq

    def cancel_sequence(self, callbox_id: str, task: int) -> int:
        with self.lock:
            return self.cancel_sequences.get((callbox_id, task), 0)

    def clear_cancel(self, callbox_id: str, task: int) -> None:
        with self.lock:
            self.cancel_sequences.pop((callbox_id, task), None)

    def publish_command(self, callbox_id: str, payload: Dict[str, object]) -> int:
        return self.publish(f"callbox/{callbox_id}/cmd", payload, qos=MQTT_QOS)


class MqttConnectionHandler(socketserver.BaseRequestHandler):
    """One MQTT TCP connection.  Unsupported/malformed packets close safely."""

    state: BrokerState

    def setup(self) -> None:
        self.request.settimeout(35.0)
        self.client_id = "unknown"
        self._send_lock = threading.Lock()
        self._seen_qos1: List[int] = []

    def send_packet(self, packet: bytes) -> None:
        with self._send_lock:
            self.request.sendall(packet)

    def handle(self) -> None:
        try:
            while True:
                packet_type, flags, body = self._read_packet()
                self._process_packet(packet_type, flags, body)
        except (ConnectionError, OSError, ValueError, socket.timeout):
            pass
        finally:
            self.state.unregister(self)

    def _read_exact(self, count: int) -> bytes:
        data = bytearray()
        while len(data) < count:
            chunk = self.request.recv(count - len(data))
            if not chunk:
                raise ConnectionError("peer closed connection")
            data.extend(chunk)
        return bytes(data)

    def _read_packet(self) -> Tuple[int, int, bytes]:
        header = self._read_exact(1)[0]
        value, multiplier = 0, 1
        for _ in range(4):
            byte = self._read_exact(1)[0]
            value += (byte & 0x7F) * multiplier
            if not byte & 0x80:
                return header >> 4, header & 0x0F, self._read_exact(value) if value else b""
            multiplier *= 128
        raise ValueError("invalid MQTT remaining length")

    def _process_packet(self, packet_type: int, flags: int, body: bytes) -> None:
        if packet_type == 1:
            self._connect(body)
        elif packet_type == 3:
            self._publish(flags, body)
        elif packet_type == 4:
            if len(body) != 2:
                raise ValueError("malformed PUBACK")
            packet_id = int.from_bytes(body, "big")
            if self.state.puback(self, packet_id):
                print(f"[PUBACK] command delivery confirmed id={packet_id}")
        elif packet_type == 8:
            self._subscribe(flags, body)
        elif packet_type == 12:
            if body or flags:
                raise ValueError("malformed PINGREQ")
            self.send_packet(b"\xD0\x00")
        elif packet_type == 14:
            return
        else:
            raise ValueError(f"unsupported MQTT packet type {packet_type}")

    def _connect(self, body: bytes) -> None:
        protocol, offset = mqtt_read_utf8(body, 0)
        if protocol != "MQTT" or offset + 4 > len(body) or body[offset] != 4:
            raise ValueError("only MQTT 3.1.1 is supported")
        connect_flags = body[offset + 1]
        keepalive = int.from_bytes(body[offset + 2:offset + 4], "big")
        self.client_id, offset = mqtt_read_utf8(body, offset + 4)
        # Credentials are deliberately accepted but never logged or retained.
        if not self.client_id or connect_flags & 0x01:
            raise ValueError("invalid MQTT CONNECT")
        self.state.register(self, self.client_id)
        self.send_packet(b"\x20\x02\x00\x00")
        print(f"[CONNECT] client_id={self.client_id} keepalive={keepalive}s")

    def _subscribe(self, flags: int, body: bytes) -> None:
        if flags != 0x02 or len(body) < 5:
            raise ValueError("malformed SUBSCRIBE")
        packet_id = int.from_bytes(body[:2], "big")
        offset = 2
        granted: List[int] = []
        filters: List[Tuple[str, int]] = []
        while offset < len(body):
            topic_filter, offset = mqtt_read_utf8(body, offset)
            if offset >= len(body):
                raise ValueError("truncated subscription QoS")
            requested_qos = body[offset]
            offset += 1
            if requested_qos not in (0, 1):
                granted.append(0x80)
                continue
            granted.append(self.state.add_subscription(self, topic_filter, requested_qos))
            filters.append((topic_filter, requested_qos))
        self.send_packet(b"\x90" + mqtt_enc_len(2 + len(granted)) +
                         packet_id.to_bytes(2, "big") + bytes(granted))
        print(f"[SUBSCRIBE] {self.client_id}: {filters}; granted={granted}")

    def _publish(self, flags: int, body: bytes) -> None:
        qos = (flags >> 1) & 0x03
        if qos == 2:
            raise ValueError("QoS 2 is intentionally unsupported")
        topic, offset = mqtt_read_utf8(body, 0)
        packet_id = 0
        if qos == 1:
            if offset + 2 > len(body):
                raise ValueError("truncated QoS1 packet id")
            packet_id = int.from_bytes(body[offset:offset + 2], "big")
            offset += 2
        payload = body[offset:].decode("utf-8")
        duplicate = bool(flags & 0x08)
        is_duplicate = qos == 1 and packet_id in self._seen_qos1
        if qos == 1:
            self.send_packet(mqtt_build_puback(packet_id))
            if not is_duplicate:
                self._seen_qos1.append(packet_id)
                self._seen_qos1 = self._seen_qos1[-64:]
        if is_duplicate:
            print(f"[QOS1] duplicate event acknowledged id={packet_id}; not processed twice")
            return
        self._handle_application_publish(topic, payload, duplicate)

    def _handle_application_publish(self, topic: str, payload: str, duplicate: bool) -> None:
        try:
            event = json.loads(payload)
        except json.JSONDecodeError:
            print(f"[EVENT] ignored invalid JSON on {topic}")
            return
        if not isinstance(event, dict):
            print(f"[EVENT] ignored non-object JSON on {topic}")
            return
        print(f"[EVENT] {topic} {json.dumps(event, ensure_ascii=False)}")
        parts = topic.split("/")
        if len(parts) != 3 or parts[0] != "callbox":
            return
        callbox_id = parts[1]
        if parts[2] == "event":
            self._handle_event(callbox_id, event)
        elif parts[2] == "status":
            print(f"[STATUS] {callbox_id}: {event}")

    def _handle_event(self, callbox_id: str, event: Dict[str, object]) -> None:
        event_type = event.get("type")
        task = event.get("task")
        sequence = event.get("seq")
        if not isinstance(sequence, int) or sequence <= 0:
            print("[WCS] ignored event without a valid seq")
            return
        if event_type == "sync_request":
            self._reply_sync(callbox_id, sequence)
            return
        if event_type not in ("call", "cancel") or task not in (1, 2):
            print("[WCS] ignored unsupported event")
            return
        task_id = int(task)
        if event_type == "call":
            self.state.set_mission(callbox_id, task_id, "pending_call", seq=sequence)
            print(f"[WCS] call task={task_id} seq={sequence}")
            if self.state.auto_accept:
                self.command(callbox_id, "accepted", task_id)
        else:
            self.state.note_cancel(callbox_id, task_id, sequence)
            print(f"[WCS] cancel task={task_id} seq={sequence}")
            if self.state.auto_accept:
                self.command(callbox_id, "cancel_ack", task_id)

    def _reply_sync(self, callbox_id: str, sequence: int) -> None:
        task1, task2 = self.state.mission(callbox_id, 1), self.state.mission(callbox_id, 2)
        for mission in (task1, task2):
            if mission["state"] == "pending_call":
                mission["state"] = "idle"
                mission["seq"] = 0
        payload = {
            "type": "sync", "ref_seq": sequence, "ts": int(time.time()),
            "task1_state": task1["state"], "task1_seq": task1["seq"],
            "task1_agv_id": task1["agv_id"], "task2_state": task2["state"],
            "task2_seq": task2["seq"], "task2_agv_id": task2["agv_id"],
        }
        self.state.publish_command(callbox_id, payload)
        print(f"[SYNC] reply for {callbox_id}, ref_seq={sequence}")

    def command(self, callbox_id: str, command: str, task: int) -> bool:
        """Issue a WCS command with the only valid transaction sequence."""
        if command == "cancel_ack":
            ref_seq = self.state.cancel_sequence(callbox_id, task)
        else:
            ref_seq = int(self.state.mission(callbox_id, task)["seq"])
        if ref_seq <= 0:
            print(f"[WCS] cannot send {command}: task {task} has no matching transaction")
            return False
        current = self.state.mission(callbox_id, task)
        agv_id = str(current["agv_id"])
        if command == "accepted":
            current = self.state.set_mission(callbox_id, task, "queued", seq=ref_seq)
        elif command == "assigned":
            current = self.state.set_mission(callbox_id, task, "assigned", agv_id=f"AGV-{task}")
        elif command == "locked":
            current = self.state.set_mission(callbox_id, task, "locked")
        elif command in ("completed", "rejected"):
            current = self.state.set_mission(callbox_id, task, "idle", seq=0, agv_id="")
        elif command == "cancel_ack":
            current = self.state.set_mission(callbox_id, task, "idle", seq=0, agv_id="")
            self.state.clear_cancel(callbox_id, task)
        elif command != "overdue":
            raise ValueError(f"unsupported command {command}")
        payload = {"type": command, "task": task, "ref_seq": ref_seq,
                   "ts": int(time.time()), "agv_id": current["agv_id"]}
        recipients = self.state.publish_command(callbox_id, payload)
        print(f"[WCS] {command} task={task} ref_seq={ref_seq}; recipients={recipients}")
        return recipients > 0


class ReusableThreadingTCPServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def command_loop(state: BrokerState, target_id: str) -> None:
    commands = {"1": "accepted", "2": "assigned", "3": "locked", "4": "completed",
                "5": "rejected", "6": "overdue", "7": "cancel_ack"}
    print("Commands: 1=accepted 2=assigned 3=locked 4=completed 5=rejected "
          "6=overdue 7=cancel_ack, then task 1/2; q=quit")
    while True:
        try:
            key = input("> ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            return
        if key == "q":
            return
        command = commands.get(key)
        if command is None:
            continue
        try:
            task = int(input("Task (1/2): ").strip())
        except ValueError:
            continue
        if task not in (1, 2):
            continue
        # Select any connected Callbox id if caller did not use the default.
        handler = None
        with state.lock:
            for record in state.clients.values():
                if record.client_id == f"AUBOT-Callbox-{target_id}":
                    handler = record.handler
                    break
        if handler is None:
            print(f"[WCS] Callbox {target_id} is not connected")
            continue
        handler.command(target_id, command, task)


def read_packet(sock: socket.socket) -> Tuple[int, int, bytes]:
    def read_exact(count: int) -> bytes:
        data = bytearray()
        while len(data) < count:
            chunk = sock.recv(count - len(data))
            if not chunk:
                raise ConnectionError("peer closed")
            data.extend(chunk)
        return bytes(data)
    header = read_exact(1)[0]
    value, multiplier = 0, 1
    for _ in range(4):
        byte = read_exact(1)[0]
        value += (byte & 0x7F) * multiplier
        if not byte & 0x80:
            return header >> 4, header & 0x0F, read_exact(value) if value else b""
        multiplier *= 128
    raise AssertionError("bad remaining length")


def build_connect(client_id: str) -> bytes:
    body = mqtt_utf8("MQTT") + bytes([4, 0x02, 0, 30]) + mqtt_utf8(client_id)
    return b"\x10" + mqtt_enc_len(len(body)) + body


def build_subscribe(topic: str, packet_id: int = 1) -> bytes:
    body = packet_id.to_bytes(2, "big") + mqtt_utf8(topic) + b"\x01"
    return b"\x82" + mqtt_enc_len(len(body)) + body


def parse_publish(flags: int, body: bytes) -> Tuple[str, int, Dict[str, object]]:
    topic, offset = mqtt_read_utf8(body, 0)
    qos = (flags >> 1) & 3
    packet_id = int.from_bytes(body[offset:offset + 2], "big") if qos == 1 else 0
    offset += 2 if qos == 1 else 0
    return topic, packet_id, json.loads(body[offset:].decode("utf-8"))


def selftest(port: int) -> None:
    """Exercise the broker with two real clients, including QoS1 downlink/PUBACK."""
    box = socket.create_connection(("127.0.0.1", port), timeout=3)
    wcs = socket.create_connection(("127.0.0.1", port), timeout=3)
    try:
        box.sendall(build_connect("AUBOT-Callbox-02"))
        assert read_packet(box) == (2, 0, b"\x00\x00")
        box.sendall(build_subscribe("callbox/02/cmd"))
        packet_type, _, body = read_packet(box)
        assert packet_type == 9 and body == b"\x00\x01\x01", "SUBACK must grant QoS1"
        wcs.sendall(build_connect("WCS-selftest"))
        assert read_packet(wcs) == (2, 0, b"\x00\x00")
        wcs.sendall(mqtt_build_publish("callbox/02/event",
                                       json.dumps({"type": "call", "task": 1,
                                                   "seq": 42, "ts": 1}),
                                       qos=1, packet_id=7))
        assert read_packet(wcs) == (4, 0, b"\x00\x07"), "broker must PUBACK event"
        packet_type, flags, body = read_packet(box)
        assert packet_type == 3 and ((flags >> 1) & 3) == 1
        topic, packet_id, command = parse_publish(flags, body)
        assert topic == "callbox/02/cmd" and command["type"] == "accepted"
        assert command["ref_seq"] == 42 and packet_id != 0
        box.sendall(mqtt_build_puback(packet_id))
        # Verify cancel uses the separate cancel sequence, not the call sequence.
        wcs.sendall(mqtt_build_publish("callbox/02/event",
                                       json.dumps({"type": "cancel", "task": 1,
                                                   "seq": 43, "ts": 2}),
                                       qos=1, packet_id=8))
        assert read_packet(wcs) == (4, 0, b"\x00\x08")
        packet_type, flags, body = read_packet(box)
        topic, packet_id, command = parse_publish(flags, body)
        assert packet_type == 3 and topic == "callbox/02/cmd"
        assert command["type"] == "cancel_ack" and command["ref_seq"] == 43
        box.sendall(mqtt_build_puback(packet_id))
        # Do not acknowledge this command immediately: the second packet must
        # retain its packet id and set MQTT DUP, proving the retry path works.
        wcs.sendall(mqtt_build_publish("callbox/02/event",
                                       json.dumps({"type": "call", "task": 2,
                                                   "seq": 44, "ts": 3}),
                                       qos=1, packet_id=9))
        assert read_packet(wcs) == (4, 0, b"\x00\x09")
        packet_type, flags, body = read_packet(box)
        _, retry_packet_id, command = parse_publish(flags, body)
        assert packet_type == 3 and command["ref_seq"] == 44
        packet_type, retry_flags, retry_body = read_packet(box)
        _, repeated_packet_id, repeated_command = parse_publish(retry_flags, retry_body)
        assert packet_type == 3 and retry_flags & 0x08, "retry must set MQTT DUP"
        assert repeated_packet_id == retry_packet_id and repeated_command == command
        box.sendall(mqtt_build_puback(retry_packet_id))
        print("[SELFTEST] PASS: QoS1 subscribe, PUBACK, call/cancel ref_seq, DUP retry")
    finally:
        box.close()
        wcs.close()


def get_local_ip() -> str:
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.connect(("8.8.8.8", 80))
            return probe.getsockname()[0]
    except OSError:
        return "<laptop-LAN-IP>"


def main() -> None:
    parser = argparse.ArgumentParser(description="MQTT 3.1.1 LAN test broker for Callbox")
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--id", default="02", help="Callbox numeric ID for the terminal menu")
    parser.add_argument("--no-auto-accept", dest="auto_accept", action="store_false",
                        help="wait for manual WCS commands after receiving call/cancel")
    parser.add_argument("--daemon", action="store_true")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    state = BrokerState(auto_accept=args.auto_accept)
    MqttConnectionHandler.state = state
    with ReusableThreadingTCPServer((args.bind, args.port), MqttConnectionHandler) as server:
        threading.Thread(target=server.serve_forever, daemon=True).start()
        threading.Thread(target=lambda: _retry_loop(state), daemon=True).start()
        print(f"[BROKER] MQTT 3.1.1 test broker listening on {args.bind}:{args.port}")
        print(f"[BROKER] Configure Callbox: broker={get_local_ip()} port={args.port}, id={args.id}")
        print("[BROKER] LAN test only: no TLS, no authentication, no production persistence")
        if args.selftest:
            selftest(args.port)
            return
        try:
            if args.daemon:
                while True:
                    time.sleep(3600)
            else:
                command_loop(state, args.id)
        except KeyboardInterrupt:
            pass
        finally:
            server.shutdown()


def _retry_loop(state: BrokerState) -> None:
    while True:
        time.sleep(0.2)
        state.retry_expired()


if __name__ == "__main__":
    main()
