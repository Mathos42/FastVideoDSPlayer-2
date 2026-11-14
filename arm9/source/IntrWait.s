/*---------------------------------------------------------------------------------

  Copyright (C) 2005 - 2016
    Dave Murphy (WinterMute)

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any
  damages arising from the use of this software.

  Permission is granted to anyone to use this software for any
  purpose, including commercial applications, and to alter it and
  redistribute it freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you
     must not claim that you wrote the original software. If you use
     this software in a product, an acknowledgment in the product
     documentation would be appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and
     must not be misrepresented as being the original software.
  3. This notice may not be removed or altered from any source
     distribution.

---------------------------------------------------------------------------------*/
/*
 * Legacy libnds 1.8 ARM9 interrupt-wait compatibility fix.
 * Source: devkitPro/libnds, revision 04ff07cd2ac0f3f0c13adc7d54fd2c5b850341d8,
 * source/arm9/system/IntrWait.s. The original notice is retained above.
 *
 * Local modification: restore the MMIO base at flag_set. When discard is zero
 * and an IRQ flag is already set, the early return otherwise writes IME through
 * r12 == 0. The player uses this path in swiIntrWait(0, IRQ_VBLANK).
 *
 * Keep the installed SDK untouched. This object supplies both entry points
 * from the legacy library object; newer SDKs keep their own implementation.
 * libnds 1.8.3 also reports 1.8.0 in libversion.h, so match major/minor only.
 */
#include <nds/libversion.h>

#if _LIBNDS_MAJOR_ == 1 && _LIBNDS_MINOR_ == 8
#include <nds/asminc.h>

	.arch	armv5te
	.cpu	arm946e-s

	.text
	.arm


//---------------------------------------------------------------------------------
BEGIN_ASM_FUNC swiWaitForVBlank
//---------------------------------------------------------------------------------
	mov	r0, #1
	mov	r1, #1

//---------------------------------------------------------------------------------
BEGIN_ASM_FUNC swiIntrWait
//---------------------------------------------------------------------------------
	push	{lr}
	mov	r12, #0x4000000
	str	r12, [r12, #0x208]

	mrc	p15, 0, r3, c9, c1, 0
	mov	r3, r3, lsr#12
	mov	r3, r3, lsl#12
	add	r3, r3, #0x4000

	mov 	r12, r0
	bl 	check_flags
	beq	wait

	cmp	r12, #0
	beq	flag_set
wait:
	mov	r12, #0x4000000
wait_flags:
	mov	r2, #1
	str  	r2, [r12,#0x208]

	mov 	r0, #0
	mcr 	p15, 0, r0, c7, c0, 4

	str	r12, [r12, #0x208]
	bl 	check_flags
	beq	wait_flags

flag_set:
	@ The ready-flag path still holds the discard argument in r12.
	@ Reload the IO base before enabling interrupts on either return path.
	mov	r12, #0x04000000
	mov	r2, #1
	str  	r2, [r12,#0x208]
	pop	{pc}

//---------------------------------------------------------------------------------
check_flags:
//---------------------------------------------------------------------------------
	ldr	r2, [r3, #-8]
	ands	r0, r1, r2
	eorne	r2, r2, r0
	strne	r2, [r3, #-8]
	bx	lr

#endif /* libnds 1.8 */
