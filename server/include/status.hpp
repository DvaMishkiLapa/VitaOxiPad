#ifndef STATUS_HPP
#define STATUS_HPP

#include <atomic>

#include "events.hpp"

extern std::atomic<bool> g_status_thread_running;

int status_thread(unsigned int arglen, void *argp);

#endif // STATUS_HPP
