#include "shared.h"

#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>

int main(int argc, char* argv[])
{
    if (argc != 5) {
        std::cerr << "Usage: Lab4Sender file semaphore_prefix number receiver_pid\n";
        return 1;
    }

    int fd = open(argv[1], O_RDWR);
    if (fd == -1) fail("open message file");

    std::string prefix = argv[2];
    sem_t* empty = sem_open((prefix + "_empty").c_str(), 0);
    sem_t* full = sem_open((prefix + "_full").c_str(), 0);
    if (empty == SEM_FAILED || full == SEM_FAILED) fail("sem_open Sender");

    char* end = nullptr;
    const long pid_value = std::strtol(argv[4], &end, 10);
    if (end == argv[4] || *end != '\0' || pid_value <= 0) {
        std::cerr << "Invalid Receiver PID.\n";
        return 1;
    }
    const pid_t receiver_pid = static_cast<pid_t>(pid_value);

    lock_file(fd, LOCK_EX);
    Header ready_header{};
    read_at(fd, &ready_header, sizeof(ready_header), 0);
    ++ready_header.ready_senders;
    write_at(fd, &ready_header, sizeof(ready_header), 0);
    lock_file(fd, LOCK_UN);

    if (kill(receiver_pid, SIGUSR1) == -1) fail("kill");

    std::cout << "Sender " << argv[3] << " is ready.\n";

    std::string command;
    while (true) {
        std::cout << "[Sender " << argv[3] << "] s - send, q - quit: " << std::flush;
        if (!std::getline(std::cin, command) || command == "q") break;
        if (command != "s") {
            std::cout << "Unknown command. Use s or q.\n";
            continue;
        }

        std::cout << "Message (1-19 bytes): " << std::flush;
        std::string text;
        if (!std::getline(std::cin, text)) break;
        if (text.empty() || text.size() >= sizeof(Message::text)) {
            std::cout << "Message length must be from 1 to 19 bytes.\n";
            continue;
        }

        std::cout << "Waiting for a free slot...\n" << std::flush;
        wait_sem(empty);
        lock_file(fd, LOCK_EX);

        Header header{};
        read_at(fd, &header, sizeof(header), 0);
        if (header.closed) {
            lock_file(fd, LOCK_UN);
            std::cout << "Receiver finished.\n";
            break;
        }

        Message message{};
        std::memcpy(message.text, text.data(), text.size());
        write_at(fd, &message, sizeof(message),
                 sizeof(Header) + static_cast<off_t>(header.write_index) * sizeof(Message));
        header.write_index = (header.write_index + 1) % header.capacity;
        ++header.message_count;
        write_at(fd, &header, sizeof(header), 0);

        lock_file(fd, LOCK_UN);
        post_sem(full);
        std::cout << "Sent: " << text << "\n";
    }

    lock_file(fd, LOCK_EX);
    Header header{};
    read_at(fd, &header, sizeof(header), 0);
    --header.active_senders;
    write_at(fd, &header, sizeof(header), 0);
    if (header.active_senders == 0) post_sem(full);
    lock_file(fd, LOCK_UN);

    sem_close(empty);
    sem_close(full);
    close(fd);

    std::cout << "Sender " << argv[3] << " finished.\n";
    return 0;
}
