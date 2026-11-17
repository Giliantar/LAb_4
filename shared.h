#pragma once

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <semaphore.h>
#include <signal.h>
#include <sys/file.h>
#include <unistd.h>

struct Header {
    int capacity;
    int read_index;
    int write_index;
    int closed;
    int active_senders;
    int message_count;
    int ready_senders;
};

struct Message {
    char text[20];
};

inline void fail(const char* action)
{
    std::perror(action);
    std::exit(EXIT_FAILURE);
}

inline void wait_sem(sem_t* semaphore)
{
    while (sem_wait(semaphore) == -1) {
        if (errno != EINTR) fail("sem_wait");
    }
}

inline void post_sem(sem_t* semaphore)
{
    if (sem_post(semaphore) == -1) fail("sem_post");
}

inline void lock_file(int fd, int operation)
{
    while (flock(fd, operation) == -1) {
        if (errno != EINTR) fail("flock");
    }
}

inline void read_at(int fd, void* data, std::size_t size, off_t offset)
{
    if (pread(fd, data, size, offset) != static_cast<ssize_t>(size)) {
        std::fputs("Cannot read message file.\n", stderr);
        std::exit(EXIT_FAILURE);
    }
}

inline void write_at(int fd, const void* data, std::size_t size, off_t offset)
{
    if (pwrite(fd, data, size, offset) != static_cast<ssize_t>(size)) {
        std::fputs("Cannot write message file.\n", stderr);
        std::exit(EXIT_FAILURE);
    }
}
