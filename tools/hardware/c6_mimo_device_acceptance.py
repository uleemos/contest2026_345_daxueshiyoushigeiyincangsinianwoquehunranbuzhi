#!/usr/bin/env python3
"""Run credential-safe ESP32-P4/C6 TLS and real MiMo acceptance."""

import argparse
import datetime
import pathlib
import sys
import time

import serial
from serial.serialutil import SerialException


def load_env(path: pathlib.Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip().strip('"').strip("'")
    return values


def open_port(name: str, timeout: float) -> serial.Serial:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            port = serial.Serial()
            port.port = name
            port.baudrate = 115200
            port.timeout = 0.1
            port.write_timeout = 2
            port.dtr = False
            port.rts = False
            port.open()
            return port
        except (OSError, SerialException):
            time.sleep(0.2)
    raise SerialException(f"{name} did not become available")


def emit(data: bytes, log_file) -> None:
    sys.stdout.buffer.write(data)
    sys.stdout.buffer.flush()
    log_file.write(data)
    log_file.flush()


def read_until(port: serial.Serial, token: bytes, timeout: float,
               log_file, secrets: tuple[bytes, ...] = ()) -> bytes:
    """Buffer until token, then redact before any bytes leave the process."""
    deadline = time.monotonic() + timeout
    data = bytearray()
    while time.monotonic() < deadline:
        chunk = port.read(port.in_waiting or 1)
        if chunk:
            data.extend(chunk)
            if token in data:
                clean = bytes(data)
                for secret in secrets:
                    if secret:
                        clean = clean.replace(secret, b"<redacted>")
                emit(clean, log_file)
                return clean
        else:
            time.sleep(0.02)
    clean = bytes(data)
    for secret in secrets:
        if secret:
            clean = clean.replace(secret, b"<redacted>")
    emit(clean, log_file)
    raise TimeoutError(f"timed out waiting for {token!r}")


def send(port: serial.Serial, value: str) -> None:
    # NuttX's canonical console consumes CR as Enter.  CRLF can leave LF for
    # the next fgets() prompt and accidentally submit an empty credential.
    port.write(value.encode("utf-8") + b"\r")
    port.flush()


def command(port: serial.Serial, value: str, timeout: float, log_file,
            secrets: tuple[bytes, ...]) -> bytes:
    marker = f"\n===== COMMAND: {value} =====\n".encode()
    emit(marker, log_file)
    send(port, value)
    return read_until(port, b"nsh>", timeout, log_file, secrets)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--workout", action="store_true",
                        help="Validate fixed structured workout via real cloud agent")
    parser.add_argument("--sd-readonly", action="store_true",
                        help="Read/mount SD with C6 active, then repeat after MiMo; no writes")
    parser.add_argument("--queue-workout", action="store_true",
                        help="New exclusive SD test directory: persisted injected failure then real sender")
    parser.add_argument("--env", type=pathlib.Path, required=True)
    parser.add_argument("--log", type=pathlib.Path, required=True)
    parser.add_argument("--asr-fixture", action="store_true",
                        help="Upload synthetic speech from P4, never record microphone")
    parser.add_argument("--tts", action="store_true",
                        help="Stream real mimo-v2.5-tts PCM16 to the speaker")
    parser.add_argument("--coach", action="store_true",
                        help="Fixed summary to real advice, LCD and TTS")
    parser.add_argument("--demo-config", action="store_true",
                        help="Store Wi-Fi/API credentials in RAM without starting C6")
    args = parser.parse_args()
    if sum((args.workout, args.asr_fixture, args.tts, args.coach,
            args.demo_config)) > 1:
        parser.error('fixture flags and --demo-config are mutually exclusive')
    if args.queue_workout and (args.workout or args.asr_fixture or args.tts or args.coach or args.sd_readonly):
        parser.error('--queue-workout requires fresh boot and is exclusive of other fixture flags')

    env = load_env(args.env)
    ssid = env.get("VELAFIT_WIFI_SSID", "")
    password = env.get("VELAFIT_WIFI_PASSWORD", "")
    api_key = env.get("MIMO_API_KEY", "")
    if not ssid or not password:
        print("missing VELAFIT_WIFI_SSID or VELAFIT_WIFI_PASSWORD",
              file=sys.stderr)
        return 2
    if not api_key.startswith("sk-"):
        print("missing ordinary sk- MIMO_API_KEY", file=sys.stderr)
        return 2

    secrets = (password.encode("utf-8"), api_key.encode("utf-8"))
    args.log.parent.mkdir(parents=True, exist_ok=True)
    transcript = bytearray()
    sd_pass = True
    port = open_port(args.port, 20)
    port.dtr = False
    port.rts = False
    try:
        with args.log.open("wb") as log_file:
            started = datetime.datetime.now().astimezone().isoformat()
            emit((f"C6 MiMo device acceptance started={started} "
                  f"port={args.port} endpoint="
                  "https://api.xiaomimimo.com/v1/chat/completions "
                  f"model={'mimo-v2.5-asr' if args.asr_fixture else 'mimo-v2.5-tts+mimo-v2.5' if args.coach else 'mimo-v2.5-tts' if args.tts else 'mimo-v2.5'}\n").encode(), log_file)
            send(port, "")
            transcript.extend(read_until(port, b"nsh>", 15, log_file,
                                         secrets))

            if args.demo_config:
                emit(b"\n===== COMMAND: c6_wifi demo-config =====\n",
                     log_file)
                send(port, "c6_wifi demo-config")
                transcript.extend(read_until(port, b"Wi-Fi SSID:", 10,
                                             log_file, secrets))
                send(port, ssid)
                transcript.extend(read_until(
                    port, b"Wi-Fi password (hidden):", 10, log_file,
                    secrets))
                send(port, password)
                transcript.extend(read_until(port, b"MiMo API key (hidden):",
                                             10, log_file, secrets))
                send(port, api_key)
                configured = read_until(port, b"nsh>", 15, log_file,
                                        secrets)
                transcript.extend(configured)
                passed = (b"[VELAFIT] DEMO_CONFIG PASS network_started=no"
                          in configured)
                print(f"CHECK deferred demo config: "
                      f"{'PASS' if passed else 'FAIL'}")
                return 0 if passed else 2

            emit(b"\n===== COMMAND: c6_wifi connect (interactive) =====\n",
                 log_file)
            send(port, "c6_wifi connect")
            transcript.extend(read_until(port, b"Wi-Fi SSID:", 20,
                                         log_file, secrets))
            send(port, ssid)
            transcript.extend(read_until(port, b"Wi-Fi password (hidden):",
                                         10, log_file, secrets))
            send(port, password)
            connect = read_until(port, b"nsh>", 45, log_file, secrets)
            transcript.extend(connect)
            if b"netdev=eth0" not in connect:
                print("CHECK association/netdev: FAIL", file=sys.stderr)
                return 2

            transcript.extend(command(port, "ifup eth0", 10, log_file,
                                      secrets))
            transcript.extend(command(port, "ifconfig eth0 dhcp", 30,
                                      log_file, secrets))

            if args.sd_readonly:
                result = command(port, "c6_wifi sd-file-readback", 90,
                                 log_file, secrets)
                transcript.extend(result)
                sd_pass = b"SDTEST: reboot readback PASS ret=0 bytes=16384" in result
                if not sd_pass:
                    print("CHECK shared SD initial read: FAIL", file=sys.stderr)
                    return 2

            # Real SNI and VERIFY_REQUIRED authenticate the live connection.
            # Then explicitly verify its chain against a wrong expected name,
            # before application data.  Server SNI rejection is not evidence
            # of local hostname verification.
            negative = command(port, "c6_wifi tls invalid.example", 60,
                               log_file, secrets)
            transcript.extend(negative)
            negative_pass = (b"TLS hostname check FAIL:" in negative and
                             b"phase=post-handshake" in negative and
                             b"hostname_mismatch=yes" in negative and
                             b"TLS verify PASS:" not in negative)

            positive = command(port, "c6_wifi tls", 60, log_file, secrets)
            transcript.extend(positive)
            positive_pass = b"TLS verify PASS: host=api.xiaomimimo.com" in positive

            request_command = ("c6_wifi queue-workout" if args.queue_workout else
                               "c6_wifi workout" if args.workout else
                               "c6_wifi asr-fixture" if args.asr_fixture else
                               "c6_wifi coach-test" if args.coach else
                               "c6_wifi tts" if args.tts else "c6_wifi mimo")
            emit(f"\nCOMMAND: {request_command} (interactive key)\n".encode(), log_file)
            send(port, request_command)
            transcript.extend(read_until(port, b"MiMo API key (hidden):",
                                         30, log_file, secrets))
            send(port, api_key)
            mimo = read_until(port, b"nsh>", 180, log_file, secrets)
            transcript.extend(mimo)
            if args.coach:
                mimo_pass = (b"[VELAFIT] AI_ANALYSIS -> AI_RESULT http=200" in mimo and
                             b"MiMo TTS STREAM_DONE status=200" in mimo and
                             b"[VELAFIT] PLAYBACK_DONE -> IDLE" in mimo and
                             b"[COACH-TEST] PASS advice_http=200" in mimo)
            elif args.tts:
                mimo_pass = (b"MiMo TTS HTTP status=200" in mimo and
                             b"MiMo TTS STREAM_DONE status=200" in mimo and
                             b"[TTS-TEST] PLAYBACK_DONE" in mimo and
                             b"TLS verify PASS: host=api.xiaomimimo.com" in mimo)
            else:
                mimo_pass = (b"MiMo device PASS: status=200" in mimo and
                             b"MiMo response:" in mimo and
                             b"TLS verify PASS: host=api.xiaomimimo.com" in mimo)
            if args.asr_fixture:
                mimo_pass = mimo_pass and "运动教练".encode() in mimo
            if args.workout:
                mimo_pass = mimo_pass and b"Workout schema PASS session=VF-FIXED-001" in mimo
            if args.queue_workout:
                mimo_pass = (mimo_pass and
                    b"Workout queue schema PASS session=VF-SD-CLOUD-20260915" in mimo and
                    b"SDCLOUD persisted retry PASS ret=0 injected_offline=1 real_sender_calls=1" in mimo)
            if args.sd_readonly:
                result = command(port, "c6_wifi sd-file-readback", 90,
                                 log_file, secrets)
                transcript.extend(result)
                sd_pass = sd_pass and b"SDTEST: reboot readback PASS ret=0 bytes=16384" in result
    finally:
        port.close()
        password = ""
        api_key = ""

    checks = {
        "association/netdev": b"netdev=eth0" in transcript,
        "hostname mismatch rejected": negative_pass,
        "trusted TLS + hostname": positive_pass,
        "real summary/advice/LCD/TTS pipeline" if args.coach else
        "real MiMo TTS PCM speaker pipeline" if args.tts else
        "real MiMo HTTP/semantic response": mimo_pass,
    }
    if args.sd_readonly:
        checks["shared SD existing-file read before/after cloud"] = sd_pass
    for name, passed in checks.items():
        print(f"CHECK {name}: {'PASS' if passed else 'FAIL'}")
    return 0 if all(checks.values()) else 2


if __name__ == "__main__":
    raise SystemExit(main())
