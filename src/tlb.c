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

static tlb_entry_t* tlb_l1_lookup(va_t virtual_page_number) {
  for (size_t i = 0; i < TLB_L1_SIZE; ++i) {
    if (tlb_l1[i].valid && tlb_l1[i].virtual_page_number == virtual_page_number)
      return &tlb_l1[i];
  }
  return NULL;
}

static size_t tlb_l1_lru_lookup() {
  size_t victim = 0;
  uint64_t oldest = UINT64_MAX;
  for (size_t i = 0; i < TLB_L1_SIZE; ++i) {
    if (tlb_l1[i].last_access < oldest) {
      oldest = tlb_l1[i].last_access;
      victim = i;
    }
  }
  return victim;
}

static tlb_entry_t* tlb_l1_select_victim() {
  for (size_t i = 0; i < TLB_L1_SIZE; ++i) {
    if (!tlb_l1[i].valid) return &tlb_l1[i];
  }
  size_t victim = tlb_l1_lru_lookup();
  return &tlb_l1[victim];
}

static void tlb_l1_evict_entry(tlb_entry_t* entry) {
  if (entry->valid && entry->dirty)
      write_back_tlb_entry(entry->physical_page_number << PAGE_SIZE_BITS);
  entry->valid = false;
  entry->dirty = false;
  entry->last_access = 0;
  entry->virtual_page_number = 0;
  entry->physical_page_number = 0;
}

void tlb_invalidate(va_t virtual_page_number) {
  increment_time(TLB_L1_LATENCY_NS);
  tlb_entry_t* entry = tlb_l1_lookup(virtual_page_number);
  if (entry) {
    entry->valid = false;
    tlb_l1_invalidations++;
  }
}

pa_dram_t tlb_translate(va_t virtual_address, op_t op) {
  virtual_address &= VIRTUAL_ADDRESS_MASK;
  increment_time(TLB_L1_LATENCY_NS);

  va_t virtual_page_number = virtual_address >> PAGE_SIZE_BITS;
  va_t offset = virtual_address & PAGE_OFFSET_MASK;

  tlb_entry_t* entry = tlb_l1_lookup(virtual_page_number);
  if (entry) {
    tlb_l1_hits++;
    entry->last_access = get_time();
    if (op == OP_WRITE) entry->dirty = true;
    return (entry->physical_page_number << PAGE_SIZE_BITS) | offset;
  }

  tlb_l1_misses++;
  pa_dram_t physical_address = page_table_translate(virtual_address, op);
  pa_dram_t physical_page_number = physical_address >> PAGE_SIZE_BITS;

  entry = tlb_l1_select_victim();
  if (entry->valid) tlb_l1_evict_entry(entry);

  entry->valid = true;
  entry->dirty = (op == OP_WRITE);
  entry->virtual_page_number = virtual_page_number;
  entry->physical_page_number = physical_page_number;
  entry->last_access = get_time();

  return physical_address;
}
