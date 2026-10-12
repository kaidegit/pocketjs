/* Staged memory diagnostics for the AIC port — see pocketjs_diag.h and
 * docs/aic-memory-optimization.md §3 (P0). One sample block per startup
 * boundary (S0..S5) and on demand through the `pjs_mem` console command,
 * which queues the sample so it runs on the guest task at the next tick:
 * JS_ComputeMemoryUsage walks engine-internal structures and must not race
 * a running frame. Everything prints with the [PJS-DIAG] prefix; lines are
 * kept short because rt_kprintf truncates at the console buffer.
 *
 * The TLSF free-block walk (rt_memory_walk) runs only here at safe sample
 * points, never on the allocation path, and the failure ring lives in
 * pocketjs_heap.c in fixed storage. */
#include <rtconfig.h>
#include <rtthread.h>
#include <string.h>

#include "pocketjs_diag.h"
#include "pocketjs_heap.h"

#ifdef POCKETJS_AIC_DIAG

/* SDK bsp/artinchip/drv/mem/rt_tlsf_heap.c (RT_USING_USERHEAP): sum of free
 * blocks, largest contiguous free block, free block count. Block sizes are
 * the allocator's physical sizes, so the largest allocatable request is a
 * little below the reported number. */
extern void rt_memory_walk(rt_size_t *free_total, rt_size_t *largest_free,
                           rt_size_t *free_blocks);

static volatile rt_bool_t pocketjs_diag_pending = RT_FALSE;
static char pocketjs_diag_label[POCKETJS_DIAG_LABEL_MAX] = "manual";

static rt_size_t diag_stack_used(rt_thread_t thread) {
  /* Same '#'-fill scan as list_thread; only meaningful for threads that
   * already ran (the fill byte is untouched stack). */
  if (thread == RT_NULL || thread->stack_addr == RT_NULL) {
    return 0U;
  }
  const rt_uint8_t *stack = (const rt_uint8_t *)thread->stack_addr;
  rt_size_t index = 0U;
  while (index < thread->stack_size && stack[index] == '#') {
    index++;
  }
  return thread->stack_size - index;
}

static void diag_print_sys(const char *stage) {
  rt_size_t total = 0U, used = 0U, max_used = 0U;
  pocketjs_heap_stats(&total, &used, &max_used);
  rt_size_t free_total = 0U, largest = 0U, blocks = 0U;
  rt_memory_walk(&free_total, &largest, &blocks);
  const uint32_t now_ms =
      (uint32_t)(rt_tick_get() * 1000UL / RT_TICK_PER_SECOND);
  rt_kprintf("[PJS-DIAG] %s t=%u sys=%u/%u sys_max=%u\n", stage,
             (unsigned)now_ms, (unsigned)used, (unsigned)total,
             (unsigned)max_used);
  rt_kprintf("[PJS-DIAG] %s walk_free=%u walk_maxfree=%u walk_frags=%u\n",
             stage, (unsigned)free_total, (unsigned)largest,
             (unsigned)blocks);
}

static void diag_print_regions(const char *stage) {
  rt_object_t object = rt_object_find("heap_cma", RT_Object_Class_MemHeap);
  if (object != RT_NULL) {
    rt_size_t total = 0U, used = 0U, max_used = 0U;
    rt_memheap_info((struct rt_memheap *)object, &total, &used, &max_used);
    rt_kprintf("[PJS-DIAG] %s cma=%u/%u cma_max=%u\n", stage, (unsigned)used,
               (unsigned)total, (unsigned)max_used);
  }
  rt_thread_t task = rt_thread_find("pocketjs");
  if (task != RT_NULL) {
    rt_kprintf("[PJS-DIAG] %s stack_pjs=%u/%u\n", stage,
               (unsigned)diag_stack_used(task),
               (unsigned)task->stack_size);
  }
}

void pocketjs_diag_sample(const char *stage, pocketjs_guest_t *guest) {
  pocketjs_heap_diag_set_stage(stage);
  diag_print_sys(stage);

  pocketjs_heap_diag_t heap;
  pocketjs_heap_diag(&heap);
  rt_kprintf("[PJS-DIAG] %s caps_live=%u caps_peak=%u caps_blocks=%u "
             "caps_fails=%u\n",
             stage, (unsigned)heap.live, (unsigned)heap.peak,
             (unsigned)heap.blocks, (unsigned)heap.fails);

  if (guest != RT_NULL) {
    pocketjs_guest_memstats_t mem = {.struct_size = sizeof(mem)};
    pocketjs_guest_allocstats_t alloc = {.struct_size = sizeof(alloc)};
    pocketjs_guest_stats_t stats = {.struct_size = sizeof(stats)};
    pocketjs_guest_memstats(guest, &mem);
    pocketjs_guest_allocstats(guest, &alloc);
    pocketjs_guest_stats(guest, &stats);
    rt_kprintf("[PJS-DIAG] %s js=%u/%u atoms=%u str=%u obj=%u\n", stage,
               (unsigned)mem.malloc_size, (unsigned)mem.malloc_limit,
               (unsigned)mem.atom_count, (unsigned)mem.str_count,
               (unsigned)mem.obj_count);
    rt_kprintf("[PJS-DIAG] %s shape=%u func_code=%u frames=%u/%u\n", stage,
               (unsigned)mem.shape_count,
               (unsigned)mem.js_func_code_size, (unsigned)stats.frames,
               (unsigned)stats.frame_errors);
    rt_kprintf("[PJS-DIAG] %s guest_live=%u guest_peak=%u rloc=%u/%u\n",
               stage, (unsigned)alloc.live, (unsigned)alloc.peak,
               (unsigned)alloc.reallocs, (unsigned)alloc.realloc_shrinks);
  }

  diag_print_regions(stage);
  pocketjs_heap_diag_print_fails();
}

void pocketjs_diag_dump(void) {
  pocketjs_heap_diag_t heap;
  pocketjs_heap_diag(&heap);
  rt_kprintf("[PJS-DIAG] dump caps_live=%u caps_peak=%u caps_fails=%u\n",
             (unsigned)heap.live, (unsigned)heap.peak, (unsigned)heap.fails);
  pocketjs_heap_diag_print_fails();
}

void pocketjs_diag_reset_peaks(void) {
  pocketjs_heap_diag_reset_peak();
  rt_kprintf("[PJS-DIAG] peaks reset\n");
}

void pocketjs_diag_request(const char *label) {
  if (label == RT_NULL) {
    label = "manual";
  }
  /* One console writer at a time; the label buffer is plain storage. */
  rt_size_t index = 0U;
  while (label[index] != '\0' && index + 1U < POCKETJS_DIAG_LABEL_MAX) {
    pocketjs_diag_label[index] = label[index];
    index++;
  }
  pocketjs_diag_label[index] = '\0';
  pocketjs_diag_pending = RT_TRUE;
  rt_kprintf("[PJS-DIAG] sample '%s' queued for the next tick\n",
             pocketjs_diag_label);
}

rt_bool_t pocketjs_diag_take_request(char *label, rt_size_t size) {
  if (!pocketjs_diag_pending) {
    return RT_FALSE;
  }
  pocketjs_diag_pending = RT_FALSE;
  rt_size_t index = 0U;
  while (pocketjs_diag_label[index] != '\0' && index + 1U < size) {
    label[index] = pocketjs_diag_label[index];
    index++;
  }
  label[index] = '\0';
  return RT_TRUE;
}

static void pjs_mem(int argc, char **argv) {
  if (argc >= 2 && strcmp(argv[1], "reset") == 0) {
    pocketjs_diag_reset_peaks();
    return;
  }
  pocketjs_diag_request(argc >= 2 ? argv[1] : "manual");
}
MSH_CMD_EXPORT(pjs_mem, queue a PocketJS memory sample (or 'reset' peaks));

#endif /* POCKETJS_AIC_DIAG */
