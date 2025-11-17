/*
    Copyright 2023 The Operating System Group at the UAH
    sim_pag_lru.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "./sim_paging.h"

// Function that initialises the tables

void init_tables(ssystem* S) {
  int i;

  // Reset pages
  memset(S->pgt, 0, sizeof(spage) * S->numpags);

  // Empty LRU stack
  S->lru = -1;

  // Reset LRU(t) time
  S->clock = 0;

  // Circular list of free frames
  for (i = 0; i < S->numframes - 1; i++) {
    S->frt[i].page = -1;
    S->frt[i].next = i + 1;
  }

  S->frt[i].page = -1;  // Now i == numframes-1
  S->frt[i].next = 0;   // Close circular list
  S->listfree = i;      // Point to the last one

  // Empty circular list of occupied frames
  S->listoccupied = -1;
}

// Functions that simulate the hardware of the MMU

unsigned sim_mmu(ssystem* S, unsigned virtual_addr, char op) {
  unsigned physical_addr;
  int page, frame, offset;

  page   = virtual_addr / S->pagsz;
  offset = virtual_addr % S->pagsz;

  if (page < 0 || page >= S->numpags) {
    S->numillegalrefs++;
    return ~0U;
  }
  
  if (!S->pgt[page].present)
    handle_page_fault(S, virtual_addr);
    
  frame = S->pgt[page].frame;
  physical_addr = frame * S->pagsz + offset;
  
  reference_page(S, page, op);
  
  if (S->detailed) {
    printf("\t %c %u==P %d(M %d)+ %d\n", op, virtual_addr, page, frame, offset);
  }

  return physical_addr;
}

void reference_page(ssystem* S, int page, char op) {
  // Increment read or write counter
  if (op == 'R') {
    S->numrefsread++;
  } else {
    S->numrefswrite++;
  }
  
  // If it's a write, mark page as modified
  if (op == 'W') {
    S->pgt[page].modified = 1;
  }
  
  // LRU: Store current clock value as timestamp
  S->pgt[page].timestamp = S->clock;
  
  // Increment clock
  S->clock++;
  
  // Check for clock overflow
  if (S->clock == 0) {
    fprintf(stderr, "WARNING: Clock overflow! Timestamp values may be unreliable.\n");
  }
}

// Functions that simulate the operating system

void handle_page_fault(ssystem* S, unsigned virtual_addr) {
  int page, victim, frame, last;

  S->numpagefaults++;
  page = virtual_addr / S->pagsz;
  
  if (S->detailed) {
    printf("@ PAGE_FAULT in P %d!\n", page);
  }
  
  if (S->listfree != -1) {
    // There are free frames
    last = S->listfree;
    frame = S->frt[last].next;
    
    if (frame == last) {
      // Then, this is the last one left.
      S->listfree = -1;
    } else {
      // Otherwise, bypass
      S->frt[last].next = S->frt[frame].next;
    }
    
    occupy_free_frame(S, frame, page);
    
  } else {
    // There are not free frames
    victim = choose_page_to_be_replaced(S);
    replace_page(S, victim, page);
  }
}

int choose_page_to_be_replaced(ssystem* S) {
  int victim = -1;
  unsigned min_timestamp = ~0U;  // Maximum unsigned value
  int i;
  
  // Sequential search for the page with lowest timestamp
  for (i = 0; i < S->numpags; i++) {
    if (S->pgt[i].present) {
      if (S->pgt[i].timestamp < min_timestamp) {
        min_timestamp = S->pgt[i].timestamp;
        victim = i;
      }
    }
  }
  
  if (S->detailed) {
    printf("@ Choosing P %d (timestamp %u) from M %d for replacement\n",
           victim, S->pgt[victim].timestamp, S->pgt[victim].frame);
  }
  
  return victim;
}

void replace_page(ssystem* S, int victim, int newpage) {
  int frame;

  frame = S->pgt[victim].frame;

  if (S->pgt[victim].modified) {
    if (S->detailed)
      printf(
          "@ Writing modified P%d back (to disc) to "
          "replace it\n",
          victim);

    S->numpgwriteback++;
  }

  if (S->detailed)
    printf("@ Replacing victim P%d with P%d in F%d\n", victim, newpage, frame);

  S->pgt[victim].present = 0;

  S->pgt[newpage].present = 1;
  S->pgt[newpage].frame = frame;
  S->pgt[newpage].modified = 0;

  S->frt[frame].page = newpage;
}

void occupy_free_frame(ssystem* S, int frame, int page) {
  if (S->detailed) printf("@ Storing P%d in F%d\n", page, frame);

    // Update page table
    S->pgt[page].present = 1;
    S->pgt[page].frame = frame;
    S->pgt[page].modified = 0;
    S->pgt[page].referenced = 0;
    S->pgt[page].timestamp = 0;
  
    // Update frame table
    S->frt[frame].page = page;
}

// Functions that show results

void print_page_table(ssystem* S) {
  int i;
  
  printf("---------- PAGE TABLE ----------\n");
  printf("PAGE    Present  Frame  Modified  Timestamp\n");
  
  for (i = 0; i < S->numpags; i++) {
    printf("%4d    ", i);
    
    if (S->pgt[i].present) {
      printf("%4d     %4d      %4d      %u\n",
             S->pgt[i].present,
             S->pgt[i].frame,
             S->pgt[i].modified,
             S->pgt[i].timestamp);
    } else {
      printf("%4d        -         -           -\n",
             S->pgt[i].present);
    }
  }
  
  printf("--------------------------------\n");
}

void print_frames_table(ssystem* S) {
  int p, f;

  printf("%10s %10s %10s   %s\n", "FRAME", "Page", "Present", "Modified");

  for (f = 0; f < S->numframes; f++) {
    p = S->frt[f].page;

    if (p == -1)
      printf("%8d   %8s   %6s     %6s\n", f, "-", "-", "-");
    else if (S->pgt[p].present)
      printf("%8d   %8d   %6d     %6d\n", f, p, S->pgt[p].present,
             S->pgt[p].modified);
    else
      printf("%8d   %8d   %6d     %6s   ERROR!\n", f, p, S->pgt[p].present,
             "-");
  }
}

void print_replacement_report(ssystem* S) {
  int i;
  unsigned min_timestamp = ~0U;
  unsigned max_timestamp = 0;
  
  printf("--------- REPLACEMENT REPORT ---------\n");
  printf("LRU replacement policy\n");
  printf("Current clock value: %u\n", S->clock);
  
  // Find min and max timestamps of present pages
  for (i = 0; i < S->numpags; i++) {
    if (S->pgt[i].present) {
      if (S->pgt[i].timestamp < min_timestamp) {
        min_timestamp = S->pgt[i].timestamp;
      }
      if (S->pgt[i].timestamp > max_timestamp) {
        max_timestamp = S->pgt[i].timestamp;
      }
    }
  }
  
  if (min_timestamp != ~0U) {
    printf("Min timestamp in memory: %u\n", min_timestamp);
    printf("Max timestamp in memory: %u\n", max_timestamp);
  }
  
  printf("--------------------------------------\n");
  printf("PAGE FAULTS: --->> %d <<---\n", S->numpagefaults);
}
