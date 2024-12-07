#include <psp2/libdbg.h>

#include "thread_helper.hpp"

SceUID create_and_start_thread(const ThreadParams &params) {
  SceUID threadUid =
      sceKernelCreateThread(params.name, params.entry, params.initPriority, params.stackSize,
                            params.attr, params.cpuAffinityMask, params.option);

  if (threadUid < 0) {
    SCE_DBG_LOG_ERROR("Error creating thread %s: 0x%08X", params.name, threadUid);
    return -1;
  }

  int startResult = sceKernelStartThread(threadUid, params.threadArgSize, params.threadArgs);
  if (startResult < 0) {
    SCE_DBG_LOG_ERROR("Error starting thread %s: 0x%08X", params.name, startResult);
    sceKernelDeleteThread(threadUid);
    return -1;
  }

  return threadUid;
}

int stop_thread(SceUID thread_uid, SceUInt timeout) {
  int wait_result = sceKernelWaitThreadEnd(thread_uid, NULL, &timeout);
  if (wait_result < 0) {
    SCE_DBG_LOG_ERROR("Error waiting for thread to end: 0x%08X", wait_result);
  }
  int delete_result = sceKernelDeleteThread(thread_uid);
  if (delete_result < 0) {
    SCE_DBG_LOG_ERROR("Error deleting thread: 0x%08X", delete_result);
  }
  SCE_DBG_LOG_TRACE("Thread stopped and deleted successfully");
  return delete_result;
}