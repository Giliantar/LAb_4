#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "common.h"

using namespace std;

int main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " <shared_file>\n";
        return 1;
    }

    string filename = argv[1];

    int fd = open(filename.c_str(), O_RDWR);
    if (fd == -1) {
        perror("open");
        return 1;
    }

    SharedBuffer* buf = (SharedBuffer*)mmap(nullptr, sizeof(SharedBuffer), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buf == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }

    cout << "[Sender] Ready to work. Waiting for messages...\n";

    while (true) {
        cout << "[Sender] Enter message (or 'exit'): ";
        string msg;
        getline(cin, msg);

        if (msg == "exit") break;

        if (msg.length() >= MSG_LEN) {
            cout << "Message too long! Max " << MSG_LEN - 1 << " chars.\n";
            continue;
        }

        sem_wait(&buf->empty);
        sem_wait(&buf->mutex);

        strncpy(buf->messages[buf->tail], msg.c_str(), MSG_LEN);
        buf->tail = (buf->tail + 1) % BUF_SIZE;

        sem_post(&buf->mutex);
        sem_post(&buf->full);
    }

    munmap(buf, sizeof(SharedBuffer));
    close(fd);
    return 0;
}