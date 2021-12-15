/*
Jinbean Park - 805330751
*/

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <fstream>
#include <thread>
#include <vector>
#include <sys/select.h>
#include <sys/time.h>
#include <assert.h>
#include <csignal>
#include <signal.h>
#include <iostream>
#include <sstream>

#include "CRC.h"
//#include <endian.h>

#define BUF_SIZE 1024
#define TIMEOUT 10 
#define MAX_FILE_SIZE 104857600 // 100 MiB = 104857600
#define CRCSIZE 8

CRC crc = CRC();
std::vector<std::thread> connectedClients;
bool connecting = true;
int sockfd;

void setNonblock(int sockfd) {
    int flags;
    flags = fcntl(sockfd, F_GETFL, 0);
    assert(flags != -1);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    std::cout << "Successfully set Non block!" << std::endl;
}

void signalHandler(int sigNum) {

    if (sigNum == SIGQUIT)
        fprintf(stderr, "Received SIGQUIT!\n");
    else if (sigNum == SIGTERM)
        fprintf(stderr, "Received SIGTERM\n");
    exit(0);
}

void connection(int clientSockfd, int numConnectCount, char *direc) {
    
    int ret, receivedBytes = 0, totalReceBytes = 0;
    char buf[BUF_SIZE + 1] = {0};
    struct timeval tv;
    tv.tv_sec = TIMEOUT;
    tv.tv_usec = 0;
    fd_set readFds;
    FD_ZERO(&readFds);
    FD_SET(clientSockfd, &readFds);
    

    // Set the name of received file.
    std::cout << direc << std::endl;

    std::string recvFile(direc);
    
    if (recvFile[recvFile.length() - 1] == '/')
        recvFile += (std::to_string(numConnectCount) + ".file");
    else
        recvFile += ('/' + std::to_string(numConnectCount) + ".file");


    // Open the file to save the received data.
    std::ofstream receivedFile(recvFile, std::fstream::binary);
    if (receivedFile.fail()) {
        std::cerr << "ERROR: Failed to open received file" << std::endl;
        close(clientSockfd);
        return;
    }

    // read / write data from / into the connection
    while (connecting) {

        memset(buf, '\0', BUF_SIZE);

        // Receive the data from a client.
        receivedBytes = recv(clientSockfd, buf, BUF_SIZE, 0);

        std::cout << "Received bytes: " << receivedBytes << std::endl;
        
        if (receivedBytes == -1 && errno != EWOULDBLOCK && errno != EAGAIN) {
            std::cerr << "ERROR: Failed to receive a data from a client" << std::endl;
            std::fprintf(stderr, "RECV ERROR: %s\n", strerror(errno));
            receivedFile.close();
            close(clientSockfd);
            return;
        }
        else if (receivedBytes == 0)
            break;

        // Check whether the size of received file is bigger than 100 MiB.
        if (totalReceBytes + receivedBytes >= MAX_FILE_SIZE) {
            std::cout << "The total size of received file exceeds 100 MiB" << std::endl;
            receivedFile.write(buf, MAX_FILE_SIZE - totalReceBytes);
            receivedFile.close();
            close(clientSockfd);
            return;
        }

        // Check whether the received data has error by using CRC code before saving it in the file.
        else if (receivedBytes > 0){

            uint64_t crcReceived = (uint64_t) (
                ((((uint64_t) buf[receivedBytes - CRCSIZE + 0]) << 56) & 0xFF00000000000000) |
                ((((uint64_t) buf[receivedBytes - CRCSIZE + 1]) << 48) & 0x00FF000000000000) |
                ((((uint64_t) buf[receivedBytes - CRCSIZE + 2]) << 40) & 0x0000FF0000000000) |
                ((((uint64_t) buf[receivedBytes - CRCSIZE + 3]) << 32) & 0x000000FF00000000) |
                ((((uint64_t) buf[receivedBytes - CRCSIZE + 4]) << 24) & 0x00000000FF000000) |
                ((((uint64_t) buf[receivedBytes - CRCSIZE + 5]) << 16) & 0x0000000000FF0000) |
                ((((uint64_t) buf[receivedBytes - CRCSIZE + 6]) << 8)  & 0x000000000000FF00) |
                ((((uint64_t) buf[receivedBytes - CRCSIZE + 7]) << 0)  & 0x00000000000000FF)
            );
            
            char dataWord[receivedBytes - CRCSIZE];
            strncpy(dataWord, buf, receivedBytes - CRCSIZE);

            if (crc.get_crc_code((u_int8_t*)dataWord, 64) == (u_int64_t)crcReceived) {

                std::cout << "CRC code is correct" << std::endl;

                totalReceBytes += receivedBytes;
                
                // Write the data to file if there is no error in the received file.
                receivedFile.write(buf, receivedBytes);
            }
            else {
                std::cerr << "ERROR: CRC code is incorrect" << std::endl;
                char errorMsg[29] = "ERROR: CRC code is incorrect";
                receivedFile.write(errorMsg, 29);
                receivedFile.close();
                close(clientSockfd);
                return;
            }
        }
        
        // Check whether there is fd to receive / send file for 10s
        ret = select(clientSockfd + 1, &readFds, NULL, NULL, &tv);
        if (ret < 0) {
            std::cerr << "ERROR: Failed to select the fd which ready to receive / send files" << std::endl;
            receivedFile.close();
            close(clientSockfd);
            return;
        }
        else if (ret == 0) {
            std::cerr << "ERROR: Failed to find fd for 10s" << std::endl;
            char errorMsg[33] = "ERROR: Failed to find fd for 10s";
            receivedFile.write(errorMsg, 33);
            receivedFile.close();
            close(clientSockfd);
            return;
        }
        else {
            std::cerr << "Connection succeeded" << std::endl;
        }
    }

    std::cout << "Total received bytes: " << totalReceBytes << std::endl;
    std::cout << "The number of " << numConnectCount << " successfully sent the file" << std::endl;
    std::cout << "Sucessfully saved the file " << recvFile << std::endl;
    receivedFile.close();
    close(clientSockfd);
    return;
}

int main(int argc, char **argv) {

    int clientSockfd, numConnectCount = 0;
    char *direc;
    struct sockaddr_in clientAddr;
    socklen_t clientAddrSize = sizeof(clientAddr);

    // Execute the signalHandler if program receives SIGQUIT or SIGTERM.
    signal(SIGQUIT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // Check the number of arguments
    if (argc != 3) {
        std::cerr << "ERROR: the number of arguments is incorrect" << std::endl;
        exit(1);
    }

    // Assign the directory
    direc = argv[2];

    // Check the port number
    if (0 <= atoi(argv[1]) && atoi(argv[1]) <= 1023) {
        std::cerr << "ERROR: port number should NOT be between 0 to 1023" << std::endl;
        exit(1);
    }

    // The server open a listening socket on the specified port number
    // Create a socket using TCP IP
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        std::cerr << "ERROR: Failed to create a socket" << std::endl;
        exit(1);
    }

    // Allow others to reuse the address
    int yes = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
        std::cerr << "ERROR: Failed to reuse the address" << std::endl;
        //perror("setsockopt");
        exit(1);
    }

    // Bind address to socket
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(argv[1]));
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    memset(addr.sin_zero, '\0', sizeof(addr.sin_zero));

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        std::cerr << "ERROR: Failed to bind a socket" << std::endl;
        exit(1);
    }

    // Set socket to listen status
    // Listen() allows multiple TCP connection
    if (listen(sockfd, 10) < 0) {
        std::cerr << "ERROR: Failed to listen from clients" << std::endl;
        exit(1);
    }

    std::cout << ("Start listening") << std::endl;

    // The server should be able to accept and process connections from multiple clients at the same time.
    while (true) {

        // Accept a new connection
        if ((clientSockfd = accept(sockfd, (struct sockaddr *)&clientAddr, &clientAddrSize)) < 0) {
            continue;
        }

        // After connecting from client, set to non-block mode.
        setNonblock(clientSockfd);

        // The server must count all established connections.
        numConnectCount++;

        char ipstr[INET_ADDRSTRLEN] = {'\0'};
        inet_ntop(clientAddr.sin_family, &clientAddr.sin_addr, ipstr, sizeof(ipstr));
        std::cout << "Accept a connection from: " << ipstr << ":" << ntohs(clientAddr.sin_port) << std::endl;

        // Add a connected client object to vector "connectedClients".
        connectedClients.push_back(std::thread(connection, clientSockfd, numConnectCount, direc));
    }
}
