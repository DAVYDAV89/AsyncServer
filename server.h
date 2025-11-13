#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <atomic>
#include <unordered_map>
#include <sys/epoll.h>

class AsyncServer {
private:
    int tcp_fd;
    int udp_fd;
    int epoll_fd;
    bool running;
    std::atomic<int> total_clients;
    std::atomic<int> current_clients;
    std::unordered_map<int, std::string> client_buffers;

    std::string trim(const std::string& str);
    bool setup_tcp_socket(int port);
    bool setup_udp_socket(int port);
    bool setup_epoll();
    void handle_tcp_connection();
    void handle_tcp_data(int client_fd);
    void handle_udp_data();
    void process_command(int fd, const std::string& message, bool is_udp = false, struct sockaddr_in* client_addr = nullptr);
    void send_response(int fd, const std::string& response, bool is_udp = false, struct sockaddr_in* client_addr = nullptr);
    std::string get_current_time();
    std::string get_stats();
    void remove_client(int client_fd);

public:
    AsyncServer();
    ~AsyncServer();
    bool start(int port = 8080);
    void stop();
};

#endif