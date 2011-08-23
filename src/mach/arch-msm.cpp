// Support for Qualcomm MSMxxxx cpus.
#include "script.h" // runMemScript
#include "arch-arm.h" // cpuFlushCache_arm6
#include "arch-msm.h"
#include "memory.h" // memPhysMap
#include "linboot.h" // __preload
#include "video.h" // vidGetVRAM
#include "fbwrite.h" // fb_putc / struct fbinfo
#include "cpu.h" // DEF_GETCPRATTR

DEF_GETCPRATTR(getMMUReg, p15, 0, c1, c0, 0, __preload,)

static void
defineMsmGpios()
{
    runMemScript(
        // out registers
        "addlist gpios p2v(0xa9200800)\n"
        "addlist gpios p2v(0xa9300c00)\n"
        "addlist gpios p2v(0xa9200804)\n"
        "addlist gpios p2v(0xa9200808)\n"
        "addlist gpios p2v(0xa920080c)\n"
        "addlist gpios p2v(0xa9200850)\n"
        // in registers
        "addlist gpios p2v(0xa9200834)\n"
        "addlist gpios p2v(0xa9300c20)\n"
        "addlist gpios p2v(0xa9200838)\n"
        "addlist gpios p2v(0xa920083c)\n"
        "addlist gpios p2v(0xa9200840)\n"
        "addlist gpios p2v(0xa9200844)\n"
        // out enable registers
        "addlist gpios p2v(0xa9200810)\n"
        "addlist gpios p2v(0xa9300c08)\n"
        "addlist gpios p2v(0xa9200814)\n"
        "addlist gpios p2v(0xa9200818)\n"
        "addlist gpios p2v(0xa920081c)\n"
        "addlist gpios p2v(0xa9200854)\n"
        );
}

static void
defineQsdGpios()
{
    // QSD8xxx has eight GPIO banks (0-7)
    // GPIO1 base: 0xA9000000 + 0x800
    // GPIO2 base: 0xA9100000 + 0xc00
    runMemScript(
        // output registers
        "addlist gpios p2v(0xa9000800)\n"
        "addlist gpios p2v(0xa9100c00)\n"
        "addlist gpios p2v(0xa9000804)\n"
        "addlist gpios p2v(0xa9000808)\n"
        "addlist gpios p2v(0xa900080c)\n"
        "addlist gpios p2v(0xa9000810)\n"
        "addlist gpios p2v(0xa9000814)\n"
        "addlist gpios p2v(0xa9000818)\n"
        // input read registers
        "addlist gpios p2v(0xa9000850)\n"
        "addlist gpios p2v(0xa9100c20)\n"
        "addlist gpios p2v(0xa9000854)\n"
        "addlist gpios p2v(0xa9000858)\n"
        "addlist gpios p2v(0xa900085c)\n"
        "addlist gpios p2v(0xa9000860)\n"
        "addlist gpios p2v(0xa9000864)\n"
        "addlist gpios p2v(0xa900086c)\n"
        // output enable registers
        "addlist gpios p2v(0xa9000820)\n"
        "addlist gpios p2v(0xa9100c08)\n"
        "addlist gpios p2v(0xa9000824)\n"
        "addlist gpios p2v(0xa9000828)\n"
        "addlist gpios p2v(0xa900082c)\n"
        "addlist gpios p2v(0xa9000830)\n"
        "addlist gpios p2v(0xa9000834)\n"
        "addlist gpios p2v(0xa9000838)\n"
        );
}


/****************************************************************
 * MSM 7xxxA
 ****************************************************************/

MachineMSM7xxxA::MachineMSM7xxxA()
{
    name = "Generic MSM7xxxA";
    flushCache = cpuFlushCache_arm6;
    arm6mmu = 1;
    archname = "MSM7xxxA";
    CPUInfo[0] = L"MSM7201A";
}

void
MachineMSM7xxxA::init()
{
    runMemScript(
        "set ramaddr 0x10000000\n"
        "addlist irqs p2v(0xc0000080) 0x100 32 0\n"
        "addlist irqs p2v(0xc0000084) 0 32 0\n"
        );
    defineMsmGpios();
}

REGMACHINE(MachineMSM7xxxA)


/****************************************************************
 * MSM 7xxx
 ****************************************************************/

MachineMSM7xxx::MachineMSM7xxx()
{
    name = "Generic MSM7xxx";
    flushCache = cpuFlushCache_arm6;
    arm6mmu = 1;
    archname = "MSM7xxx";
    CPUInfo[0] = L"MSM7500";
    CPUInfo[1] = L"MSM7200";
}

void
MachineMSM7xxx::init()
{
    runMemScript(
        "set ramaddr 0x10000000\n"
        "addlist irqs p2v(0xc0000000) 0x100 32 0\n"
        "addlist irqs p2v(0xc0000004) 0 32 0\n"
        );
    defineMsmGpios();
}

REGMACHINE(MachineMSM7xxx)

/****************************************************************
 * QSD 8xxx
 ****************************************************************/

MachineQSD8xxx::MachineQSD8xxx()
{
    name = "Generic QSD8xxx";
    flushCache = cpuFlushCache_arm7;
    arm6mmu = 1;
    archname = "QSD8xxx";
    CPUInfo[0] = L"QSD8250B"; // First seen on HTC Leo
    CPUInfo[1] = L"QSD8250"; // First seen on Acer S200 (F1)
    customStartFunc = bootQSD8xxx;
}

void
MachineQSD8xxx::init()
{
    runMemScript(
        "set ramaddr 0x18800000\n"
        "addlist irqs p2v(0xac000080) 0x100 32 0\n" // 0x100 masks out the 9th interrupt (DEBUG_TIMER_EXP)
        "addlist irqs p2v(0xac000084) 0 32 0\n"
    );
    defineQsdGpios();
}

void
MachineQSD8xxx::shutdownInterrupts()
{
    uint32 dummyRead;

    // Map the interrupt controller registers
    uint32 volatile *VIC_INT_SELECT0		= (uint32*)memPhysMap(0xAC000000+0x0000);
    uint32 volatile *VIC_INT_SELECT1		= (uint32*)memPhysMap(0xAC000000+0x0004);
    uint32 volatile *VIC_INT_EN0		= (uint32*)memPhysMap(0xAC000000+0x0010);
    uint32 volatile *VIC_INT_EN1		= (uint32*)memPhysMap(0xAC000000+0x0014);
    uint32 volatile *VIC_INT_TYPE0		= (uint32*)memPhysMap(0xAC000000+0x0040);
    uint32 volatile *VIC_INT_TYPE1		= (uint32*)memPhysMap(0xAC000000+0x0044);
    uint32 volatile *VIC_INT_POLARITY0		= (uint32*)memPhysMap(0xAC000000+0x0050);
    uint32 volatile *VIC_INT_POLARITY1		= (uint32*)memPhysMap(0xAC000000+0x0054);
    uint32 volatile *VIC_CONFIG			= (uint32*)memPhysMap(0xAC000000+0x006C);
    uint32 volatile *VIC_INT_MASTEREN		= (uint32*)memPhysMap(0xAC000000+0x0068);
    uint32 volatile *VIC_IRQ_VEC_RD		= (uint32*)memPhysMap(0xAC000000+0x00D0);
    uint32 volatile *VIC_IRQ_VEC_PEND_RD	= (uint32*)memPhysMap(0xAC000000+0x00D4);
    uint32 volatile *VIC_IRQ_VEC_WR		= (uint32*)memPhysMap(0xAC000000+0x00D8);

    // Disable the interrupt controller
    *VIC_INT_MASTEREN = 0;
    *VIC_CONFIG = 0;
    *VIC_INT_EN0 = 0;
    *VIC_INT_EN1 = 0;
    *VIC_INT_SELECT0 = 0;
    *VIC_INT_SELECT1 = 0;
    *VIC_INT_POLARITY0 = 0;
    *VIC_INT_POLARITY1 = 0;
    *VIC_INT_TYPE0 = 0;
    *VIC_INT_TYPE1 = 0;

    // Reset the interrupt priority register
    // (Must read the existing values first for some reason)
    dummyRead = *VIC_IRQ_VEC_RD;
    dummyRead = *VIC_IRQ_VEC_PEND_RD;
    *VIC_IRQ_VEC_WR = ~0;
}

void
MachineQSD8xxx::shutdownSirc()
{
    // Shut down the second interrupt controller
    uint32 volatile *SIRC_INT_SELECT        = (uint32*)memPhysMap(0xAC200000+0x00);
    uint32 volatile *SIRC_INT_ENABLE        = (uint32*)memPhysMap(0xAC200000+0x04);
    uint32 volatile *SIRC_INT_ENABLE_CLEAR  = (uint32*)memPhysMap(0xAC200000+0x08);
    uint32 volatile *SIRC_INT_ENABLE_SET    = (uint32*)memPhysMap(0xAC200000+0x0C);
    uint32 volatile *SIRC_INT_TYPE          = (uint32*)memPhysMap(0xAC200000+0x10);
    uint32 volatile *SIRC_INT_POLARITY      = (uint32*)memPhysMap(0xAC200000+0x14);
    uint32 volatile *SIRC_INT_IRQ_STATUS    = (uint32*)memPhysMap(0xAC200000+0x1C);
    uint32 volatile *SIRC_INT_IRQ1_STATUS   = (uint32*)memPhysMap(0xAC200000+0x20);
    uint32 volatile *SIRC_INT_RAW_STATUS    = (uint32*)memPhysMap(0xAC200000+0x24);
    uint32 volatile *SIRC_INT_CLEAR         = (uint32*)memPhysMap(0xAC200000+0x28);

    *SIRC_INT_ENABLE = 0;
    *SIRC_INT_ENABLE_CLEAR = 0;
    *SIRC_INT_ENABLE_SET = 0;
    *SIRC_INT_TYPE = 0;
    *SIRC_INT_POLARITY = 0;
    *SIRC_INT_CLEAR = 0;
    *SIRC_INT_SELECT = 0;
    *SIRC_INT_RAW_STATUS = 0;
    *SIRC_INT_IRQ_STATUS = 0;
    *SIRC_INT_IRQ1_STATUS = 0;
}

void
MachineQSD8xxx::shutdownTimers()
{
    // Map the timer registers
    uint32 volatile *AGPT_MATCH_VAL = (uint32*)memPhysMap(0xAC100000);
    uint32 volatile *AGPT_COUNT_VAL = (uint32*)memPhysMap(0xAC100004);
    uint32 volatile *AGPT_ENABLE    = (uint32*)memPhysMap(0xAC100008);
    uint32 volatile *AGPT_CLEAR     = (uint32*)memPhysMap(0xAC10000C);
    uint32 volatile *ADGT_MATCH_VAL = (uint32*)memPhysMap(0xAC100010);
    uint32 volatile *ADGT_COUNT_VAL = (uint32*)memPhysMap(0xAC100014);
    uint32 volatile *ADGT_ENABLE    = (uint32*)memPhysMap(0xAC100018);
    uint32 volatile *ADGT_CLEAR     = (uint32*)memPhysMap(0xAC10001C);

    // Disable GP timer
    *AGPT_ENABLE = 0;
    *AGPT_CLEAR = 1;
    *AGPT_COUNT_VAL = 0;
    *AGPT_MATCH_VAL = ~0;

    // Disable DG timer
    *ADGT_ENABLE = 0;
    *ADGT_CLEAR = 1;
    *ADGT_COUNT_VAL = 0;
    *ADGT_MATCH_VAL = ~0;
}

#define MSM_DMOV_CHANNEL_COUNT 16
#define MSM_DMOV_PHYS 0xA9700000
#define DMOV_SD0(off, ch) (MSM_DMOV_PHYS + 0x0000 + (off) + ((ch) << 2))
#define DMOV_SD1(off, ch) (MSM_DMOV_PHYS + 0x0400 + (off) + ((ch) << 2))
#define DMOV_SD2(off, ch) (MSM_DMOV_PHYS + 0x0800 + (off) + ((ch) << 2))
#define DMOV_SD3(off, ch) (MSM_DMOV_PHYS + 0x0C00 + (off) + ((ch) << 2))
#define DMOV_CONFIG(ch)       DMOV_SD3(0x300, ch)

void
MachineQSD8xxx::shutdownDMA()
{
    for (int i=0; i < MSM_DMOV_CHANNEL_COUNT; i++)
    {
        uint32 volatile *dmaChannelConfig = (uint32*)memPhysMap(DMOV_CONFIG(i));
        *dmaChannelConfig = 0;
    }
}

void
MachineQSD8xxx::hardwareShutdown(struct fbinfo *)
{
    shutdownTimers();
    shutdownInterrupts();
    shutdownSirc();
    shutdownDMA();
}

struct QSD8xxxFbDmaData
{
    volatile uint8 *fbDmaSize;
    volatile uint8 *fbDmaPhysFb;
    volatile uint8 *fbDmaStride;
    volatile uint8 *fbDmaStart;
    uint32 fbPhysAddr;
};

static void __preload
QSD8xxxFbPutc(struct fbinfo *fbi, char c)
{
    // Draw the character to the fb as usual
    fb_putc(fbi, c);

    // Only initiate DMA transfer after a newline
    if (c != '\n')
        return;

    // Initiate the DMA transfer to update the display
    QSD8xxxFbDmaData* data = (QSD8xxxFbDmaData*)fbi->putcFuncData;
    if (getMMUReg() & 0x1)
    {
        *(uint32 *)data->fbDmaSize = (fbi->scry << 16) | (fbi->scrx);
        *(uint32 *)data->fbDmaPhysFb = data->fbPhysAddr;
        *(uint32 *)data->fbDmaStride = fbi->scrx * 2;
        *(uint32 *)data->fbDmaStart = 0;
    }
    else
    {
        *(uint32 *)(0xaa200000 + 0x90004) = (fbi->scry << 16) | (fbi->scrx);
        *(uint32 *)(0xaa200000 + 0x90008) = data->fbPhysAddr;
        *(uint32 *)(0xaa200000 + 0x9000c) = fbi->scrx * 2;
        *(uint32 *)(0xaa200000 + 0x00044) = 0;
    }
}

void
MachineQSD8xxx::configureFb(struct fbinfo *fbi)
{
    // Cast the data pointer so we can use it to contain our own data
    QSD8xxxFbDmaData* data = (QSD8xxxFbDmaData*)fbi->putcFuncData;

    // Get a mapping for the physical addresses we'll need to do the DMA
    data->fbDmaSize = memPhysMap(0xaa200000 + 0x90004);
    data->fbDmaPhysFb = memPhysMap(0xaa200000 + 0x90008);
    data->fbDmaStride = memPhysMap(0xaa200000 + 0x9000c);
    data->fbDmaStart = memPhysMap(0xaa200000 + 0x00044);
    data->fbPhysAddr = vidGetVRAM();

    // Override the fb_putc() with our own function
    fbi->putcFunc = &QSD8xxxFbPutc;
}

int
MachineQSD8xxx::preHardwareShutdown(struct fbinfo *fbi)
{
    configureFb(fbi);
    return 0;
}

REGMACHINE(MachineQSD8xxx)


/****************************************************************
 * MSM 722x
 ****************************************************************/

MachineMSM722x::MachineMSM722x()
{
    name = "Generic MSM722x";
    flushCache = cpuFlushCache_arm6;
    arm6mmu = 1;
    archname = "MSM722x";
    CPUInfo[0] = L"MSM7225";
    CPUInfo[1] = L"MSM7227";
    //customStartFunc = bootMSM722x;
}

void
MachineMSM722x::init()
{
    // TODO MS: Confirm interrupts are at these addresses
    runMemScript(
        "set ramaddr 0x10000000\n"
        "addlist irqs p2v(0xc0000080) 0x100 32 0\n"
        "addlist irqs p2v(0xc0000084) 0 32 0\n"
        );
    defineMsmGpios();
}

void
MachineMSM722x::hardwareShutdown(struct fbinfo *fbi)
{
//    shutdownTimers();
//    shutdownInterrupts();
//    shutdownDMA();
}

void
MachineMSM722x::shutdownInterrupts()
{
    uint32 dummyRead;

    uint32 volatile *VIC_INT_SELECT0 = (uint32*)memPhysMap(0xC0000000 + 0x0000);
    uint32 volatile *VIC_INT_SELECT1 = (uint32*)memPhysMap(0xC0000000 + 0x0004);
    uint32 volatile *VIC_INT_SELECT2 = (uint32*)memPhysMap(0xC0000000 + 0x0008);
    uint32 volatile *VIC_INT_SELECT3 = (uint32*)memPhysMap(0xC0000000 + 0x000C);
    uint32 volatile *VIC_INT_EN0 = (uint32*)memPhysMap(0xC0000000 + 0x0010);
    uint32 volatile *VIC_INT_EN1 = (uint32*)memPhysMap(0xC0000000 + 0x0014);
    uint32 volatile *VIC_INT_EN2 = (uint32*)memPhysMap(0xC0000000 + 0x0018);
    uint32 volatile *VIC_INT_EN3 = (uint32*)memPhysMap(0xC0000000 + 0x001C);
    uint32 volatile *VIC_INT_ENCLEAR0 = (uint32*)memPhysMap(0xC0000000 + 0x0020);
    uint32 volatile *VIC_INT_ENCLEAR1 = (uint32*)memPhysMap(0xC0000000 + 0x0024);
    uint32 volatile *VIC_INT_ENCLEAR2 = (uint32*)memPhysMap(0xC0000000 + 0x0028);
    uint32 volatile *VIC_INT_ENCLEAR3 = (uint32*)memPhysMap(0xC0000000 + 0x002C);
    uint32 volatile *VIC_INT_ENSET0 = (uint32*)memPhysMap(0xC0000000 + 0x0030);
    uint32 volatile *VIC_INT_ENSET1 = (uint32*)memPhysMap(0xC0000000 + 0x0034);
    uint32 volatile *VIC_INT_ENSET2 = (uint32*)memPhysMap(0xC0000000 + 0x0038);
    uint32 volatile *VIC_INT_ENSET3 = (uint32*)memPhysMap(0xC0000000 + 0x003C);
    uint32 volatile *VIC_INT_TYPE0 = (uint32*)memPhysMap(0xC0000000 + 0x0040);
    uint32 volatile *VIC_INT_TYPE1 = (uint32*)memPhysMap(0xC0000000 + 0x0044);
    uint32 volatile *VIC_INT_TYPE2 = (uint32*)memPhysMap(0xC0000000 + 0x0048);
    uint32 volatile *VIC_INT_TYPE3 = (uint32*)memPhysMap(0xC0000000 + 0x004C);
    uint32 volatile *VIC_INT_POLARITY0 = (uint32*)memPhysMap(0xC0000000 + 0x0050);
    uint32 volatile *VIC_INT_POLARITY1 = (uint32*)memPhysMap(0xC0000000 + 0x0054);
    uint32 volatile *VIC_INT_POLARITY2 = (uint32*)memPhysMap(0xC0000000 + 0x0058);
    uint32 volatile *VIC_INT_POLARITY3 = (uint32*)memPhysMap(0xC0000000 + 0x005C);
    uint32 volatile *VIC_NO_PEND_VAL = (uint32*)memPhysMap(0xc0000000 + 0x0060);

    uint32 volatile *VIC_INT_MASTEREN = (uint32*)memPhysMap(0xc0000000 + 0x0064);
    uint32 volatile *VIC_CONFIG = (uint32*)memPhysMap(0xc0000000 + 0x0068);
    uint32 volatile *VIC_PROTECTION = (uint32*)memPhysMap(0xc0000000 + 0x006C);

    uint32 volatile *VIC_IRQ_STATUS0 = (uint32*)memPhysMap(0xc0000000 + 0x0080);
    uint32 volatile *VIC_IRQ_STATUS1 = (uint32*)memPhysMap(0xc0000000 + 0x0084);
    uint32 volatile *VIC_IRQ_STATUS2 = (uint32*)memPhysMap(0xc0000000 + 0x0088);
    uint32 volatile *VIC_IRQ_STATUS3 = (uint32*)memPhysMap(0xc0000000 + 0x008C);
    uint32 volatile *VIC_FIQ_STATUS0 = (uint32*)memPhysMap(0xc0000000 + 0x0090);
    uint32 volatile *VIC_FIQ_STATUS1 = (uint32*)memPhysMap(0xc0000000 + 0x0094);
    uint32 volatile *VIC_FIQ_STATUS2 = (uint32*)memPhysMap(0xc0000000 + 0x0098);
    uint32 volatile *VIC_FIQ_STATUS3 = (uint32*)memPhysMap(0xc0000000 + 0x009C);
    uint32 volatile *VIC_RAW_STATUS0 = (uint32*)memPhysMap(0xc0000000 + 0x00A0);
    uint32 volatile *VIC_RAW_STATUS1 = (uint32*)memPhysMap(0xc0000000 + 0x00A4);
    uint32 volatile *VIC_RAW_STATUS2 = (uint32*)memPhysMap(0xc0000000 + 0x00A8);
    uint32 volatile *VIC_RAW_STATUS3 = (uint32*)memPhysMap(0xc0000000 + 0x00AC);
    uint32 volatile *VIC_INT_CLEAR0 = (uint32*)memPhysMap(0xc0000000 + 0x00B0);
    uint32 volatile *VIC_INT_CLEAR1 = (uint32*)memPhysMap(0xc0000000 + 0x00B4);
    uint32 volatile *VIC_INT_CLEAR2 = (uint32*)memPhysMap(0xc0000000 + 0x00B8);
    uint32 volatile *VIC_INT_CLEAR3 = (uint32*)memPhysMap(0xc0000000 + 0x00BC);
    uint32 volatile *VIC_SOFTINT0 = (uint32*)memPhysMap(0xc0000000 + 0x00C0);
    uint32 volatile *VIC_SOFTINT1 = (uint32*)memPhysMap(0xc0000000 + 0x00C4);
    uint32 volatile *VIC_SOFTINT2 = (uint32*)memPhysMap(0xc0000000 + 0x00C8);
    uint32 volatile *VIC_SOFTINT3 = (uint32*)memPhysMap(0xc0000000 + 0x00CC);
    uint32 volatile *VIC_IRQ_VEC_RD = (uint32*)memPhysMap(0xc0000000 + 0x00D0);
    uint32 volatile *VIC_IRQ_VEC_PEND_RD = (uint32*)memPhysMap(0xc0000000 + 0x00D4);
    uint32 volatile *VIC_IRQ_VEC_WR = (uint32*)memPhysMap(0xc0000000 + 0x00D8);

    uint32 volatile *VIC_IRQ_IN_SERVICE = (uint32*)memPhysMap(0xc0000000 + 0x00E0);
    uint32 volatile *VIC_IRQ_IN_STACK = (uint32*)memPhysMap(0xc0000000 + 0x00E4);
    uint32 volatile *VIC_TEST_BUS_SEL = (uint32*)memPhysMap(0xc0000000 + 0x00E8);

    // Disable the interrupt controller
    *VIC_INT_MASTEREN = 0;
    *VIC_CONFIG = 0;
    *VIC_INT_EN0 = 0;
    *VIC_INT_EN1 = 0;
    *VIC_INT_SELECT0 = 0;
    *VIC_INT_SELECT1 = 0;
    *VIC_INT_POLARITY0 = 0;
    *VIC_INT_POLARITY1 = 0;
    *VIC_INT_TYPE0 = 0;
    *VIC_INT_TYPE1 = 0;

    // Reset the interrupt priority register
    // (Must read the existing values first for some reason)
    dummyRead = *VIC_IRQ_VEC_RD;
    dummyRead = *VIC_IRQ_VEC_PEND_RD;
    *VIC_IRQ_VEC_WR = ~0;
}

void
MachineMSM722x::shutdownTimers()
{
    uint32 volatile *TIMER_MATCH_VAL = (uint32*)memPhysMap(0xC0100010 + 0x0000);
    uint32 volatile *TIMER_COUNT_VAL = (uint32*)memPhysMap(0xC0100010 + 0x0004);
    uint32 volatile *TIMER_ENABLE = (uint32*)memPhysMap(0xC0100010 + 0x0008);
    uint32 volatile *TIMER_CLEAR = (uint32*)memPhysMap(0xC0100010 + 0x000C);
    uint32 volatile *CSR_PROTECTION = (uint32*)memPhysMap(0xC0100010 + 0x0020);

    *TIMER_MATCH_VAL = ~0;
    *TIMER_COUNT_VAL = 0;
    *TIMER_ENABLE = 0;
    *TIMER_CLEAR = 1;
    *CSR_PROTECTION = 0;    
}


void
MachineMSM722x::shutdownDMA()
{
    for (int i=0; i < MSM_DMOV_CHANNEL_COUNT; i++)
    {
        uint32 volatile *dmaChannelConfig = (uint32*)memPhysMap(DMOV_CONFIG(i));
        *dmaChannelConfig = 0;
    }
}

REGMACHINE(MachineMSM722x)

