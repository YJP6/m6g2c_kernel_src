/*
 * Linux OS Independent Layer
 *
 * Copyright (C) 1999-2016, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: linux_osl.c 602478 2015-11-26 04:46:12Z $
 */

#define LINUX_PORT

#include <typedefs.h>
#include <bcmendian.h>
#include <linuxver.h>
#include <bcmdefs.h>
#include <dngl_stats.h>
#include <dhd.h>


#if !defined(STBLINUX)
#if defined(__ARM_ARCH_7A__) && !defined(DHD_USE_COHERENT_MEM_FOR_RING)
#include <asm/cacheflush.h>
#endif /* __ARM_ARCH_7A__ && !DHD_USE_COHERENT_MEM_FOR_RING */
#endif /* STBLINUX */

#include <linux/random.h>

#include <osl.h>
#include <bcmutils.h>
#include <linux/delay.h>
#include <pcicfg.h>


#ifdef BCM_SECURE_DMA
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/printk.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/moduleparam.h>
#include <asm/io.h>
#include <linux/skbuff.h>
#include <linux/vmalloc.h>
#include <linux/highmem.h>
#include <linux/dma-mapping.h>
#include <asm/memory.h>
#if defined(__ARM_ARCH_7A__)
#include <arch/arm/include/asm/tlbflush.h>
#include <arch/arm/mm/mm.h>
#endif
#include <linux/brcmstb/cma_driver.h>
#endif /* BCM_SECURE_DMA */

#include <linux/fs.h>


#ifdef BCM_OBJECT_TRACE
#include <bcmutils.h>
#endif /* BCM_OBJECT_TRACE */

#define PCI_CFG_RETRY		10

#define OS_HANDLE_MAGIC		0x1234abcd	/* Magic # to recognize osh */
#define BCM_MEM_FILENAME_LEN	24		/* Mem. filename length */
#define DUMPBUFSZ 1024

/* dependancy check */
#if !defined(BCMPCIE) && defined(DHD_USE_STATIC_CTRLBUF)
#error "DHD_USE_STATIC_CTRLBUF suppored PCIE target only"
#endif /* !BCMPCIE && DHD_USE_STATIC_CTRLBUF */

#ifdef CONFIG_DHD_USE_STATIC_BUF
#ifdef DHD_USE_STATIC_CTRLBUF
#define DHD_SKB_1PAGE_BUFSIZE	(PAGE_SIZE*1)
#define DHD_SKB_2PAGE_BUFSIZE	(PAGE_SIZE*2)
#define DHD_SKB_4PAGE_BUFSIZE	(PAGE_SIZE*4)

#define PREALLOC_FREE_MAGIC	0xFEDC
#define PREALLOC_USED_MAGIC	0xFCDE
#else
#define DHD_SKB_HDRSIZE		336
#define DHD_SKB_1PAGE_BUFSIZE	((PAGE_SIZE*1)-DHD_SKB_HDRSIZE)
#define DHD_SKB_2PAGE_BUFSIZE	((PAGE_SIZE*2)-DHD_SKB_HDRSIZE)
#define DHD_SKB_4PAGE_BUFSIZE	((PAGE_SIZE*4)-DHD_SKB_HDRSIZE)
#endif /* DHD_USE_STATIC_CTRLBUF */

#define STATIC_BUF_MAX_NUM	16
#define STATIC_BUF_SIZE	(PAGE_SIZE*2)
#define STATIC_BUF_TOTAL_LEN	(STATIC_BUF_MAX_NUM * STATIC_BUF_SIZE)

typedef struct bcm_static_buf {
	struct semaphore static_sem;
	unsigned char *buf_ptr;
	unsigned char buf_use[STATIC_BUF_MAX_NUM];
} bcm_static_buf_t;

static bcm_static_buf_t *bcm_static_buf = 0;

#ifdef DHD_USE_STATIC_CTRLBUF
#define STATIC_PKT_4PAGE_NUM	0
#define DHD_SKB_MAX_BUFSIZE	DHD_SKB_2PAGE_BUFSIZE
#elif defined(ENHANCED_STATIC_BUF)
#define STATIC_PKT_4PAGE_NUM	1
#define DHD_SKB_MAX_BUFSIZE	DHD_SKB_4PAGE_BUFSIZE
#else
#define STATIC_PKT_4PAGE_NUM	0
#define DHD_SKB_MAX_BUFSIZE	DHD_SKB_2PAGE_BUFSIZE
#endif /* DHD_USE_STATIC_CTRLBUF */

#ifdef DHD_USE_STATIC_CTRLBUF
#define STATIC_PKT_1PAGE_NUM	0
#define STATIC_PKT_2PAGE_NUM	64
#else
#define STATIC_PKT_1PAGE_NUM	8
#define STATIC_PKT_2PAGE_NUM	8
#endif /* DHD_USE_STATIC_CTRLBUF */

#define STATIC_PKT_1_2PAGE_NUM	\
	((STATIC_PKT_1PAGE_NUM) + (STATIC_PKT_2PAGE_NUM))
#define STATIC_PKT_MAX_NUM	\
	((STATIC_PKT_1_2PAGE_NUM) + (STATIC_PKT_4PAGE_NUM))

typedef struct bcm_static_pkt {
#ifdef DHD_USE_STATIC_CTRLBUF
	struct sk_buff *skb_8k[STATIC_PKT_2PAGE_NUM];
	unsigned char pkt_invalid[STATIC_PKT_2PAGE_NUM];
	spinlock_t osl_pkt_lock;
	uint32 last_allocated_index;
#else
	struct sk_buff *skb_4k[STATIC_PKT_1PAGE_NUM];
	struct sk_buff *skb_8k[STATIC_PKT_2PAGE_NUM];
#ifdef ENHANCED_STATIC_BUF
	struct sk_buff *skb_16k;
#endif /* ENHANCED_STATIC_BUF */
	struct semaphore osl_pkt_sem;
#endif /* DHD_USE_STATIC_CTRLBUF */
	unsigned char pkt_use[STATIC_PKT_MAX_NUM];
} bcm_static_pkt_t;

static bcm_static_pkt_t *bcm_static_skb = 0;

void* wifi_platform_prealloc(void *adapter, int section, unsigned long size);
#endif /* CONFIG_DHD_USE_STATIC_BUF */

typedef struct bcm_mem_link {
	struct bcm_mem_link *prev;
	struct bcm_mem_link *next;
	uint	size;
	int	line;
	void 	*osh;
	char	file[BCM_MEM_FILENAME_LEN];
} bcm_mem_link_t;

struct osl_cmn_info {
	atomic_t malloced;
	atomic_t pktalloced;    /* Number of allocated packet buffers */
	spinlock_t dbgmem_lock;
	bcm_mem_link_t *dbgmem_list;
	spinlock_t pktalloc_lock;
	atomic_t refcount; /* Number of references to this shared structure. */
};
typedef struct osl_cmn_info osl_cmn_t;

struct osl_info {
	osl_pubinfo_t pub;
	uint32  flags;		/* If specific cases to be handled in the OSL */
#ifdef CTFPOOL
	ctfpool_t *ctfpool;
#endif /* CTFPOOL */
	uint magic;
	void *pdev;
	uint failed;
	uint bustype;
	osl_cmn_t *cmn; /* Common OSL related data shred between two OSH's */

	void *bus_handle;
#ifdef BCMDBG_CTRACE
	spinlock_t ctrace_lock;
	struct list_head ctrace_list;
	int ctrace_num;
#endif /* BCMDBG_CTRACE */
#ifdef	BCM_SECURE_DMA
	struct cma_dev *cma;
	struct sec_mem_elem *sec_list_512;
	struct sec_mem_elem *sec_list_base_512;
	struct sec_mem_elem *sec_list_2048;
	struct sec_mem_elem *sec_list_base_2048;
	struct sec_mem_elem *sec_list_4096;
	struct sec_mem_elem *sec_list_base_4096;
	phys_addr_t  contig_base;
	void *contig_base_va;
	phys_addr_t  contig_base_alloc;
	void *contig_base_alloc_va;
	phys_addr_t contig_base_alloc_coherent;
	void *contig_base_alloc_coherent_va;
	phys_addr_t contig_delta_va_pa;
	struct {
		phys_addr_t pa;
		void *va;
		bool avail;
	} sec_cma_coherent[SEC_CMA_COHERENT_MAX];

#endif /* BCM_SECURE_DMA */
};
#ifdef BCM_SECURE_DMA
phys_addr_t g_contig_delta_va_pa;
static void osl_sec_dma_setup_contig_mem(osl_t *osh, unsigned long memsize, int regn);
static int osl_sec_dma_alloc_contig_mem(osl_t *osh, unsigned long memsize, int regn);
static void osl_sec_dma_free_contig_mem(osl_t *osh, u32 memsize, int regn);
static void * osl_sec_dma_ioremap(osl_t *osh, struct page *page, size_t size,
	bool iscache, bool isdecr);
static void osl_sec_dma_iounmap(osl_t *osh, void *contig_base_va, size_t size);
static void osl_sec_dma_init_elem_mem_block(osl_t *osh, size_t mbsize, int max,
	sec_mem_elem_t **list);
static void osl_sec_dma_deinit_elem_mem_block(osl_t *osh, size_t mbsize, int max,
	void *sec_list_base);
static sec_mem_elem_t * osl_sec_dma_alloc_mem_elem(osl_t *osh, void *va, uint size,
	int direction, struct sec_cma_info *ptr_cma_info, uint offset);
static void osl_sec_dma_free_mem_elem(osl_t *osh, sec_mem_elem_t *sec_mem_elem);
static void osl_sec_dma_init_consistent(osl_t *osh);
static void *osl_sec_dma_alloc_consistent(osl_t *osh, uint size, uint16 align_bits,
	ulong *pap);
static void osl_sec_dma_free_consistent(osl_t *osh, void *va, uint size, dmaaddr_t pa);
#endif /* BCM_SECURE_DMA */

#ifdef BCM_OBJECT_TRACE
/* don't clear the first 4 byte that is the pkt sn */
#define OSL_PKTTAG_CLEAR(p) \
do { \
	struct sk_buff *s = (struct sk_buff *)(p); \
	ASSERT(OSL_PKTTAG_SZ == 32); \
	*(uint32 *)(&s->cb[4]) = 0; \
	*(uint32 *)(&s->cb[8]) = 0; *(uint32 *)(&s->cb[12]) = 0; \
	*(uint32 *)(&s->cb[16]) = 0; *(uint32 *)(&s->cb[20]) = 0; \
	*(uint32 *)(&s->cb[24]) = 0; *(uint32 *)(&s->cb[28]) = 0; \
} while (0)
#else
#define OSL_PKTTAG_CLEAR(p) \
do { \
	struct sk_buff *s = (struct sk_buff *)(p); \
	ASSERT(OSL_PKTTAG_SZ == 32); \
	*(uint32 *)(&s->cb[0]) = 0; *(uint32 *)(&s->cb[4]) = 0; \
	*(uint32 *)(&s->cb[8]) = 0; *(uint32 *)(&s->cb[12]) = 0; \
	*(uint32 *)(&s->cb[16]) = 0; *(uint32 *)(&s->cb[20]) = 0; \
	*(uint32 *)(&s->cb[24]) = 0; *(uint32 *)(&s->cb[28]) = 0; \
} while (0)
#endif /* BCM_OBJECT_TRACE */

/* PCMCIA attribute space access macros */

/* Global ASSERT type flag */
uint32 g_assert_type = 1;
module_param(g_assert_type, int, 0);

static int16 linuxbcmerrormap[] =
{	0, 			/* 0 */
	-EINVAL,		/* BCME_ERROR */
	-EINVAL,		/* BCME_BADARG */
	-EINVAL,		/* BCME_BADOPTION */
	-EINVAL,		/* BCME_NOTUP */
	-EINVAL,		/* BCME_NOTDOWN */
	-EINVAL,		/* BCME_NOTAP */
	-EINVAL,		/* BCME_NOTSTA */
	-EINVAL,		/* BCME_BADKEYIDX */
	-EINVAL,		/* BCME_RADIOOFF */
	-EINVAL,		/* BCME_NOTBANDLOCKED */
	-EINVAL, 		/* BCME_NOCLK */
	-EINVAL, 		/* BCME_BADRATESET */
	-EINVAL, 		/* BCME_BADBAND */
	-E2BIG,			/* BCME_BUFTOOSHORT */
	-E2BIG,			/* BCME_BUFTOOLONG */
	-EBUSY, 		/* BCME_BUSY */
	-EINVAL, 		/* BCME_NOTASSOCIATED */
	-EINVAL, 		/* BCME_BADSSIDLEN */
	-EINVAL, 		/* BCME_OUTOFRANGECHAN */
	-EINVAL, 		/* BCME_BADCHAN */
	-EFAULT, 		/* BCME_BADADDR */
	-ENOMEM, 		/* BCME_NORESOURCE */
	-EOPNOTSUPP,		/* BCME_UNSUPPORTED */
	-EMSGSIZE,		/* BCME_BADLENGTH */
	-EINVAL,		/* BCME_NOTREADY */
	-EPERM,			/* BCME_EPERM */
	-ENOMEM, 		/* BCME_NOMEM */
	-EINVAL, 		/* BCME_ASSOCIATED */
	-ERANGE, 		/* BCME_RANGE */
	-EINVAL, 		/* BCME_NOTFOUND */
	-EINVAL, 		/* BCME_WME_NOT_ENABLED */
	-EINVAL, 		/* BCME_TSPEC_NOTFOUND */
	-EINVAL, 		/* BCME_ACM_NOTSUPPORTED */
	-EINVAL,		/* BCME_NOT_WME_ASSOCIATION */
	-EIO,			/* BCME_SDIO_ERROR */
	-ENODEV,		/* BCME_DONGLE_DOWN */
	-EINVAL,		/* BCME_VERSION */
	-EIO,			/* BCME_TXFAIL */
	-EIO,			/* BCME_RXFAIL */
	-ENODEV,		/* BCME_NODEVICE */
	-EINVAL,		/* BCME_NMODE_DISABLED */
	-ENODATA,		/* BCME_NONRESIDENT */
	-EINVAL,		/* BCME_SCANREJECT */
	-EINVAL,		/* BCME_USAGE_ERROR */
	-EIO,     		/* BCME_IOCTL_ERROR */
	-EIO,			/* BCME_SERIAL_PORT_ERR */
	-EOPNOTSUPP,	/* BCME_DISABLED, BCME_NOTENABLED */
	-EIO,			/* BCME_DECERR */
	-EIO,			/* BCME_ENCERR */
	-EIO,			/* BCME_MICERR */
	-ERANGE,		/* BCME_REPLAY */
	-EINVAL,		/* BCME_IE_NOTFOUND */
	-EINVAL,		/* BCME_DATA_NOTFOUND */

/* When an new error code is added to bcmutils.h, add os
 * specific error translation here as well
 */
/* check if BCME_LAST changed since the last time this function was updated */
#if BCME_LAST != -53
#error "You need to add a OS error translation in the linuxbcmerrormap \
	for new error code defined in bcmutils.h"
#endif
};
uint lmtest = FALSE;

/* translate bcmerrors into linux errors */
int
osl_error(int bcmerror)
{
	if (bcmerror > 0)
		bcmerror = 0;
	else if (bcmerror < BCME_LAST)
		bcmerror = BCME_ERROR;

	/* Array bounds covered by ASSERT in osl_attach */
	return linuxbcmerrormap[-bcmerror];
}

osl_t *
#ifdef SHARED_OSL_CMN
osl_attach(void *pdev, uint bustype, bool pkttag, void **osl_cmn)
#else
osl_attach(void *pdev, uint bustype, bool pkttag)
#endif /* SHARED_OSL_CMN */
{
#ifndef SHARED_OSL_CMN
	void **osl_cmn = NULL;
#endif /* SHARED_OSL_CMN */
	osl_t *osh;
	gfp_t flags;

	flags = CAN_SLEEP() ? GFP_KERNEL: GFP_ATOMIC;
	if (!(osh = kmalloc(sizeof(osl_t), flags)))
		return osh;

	ASSERT(osh);

	bzero(osh, sizeof(osl_t));

	if (osl_cmn == NULL || *osl_cmn == NULL) {
		if (!(osh->cmn = kmalloc(sizeof(osl_cmn_t), flags))) {
			kfree(osh);
			return NULL;
		}
		bzero(osh->cmn, sizeof(osl_cmn_t));
		if (osl_cmn)
			*osl_cmn = osh->cmn;
		atomic_set(&osh->cmn->malloced, 0);
		osh->cmn->dbgmem_list = NULL;
		spin_lock_init(&(osh->cmn->dbgmem_lock));

		spin_lock_init(&(osh->cmn->pktalloc_lock));

	} else {
		osh->cmn = *osl_cmn;
	}
	atomic_add(1, &osh->cmn->refcount);

	bcm_object_trace_init();

	/* Check that error map has the right number of entries in it */
	ASSERT(ABS(BCME_LAST) == (ARRAYSIZE(linuxbcmerrormap) - 1));

	osh->failed = 0;
	osh->pdev = pdev;
	osh->pub.pkttag = pkttag;
	osh->bustype = bustype;
	osh->magic = OS_HANDLE_MAGIC;
#ifdef BCM_SECURE_DMA

	osl_sec_dma_setup_contig_mem(osh, CMA_MEMBLOCK, CONT_ARMREGION);

	osh->contig_base_alloc_coherent_va = osl_sec_dma_ioremap(osh,
		phys_to_page((u32)osh->contig_base_alloc),
		CMA_DMA_DESC_MEMBLOCK, FALSE, TRUE);

	osh->contig_base_alloc_coherent = osh->contig_base_alloc;
	osl_sec_dma_init_consistent(osh);

	osh->contig_base_alloc += CMA_DMA_DESC_MEMBLOCK;

	osh->contig_base_alloc_va = osl_sec_dma_ioremap(osh,
		phys_to_page((u32)osh->contig_base_alloc), CMA_DMA_DATA_MEMBLOCK, TRUE, FALSE);
	osh->contig_base_va = osh->contig_base_alloc_va;

	/*
	* osl_sec_dma_init_elem_mem_block(osh, CMA_BUFSIZE_512, CMA_BUFNUM, &osh->sec_list_512);
	* osh->sec_list_base_512 = osh->sec_list_512;
	* osl_sec_dma_init_elem_mem_block(osh, CMA_BUFSIZE_2K, CMA_BUFNUM, &osh->sec_list_2048);
	* osh->sec_list_base_2048 = osh->sec_list_2048;
	*/
	osl_sec_dma_init_elem_mem_block(osh, CMA_BUFSIZE_4K, CMA_BUFNUM, &osh->sec_list_4096);
	osh->sec_list_base_4096 = osh->sec_list_4096;

#endif /* BCM_SECURE_DMA */

	switch (bustype) {
		case PCI_BUS:
		case SI_BUS:
		case PCMCIA_BUS:
			osh->pub.mmbus = TRUE;
			break;
		case JTAG_BUS:
		case SDIO_BUS:
		case USB_BUS:
		case SPI_BUS:
		case RPC_BUS:
			osh->pub.mmbus = FALSE;
			break;
		default:
			ASSERT(FALSE);
			break;
	}

#ifdef BCMDBG_CTRACE
	spin_lock_init(&osh->ctrace_lock);
	INIT_LIST_HEAD(&osh->ctrace_list);
	osh->ctrace_num = 0;
#endif /* BCMDBG_CTRACE */


	return osh;
}

int osl_static_mem_init(osl_t *osh, void *adapter)
{
#ifdef CONFIG_DHD_USE_STATIC_BUF
		if (!bcm_static_buf && adapter) {
			if (!(bcm_static_buf = (bcm_static_buf_t *)wifi_platform_prealloc(adapter,
				DHD_PREALLOC_OSL_BUF, STATIC_BUF_SIZE + STATIC_BUF_TOTAL_LEN))) {
				printk("can not alloc static buf!\n");
				bcm_static_skb = NULL;
				ASSERT(osh->magic == OS_HANDLE_MAGIC);
				return -ENOMEM;
			} else {
				printk("alloc static buf at %p!\n", bcm_static_buf);
			}

			sema_init(&bcm_static_buf->static_sem, 1);

			bcm_static_buf->buf_ptr = (unsigned char *)bcm_static_buf + STATIC_BUF_SIZE;
		}

#if defined(BCMSDIO) || defined(DHD_USE_STATIC_CTRLBUF)
		if (!bcm_static_skb && adapter) {
			int i;
			void *skb_buff_ptr = 0;
			bcm_static_skb = (bcm_static_pkt_t *)((char *)bcm_static_buf + 2048);
			skb_buff_ptr = wifi_platform_prealloc(adapter, DHD_PREALLOC_SKB_BUF, 0);
			if (!skb_buff_ptr) {
				printk("cannot alloc static buf!\n");
				bcm_static_buf = NULL;
				bcm_static_skb = NULL;
				ASSERT(osh->magic == OS_HANDLE_MAGIC);
				return -ENOMEM;
			}

			bcopy(skb_buff_ptr, bcm_static_skb, sizeof(struct sk_buff *) *
				(STATIC_PKT_MAX_NUM));
			for (i = 0; i < STATIC_PKT_MAX_NUM; i++) {
				bcm_static_skb->pkt_use[i] = 0;
			}

#ifdef DHD_USE_STATIC_CTRLBUF
			spin_lock_init(&bcm_static_skb->osl_pkt_lock);
			bcm_static_skb->last_allocated_index = 0;
#else
			sema_init(&bcm_static_skb->osl_pkt_sem, 1);
#endif /* DHD_USE_STATIC_CTRLBUF */
		}
#endif /* BCMSDIO || DHD_USE_STATIC_CTRLBUF */
#endif /* CONFIG_DHD_USE_STATIC_BUF */

	return 0;
}

void osl_set_bus_handle(osl_t *osh, void *bus_handle)
{
	osh->bus_handle = bus_handle;
}

void* osl_get_bus_handle(osl_t *osh)
{
	return osh->bus_handle;
}

void
osl_detach(osl_t *osh)
{
	if (osh == NULL)
		return;

#ifdef BCM_SECURE_DMA
	osl_sec_dma_free_contig_mem(osh, CMA_MEMBLOCK, CONT_ARMREGION);
	osl_sec_dma_deinit_elem_mem_block(osh, CMA_BUFSIZE_512, CMA_BUFNUM, osh->sec_list_base_512);
	osl_sec_dma_deinit_elem_mem_block(osh, CMA_BUFSIZE_2K, CMA_BUFNUM, osh->sec_list_base_2048);
	osl_sec_dma_deinit_elem_mem_block(osh, CMA_BUFSIZE_4K, CMA_BUFNUM, osh->sec_list_base_4096);
	osl_sec_dma_iounmap(osh, osh->contig_base_va, CMA_MEMBLOCK);
#endif /* BCM_SECURE_DMA */


	bcm_object_trace_deinit();

	ASSERT(osh->magic == OS_HANDLE_MAGIC);
	atomic_sub(1, &osh->cmn->refcount);
	if (atomic_read(&osh->cmn->refcount) == 0) {
			kfree(osh->cmn);
	}
	kfree(osh);
}

int osl_static_mem_deinit(osl_t *osh, void *adapter)
{
#ifdef CONFIG_DHD_USE_STATIC_BUF
	if (bcm_static_buf) {
		bcm_static_buf = 0;
	}
#ifdef BCMSDIO
	if (bcm_static_skb) {
		bcm_static_skb = 0;
	}
#endif /* BCMSDIO */
#endif /* CONFIG_DHD_USE_STATIC_BUF */
	return 0;
}

static struct sk_buff *osl_alloc_skb(osl_t *osh, unsigned int len)
{
	struct sk_buff *skb;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)
	gfp_t flags = (in_atomic() || irqs_disabled()) ? GFP_ATOMIC : GFP_KERNEL;
#if defined(CONFIG_SPARSEMEM) && defined(CONFIG_ZONE_DMA)
	flags |= GFP_ATOMIC;
#endif
#ifdef DHD_USE_ATOMIC_PKTGET
	flags = GFP_ATOMIC;
#endif /* DHD_USE_ATOMIC_PKTGET */
	skb = __dev_alloc_skb(len, flags);
#else
	skb = dev_alloc_skb(len);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25) */
	return skb;
}

#ifdef CTFPOOL

#ifdef CTFPOOL_SPINLOCK
#define CTFPOOL_LOCK(ctfpool, flags)	spin_lock_irqsave(&(ctfpool)->lock, flags)
#define CTFPOOL_UNLOCK(ctfpool, flags)	spin_unlock_irqrestore(&(ctfpool)->lock, flags)
#else
#define CTFPOOL_LOCK(ctfpool, flags)	spin_lock_bh(&(ctfpool)->lock)
#define CTFPOOL_UNLOCK(ctfpool, flags)	spin_unlock_bh(&(ctfpool)->lock)
#endif /* CTFPOOL_SPINLOCK */
/*
 * Allocate and add an object to packet pool.
 */
void *
osl_ctfpool_add(osl_t *osh)
{
	struct sk_buff *skb;
#ifdef CTFPOOL_SPINLOCK
	unsigned long flags;
#endif /* CTFPOOL_SPINLOCK */

	if ((osh == NULL) || (osh->ctfpool == NULL))
		return NULL;

	CTFPOOL_LOCK(osh->ctfpool, flags);
	ASSERT(osh->ctfpool->curr_obj <= osh->ctfpool->max_obj);

	/* No need to allocate more objects */
	if (osh->ctfpool->curr_obj == osh->ctfpool->max_obj) {
		CTFPOOL_UNLOCK(osh->ctfpool, flags);
		return NULL;
	}

	/* Allocate a new skb and add it to the ctfpool */
	skb = osl_alloc_skb(osh, osh->ctfpool->obj_size);
	if (skb == NULL) {
		printf("%s: skb alloc of len %d failed\n", __FUNCTION__,
		       osh->ctfpool->obj_size);
		CTFPOOL_UNLOCK(osh->ctfpool, flags);
		return NULL;
	}

	/* Add to ctfpool */
	skb->next = (struct sk_buff *)osh->ctfpool->head;
	osh->ctfpool->head = skb;
	osh->ctfpool->fast_frees++;
	osh->ctfpool->curr_obj++;

	/* Hijack a skb member to store ptr to ctfpool */
	CTFPOOLPTR(osh, skb) = (void *)osh->ctfpool;

	/* Use bit flag to indicate skb from fast ctfpool */
	PKTFAST(osh, skb) = FASTBUF;

	CTFPOOL_UNLOCK(osh->ctfpool, flags);

	return skb;
}

/*
 * Add new objects to the pool.
 */
void
osl_ctfpool_replenish(osl_t *osh, uint thresh)
{
	if ((osh == NULL) || (osh->ctfpool == NULL))
		return;

	/* Do nothing if no refills are required */
	while ((osh->ctfpool->refills > 0) && (thresh--)) {
		osl_ctfpool_add(osh);
		osh->ctfpool->refills--;
	}
}

/*
 * Initialize the packet pool with specified number of objects.
 */
int32
osl_ctfpool_init(osl_t *osh, uint numobj, uint size)
{
	gfp_t flags;

	flags = CAN_SLEEP() ? GFP_KERNEL: GFP_ATOMIC;
	osh->ctfpool = kzalloc(sizeof(ctfpool_t), flags);
	ASSERT(osh->ctfpool);

	osh->ctfpool->max_obj = numobj;
	osh->ctfpool->obj_size = size;

	spin_lock_init(&osh->ctfpool->lock);

	while (numobj--) {
		if (!osl_ctfpool_add(osh))
			return -1;
		osh->ctfpool->fast_frees--;
	}

	return 0;
}

/*
 * Cleanup the packet pool objects.
 */
void
osl_ctfpool_cleanup(osl_t *osh)
{
	struct sk_buff *skb, *nskb;
#ifdef CTFPOOL_SPINLOCK
	unsigned long flags;
#endif /* CTFPOOL_SPINLOCK */

	if ((osh == NULL) || (osh->ctfpool == NULL))
		return;

	CTFPOOL_LOCK(osh->ctfpool, flags);

	skb = osh->ctfpool->head;

	while (skb != NULL) {
		nskb = skb->next;
		dev_kfree_skb(skb);
		skb = nskb;
		osh->ctfpool->curr_obj--;
	}

	ASSERT(osh->ctfpool->curr_obj == 0);
	osh->ctfpool->head = NULL;
	CTFPOOL_UNLOCK(osh->ctfpool, flags);

	kfree(osh->ctfpool);
	osh->ctfpool = NULL;
}

void
osl_ctfpool_stats(osl_t *osh, void *b)
{
	struct bcmstrbuf *bb;

	if ((osh == NULL) || (osh->ctfpool == NULL))
		return;

#ifdef CONFIG_DHD_USE_STATIC_BUF
	if (bcm_static_buf) {
		bcm_static_buf = 0;
	}
#ifdef BCMSDIO
	if (bcm_static_skb) {
		bcm_static_skb = 0;
	}
#endif /* BCMSDIO */
#endif /* CONFIG_DHD_USE_STATIC_BUF */

	bb = b;

	ASSERT((osh != NULL) && (bb != NULL));

	bcm_bprintf(bb, "max_obj %d obj_size %d curr_obj %d refills %d\n",
	            osh->ctfpool->max_obj, osh->ctfpool->obj_size,
	            osh->ctfpool->curr_obj, osh->ctfpool->refills);
	bcm_bprintf(bb, "fast_allocs %d fast_frees %d slow_allocs %d\n",
	            osh->ctfpool->fast_allocs, osh->ctfpool->fast_frees,
	            osh->ctfpool->slow_allocs);
}

static inline struct sk_buff *
osl_pktfastget(osl_t *osh, uint len)
{
	struct sk_buff *skb;
#ifdef CTFPOOL_SPINLOCK
	unsigned long flags;
#endif /* CTFPOOL_SPINLOCK */

	/* Try to do fast allocate. Return null if ctfpool is not in use
	 * or if there are no items in the ctfpool.
	 */
	if (osh->ctfpool == NULL)
		return NULL;

	CTFPOOL_LOCK(osh->ctfpool, flags);
	if (osh->ctfpool->head == NULL) {
		ASSERT(osh->ctfpool->curr_obj == 0);
		osh->ctfpool->slow_allocs++;
		CTFPOOL_UNLOCK(osh->ctfpool, flags);
		return NULL;
	}

	if (len > osh->ctfpool->obj_size) {
		CTFPOOL_UNLOCK(osh->ctfpool, flags);
		return NULL;
	}

	ASSERT(len <= osh->ctfpool->obj_size);

	/* Get an object from ctfpool */
	skb = (struct sk_buff *)osh->ctfpool->head;
	osh->ctfpool->head = (void *)skb->next;

	osh->ctfpool->fast_allocs++;
	osh->ctfpool->curr_obj--;
	ASSERT(CTFPOOLHEAD(osh, skb) == (struct sock *)osh->ctfpool->head);
	CTFPOOL_UNLOCK(osh->ctfpool, flags);

	/* Init skb struct */
	skb->next = skb->prev = NULL;
#if defined(__ARM_ARCH_7A__)
	skb->data = skb->head + NET_SKB_PAD;
	skb->tail = skb->head + NET_SKB_PAD;
#else
	skb->data = skb->head + 16;
	skb->tail = skb->head + 16;
#endif /* __ARM_ARCH_7A__ */
	skb->len = 0;
	skb->cloned = 0;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 14)
	skb->list = NULL;
#endif
	atomic_set(&skb->users, 1);

	PKTSETCLINK(skb, NULL);
	PKTCCLRATTR(skb);
	PKTFAST(osh, skb) &= ~(CTFBUF | SKIPCT | CHAINED);

	return skb;
}
#endif /* CTFPOOL */

#if defined(BCM_GMAC3)
/* Account for a packet delivered to downstream forwarder.
 * Decrement a GMAC forwarder interface's pktalloced count.
 */
void BCMFASTPATH
osl_pkt_tofwder(osl_t *osh, void *skbs, int skb_cnt)
{

	atomic_sub(skb_cnt, &osh->cmn->pktalloced);
}

/* Account for a downstream forwarder delivered packet to a WL/DHD driver.
 * Increment a GMAC forwarder interface's pktalloced count.
 */
void BCMFASTPATH
#ifdef BCMDBG_CTRACE
osl_pkt_frmfwder(osl_t *osh, void *skbs, int skb_cnt, int line, char *file)
#else
osl_pkt_frmfwder(osl_t *osh, void *skbs, int skb_cnt)
#endif /* BCMDBG_CTRACE */
{
#if defined(BCMDBG_CTRACE)
	int i;
	struct sk_buff *skb;
#endif 

#if defined(BCMDBG_CTRACE)
	if (skb_cnt > 1) {
		struct sk_buff **skb_array = (struct sk_buff **)skbs;
		for (i = 0; i < skb_cnt; i++) {
			skb = skb_array[i];
#if defined(BCMDBG_CTRACE)
			ASSERT(!PKTISCHAINED(skb));
			ADD_CTRACE(osh, skb, file, line);
#endif /* BCMDBG_CTRACE */
		}
	} else {
		skb = (struct sk_buff *)skbs;
#if defined(BCMDBG_CTRACE)
		ASSERT(!PKTISCHAINED(skb));
		ADD_CTRACE(osh, skb, file, line);
#endif /* BCMDBG_CTRACE */
	}
#endif 

	atomic_add(skb_cnt, &osh->cmn->pktalloced);
}

#endif /* BCM_GMAC3 */

/* Convert a driver packet to native(OS) packet
 * In the process, packettag is zeroed out before sending up
 * IP code depends on skb->cb to be setup correctly with various options
 * In our case, that means it should be 0
 */
struct sk_buff * BCMFASTPATH
osl_pkt_tonative(osl_t *osh, void *pkt)
{
	struct sk_buff *nskb;
#ifdef BCMDBG_CTRACE
	struct sk_buff *nskb1, *nskb2;
#endif

	if (osh->pub.pkttag)
		OSL_PKTTAG_CLEAR(pkt);

	/* Decrement the packet counter */
	for (nskb = (struct sk_buff *)pkt; nskb; nskb = nskb->next) {
		atomic_sub(PKTISCHAINED(nskb) ? PKTCCNT(nskb) : 1, &osh->cmn->pktalloced);

#ifdef BCMDBG_CTRACE
		for (nskb1 = nskb; nskb1 != NULL; nskb1 = nskb2) {
			if (PKTISCHAINED(nskb1)) {
				nskb2 = PKTCLINK(nskb1);
			}
			else
				nskb2 = NULL;

			DEL_CTRACE(osh, nskb1);
		}
#endif /* BCMDBG_CTRACE */
	}
	return (struct sk_buff *)pkt;
}

/* Convert a native(OS) packet to driver packet.
 * In the process, native packet is destroyed, there is no copying
 * Also, a packettag is zeroed out
 */
void * BCMFASTPATH
#ifdef BCMDBG_CTRACE
osl_pkt_frmnative(osl_t *osh, void *pkt, int line, char *file)
#else
osl_pkt_frmnative(osl_t *osh, void *pkt)
#endif /* BCMDBG_CTRACE */
{
	struct sk_buff *nskb;
#ifdef BCMDBG_CTRACE
	struct sk_buff *nskb1, *nskb2;
#endif

	if (osh->pub.pkttag)
		OSL_PKTTAG_CLEAR(pkt);

	/* Increment the packet counter */
	for (nskb = (struct sk_buff *)pkt; nskb; nskb = nskb->next) {
		atomic_add(PKTISCHAINED(nskb) ? PKTCCNT(nskb) : 1, &osh->cmn->pktalloced);

#ifdef BCMDBG_CTRACE
		for (nskb1 = nskb; nskb1 != NULL; nskb1 = nskb2) {
			if (PKTISCHAINED(nskb1)) {
				nskb2 = PKTCLINK(nskb1);
			}
			else
				nskb2 = NULL;

			ADD_CTRACE(osh, nskb1, file, line);
		}
#endif /* BCMDBG_CTRACE */
	}
	return (void *)pkt;
}

/* Return a new packet. zero out pkttag */
void * BCMFASTPATH
#ifdef BCMDBG_CTRACE
osl_pktget(osl_t *osh, uint len, int line, char *file)
#else
#ifdef BCM_OBJECT_TRACE
osl_pktget(osl_t *osh, uint len, int line, const char *caller)
#else
osl_pktget(osl_t *osh, uint len)
#endif /* BCM_OBJECT_TRACE */
#endif /* BCMDBG_CTRACE */
{
	struct sk_buff *skb;
	uchar num = 0;
	if (lmtest != FALSE) {
		get_random_bytes(&num, sizeof(uchar));
		if ((num + 1) <= (256 * lmtest / 100))
			return NULL;
	}

#ifdef CTFPOOL
	/* Allocate from local pool */
	skb = osl_pktfastget(osh, len);
	if ((skb != NULL) || ((skb = osl_alloc_skb(osh, len)) != NULL))
#else /* CTFPOOL */
	if ((skb = osl_alloc_skb(osh, len)))
#endif /* CTFPOOL */
	{
		skb->tail += len;
		skb->len  += len;
		skb->priority = 0;

#ifdef BCMDBG_CTRACE
		ADD_CTRACE(osh, skb, file, line);
#endif
		atomic_inc(&osh->cmn->pktalloced);
#ifdef BCM_OBJECT_TRACE
		bcm_object_trace_opr(skb, BCM_OBJDBG_ADD_PKT, caller, line);
#endif /* BCM_OBJECT_TRACE */
	}

	return ((void*) skb);
}

#ifdef CTFPOOL
static inline void
osl_pktfastfree(osl_t *osh, struct sk_buff *skb)
{
	ctfpool_t *ctfpool;
#ifdef CTFPOOL_SPINLOCK
	unsigned long flags;
#endif /* CTFPOOL_SPINLOCK */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 14)
	skb->tstamp.tv.sec = 0;
#else
	skb->stamp.tv_sec = 0;
#endif

	/* We only need to init the fields that we change */
	skb->dev = NULL;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 36)
	skb->dst = NULL;
#endif
	OSL_PKTTAG_CLEAR(skb);
	skb->ip_summed = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
	skb_orphan(skb);
#else
	skb->destructor = NULL;
#endif

	ctfpool = (ctfpool_t *)CTFPOOLPTR(osh, skb);
	ASSERT(ctfpool != NULL);

	/* Add object to the ctfpool */
	CTFPOOL_LOCK(ctfpool, flags);
	skb->next = (struct sk_buff *)ctfpool->head;
	ctfpool->head = (void *)skb;

	ctfpool->fast_frees++;
	ctfpool->curr_obj++;

	ASSERT(ctfpool->curr_obj <= ctfpool->max_obj);
	CTFPOOL_UNLOCK(ctfpool, flags);
}
#endif /* CTFPOOL */

/* Free the driver packet. Free the tag if present */
void BCMFASTPATH
#ifdef BCM_OBJECT_TRACE
osl_pktfree(osl_t *osh, void *p, bool send, int line, const char *caller)
#else
osl_pktfree(osl_t *osh, void *p, bool send)
#endif /* BCM_OBJECT_TRACE */
{
	struct sk_buff *skb, *nskb;
	if (osh == NULL)
		return;

	skb = (struct sk_buff*) p;

	if (send && osh->pub.tx_fn)
		osh->pub.tx_fn(osh->pub.tx_ctx, p, 0);

	PKTDBG_TRACE(osh, (void *) skb, PKTLIST_PKTFREE);

#if defined(CONFIG_DHD_USE_STATIC_BUF) && defined(DHD_USE_STATIC_CTRLBUF)
	if (skb && (skb->mac_len == PREALLOC_USED_MAGIC)) {
		printk("%s: pkt %p is from static pool\n",
			__FUNCTION__, p);
		dump_stack();
		return;
	}

	if (skb && (skb->mac_len == PREALLOC_FREE_MAGIC)) {
		printk("%s: pkt %p is from static pool and not in used\n",
			__FUNCTION__, p);
		dump_stack();
		return;
	}
#endif /* CONFIG_DHD_USE_STATIC_BUF && DHD_USE_STATIC_CTRLBUF */

	/* perversion: we use skb->next to chain multi-skb packets */
	while (skb) {
		nskb = skb->next;
		skb->next = NULL;

#ifdef BCMDBG_CTRACE
		DEL_CTRACE(osh, skb);
#endif


#ifdef BCM_OBJECT_TRACE
		bcm_object_trace_opr(skb, BCM_OBJDBG_REMOVE, caller, line);
#endif /* BCM_OBJECT_TRACE */

#ifdef CTFPOOL
		if (PKTISFAST(osh, skb)) {
			if (atomic_read(&skb->users) == 1)
				smp_rmb();
			else if (!atomic_dec_and_test(&skb->users))
				goto next_skb;
			osl_pktfastfree(osh, skb);
		} else
#endif
		{
			dev_kfree_skb_any(skb);
		}
#ifdef CTFPOOL
next_skb:
#endif
		atomic_dec(&osh->cmn->pktalloced);
		skb = nskb;
	}
}

#ifdef CONFIG_DHD_USE_STATIC_BUF
void*
osl_pktget_static(osl_t *osh, uint len)
{
	int i = 0;
	struct sk_buff *skb;
#ifdef DHD_USE_STATIC_CTRLBUF
	unsigned long flags;
#endif /* DHD_USE_STATIC_CTRLBUF */

	if (!bcm_static_skb)
		return osl_pktget(osh, len);

	if (len > DHD_SKB_MAX_BUFSIZE) {
		printk("%s: attempt to allocate huge packet (0x%x)\n", __FUNCTION__, len);
		return osl_pktget(osh, len);
	}

#ifdef DHD_USE_STATIC_CTRLBUF
	spin_lock_irqsave(&bcm_static_skb->osl_pkt_lock, flags);

	if (len <= DHD_SKB_2PAGE_BUFSIZE) {
		uint32 index;
		for (i = 0; i < STATIC_PKT_2PAGE_NUM; i++) {
			index = bcm_static_skb->last_allocated_index % STATIC_PKT_2PAGE_NUM;
			bcm_static_skb->last_allocated_index++;
			if (bcm_static_skb->skb_8k[index] &&
				bcm_static_skb->pkt_use[index] == 0) {
				break;
			}
		}

		if ((i != STATIC_PKT_2PAGE_NUM) &&
			(index >= 0) && (index < STATIC_PKT_2PAGE_NUM)) {
			bcm_static_skb->pkt_use[index] = 1;
			skb = bcm_static_skb->skb_8k[index];
			skb->data = skb->head;
#ifdef NET_SKBUFF_DATA_USES_OFFSET
			skb_set_tail_pointer(skb, NET_SKB_PAD);
#else
			skb->tail = skb->data + NET_SKB_PAD;
#endif /* NET_SKBUFF_DATA_USES_OFFSET */
			skb->data += NET_SKB_PAD;
			skb->cloned = 0;
			skb->priority = 0;
#ifdef NET_SKBUFF_DATA_USES_OFFSET
			skb_set_tail_pointer(skb, len);
#else
			skb->tail = skb->data + len;
#endif /* NET_SKBUFF_DATA_USES_OFFSET */
			skb->len = len;
			skb->mac_len = PREALLOC_USED_MAGIC;
			spin_unlock_irqrestore(&bcm_static_skb->osl_pkt_lock, flags);
			return skb;
		}
	}

	spin_unlock_irqrestore(&bcm_static_skb->osl_pkt_lock, flags);
	printk("%s: all static pkt in use!\n", __FUNCTION__);
	return NULL;
#else
	down(&bcm_static_skb->osl_pkt_sem);

	if (len <= DHD_SKB_1PAGE_BUFSIZE) {
		for (i = 0; i < STATIC_PKT_MAX_NUM; i++) {
			if (bcm_static_skb->skb_4k[i] &&
				bcm_static_skb->pkt_use[i] == 0) {
				break;
			}
		}

		if (i != STATIC_PKT_MAX_NUM) {
			bcm_static_skb->pkt_use[i] = 1;

			skb = bcm_static_skb->skb_4k[i];
#ifdef NET_SKBUFF_DATA_USES_OFFSET
			skb_set_tail_pointer(skb, len);
#else
			skb->tail = skb->data + len;
#endif /* NET_SKBUFF_DATA_USES_OFFSET */
			skb->len = len;

			up(&bcm_static_skb->osl_pkt_sem);
			return skb;
		}
	}

	if (len <= DHD_SKB_2PAGE_BUFSIZE) {
		for (i = STATIC_PKT_1PAGE_NUM; i < STATIC_PKT_1_2PAGE_NUM; i++) {
			if (bcm_static_skb->skb_8k[i - STATIC_PKT_1PAGE_NUM] &&
				bcm_static_skb->pkt_use[i] == 0) {
				break;
			}
		}

		if ((i >= STATIC_PKT_1PAGE_NUM) && (i < STATIC_PKT_1_2PAGE_NUM)) {
			bcm_static_skb->pkt_use[i] = 1;
			skb = bcm_static_skb->skb_8k[i - STATIC_PKT_1PAGE_NUM];
#ifdef NET_SKBUFF_DATA_USES_OFFSET
			skb_set_tail_pointer(skb, len);
#else
			skb->tail = skb->data + len;
#endif /* NET_SKBUFF_DATA_USES_OFFSET */
			skb->len = len;

			up(&bcm_static_skb->osl_pkt_sem);
			return skb;
		}
	}

#if defined(ENHANCED_STATIC_BUF)
	if (bcm_static_skb->skb_16k &&
		bcm_static_skb->pkt_use[STATIC_PKT_MAX_NUM - 1] == 0) {
		bcm_static_skb->pkt_use[STATIC_PKT_MAX_NUM - 1] = 1;

		skb = bcm_static_skb->skb_16k;
#ifdef NET_SKBUFF_DATA_USES_OFFSET
		skb_set_tail_pointer(skb, len);
#else
		skb->tail = skb->data + len;
#endif /* NET_SKBUFF_DATA_USES_OFFSET */
		skb->len = len;

		up(&bcm_static_skb->osl_pkt_sem);
		return skb;
	}
#endif /* ENHANCED_STATIC_BUF */

	up(&bcm_static_skb->osl_pkt_sem);
	printk("%s: all static pkt in use!\n", __FUNCTION__);
	return osl_pktget(osh, len);
#endif /* DHD_USE_STATIC_CTRLBUF */
}

void
osl_pktfree_static(osl_t *osh, void *p, bool send)
{
	int i;
#ifdef DHD_USE_STATIC_CTRLBUF
	struct sk_buff *skb = (struct sk_buff *)p;
	unsigned long flags;
#endif /* DHD_USE_STATIC_CTRLBUF */

	if (!p) {
		return;
	}

	if (!bcm_static_skb) {
		osl_pktfree(osh, p, send);
		return;
	}

#ifdef DHD_USE_STATIC_CTRLBUF
	spin_lock_irqsave(&bcm_static_skb->osl_pkt_lock, flags);

	for (i = 0; i < STATIC_PKT_2PAGE_NUM; i++) {
		if (p == bcm_static_skb->skb_8k[i]) {
			if (bcm_static_skb->pkt_use[i] == 0) {
				printk("%s: static pkt idx %d(%p) is double free\n",
					__FUNCTION__, i, p);
			} else {
				bcm_static_skb->pkt_use[i] = 0;
			}

			if (skb->mac_len != PREALLOC_USED_MAGIC) {
				printk("%s: static pkt idx %d(%p) is not in used\n",
					__FUNCTION__, i, p);
			}

			skb->mac_len = PREALLOC_FREE_MAGIC;
			spin_unlock_irqrestore(&bcm_static_skb->osl_pkt_lock, flags);
			return;
		}
	}

	spin_unlock_irqrestore(&bcm_static_skb->osl_pkt_lock, flags);
	printk("%s: packet %p does not exist in the pool\n", __FUNCTION__, p);
#else
	down(&bcm_static_skb->osl_pkt_sem);
	for (i = 0; i < STATIC_PKT_1PAGE_NUM; i++) {
		if (p == bcm_static_skb->skb_4k[i]) {
			bcm_static_skb->pkt_use[i] = 0;
			up(&bcm_static_skb->osl_pkt_sem);
			return;
		}
	}

	for (i = STATIC_PKT_1PAGE_NUM; i < STATIC_PKT_1_2PAGE_NUM; i++) {
		if (p == bcm_static_skb->skb_8k[i - STATIC_PKT_1PAGE_NUM]) {
			bcm_static_skb->pkt_use[i] = 0;
			up(&bcm_static_skb->osl_pkt_sem);
			return;
		}
	}
#ifdef ENHANCED_STATIC_BUF
	if (p == bcm_static_skb->skb_16k) {
		bcm_static_skb->pkt_use[STATIC_PKT_MAX_NUM - 1] = 0;
		up(&bcm_static_skb->osl_pkt_sem);
		return;
	}
#endif
	up(&bcm_static_skb->osl_pkt_sem);
	osl_pktfree(osh, p, send);
#endif /* DHD_USE_STATIC_CTRLBUF */
}
#endif /* CONFIG_DHD_USE_STATIC_BUF */

#ifdef CONFIG_PCI
uint32
osl_pci_read_config(osl_t *osh, uint offset, uint size)
{
	uint val = 0;
	uint retry = PCI_CFG_RETRY;

	ASSERT((osh && (osh->magic == OS_HANDLE_MAGIC)));

	/* only 4byte access supported */
	ASSERT(size == 4);

	do {
		pci_read_config_dword(osh->pdev, offset, &val);
		if (val != 0xffffffff)
			break;
	} while (retry--);


	return (val);
}

void
osl_pci_write_config(osl_t *osh, uint offset, uint size, uint val)
{
	uint retry = PCI_CFG_RETRY;

	ASSERT((osh && (osh->magic == OS_HANDLE_MAGIC)));

	/* only 4byte access supported */
	ASSERT(size == 4);

	do {
		pci_write_config_dword(osh->pdev, offset, val);
		if (offset != PCI_BAR0_WIN)
			break;
		if (osl_pci_read_config(osh, offset, size) == val)
			break;
	} while (retry--);

}

/* return bus # for the pci device pointed by osh->pdev */
uint
osl_pci_bus(osl_t *osh)
{
	ASSERT(osh && (osh->magic == OS_HANDLE_MAGIC) && osh->pdev);

#if defined(__ARM_ARCH_7A__) && LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 35)
	return pci_domain_nr(((struct pci_dev *)osh->pdev)->bus);
#else
	return ((struct pci_dev *)osh->pdev)->bus->number;
#endif
}

/* return slot # for the pci device pointed by osh->pdev */
uint
osl_pci_slot(osl_t *osh)
{
	ASSERT(osh && (osh->magic == OS_HANDLE_MAGIC) && osh->pdev);

#if defined(__ARM_ARCH_7A__) && LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 35)
	return PCI_SLOT(((struct pci_dev *)osh->pdev)->devfn) + 1;
#else
	return PCI_SLOT(((struct pci_dev *)osh->pdev)->devfn);
#endif
}

/* return domain # for the pci device pointed by osh->pdev */
uint
osl_pcie_domain(osl_t *osh)
{
	ASSERT(osh && (osh->magic == OS_HANDLE_MAGIC) && osh->pdev);

	return pci_domain_nr(((struct pci_dev *)osh->pdev)->bus);
}

/* return bus # for the pci device pointed by osh->pdev */
uint
osl_pcie_bus(osl_t *osh)
{
	ASSERT(osh && (osh->magic == OS_HANDLE_MAGIC) && osh->pdev);

	return ((struct pci_dev *)osh->pdev)->bus->number;
}

/* return the pci device pointed by osh->pdev */
struct pci_dev *
osl_pci_device(osl_t *osh)
{
	ASSERT(osh && (osh->magic == OS_HANDLE_MAGIC) && osh->pdev);

	return osh->pdev;
}

static void
osl_pcmcia_attr(osl_t *osh, uint offset, char *buf, int size, bool write)
{
}

void
osl_pcmcia_read_attr(osl_t *osh, uint offset, void *buf, int size)
{
	osl_pcmcia_attr(osh, offset, (char *) buf, size, FALSE);
}

void
osl_pcmcia_write_attr(osl_t *osh, uint offset, void *buf, int size)
{
	osl_pcmcia_attr(osh, offset, (char *) buf, size, TRUE);
}
#endif		//end of CONFIG_PCI

void *
osl_malloc(osl_t *osh, uint size)
{
	void *addr;
	gfp_t flags;

	/* only ASSERT if osh is defined */
	if (osh)
		ASSERT(osh->magic == OS_HANDLE_MAGIC);
#ifdef CONFIG_DHD_USE_STATIC_BUF
	if (bcm_static_buf)
	{
		int i = 0;
		if ((size >= PAGE_SIZE)&&(size <= STATIC_BUF_SIZE))
		{
			down(&bcm_static_buf->static_sem);

			for (i = 0; i < STATIC_BUF_MAX_NUM; i++)
			{
				if (bcm_static_buf->buf_use[i] == 0)
					break;
			}

			if (i == STATIC_BUF_MAX_NUM)
			{
				up(&bcm_static_buf->static_sem);
				printk("all static buff in use!\n");
				goto original;
			}

			bcm_static_buf->buf_use[i] = 1;
			up(&bcm_static_buf->static_sem);

			bzero(bcm_static_buf->buf_ptr+STATIC_BUF_SIZE*i, size);
			if (osh)
				atomic_add(size, &osh->cmn->malloced);

			return ((void *)(bcm_static_buf->buf_ptr+STATIC_BUF_SIZE*i));
		}
	}
original:
#endif /* CONFIG_DHD_USE_STATIC_BUF */

	flags = CAN_SLEEP() ? GFP_KERNEL: GFP_ATOMIC;
	if ((addr = kmalloc(size, flags)) == NULL) {
		if (osh)
			osh->failed++;
		return (NULL);
	}
	if (osh && osh->cmn)
		atomic_add(size, &osh->cmn->malloced);

	return (addr);
}

void *
osl_mallocz(osl_t *osh, uint size)
{
	void *ptr;

	ptr = osl_malloc(osh, size);

	if (ptr != NULL) {
		bzero(ptr, size);
	}

	return ptr;
}

void
osl_mfree(osl_t *osh, void *addr, uint size)
{
#ifdef CONFIG_DHD_USE_STATIC_BUF
	if (bcm_static_buf)
	{
		if ((addr > (void *)bcm_static_buf) && ((unsigned char *)addr
			<= ((unsigned char *)bcm_static_buf + STATIC_BUF_TOTAL_LEN)))
		{
			int buf_idx = 0;

			buf_idx = ((unsigned char *)addr - bcm_static_buf->buf_ptr)/STATIC_BUF_SIZE;

			down(&bcm_static_buf->static_sem);
			bcm_static_buf->buf_use[buf_idx] = 0;
			up(&bcm_static_buf->static_sem);

			if (osh && osh->cmn) {
				ASSERT(osh->magic == OS_HANDLE_MAGIC);
				atomic_sub(size, &osh->cmn->malloced);
			}
			return;
		}
	}
#endif /* CONFIG_DHD_USE_STATIC_BUF */
	if (osh && osh->cmn) {
		ASSERT(osh->magic == OS_HANDLE_MAGIC);

		ASSERT(size <= osl_malloced(osh));

		atomic_sub(size, &osh->cmn->malloced);
	}
	kfree(addr);
}

uint
osl_check_memleak(osl_t *osh)
{
	ASSERT((osh && (osh->magic == OS_HANDLE_MAGIC)));
	if (atomic_read(&osh->cmn->refcount) == 1)
		return (atomic_read(&osh->cmn->malloced));
	else
		return 0;
}

uint
osl_malloced(osl_t *osh)
{
	ASSERT((osh && (osh->magic == OS_HANDLE_MAGIC)));
	return (atomic_read(&osh->cmn->malloced));
}

uint
osl_malloc_failed(osl_t *osh)
{
	ASSERT((osh && (osh->magic == OS_HANDLE_MAGIC)));
	return (osh->failed);
}


uint
osl_dma_consistent_align(void)
{
	return (PAGE_SIZE);
}

void*
osl_dma_alloc_consistent(osl_t *osh, uint size, uint16 align_bits, uint *alloced, dmaaddr_t *pap)
{
	void *va;
	uint16 align = (1 << align_bits);
	ASSERT((osh && (osh->magic == OS_HANDLE_MAGIC)));

	if (!ISALIGNED(DMA_CONSISTENT_ALIGN, align))
		size += align;
	*alloced = size;

#ifndef	BCM_SECURE_DMA
#if defined(__ARM_ARCH_7A__) && !defined(DHD_USE_COHERENT_MEM_FOR_RING)
	va = kmalloc(size, GFP_ATOMIC | __GFP_ZERO);
	if (va)
		*pap = (ulong)__virt_to_phys((ulong)va);
#else
	{
		dma_addr_t pap_lin;
		struct pci_dev *hwdev = osh->pdev;
		gfp_t flags;
#ifdef DHD_ALLOC_COHERENT_MEM_FROM_ATOMIC_POOL
		flags = GFP_ATOMIC;
#else
		flags = GFP_KERNEL;
#endif /* DHD_ALLOC_COHERENT_MEM_FROM_ATOMIC_POOL */
		va = dma_alloc_coherent(&hwdev->dev, size, &pap_lin, flags);
#ifdef BCMDMA64OSL
		PHYSADDRLOSET(*pap, pap_lin & 0xffffffff);
		PHYSADDRHISET(*pap, (pap_lin >> 32) & 0xffffffff);
#else
		*pap = (dmaaddr_t)pap_lin;
#endif /* BCMDMA64OSL */
	}
#endif /* __ARM_ARCH_7A__ && !DHD_USE_COHERENT_MEM_FOR_RING */
#else
	va = osl_sec_dma_alloc_consistent(osh, size, align_bits, pap);
#endif /* BCM_SECURE_DMA */
	return va;
}

void
osl_dma_free_consistent(osl_t *osh, void *va, uint size, dmaaddr_t pa)
{
#ifdef BCMDMA64OSL
	dma_addr_t paddr;
#endif /* BCMDMA64OSL */
	ASSERT((osh && (osh->magic == OS_HANDLE_MAGIC)));

#ifndef BCM_SECURE_DMA
#if defined(__ARM_ARCH_7A__) && !defined(DHD_USE_COHERENT_MEM_FOR_RING)
	kfree(va);
#else
#ifdef BCMDMA64OSL
	PHYSADDRTOULONG(pa, paddr);
	pci_free_consistent(osh->pdev, size, va, paddr);
#else
	pci_free_consistent(osh->pdev, size, va, (dma_addr_t)pa);
#endif /* BCMDMA64OSL */
#endif /* __ARM_ARCH_7A__ && !DHD_USE_COHERENT_MEM_FOR_RING */
#else
	osl_sec_dma_free_consistent(osh, va, size, pa);
#endif /* BCM_SECURE_DMA */
}

dmaaddr_t BCMFASTPATH
osl_dma_map(osl_t *osh, void *va, uint size, int direction, void *p, hnddma_seg_map_t *dmah)
{
	int dir;
#ifdef BCMDMA64OSL
	dmaaddr_t ret;
	dma_addr_t  map_addr;
#endif /* BCMDMA64OSL */

	ASSERT((osh && (osh->magic == OS_HANDLE_MAGIC)));
	dir = (direction == DMA_TX)? PCI_DMA_TODEVICE: PCI_DMA_FROMDEVICE;




#ifdef BCMDMA64OSL
	map_addr = pci_map_single(osh->pdev, va, size, dir);
	PHYSADDRLOSET(ret, map_addr & 0xffffffff);
	PHYSADDRHISET(ret, (map_addr >> 32) & 0xffffffff);
	return ret;
#else
	return (pci_map_single(osh->pdev, va, size, dir));
#endif /* BCMDMA64OSL */
}

void BCMFASTPATH
osl_dma_unmap(osl_t *osh, dmaaddr_t pa, uint size, int direction)
{
	int dir;
#ifdef BCMDMA64OSL
	dma_addr_t paddr;
#endif /* BCMDMA64OSL */

	ASSERT((osh && (osh->magic == OS_HANDLE_MAGIC)));


	dir = (direction == DMA_TX)? PCI_DMA_TODEVICE: PCI_DMA_FROMDEVICE;
#ifdef BCMDMA64OSL
	PHYSADDRTOULONG(pa, paddr);
	pci_unmap_single(osh->pdev, paddr, size, dir);
#else
	pci_unmap_single(osh->pdev, (uint32)pa, size, dir);
#endif /* BCMDMA64OSL */
}

/* OSL function for CPU relax */
inline void BCMFASTPATH
osl_cpu_relax(void)
{
	cpu_relax();
}


#if (defined(__ARM_ARCH_7A__) && !defined(DHD_USE_COHERENT_MEM_FOR_RING) || \
	defined(CONFIG_ARCH_MSM8996) || defined(CONFIG_SOC_EXYNOS8890))

#include <asm/dma-mapping.h>

/*
 * Note that its gauranteed that the Ring is cache line aligned, but
 * the messages are not. And we see that __dma_inv_range in
 * arch/arm64/mm/cache.S invalidates only if the request size is
 * cache line aligned. If not, it will Clean and invalidate.
 * So we'll better invalidate the whole ring.
 *
 * Also, the latest Kernel versions invoke cache maintenance operations
 * from arch/arm64/mm/dma-mapping.c, __swiotlb_sync_single_for_device
 * Only if is_device_dma_coherent returns 0. Since we don't have BSP
 * source, assuming that its the case, since we pass NULL for the dev ptr
 */
inline void BCMFASTPATH
osl_cache_flush(void *va, uint size)
{
	/*
	 * using long for address arithmatic is OK, in linux
	 * 32 bit its 4 bytes and 64 bit its 8 bytes
	 */
	unsigned long end_cache_line_start;
	unsigned long end_addr;
	unsigned long next_cache_line_start;

	end_addr = (unsigned long)va + size;

	/* Start address beyond the cache line we plan to operate */
	end_cache_line_start = (end_addr & ~(L1_CACHE_BYTES - 1));
	next_cache_line_start = end_cache_line_start + L1_CACHE_BYTES;

	/* Align the start address to cache line boundary */
	va = (void *)((unsigned long)va & ~(L1_CACHE_BYTES - 1));

	/* Ensure that size is also aligned and extends partial line to full */
	size = next_cache_line_start - (unsigned long)va;

#ifndef BCM_SECURE_DMA

#ifdef CONFIG_ARM64
	/*
	 * virt_to_dma is not present in arm64/include/dma-mapping.h
	 * So have to convert the va to pa first and then get the dma addr
	 * of the same.
	 */
	{
		phys_addr_t pa;
		dma_addr_t dma_addr;
		pa = virt_to_phys(va);
		dma_addr = phys_to_dma(NULL, pa);
		if (size > 0)
			dma_sync_single_for_device(OSH_NULL, dma_addr, size, DMA_TX);
	}
#else
	if (size > 0)
		dma_sync_single_for_device(OSH_NULL, virt_to_dma(OSH_NULL, va), size, DMA_TX);
#endif /* !CONFIG_ARM64 */
#else
	phys_addr_t orig_pa = (phys_addr_t)(va - g_contig_delta_va_pa);
	if (size > 0)
		dma_sync_single_for_device(OSH_NULL, orig_pa, size, DMA_TX);
#endif /* defined BCM_SECURE_DMA */
}

inline void BCMFASTPATH
osl_cache_inv(void *va, uint size)
{
	/*
	 * using long for address arithmatic is OK, in linux
	 * 32 bit its 4 bytes and 64 bit its 8 bytes
	 */
	unsigned long end_cache_line_start;
	unsigned long end_addr;
	unsigned long next_cache_line_start;

	end_addr = (unsigned long)va + size;

	/* Start address beyond the cache line we plan to operate */
	end_cache_line_start = (end_addr & ~(L1_CACHE_BYTES - 1));
	next_cache_line_start = end_cache_line_start + L1_CACHE_BYTES;

	/* Align the start address to cache line boundary */
	va = (void *)((unsigned long)va & ~(L1_CACHE_BYTES - 1));

	/* Ensure that size is also aligned and extends partial line to full */
	size = next_cache_line_start - (unsigned long)va;

#ifndef BCM_SECURE_DMA

#ifdef CONFIG_ARM64
	/*
	 * virt_to_dma is not present in arm64/include/dma-mapping.h
	 * So have to convert the va to pa first and then get the dma addr
	 * of the same.
	 */
	{
		phys_addr_t pa;
		dma_addr_t dma_addr;
		pa = virt_to_phys(va);
		dma_addr = phys_to_dma(NULL, pa);
		dma_sync_single_for_cpu(OSH_NULL, dma_addr, size, DMA_RX);
	}
#else
	dma_sync_single_for_cpu(OSH_NULL, virt_to_dma(OSH_NULL, va), size, DMA_RX);
#endif /* !CONFIG_ARM64 */
#else
	phys_addr_t orig_pa = (phys_addr_t)(va - g_contig_delta_va_pa);
	dma_sync_single_for_cpu(OSH_NULL, orig_pa, size, DMA_RX);
#endif /* defined BCM_SECURE_DMA */
}

inline void osl_prefetch(const void *ptr)
{
	/* PLD instruction is not applicable in ARM 64. We don't care for now */
#ifndef CONFIG_ARM64
	__asm__ __volatile__("pld\t%0" :: "o"(*(const char *)ptr) : "cc");
#endif
}

int osl_arch_is_coherent(void)
{
	return 0;
}


inline int osl_acp_war_enab(void)
{
	return 0;
}

#endif 

#if defined(BCMASSERT_LOG)
void
osl_assert(const char *exp, const char *file, int line)
{
	char tempbuf[256];
	const char *basename;

	basename = strrchr(file, '/');
	/* skip the '/' */
	if (basename)
		basename++;

	if (!basename)
		basename = file;

#ifdef BCMASSERT_LOG
	snprintf(tempbuf, 64, "\"%s\": file \"%s\", line %d\n",
		exp, basename, line);
#endif /* BCMASSERT_LOG */


#if defined(BCMASSERT_LOG)
	switch (g_assert_type) {
	case 0:
		panic("%s", tempbuf);
		break;
	case 1:
		printk("%s", tempbuf);
		break;
	case 2:
		printk("%s", tempbuf);
		BUG();
		break;
	default:
		break;
	}
#endif 

}
#endif 

void
osl_delay(uint usec)
{
	uint d;

	while (usec > 0) {
		d = MIN(usec, 1000);
		udelay(d);
		usec -= d;
	}
}

void
osl_sleep(uint ms)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
	if (ms < 20)
		usleep_range(ms*1000, ms*1000 + 1000);
	else
#endif
	msleep(ms);
}



/* Clone a packet.
 * The pkttag contents are NOT cloned.
 */
void *
#ifdef BCMDBG_CTRACE
osl_pktdup(osl_t *osh, void *skb, int line, char *file)
#else
#ifdef BCM_OBJECT_TRACE
osl_pktdup(osl_t *osh, void *skb, int line, const char *caller)
#else
osl_pktdup(osl_t *osh, void *skb)
#endif /* BCM_OBJECT_TRACE */
#endif /* BCMDBG_CTRACE */
{
	void * p;

	ASSERT(!PKTISCHAINED(skb));

	/* clear the CTFBUF flag if set and map the rest of the buffer
	 * before cloning.
	 */
	PKTCTFMAP(osh, skb);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
	if ((p = pskb_copy((struct sk_buff *)skb, GFP_ATOMIC)) == NULL)
#else
	if ((p = skb_clone((struct sk_buff *)skb, GFP_ATOMIC)) == NULL)
#endif
		return NULL;

#ifdef CTFPOOL
	if (PKTISFAST(osh, skb)) {
		ctfpool_t *ctfpool;

		/* if the buffer allocated from ctfpool is cloned then
		 * we can't be sure when it will be freed. since there
		 * is a chance that we will be losing a buffer
		 * from our pool, we increment the refill count for the
		 * object to be alloced later.
		 */
		ctfpool = (ctfpool_t *)CTFPOOLPTR(osh, skb);
		ASSERT(ctfpool != NULL);
		PKTCLRFAST(osh, p);
		PKTCLRFAST(osh, skb);
		ctfpool->refills++;
	}
#endif /* CTFPOOL */

	/* Clear PKTC  context */
	PKTSETCLINK(p, NULL);
	PKTCCLRFLAGS(p);
	PKTCSETCNT(p, 1);
	PKTCSETLEN(p, PKTLEN(osh, skb));

	/* skb_clone copies skb->cb.. we don't want that */
	if (osh->pub.pkttag)
		OSL_PKTTAG_CLEAR(p);

	/* Increment the packet counter */
	atomic_inc(&osh->cmn->pktalloced);
#ifdef BCM_OBJECT_TRACE
	bcm_object_trace_opr(p, BCM_OBJDBG_ADD_PKT, caller, line);
#endif /* BCM_OBJECT_TRACE */

#ifdef BCMDBG_CTRACE
	ADD_CTRACE(osh, (struct sk_buff *)p, file, line);
#endif
	return (p);
}

#ifdef BCMDBG_CTRACE
int osl_pkt_is_frmnative(osl_t *osh, struct sk_buff *pkt)
{
	unsigned long flags;
	struct sk_buff *skb;
	int ck = FALSE;

	spin_lock_irqsave(&osh->ctrace_lock, flags);

	list_for_each_entry(skb, &osh->ctrace_list, ctrace_list) {
		if (pkt == skb) {
			ck = TRUE;
			break;
		}
	}

	spin_unlock_irqrestore(&osh->ctrace_lock, flags);
	return ck;
}

void osl_ctrace_dump(osl_t *osh, struct bcmstrbuf *b)
{
	unsigned long flags;
	struct sk_buff *skb;
	int idx = 0;
	int i, j;

	spin_lock_irqsave(&osh->ctrace_lock, flags);

	if (b != NULL)
		bcm_bprintf(b, " Total %d sbk not free\n", osh->ctrace_num);
	else
		printk(" Total %d sbk not free\n", osh->ctrace_num);

	list_for_each_entry(skb, &osh->ctrace_list, ctrace_list) {
		if (b != NULL)
			bcm_bprintf(b, "[%d] skb %p:\n", ++idx, skb);
		else
			printk("[%d] skb %p:\n", ++idx, skb);

		for (i = 0; i < skb->ctrace_count; i++) {
			j = (skb->ctrace_start + i) % CTRACE_NUM;
			if (b != NULL)
				bcm_bprintf(b, "    [%s(%d)]\n", skb->func[j], skb->line[j]);
			else
				printk("    [%s(%d)]\n", skb->func[j], skb->line[j]);
		}
		if (b != NULL)
			bcm_bprintf(b, "\n");
		else
			printk("\n");
	}

	spin_unlock_irqrestore(&osh->ctrace_lock, flags);

	return;
}
#endif /* BCMDBG_CTRACE */


/*
 * OSLREGOPS specifies the use of osl_XXX routines to be used for register access
 */

/*
 * BINOSL selects the slightly slower function-call-based binary compatible osl.
 */

uint
osl_pktalloced(osl_t *osh)
{
	if (atomic_read(&osh->cmn->refcount) == 1)
		return (atomic_read(&osh->cmn->pktalloced));
	else
		return 0;
}

uint32
osl_rand(void)
{
	uint32 rand;

	get_random_bytes(&rand, sizeof(rand));

	return rand;
}

/* Linux Kernel: File Operations: start */
void *
osl_os_open_image(char *filename)
{
	struct file *fp;

	fp = filp_open(filename, O_RDONLY, 0);
	/*
	 * 2.6.11 (FC4) supports filp_open() but later revs don't?
	 * Alternative:
	 * fp = open_namei(AT_FDCWD, filename, O_RD, 0);
	 * ???
	 */
	 if (IS_ERR(fp))
		 fp = NULL;

	 return fp;
}

int
osl_os_get_image_block(char *buf, int len, void *image)
{
	struct file *fp = (struct file *)image;
	int rdlen;

	if (!image)
		return 0;

	rdlen = kernel_read(fp, fp->f_pos, buf, len);
	if (rdlen > 0)
		fp->f_pos += rdlen;

	return rdlen;
}

void
osl_os_close_image(void *image)
{
	if (image)
		filp_close((struct file *)image, NULL);
}

int
osl_os_image_size(void *image)
{
	int len = 0, curroffset;

	if (image) {
		/* store the current offset */
		curroffset = generic_file_llseek(image, 0, 1);
		/* goto end of file to get length */
		len = generic_file_llseek(image, 0, 2);
		/* restore back the offset */
		generic_file_llseek(image, curroffset, 0);
	}
	return len;
}

/* Linux Kernel: File Operations: end */


/* APIs to set/get specific quirks in OSL layer */
void
osl_flag_set(osl_t *osh, uint32 mask)
{
	osh->flags |= mask;
}

bool
osl_is_flag_set(osl_t *osh, uint32 mask)
{
	return (osh->flags & mask);
}

#ifdef BCM_SECURE_DMA

static void
osl_sec_dma_setup_contig_mem(osl_t *osh, unsigned long memsize, int regn)
{
	int ret;

#if defined(__ARM_ARCH_7A__)
	if (regn == CONT_ARMREGION) {
		ret = osl_sec_dma_alloc_contig_mem(osh, memsize, regn);
		if (ret != BCME_OK)
			printk("linux_osl.c: CMA memory access failed\n");
	}
#endif
	/* implement the MIPS Here */
}

static int
osl_sec_dma_alloc_contig_mem(osl_t *osh, unsigned long memsize, int regn)
{
	u64 addr;

	printk("linux_osl.c: The value of cma mem block size = %ld\n", memsize);
	osh->cma = cma_dev_get_cma_dev(regn);
	printk("The value of cma = %p\n", osh->cma);
	if (!osh->cma) {
		printk("linux_osl.c:contig_region index is invalid\n");
		return BCME_ERROR;
	}
	if (cma_dev_get_mem(osh->cma, &addr, (u32)memsize, SEC_DMA_ALIGN) < 0) {
		printk("linux_osl.c: contiguous memory block allocation failure\n");
		return BCME_ERROR;
	}
	osh->contig_base_alloc = (phys_addr_t)addr;
	osh->contig_base = (phys_addr_t)osh->contig_base_alloc;
	printk("contig base alloc=%lx \n", (ulong)osh->contig_base_alloc);

	return BCME_OK;
}

static void
osl_sec_dma_free_contig_mem(osl_t *osh, u32 memsize, int regn)
{
	int ret;

	ret = cma_dev_put_mem(osh->cma, (u64)osh->contig_base, memsize);
	if (ret)
		printf("%s contig base free failed\n", __FUNCTION__);
}

static void *
osl_sec_dma_ioremap(osl_t *osh, struct page *page, size_t size, bool iscache, bool isdecr)
{

	struct page **map;
	int order, i;
	void *addr = NULL;

	size = PAGE_ALIGN(size);
	order = get_order(size);

	map = kmalloc(sizeof(struct page *) << order, GFP_ATOMIC);

	if (map == NULL)
		return NULL;

	for (i = 0; i < (size >> PAGE_SHIFT); i++)
		map[i] = page + i;

	if (iscache) {
		addr = vmap(map, size >> PAGE_SHIFT, VM_MAP, __pgprot(PAGE_KERNEL));
		if (isdecr) {
			osh->contig_delta_va_pa = (phys_addr_t)(addr - page_to_phys(page));
			g_contig_delta_va_pa = osh->contig_delta_va_pa;
		}
	}
	else {

#if defined(__ARM_ARCH_7A__)
		addr = vmap(map, size >> PAGE_SHIFT, VM_MAP,
			pgprot_noncached(__pgprot(PAGE_KERNEL)));
#endif
		if (isdecr) {
			osh->contig_delta_va_pa = (phys_addr_t)(addr - page_to_phys(page));
			g_contig_delta_va_pa = osh->contig_delta_va_pa;
		}
	}

	kfree(map);
	return (void *)addr;
}

static void
osl_sec_dma_iounmap(osl_t *osh, void *contig_base_va, size_t size)
{
	vunmap(contig_base_va);
}

static void
osl_sec_dma_init_elem_mem_block(osl_t *osh, size_t mbsize, int max, sec_mem_elem_t **list)
{
	int i;
	sec_mem_elem_t *sec_mem_elem;

	if ((sec_mem_elem = kmalloc(sizeof(sec_mem_elem_t)*(max), GFP_ATOMIC)) != NULL) {

		*list = sec_mem_elem;
		bzero(sec_mem_elem, sizeof(sec_mem_elem_t)*(max));
		for (i = 0; i < max-1; i++) {
			sec_mem_elem->next = (sec_mem_elem + 1);
			sec_mem_elem->size = mbsize;
			sec_mem_elem->pa_cma = (u32)osh->contig_base_alloc;
			sec_mem_elem->vac = osh->contig_base_alloc_va;

			osh->contig_base_alloc += mbsize;
			osh->contig_base_alloc_va += mbsize;

			sec_mem_elem = sec_mem_elem + 1;
		}
		sec_mem_elem->next = NULL;
		sec_mem_elem->size = mbsize;
		sec_mem_elem->pa_cma = (u32)osh->contig_base_alloc;
		sec_mem_elem->vac = osh->contig_base_alloc_va;

		osh->contig_base_alloc += mbsize;
		osh->contig_base_alloc_va += mbsize;

	}
	else
		printf("%s sec mem elem kmalloc failed\n", __FUNCTION__);
}


static void
osl_sec_dma_deinit_elem_mem_block(osl_t *osh, size_t mbsize, int max, void *sec_list_base)
{
	if (sec_list_base)
		kfree(sec_list_base);
}

static sec_mem_elem_t * BCMFASTPATH
osl_sec_dma_alloc_mem_elem(osl_t *osh, void *va, uint size, int direction,
	struct sec_cma_info *ptr_cma_info, uint offset)
{
	sec_mem_elem_t *sec_mem_elem = NULL;

	if (size <= 512 && osh->sec_list_512) {
		sec_mem_elem = osh->sec_list_512;
		osh->sec_list_512 = sec_mem_elem->next;
	}
	else if (size <= 2048 && osh->sec_list_2048) {
		sec_mem_elem = osh->sec_list_2048;
		osh->sec_list_2048 = sec_mem_elem->next;
	}
	else if (osh->sec_list_4096) {
		sec_mem_elem = osh->sec_list_4096;
		osh->sec_list_4096 = sec_mem_elem->next;
	} else {
		printf("%s No matching Pool available size=%d \n", __FUNCTION__, size);
		return NULL;
	}

	if (sec_mem_elem != NULL) {
		sec_mem_elem->next = NULL;

	if (ptr_cma_info->sec_alloc_list_tail) {
		ptr_cma_info->sec_alloc_list_tail->next = sec_mem_elem;
	}

	ptr_cma_info->sec_alloc_list_tail = sec_mem_elem;
	if (ptr_cma_info->sec_alloc_list == NULL)
		ptr_cma_info->sec_alloc_list = sec_mem_elem;
	}
	return sec_mem_elem;
}

static void BCMFASTPATH
osl_sec_dma_free_mem_elem(osl_t *osh, sec_mem_elem_t *sec_mem_elem)
{
	sec_mem_elem->dma_handle = 0x0;
	sec_mem_elem->va = NULL;

	if (sec_mem_elem->size == 512) {
		sec_mem_elem->next = osh->sec_list_512;
		osh->sec_list_512 = sec_mem_elem;
	}
	else if (sec_mem_elem->size == 2048) {
		sec_mem_elem->next = osh->sec_list_2048;
		osh->sec_list_2048 = sec_mem_elem;
	}
	else if (sec_mem_elem->size == 4096) {
		sec_mem_elem->next = osh->sec_list_4096;
		osh->sec_list_4096 = sec_mem_elem;
	}
	else
	printf("%s free failed size=%d \n", __FUNCTION__, sec_mem_elem->size);
}

static sec_mem_elem_t * BCMFASTPATH
osl_sec_dma_find_rem_elem(osl_t *osh, struct sec_cma_info *ptr_cma_info, dma_addr_t dma_handle)
{
	sec_mem_elem_t *sec_mem_elem = ptr_cma_info->sec_alloc_list;
	sec_mem_elem_t *sec_prv_elem = ptr_cma_info->sec_alloc_list;

	if (sec_mem_elem->dma_handle == dma_handle) {

		ptr_cma_info->sec_alloc_list = sec_mem_elem->next;

		if (sec_mem_elem == ptr_cma_info->sec_alloc_list_tail) {
			ptr_cma_info->sec_alloc_list_tail = NULL;
			ASSERT(ptr_cma_info->sec_alloc_list == NULL);
		}

		return sec_mem_elem;
	}

	while (sec_mem_elem != NULL) {

		if (sec_mem_elem->dma_handle == dma_handle) {

			sec_prv_elem->next = sec_mem_elem->next;
			if (sec_mem_elem == ptr_cma_info->sec_alloc_list_tail)
				ptr_cma_info->sec_alloc_list_tail = sec_prv_elem;

			return sec_mem_elem;
		}
		sec_prv_elem = sec_mem_elem;
		sec_mem_elem = sec_mem_elem->next;
	}
	return NULL;
}

static sec_mem_elem_t *
osl_sec_dma_rem_first_elem(osl_t *osh, struct sec_cma_info *ptr_cma_info)
{
	sec_mem_elem_t *sec_mem_elem = ptr_cma_info->sec_alloc_list;

	if (sec_mem_elem) {

		ptr_cma_info->sec_alloc_list = sec_mem_elem->next;

		if (ptr_cma_info->sec_alloc_list == NULL)
			ptr_cma_info->sec_alloc_list_tail = NULL;

		return sec_mem_elem;

	} else
		return NULL;
}

static void * BCMFASTPATH
osl_sec_dma_last_elem(osl_t *osh, struct sec_cma_info *ptr_cma_info)
{
	return ptr_cma_info->sec_alloc_list_tail;
}

dma_addr_t BCMFASTPATH
osl_sec_dma_map_txmeta(osl_t *osh, void *va, uint size, int direction, void *p,
	hnddma_seg_map_t *dmah, void *ptr_cma_info)
{
	sec_mem_elem_t *sec_mem_elem;
	struct page *pa_cma_page;
	uint loffset;
	void *vaorig = va + size;
	dma_addr_t dma_handle = 0x0;
	/* packet will be the one added with osl_sec_dma_map() just before this call */

	sec_mem_elem = osl_sec_dma_last_elem(osh, ptr_cma_info);

	if (sec_mem_elem && sec_mem_elem->va == vaorig) {

		pa_cma_page = phys_to_page(sec_mem_elem->pa_cma);
		loffset = sec_mem_elem->pa_cma -(sec_mem_elem->pa_cma & ~(PAGE_SIZE-1));

		dma_handle = dma_map_page(osh->cma->dev, pa_cma_page, loffset, size,
			(direction == DMA_TX ? DMA_TO_DEVICE:DMA_FROM_DEVICE));

	} else {
		printf("%s: error orig va not found va = 0x%p \n",
			__FUNCTION__, vaorig);
	}
	return dma_handle;
}

dma_addr_t BCMFASTPATH
osl_sec_dma_map(osl_t *osh, void *va, uint size, int direction, void *p,
	hnddma_seg_map_t *dmah, void *ptr_cma_info, uint offset)
{

	sec_mem_elem_t *sec_mem_elem;
	struct page *pa_cma_page;
	void *pa_cma_kmap_va = NULL;
	int *fragva;
	uint buflen = 0;
	struct sk_buff *skb;
	dma_addr_t dma_handle = 0x0;
	uint loffset;
	int i = 0;

	sec_mem_elem = osl_sec_dma_alloc_mem_elem(osh, va, size, direction, ptr_cma_info, offset);

	if (sec_mem_elem == NULL) {
		printk("linux_osl.c: osl_sec_dma_map - cma allocation failed\n");
		return 0;
	}
	sec_mem_elem->va = va;
	sec_mem_elem->direction = direction;
	pa_cma_page = phys_to_page(sec_mem_elem->pa_cma);

	loffset = sec_mem_elem->pa_cma -(sec_mem_elem->pa_cma & ~(PAGE_SIZE-1));
	/* pa_cma_kmap_va = kmap_atomic(pa_cma_page);
	* pa_cma_kmap_va += loffset;
	*/

	pa_cma_kmap_va = sec_mem_elem->vac;

	if (direction == DMA_TX) {

		if (p == NULL) {

			memcpy(pa_cma_kmap_va+offset, va, size);
			buflen = size;
		}
		else {
			for (skb = (struct sk_buff *)p; skb != NULL; skb = PKTNEXT(osh, skb)) {
				if (skb_is_nonlinear(skb)) {


					for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
						skb_frag_t *f = &skb_shinfo(skb)->frags[i];
						fragva = kmap_atomic(skb_frag_page(f));
						memcpy((pa_cma_kmap_va+offset+buflen),
						(fragva + f->page_offset), skb_frag_size(f));
						kunmap_atomic(fragva);
						buflen += skb_frag_size(f);
					}
				}
				else {
					memcpy((pa_cma_kmap_va+offset+buflen), skb->data, skb->len);
					buflen += skb->len;
				}
			}

		}
		if (dmah) {
			dmah->nsegs = 1;
			dmah->origsize = buflen;
		}
	}

	else if (direction == DMA_RX)
	{
		buflen = size;
		if ((p != NULL) && (dmah != NULL)) {
			dmah->nsegs = 1;
			dmah->origsize = buflen;
		}
	}
	if (direction == DMA_RX || direction == DMA_TX) {

		dma_handle = dma_map_page(osh->cma->dev, pa_cma_page, loffset+offset, buflen,
			(direction == DMA_TX ? DMA_TO_DEVICE:DMA_FROM_DEVICE));

	}
	if (dmah) {
		dmah->segs[0].addr = dma_handle;
		dmah->segs[0].length = buflen;
	}
	sec_mem_elem->dma_handle = dma_handle;
	/* kunmap_atomic(pa_cma_kmap_va-loffset); */
	return dma_handle;
}

dma_addr_t BCMFASTPATH
osl_sec_dma_dd_map(osl_t *osh, void *va, uint size, int direction, void *p, hnddma_seg_map_t *map)
{

	struct page *pa_cma_page;
	phys_addr_t pa_cma;
	dma_addr_t dma_handle = 0x0;
	uint loffset;

	pa_cma = (phys_addr_t)(va - osh->contig_delta_va_pa);
	pa_cma_page = phys_to_page(pa_cma);
	loffset = pa_cma -(pa_cma & ~(PAGE_SIZE-1));

	dma_handle = dma_map_page(osh->cma->dev, pa_cma_page, loffset, size,
		(direction == DMA_TX ? DMA_TO_DEVICE:DMA_FROM_DEVICE));

	return dma_handle;
}

void BCMFASTPATH
osl_sec_dma_unmap(osl_t *osh, dma_addr_t dma_handle, uint size, int direction,
void *p, hnddma_seg_map_t *map,	void *ptr_cma_info, uint offset)
{
	sec_mem_elem_t *sec_mem_elem;
	struct page *pa_cma_page;
	void *pa_cma_kmap_va = NULL;
	uint buflen = 0;
	dma_addr_t pa_cma;
	void *va;
	uint loffset = 0;
	int read_count = 0;
	BCM_REFERENCE(buflen);
	BCM_REFERENCE(read_count);

	sec_mem_elem = osl_sec_dma_find_rem_elem(osh, ptr_cma_info, dma_handle);
	if (sec_mem_elem == NULL) {
		printf("%s sec_mem_elem is NULL and dma_handle =0x%lx and dir=%d\n",
			__FUNCTION__, (ulong)dma_handle, direction);
		return;
	}

	va = sec_mem_elem->va;
	va -= offset;
	pa_cma = sec_mem_elem->pa_cma;

	pa_cma_page = phys_to_page(pa_cma);
	loffset = sec_mem_elem->pa_cma -(sec_mem_elem->pa_cma & ~(PAGE_SIZE-1));

	if (direction == DMA_RX) {

		if (p == NULL) {

			/* pa_cma_kmap_va = kmap_atomic(pa_cma_page);
			* pa_cma_kmap_va += loffset;
			*/

			pa_cma_kmap_va = sec_mem_elem->vac;

			dma_unmap_page(osh->cma->dev, pa_cma, size, DMA_FROM_DEVICE);
			memcpy(va, pa_cma_kmap_va, size);
			/* kunmap_atomic(pa_cma_kmap_va); */
		}
	} else {
		dma_unmap_page(osh->cma->dev, pa_cma, size+offset, DMA_TO_DEVICE);
	}

	osl_sec_dma_free_mem_elem(osh, sec_mem_elem);
}

void
osl_sec_dma_unmap_all(osl_t *osh, void *ptr_cma_info)
{

	sec_mem_elem_t *sec_mem_elem;

	sec_mem_elem = osl_sec_dma_rem_first_elem(osh, ptr_cma_info);

	while (sec_mem_elem != NULL) {

		dma_unmap_page(osh->cma->dev, sec_mem_elem->pa_cma, sec_mem_elem->size,
			sec_mem_elem->direction == DMA_TX ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
		osl_sec_dma_free_mem_elem(osh, sec_mem_elem);

		sec_mem_elem = osl_sec_dma_rem_first_elem(osh, ptr_cma_info);
	}
}

static void
osl_sec_dma_init_consistent(osl_t *osh)
{
	int i;
	void *temp_va = osh->contig_base_alloc_coherent_va;
	phys_addr_t temp_pa = osh->contig_base_alloc_coherent;

	for (i = 0; i < SEC_CMA_COHERENT_MAX; i++) {
		osh->sec_cma_coherent[i].avail = TRUE;
		osh->sec_cma_coherent[i].va = temp_va;
		osh->sec_cma_coherent[i].pa = temp_pa;
		temp_va += SEC_CMA_COHERENT_BLK;
		temp_pa += SEC_CMA_COHERENT_BLK;
	}
}

static void *
osl_sec_dma_alloc_consistent(osl_t *osh, uint size, uint16 align_bits, ulong *pap)
{

	void *temp_va = NULL;
	ulong temp_pa = 0;
	int i;

	if (size > SEC_CMA_COHERENT_BLK) {
		printf("%s unsupported size\n", __FUNCTION__);
		return NULL;
	}

	for (i = 0; i < SEC_CMA_COHERENT_MAX; i++) {
		if (osh->sec_cma_coherent[i].avail == TRUE) {
			temp_va = osh->sec_cma_coherent[i].va;
			temp_pa = osh->sec_cma_coherent[i].pa;
			osh->sec_cma_coherent[i].avail = FALSE;
			break;
		}
	}

	if (i == SEC_CMA_COHERENT_MAX)
		printf("%s:No coherent mem: va = 0x%p pa = 0x%lx size = %d\n", __FUNCTION__,
			temp_va, (ulong)temp_pa, size);

	*pap = (unsigned long)temp_pa;
	return temp_va;
}

static void
osl_sec_dma_free_consistent(osl_t *osh, void *va, uint size, dmaaddr_t pa)
{
	int i = 0;

	for (i = 0; i < SEC_CMA_COHERENT_MAX; i++) {
		if (osh->sec_cma_coherent[i].va == va) {
			osh->sec_cma_coherent[i].avail = TRUE;
			break;
		}
	}
	if (i == SEC_CMA_COHERENT_MAX)
		printf("%s:Error: va = 0x%p pa = 0x%lx size = %d\n", __FUNCTION__,
			va, (ulong)pa, size);
}

#endif /* BCM_SECURE_DMA */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0) && defined(TSQ_MULTIPLIER)
#include <linux/kallsyms.h>
#include <net/sock.h>
void
osl_pkt_orphan_partial(struct sk_buff *skb, int tsq)
{
	uint32 fraction;
	static void *p_tcp_wfree = NULL;

	if (tsq <= 0)
		return;

	if (!skb->destructor || skb->destructor == sock_wfree)
		return;

	if (unlikely(!p_tcp_wfree)) {
		char sym[KSYM_SYMBOL_LEN];
		sprint_symbol(sym, (unsigned long)skb->destructor);
		sym[9] = 0;
		if (!strcmp(sym, "tcp_wfree"))
			p_tcp_wfree = skb->destructor;
		else
			return;
	}

	if (unlikely(skb->destructor != p_tcp_wfree || !skb->sk))
		return;

	/* abstract a certain portion of skb truesize from the socket
	 * sk_wmem_alloc to allow more skb can be allocated for this
	 * socket for better cusion meeting WiFi device requirement
	 */
	fraction = skb->truesize * (tsq - 1) / tsq;
	skb->truesize -= fraction;
	atomic_sub(fraction, &skb->sk->sk_wmem_alloc);
}
#endif /* LINUX_VERSION >= 3.6.0 && TSQ_MULTIPLIER */
