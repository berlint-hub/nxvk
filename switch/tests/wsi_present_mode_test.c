/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host test for the present-mode policy in wsi_switch.c.
 *
 * The bodies are extracted verbatim by extract-driver-tests.py, so this checks
 * the code the driver runs, not a copy of it. What it pins down is the thing
 * that was wrong: on Horizon the only presentation knob is VI's swap interval,
 * and mapping MAILBOX to interval 1 made the low-latency path vblank-locked
 * exactly like FIFO. No GPU or NWindow is involved here.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <vulkan/vulkan.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* Generated verbatim from wsi_switch.c by run-host-tests.sh. */
#include "wsi_present_mode.inc"

int
main(void)
{
   /* The surface advertises exactly these three; nothing else is honoured. */
   assert(wsi_switch_present_mode_supported(VK_PRESENT_MODE_FIFO_KHR));
   assert(wsi_switch_present_mode_supported(VK_PRESENT_MODE_MAILBOX_KHR));
   assert(wsi_switch_present_mode_supported(VK_PRESENT_MODE_IMMEDIATE_KHR));
   assert(!wsi_switch_present_mode_supported(VK_PRESENT_MODE_FIFO_RELAXED_KHR));
   assert(!wsi_switch_present_mode_supported(VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR));

   /* The default path is unchanged: whatever the application asked for, and
    * vblank lock everywhere except IMMEDIATE. */
   assert(wsi_switch_swap_interval(VK_PRESENT_MODE_FIFO_KHR, false) == 1);
   assert(wsi_switch_swap_interval(VK_PRESENT_MODE_MAILBOX_KHR, false) == 1);
   assert(wsi_switch_swap_interval(VK_PRESENT_MODE_IMMEDIATE_KHR, false) == 0);

   /* Low latency: an explicit unthrottled request is kept instead of being
    * replaced by MAILBOX; a vblank-locked or unknown request becomes MAILBOX. */
   assert(wsi_switch_low_latency_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR) ==
          VK_PRESENT_MODE_IMMEDIATE_KHR);
   assert(wsi_switch_low_latency_present_mode(VK_PRESENT_MODE_MAILBOX_KHR) ==
          VK_PRESENT_MODE_MAILBOX_KHR);
   assert(wsi_switch_low_latency_present_mode(VK_PRESENT_MODE_FIFO_KHR) ==
          VK_PRESENT_MODE_MAILBOX_KHR);
   assert(wsi_switch_low_latency_present_mode(VK_PRESENT_MODE_FIFO_RELAXED_KHR) ==
          VK_PRESENT_MODE_MAILBOX_KHR);

   /* Low latency must not sit behind a vblank: this is the regression. */
   assert(wsi_switch_swap_interval(VK_PRESENT_MODE_MAILBOX_KHR, true) == 0);
   assert(wsi_switch_swap_interval(VK_PRESENT_MODE_IMMEDIATE_KHR, true) == 0);
   /* ...and FIFO still means FIFO, because the application asked for it. */
   assert(wsi_switch_swap_interval(VK_PRESENT_MODE_FIFO_KHR, true) == 1);

   /* Every mode selection the low-latency path can make must end up either
    * FIFO-locked or interval 0, never a MAILBOX that silently paces to vblank. */
   const VkPresentModeKHR requests[] = {
      VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_MAILBOX_KHR,
      VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_RELAXED_KHR,
   };
   for (unsigned i = 0; i < ARRAY_SIZE(requests); i++) {
      const VkPresentModeKHR mode =
         wsi_switch_low_latency_present_mode(requests[i]);
      const uint32_t interval = wsi_switch_swap_interval(mode, true);
      assert(mode == VK_PRESENT_MODE_MAILBOX_KHR ||
             mode == VK_PRESENT_MODE_IMMEDIATE_KHR);
      assert(interval == 0);
   }

   puts("PASS: low-latency presents at swap interval 0; default path unchanged");
   return 0;
}
