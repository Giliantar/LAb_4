#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "common.h"

using namespace std;

int main() {
    cout << "--- Receiver is starting ---\n";

    cout << "Choose how to get the binary filename:\n";
    cout << "1. Enter from console\n";
    cout << "2. Get from environment variable\n";
    cout << "Choice: ";

    int choice;
    cin >> choice;
    cin.ignore();

    string filename;

    if (choice == 1) {
        cout << "Enter binary filename: ";
        getline(cin, filename);
    }
    else if (choice == 2) {
        const char* env_var_name = "POBEGAYLO";
        const char* env_value = getenv(env_var_name);

        if (env_value == nullptr) {
            cerr << "Error: Environment variable '" << env_var_name << "' not found!\n";
            cerr << "Please create it first in terminal, e.g.:\n";
            cerr << "export " << env_var_name << "=\"my_file.bin\"\n";
            return 1;
        }
        filename = env_value;
        cout << "Got filename from env: " << filename << "\n";
    }
    else {
        cerr << "Invalid choice.\n";
        return 1;
    }

    int fd = open(filename.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd == -1) {
        perror("open");
        return 1;
    }

    if (ftruncate(fd, sizeof(SharedBuffer)) == -1) {
        perror("ftruncate");
        close(fd);
        return 1;
    }

    SharedBuffer* buf = (SharedBuffer*)mmap(nullptr, sizeof(SharedBuffer), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buf == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }

    sem_init(&buf->mutex, 1, 1);
    sem_init(&buf->empty, 1, BUF_SIZE);
    sem_init(&buf->full, 1, 0);
    buf->head = 0;
    buf->tail = 0;

    cout << "Enter number of Senders: ";
    int num_senders;
    cin >> num_senders;
    cin.ignore();

    for (int i = 0; i < num_senders; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            execl("./sender", "sender", filename.c_str(), nullptr);
            perror("execl failed");
            exit(1);
        }
    }

    while (true) {
        cout << "\n[Receiver] Enter 'r' to read message or 'q' to quit: ";
        string cmd;
        getline(cin, cmd);

        if (cmd == "q") break;
        if (cmd != "r") continue;

        cout << "[Receiver] Waiting for a message...\n";
        sem_wait(&buf->full);
        sem_wait(&buf->mutex);

        string msg = buf->messages[buf->head];
        buf->head = (buf->head + 1) % BUF_SIZE;
        cout << "[Receiver] Got message: " << msg << "\n";

        sem_post(&buf->mutex);
        sem_post(&buf->empty);
    }

    sem_destroy(&buf->mutex);
    sem_destroy(&buf->empty);
    sem_destroy(&buf->full);
    munmap(buf, sizeof(SharedBuffer));
    close(fd);

    cout << "--- Receiver finished ---\n";
    return 0;
}