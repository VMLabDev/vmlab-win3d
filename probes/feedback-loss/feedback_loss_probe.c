/* SPDX-License-Identifier: MIT */

#include <stdio.h>

/* Compile the production feedback implementation with its Mesa build flags. */
#include "yttrium_venus2_batch.c"

static unsigned reset_callbacks;
static enum pipe_reset_status reported_reset;

static void
observe_reset(void *data, enum pipe_reset_status status)
{
   unsigned *count = data;
   ++*count;
   reported_reset = status;
}

int
main(int argc, char **argv)
{
   SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
                SEM_NOOPENFILEERRORBOX);
   if (argc != 2 || (strcmp(argv[1], "--control") &&
                     strcmp(argv[1], "--read-revoked") &&
                     strcmp(argv[1], "--write-revoked") &&
                     strcmp(argv[1], "--read-unmapped") &&
                     strcmp(argv[1], "--write-unmapped"))) {
      fputs("usage: feedback_loss_probe.exe --control|--read-revoked|--write-revoked|--read-unmapped|--write-unmapped\n", stderr);
      return 2;
   }

   struct gdikmt_device device = {0};
   struct yttrium_venus venus = {0};
   struct pipe_device_reset_callback callback = {observe_reset, &reset_callbacks};
   gdikmt_device_set_reset_callback(&device, &callback);
   venus.device = &device;
   void *ring = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
   void *feedback = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
   if (!ring || !feedback) {
      fprintf(stderr, "VirtualAlloc failed: %lu\n", GetLastError());
      return 3;
   }
   venus.ring.head = (volatile uint32_t *)ring;
   venus.ring.tail = (volatile uint32_t *)ring + 1;
   venus.ring.status = (volatile uint32_t *)ring + 2;
   venus.ring.buffer = (uint8_t *)ring + 128;
   venus.ring.buffer_size = 256;
   venus.batch_feedback_mapping.map = feedback;
   venus.batch_feedback_buffer_size = sizeof(LONG);
   venus.batch_feedback_initialized = true;
   venus.batch_capacity = 1;
   volatile LONG *slot = yttrium_venus_batch_feedback_slot(&venus, 0);
   if (!yttrium_venus_batch_feedback_store(&venus, slot, VK_SUCCESS, "probe-live-store") ||
       yttrium_venus_wait_batch_feedback(&venus, 0, VK_NULL_HANDLE, "probe-live-load") != VK_SUCCESS ||
       !yttrium_venus_ring_probe(&venus, "probe-live-ring")) {
      fputs("live production feedback access failed\n", stderr);
      return 4;
   }

   const bool control = !strcmp(argv[1], "--control");
   const bool write = strstr(argv[1], "write") != NULL;
   const bool unmapped = strstr(argv[1], "unmapped") != NULL;
   if (!control) {
      DWORD old_protection;
      if (unmapped ? !VirtualFree(feedback, 0, MEM_RELEASE) :
                     !VirtualProtect(feedback, 4096, PAGE_NOACCESS, &old_protection)) {
         fprintf(stderr, "feedback mapping revocation failed: %lu\n", GetLastError());
         return 5;
      }
   }
   printf("{\"probe\":\"feedback-loss\",\"phase\":\"access\",\"mode\":\"%s\"}\n", argv[1]);
   fflush(stdout);

   VkResult result = VK_SUCCESS;
   bool stored = false;
   if (write) {
      stored = yttrium_venus_batch_feedback_store(&venus, slot, VK_NOT_READY,
                                                  "probe-revoked-store");
      result = stored ? VK_SUCCESS : VK_ERROR_DEVICE_LOST;
   } else {
      result = yttrium_venus_wait_batch_feedback(&venus, 0, VK_NULL_HANDLE,
                                                 "probe-revoked-load");
   }
   const enum pipe_reset_status reset = gdikmt_device_get_reset_status(&device);
   const bool passed = control ? result == VK_SUCCESS && !venus.failed &&
                                    reset == PIPE_NO_RESET && !reset_callbacks :
                                result == VK_ERROR_DEVICE_LOST && !stored &&
                                    venus.failed && reset == PIPE_UNKNOWN_CONTEXT_RESET &&
                                    reported_reset == reset && reset_callbacks == 1;
   printf("{\"schema\":1,\"probe\":\"feedback-loss\",\"mode\":\"%s\",\"result\":\"%s\","
          "\"vk_result\":%d,\"store_accepted\":%s,\"backend_failed\":%s,"
          "\"reset_status\":%u,\"reset_callbacks\":%u,\"reported_reset\":%u}\n",
          argv[1], passed ? "pass" : "fail", result, stored ? "true" : "false",
          venus.failed ? "true" : "false", (unsigned)reset, reset_callbacks,
          (unsigned)reported_reset);
   if (!unmapped)
      VirtualFree(feedback, 0, MEM_RELEASE);
   VirtualFree(ring, 0, MEM_RELEASE);
   return passed ? 0 : 1;
}
