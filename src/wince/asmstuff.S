@
@   Linux loader for Windows CE
@   Copyright (C) 2003 Andrew Zabolotny
@
@   For conditions of use see file COPYING
@

		.macro	CPWAIT reg
		mrc	p15, 0, \reg, c2, c0, 0
		mov	\reg, \reg
		sub 	pc, pc, #4
		.endm

		.bss
		.space  32
junkdata:	.space 0x10000		@ Spare space for data cache flush

		.text

@ Flush CPU caches - this MUST be called between
@ take_control()/return_control()
@
@ Dirties r0, r1, r2
		.global	cpuFlushCache
cpuFlushCache:
		ldr	r0, =junkdata
		add	r1, r0, #0x10000
	1:	ldr	r2, [r0], #32
		teq	r0, r1
		bne	1b

		mov	r0, #0
		mcr	p15, 0, r0, c7, c10, 4	@ Drain write buffer
		mcr	p15, 0, r0, c8, c7, 0	@ Invalidate I+D TLB
		mcr	p15, 0, r0, c7, c7, 0	@ Invalidate I+D caches & BTB
		CPWAIT  r0

		mov	pc, lr

@ Flush CPU caches on xscale - this MUST be called between
@ take_control()/return_control()
@
@ Dirties r0, r1
		.global	cpuFlushCache_xscale
cpuFlushCache_xscale:
@ Flush data cache
		ldr	r1, =junkdata
		mov	r0, #1024
	1:	mcr	p15, 0, r1, c7, c2, 5	@ Allocate cache line
		add	r1, r1, #32
		subs	r0, r0, #1
		bne	1b

		mov	r0, #0
		mcr	p15, 0, r0, c7, c10, 4	@ Drain write buffer
		mcr	p15, 0, r0, c8, c7, 0	@ Invalidate I+D TLB
		mcr	p15, 0, r0, c7, c7, 0	@ Invalidate I+D caches & BTB
		CPWAIT  r0

		mov	pc, lr

@ Flush CPU caches on arm920 - this MUST be called between
@ take_control()/return_control()
@
@ Dirties r0, r1, r2
		.global	cpuFlushCache_arm920
cpuFlushCache_arm920:
		mov	r0, #0
		mov	r1, #(8-1) << 5			@ 8 segments
1:		orr	r2, r1,#(64-1)<<26		@ 64 entries
2:		mcr	p15, 0, r2, c7, c14, 2	@ clean+invalidate D index
		subs	r2, r2, #1<<26			@
		bcs	2b				@
		subs	r1, r1, #1<<5			@
		bcs	1b				@

		mcr	p15, 0, r0, c7, c5, 0		@ invalidate I cache
		mcr	p15, 0, r0, c7, c10, 4		@ drain WB
		mcr	p15, 0, r0, c8, c7, 0		@ drain I+D TLBs
		mov	pc, r14

@ Flush CPU caches on arm925 - this MUST be called between
@ take_control()/return_control()
@
@ Dirties r0
		.global cpuFlushCache_arm925
cpuFlushCache_arm925:
		mov	r0, #(256 - 1) << 4		@ 256 entries in segment
1:		mrc	p15, 0, r0, c7, c14, 2		@ test,clean,invalidate
		subs	r0, r0, #1 << 4
		bcs	1b

		mov	r0, #0
		mcr	p15, 0, r0, c7, c5, 0		@ invalidate I cache
		mcr	p15, 0, r0, c7, c10, 4		@ drain WB
		mcr	p15, 0, r0, c8, c7, 0		@ drain I+D TLBs
		mov	pc, r14

@ Flush CPU caches on arm926 - this MUST be called between
@ take_control()/return_control()
@
@ Dirties r0
		.global cpuFlushCache_arm926
cpuFlushCache_arm926:
1:		mrc	p15, 0, r15, c7, c14, 3		@ test,clean,invalidate
		bne	1b

		mov	r0, #0
		mcr	p15, 0, r0, c7, c5, 0		@ invalidate I cache
		mcr	p15, 0, r0, c7, c10, 4		@ drain WB
		mcr	p15, 0, r0, c8, c7, 0		@ drain I+D TLBs
		mov	pc, r14

@ Flush CPU caches on arm v6
@
@ Dirties r0
		.global	cpuFlushCache_arm6
cpuFlushCache_arm6:
@ The following part is the original codes.
@ It seems to be not working.
        @ test
        @ mov r0, #0
        @ mov pc, r0
        @ end test


#if 0
		mov	r0, #0
		mcr	p15, 0, r0, c7, c14, 0		@ D cache clean+invalidate
		mcr	p15, 0, r0, c7, c5, 0		@ I+BTB cache invalidate
		mcr	p15, 0, r0, c8, c7, 0		@ drain I+D TLBs
#endif

@ The following part is from kernel wildfire arch/arm/boot/compressed/head.S
@ It seems to be working. But kernel hangs at tags relocation.
#if 1
		mov	r0, #0
		mcr	p15, 0, r0, c7, c14, 0	@ clean+invalidate D
		mcr	p15, 0, r0, c7, c5, 0	@ invalidate I+BTB
		mcr	p15, 0, r0, c7, c15, 0	@ clean+invalidate unified
#endif

@ The following part is from HD mini
@ Tested. Not working.
#if 0
                MOV     R0, #0
                MOV     R1, #0xE0

tlabel5:
                ORR     R2, R1, #0xFC000000

tlabel6:
                MCR     p15, 0, R2,c7,c14, 2
                SUBS    R2, R2, #0x4000000
                BCS     tlabel6
                SUBS    R1, R1, #0x20
                BCS     tlabel5
                MCR     p15, 0, R0,c7,c5, 0
                MCR     p15, 0, R0,c7,c10, 4
                MCR     p15, 0, R0,c8,c7, 0
                MOV     PC, LR
#endif

		@mov	pc, r14

@ Assembler stub that (when relocated) can store the location of a
@ stack and C executable function to jump to.
        .section .text.preload
        .global stackJumper
stackJumper:
stack:          .long 0
data:           .long 0
execCode:       .long 0
returnCode:     .long 0
asm_handler:
        ldr     r0, [pc, #(data - . - 8)]
        ldr     sp, [pc, #(stack - . - 8)]
        ldr     lr, [pc, #(returnCode - . - 8)]
        ldr     pc, [pc, #(execCode - . - 8)]

#if 1
@
@ Cotulla: this code only for HTC PHOTON (HTC HD MINI)
@
        .section .text.trampoline
        .global mmu_trampoline, mmu_trampoline_end
mmu_trampoline:
	LDR	R6, =mmu_trampoline2
    @ start addr of mmu_trampoline2
    @LDR	R7, =0xACCFF000
    LDR     R7, =0xc3ffc000
	MOV	R8, R7
	MOV 	R4, #(mmu_trampoline2_end - mmu_trampoline2)
1:
	LDRB	R5, [R6], #1
	STRB	R5, [R7], #1
	SUBS	R4, R4, #1		
	BNE	1b
	@LDR	R0, =0x0FFFF000
    LDR     R0, =0x0fff0000
	MOV	PC, R8
	.ltorg
mmu_trampoline_end:
#endif


@ Code that can disable the MMU and jump to a function.
@ In:   r0 = Physical address of this function
@       r1 = Virtual address of MMU in non-cached ram.
@       r2 = Physical address of code to jump to
@       r3 = cpu flush cache function
        .section .text.trampoline
        .global mmu_trampoline2, mmu_trampoline2_end
mmu_trampoline2:
	@ Store previous registers on stack.
        stmdb   sp, {r4-r14}

        @ Write back all cpu caches
        mov     r4, r0
        mov     r5, r1
        mov     r6, r2
        @mov     lr, pc
        @mov     pc, r3

#if 1
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

                MOV     R0, #0
                MOV     R1, #0xE0

tlabel5:
                ORR     R2, R1, #0xFC000000

tlabel6:
                MCR     p15, 0, R2,c7,c14, 2
                SUBS    R2, R2, #0x4000000
                BCS     tlabel6
                SUBS    R1, R1, #0x20
                BCS     tlabel5
                MCR     p15, 0, R0,c7,c5, 0
                MCR     p15, 0, R0,c7,c10, 4
                MCR     p15, 0, R0,c8,c7, 0
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
#endif


        @ Turn off mapping for this code
        mov     r1, r4, lsr #20
        mov     r0, r1, asl #20         @ r0 = funcaddr & 0xfff00000
        orr     r0, r0, #0x00000C00
        orr     r0, r0, #0x00000002     @ r0 |= 0xC02
@@@@@@@@@@@@@@@ The above 4 lines try to "turn off mapping" for this function @@@@@@
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
                                        @ r5 == virtual address of MMU in non-cache ram


#if 1
        str     r0, [r5, r1, asl #2]    @ mmu[funcaddr>>20] = r0
#else
#if 0
        @ Patch by Radim
        @ http://lists.linuxtogo.org/pipermail/haret-devel/2010-February/000013.html
        mov     r1, r4
        mrc     p15, 0, r7, c13, c0, 0 @ read FCSE PID to r7
        and     r7, r7, #0xFE000000    @ get [31:25] PID bits
        orr     r1, r1, r7
        mov     r1, r1, lsr #20
#else
	mov	r8, r4
        mov     r1, r4
        mrc     p15, 0, r7, c13, c0, 0
        and     r7, r7, #0xFE000000
        orr     r1, r1, r7
        mov     r1, r1, lsr #20
        str     r0, [r5, r1, asl #2]    @ mmu[funcaddr>>20] = r0

	mov     r1, r8, lsr #20
        str     r0, [r5, r1, asl #2]    @ mmu[funcaddr>>20] = r0
	NOP
	NOP
	NOP
	NOP
	NOP
#endif
#endif
       


        @ Force MMU update and invalidate I cache
        mov	r0, #0
        mcr	p15, 0, r0, c7, c10, 4	@ Drain write buffer
        mcr     p15, 0, r0, c8, c5, 0   @ flush instruction TLB
        mcr     p15, 0, r0, c8, c6, 0   @ flush data TLB
        mcr	p15, 0, r0, c8, c7, 0	@ Invalidate I+D TLB
        mcr	p15, 0, r0, c7, c5, 0	@ Invalidate I cache & BTB
        CPWAIT  r0

        @ Jump to code with shared virtual/physical mapping
        add     pc, r4, #(1f - mmu_trampoline2)
1:      @ Code is running at a vm address that is the same as its
        @ physical address.  Now disable the MMU.

	NOP
	NOP

        @ meidh test
        @mov     pc, #0

Loop1:
        MOV R1, #0
        MCR P15, 0, R1, C7, C14, 0    @ Clean (or Clean & Invalidate) Cache
        MRS R2, CPSR
        CPSID iaf                      @ Disable interrupts
        MRC P15, 0, R1, C7, C10, 6    @ Read Cache Dirty Status Register
        ANDS R1, R1, #01               @ Check if it is clean
        BEQ UseClean
        MSR CPSR, R2                   @ Re-enable interrupts
        B Loop1                        @ - clean the cache again
UseClean:
        mrc p15, 0, r0, c1, c0, 0
        and r0, r0, #0xFFFFFFFB

        mcr	p15, 0, r0, c1, c0, 0	@ disable the MMU step 1 clear bit 2
        CPWAIT  r0

        mrc p15, 0, r0, c1, c0, 0
        and r0, r0, #0xFFFFFFFE

        mcr	p15, 0, r0, c1, c0, 0	@ disable the MMU step 2 clear bit 0
        CPWAIT  r0

        @mov	r0, #0
        @mcr	p15, 0, r0, c13, c0, 0	@ clear PID
        @mcr	p15, 0, r0, c8, c7, 0	@ invalidate I+D TLB
        @mcr	p15, 0, r0, c7, c5, 0	@ Invalidate I cache & BTB
        @CPWAIT  r0

        CPSID iaf @ disable interrupts
        @ MMU is now disabled.  Call function.
        mov     pc, r6

	.ltorg
mmu_trampoline2_end:



#if 0
@ Code that can disable the MMU and jump to a function.
@ In:   r0 = Physical address of this function
@       r1 = Virtual address of MMU in non-cached ram.
@       r2 = Physical address of code to jump to
@       r3 = cpu flush cache function
        .section .text.trampoline
        .global mmu_trampoline, mmu_trampoline_end
mmu_trampoline:
	@ Store previous registers on stack.
        stmdb   sp, {r4-r14}

        @ Write back all cpu caches
        mov     r4, r0
        mov     r5, r1
        mov     r6, r2
        mov     lr, pc
        mov     pc, r3

        @ Turn off mapping for this code
        mov     r1, r4, lsr #20
        mov     r0, r1, asl #20         @ r0 = funcaddr & 0xfff00000
        orr     r0, r0, #0x00000C00
        orr     r0, r0, #0x00000002     @ r0 |= 0xC02
        
        @ Patch by Radim
        @ http://lists.linuxtogo.org/pipermail/haret-devel/2010-February/000013.html
        mov     r1, r4
        mrc     p15, 0, r7, c13, c0, 0
        and     r7, r7, #0xFE000000
        orr     r1, r1, r7
        mov     r1, r1, lsr #20
        
        str     r0, [r5, r1, asl #2]    @ mmu[funcaddr>>20] = r0

        @ Force MMU update and invalidate I cache
        mov	r0, #0
        mcr p15, 0, r0, c7, c10, 5  @ drain DMB
        mcr	p15, 0, r0, c7, c10, 4	@ Data Synchronization Barrier/Drain write buffer
        mcr	p15, 0, r0, c8, c7, 0	@ Invalidate I+D TLB
        mcr	p15, 0, r0, c7, c5, 0	@ Invalidate I cache & BTB
        CPWAIT  r0

        @ Jump to code with shared virtual/physical mapping
        add     pc, r4, #(1f - mmu_trampoline)
        nop
        nop
        nop
        nop


1:      @ Code is running at a vm address that is the same as its
        @ physical address.  Now disable the MMU.
#if 0
        mrc	p15, 0, r0, c1, c0, 0
        bic	r0, r0, #5		@ MMU & Dcache off
        bic	r0, r0, #0x1000         @ Icache off
        mcr	p15, 0, r0, c1, c0, 0	@ disable the MMU & caches
        CPWAIT  r0

        mov	r0, #0
        mcr	p15, 0, r0, c13, c0, 0	@ clear PID
        mcr	p15, 0, r0, c8, c7, 0	@ invalidate I+D TLB
        mcr	p15, 0, r0, c7, c5, 0	@ Invalidate I cache & BTB
        CPWAIT  r0
#endif

@
@ Micdav test
@
        
		mrc	p15, 0, r0, c1, c0
		bic	r0, r0, #0x000d
		mcr	p15, 0, r0, c1, c0	@ turn MMU and cache off

		mov	r0, #0
		mcr	p15, 0, r0, c7, c7	@ invalidate whole cache v4
		mcr	p15, 0, r0, c8, c7	@ invalidate whole TLB v4

        @ MMU is now disabled.  Call function.
        mov     pc, r6

mmu_trampoline_end:
#endif
        .end
