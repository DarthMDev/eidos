/*
 * sdk/pxi.h -- the inter-processor FIFO and IPCSYNC, and the ARM7 behind
 * them.  docs/boot.md, "Three layers", puts the ARM7 in the third layer: a
 * high-level stand-in, because the ARM7's firmware is not part of the
 * matched ARM9 binary and cannot run on a host.
 *
 * Two halves:
 *
 *  - sdk/pxi.c models the hardware between the processors as the ARM9's
 *    register file sees it: IPCSYNC (0x04000180), IPCFIFOCNT (0x04000184),
 *    the send register (0x04000188) and the receive register (0x04100000),
 *    each FIFO 16 words deep (GBATEK, "DS Inter Process Communication").
 *    The matched ARM9 senders (PXI_SendWordByFifo and every wrapper over it)
 *    run as written: their store to 0x04000188 lands in the register file
 *    and the write hook installed here, or bw_pxi_service(), moves it into
 *    the ARM9-to-ARM7 FIFO.
 *
 *  - sdk/arm7/ holds one responder per PXI tag, answering with the words
 *    the real ARM7 firmware sends, so the ARM9 callbacks the matched code
 *    installed fire with the replies they expect.  sdk/arm7/arm7.h is their
 *    interface.
 *
 * Synchronisation.  The ARM7 runs when the port lets it, at the points the
 * DS's ARM9 would be waiting for it or would take an interrupt:
 *
 *    bw_arm7_step()  (sdk/arm7/arm7.h) = bw_pxi_service() + the ARM7's own
 *                    shared-memory handshakes.
 *
 * The CPU stand-ins are expected to call bw_arm7_step() from
 * OS_RestoreInterrupts and OS_EnableInterrupts (every PXI_SendWordByFifo
 * ends in OS_RestoreInterrupts, so each word is consumed before the next
 * store can overwrite it), from SVC_WaitByLoop and OS_SpinWaitSysCycles
 * (the ARM9's own "wait for the other processor"), and before interrupt
 * delivery.  A reply raises IF bit 18 through the interrupt sink below; the
 * interrupt stand-in then calls the IRQ table entry the matched PXI init
 * installed, which is PXIi_HandlerRecvFifoNotEmpty here.
 *
 * Every word on the wire is the SDK's PXIFifoMessage: tag in bits 0-4, the
 * error flag in bit 5, 26 data bits above (pxi_fifo.o's DWARF).
 */

#ifndef BW_PORT_PXI_H
#define BW_PORT_PXI_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Registers, as the ARM9 sees them (GBATEK).                              */
#define BW_REG_IPCSYNC      0x04000180u
#define BW_REG_IPCFIFOCNT   0x04000184u
#define BW_REG_IPCFIFOSEND  0x04000188u
#define BW_REG_IPCFIFORECV  0x04100000u
#define BW_REG_IF           0x04000214u

/* IPCFIFOCNT bits.                                                         */
#define BW_FIFOCNT_SEND_EMPTY   0x0001u
#define BW_FIFOCNT_SEND_FULL    0x0002u
#define BW_FIFOCNT_SEND_IRQ     0x0004u
#define BW_FIFOCNT_SEND_CLEAR   0x0008u
#define BW_FIFOCNT_RECV_EMPTY   0x0100u
#define BW_FIFOCNT_RECV_FULL    0x0200u
#define BW_FIFOCNT_RECV_IRQ     0x0400u
#define BW_FIFOCNT_ERROR        0x4000u
#define BW_FIFOCNT_ENABLE       0x8000u

/* IF bits the PXI raises.                                                  */
#define BW_IRQ_IPC_SYNC         0x00010000u
#define BW_IRQ_IPC_SEND_EMPTY   0x00020000u
#define BW_IRQ_IPC_RECV         0x00040000u

#define BW_PXI_FIFO_DEPTH   16

/*
 * PXI tags, from the matched ARM9 senders and callback installers:
 * RTC 5 (RTC_Init, RtcSendPxiCommand), SOUND 7 (snd_command.o's InitPXI),
 * PM 8 (PM_Init, PMi_SendPxiData), FS 11 (CARDi_Request, the backup
 * commands), OS 12 (OSi_SendToPxi, OS_Init), CTRDG 13 (CTRDG_DummyInit),
 * CARD 14 (CARD_InitPulledOutCallback).
 */
enum {
    BW_PXI_TAG_EX         = 0,
    BW_PXI_TAG_NVRAM      = 4,
    BW_PXI_TAG_RTC        = 5,
    BW_PXI_TAG_TOUCHPANEL = 6,
    BW_PXI_TAG_SOUND      = 7,
    BW_PXI_TAG_PM         = 8,
    BW_PXI_TAG_MIC        = 9,
    BW_PXI_TAG_WM         = 10,
    BW_PXI_TAG_FS         = 11,
    BW_PXI_TAG_OS         = 12,
    BW_PXI_TAG_CTRDG      = 13,
    BW_PXI_TAG_CARD       = 14,
    BW_PXI_TAGS           = 32
};

/* The PXIFifoMessage packing.                                              */
#define BW_PXI_WORD(tag, data, err) \
    (((u32)(tag) & 0x1Fu) | (((u32)(err) & 1u) << 5) | ((u32)(data) << 6))
#define BW_PXI_TAG_OF(w)   ((int)((w) & 0x1Fu))
#define BW_PXI_ERR_OF(w)   ((int)(((w) >> 5) & 1u))
#define BW_PXI_DATA_OF(w)  ((u32)((w) >> 6))

/*
 * The OS system work's pxiHandleChecker[2] (HW_MAIN_MEM_SYSTEM 0x02FFFC00 +
 * 0x388): bit `tag` of [proc] says that processor has a callback for the
 * tag.  PXI_IsCallbackReady(tag, 1) reads the ARM7's word.
 */
#define BW_PXI_HANDLE_CHECKER   0x02FFFF88u

/* ------------------------------------------------------------------ */
/* Life cycle                                                          */
/* ------------------------------------------------------------------ */

/*
 * Power-on state of the PXI: both FIFOs empty, IPCSYNC zero, the register
 * file's PXI registers at their reset values, the hooks installed, the
 * ARM7 powered off (sdk/arm7/arm7.h).  Call after bw_ds_reset(), which may
 * drop the register hooks this installs.
 */
void bw_pxi_reset(void);

/*
 * Whether bw_pxi_reset has run.  Until it has, bw_pxi_service and
 * bw_arm7_step do nothing, so a unit test that never touches PXI can call
 * the CPU stand-ins freely; the PXI_InitFifo shim resets the PXI itself if
 * the port has not.
 */
int bw_pxi_powered(void);

/*
 * Let the hardware and the ARM7's PXI layer catch up: take any word the
 * ARM9 has stored into the send register since the last call, deliver
 * everything in the ARM9-to-ARM7 FIFO to the ARM7's callbacks (which may
 * answer), and advance the IPCSYNC start-up handshake.  Cheap when idle;
 * safe to call from anywhere, including from inside a callback (a nested
 * call returns at once and the outer one picks the work up).
 */
void bw_pxi_service(void);

/*
 * Where an interrupt request goes.  The default ORs the bit into IF in the
 * register file, which is what the hardware does; the interrupt stand-in
 * (sdk/os_irq.c) replaces it with its own "raise" so a request is not
 * mistaken for a write-one-to-clear acknowledgement at the next
 * bw_io_sync().  The sink must only record the request: delivery happens
 * at the stand-in's own delivery points, never from inside this call.
 */
typedef void (*BwPxiIrqSink)(u32 if_mask);
void bw_pxi_set_irq_sink(BwPxiIrqSink sink);

/* ------------------------------------------------------------------ */
/* The ARM9's side, for shims                                          */
/* ------------------------------------------------------------------ */

/* Pop one word the ARM7 sent.  0 on success, -1 when the FIFO is empty.   */
int bw_pxi_arm9_recv(u32 *word);

/*
 * Push one word towards the ARM7, as a store to the send register does.
 * 0 on success, -2 when the send FIFO is full, -1 when the FIFO is disabled
 * (PXI_SendWordByFifo's own return codes).
 */
int bw_pxi_arm9_send(u32 word);

/* Words waiting for the ARM9 (tests and diagnostics).                      */
int bw_pxi_arm9_pending(void);

/* ------------------------------------------------------------------ */
/* The ARM7's side, for sdk/arm7/                                      */
/* ------------------------------------------------------------------ */

typedef void (*BwPxiArm7Callback)(int tag, u32 data, int err);

/*
 * The ARM7's PXI_SetFifoRecvCallback: install (or with NULL remove) the
 * responder for `tag` and publish it in pxiHandleChecker[1], which is what
 * the ARM9's PXI_IsCallbackReady(tag, 1) polls.
 */
void bw_pxi_arm7_set_callback(int tag, BwPxiArm7Callback cb);

/*
 * Send one word to the ARM9.  When the 16-word hardware FIFO is full the
 * word waits in the ARM7's own queue (the firmware retries until there is
 * room) and moves in as the ARM9 drains.  0 on success, -1 when even that
 * queue is full, which is a responder bug.
 */
int bw_pxi_arm7_send(int tag, u32 data, int err);

/* Write pxiHandleChecker[1] again from the ARM7's callback table.         */
void bw_pxi_arm7_publish(void);

/* The ARM7's IPCSYNC output nibble and its view of the ARM9's.            */
u32 bw_pxi_arm7_sync_out(void);
u32 bw_pxi_arm7_sync_in(void);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_PXI_H */
