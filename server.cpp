#include "server.h"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <ctime>
#include <sstream>
#include <algorithm>
#include <cctype>

#define MAX_EVENTS 64
#define BUFFER_SIZE 1024


AsyncServer::AsyncServer() 
    : tcp_fd(-1), udp_fd(-1), epoll_fd(-1), running(false), 
      total_clients(0), current_clients(0) {
}

AsyncServer::~AsyncServer() {
    stop();
}

bool AsyncServer::setup_tcp_socket(int port) {
    tcp_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (tcp_fd == -1) {
        std::cerr << "Failed to create TCP socket" << std::endl;
        return false;
    }

    int opt = 1;
    if (setsockopt(tcp_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "Failed to set TCP socket options" << std::endl;
        close(tcp_fd);
        return false;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(tcp_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind TCP socket" << std::endl;
        close(tcp_fd);
        return false;
    }

    if (listen(tcp_fd, SOMAXCONN) < 0) {
        std::cerr << "Failed to listen on TCP socket" << std::endl;
        close(tcp_fd);
        return false;
    }

    return true;
}

bool AsyncServer::setup_udp_socket(int port) {
    udp_fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (udp_fd == -1) {
        std::cerr << "Failed to create UDP socket" << std::endl;
        return false;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(udp_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind UDP socket" << std::endl;
        close(udp_fd);
        return false;
    }

    return true;
}

bool AsyncServer::setup_epoll() {
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        std::cerr << "Failed to create epoll instance" << std::endl;
        return false;
    }

    // Добавляем TCP сокет в epoll
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = tcp_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tcp_fd, &event) == -1) {
        std::cerr << "Failed to add TCP socket to epoll" << std::endl;
        return false;
    }

    // Добавляем UDP сокет в epoll
    event.data.fd = udp_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, udp_fd, &event) == -1) {
        std::cerr << "Failed to add UDP socket to epoll" << std::endl;
        return false;
    }

    return true;
}

bool AsyncServer::start(int port) {
    if (!setup_tcp_socket(port) || !setup_udp_socket(port) || !setup_epoll()) {
        return false;
    }

    running = true;
    std::cout << "Server started on port " << port << std::endl;

    struct epoll_event events[MAX_EVENTS];

    while (running) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        
        if (nfds == -1) {
            if (errno == EINTR) continue;
            std::cerr << "epoll_wait error" << std::endl;
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == tcp_fd) {
                handle_tcp_connection();
            } else if (events[i].data.fd == udp_fd) {
                handle_udp_data();
            } else {
                handle_tcp_data(events[i].data.fd);
            }
        }
    }

    return true;
}

void AsyncServer::handle_tcp_connection() {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    int client_fd = accept(tcp_fd, (struct sockaddr*)&client_addr, &addr_len);
    if (client_fd == -1) {
        return;
    }

    // Устанавливаем неблокирующий режим
    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

    // Добавляем клиента в epoll
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = client_fd;
    
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
        close(client_fd);
        return;
    }

    total_clients++;
    current_clients++;
    
    std::cout << "New TCP client connected: " << inet_ntoa(client_addr.sin_addr) 
              << ":" << ntohs(client_addr.sin_port) 
              << " (Total: " << total_clients 
              << ", Current: " << current_clients << ")" << std::endl;
}

void AsyncServer::handle_tcp_data(int client_fd) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    while ((bytes_read = read(client_fd, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes_read] = '\0';
        std::string message(buffer);
        
        // Обрабатываем команду или зеркалим сообщение
        process_command(client_fd, message);
    }

    if (bytes_read == 0 || (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        remove_client(client_fd);
    }
}

void AsyncServer::handle_udp_data() {
    char buffer[BUFFER_SIZE];
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    ssize_t bytes_read = recvfrom(udp_fd, buffer, sizeof(buffer) - 1, 0,
                                 (struct sockaddr*)&client_addr, &addr_len);
    
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        std::string message(buffer);
        
        // Для UDP считаем каждое сообщение от нового клиента
        total_clients++;
        
        process_command(udp_fd, message, true, &client_addr);
    }
}

void AsyncServer::process_command(int fd, const std::string& message, bool is_udp, struct sockaddr_in* client_addr) {
    std::string cleaned_message = trim(message);
    
    std::string response;

    if (!cleaned_message.empty() && cleaned_message[0] == '/') {
        // Это команда
        if (cleaned_message == "/time") {
            response = get_current_time();
        } else if (cleaned_message == "/stats") {
            response = get_stats();
        } else if (cleaned_message == "/shutdown") {
            response = "Server shutting down...";
            send_response(fd, response, is_udp, client_addr);
            stop();
            return;
        } else {
            response = "Unknown command: " + cleaned_message;
        }
    } else {
        // Зеркалим сообщение
        response = cleaned_message;
    }

    // Добавляем перевод строки для лучшего отображения в telnet
    if (!is_udp) {
        response += "\r\n";
    }
    send_response(fd, response, is_udp, client_addr);
}

void AsyncServer::send_response(int fd, const std::string& response, bool is_udp, struct sockaddr_in* client_addr) {
    if (is_udp && client_addr) {
        sendto(udp_fd, response.c_str(), response.length(), 0,
               (struct sockaddr*)client_addr, sizeof(*client_addr));
    } else {
        send(fd, response.c_str(), response.length(), 0);
    }
}

std::string AsyncServer::get_current_time() {
    std::time_t now = std::time(nullptr);
    std::tm* tm = std::localtime(&now);
    
    char buffer[20];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm);
    
    return std::string(buffer);
}

std::string AsyncServer::get_stats() {
    std::stringstream ss;
    ss << "Total clients: " << total_clients 
       << ", Current clients: " << current_clients;
    return ss.str();
}

void AsyncServer::remove_client(int client_fd) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
    close(client_fd);
    current_clients--;
    
    std::cout << "TCP client disconnected. Current clients: " << current_clients << std::endl;
}

void AsyncServer::stop() {
    running = false;
    
    if (tcp_fd != -1) {
        close(tcp_fd);
        tcp_fd = -1;
    }
    
    if (udp_fd != -1) {
        close(udp_fd);
        udp_fd = -1;
    }
    
    if (epoll_fd != -1) {
        close(epoll_fd);
        epoll_fd = -1;
    }
    
    std::cout << "Server stopped" << std::endl;
}

std::string AsyncServer::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r\f\v");
    size_t end = str.find_last_not_of(" \t\n\r\f\v");
    
    if (start == std::string::npos) {
        return ""; // Пустая строка или только пробелы
    }
    
    return str.substr(start, end - start + 1);
}
