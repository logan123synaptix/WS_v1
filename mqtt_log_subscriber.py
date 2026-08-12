#!/usr/bin/env python3
"""
Subscribe to WS_v1's telemetry + heartbeat MQTT topics and log every
message (with a local receive timestamp) to log_test_weatherstation.log.

Topics match SynaptiX_FDK/app/app_config.h:
    MQTT_STATION_DATA_TOPIC      = "hanoi/air_quality/data/"
    MQTT_STATION_HEARTBEAT_TOPIC = "hanoi/air_quality/heartbeat/"
Subscribed as "<prefix>#" so it picks up every device_id, not just "001".

Broker defaults to broker.hivemq.com:1883 (the public test broker seen
in the firmware's own logs: "MQTT connected to tcp://broker.hivemq.com:1883").
Override with --host/--port/--device-id if your network_config points
somewhere else (see network_config.c's runtime-editable broker host/port).

Usage:
    python3 mqtt_log_subscriber.py
    python3 mqtt_log_subscriber.py --host 192.168.1.50 --port 1883
    python3 mqtt_log_subscriber.py --device-id 001   # narrow to one device

Requires: pip install paho-mqtt --break-system-packages
"""

import argparse
import datetime
import json
import signal
import sys

import paho.mqtt.client as mqtt

DATA_TOPIC_PREFIX = "hanoi/air_quality/data/"
HEARTBEAT_TOPIC_PREFIX = "hanoi/air_quality/heartbeat/"
LOG_FILENAME = "log_test_weatherstation.log"
BROKER_NAME = "broker.emqx.io"


def build_topics(device_id: str | None) -> list[str]:
    """"#" wildcard picks up every device_id; a specific --device-id
    narrows to just that station instead."""
    if device_id:
        return [f"{DATA_TOPIC_PREFIX}{device_id}", f"{HEARTBEAT_TOPIC_PREFIX}{device_id}"]
    return [f"{DATA_TOPIC_PREFIX}#", f"{HEARTBEAT_TOPIC_PREFIX}#"]


def make_on_connect(topics: list[str]):
    def on_connect(client, userdata, flags, reason_code, properties=None):
        if reason_code == 0 or reason_code == "Success":
            print(f"[connected] broker OK, subscribing to {len(topics)} topic(s)...")
            for t in topics:
                client.subscribe(t, qos=1)
                print(f"  subscribed: {t}")
        else:
            print(f"[connect FAILED] reason_code={reason_code}")
    return on_connect


def make_on_message(log_file):
    def on_message(client, userdata, msg):
        now = datetime.datetime.now().strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3]
        payload_raw = msg.payload.decode("utf-8", errors="replace")

        # Pretty-print if it's valid JSON, keep the raw string as a fallback
        # otherwise (e.g. a malformed/truncated payload -- worth logging
        # as-is rather than dropping it, since a truncation bug is exactly
        # the kind of thing this log is meant to catch).
        try:
            parsed = json.loads(payload_raw)
            payload_str = json.dumps(parsed, ensure_ascii=False)
        except json.JSONDecodeError:
            payload_str = payload_raw

        line = f"[{now}] topic={msg.topic} qos={msg.qos} payload={payload_str}"
        print(line)
        log_file.write(line + "\n")
        log_file.flush()

    return on_message


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default=BROKER_NAME, help="MQTT broker host (default: broker.hivemq.com)")
    parser.add_argument("--port", type=int, default=1883, help="MQTT broker port (default: 1883)")
    parser.add_argument("--device-id", default=None, help="Narrow to one device_id instead of subscribing to all (default: all, via '#')")
    parser.add_argument("--out", default=LOG_FILENAME, help=f"Output log file path (default: {LOG_FILENAME})")
    args = parser.parse_args()

    topics = build_topics(args.device_id)

    log_file = open(args.out, "a", encoding="utf-8")
    log_file.write(f"\n=== session start {datetime.datetime.now().isoformat()} "
                    f"host={args.host}:{args.port} topics={topics} ===\n")
    log_file.flush()

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"log_test_weatherstation_{datetime.datetime.now().timestamp():.0f}")
    client.on_connect = make_on_connect(topics)
    client.on_message = make_on_message(log_file)

    def handle_sigint(sig, frame):
        print("\n[stopping] closing log file and disconnecting...")
        log_file.write(f"=== session end {datetime.datetime.now().isoformat()} ===\n")
        log_file.close()
        client.disconnect()
        sys.exit(0)

    signal.signal(signal.SIGINT, handle_sigint)

    print(f"Connecting to {args.host}:{args.port} ...")
    print(f"Logging to: {args.out}")
    client.connect(args.host, args.port, keepalive=60)
    client.loop_forever()


if __name__ == "__main__":
    main()

# python3 mqtt_log_subscriber.py --device-id 001          # chỉ theo dõi 1 thiết bị
# python3 mqtt_log_subscriber.py --host <ip broker riêng> --port 1883
# python3 mqtt_log_subscriber.py --out /path/khac/log.txt  # đổi nơi lưu