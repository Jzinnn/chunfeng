# aboot framework (Melis / f136_m2_cxx)

Layout:
  aboot_core.*       — public API + glue
  aboot_smux.c/.h    — framing + HELLO handshake
  aboot_transport.c  — SMUX cmd/data + wait_response
  aboot_download.c   — ASR script image download (sync, no Contiki)
  aboot_preamble.c   — no-op (SMUX covers UABT)
  port_usbh_serial.c — Melis CherryUSB ttyUSB IO
  ../cmd_aboot.c     — msh: aboot / aboot connect

msh:
  aboot help
  aboot connect [/dev/ttyUSB0]
  aboot /mnt/D/xxx.bin [/dev/ttyUSB0]

Need CONFIG_CHERRYUSB_HOST + HOST_GSM and a real ASR aboot image package.
Image format: 8-char magic + 8-hex size, then cmd/response lines + binaries.
