#ifndef THREADS_FLAGS_H
#define THREADS_FLAGS_H

#define FLAG_MBS (1 << 1)   /**< Must be set. */
#define FLAG_IF (1 << 9)    /**< Interrupt Flag. */

/* Page fault error code bits. */
#define PF_P 0x1    /**< 0: not-present page. 1: access rights violation. */
#define PF_W 0x2    /**< 0: read, 1: write. */
#define PF_U 0x4    /**< 0: kernel, 1: user process. */

#endif 
