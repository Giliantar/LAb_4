#pragma once
#include <semaphore.h>

constexpr int BUF_SIZE = 10;
constexpr int MSG_LEN = 20;

struct SharedBuffer {
    sem_t mutex;
    sem_t empty;
    sem_t full;
    int head;
    int tail;
    char messages[BUF_SIZE][MSG_LEN];
};