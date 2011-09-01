/*
 * Linux loader for Windows CE
 *
 * (C) Copyright 2006 Kevin O'Connor <kevin@koconnor.net>
 * Copyright (C) 2003 Andrew Zabolotny
 *
 * This file may be distributed under the terms of the GNU GPL license.
 */

#include <windows.h> // Sleep
#include <wchar.h> // _wfopen
#include <stdio.h> // FILE, fopen, fseek, ftell
#include <ctype.h> // toupper

#define CONFIG_ACCEPT_GPL
#include "setup.h"

#include "xtypes.h"
#include "script.h" // REG_CMD
#include "memory.h" // memPhysMap, memPhysAddr, memPhysSize
#include "output.h" // Output, Screen, fnprepare
#include "cpu.h" // take_control, return_control
#include "video.h" // vidGetVRAM
#include "machines.h" // Mach
#include "fbwrite.h" // fb_puts
#include "winvectors.h" // stackJumper_s
#include "linboot.h"
#include "resource.h"

// Kernel file name
static char *bootKernel = "zImage";
// Initrd file name
static char *bootInitrd = "";
// Kernel command line
static char *bootCmdline = "root=/dev/ram0 ro console=tty0";
// ARM machine type (see linux/arch/arm/tools/mach-types)
static uint32 bootMachineType = 0;
// Enable framebuffer writes during bootup.
static uint32 FBDuringBoot = 1;
// Tags offset from physical ram start
static uint32 tagsOffset = 0x100;
// Kernel offset from physical ram start
static uint32 kernelOffset = 0x8000;
// Initrd offset from physical ram start
static uint32 initrdOffset = 0x508000;

REG_VAR_STR(0, "KERNEL", bootKernel, "Linux kernel file name")
REG_VAR_STR(0, "INITRD", bootInitrd, "Initial Ram Disk file name")
REG_VAR_STR(0, "CMDLINE", bootCmdline, "Kernel command line")
REG_VAR_INT(0, "MTYPE", bootMachineType
            , "ARM machine type (see linux/arch/arm/tools/mach-types)")
REG_VAR_INT(0, "FBDURINGBOOT", FBDuringBoot
            , "Enable/disable writing status lines to screen during boot")
REG_VAR_INT(0, "TAGS_OFFSET", tagsOffset, "Tags offset from physical ram start" \
	" (note: tags size is limited to one page, so don't put offset to the" \
	" end of a page boundary)")
REG_VAR_INT(0, "KERNEL_OFFSET", kernelOffset, "Kernel offset from physical ram start")
REG_VAR_INT(0, "INITRD_OFFSET", initrdOffset, "Initrd offset from physical ram start")

/*
 * Theory of operation:
 *
 * This code is tasked with loading the linux kernel (and an optional
 * initrd) into memory and then jumping to that kernel so that linux
 * can start.  In order to jump to the kernel, all hardware must be
 * disabled (see Mach->hardwareShutdown), the Memory Management Unit
 * (MMU) must be off (see mmu_trampoline in asmstuff.S), and the
 * kernel must be allocated in physically continous ram at a certain
 * position in memory.  Note that it is difficult to allocate
 * physically continuous ram at preset locations while CE is still
 * running (because some other program or the OS might already be
 * using those pages).  To account for this, the code will disable the
 * hardware/mmu, jump to a "preloader" function which will copy the
 * kernel from any arbitrary memory location to the necessary preset
 * areas of memory, and then jump to the kernel.
 *
 * For the above to work, it must load the kernel image into memory
 * while CE is still running.  As a result of this, the kernel is
 * loaded into "virtual memory".  However the preloader is run while
 * the MMU is off and thus sees "physical memory".  A list of physical
 * addresses for each virtual page is maintained so that the preloader
 * can find the proper pages when the mmu is off.  This is complicated
 * because the list itself is built while CE is running (it is
 * allocated in virtual memory) and it can exceed one page in size.
 * To handle this, the system uses a "three level page list" - a list
 * of pointers to pages which contain pointers to pages.  The
 * preloader is passed in a data structure which can not exceed one
 * page (see preloadData).  This structure has a list of pointers to
 * pages (indexPages) that contain pointers to pages of the kernel.
 *
 * Because the preloader and hardware shutdown can be complicated, the
 * code will try to write status messages directly to the framebuffer.
 */


/****************************************************************
 * Linux utility functions
 ****************************************************************/

/* Set up kernel parameters. ARM/Linux kernel uses a series of tags,
 * every tag describe some aspect of the machine it is booting on.
 */
static void
setup_linux_params(char *tagaddr, uint32 phys_initrd_addr, uint32 initrd_size)
{
  struct tag *tag = (struct tag *)tagaddr;

  // Core tag
  tag->hdr.tag = ATAG_CORE;
  tag->hdr.size = tag_size (tag_core);
  tag->u.core.flags = 0;
  tag->u.core.pagesize = 0x00001000;
  tag->u.core.rootdev = 0x0000; // not used, use kernel cmdline for this
  tag = tag_next (tag);

  // now the cmdline tag
  tag->hdr.tag = ATAG_CMDLINE;
  // tag header, zero-terminated string and round size to 32-bit words
  tag->hdr.size = (sizeof (struct tag_header) + strlen (bootCmdline) + 1 + 3) >> 2;
  strcpy (tag->u.cmdline.cmdline, bootCmdline);
  tag = tag_next (tag);

  // now the mem32 tag
  tag->hdr.tag = ATAG_MEM;
  tag->hdr.size = tag_size (tag_mem32);
  tag->u.mem.start = memPhysAddr;
  tag->u.mem.size = memPhysSize;
  tag = tag_next (tag);

  /* and now the initrd tag */
  if (initrd_size)
  {
    tag->hdr.tag = ATAG_INITRD2;
    tag->hdr.size = tag_size (tag_initrd);
    tag->u.initrd.start = phys_initrd_addr;
    tag->u.initrd.size = initrd_size;
    tag = tag_next (tag);
  }

  // now the NULL tag
  tag->hdr.tag = ATAG_NONE;
  tag->hdr.size = 0;
}


/****************************************************************
 * Preloader
 ****************************************************************/

// Maximum number of index pages.
#define MAX_INDEX 6
#define PAGES_PER_INDEX (PAGE_SIZE / sizeof(uint32))

// Data Shared between normal haret code and C preload code.
struct preloadData {
    uint32 machtype;
    uint32 startRam;
    uint32 preloadStart;
    uint32 preloadPhys;

    char *tags;
    uint32 tagsOffset;
    uint32 tagsSize;
    uint32 kernelOffset;
    uint32 kernelSize;
    uint32 initrdOffset;
    uint32 initrdSize;
    const char **indexPages[MAX_INDEX];

    // Optional CRC check
    uint32 doCRC;
    uint32 tagsCRC, kernelCRC, initrdCRC;

    // Framebuffer info
    fbinfo fbi;
    uint32 physFB, physFonts;
    unsigned char fonts[FONTDATAMAX];
    
    // Extra machine info
    startfunc_t machStartFunc;
};

// CRC a block of ram (from linux/lib/crc32.c)
#define CRCPOLY_BE 0x04c11db7
static uint32 __preload
crc32_be(uint32 crc, const char *data, uint32 len)
{
    const unsigned char *p = (const unsigned char *)data;
    int i;
    while (len--) {
        crc ^= *p++ << 24;
        for (i = 0; i < 8; i++)
            crc = (crc << 1) ^ ((crc & 0x80000000) ? CRCPOLY_BE : 0);
    }
    return crc;
}
static uint32 __preload
crc32_be_finish(uint32 crc, uint32 len)
{
    for (; len; len >>= 8) {
        unsigned char l = len;
        crc = crc32_be(crc, (char *)&l, 1);
    }
    return ~crc & 0xFFFFFFFF;
}

// Copy memory (need a memcpy with __preload tag).
void __preload
do_copy(char *dest, const char *src, int count)
{
    uint32 *d = (uint32*)dest, *s = (uint32*)src, *e = (uint32*)&src[count];
    while (s < e)
        *d++ = *s++;
}

// Copy a list of pages to a linear area of memory
static void __preload
do_copyPages(char *dest, const char ***pages, int start, int pagecount)
{
    for (int i=start; i<start+pagecount; i++) {
        do_copy(dest, pages[i/PAGES_PER_INDEX][i%PAGES_PER_INDEX], PAGE_SIZE);
        dest += PAGE_SIZE;
    }
}

// Get Program Status Register value (in __preload section)
static inline uint32 __preload do_cpuGetPSR(void) {
    uint32 val;
    asm volatile("mrs %0, cpsr" : "=r" (val));
    return val;
}

// GCC really wants to put constant strings into the .rtext section -
// this can break relocated code that can't see the .rtext section.
// This macro will force a string into a code section.  Note backslash
// and percent characters must be double escaped.
#define STR_IN_CODE(sec,str) ({                 \
    const char *__str;                          \
    asm(".section " #sec ", 1\n"                \
        "1:      .asciz \"" str "\"\n"          \
        "        .balign 4\n"                   \
        "        .section " #sec ", 0\n"        \
        "        add %0, pc, #( 1b - . - 8 )\n" \
        : "=r" (__str));                        \
    __str;                                      \
})

#define FB_PRINTF(fbi,fmt,args...) fb_printf((fbi), STR_IN_CODE(.text.preload, fmt) , ##args )

static inline int __preload
fbOverlaps(struct preloadData *pd)
{
    // Assumes initrd is placed as last in order from phys ram start (i.e. tags -> kernel -> initrd)
    return IN_RANGE(pd->physFB, pd->startRam
                    , pd->initrdOffset + pd->initrdSize);
}

// Code to launch kernel.
static void __preload
preloader(struct preloadData *data)
{
    data->fbi.fb = (uint16 *)data->physFB;
    data->fbi.putcFunc = (fb_putc_t)((char*)data->fbi.putcFunc - data->preloadStart + data->preloadPhys);
    data->fbi.fonts = (unsigned char *)data->physFonts;
    if (data->machStartFunc)
        data->machStartFunc = (startfunc_t)((char*)data->machStartFunc - data->preloadStart + data->preloadPhys);
    FB_PRINTF(&data->fbi, "In preloader\\n");

    uint32 psr = do_cpuGetPSR();
    FB_PRINTF(&data->fbi, "PSR=%%x\\n", psr);
    if ((psr & 0xc0) != 0xc0)
        FB_PRINTF(&data->fbi, "ERROR: IRQS not off\\n");

    if (fbOverlaps(data)) {
        FB_PRINTF(&data->fbi, "Disabling framebuffer feedback\\n");
        data->fbi.fb = 0;
    }

    // Copy tags to beginning of ram.
    char *destTags = (char *)data->startRam + data->tagsOffset;
    do_copy(destTags, data->tags, data->tagsSize);

    FB_PRINTF(&data->fbi, "Tags relocated to 0x%%x (size 0x%%x)\\n",
        destTags, data->tagsSize);

    // Copy kernel image
    char *destKernel = (char *)data->startRam + data->kernelOffset;
    int kernelCount = PAGE_ALIGN(data->kernelSize) / PAGE_SIZE;
    do_copyPages((char *)destKernel, data->indexPages, 0, kernelCount);

    FB_PRINTF(&data->fbi, "Kernel relocated to 0x%%x (size 0x%%x)\\n",
        destKernel, data->kernelSize);

    // Copy initrd (if applicable)
    char *destInitrd = (char *)data->startRam + data->initrdOffset;
    int initrdCount = PAGE_ALIGN(data->initrdSize) / PAGE_SIZE;
    do_copyPages(destInitrd, data->indexPages, kernelCount, initrdCount);

    FB_PRINTF(&data->fbi, "Initrd relocated to 0x%%x (size 0x%%x)\\n",
        destInitrd, data->initrdSize);

    // Do CRC check (if enabled).
    if (data->doCRC) {
        FB_PRINTF(&data->fbi, "Checking tags crc...");
        uint32 crc = crc32_be(0, destTags, data->tagsSize);
        crc = crc32_be_finish(crc, data->tagsSize);
        if (crc == data->tagsCRC)
            FB_PRINTF(&data->fbi, "okay\\n");
        else
            FB_PRINTF(&data->fbi, "FAIL FAIL FAIL\\n");

        FB_PRINTF(&data->fbi, "Checking kernel crc...");
        crc = crc32_be(0, destKernel, data->kernelSize);
        crc = crc32_be_finish(crc, data->kernelSize);
        if (crc == data->kernelCRC)
            FB_PRINTF(&data->fbi, "okay\\n");
        else
            FB_PRINTF(&data->fbi, "FAIL FAIL FAIL\\n");

        if (data->initrdSize) {
            FB_PRINTF(&data->fbi, "Checking initrd crc...");
            crc = crc32_be(0, destInitrd, data->initrdSize);
            crc = crc32_be_finish(crc, data->initrdSize);
            if (crc == data->initrdCRC)
                FB_PRINTF(&data->fbi, "okay\\n");
            else
                FB_PRINTF(&data->fbi, "FAIL FAIL FAIL\\n");
        }
    }

    // vibration test codes
#ifdef TEST_HARET
    asm volatile(" mov r0,#0x80000000\n\t"
                       " orr r0,r0,#0x16\n\t"
                       " mcr p15, 0, r0, c15, c2, 4\n\t");
    volatile unsigned *bank5_in = (unsigned int*)(0xa9200844);
    volatile unsigned *bank5_out = (unsigned int*)(0xa9200850);
     *bank5_out = *bank5_in ^ 0x00000400;
#endif
    // Boot
    if (data->machStartFunc) {
        FB_PRINTF(&data->fbi, "Jumping to Kernel (custom)...\\n");
        data->machStartFunc(destKernel, data->machtype, destTags);
    }
    else {
        FB_PRINTF(&data->fbi, "Jumping to Kernel...\\n");
        typedef void (*lin_t)(uint32 zero, uint32 mach, char *tags);
        lin_t startfunc = (lin_t)destKernel;
        // buzz 2451
        // bahamas 1940
        startfunc(0, 2451, destTags);
    }
}


/****************************************************************
 * Kernel ram allocation and setup
 ****************************************************************/

extern "C" {
    // Symbols added by linker.
    extern char preload_start;
    extern char preload_end;
}
#define preload_size (&preload_end - &preload_start)
#define preloadExecOffset ((char *)&preloader - &preload_start)
#define stackJumperOffset ((char *)&stackJumper - &preload_start)
#define stackJumperExecOffset (stackJumperOffset        \
    + (uint32)&((stackJumper_s*)0)->asm_handler)

// Description of memory alocated by prepForKernel()
struct bootmem {
    char *imagePages[PAGES_PER_INDEX * MAX_INDEX];
    char **kernelPages, **initrdPages;
    char *tagsPage;
    uint32 physExec;
    uint32 pageCount;
    void *allocedRam;
    struct preloadData *pd;
};

// Release resources allocated in prepForKernel.
static void
cleanupBootMem(struct bootmem *bm)
{
    if (!bm)
        return;
    freePages(bm->allocedRam);
    free(bm);
}

// Allocate memory for a kernel (and possibly initrd), and configure a
// preloader that can launch that kernel.  Note the caller needs to
// copy the kernel and initrd into the pages allocated.
static bootmem *
prepForKernel(uint32 kernelSize, uint32 initrdSize)
{
    // Sanity test.
    if (preload_size > PAGE_SIZE || sizeof(preloadData) > PAGE_SIZE) {
        Output(C_ERROR "Internal error.  Preloader too large");
        return NULL;
    }

    // Determine machine type
    uint32 machType = bootMachineType;
    if (! machType)
        machType = Mach->machType;
    if (! machType) {
        Output(C_ERROR "undefined MTYPE");
        return NULL;
    }
    if (memPhysAddr == 0xFFFFFFFF) {
        Output(C_ERROR "Please set start of ram (RAMADDR)");
        return NULL;
    }
    Output("boot params: RAMADDR=%08x RAMSIZE=%08x MTYPE=%d CMDLINE='%s'"
           , memPhysAddr, memPhysSize, machType, bootCmdline);
    Output("Boot FB feedback: %d", FBDuringBoot);

    // Allocate ram for kernel/initrd
    uint32 kernelCount = PAGE_ALIGN(kernelSize) / PAGE_SIZE;
    int initrdCount = PAGE_ALIGN(initrdSize) / PAGE_SIZE;
    int indexCount = PAGE_ALIGN((initrdCount + kernelCount)
                                * sizeof(char*)) / PAGE_SIZE;
    int totalCount = kernelCount + initrdCount + indexCount + 4;
    if (indexCount > MAX_INDEX) {
        Output(C_ERROR "Image too large (%d+%d) - largest size is %d"
               , kernelSize, initrdSize
               , MAX_INDEX * PAGES_PER_INDEX * PAGE_SIZE);
        return NULL;
    }

    // Allocate data structure.
    struct bootmem *bm = (bootmem*)calloc(sizeof(bootmem), 1);
    if (!bm) {
        Output(C_ERROR "Failed to allocate bootmem struct");
        return NULL;
    }

    struct pageAddrs pages[PAGES_PER_INDEX * MAX_INDEX + 4];
    bm->pageCount = totalCount;
    bm->allocedRam = allocPages(pages, totalCount);
    if (! bm->allocedRam) {
	free(bm);
	return NULL;
    }

    Output("Built virtual to physical page mapping");

    struct pageAddrs *pg_tag = &pages[0];
    struct pageAddrs *pgs_kernel = &pages[1];
    struct pageAddrs *pgs_initrd = &pages[kernelCount+1];
    struct pageAddrs *pgs_index = &pages[initrdCount+kernelCount+1];
    struct pageAddrs *pg_stack = &pages[totalCount-3];
    struct pageAddrs *pg_data = &pages[totalCount-2];
    struct pageAddrs *pg_preload = &pages[totalCount-1];
    
    // Determine the size of the tags, which is the area of the start to
    // the end of the page (i.e. at 0x100, size is PAGE_SIZE - 0x100)
    uint32 tagsSize = PAGE_SIZE - (tagsOffset % PAGE_SIZE);

    // Prevent pages from being overwritten during relocation
    for (int i=0; i<totalCount; i++) {
        struct pageAddrs *pg = &pages[i], *ovpg;
        uint32 relPhys = pg->physLoc - memPhysAddr;
        // See if this page will be overwritten in preloader.
        if (relPhys >= PAGE_ALIGN_DOWN(tagsOffset)
            && relPhys < tagsOffset + tagsSize)
            ovpg = pg_tag;
        else if (relPhys >= PAGE_ALIGN_DOWN(kernelOffset)
                 && relPhys < kernelOffset + kernelSize)
            ovpg = &pgs_kernel[(relPhys - kernelOffset) / PAGE_SIZE];
        else if (initrdSize
                 && (relPhys >= PAGE_ALIGN_DOWN(initrdOffset)
                     && relPhys < initrdOffset + initrdSize))
            ovpg = &pgs_initrd[(relPhys - initrdOffset) / PAGE_SIZE];
        else
            // This page wont be overwritten.
            continue;
        if (pg == ovpg)
            // This page will be overwritten by itself - no problem
            continue;
        // This page will be overwritten - swap it with the page that
        // it will be overwritten by and retry.
        struct pageAddrs tmp = *pg;
        *pg = *ovpg;
        *ovpg = tmp;
        i--;
    }

    Output("Allocated %d pages (tags=%p/%08x kernel=%p/%08x initrd=%p/%08x"
           " index=%p/%08x)"
           , totalCount
           , pg_tag->virtLoc, pg_tag->physLoc
           , pgs_kernel->virtLoc, pgs_kernel->physLoc
           , pgs_initrd->virtLoc, pgs_initrd->physLoc
           , pgs_index->virtLoc, pgs_index->physLoc);

    // Setup linux tags.
    setup_linux_params(pg_tag->virtLoc, memPhysAddr + initrdOffset, initrdSize);
    Output("Built kernel tags area");

    // Setup kernel/initrd indexes
    for (uint32 i=0; i<kernelCount+initrdCount; i++) {
        uint32 *index = (uint32*)pgs_index[i/PAGES_PER_INDEX].virtLoc;
        index[i % PAGES_PER_INDEX] = pgs_kernel[i].physLoc;
        bm->imagePages[i] = pgs_kernel[i].virtLoc;
    }
    bm->kernelPages = &bm->imagePages[0];
    bm->initrdPages = &bm->imagePages[kernelCount];
    bm->tagsPage = pg_tag->virtLoc;
    Output("Built page index");

    // Setup preloader data.
    struct preloadData *pd = (struct preloadData *)pg_data->virtLoc;
    pd->machtype = machType;
    pd->tags = (char *)pg_tag->physLoc;
    pd->tagsOffset = tagsOffset;
    pd->tagsSize = tagsSize;
    pd->kernelOffset = kernelOffset;
    pd->kernelSize = kernelSize;
    pd->initrdOffset = initrdOffset;
    pd->initrdSize = initrdSize;
    for (int i=0; i<indexCount; i++)
        pd->indexPages[i] = (const char **)pgs_index[i].physLoc;
    pd->startRam = memPhysAddr;
    pd->preloadStart = (uint32)&preload_start;
    pd->preloadPhys = pg_preload->physLoc;
    bm->pd = pd;

    Output("Tags will be at offset 0x%08x (0x%x)", pd->tagsOffset, pd->tagsSize);
    Output("Kernel will be at offset 0x%08x (0x%x)", pd->kernelOffset, pd->kernelSize);
    Output("Initrd will be at offset 0x%08x (0x%x)", pd->initrdOffset, pd->initrdSize);
    
    if (FBDuringBoot) {
        fb_init(&pd->fbi);
        memcpy(&pd->fonts, fontdata_mini_4x6, sizeof(fontdata_mini_4x6));
        pd->physFonts = pg_data->physLoc + offsetof(struct preloadData, fonts);
        pd->physFB = vidGetVRAM();
        Output("Video Phys FB=%08x Fonts=%08x", pd->physFB, pd->physFonts);
        if (fbOverlaps(pd))
            Output("Framebuffer overlaps with kernel destination");
    }

    // Setup preloader code.
    memcpy(pg_preload->virtLoc, &preload_start, preload_size);

    stackJumper_s *sj = (stackJumper_s*)&pg_preload->virtLoc[stackJumperOffset];
    sj->stack = pg_stack->physLoc + PAGE_SIZE;
    sj->data = pg_data->physLoc;
    sj->execCode = pg_preload->physLoc + preloadExecOffset;

    bm->physExec = pg_preload->physLoc + stackJumperExecOffset;

    Output("preload=%d@%p/%08x sj=%p stack=%p/%08x data=%p/%08x exec=%08x"
           , preload_size, pg_preload->virtLoc, pg_preload->physLoc
           , sj, pg_stack->virtLoc, pg_stack->physLoc
           , pg_data->virtLoc, pg_data->physLoc, sj->execCode);

    return bm;
}


/****************************************************************
 * Hardware shutdown and trampoline setup
 ****************************************************************/

extern "C" {
    // Assembler code
    void mmu_trampoline(uint32 phys, uint8 *mmu, uint32 code, void (*)(void));
    void mmu_trampoline_end();
}

// Verify the mmu-disabling trampoline.
static uint32
setupTrampoline()
{
    uint32 virtTram = MVAddr((uint32)mmu_trampoline);
    uint32 virtTramEnd = MVAddr((uint32)mmu_trampoline_end);
    if ((virtTram & 0xFFFFF000) != ((virtTramEnd-1) & 0xFFFFF000)) {
        Output(C_ERROR "Can't handle trampoline spanning page boundary"
               " (%p %08x %08x)"
               , mmu_trampoline, virtTram, virtTramEnd);
        return 0;
    }
    uint32 physAddrTram = retryVirtToPhys(virtTram);
    if (physAddrTram == (uint32)-1) {
        Output(C_ERROR "Trampoline not in physical ram. (virt=%08x)"
               , virtTram);
        return 0;
    }
    uint32 physTramL1 = physAddrTram & 0xFFF00000;
    if (IN_RANGE(virtTram, physTramL1, 0x100000)) {
        Output(C_ERROR "Trampoline physical/virtual addresses overlap.");
        return 0;
    }

    Output("Trampoline setup (tram=%d@%p/%08x/%08x)"
           , virtTramEnd - virtTram, mmu_trampoline, virtTram, physAddrTram);

    return physAddrTram;
}

// Launch a kernel loaded in memory.
static void
launchKernel(struct bootmem *bm)
{
    // Prep the trampoline.
    uint32 physAddrTram = setupTrampoline();
    if (! physAddrTram)
        return;

    // Cache an mmu pointer for the trampoline
    uint8 *virtAddrMmu = memPhysMap(cpuGetMMU());
    Output("MMU setup: mmu=%p/%08x", virtAddrMmu, cpuGetMMU());

    // Call per-arch setup.
    int ret = Mach->preHardwareShutdown(&bm->pd->fbi);
    if (ret) {
        Output(C_ERROR "Setup for machine shutdown failed");
        return;
    }
    
    // Set the custom start func
    bm->pd->machStartFunc = Mach->customStartFunc;

    Screen("Go Go Go...");

    // Disable interrupts
    take_control();
  
    fb_clear(&bm->pd->fbi);
    fb_printf(&bm->pd->fbi, "HaRET boot\nShutting down hardware\n");
 
    // Call per-arch boot prep function.
    Mach->hardwareShutdown(&bm->pd->fbi);
  
    fb_printf(&bm->pd->fbi, "Turning off MMU...\n");

    // Disable MMU and launch linux.
    mmu_trampoline(physAddrTram, virtAddrMmu, bm->physExec, Mach->flushCache);

    // The above should not ever return, but we attempt recovery here.
    return_control();
}


/****************************************************************
 * File reading
 ****************************************************************/

// Open a file on disk.
static HANDLE
file_open(const char *name)
{
    Output("Opening file %s", name);
    wchar_t fn[200];
    wchar_t wname[200];
    size_t nConverted = mbstowcs(wname, name, sizeof(wname));
    if (nConverted <= 0)
    {
        Output(C_ERROR "Failed to get wide-charater file name");
        return NULL;
    }

    fnprepare(wname, fn, sizeof(fn));
//    FILE *fk = _wfopen(fn, L"rb");

    HANDLE hFile = CreateFile(fn, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (!hFile) {
        //Output(C_ERROR "Failed to load file %s", fn);
        MessageBox(NULL, fn, L"Failed to load file", MB_OK);
        return NULL;
    }
     
    return hFile;
}

// Find out the size of an open file.
static uint32
get_file_size(HANDLE hFile)
{
//    fseek(fk, 0, SEEK_END);
//    uint32 size = 0;
//    size = ftell(fk);
//    fseek(fk, 0, SEEK_SET);

    DWORD size = GetFileSize(hFile, 0);
    return size;
}

// Copy data from a file into memory and check for success.
static int
file_read(HANDLE hFile, char **pages, uint32 size)
{
    Output("Reading %d bytes...", size);
    while (size) {
        uint32 s = size < PAGE_SIZE ? size : PAGE_SIZE;
        DWORD dwRead = 0;
        ReadFile(hFile, *pages, s, &dwRead, NULL);
//        uint32 ret = fread(*pages, 1, s, f);
        if ((uint32)dwRead != s) {
            Output(C_ERROR "Error reading file.  Expected %d got %d", s, dwRead);
            return -1;
        }
        pages++;
        size -= s;
        AddProgress(s);
    }
    Output("Read complete");
    return 0;
}


// Load a kernel (and possibly initrd) from disk into ram and prep it
// for kernel starting.
// fKernel is read before fInitrd, and they be the same file
static bootmem *
loadHandleKernel(HANDLE fKernel, HANDLE fInitrd, uint32 kernelSize, uint32 initrdSize)
{
    // Obtain ram for the kernel
    int ret;
    struct bootmem *bm = NULL;
    bm = prepForKernel(kernelSize, initrdSize);
    if (!bm)
        goto abort;

    InitProgress(DLG_PROGRESS_BOOT, kernelSize + initrdSize);

    // Load kernel
    ret = file_read(fKernel, bm->kernelPages, kernelSize);
    if (ret)
        goto abort;
    // Load initrd
    if (fInitrd) {
        ret = file_read(fInitrd, bm->initrdPages, initrdSize);
	if (ret)
    	goto abort;
    }

    DoneProgress();

    return bm;

abort:
    DoneProgress();

    cleanupBootMem(bm);
    return NULL;
}

// Load a kernel (and possibly initrd) from disk into ram and prep it
// for kernel starting.
static bootmem *
loadDiskKernel()
{
    Output("boot KERNEL=%s INITRD=%s", bootKernel, bootInitrd);

    // Open kernel file
    HANDLE kernelFile = file_open(bootKernel);
    if (!kernelFile)
        return NULL;
    uint32 kernelSize = 0;
    kernelSize = get_file_size(kernelFile);
    Output("============================\nKernel size is: %d", kernelSize);

    // Open initrd file
    HANDLE initrdFile = NULL;
    uint32 initrdSize = 0;
    if (bootInitrd[0]) {
        initrdFile = file_open(bootInitrd);
        if (!initrdFile)
            return NULL;
        initrdSize = get_file_size(initrdFile);
    }
    
    struct bootmem *bm = loadHandleKernel(kernelFile, initrdFile, kernelSize, initrdSize);

    CloseHandle(kernelFile);
    if (initrdFile)
        CloseHandle(initrdFile);

    return bm;
}


/****************************************************************
 * Resume vector hooking
 ****************************************************************/

// Setup a kernel in ram and hook the wince resume vector so that it
// runs on resume.
static void
resumeIntoBoot(struct bootmem *bm)
{
    // Overwrite the resume vector.
    int ret = hookResume(bm->physExec, 0, 0);
    if (ret) {
        Output("Failed to hook resume vector");
        return;
    }

    // Wait for user to suspend/resume
    Screen("Ready to boot.  Please suspend/resume");
    // SetSystemPowerState(NULL,POWER_STATE_SUSPEND, POWER_FORCE)
    Sleep(300 * 1000);

    // Cleanup (if boot failed somehow).
    Output("Timeout. Restoring original resume vector");
    unhookResume();
}


/****************************************************************
 * Boot code
 ****************************************************************/

static uint32 KernelCRC;
REG_VAR_INT(0, "KERNELCRC", KernelCRC
            , "If set, perform a CRC check on the kernel/initrd.")

// Test the CRC of a set of pages.
static uint32
crc_pages(char **pages, uint32 origsize)
{
    uint32 crc = 0;
    uint32 size = origsize;
    while (size) {
        uint32 s = size < PAGE_SIZE ? size : PAGE_SIZE;
        crc = crc32_be(crc, *pages, s);
        pages++;
        size -= s;
    }
    return crc32_be_finish(crc, origsize);
}

// Boot a kernel loaded into memory via one of two mechanisms.
static void
tryLaunch(struct bootmem *bm, int bootViaResume)
{
    // Setup CRC (if enabled).
    if (KernelCRC) {
        bm->pd->tagsCRC = crc_pages(&bm->tagsPage, bm->pd->tagsSize);
        bm->pd->kernelCRC = crc_pages(bm->kernelPages, bm->pd->kernelSize);
        if (bm->pd->initrdSize)
            bm->pd->initrdCRC = crc_pages(bm->initrdPages, bm->pd->initrdSize);
        bm->pd->doCRC = 1;
        Output("CRC test complete.  tags=%u kernel=%u initrd=%u"
               , bm->pd->tagsCRC, bm->pd->kernelCRC, bm->pd->initrdCRC);
    }

    Output("Launching to physical address %08x", bm->physExec);
    if (bootViaResume)
        resumeIntoBoot(bm);
    else
        launchKernel(bm);

    // Cleanup (if boot failed somehow).
    cleanupBootMem(bm);
}

// Load a kernel from disk, disable hardware, and jump into kernel.
static void
bootLinux(const char *cmd, const char *args)
{
    int bootViaResume = toupper(cmd[0]) == 'R';

    // Load the kernel/initrd/tags/preloader into memory
    struct bootmem *bm = loadDiskKernel();
    if (!bm)
        return;

    // Launch it.
    tryLaunch(bm, bootViaResume);
}
REG_CMD(0, "BOOT|LINUX", bootLinux,
        "BOOTLINUX\n"
        "  Start booting linux kernel. See HELP VARS for variables affecting boot.")
REG_CMD_ALT(0, "BOOT2", bootLinux, boot2, 0)
REG_CMD_ALT(
    0, "RESUMEINTOBOOT", bootLinux, resumeintoboot,
    "RESUMEINTOBOOT\n"
    "  Overwrite the wince resume vector so that the kernel boots\n"
    "  after suspending/resuming the pda")


/****************************************************************
 * Boot from kernel already in ram
 ****************************************************************/

static void
copy_pages(char **pages, const char *src, uint32 size)
{
    while (size) {
        uint32 s = size < PAGE_SIZE ? size : PAGE_SIZE;
        memcpy(*pages, src, s);
        src += s;
        pages++;
        size -= s;
        AddProgress(s);
    }
}

// Load a kernel already in memory, disable hardware, and jump into
// kernel.
void
bootRamLinux(const char *kernel, uint32 kernelSize
             , const char *initrd, uint32 initrdSize
             , int bootViaResume)
{
    // Obtain ram for the kernel
    struct bootmem *bm = prepForKernel(kernelSize, initrdSize);
    if (!bm)
        return;

    // Copy kernel / initrd.
    InitProgress(DLG_PROGRESS_BOOT, kernelSize + initrdSize);
    copy_pages(bm->kernelPages, kernel, kernelSize);
    copy_pages(bm->initrdPages, initrd, initrdSize);
    DoneProgress();

    // Launch it.
    tryLaunch(bm, bootViaResume);
}

void
bootHandleLinux(FILE *f, int kernelSize, int initrdSize, int bootViaResume)
{
    // Load the kernel/initrd/tags/preloader into memory
    struct bootmem *bm = loadHandleKernel(f, f, kernelSize, initrdSize);
    if (!bm)
        return;

    // Launch it.
    tryLaunch(bm, bootViaResume);
}
