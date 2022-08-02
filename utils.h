#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <math.h>
#include <string.h>


#include "rio.h"
#include "message.pb.h"

// protocol design:
// CLIENT SEND <size: uint32_t> <operation: ADD(1) SUB(2) TERMINATION(3) : uint32_t> <operand: int32_t>
// SERVER SEND <size: uint32_t> <operation: COUNTER(4) : uint32_t>                   <operand: int32_t>

// Client -> Server
const int32_t OPERATION_ADD = 1;
const int32_t OPERATION_SUB = 2;
const int32_t OPERATION_TERMINATION = 3;

// Server -> Client
const int32_t OPERATION_COUNTER = 4;

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Open a SOCK_STREAM socket listening on the specified port.
     * @return sockfd if successful, -1 on failure
     */
    int listening_socket(int port);

    /**
     * @brief Open a socket to the specified hostname and port
     * @return sockfd if successful, -1 on failure
     */
    int connect_socket(const char *hostname, const int port);

    /**
     * @brief Accept a connection on the specified sockfd.
     * @return sockfd of new connection when successful, -1 on failure
     */
    int accept_connection(int sockfd);

    /**
     * @brief Read one message from sockfd and fill the parameters.
     *        Make sure that the whole message is read.
     * @return 0 on success, 1 on failure
     */
    int recv_msg(int sockfd, int32_t *operation_type, int64_t *argument);

    /**
     * @brief Send one message to sockfd given the parameters.
     *        Make sure that the whole message is sent.
     * @return 0 on success, 1 on failure
     */
    int send_msg(int sockfd, int32_t operation_type, int64_t argument);

    /**
     * @brief Convert an interger number in a char array (char *)
     * @return the converted char array
     */
    char *to_char_array(int number);

    /**
     * @brief Display the error msg
     * @return void
     */
    void err_report(bool condition, const char *errmsg);

#ifdef __cplusplus
}
#endif
