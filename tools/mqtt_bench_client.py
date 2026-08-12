#!/usr/bin/env python3
"""Dependency-free MQTT 3.1.1 bench subscriber/publisher."""

import argparse
import asyncio
import json
import os
import ssl
import time


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
    if args.topic:
        writer.write(packet(0x82, packet_id.to_bytes(2, "big") +
                            mqtt_string(args.topic) + b"\x01"))
        await writer.drain()
        log("subscribe_sent", topic=args.topic, qos=1)

    publish_items = list(args.publish)
    for command in args.command:
        publish_items.append(
            f"gateway/{args.gateway_id}/cmd=" + json.dumps({"cmd": command}, separators=(",", ":"))
        )
    malformed = {
        "empty": {},
        "unknown": {"cmd": "unknown"},
        "numeric": {"cmd": 123},
        "oversize": {"cmd": "x" * 1100},
    }
    for name in args.malformed:
        publish_items.append(
            f"gateway/{args.gateway_id}/cmd=" + json.dumps(malformed[name], separators=(",", ":"))
        )
    for item in publish_items:
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
            log("message", topic=raw_topic.decode(errors="replace"), qos=qos,
                retain=bool(first & 1), payload=data[offset:].decode(errors="replace"))
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
    log("complete")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("host")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--topic")
    parser.add_argument("--publish", action="append", default=[])
    parser.add_argument("--command", choices=("ping", "request_snapshot"), action="append", default=[])
    parser.add_argument("--malformed", choices=("empty", "unknown", "numeric", "oversize"), action="append", default=[])
    parser.add_argument("--gateway-id", default="GW-01")
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
