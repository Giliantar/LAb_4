#include "shared.h"

#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>

static constexpr const char* ENV_NAME = "SHPAKOVICH";

static void on_ready_signal(int)
{
}

static void install_ready_handler()
{
    struct sigaction action {};
    action.sa_handler = on_ready_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    if (sigaction(SIGUSR1, &action, nullptr) == -1) fail("sigaction");
}

static void wait_for_senders(int fd, int count)
{
    sigset_t blocked;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGUSR1);

    sigset_t old_mask;
    if (sigprocmask(SIG_BLOCK, &blocked, &old_mask) == -1) fail("sigprocmask");

    while (true) {
        Header header{};
        lock_file(fd, LOCK_EX);
        read_at(fd, &header, sizeof(header), 0);
        lock_file(fd, LOCK_UN);
        if (header.ready_senders >= count) break;
        if (sigsuspend(&old_mask) == -1 && errno != EINTR) fail("sigsuspend");
    }

    if (sigprocmask(SIG_SETMASK, &old_mask, nullptr) == -1) fail("sigprocmask");
}

std::string quote_shell(const std::string& text)
{
    std::string result = "'";
    for (char symbol : text) {
        if (symbol == '\'') result += "'\\''";
        else result += symbol;
    }
    return result + "'";
}

std::string script_name(const std::string& prefix, int number)
{
    return "/tmp" + prefix + "_sender_" + std::to_string(number) + ".command";
}

int main(int, char* argv[])
{
    std::cout << "File name: 1 - type, 2 - from environment (" << ENV_NAME << "): ";
    int option;
    if (!(std::cin >> option)) return 1;
    std::cin.ignore();

    std::string filename;
    if (option == 1) {
        std::cout << "Binary file name: ";
        std::getline(std::cin, filename);
    } else if (option == 2) {
        const char* value = std::getenv(ENV_NAME);
        if (!value || !*value) {
            std::cerr << "Environment variable " << ENV_NAME
                      << " does not exist or is empty.\n";
            return 1;
        }
        filename = value;
    } else {
        std::cerr << "Choose 1 or 2.\n";
        return 1;
    }

    if (filename.empty()) {
        std::cerr << "File name cannot be empty.\n";
        return 1;
    }

    int capacity, sender_count;
    std::cout << "Number of message slots: ";
    if (!(std::cin >> capacity) || capacity <= 0) {
        std::cerr << "Queue capacity must be a positive integer.\n";
        return 1;
    }

    std::cout << "Number of Senders: ";
    if (!(std::cin >> sender_count) || sender_count <= 0) {
        std::cerr << "Sender count must be a positive integer.\n";
        return 1;
    }
    std::cin.ignore();

    filename = std::filesystem::absolute(filename).string();
    int fd = open(filename.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0600);
    if (fd == -1) fail("open message file");

    Header header{capacity, 0, 0, 0, sender_count, 0, 0};
    write_at(fd, &header, sizeof(header), 0);
    if (ftruncate(fd, sizeof(Header) + static_cast<off_t>(capacity) * sizeof(Message)) == -1)
        fail("ftruncate");

    std::string prefix = "/lab4_" + std::to_string(getpid());
    sem_t* empty = sem_open((prefix + "_empty").c_str(), O_CREAT | O_EXCL, 0600, capacity);
    sem_t* full = sem_open((prefix + "_full").c_str(), O_CREAT | O_EXCL, 0600, 0);
    if (empty == SEM_FAILED || full == SEM_FAILED) fail("sem_open Receiver");

    install_ready_handler();

    std::string sender_path =
        (std::filesystem::absolute(argv[0]).parent_path() / "Lab4Sender").string();
    if (!std::filesystem::exists(sender_path)) {
        std::cerr << "Cannot find Sender executable: " << sender_path << '\n';
        return 1;
    }

    const pid_t receiver_pid = getpid();

    for (int number = 1; number <= sender_count; ++number) {
        std::string path = script_name(prefix, number);
        std::ofstream script(path);
        if (!script) fail("create Sender script");
        script << "#!/bin/sh\nexec " << quote_shell(sender_path) << ' '
               << quote_shell(filename) << ' ' << quote_shell(prefix) << ' '
               << number << ' ' << receiver_pid << '\n';
        script.close();
        if (chmod(path.c_str(), 0700) == -1) fail("chmod");
        if (std::system(("open -a Terminal " + quote_shell(path)).c_str()) != 0) {
            std::cerr << "Cannot open Terminal for Sender.\n";
            return 1;
        }
    }

    std::cout << "Waiting until all Senders are ready...\n" << std::flush;
    wait_for_senders(fd, sender_count);

    std::string command;
    while (true) {
        std::cout << "[Receiver] r - read, q - quit: " << std::flush;
        if (!std::getline(std::cin, command) || command == "q") break;
        if (command != "r") {
            std::cout << "Unknown command. Use r or q.\n";
            continue;
        }

        std::cout << "Waiting for a message...\n" << std::flush;
        wait_sem(full);
        lock_file(fd, LOCK_EX);

        read_at(fd, &header, sizeof(header), 0);
        if (header.message_count == 0 && header.active_senders == 0) {
            lock_file(fd, LOCK_UN);
            std::cout << "All Senders finished. No messages left.\n";
            break;
        }

        Message message{};
        read_at(fd, &message, sizeof(message),
                sizeof(Header) + static_cast<off_t>(header.read_index) * sizeof(Message));
        header.read_index = (header.read_index + 1) % header.capacity;
        --header.message_count;
        write_at(fd, &header, sizeof(header), 0);

        lock_file(fd, LOCK_UN);
        post_sem(empty);
        std::cout << "Received: " << message.text << '\n';
    }

    lock_file(fd, LOCK_EX);
    read_at(fd, &header, sizeof(header), 0);
    header.closed = 1;
    write_at(fd, &header, sizeof(header), 0);
    lock_file(fd, LOCK_UN);

    for (int i = 0; i < sender_count; ++i) post_sem(empty);

    sem_close(empty);
    sem_close(full);
    sem_unlink((prefix + "_empty").c_str());
    sem_unlink((prefix + "_full").c_str());

    for (int number = 1; number <= sender_count; ++number)
        unlink(script_name(prefix, number).c_str());

    close(fd);
    std::cout << "Receiver finished. Enter q in any remaining Sender windows.\n";
    return 0;
}
