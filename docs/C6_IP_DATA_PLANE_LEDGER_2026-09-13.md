# ESP32-C6 IP data-plane evidence ledger

Date: 2026-09-13 (Asia/Shanghai)

## Hardware and protocol identity

- Board: ESP32-P4X Function EV Board V1.8, schematic dated 2026-08-05.
- Detected P4 silicon in prior acceptance: ESP32-P4 revision v3.2.
- Schematic sheet 5/6, `05_Ethernet_SDMMC_WiFi`: U1 is
  `ESP32-C6-MINI-1`. P4 GPIO14/15/16/17/18/19 connect through populated
  resistors to `SD2_D0/D1/D2/D3/CLK/CMD`; GPIO54 connects to `C6_EN`; GPIO6
  is reserved for `C6_WAKEUP`.
- No pin selection or wiring changes are introduced by the data-plane work.
- Local schematic SHA-256:
  `a87457fdcc4c8b3b3b5461603ce87a82b2ce9e99d851e4e615d4f5732aa649a5`.
- Local P4 datasheet SHA-256:
  `ef4ae149dd391ce7e2c520415492cef537ab662bb5710fb495efedf34dbc73a5`.
- Local P4 TRM SHA-256:
  `9e2d9d36b0e2c058e37bf581c1da527a88943f522e61c4ccd16cbe153b20c0e0`.
- Local P4 errata SHA-256:
  `d11bd62f1274999c60180d774e46351987482d040ed6d94d6522ded0062251a1`.

The SDMMC register/DMA implementation is not changed here; it was previously
stabilized and hardware-tested. Applicable conflicts remain: SDMMC slot 1 and
GPIO14-19 are dedicated to C6 while this feature is enabled, GPIO54 controls
C6 reset, and GPIO6 remains reserved. The inserted microSD slot is therefore
not available concurrently on the same controller configuration.

The protocol reference is Espressif `esp-hosted-mcu` commit
`3450367b247f1881153ca3ea6703e1d82cfb2405`. Its V1 wire protocol uses a
12-byte `esp_payload_header`; current enum values are STA=1, SERIAL=3 and
PRIVATE=5. STA payloads are complete Ethernet 802.3 frames. The current board
code already proves the matching SERIAL/PRIVATE values and v2 RPC IDs against
the fitted C6 firmware.

## Starting point

- SDIO function 1 discovery and CMD53 DMA: hardware PASS.
- ESP-Hosted private initialization event: hardware PASS.
- v2 RPC control and STA association event: hardware PASS.
- NuttX network interface registration: absent.
- STA Ethernet-frame TX/RX dispatch: absent; current RPC receive loop discards
  non-SERIAL frames.
- DHCP client command: disabled in the competition defconfig.

## Observable milestone

After boot, a NuttX Ethernet interface is registered. After secure credential
entry and C6 association, it must:

1. transmit and receive ESP-Hosted STA Ethernet frames;
2. obtain IPv4 address/router/DNS using DHCP;
3. resolve an external hostname;
4. exchange ICMP and TCP traffic;
5. preserve useful TX/RX/drop/error counters and report disconnects;
6. avoid printing the Wi-Fi password.

## Required evidence states

- Compile-only: successful clean `velafit_headless` build, compiler identity,
  image paths/sizes, and warnings.
- Boot-tested: C6 probe plus registered interface visible in `ifconfig`.
- Data-plane-tested: association, DHCP lease, DNS, ping/TCP, and repeated
  traffic captured from COM3, with the secret redacted.
- Remaining after P0: device TLS and real MiMo request are separate milestones.

## Implementation completed

- Added an Ethernet `netdev_lowerhalf_s` named `eth0`, with the MAC address
  obtained from the C6 `GetMacAddress` RPC instead of using a fabricated MAC.
- Added bidirectional ESP-Hosted STA Ethernet-frame transport, serialized SDIO
  control/data access, C6 carrier state, RX queueing, counters and a background
  receive poller.
- Corrected ESP-Hosted SDIO credit accounting to use the peer's 1536-byte
  buffer units and allowed an 8192-byte cumulative FIFO read containing
  multiple framed packets.
- Selected `NETDEV_RX_THREAD` for network upper-half processing. The earlier
  `NETDEV_RX_WORK` attempt incorrectly supplied scheduler priority 100 where
  NuttX expects a work-queue ID, so valid DHCP offers remained in the lower
  queue and the IPv4 receive counter stayed at zero.
- Enabled DHCP, broadcast traffic, DNS, IPv4 TCP/UDP/ICMP and network
  statistics in both Wi-Fi-capable defconfigs.
- Made credential entry interactive and erased the local password buffer after
  association. Added a host-side acceptance runner that reads the ignored
  `.secrets/velafit.env` and redacts the password before console/log output.
- Added `c6_wifi tcp <host> <port>` for a DNS-backed device-side TCP connect
  probe without embedding a destination in firmware.

## Final build and hardware result

Release-candidate test started at `2026-09-13T12:24:44+08:00` on the physical
ESP32-P4 revision v3.2 through COM3, with the fitted C6 and a real 2.4 GHz
hotspot. Build used `riscv-none-elf-gcc 13.4.0` and completed without a compiler
error. The flashed `nuttx.bin` is 3,520,132 bytes with SHA-256
`a1c875834acb35ff1a57868fda650a46b843a94d4ef38fb2ade1ed1d43374463`;
esptool verified the written flash hash and performed an automatic hard reset.

| Check | Result | Physical evidence |
| --- | --- | --- |
| C6 SDIO/CMD53/Hosted init | PASS | CIS `0092:6666`, DMA scratch and init event |
| STA association/netdev | PASS | `eth0`, C6 MAC `10:bd:a3:8b:04:a4` |
| DHCP | PASS | IPv4 `192.168.199.224`, router `192.168.199.1`, `/24` mask |
| RX into NuttX IPv4/UDP | PASS | 2 DHCP IPv4/UDP receives, zero drops at snapshot |
| DNS | PASS | `api.xiaomimimo.com` resolved by the device |
| External ICMP | PASS | `1.1.1.1`, 3 transmitted / 3 received, 0% loss |
| MiMo endpoint TCP reachability | PASS | Device connected to `api.xiaomimimo.com:443` |

Credential-safe raw evidence is in
`hardware-logs/c6-ip-data-plane-20260913-final.log`.

The ROM prints a SHA comparison warning because this board's image command
uses an Espressif RAM-only header and intentionally does not append a digest.
This is not the flash-write check: esptool independently reported `Hash of data
verified` before reset.

## Boundary of this milestone

This closes the C6 **IP data plane** milestone: association, Ethernet transport,
DHCP, DNS, ICMP and TCP/443 are hardware-proven. It does not claim device TLS,
certificate/hostname validation, HTTP request/response parsing, API
authentication, MiMo inference, retries, or an end-to-end camera/cloud result.
Those remain the next cloud-transport milestone and must use the normal MiMo
API key only from private configuration.
