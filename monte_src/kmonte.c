/*------------------------------------------------------------ -*- C -*-
 *
 *  Two-kernel Monte for MIPS
 *  Copyright (C) 2003 MuscleNerd <musclenerd2000@hotmail.com>
 *
 *  Based on the x86 version
 *  Copyright (C) 2000 Erik Arjan Hendriks <hendriks@scyld.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *  kmonte.c - Kernel-level code
 *  $Revision: 1.2 $
 *--------------------------------------------------------------------*/

/*
/* Auto-configuration stuff for things living outside the linux kernel
 * source tree. */
/* Include files, designed to support most kernel versions 2.0.0 and later. */
#include <linux/config.h>
#if defined(CONFIG_MODVERSIONS) && ! defined(MODVERSIONS)
#define MODVERSIONS
#endif

#include <linux/version.h>
#include <linux/module.h>
/* Older kernels do not include this automatically. */
#if LINUX_VERSION_CODE < 0x20300  &&  defined(MODVERSIONS)
#include <linux/modversions.h>
#endif

#include <asm/asm.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <linux/kernel.h>
#include <linux/pagemap.h>
#include <linux/pci.h>
#include <linux/major.h>
#include <linux/kdev_t.h>
#ifdef CONFIG_TIVO
#include <linux/tivoconfig.h>
#include <asm/bootinfo.h>
#endif
#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

MODULE_AUTHOR("MuscleNerd <musclenerd2000@hotmail.com>");
MODULE_DESCRIPTION("Two-kernel Monte: Loads new linux kernel over current one (MIPS).");

/* User interface */
#define MONTE_MAGIC_1 0xdeaddead
#define MONTE_MAGIC_2 0xdeaddead
struct monte_param_t {
    char *command_line;
    int   kernel_fd;
};

/*--------------------------------------------------------------------
 * The boot block header directs the whole boot process.
 * It sits at the start of the kernel's disk image.  We'll use
 * it directly in monte, and also pass a copy of it to the new
 * kernel just as it starts up (the kernel needs the boot block
 * even though the entire image has been loaded by then).
 *------------------------------------------------------------------*/
#define BS_SIG_VAL 0x0052504f       /* kernel.signature */

typedef struct boot_block {
  unsigned long boot_flag;          /* BS_SIG_VAL */
  unsigned long load_addr;          /* Load address of the image */
  unsigned long image_sects;        /* Image size in 512 byte blocks (rounded up) */
  unsigned long debug_flag;         /* Should be zero */
  unsigned long entry_point;        /* Entry address after loading */
  unsigned long kern_size;          /* Size of kernel */
  unsigned long image_size;         /* Size of boot image (kern_size + initrd) */
  unsigned long reserved;           /* Always zero */
} boot_block_t;

#define BOOT_BLOCK_SIZE sizeof(boot_block_t)

/* We'll pass the new kernel a copy of the boot block at
   KSEG0 + 4MB, which should be outside the kernel's footprint */
#define MONTE_SETUP_BEGIN  0x80400000

/* These are defined as macros like this because the address where
 * we're storing this stuff could change... (See memory management
 * below) This is safe w/o checking the result since we ask for this
 * page to write it before ever using this macro.  */
#define m_setup ((boot_block_t *)m_get_addr(MONTE_SETUP_BEGIN))

/* The command line can go in the "free" space between the
   end of the boot block and the next PAGE_SIZE boundary */
#define CMDLINE ((char *)m_get_addr(MONTE_SETUP_BEGIN+BOOT_BLOCK_SIZE))

#ifdef  COMMAND_LINE_SIZE
#define CMDLINE_SIZE MIN(COMMAND_LINE_SIZE-1,PAGE_SIZE-BOOT_BLOCK_SIZE-1)
#else
#define CMDLINE_SIZE (PAGE_SIZE-BOOT_BLOCK_SIZE-1)
#endif

/*--------------------------------------------------------------------
 * Monte memory management
 *--------------------------------------------------------------------
The problem:

 In order to boot a kernel we need to copy certain things to certain
 locations in memory.  It is likely that there could be kernel code or
 data already occupying these pages.  In the case of inital ramdisks,
 we don't necessarily need specific pages but allocating several
 contiguous megabytes of memory is not possible.

The Solution:

 We setup lists of pages and where they have to be moved to.  (Unlike
 on the x86,the MIPS doesn't page kernel memory, so it's a bit easier.)
 Once we're ready to go, we jump away from the current kernel into
 code that runs through this list and move all the blocks of memory to
 their final destinations.  (At that point we don't care about
 overwriting the kernel or our page table.)

 Setting up these lists is complicated by a few things.
  
  1 We don't know which pages we will want to use by the time we have
    to start allocating the first pages.

  2 We have to make sure that the movements can be performed safely in
    the sequence that they are given.

  3 The ASM code doing the movements must be as small as possible.

 Number 2 is the biggest issue.  It is resolved by looking for
 conflicts every time a new page is allocated in this system.

 The interface to this memory management code is:

   unsigned long m_get_addr(unsigned long addr);

 It returns the address of a page.  The data on that page
 will be moved to the address given by addr during the restart.  The
 addresses passed into and out of this function are in the kernel's
 (cached) physical memory space KSEG0.
 */


/* The list of movements to be done is stored in the following
 * structure.  If a page happens to fall on the address where it needs
 * to go, it still appears in this list.
 *
 * Pages used to store the list of pages (indirect pages) do not
 * appear in this list.
 *
 * The addresses in these structures are virtual addresses while these
 * structures are being setup.
 *
 * They are translated to physical addresses just before restart.
 */


/* Warning: This struct is referenced by ASM code below.  If you
 * re-arrange it, you'll have to fix the ASM as well. */

struct monte_indirect_t {
    unsigned long dest;        // page that stuff will be copied to
    unsigned long src;         // page that stuff is copied from
    struct monte_indirect_t *next;
    unsigned long _pad;        // fit an even number of indirects per page
};

static struct monte_indirect_t *m_page_list=0;
static struct monte_indirect_t *m_unused_list=0;
static int m_page_count=0;
static int m_ind_count=0;
static unsigned long board_rev_id=0;

/*------------------------------------------------------------------
 * Given a dest page num, find the indirect record that handles
 * copying to that page.
 * Or, return 0 if no such record has been allocated yet.
 *-------------------------------------------------------------*/
static
struct monte_indirect_t *m_find_dest(unsigned long addr) {
    struct monte_indirect_t *m = m_page_list;
    while (m) {
	if (m->dest == addr) { return m; }
	m = m->next;
    }
    return 0;
}

/*------------------------------------------------------------------
 * Given a src page num, find the indirect record that handles
 * copying from that page.
 * Or, return 0 if no such record has been allocated yet.
 *-------------------------------------------------------------*/
static
struct monte_indirect_t *m_find_src(unsigned long addr) {
    struct monte_indirect_t *m = m_page_list;
    while (m) {
	if (m->src == addr) return m;
	m = m->next;
    }
    return 0;
}

/*------------------------------------------------------------------
 * Given a page num, see if we've allocated that page as storage for
 * part of our indirect page list.  (If that's the case, we'll
 * have to move the indirects off the page to free it up for
 * real boot data -- that's done in another function).
 *-------------------------------------------------------------*/
static
struct monte_indirect_t *m_find_ind(unsigned long addr) {
    struct monte_indirect_t *m = m_page_list;
    while (m) {
	if ((((unsigned long)m)&PAGE_MASK) == addr) return m;
	m = m->next;
    }
    /* Check the unused list as well */
    m = m_unused_list;
    while (m) {
	if ((((unsigned long)m)&PAGE_MASK) == addr) return m;
	m = m->next;
    }
    return 0;
}

/*------------------------------------------------------------------
 * Allocate a brand new new page.
 * If we find this new page num is the same as the dest page num of
 * an existing indirect record, copy that record's src page over this
 * page, and then re-use that record's src page as our newly allocated page.
 *-----------------------------------------------------------------*/
static
unsigned long m_get_new_page(void) {
    unsigned long pg, newpg;
    struct monte_indirect_t *m;

    pg = get_free_page(GFP_KERNEL);
    if (!pg) return 0;
    
    /* Check to see if somebody else wants this particular page */
    m = m_find_dest(pg);
    if (m) {
	memcpy((void *)pg, (void *)m->src, PAGE_SIZE);
	newpg = m->src;
	m->src = pg;		/* Update this page list entry. */
	pg = newpg;
    }
    return pg;
}

/*------------------------------------------------------------------
 * Create a new indirect record for the given dest and src page nums.
 *-----------------------------------------------------------------*/
static
int m_add_page(unsigned long dest, unsigned long src) {
    int i;
    struct monte_indirect_t *m;

    /* At this point, we've got a new page full of data that's not
       on the list yet.  So let's add it. */

    if (!m_unused_list) {
        /* Oops, our list is full, so we'll have to allocate another
	   page of indirect structs.
	   We need to let m_get_new_page() know about the dest we're
	   about to add, so that it can do the right thing if it
	   happens to allocate a new page that equals dest.
	   So, temporarily add a record for this dest before the call */
 	struct monte_indirect_t tmp;
	tmp.dest = dest;
	tmp.src  = src;
	tmp.next = m_page_list; /* prepend it to the list (temporarily) */
	m_page_list = &tmp;	

	/* Now allocate a page to hold this new chunk of indirects */
	m = (struct monte_indirect_t *) m_get_new_page();
	if (!m) return -ENOMEM;

	/* Now we can remove the temporary element for this dest */
	m_page_list = tmp.next;

	/* Set up the next pointers on all the new entries. */
	/* Entries are laid out on the page in reverse order */
	m[0].next = 0;
	for (i=1; i < PAGE_SIZE/sizeof(struct monte_indirect_t); i++)
	    m[i].next = &m[i-1];

	m_unused_list = &m[i-1];
	m_ind_count++;

	/* It's possible our new page got relocated. */
	dest = tmp.dest;
	src  = tmp.src;
    }
    
    /* Grab the first record from the free list */
    m = m_unused_list;
    m_unused_list = m_unused_list->next;

    /* Populate the record */
    m->dest = dest;
    m->src  = src;

    /* Prepend the record to the existing m_page_list */
    m->next = m_page_list;
    m_page_list = m;

    m_page_count++;

    return 0;
}

/*------------------------------------------------------------------
 * Relocate an indirect record's source page (allocates
 * a new page to do this).
 * Returns 0 on success.
 *-----------------------------------------------------------------*/
static
int m_relocate_page(struct monte_indirect_t *m) {
    unsigned long pg;

    pg = m_get_new_page();
    if (pg == 0) return -ENOMEM;
    
    memcpy((void *) pg, (void *) m->src, PAGE_SIZE);
    m->src = pg;
    return 0;
}

/*------------------------------------------------------------------
 * Relocate an entire chunk of indirect records themselves.
 * A newly-allocate page num must be passed into the function (i.e.
 * the new page isn't allocated here).
 *-----------------------------------------------------------------*/
static
void m_relocate_ind_page(unsigned long addr, struct monte_indirect_t **list, unsigned long pg) {
    struct monte_indirect_t *m, *last=0, *new_m;
    /* This is nasty.... we want to move all the indirect structs off
     * a particular page and onto the same spot on another page. */
    m = *list;
    while (m) {
	if ((((unsigned long)m)&PAGE_MASK) == addr) {
	    /* Ok we want to move this indirect entry */
	    new_m = (void *)(pg + (((unsigned long)m)&~PAGE_MASK));
	    *new_m = *m;

	    /* Keep those next pointers up to date. */
	    if (last)
		last->next = new_m;
	    else
		*list = new_m;
	}
	last = m;
	m = m->next;
    }
}

/*------------------------------------------------------------------
 * This function is called if m_find_ind() has found that one or
 * more indirect records reside on the given page num.
 * In that case, we move all of them to a newly-allocate page.
 *-----------------------------------------------------------------*/
static
int m_relocate_ind(unsigned long addr) {
    unsigned long pg;
    struct monte_indirect_t *m, *last, *new_m;

    /* First off, grab a new page to move them to. */
    pg = m_get_new_page();
    if (pg == 0) return -ENOMEM;

    /* Go through our page list transplanting entries */
    m_relocate_ind_page(addr, &m_page_list, pg);
    m_relocate_ind_page(addr, &m_unused_list, pg);
    return 0;
}
	

/*------------------------------------------------------------------
 * Given a dest page num, return the page holding the data that
 * will be stuffed in that page.
 * Along the way, it deals with: allocating for unseen dest pages,
 * linking into the indirect list, relocating any indirect records
 * that may be sitting on the dest page, and relocating any
 * src pages that may be sitting on the dest page.
 *-----------------------------------------------------------------*/
static
unsigned long m_get_page(unsigned long addr) {
    int err;
    unsigned long pg;
    struct monte_indirect_t *m;

    /* First check if we have this page on our list somewhere */
    m = m_find_dest(addr);
    if (m) return m->src;

    /* This page isn't on our lists.  It's possible that we've already
     * used the page we want as an indirect page or as a source page
     * for another destination. */

    /* Check for indirect pages sitting on this address.  If so, move the indirect page. */
    m = m_find_ind(addr);
    if (m) {
	if ((err = m_relocate_ind(addr))) return err;
	m_add_page(addr,addr);
	return addr;
    }

    /* See if another page's source page is sitting on the dest page
     * we want to be in.  If so, relocate that page and stick
     * ourselves in that page. */
    m = m_find_src(addr);
    if (m) {
	/* Another data page is sitting on the spot we want to be.  We'll have to move it. */
	if ((err = m_relocate_page(m))) return err;
	m_add_page(addr,addr);
	return addr;
    }

    /* Ok, finally, the simple case. */
    pg = m_get_new_page();
    m_add_page(addr, pg);
    return pg;
}


/*------------------------------------------------------------------
 * Given a dest address, return the src address holding the
 * data that will be stuffed in that dest address.
 * This essentially just invokes m_get_page() above, but then
 * it returns the address (page+modulo) into that page.
 *-----------------------------------------------------------------*/
static
unsigned long m_get_addr(unsigned long addr) {
    unsigned long pg;
    pg = m_get_page(((unsigned long)addr)&PAGE_MASK);
    if (!pg) return 0;
    return (unsigned long)(pg + (((unsigned long)addr)&~PAGE_MASK));
}

static
void m_free_all_ind(struct monte_indirect_t **list) {
    unsigned long *ctr;
    struct monte_indirect_t *m, *next_m;

    /* There are 256 entries per page.  Use the dest field of the
     * first entry on a page as a counter.  Start deleting entries
     * from the lists.  When the counter on the page reaches zero,
     * free the indirect page. */

    m = *list;
    *list = 0;
    while (m) {
	next_m = m->next;
	/* Update the page counter */
	ctr = (unsigned long *)(((unsigned long)m)&PAGE_MASK);
	(*ctr)--;
	if (*ctr == 0) {
	    free_page((unsigned long)ctr);
	}
	m = next_m;
    }
}

static
void m_free_all(void) {
    struct monte_indirect_t *m;

    /* Pass 1: free all data pages and initialize the counters for pass 2 */
    m = m_page_list;
    while (m) {
	free_page(m->src);
	m->dest = PAGE_SIZE/sizeof(struct monte_indirect_t); /* init for pass2 */
	m = m->next;
    }

    /* Pass 2: free the indirect pages */
    m_free_all_ind(&m_page_list);
    m_free_all_ind(&m_unused_list);
}


/*--------------------------------------------------------------------
 * Kernel Image loading
 *------------------------------------------------------------------*/

/* Tidbits to allow easy file reads from kernel space */

/* embeem's trick that allows sys_call_table's location to vary 
 * a little.  init_module will start scanning for it at
 * system_utsname, and go upwards from there */
extern void  * system_utsname;
static void ** sys_call_table = &system_utsname;

#define k_sys_call3(rt,sys,t1,arg1,t2,arg2,t3,arg3) \
            (((rt (*)(t1,t2,t3)) \
              sys_call_table[(sys)])((arg1),(arg2),(arg3)))
#define k_sys_read(fd,buf,count) k_sys_call3(int,__NR_read,int,(fd),char*,(buf),size_t,(count))

static
ssize_t k_read(int fd, unsigned long buffer, size_t len) {
    int err;
    mm_segment_t oldfs;
    oldfs = get_fs(); set_fs(KERNEL_DS);
    err = k_sys_read(fd, (void *)buffer, len);
    set_fs(oldfs);
    return err;
}

static
ssize_t k_read_phys(int fd, unsigned long buffer, size_t bytes) {
    int len, r, bytes_read = 0;
    unsigned long dest;
    
    /* We have to be careful not to read across page boundaries. */

    while (bytes_read < bytes) {
	dest = m_get_addr(buffer);
	
	/* Read up until the end of the page only */
	len = PAGE_SIZE - (buffer & ~PAGE_MASK);
	if (len > bytes-bytes_read) len = bytes-bytes_read;

	r = k_read(fd, dest, len);
	if (r == 0) return bytes_read;
	if (r < 0)  return bytes_read ? bytes_read : r;

	bytes_read += r;
	buffer += r;
    }
    return bytes_read;
}

static
int monte_load_kernel(int fd) {
    int r;
    int len = m_setup->image_size - BOOT_BLOCK_SIZE;

    r = k_read_phys(fd, m_setup->load_addr, len);
    if (r < 0) return r;
    if (r != len) {
      printk("monte: Short read on image (got 0x%0x of 0x%0x expected bytes)\n",
	     r, len);
      return -EIO;
    }
    printk("monte: loaded kernel image (target load_addr=0x%0x, len=0x%0x) at 0x%0x\n",
	   m_setup->load_addr, len, m_get_addr(m_setup->load_addr));

    return 0;
}

static
void monte_pci_disable_bus(struct pci_bus *bus) {
    struct pci_dev *dev;
    u16 cmd;
    struct list_head *l;

    for (l=bus->devices.next; l != &bus->devices; l=l->next) {
	dev = pci_dev_b(l);
	//printk("Disabling bus master for devfn=%x on bus %x\n", dev->devfn, dev->bus->number);
	pcibios_read_config_word(dev->bus->number, dev->devfn, PCI_COMMAND, &cmd);
	cmd &= ~PCI_COMMAND_MASTER;
	pcibios_write_config_word(dev->bus->number, dev->devfn, PCI_COMMAND, cmd);
    }
}

static
void monte_pci_disable(void) {
    struct pci_bus *bus;
    struct list_head *l;
    /* Turn off PCI bus masters to keep them from scribbling on our
     * memory later on. */
    if (pcibios_present()) {
	for (l=pci_root_buses.next; l != &pci_root_buses; l=l->next)
	    monte_pci_disable_bus(pci_bus_b(l));
    }
}

static
void monte_sync_fs(void) {
  int i;
  kdev_t devp;
  struct super_block *sb;

  // Try to sync any rw-mounted filesystems */
  for (i=1; i<16; i++) {
    devp = MKDEV(HD_MAJOR,i);
    if ((sb=get_super(devp)) && !(sb->s_flags & MS_RDONLY))
      sync_dev(devp);
  }
}

/*--------------------------------------------------------------------
 *  Syscall interface
 *------------------------------------------------------------------*/
static int monte_restart(void);
int (*real_reboot)(int, int, int, void *);

asmlinkage int sys_monte(int magic1, int magic2, int cmd, void *arg) {
    int err, r;
    struct monte_param_t param;

    MOD_INC_USE_COUNT;
    if (magic1 != MONTE_MAGIC_1 || magic2 != MONTE_MAGIC_2) {
	err = real_reboot(magic1, magic2, cmd, arg);
	MOD_DEC_USE_COUNT;
	return err;
    }

    m_page_list   = 0;
    m_unused_list = 0;
    m_page_count  = 0;
    m_ind_count   = 0;

    if (!capable(CAP_SYS_BOOT)) {
	err = -EPERM;
	goto sys_monte_bail_out;
    }
#ifdef CONFIG_TIVO
    GetTivoConfig(kTivoConfigBoardID, &board_rev_id);
#endif

    /* Get all the arguments from the user */
    if (copy_from_user(&param, arg, sizeof(param))) {
	err = -EPERM;
	goto sys_monte_bail_out;
    }

    /* Load the kernel's boot header */
    /* Tag it such that at restart time, it will be copied to address MONTE_SETUP_BEGIN */
    r = k_read_phys(param.kernel_fd, MONTE_SETUP_BEGIN, BOOT_BLOCK_SIZE);
    if (r < 0) return r;
    if (r != BOOT_BLOCK_SIZE) {
	printk("monte: Couldn't read full boot block.\n");
	err = -EIO;
	goto sys_monte_bail_out;
    }

    if (m_setup->boot_flag != BS_SIG_VAL) { 
	printk("monte: Boot signature not found (%lx instead of %lx).\n",
	       m_setup->boot_flag, BS_SIG_VAL);
	err = -EINVAL;
	goto sys_monte_bail_out;
    }

    /* Copy the command line from user */
    if (strlen(param.command_line) > CMDLINE_SIZE) {
	err = -ENAMETOOLONG;
	goto sys_monte_bail_out;
    }
    err = strncpy_from_user(CMDLINE, param.command_line, CMDLINE_SIZE);
    if (err < 0) {
	err = -EFAULT;
	goto sys_monte_bail_out;
    }
    *(CMDLINE+CMDLINE_SIZE) = '\0';

    /* Load the kernel from the rest of the file */
    err = monte_load_kernel(param.kernel_fd);
    if (err) goto sys_monte_bail_out;

    /* Restart with a new kernel (from memory) */
    //printk("monte: restarting system...\n");
    err = monte_restart();

    /* If we come back some error ocurred...
       drop through to the error cleanup */
 sys_monte_bail_out:
    m_free_all();
    MOD_DEC_USE_COUNT;
    return err;
}

/*--------------------------------------------------------------------
 *  assembly code
 *------------------------------------------------------------------*/

extern void usetup(void) {
    __asm__ __volatile__ ( "    .set  noreorder       \n\t"
                           "   	li    $2,0x7000       \n\t"
                           "1: 	bgtz  $2, 1b          \n\t"
                           "   	addiu $2,$2,-1        \n\t"
                           "   	lui   $2,0xb510       \n\t"
			   "    sw    $0,0($2)        \n\t"
                           "   	li    $3,6            \n\t"
                           "   	sb    $3,0x148($2)    \n\t"
                           "   	li    $3,1            \n\t"
                           "   	sb    $3,0x148($2)    \n\t"
                           "   	li    $3,0x80         \n\t"
                           "   	sb    $3,0x14c($2)    \n\t"
                           "   	li    $3,18           \n\t"
                           "   	sb    $0,0x144($2)    \n\t"
                           "   	sb    $3,0x140($2)    \n\t"
                           "   	li    $3,3            \n\t"
                           "   	sb    $3,0x14c($2)    \n\t"
                           "    .set  reorder         \n\t" 
                           : : : "$2","$3");
}
extern void usetup_end(void) {}

extern void do_reload(unsigned long p) __attribute__((noreturn));
void do_reload(unsigned long p) {
  __asm__ __volatile__ (   "    .set    noreorder    \n\t"
			   "    move    $11,%0       \n\t"
			   "    move    $12,$31      \n\t"
			   "    lui     $2,0xb3e0    \n\t"
			   "    lui     $3,0x1070    \n\t"
			   "    ori     $3,$3,0x0080 \n\t"
			   "    li      $5,0x1015    \n\t"
			   "    sw      $3,0xcf8($2) \n\t"
			   "    sw      $5,0xcfc($2) \n\t"
			   "    mfc0    $7,$12       \n\t"
			   "    lui     $2,0x50      \n\t"
			   "    lui     $3,0x40      \n\t"
			   "    and     $7,$7,$2     \n\t"
			   "    or      $7,$7,$3     \n\t"
			   "    mtc0    $7,$12       \n\t"
			   "    nop                  \n\t"
			   "    nop                  \n\t"
			   "    lw      $9,0($11)    \n\t"
			   "1:  beqz    $9, 3f       \n\t"
			   "    nop                  \n\t"
			   "    lw      $3,0($9)     \n\t"
			   "    lw      $5,4($9)     \n\t"
			   "    addiu   $4,$3,0x1000 \n\t"
			   "2:  lw      $6,0($5)     \n\t"
			   "    sw      $6,0($3)     \n\t"
			   "    addiu   $3,$3,4      \n\t"
			   "    bne     $3,$4,2b     \n\t"
			   "    addiu   $5,$5,4      \n\t"
			   "    b       1b           \n\t"
			   "    lw      $9,8($9)     \n\t"
			   "3:  .set    mips3        \n\t"
			   "	lui	$2,0x8000    \n\t"
			   "	lui	$3,0x8000    \n\t"
			   "	ori	$3,$3,0x4000 \n\t"
			   "4:	cache	0x1,0($2)    \n\t"
			   "	cache	0x1,1($2)    \n\t"
			   "	addiu	$2,$2,32     \n\t"
			   "	bne	$2,$3,4b     \n\t"
                           "	nop                  \n\t"
			   "    mfc0    $7,$12       \n\t"
			   "    lui     $2,0x50      \n\t"
			   "    and     $7,$7,$2     \n\t"
			   "    mtc0    $7,$12       \n\t"
			   "    nop                  \n\t"
			   "    lw      $7,4($11)    \n\t"
			   "    addiu   $4,$7,32     \n\t"
			   "    lw      $5,8($11)    \n\t"
			   "    la      $6,0x5469566f\n\t"
			   "    lw      $2,16($7)    \n\t"
			   "    jr      $2           \n\t"
                           "	nop                  \n\t"
                           "   .set   reorder       \n\t" 
                           : : "r" (p) :
			   "$2","$3","$5","$6","$7");
  while (1);
}
extern void do_reload_end(void) {}
static void (*do_it)(unsigned long);

#define SZ(func) ((unsigned long)( func ## _end ) - (unsigned long)(func))

/*-------------------------------------------------------------------------
 * Machine restart code
 *-----------------------------------------------------------------------*/
typedef struct reloadparms {
  struct monte_indirect_t *plist;
  boot_block_t            *msetup;
  unsigned long            rev_id;
  unsigned long            jtable[5];
  unsigned long            stable[8];
  char                     sdata[192];
} reloadparms_t;

static
int monte_restart(void) {
    int err=0, i;
    unsigned long a_usetup, a_uart_print, a_uart_print_word, a_do_reload, pg;
    char *s, *d;
    reloadparms_t *p;
    struct monte_indirect_t *m;

    monte_sync_fs();
    flush_cache_all();
    monte_pci_disable();

    /* Now that the lists have all been constructed, the following call should
       yield a page that isn't equal to any of dest pages.  So it won't get clobbered */
    pg = m_get_new_page();
    if (!pg) return -ENOMEM;
    p = (reloadparms_t *)pg;

    printk("monte: total pages used: %d for image, %d for indirect tables, 1 for reload code\n",
	   m_page_count, m_ind_count);

    a_usetup = pg + sizeof(reloadparms_t);
    memcpy((void *)a_usetup, usetup, SZ(usetup));
    a_do_reload = a_usetup + SZ(usetup);
    memcpy((void *)a_do_reload, do_reload, SZ(do_reload));

    p->plist  = m_page_list;
    p->msetup = (boot_block_t*)MONTE_SETUP_BEGIN;
    p->rev_id = board_rev_id;
    p->jtable[0] = a_usetup;
    flush_cache_all();
    do_it = (void *)a_do_reload;
    cli();
    (*do_it)((unsigned long)p);

    /* NOT REACHED */
}


/*--------------------------------------------------------------------
 *  Module mechanics
 *------------------------------------------------------------------*/

int init_module(void) {
    unsigned char *sys_narg_table;

    /* We allow sys_call_table's location to vary 
       a little.  Start scanning for it at system_utsname, and go upwards */
    while (sys_call_table[__NR_close] != &sys_close) sys_call_table++;

    /* Found the sys_call_table.  Now we find the sys_narg_table because
       we know the size of sys_call_table is (__NR_Linux+ __NR_Linux_syscalls) */
    sys_narg_table = (char *)(sys_call_table + __NR_Linux+ __NR_Linux_syscalls + 1);

    /* Found the sys_narg_table.  Change the number of required args */
    sys_narg_table[__NR_reboot]=4; // require 4 args

    printk(KERN_INFO
           "monte: Two-kernel Monte for MIPS (Version %s)\n"
	   "monte: MuscleNerd (MIPS version), Erik Arjan Hendriks (x86 version)\n",
	   PACKAGE_VERSION);

    /* While this module is loaded, we intercept reboot sys_calls */
    real_reboot = sys_call_table[__NR_reboot];
    sys_call_table[__NR_reboot] = sys_monte;
    return 0;
}

void cleanup_module(void) {
    sys_call_table[__NR_reboot] = real_reboot;
}

