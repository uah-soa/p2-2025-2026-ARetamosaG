/*
    Copyright 2023 The Operating System Group at the UAH
    sim_pag_fifo.c
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
  if (op == 'R') {              // If it's a read,
    S->numrefsread++;           // count it
  } else if (op == 'W') {       // If it's a write,
    S->pgt[page].modified = 1;  // count it and mark the
    S->numrefswrite++;          // page 'modified'
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
  int victim_frame;
  int victim_page;
  
  // The first frame in the circular list is the oldest (FIFO)
  // It's the one after listoccupied (since the list is circular)
  victim_frame = S->frt[S->listoccupied].next;
  victim_page = S->frt[victim_frame].page;
  
  if (S->detailed) {
    printf("@ Choosing P %d (FIFO - oldest) from M %d for replacement\n",
           victim_page, victim_frame);
  }
  
  return victim_page;
}

void replace_page(ssystem* S, int victim, int newpage) {
  int frame;
  
  frame = S->pgt[victim].frame;
  
  if (S->detailed) {
    if (S->pgt[victim].modified) {
      printf("@ Writing back modified P %d to disk for replacement\n", victim);
    }
    printf("@ Replacing victim P %d with P %d in M %d\n", victim, newpage, frame);
  }
  
  // Update counter if page was modified
  if (S->pgt[victim].modified) {
    S->numpgwriteback++;
  }
  
  // Remove victim from page table
  S->pgt[victim].present = 0;
  S->pgt[victim].frame = -1;
  S->pgt[victim].modified = 0;
  
  // Load new page in the frame
  S->pgt[newpage].present = 1;
  S->pgt[newpage].frame = frame;
  S->pgt[newpage].modified = 0;
  S->pgt[newpage].referenced = 0;
  S->pgt[newpage].timestamp = 0;
  
  // Update frame table
  S->frt[frame].page = newpage;
  
  // FIFO: Move this frame to the end of the circular list
  // (it's now the "newest" since it just got a new page)
  S->listoccupied = frame;
}

void occupy_free_frame(ssystem* S, int frame, int page) {
  // Update page table
  S->pgt[page].present = 1;
  S->pgt[page].frame = frame;
  S->pgt[page].modified = 0;
  S->pgt[page].referenced = 0;
  S->pgt[page].timestamp = 0;
  
  // Update frame table
  S->frt[frame].page = page;
  
  // FIFO: Add frame to the circular list of occupied frames
  if (S->listoccupied == -1) {
    // First frame in the list - points to itself
    S->frt[frame].next = frame;
    S->listoccupied = frame;
  } else {
    // Insert at the end of the circular list
    S->frt[frame].next = S->frt[S->listoccupied].next;
    S->frt[S->listoccupied].next = frame;
    S->listoccupied = frame;  // Update last element pointer
  }
  
  if (S->detailed) {
    printf("@ Lodging P %d in M %d\n", page, frame);
  }
}

// Functions that show results

void print_page_table(ssystem* S) {
  int i;
  
  printf("---------- PAGE TABLE ----------\n");
  printf("PAGE    Present  Frame  Modified\n");
  
  for (i = 0; i < S->numpags; i++) {
    printf("%4d    ", i);
    
    if (S->pgt[i].present) {
      printf("%4d     %4d      %4d\n",
             S->pgt[i].present,
             S->pgt[i].frame,
             S->pgt[i].modified);
    } else {
      printf("%4d        -         -\n",
             S->pgt[i].present);
    }
  }
  
  printf("--------------------------------\n");
}

void print_frames_table(ssystem* S) {
  int i, frame;
  
  printf("---------- FRAMES TABLE ----------\n");
  printf("FRAME   Page   Present  Modified  FIFO_Order\n");
  
  for (i = 0; i < S->numframes; i++) {
    printf("%4d    ", i);
    
    if (S->frt[i].page >= 0) {
      int page = S->frt[i].page;
      printf("%4d     %4d      %4d      ",
             page,
             S->pgt[page].present,
             S->pgt[page].modified);
      
      // Show FIFO position (which one will be replaced first)
      int pos = 1;
      frame = S->frt[S->listoccupied].next;  // Start from oldest
      while (frame != i && pos <= S->numframes) {
        frame = S->frt[frame].next;
        pos++;
      }
      printf("%d\n", pos);
    } else {
      printf("   -        -         -          -\n");
    }
  }
  
  printf("----------------------------------\n");
}

void print_replacement_report(ssystem* S) {
  int frame, count = 0;
  
  printf("--------- REPLACEMENT REPORT ---------\n");
  printf("FIFO replacement policy\n");
  
  if (S->listoccupied != -1) {
    printf("Occupied frames (FIFO order - oldest first):\n");
    frame = S->frt[S->listoccupied].next;  // Start from the oldest
    
    do {
      printf("  M %d -> P %d%s\n",
             frame,
             S->frt[frame].page,
             (frame == S->frt[S->listoccupied].next) ? " (next victim)" : "");
      frame = S->frt[frame].next;
      count++;
    } while (frame != S->frt[S->listoccupied].next && count < S->numframes);
  }
  
  printf("--------------------------------------\n");
  printf("PAGE FAULTS: --->> %d <<---\n", S->numpagefaults);
}
