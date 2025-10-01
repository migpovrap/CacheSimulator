#include "tlb.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "clock.h"
#include "constants.h"
#include "log.h"
#include "memory.h"
#include "page_table.h"

typedef struct {
  bool valid;
  bool dirty;
  uint64_t last_access;
  va_t virtual_page_number;
  pa_dram_t physical_page_number;
} tlb_entry_t;

tlb_entry_t tlb_l1[TLB_L1_SIZE];
tlb_entry_t tlb_l2[TLB_L2_SIZE];

uint64_t tlb_l1_hits = 0;
uint64_t tlb_l1_misses = 0;
uint64_t tlb_l1_invalidations = 0;

uint64_t tlb_l2_hits = 0;
uint64_t tlb_l2_misses = 0;
uint64_t tlb_l2_invalidations = 0;

uint64_t get_total_tlb_l1_hits() { return tlb_l1_hits; }
uint64_t get_total_tlb_l1_misses() { return tlb_l1_misses; }
uint64_t get_total_tlb_l1_invalidations() { return tlb_l1_invalidations; }

uint64_t get_total_tlb_l2_hits() { return tlb_l2_hits; }
uint64_t get_total_tlb_l2_misses() { return tlb_l2_misses; }
uint64_t get_total_tlb_l2_invalidations() { return tlb_l2_invalidations; }

void tlb_init() {
  memset(tlb_l1, 0, sizeof(tlb_l1));
  memset(tlb_l2, 0, sizeof(tlb_l2));
  tlb_l1_hits = 0;
  tlb_l1_misses = 0;
  tlb_l1_invalidations = 0;
  tlb_l2_hits = 0;
  tlb_l2_misses = 0;
  tlb_l2_invalidations = 0;
}

static tlb_entry_t* tlb_lookup(tlb_entry_t* tlb, size_t tlb_size, va_t virtual_page_number) {
  for (size_t i = 0; i < tlb_size; ++i) {
    if (tlb[i].valid && tlb[i].virtual_page_number == virtual_page_number)
      return &tlb[i];
  }
  return NULL;
}

static uint64_t tlb_elapsed_time(uint64_t last_access, uint64_t current_time) {
  if (last_access <= current_time)
    return current_time - last_access;
  return (UINT64_MAX - last_access) + current_time + 1;
}

static size_t tlb_lru_lookup(tlb_entry_t* tlb, size_t tlb_size) {
  size_t victim = 0;
  uint64_t longest_inactive = 0;
  uint64_t eval_time = get_time();
  for (size_t i = 0; i < tlb_size; ++i) {
    uint64_t elapsed = tlb_elapsed_time(tlb[i].last_access, eval_time);
    if (elapsed > longest_inactive) {
      longest_inactive = elapsed;
      victim = i;
    }
  }
  return victim;
}

static tlb_entry_t* tlb_select_victim(tlb_entry_t* tlb, size_t tlb_size) {
  for (size_t i = 0; i < tlb_size; ++i) {
    if (!tlb[i].valid)
      return &tlb[i];
  }
  size_t victim = tlb_lru_lookup(tlb, tlb_size);
  return &tlb[victim];
}

static void tlb_evict_entry(tlb_entry_t* entry, bool write_back) {
  if (entry->valid && entry->dirty && write_back)
    write_back_tlb_entry(entry->physical_page_number << PAGE_SIZE_BITS);
  entry->valid = false;
  entry->dirty = false;
  entry->last_access = 0;
  entry->physical_page_number = 0;
  entry->physical_page_number = 0;
}

static tlb_entry_t* tlb_replace_entry(tlb_entry_t* tlb, size_t tlb_size, va_t virtual_page_number,
  pa_dram_t physical_page_number, bool dirty, bool write_back) {

  tlb_entry_t* entry = tlb_select_victim(tlb, tlb_size);
  if (entry->valid)
    tlb_evict_entry(entry, write_back);
  entry->valid = true;
  entry->dirty = dirty;
  entry->virtual_page_number = virtual_page_number;
  entry->physical_page_number = physical_page_number;
  entry->last_access = get_time();
  return entry;
}

static pa_dram_t tlb_hit(tlb_entry_t* entry, op_t op, va_t offset, uint64_t* hit_counter) {
  (*hit_counter)++;
  entry->last_access = get_time();
  if (op == OP_WRITE)
    entry->dirty = true;
  return (entry->physical_page_number << PAGE_SIZE_BITS) | offset;
}

void tlb_invalidate(va_t virtual_page_number) {
  increment_time(TLB_L1_LATENCY_NS);
  tlb_entry_t* entry = tlb_lookup(tlb_l1, TLB_L1_SIZE, virtual_page_number);
  if (entry) {
    tlb_evict_entry(entry, false);
    tlb_l1_invalidations++;
  }

  increment_time(TLB_L2_LATENCY_NS);
  entry = tlb_lookup(tlb_l2, TLB_L2_SIZE, virtual_page_number);
  if (entry) {
    tlb_evict_entry(entry, true);
    tlb_l2_invalidations++;
  }
}

pa_dram_t tlb_translate(va_t virtual_address, op_t op) {

  increment_time(TLB_L1_LATENCY_NS);
  va_t virtual_page_number = (virtual_address >> PAGE_SIZE_BITS) & PAGE_INDEX_MASK;
  va_t offset = virtual_address & PAGE_OFFSET_MASK;
  tlb_entry_t* entry = tlb_lookup(tlb_l1, TLB_L1_SIZE, virtual_page_number);
  if (entry)
    return tlb_hit(entry, op, offset, &tlb_l1_hits);

  tlb_l1_misses++;
  increment_time(TLB_L2_LATENCY_NS);

  entry = tlb_lookup(tlb_l2, TLB_L2_SIZE, virtual_page_number);
  if (entry) {
    pa_dram_t physical_address = tlb_hit(entry, op, offset, &tlb_l2_hits);
    tlb_replace_entry(tlb_l1, TLB_L1_SIZE, virtual_page_number, entry->physical_page_number, entry->dirty, false);
    return physical_address;
  }

  tlb_l2_misses++;
  pa_dram_t physical_address = page_table_translate(virtual_address, op);
  pa_dram_t physical_page_number = physical_address >> PAGE_SIZE_BITS;
  tlb_replace_entry(tlb_l2, TLB_L2_SIZE, virtual_page_number, physical_page_number, (op == OP_WRITE), true);
  tlb_replace_entry(tlb_l1, TLB_L1_SIZE, virtual_page_number, physical_page_number, (op == OP_WRITE), false);
  return physical_address;
}
