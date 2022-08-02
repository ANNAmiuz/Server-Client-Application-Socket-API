#include "utils.h"
#define MAXLINE 1024
#define LISTENQ 1024 /* Second argument to listen() */

extern "C"
{
    int listening_socket(int port_number)
    {
        struct addrinfo hints, *listp, *p;
        int listenfd, rc, optval = 1;

        /* Get a list of potential server addresses */
        memset(&hints, 0, sizeof(struct addrinfo));
        hints.ai_socktype = SOCK_STREAM;             /* Accept connections */
        hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG; /* ... on any IP address */
        hints.ai_flags |= AI_NUMERICSERV;            /* ... using port number */

        /* getaddrinfo error */
        char *port = to_char_array(port_number);
        if ((rc = getaddrinfo(NULL, port, &hints, &listp)) != 0)
        {
            fprintf(stderr, "getaddrinfo failed (port %s): %s\n", port, gai_strerror(rc));
            return -1;
        }

        /* Walk the list for one that we can bind to */
        for (p = listp; p; p = p->ai_next)
        {
            /* Create a socket descriptor */
            if ((listenfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0)
                continue; /* Socket failed, try the next */

            /* Eliminates "Address already in use" error from bind */
            setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(int));

            /* Bind the descriptor to the address */
            if (bind(listenfd, p->ai_addr, p->ai_addrlen) == 0)
                break; /* Success */
            if (close(listenfd) < 0)
            { /* Bind failed, try the next */
                fprintf(stderr, "open_listenfd close failed: %s\n", strerror(errno));
                return -1;
            }
        }

        /* Clean up */
        freeaddrinfo(listp);
        if (!p) /* No address worked */
            return -1;

        /* Make it a listening socket ready to accept connection requests */
        if (listen(listenfd, LISTENQ) < 0)
        {
            close(listenfd);
            return -1;
        }
        return listenfd;
    }

    int connect_socket(const char *hostname, const int port_number)
    {
        int clientfd, rc;
        struct addrinfo hints, *listp, *p;
        char *port = to_char_array(port_number);

        /* Get a list of potential server addresses */
        memset(&hints, 0, sizeof(struct addrinfo));
        hints.ai_socktype = SOCK_STREAM; /* Open a connection */
        hints.ai_flags = AI_NUMERICSERV; /* ... using a numeric port arg. */
        hints.ai_flags |= AI_ADDRCONFIG; /* Recommended for connections */

        /* getaddrinfo error */
        if ((rc = getaddrinfo(hostname, port, &hints, &listp)) != 0)
        {
            fprintf(stderr, "getaddrinfo failed (%s:%s): %s\n", hostname, port, gai_strerror(rc));
            return -1;
        }

        /* Walk the list for one that we can successfully connect to */
        for (p = listp; p; p = p->ai_next)
        {
            /* Create a socket descriptor */
            if ((clientfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0)
                continue; /* Socket failed, try the next */

            /* Connect to the server */
            if (connect(clientfd, p->ai_addr, p->ai_addrlen) != -1)
                break; /* Success */
            if (close(clientfd) < 0)
            { /* Connect failed, try another */ // line:netp:openclientfd:closefd
                fprintf(stderr, "open_clientfd: close failed: %s\n", strerror(errno));
                return -1;
            }
        }

        /* Clean up */
        freeaddrinfo(listp);
        if (!p) /* All connects failed */
            return -1;
        else /* The last connect succeeded */
            return clientfd;
    }

    int accept_connection(int sockfd)
    {
        // DEBUG: get client info
        // socklen_t clientlen;
        // struct sockaddr_storage clientaddr; /* Enough space for any address */
        // char client_hostname[MAXLINE], client_port[MAXLINE];

        // int connfd = accept(sockfd, &clientaddr, &clientlen);
        // Getnameinfo((SA *)&clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
        // printf("Connected to (%s, %s)\n", client_hostname, client_port);

        int connfd = accept(sockfd, NULL, NULL);
        return connfd;
    }

    int recv_msg(int sockfd, int32_t *operation_type, int64_t *argument)
    {
        // read the size of payload (bytes)
        char size_buf[sizeof(uint32_t)];
        ssize_t size_ret = rio_readn(sockfd, (void *)size_buf, sizeof(uint32_t));
        // err_report(size_ret <= 0, "recv read size <= 0");
        if (size_ret <= 0)
            return 1;
        uint32_t size = atoi(size_buf);

#ifdef DEBUG
        printf("the payload size is %s (%d). \n", size_buf, size);
#endif

        // get the payload
        sockets::message msg;
        char payload_buf[size];
        ssize_t payload_ret = rio_readn(sockfd, (void *)payload_buf, size);
        // err_report(payload_ret <= 0, "recv payload size <= 0");
        if (payload_ret <= 0)
            return 1;

        // analyze the payload
        msg.ParseFromArray(payload_buf, size);
        *operation_type = static_cast<int32_t>(msg.type());
        *argument = msg.argument();
        return 0;
    }

    int send_msg(int sockfd, int32_t operation_type, int64_t argument)
    {
        // make protobuf msg
        sockets::message msg;
        msg.set_type(static_cast<sockets::message_OperationType>(operation_type));
        msg.set_argument(argument);

        ssize_t size = msg.ByteSizeLong();
        char payload_buf[size];
        msg.SerializeToArray(payload_buf, size);

#ifdef DEBUG
        printf("the payload size is (%d). \n", size);
#endif

        // write the size of payload (bytes)
        char *size_buf = to_char_array(size);
        ssize_t ret = rio_writen(sockfd, (void *)size_buf, sizeof(uint32_t));
        if (ret <= 0)
            return 1;

        // write the payload
        ssize_t payload_ret = rio_writen(sockfd, (void *)payload_buf, size);
        if (payload_ret <= 0)
            return 1;

        return 0;
    }

    char *to_char_array(int number)
    {
        int n = log10(number) + 1;
        int i;
        char *numberArray = (char *)calloc(1, sizeof(uint32_t));
        for (i = n - 1; i >= 0; --i, number /= 10)
        {
            numberArray[i] = (number % 10) + '0';
        }
        return numberArray;
    }

    /* some helper functions */
    void err_report(bool condition, const char *errmsg)
    {
        if (condition)
        {
            perror(errmsg);
            exit(EXIT_FAILURE);
        }
    }
}