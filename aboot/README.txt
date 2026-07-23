# aboot framework (Melis / f136_m2_cxx)

Layout:
  aboot_core.*       — transport glue + notify + dedicated callbacks
  aboot_api.*        — high-level API (CLI equivalents for app/UI)
  aboot_smux.c/.h    — framing + HELLO handshake
  aboot_transport.c  — SMUX cmd/data + wait_response
  aboot_download.c   — ASR script image download (sync, no Contiki)
  aboot_preamble.c   — no-op (SMUX covers UABT)
  port_usbh_serial.c — Melis CherryUSB ttyUSB IO
  ../cmd_aboot.c     — thin msh wrapper over aboot_api

Callbacks (aboot_cbs_t / setters in aboot_core.h):
  on_status(status, error, ctx)     — CONNECTING / PREBOOT / SUCCEEDED / FAILED / ...
  on_progress(src, percent, ctx)    — script or device 0..100
  on_log(message, ctx)              — optional text log

API (aboot_api.h):
  aboot_api_init(io, &cbs)
  aboot_api_connect(dev)            — handshake probe
  aboot_api_download(img, dev, v) — connect + flash + disconnect
  aboot_api_at(dev, cmd, timeout_ms)
  aboot_api_deinit()

msh:
  aboot help
  aboot connect [/dev/ttyUSB0]
  aboot /mnt/D/xxx.bin [/dev/ttyUSB0]

Need CONFIG_CHERRYUSB_HOST + HOST_GSM and a real ASR aboot image package.
Image format: 8-char magic + 8-hex size, then cmd/response lines + binaries.
