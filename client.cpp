#include <iostream>
#include <string>
#include <thread>
#include <mutex>

#include "message.pb.h"

#include "utils.h"

std::mutex print_lock;

/* thread-safe version of display */
void ts_printf(int64_t counter)
{
    print_lock.lock();
    printf("%ld\n", counter);
    print_lock.unlock();
}

void client(int connfd, int num_msg, int add, int sub)
{
    /* send operation & operand to server */
    int ret{0};
    for (int i = 0; i < num_msg; ++i)
    {
        if ((i & 0x1) == 0)
            ret = send_msg(connfd, 1, add);
        else
            ret = send_msg(connfd, 2, sub);
#ifdef DEBUG
        if (ret == 1)
        {
            fprintf(stderr, "error in client send message");
            return;
        }
#endif
    }

    /* send termination to server */
    ret = send_msg(connfd, 3, 0);
#ifdef DEBUG
    if (ret == 1)
    {
        fprintf(stderr, "error in client send message");
        return;
    }
#endif

    /* receive count number from server */
    int32_t operation_type{0};
    int64_t argument{0};
    ret = recv_msg(connfd, &operation_type, &argument);
#ifdef DEBUG
    if (ret == 1 || operation_type != 4)
    {
        fprintf(stderr, "error in client recv counter");
        return;
    }
#endif

    /* print the receive counter from server */
    ts_printf(argument);
    // fprintf(stderr, "final result is %d\n", argument);
}

int main(int args, char *argv[])
{
    if (args < 7)
    {
        std::cerr << "usage: ./client <num_threads> <hostname> <port> "
                     "<num_messages> <add> <sub>\n";
        exit(1);
    }

    /* get client argument */
    int numClients = std::atoi(argv[1]);
    std::string hostname = argv[2];
    int port = std::atoi(argv[3]);
    int numMessages = std::atoi(argv[4]);
    int add = std::atoi(argv[5]);
    int sub = std::atoi(argv[6]);

    /* spawn threads */
    std::vector<std::thread> clients;
    std::vector<int> clientfds;
    for (int i = 0; i < numClients; ++i)
    {
        /* build connection to server */
        int connfd = connect_socket(hostname.c_str(), port);
#ifdef DEBUG
        fprintf(stderr, "connect to server with fd = %d\n", connfd);
#endif
        if (connfd == -1)
            return -1;
        clientfds.push_back(connfd);
        clients.push_back(std::thread{client, connfd, numMessages, add, sub});
    }
    /* join threads */
    for (auto &client : clients)
    {
        client.join();
    }
    /* close fd */
    for (auto &clientfd : clientfds)
    {
        close(clientfd);
    }

    return 0;
}
