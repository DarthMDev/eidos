/*
 * function: func_02157138
 * address:  0x02157138
 * region:   ov009
 * isa:      thumb
 *
 * PORT STAND-IN for an overlay function aletheia has not matched yet
 * (tools/ovlink.py compiles stubs/ov/ovNNN_*.c as units of overlay NNN, and
 * leaves one out, reported as obsolete, once a matched file defines its
 * function).  Written from aletheia's asm/overlays/ov009.s:
 *
 *     02157138  push  {r4, lr}
 *     0215713A  adds  r4, r0, #0
 *     0215713C  movs  r0, #0
 *     0215713E  adds  r1, r4, #0
 *     02157140  movs  r2, #0x40
 *     02157142  blx   0x02082AC0          MIi_CpuClearFast
 *     02157146  adds  r0, r4, #0
 *     02157148  blx   0x02059718          DWC_CheckUserData
 *     0215714C  cmp   r0, #0
 *     0215714E  bne   0x0215715C
 *     02157150  adds  r0, r4, #0
 *     02157152  blx   0x020596FC          DWC_CreateUserData
 *     02157156  adds  r0, r4, #0
 *     02157158  blx   0x02059870          veneer to DWCi_Acc_ClearDirty
 *     0215715C  pop   {r4, pc}
 *
 * The callee names are layout/symbols_sdk.txt's.  The game calls it from
 * sub_02009800 (the save block that holds the DWC user data), with ov009
 * swapped in for the call: clear the 0x40-byte block, and when it does not
 * hold valid user data (its CRC does not check), create fresh user data
 * and clear its dirty flag.
 */

typedef unsigned int u32;

extern void MIi_CpuClearFast(u32 data, void *dest, u32 size);
extern int  DWC_CheckUserData(void *userdata);
extern void DWC_CreateUserData(void *userdata);
extern void DWCi_Acc_ClearDirty(void *userdata);

void func_02157138(void *userdata)
{
    MIi_CpuClearFast(0, userdata, 0x40);
    if (DWC_CheckUserData(userdata) == 0) {
        DWC_CreateUserData(userdata);
        DWCi_Acc_ClearDirty(userdata);
    }
}
