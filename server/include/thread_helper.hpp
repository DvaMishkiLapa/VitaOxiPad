#include <psp2/kernel/threadmgr.h>

#ifndef THREAD_HELPER_HPP
#define THREAD_HELPER_HPP

struct ThreadParams {
  const char *name;
  SceKernelThreadEntry entry;
  int initPriority;
  SceSize stackSize;
  SceUInt attr;
  int cpuAffinityMask;
  const SceKernelThreadOptParam *option;
  void *threadArgs;
  SceSize threadArgSize;
};

SceUID create_and_start_thread(const ThreadParams &params);
int stop_thread(SceUID thread_uid, SceUInt timeout);

#endif // THREAD_HELPER_HPP
