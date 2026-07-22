#include "aboot_internal.h"

/*
 * Raw PC preamble (UUUU/UABT + 0x7E) lived in aboot-tiny preamble.c.
 * Melis SMUX path uses framed UABT inside aboot_smux_handshake() instead.
 * Kept as no-op so older call sites still link.
 */
int aboot_preamble_start(void)
{
	aboot_notify_log("preamble: skipped (use smux handshake)");
	return 0;
}
