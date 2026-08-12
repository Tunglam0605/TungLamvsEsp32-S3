#!/usr/bin/env python3
"""Dependency-free MQTT 3.1.1 bench client for the Gateway contract."""

import argparse
import asyncio
import json
import os
import re
import ssl
import time


VALID_STATES = ("EMPTY", "OCCUPIED", "UNKNOWN", "FAULT")
STATE_PAIRS = {"EMPTY": "00", "OCCUPIED": "01", "UNKNOWN": "10", "FAULT": "11"}
STATUS_JSON_FIELDS = (
    "schema", "source_type", "company_id", "site_id", "warehouse_id",
    "warehouse_name", "slot_count", "order", "state_bits", "states",
    "occupied_count", "empty_count", "unknown_count", "fault_count",
    "sequence", "layout_version", "generated_at",
)
RFC3339_FRACTIONAL = re.compile(
    r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{6}Z$"
)


def validate_segment(name, value):
    if not value or len(value) > 31 or not value[0].isalnum():
        raise ValueError(f"{name} must be 1..31 lowercase ASCII letters, digits, '-' or '_'")
    if any(character not in "abcdefghijklmnopqrstuvwxyz0123456789-_" for character in value):
        raise ValueError(f"invalid {name}: {value!r}")
    return value


def gateway_topics(args):
    company = validate_segment("company_id", args.company_id)
    site = validate_segment("site_id", args.site_id)
    warehouse = validate_segment("warehouse_id", args.warehouse_id)
    base = f"warehouse/sensor/{company}/{site}/{warehouse}"
    return {
        "base": base,
        "status_json": f"{base}/status/json",
        "status_bits": f"{base}/status/bits",
    }


class StatusPairVerifier:
    def __init__(self, topics):
        self.topics = topics
        self.json_snapshot = None
        self.raw_bits = None
        self.consistent_pairs = 0
        self.errors = 0

    def _error(self, detail, **fields):
        self.errors += 1
        log("status_pair_error", detail=detail, **fields)

    def accept(self, topic, payload):
        if topic == self.topics["status_json"]:
            try:
                snapshot = json.loads(payload.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError) as error:
                self._error("status/json is not valid UTF-8 JSON", error=str(error))
                return
            actual_fields = tuple(snapshot.keys()) if isinstance(snapshot, dict) else ()
            if actual_fields != STATUS_JSON_FIELDS:
                self._error("status/json fields differ from the Vision-compatible contract",
                            expected=list(STATUS_JSON_FIELDS), actual=list(actual_fields))
                return
            if snapshot.get("schema") != "WAREHOUSE_STATUS_V1" or snapshot.get("source_type") != "sensor":
                self._error("status/json has an unexpected schema or source_type")
                return
            expected_identity = (self.topics["base"].split("/", 5)[2:5])
            actual_identity = [snapshot.get("company_id"), snapshot.get("site_id"),
                               snapshot.get("warehouse_id")]
            if list(expected_identity) != actual_identity:
                self._error("status/json identity does not match its topic",
                            expected=list(expected_identity), actual=actual_identity)
                return
            slot_count = snapshot.get("slot_count")
            states = snapshot.get("states")
            state_bits = snapshot.get("state_bits")
            if slot_count not in (8, 12) or not isinstance(states, list) or len(states) != slot_count:
                self._error("slot_count/states length must be exactly 8 or 12",
                            slot_count=slot_count, states_length=len(states) if isinstance(states, list) else None)
                return
            if any(state not in VALID_STATES for state in states):
                self._error("states contains an unknown value", states=states)
                return
            encoded = "".join(STATE_PAIRS[state] for state in states)
            if state_bits != encoded:
                self._error("states and state_bits disagree", state_bits=state_bits, encoded=encoded)
                return
            counts = {state: states.count(state) for state in VALID_STATES}
            reported = {
                "EMPTY": snapshot.get("empty_count"),
                "OCCUPIED": snapshot.get("occupied_count"),
                "UNKNOWN": snapshot.get("unknown_count"),
                "FAULT": snapshot.get("fault_count"),
            }
            if counts != reported:
                self._error("state counters disagree with states", counted=counts, reported=reported)
                return
            generated_at = snapshot.get("generated_at")
            if generated_at is not None and (
                    not isinstance(generated_at, str) or
                    RFC3339_FRACTIONAL.fullmatch(generated_at) is None):
                self._error("generated_at must be null or fractional UTC RFC3339",
                            generated_at=generated_at)
                return
            self.json_snapshot = snapshot
            log("status_json_valid", sequence=snapshot["sequence"], state_bits=state_bits,
                slot_count=slot_count, layout_version=snapshot["layout_version"])
        elif topic == self.topics["status_bits"]:
            try:
                bits = payload.decode("ascii")
            except UnicodeDecodeError as error:
                self._error("status/bits is not ASCII", error=str(error))
                return
            if len(bits) not in (16, 24) or any(bit not in "01" for bit in bits):
                self._error("status/bits must be exactly 16 or 24 ASCII bits",
                            length=len(bits), payload=bits)
                return
            self.raw_bits = bits
            log("status_bits_valid", state_bits=bits, slot_count=len(bits) // 2)
        else:
            return

        if self.json_snapshot is not None and self.raw_bits is not None:
            json_bits = self.json_snapshot["state_bits"]
            if json_bits == self.raw_bits:
                self.consistent_pairs += 1
                log("status_pair_consistent", sequence=self.json_snapshot["sequence"],
                    state_bits=self.raw_bits)
                self.json_snapshot = None
                self.raw_bits = None
            else:
                self._error("status/json state_bits differs from raw status/bits",
                            json_bits=json_bits, raw_bits=self.raw_bits)
                # Keep the newest value from each topic so a following publish can
                # still complete the pair after an interleaved retained/live update.


def log(event, **fields):
    print(json.dumps({"time": time.time(), "event": event, **fields},
                     separators=(",", ":")), flush=True)


def encode_length(value):
    out = bytearray()
    while True:
        byte = value % 128
        value //= 128
        if value:
            byte |= 0x80
        out.append(byte)
        if not value:
            return bytes(out)


def packet(header, body=b""):
    return bytes([header]) + encode_length(len(body)) + body


def mqtt_string(value):
    data = value.encode() if isinstance(value, str) else value
    return len(data).to_bytes(2, "big") + data


def take_string(data, offset):
    length = int.from_bytes(data[offset:offset + 2], "big")
    start = offset + 2
    return data[start:start + length], start + length


async def read_packet(reader):
    first = (await reader.readexactly(1))[0]
    remaining = 0
    multiplier = 1
    for _ in range(4):
        byte = (await reader.readexactly(1))[0]
        remaining += (byte & 0x7f) * multiplier
        if not byte & 0x80:
            return first, await reader.readexactly(remaining)
        multiplier *= 128
    raise ValueError("invalid MQTT remaining length")


async def run(args):
    topics = gateway_topics(args)
    verifier = StatusPairVerifier(topics) if args.verify_status_pair else None
    context = ssl.create_default_context() if args.tls else None
    reader, writer = await asyncio.open_connection(args.host, args.port, ssl=context)
    client_id = args.client_id or f"gateway-bench-{int(time.time())}"
    username = os.environ.get("MQTT_BENCH_USERNAME", "")
    password = os.environ.get("MQTT_BENCH_PASSWORD", "")
    flags = 0x02 | (0x80 if username else 0) | (0x40 if password else 0)
    body = mqtt_string("MQTT") + b"\x04" + bytes([flags]) + args.keepalive.to_bytes(2, "big")
    body += mqtt_string(client_id)
    if username:
        body += mqtt_string(username)
    if password:
        body += mqtt_string(password)
    writer.write(packet(0x10, body))
    await writer.drain()
    first, data = await asyncio.wait_for(read_packet(reader), args.timeout)
    if first >> 4 != 2 or len(data) != 2 or data[1] != 0:
        raise RuntimeError(f"CONNACK rejected: {data.hex()}")
    log("connected", host=args.host, port=args.port, client_id=client_id)

    packet_id = 1
    subscribe_topic = args.topic
    if args.verify_status_pair and subscribe_topic is None:
        subscribe_topic = f"{topics['base']}/status/+"
    if subscribe_topic:
        writer.write(packet(0x82, packet_id.to_bytes(2, "big") +
                            mqtt_string(subscribe_topic) + b"\x01"))
        await writer.drain()
        log("subscribe_sent", topic=subscribe_topic, qos=1)

    for item in args.publish:
        topic, separator, payload = item.partition("=")
        if not separator:
            raise ValueError("--publish must be TOPIC=PAYLOAD")
        packet_id += 1
        body = mqtt_string(topic) + packet_id.to_bytes(2, "big") + payload.encode()
        writer.write(packet(0x32, body))
        await writer.drain()
        log("publish_sent", topic=topic, qos=1, payload=payload)

    deadline = time.monotonic() + args.duration
    while time.monotonic() < deadline:
        remaining = min(args.timeout, max(0.1, deadline - time.monotonic()))
        try:
            first, data = await asyncio.wait_for(read_packet(reader), remaining)
        except asyncio.TimeoutError:
            writer.write(packet(0xC0))
            await writer.drain()
            continue
        kind = first >> 4
        if kind == 3:
            raw_topic, offset = take_string(data, 0)
            qos = (first >> 1) & 3
            incoming_id = None
            if qos:
                incoming_id = int.from_bytes(data[offset:offset + 2], "big")
                offset += 2
            topic = raw_topic.decode(errors="replace")
            payload = data[offset:]
            log("message", topic=topic, qos=qos,
                retain=bool(first & 1), payload=payload.decode(errors="replace"))
            if verifier is not None:
                verifier.accept(topic, payload)
            if qos == 1:
                writer.write(packet(0x40, incoming_id.to_bytes(2, "big")))
                await writer.drain()
        elif kind == 9:
            log("suback", packet_id=int.from_bytes(data[:2], "big"), granted=list(data[2:]))
        elif kind == 4:
            log("puback", packet_id=int.from_bytes(data[:2], "big"))
        elif kind == 13:
            log("pingresp")
        else:
            log("packet", kind=kind, data=data.hex())

    writer.write(packet(0xE0))
    await writer.drain()
    writer.close()
    await writer.wait_closed()
    if verifier is not None:
        log("status_pair_summary", consistent_pairs=verifier.consistent_pairs,
            errors=verifier.errors)
        if verifier.errors or verifier.consistent_pairs == 0:
            raise RuntimeError("no valid matching status/json + status/bits pair received")
    log("complete")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("host")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--topic")
    parser.add_argument("--publish", action="append", default=[])
    parser.add_argument("--company-id", default="aubot")
    parser.add_argument("--site-id", default="ha-noi")
    parser.add_argument("--warehouse-id", default="kho-01")
    parser.add_argument("--verify-status-pair", action="store_true",
                        help="subscribe status/+ and require matching valid JSON and raw bits")
    parser.add_argument("--duration", type=float, default=15)
    parser.add_argument("--timeout", type=float, default=5)
    parser.add_argument("--keepalive", type=int, default=20)
    parser.add_argument("--client-id")
    parser.add_argument("--tls", action="store_true")
    try:
        asyncio.run(run(parser.parse_args()))
    except Exception as error:
        log("error", type=type(error).__name__, detail=str(error))
        raise
