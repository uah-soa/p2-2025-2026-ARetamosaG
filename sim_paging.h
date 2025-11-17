/*
    sim_paging.h
*/

#ifndef _SIM_PAGING_H_
#define _SIM_PAGING_H_

// Structure that holds the state of a page,
// sumulating an entry of the page table

typedef struct
{
    char present;       // 1 = loaded in a frame
    int frame;          // Frame where it is loaded
    char modified;      // 1 = must be written back to disc
                            // if moved out of the frame
    // For FIFO 2nd chance
    char referenced;    // 1 = page referenced recently

    // For LRU(t)
    unsigned timestamp; // Time mark of last reference

    // NOTE: The previous two fiels are in this structure
    //       ---and not in sframe--- because they simulate
    //       a mechanism that, in reality, would be
    //       supported by the hardware.
}
spage;

// Structure that holds the state of a frame
// (the hardware doesn't know anything about this struct)

typedef struct
{
    int page;           // Number of the page loaded, if any

    // For managing free frames and for FIFO and FIFO 2nd ch.
    int next;           // Next frame in the list
}
sframe;

// Struture that contains the state of the whole system

typedef struct
{
    // Page table (maintained by HW and OS)
    int pagsz;
    int numpags;
    spage * pgt;
    int lru;               // Only for LRU replacement
    unsigned clock;        // Only for LRU(t) replacement

    // Frames table (maintained by the OS only)
    int numframes;
    sframe * frt;
    int listfree;
    int listoccupied;      // Only for FIFO and FIFO 2nd ch.

    // Trace data
    int numrefsread;       // Counter of read operations
    int numrefswrite;      // Counter of write operations
    int numpagefaults;     // Counter of page faults
    int numpgwriteback;    // Counter of write back (to disc) ops.
    int numillegalrefs;    // References out of range
    char detailed;         // 1 = show step-by-step information
}
ssystem;

// Function that initialises the tables

void init_tables (ssystem * S);

// Functions that simulate the hardware of the MMU

unsigned sim_mmu(ssystem* S, unsigned virtual_addr, char op) {
  unsigned physical_addr;
  int page, frame, offset;

  // TODO(student):
  //       Type in the code that simulates the MMU's (hardware)
  //       behaviour in response to a memory access operation
  
  page   = virtual_addr / S-> pagsz ;	// Quotient 
  offset = virtual_addr % S-> pagsz ;	// Reminder

  if ( page <0 || page >= S->numpags )
  {
	S->numillegalrefs++;  // References out of range 
	return ~0U;	// Return invalid physical 0xFFF..F
  }
  
  if (! S->pgt[page].present )
	// Not present: trigger page fault exception 
	handle_page_fault(S, virtual_addr);
	
  // Now it is present
  frame = S->pgt[page].frame ;	
  physical_addr = frame*S->pagsz+offset;
  
  reference_page (S, page, op);
  
  if (S->detailed) {
	printf ("\t %c %u==P %d(M %d)+ %d\n", op, virtual_addr, page, frame, offset);
  }


  return physical_addr;
}

void reference_page (ssystem * S, int page, char op);

// Functions that simulate the operating system

void handle_page_fault (ssystem * S, unsigned virtual_addr){

  int page, frame, last, victim;

  S->numpagefaults++;
  page = virtual_addr / S->pagsz;
  
  if (S->detailed) {
	printf ("@ PAGE_FAULT in P %d!\n", page);
  }
  
  if (S->listfree != -1) {
	// There are free frames
	last = S->listfree;
	frame = S->frt[last].next;
	
	if (frame==last) {
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

int choose_page_to_be_replaced (ssystem * S);
void replace_page (ssystem * S, int victim, int newpage);
void occupy_free_frame (ssystem * S, int frame, int page);

// Functions that show results

void print_report (ssystem * S);
void print_page_table (ssystem * S);
void print_frames_table (ssystem * S);
void print_replacement_report (ssystem * S);

#endif // _SIM_PAGING_H_

