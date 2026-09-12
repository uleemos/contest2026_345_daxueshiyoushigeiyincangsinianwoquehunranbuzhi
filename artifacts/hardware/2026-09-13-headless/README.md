# ESP32-P4 headless validation evidence

Raw COM3 transcripts remain in this directory locally and are ignored because
the 30-minute transcript contains more than 50,000 repetitive CRLF serial
lines. The hashes below make the original evidence auditable without adding a
multi-megabyte terminal dump to Git.

| Transcript | SHA-256 | Key result |
|---|---|---|
| `camera-30min-soak-com3.log` | `831d331f5227f4387b26f00d73bc8a1c403a34573f0e93e251fc9c715a8d7f52` | 1,800 s; frames=12826, crc_changes=12825, timeout/decoder/slave/LLI/guard=0; PASS |
| `final-candidate-com3.log` | `72a7d5208355040104c60b690f742dbf98d9a336236bb0ed2263e2a6dea66958` | final firmware boot, tmpfs, C6, three camera frames, FSMs, PPA pipeline and offscreen render; PASS within headless scope |
| `final-automount-render-com3.log` | `04bb1364536b8e571ba5bef4d0d2f59e5e0930ed8a065713c5e17c6e6dde46bc` | cold-boot tmpfs automount and 230415-byte PPM export; PASS |

The final transcript deliberately contains `simulation (NOT REAL INFERENCE)`
and `DISPLAY NOT VERIFIED`. These are acceptance boundaries, not failures to
be relabelled: no real pose model or physical display was tested in this run.
