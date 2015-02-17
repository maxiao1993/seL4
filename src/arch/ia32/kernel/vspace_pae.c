/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <config.h>
#include <machine/io.h>
#include <kernel/boot.h>
#include <model/statedata.h>
#include <arch/kernel/vspace.h>

#ifdef CONFIG_PAE_PAGING

/* The boot pd is referenced by code that runs before paging, so
 * place it in PHYS_DATA. In PAE mode the top level is actually
 * a PDPTE, but we call it _boot_pd for compatibility */
pdpte_t _boot_pd[BIT(PDPT_BITS)] __attribute__((aligned(BIT(PAGE_BITS)))) PHYS_DATA;
/* Allocate enough page directories to fill every slot in the PDPT */
pde_t _boot_pds[BIT(PD_BITS + PDPT_BITS)] __attribute__((aligned(BIT(PAGE_BITS)))) PHYS_DATA;

BOOT_CODE
pde_t *get_boot_pd()
{
    /* return a pointer to the continus array of boot pds */
    return _boot_pds;
}

/* These functions arefrom what is generated by the bitfield tool. It is
 * required by functions that need to call it before the MMU is turned on.
 * Any changes made to the bitfield generation need to be replicated here.
 */
PHYS_CODE
static inline void
pdpte_ptr_new_phys(pdpte_t *pdpte_ptr, uint32_t pd_base_address, uint32_t avl, uint32_t cache_disabled, uint32_t write_through, uint32_t present)
{
    pdpte_ptr->words[0] = 0;
    pdpte_ptr->words[1] = 0;

    pdpte_ptr->words[0] |= (pd_base_address & 0xfffff000) >> 0;
    pdpte_ptr->words[0] |= (avl & 0x7) << 9;
    pdpte_ptr->words[0] |= (cache_disabled & 0x1) << 4;
    pdpte_ptr->words[0] |= (write_through & 0x1) << 3;
    pdpte_ptr->words[0] |= (present & 0x1) << 0;
}

PHYS_CODE
static inline void
pde_pde_large_ptr_new_phys(pde_t *pde_ptr, uint32_t page_base_address,
                           uint32_t pat, uint32_t avl, uint32_t global, uint32_t dirty,
                           uint32_t accessed, uint32_t cache_disabled, uint32_t write_through,
                           uint32_t super_user, uint32_t read_write, uint32_t present)
{
    pde_ptr->words[0] = 0;
    pde_ptr->words[1] = 0;

    pde_ptr->words[0] |= (page_base_address & 0xffe00000) >> 0;
    pde_ptr->words[0] |= (pat & 0x1) << 12;
    pde_ptr->words[0] |= (avl & 0x7) << 9;
    pde_ptr->words[0] |= (global & 0x1) << 8;
    pde_ptr->words[0] |= ((uint32_t)pde_pde_large & 0x1) << 7;
    pde_ptr->words[0] |= (dirty & 0x1) << 6;
    pde_ptr->words[0] |= (accessed & 0x1) << 5;
    pde_ptr->words[0] |= (cache_disabled & 0x1) << 4;
    pde_ptr->words[0] |= (write_through & 0x1) << 3;
    pde_ptr->words[0] |= (super_user & 0x1) << 2;
    pde_ptr->words[0] |= (read_write & 0x1) << 1;
    pde_ptr->words[0] |= (present & 0x1) << 0;

}

PHYS_CODE VISIBLE void
init_boot_pd(void)
{
    unsigned int i;

    /* first map in all the pds into the pdpt */
    for (i = 0; i < BIT(PDPT_BITS); i++) {
        uint32_t pd_base = (uint32_t)&_boot_pds[i * BIT(PD_BITS)];
        pdpte_ptr_new_phys(
            _boot_pd + i,
            pd_base,    /* pd_base_address */
            0,          /* avl */
            0,          /* cache_disabled */
            0,          /* write_through */
            1           /* present */
        );
    }

    /* identity mapping from 0 up to PPTR_BASE (virtual address) */
    for (i = 0; (i << IA32_2M_bits) < PPTR_BASE; i++) {
        pde_pde_large_ptr_new_phys(
            _boot_pds + i,
            i << IA32_2M_bits, /* physical address */
            0, /* pat            */
            0, /* avl            */
            1, /* global         */
            0, /* dirty          */
            0, /* accessed       */
            0, /* cache_disabled */
            0, /* write_through  */
            0, /* super_user     */
            1, /* read_write     */
            1  /* present        */
        );
    }

    /* mapping of PPTR_BASE (virtual address) to PADDR_BASE up to end of virtual address space */
    for (i = 0; (i << IA32_2M_bits) < -PPTR_BASE; i++) {
        pde_pde_large_ptr_new_phys(
            _boot_pds + i + (PPTR_BASE >> IA32_2M_bits),
            (i << IA32_2M_bits) + PADDR_BASE, /* physical address */
            0, /* pat            */
            0, /* avl            */
            1, /* global         */
            0, /* dirty          */
            0, /* accessed       */
            0, /* cache_disabled */
            0, /* write_through  */
            0, /* super_user     */
            1, /* read_write     */
            1  /* present        */
        );
    }
}

BOOT_CODE void
map_it_frame_cap(cap_t vspace_cap, cap_t frame_cap)
{
    pdpte_t *pdpt = PDPTE_PTR(pptr_of_cap(vspace_cap));
    pde_t *pd;
    pte_t *pt;
    void *frame = (void*)cap_frame_cap_get_capFBasePtr(frame_cap);
    vptr_t vptr = cap_frame_cap_get_capFMappedAddress(frame_cap);

    assert(cap_frame_cap_get_capFMappedASID(frame_cap) != 0);
    pdpt += (vptr >> IA32_1G_bits);
    assert(pdpte_ptr_get_present(pdpt));
    pd = paddr_to_pptr(pdpte_ptr_get_pd_base_address(pdpt));
    pd += ( (vptr & MASK(IA32_1G_bits)) >> IA32_2M_bits);
    assert(pde_pde_small_ptr_get_present(pd));
    pt = paddr_to_pptr(pde_pde_small_ptr_get_pt_base_address(pd));
    pte_ptr_new(
        pt + ((vptr & MASK(IA32_2M_bits)) >> IA32_4K_bits),
        pptr_to_paddr(frame),
        0, /* avl */
        0, /* global */
        0, /* pat */
        0, /* dirty */
        0, /* accessed */
        0, /* cache_disabled */
        0, /* write_through */
        1, /* super_user */
        1, /* read_write */
        1  /* present */
    );
    invalidatePageStructureCache();
}

BOOT_CODE void
map_it_pt_cap(cap_t vspace_cap, cap_t pt_cap)
{
    pdpte_t *pdpt = PDPTE_PTR(pptr_of_cap(vspace_cap));
    pde_t *pd;
    pte_t *pt = PT_PTR(cap_page_table_cap_get_capPTBasePtr(pt_cap));
    vptr_t vptr = cap_page_table_cap_get_capPTMappedAddress(pt_cap);

    assert(cap_page_table_cap_get_capPTIsMapped(pt_cap));
    pdpt += (vptr >> IA32_1G_bits);
    assert(pdpte_ptr_get_present(pdpt));
    pd = paddr_to_pptr(pdpte_ptr_get_pd_base_address(pdpt));
    pde_pde_small_ptr_new(
        pd + (vptr >> IA32_2M_bits),
        pptr_to_paddr(pt),
        0, /* avl*/
        0, /* accessed */
        0, /* cache_disabled */
        0, /* write_through */
        1, /* super_user */
        1, /* read_write */
        1  /* present */
    );
    invalidatePageStructureCache();
}

BOOT_CODE void
map_it_pd_cap(cap_t vspace_cap, cap_t pd_cap)
{
    pdpte_t *pdpt = PDPTE_PTR(pptr_of_cap(vspace_cap));
    pde_t *pd = PDE_PTR(cap_page_directory_cap_get_capPDBasePtr(pd_cap));
    vptr_t vptr = cap_page_directory_cap_get_capPDMappedAddress(pd_cap);

    assert(cap_page_directory_cap_get_capPDIsMapped(pd_cap));
    pdpte_ptr_new(
        pdpt + (vptr >> IA32_1G_bits),
        pptr_to_paddr(pd),
        0, /* avl */
        0, /* cache_disabled */
        0, /* write_through */
        1  /* present */
    );
    invalidatePageStructureCache();
}

/* ==================== BOOT CODE FINISHES HERE ==================== */

void copyGlobalMappings(void* new_vspace)
{
    unsigned int i;
    pdpte_t *pdpt = (pdpte_t*)new_vspace;

    for (i = PPTR_BASE >> IA32_1G_bits; i < BIT(PDPT_BITS); i++) {
        pdpt[i] = ia32KSkernelPDPT[i];
    }
}

bool_t CONST isVTableRoot(cap_t cap)
{
    return cap_get_capType(cap) == cap_pdpt_cap;
}

bool_t CONST isValidNativeRoot(cap_t cap)
{
    if (!isVTableRoot(cap) ||
            !cap_pdpt_cap_get_capPDPTIsMapped(cap)) {
        return false;
    }
    return true;
}

bool_t CONST isValidVTableRoot(cap_t cap) {
    return isValidNativeRoot(cap);
}

void *getValidNativeRoot(cap_t vspace_cap)
{
    if (isValidNativeRoot(vspace_cap)) {
        return PDPTE_PTR(cap_pdpt_cap_get_capPDPTBasePtr(vspace_cap));
    }
    return NULL;
}

static inline pdpte_t *lookupPDPTSlot(void *vspace, vptr_t vptr)
{
    pdpte_t *pdpt = PDPT_PTR(vspace);
    return pdpt + (vptr >> IA32_1G_bits);
}

lookupPDSlot_ret_t lookupPDSlot(void *vspace, vptr_t vptr)
{
    pdpte_t *pdptSlot;
    lookupPDSlot_ret_t ret;

    pdptSlot = lookupPDPTSlot(vspace, vptr);

    if (!pdpte_ptr_get_present(pdptSlot)) {
        current_lookup_fault = lookup_fault_missing_capability_new(PAGE_BITS + PT_BITS + PD_BITS);
        ret.pdSlot = NULL;
        ret.status = EXCEPTION_LOOKUP_FAULT;
        return ret;
    } else {
        pde_t *pd;
        pde_t *pdSlot;
        unsigned int pdIndex;

        pd = paddr_to_pptr(pdpte_ptr_get_pd_base_address(pdptSlot));
        pdIndex = (vptr >> (PAGE_BITS + PT_BITS)) & MASK(PD_BITS);
        pdSlot = pd + pdIndex;

        ret.pdSlot = pdSlot;
        ret.status = EXCEPTION_NONE;
        return ret;
    }
}

exception_t performASIDPoolInvocation(asid_t asid, asid_pool_t* poolPtr, cte_t* vspaceCapSlot)
{
    cap_pdpt_cap_ptr_set_capPDPTMappedASID(&vspaceCapSlot->cap, asid);
    cap_pdpt_cap_ptr_set_capPDPTIsMapped(&vspaceCapSlot->cap, 1);
    poolPtr->array[asid & MASK(asidLowBits)] = PDPTE_PTR(cap_pdpt_cap_get_capPDPTBasePtr(vspaceCapSlot->cap));

    return EXCEPTION_NONE;
}

void unmapPageDirectory(asid_t asid, vptr_t vaddr, pde_t *pd)
{
    findVSpaceForASID_ret_t find_ret;
    cap_t threadRoot;
    pdpte_t *pdptSlot;

    find_ret = findVSpaceForASID(asid);
    if (find_ret.status != EXCEPTION_NONE) {
        return;
    }

    pdptSlot = lookupPDPTSlot(find_ret.vspace_root, vaddr);

    *pdptSlot = pdpte_new(
                    0,  /* pd_base_address  */
                    0,  /* avl              */
                    0,  /* cache_disabled   */
                    0,  /* write_through    */
                    0   /* present          */
                );
    /* check if page directory belongs to current address space */
    threadRoot = TCB_PTR_CTE_PTR(ksCurThread, tcbVTable)->cap;
    if (isValidNativeRoot(threadRoot) && (void*)pptr_of_cap(threadRoot) == find_ret.vspace_root) {
        /* according to the intel manual if we modify a pdpt we must
         * reload cr3 */
        write_cr3(read_cr3());
    }
    invalidatePageStructureCache();
}

exception_t
decodeIA32PageDirectoryInvocation(
    word_t label,
    unsigned int length,
    cte_t* cte,
    cap_t cap,
    extra_caps_t extraCaps,
    word_t* buffer
)
{
    word_t          vaddr;
    vm_attributes_t attr;
    pdpte_t*        pdptSlot;
    cap_t           vspaceCap;
    void*           vspace;
    pdpte_t         pdpte;
    paddr_t         paddr;
    asid_t          asid;
    cap_t           threadRoot;

    if (label == IA32PageDirectoryUnmap) {
        if (!isFinalCapability(cte)) {
            current_syscall_error.type = seL4_RevokeFirst;
            userError("IA32PageDirectory: Cannot unmap if more than one cap exists.");
            return EXCEPTION_SYSCALL_ERROR;
        }
        setThreadState(ksCurThread, ThreadState_Restart);

        if (cap_page_directory_cap_get_capPDIsMapped(cap)) {
            pde_t *pd = PDE_PTR(cap_page_directory_cap_get_capPDBasePtr(cap));
            unmapPageDirectory(
                cap_page_directory_cap_get_capPDMappedASID(cap),
                cap_page_directory_cap_get_capPDMappedAddress(cap),
                pd
            );
            clearMemory((void *)pd, cap_get_capSizeBits(cap));
        }
        cap_page_directory_cap_ptr_set_capPDIsMapped(&(cte->cap), 0);

        return EXCEPTION_NONE;
    }

    if (label != IA32PageDirectoryMap) {
        userError("IA32PageDirectory: Illegal operation.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (length < 2 || extraCaps.excaprefs[0] == NULL) {
        userError("IA32PageDirectory: Truncated message.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (cap_page_directory_cap_get_capPDIsMapped(cap)) {
        userError("IA32PageDirectory: Page directory is already mapped to a PDPT.");
        current_syscall_error.type = seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 0;
        return EXCEPTION_SYSCALL_ERROR;
    }

    vaddr = getSyscallArg(0, buffer) & (~MASK(IA32_1G_bits));
    attr = vmAttributesFromWord(getSyscallArg(1, buffer));
    vspaceCap = extraCaps.excaprefs[0]->cap;

    if (!isValidNativeRoot(vspaceCap)) {
        current_syscall_error.type = seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 1;
        return EXCEPTION_SYSCALL_ERROR;
    }

    vspace = (void*)pptr_of_cap(vspaceCap);
    asid = cap_get_capMappedASID(vspaceCap);

    if (vaddr >= PPTR_USER_TOP) {
        userError("IA32PageDirectory: Mapping address too high.");
        current_syscall_error.type = seL4_InvalidArgument;
        current_syscall_error.invalidArgumentNumber = 0;
        return EXCEPTION_SYSCALL_ERROR;
    }

    {
        findVSpaceForASID_ret_t find_ret;

        find_ret = findVSpaceForASID(asid);
        if (find_ret.status != EXCEPTION_NONE) {
            current_syscall_error.type = seL4_FailedLookup;
            current_syscall_error.failedLookupWasSource = false;
            return EXCEPTION_SYSCALL_ERROR;
        }

        if (find_ret.vspace_root != vspace) {
            current_syscall_error.type = seL4_InvalidCapability;
            current_syscall_error.invalidCapNumber = 1;
            return EXCEPTION_SYSCALL_ERROR;
        }
    }

    pdptSlot = lookupPDPTSlot(vspace, vaddr);

    if (pdpte_ptr_get_present(pdptSlot)) {
        current_syscall_error.type = seL4_DeleteFirst;
        return EXCEPTION_SYSCALL_ERROR;
    }

    paddr = pptr_to_paddr(PDE_PTR(cap_page_directory_cap_get_capPDBasePtr(cap)));
    pdpte = pdpte_new(
                paddr,                                      /* pd_base_address  */
                0,                                          /* avl              */
                vm_attributes_get_ia32PCDBit(attr),      /* cache_disabled   */
                vm_attributes_get_ia32PWTBit(attr),      /* write_through    */
                1                                           /* present          */
            );

    cap = cap_page_directory_cap_set_capPDIsMapped(cap, 1);
    cap = cap_page_directory_cap_set_capPDMappedASID(cap, asid);
    cap = cap_page_directory_cap_set_capPDMappedAddress(cap, vaddr);

    cte->cap = cap;
    *pdptSlot = pdpte;

    /* according to the intel manual if we modify a pdpt we must
     * reload cr3 */
    threadRoot = TCB_PTR_CTE_PTR(ksCurThread, tcbVTable)->cap;
    if (isValidNativeRoot(threadRoot) && (void*)pptr_of_cap(threadRoot) == (void*)pptr_of_cap(vspaceCap)) {
        write_cr3(read_cr3());
    }

    setThreadState(ksCurThread, ThreadState_Restart);
    invalidatePageStructureCache();
    return EXCEPTION_NONE;
}

#endif
