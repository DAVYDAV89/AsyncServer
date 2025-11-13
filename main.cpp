#include "server.h"
#include <iostream>
#include <csignal>
#include <cstdlib>

AsyncServer* server = nullptr;

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    if (server) {
        server->stop();
    }
}

int main(int argc, char* argv[]) {
    int port = 8080;
    
    if (argc > 1) {
        port = std::atoi(argv[1]);
    }
    
    // Устанавливаем обработчики сигналов
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    AsyncServer srv;
    server = &srv;
    
    if (!srv.start(port)) {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }
    
    return 0;
}