#include "vm/swap.h"
#include <bitmap.h>
#include <debug.h>
#include <stdio.h>
#include "vm/frame.h"
#include "vm/page.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

/* The swap device. */
static struct block *swap_device;

/* Used swap pages. */
static struct bitmap *swap_bitmap;

/* Protects swap_bitmap. */
static struct lock swap_lock;

/* Number of sectors per page. */
#define PAGE_SECTORS (PGSIZE / BLOCK_SECTOR_SIZE)

/* Sets up swap. */
void
swap_init (void)
{
  swap_device = block_get_role (BLOCK_SWAP);
  if (swap_device == NULL)
    {
      printf ("no swap device--swap disabled\n");
      swap_bitmap = bitmap_create (0);
    }
  else
    swap_bitmap = bitmap_create (block_size (swap_device)
                                 / PAGE_SECTORS);
  if (swap_bitmap == NULL)
    PANIC ("couldn't create swap bitmap");
  lock_init (&swap_lock);
}

/* Swaps in page P, which must have a locked frame
   (and be swapped out). */
void
swap_in (struct page *p)
{
  size_t i;

  ASSERT (p->frame != NULL);
  ASSERT (lock_held_by_current_thread (&p->frame->lock));
  ASSERT (p->sector != (block_sector_t) -1);

  /* Read data from its home in the swap slot. */
  for (i = 0; i < PAGE_SECTORS; i++)
    block_read (swap_device, p->sector + i,
                p->frame->base + i * BLOCK_SECTOR_SIZE);

  /* *
   * DO NOT free the slot (bitmap_reset) and
   * DO NOT clear p->sector.
   * This swap slot is still the page's backing store
   * in case it gets evicted clean.
   * */
}

/* Swaps out page P, which must have a locked frame. */
bool
swap_out (struct page *p)
{
  size_t new_slot;
  size_t i;

  ASSERT (p->frame != NULL);
  ASSERT (lock_held_by_current_thread (&p->frame->lock));

  lock_acquire (&swap_lock);

  /* 1. Find a NEW, empty slot for the dirty data. */
  new_slot = bitmap_scan_and_flip (swap_bitmap, 0, 1, false);
  if (new_slot == BITMAP_ERROR)
    {
      lock_release (&swap_lock);
      return false;
    }

  /* 2. If the page was ALREADY in swap, free its OLD slot. */
  if (p->sector != (block_sector_t) -1)
    {
      bitmap_reset (swap_bitmap, p->sector / PAGE_SECTORS);
    }
  
  lock_release (&swap_lock);

  /* 3. Write the frame's data to the NEW slot. */
  block_sector_t new_sector = new_slot * PAGE_SECTORS;
  for (i = 0; i < PAGE_SECTORS; i++)
  {
    block_write (swap_device, new_sector + i,
                 (uint8_t *) p->frame->base + i * BLOCK_SECTOR_SIZE);
  }

  /* 4. Update the page to be a "swap-backed" page
   * pointing to its NEW home.
   */
  p->sector = new_sector;
  p->file = NULL;
  p->file_offset = 0;
  p->file_bytes = 0;

  return true;
}

/* --- ADD THIS FUNCTION --- */
/* Used by page_exit() to free a page's swap slot. */
void
swap_free (block_sector_t sector)
{
  if (sector == (block_sector_t) -1)
    return;
    
  lock_acquire (&swap_lock);
  bitmap_reset (swap_bitmap, sector / PAGE_SECTORS);
  lock_release (&swap_lock);
}