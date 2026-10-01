/*
 * sdk/cp.h -- the ARM9's divider and square-root unit (GBATEK, "DS Maths").
 *
 * The matched SDK drives this unit with inline register code, not calls:
 * CP_SetDiv32_32 stores DIVCNT, DIV_NUMER and DIV_DENOM, CP_WaitDiv spins on
 * DIVCNT's busy bit, CP_GetDivResult32 loads DIV_RESULT (tp.o's calibration,
 * fx.o's divisions).  After tools/dsaddr.py each of those is a plain store to
 * or load from the register file, so on its own the unit never computes:
 * the result register reads whatever was last stored there.  sdk/cp.c
 * installs an access hook (sdk/dsmem.h, bw_io_access_hook) over the unit's
 * window, and every translation of a result or control address computes the
 * result from the operands as they stand, before the load lands.  The busy
 * bit is never set: the answer is ready by the time anything can look.
 */

#ifndef BW_PORT_CP_H
#define BW_PORT_CP_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BW_REG_DIVCNT       0x04000280u
#define BW_REG_DIV_NUMER    0x04000290u
#define BW_REG_DIV_DENOM    0x04000298u
#define BW_REG_DIV_RESULT   0x040002A0u
#define BW_REG_DIV_REM      0x040002A8u
#define BW_REG_SQRTCNT      0x040002B0u
#define BW_REG_SQRT_RESULT  0x040002B4u
#define BW_REG_SQRT_PARAM   0x040002B8u

/*
 * Install the unit's access hook.  bw_ds_reset drops every access hook, so
 * this runs after it; bw_port_reset (sdk/port.h) calls it in order.
 */
void bw_cp_reset(void);

/* Compute both results from the registers now (tests, CPi_RestoreContext). */
void bw_cp_update(void);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_CP_H */
