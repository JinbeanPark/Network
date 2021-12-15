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
#include <string>
#include <assert.h>
#include <errno.h>
#include <iostream>
#include <sstream>

//#include <endian.h>

#include "CRC.h"

#define BUF_SIZE 1024 
#define TIMEOUT 10
#define MAX_FILE_SIZE 104857600 // 100 MiB = 104857600


#define CRCSIZE 8

void setNonblock(int sockfd) {
    int flags;
    flags = fcntl(sockfd, F_GETFL, 0);
    assert(flags != -1);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
}

int main(int argc, char **argv) {

    int sockfd, ret, totalReadBytes = 0;
    char buf[BUF_SIZE + 1];
    struct timeval tv;
    tv.tv_sec = TIMEOUT;
    tv.tv_usec = 0;
    fd_set writeFds;
    FD_ZERO(&writeFds);

    CRC crc = CRC();

    // Create a socket using TCP IP
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        std::cerr << "ERROR: Failed to create a socket" << std::endl;
        exit(1);
    }

    // Check the number of arguments
    if (argc != 4) {
        std::cerr << "ERROR: the number of arguments is incorrect" << std::endl;
        exit(1);
    }

    // Check the port number
    if (0 <= atoi(argv[2]) && atoi(argv[2]) <= 1023) {
        std::cerr << "ERROR: port number should NOT be between 0 to 1023" << std::endl;
        exit(1);
    }

    // Bind address to socket
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(atoi(argv[2]));
    serverAddr.sin_addr.s_addr = inet_addr(argv[1]);
    if (inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr) <= 0) {
        std::cerr << "Invalid ip address" << std::endl;
        exit(1);
    }
    memset(serverAddr.sin_zero, '\0', sizeof(serverAddr.sin_zero));
    

    // Open the file to check the size and send the file.
    std::fstream sendFile;
    sendFile.open(argv[3], std::ios::in | std::ios::out | std::ios::binary);

    std::cout << "Read the file successfully" << std::endl;

    // Set Non block mode to wait for only 10 sec
    setNonblock(sockfd);

    // Connect to the server
    int connectResult = connect(sockfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
    
    if (connectResult == -1 && errno != EINPROGRESS) {
        std::cerr << "ERROR: Failed to connect to server" << std::endl;
        exit(1);
    }
    
    // 4. Check whether the event happened for 10 sec
    FD_SET(sockfd, &writeFds);
    ret = select(sockfd + 1, NULL, &writeFds, NULL, &tv);
    if (ret == - 1) {
        std::cerr << "ERROR: Error happened for selecting" << std::endl;
        close(sockfd);
        exit(1);
    }
    else if (ret == 0) {
        std::cerr << "ERROR: Failed to select the fd for 10s" << std::endl;
        close(sockfd);
        exit(1);
    }

    char ipstr[INET_ADDRSTRLEN] = {'\0'};
    inet_ntop(serverAddr.sin_family, &serverAddr.sin_addr, ipstr, sizeof(ipstr));
    std::cout << "Set up a connection from: " << ipstr << ":" << ntohs(serverAddr.sin_port) << std::endl;

    while (true) {

        uint64_t crcCode = 0;
        
        memset(buf, '\0', BUF_SIZE);

        int readBytes = sendFile.read(buf, BUF_SIZE - 8).gcount(), sendBytes;

        
        if (totalReadBytes + readBytes + 8 >= MAX_FILE_SIZE) {
            std::cout << "The total size of read file exceeds 100 MiB" << std::endl;
            sendFile.close();
            close(sockfd);
            exit(0);
        }
        else {
            if (readBytes > 0) {
                totalReadBytes += (readBytes);
                
                std::cout << "Total read bytes: " << totalReadBytes << std::endl;

                crcCode = crc.get_crc_code((uint8_t*)buf, 64);
                
                buf[readBytes] = (char) ((crcCode & 0xFF00000000000000ull) >> 56);
                buf[readBytes + 1] = (char) ((crcCode & 0x00FF000000000000ull) >> 48);
                buf[readBytes + 2] = (char) ((crcCode & 0x0000FF0000000000ull) >> 40);
                buf[readBytes + 3] = (char) ((crcCode & 0x000000FF00000000ull) >> 32);
                buf[readBytes + 4] = (char) ((crcCode & 0x00000000FF000000ull) >> 24);
                buf[readBytes + 5] = (char) ((crcCode & 0x0000000000FF0000ull) >> 16);
                buf[readBytes + 6] = (char) ((crcCode & 0x000000000000FF00ull) >> 8);
                buf[readBytes + 7] = (char) ((crcCode & 0x00000000000000FFull) >> 0);

            }
        }
        
        if (readBytes <= 0)
            break;
        else {
            
            sendBytes = send(sockfd, buf, readBytes + 8, 0);

            std::cout << "Send " + std::to_string(sendBytes) + "bytes " << std::endl;
           
            if (sendBytes == -1) {
                std::cerr << "ERROR: Failed to send the file" << std::endl;
                std::cerr << strerror(errno) << std::endl;
                exit(1);
            }
            
            ret = select(sockfd + 1, NULL, &writeFds, NULL, &tv);
            if (ret == - 1) {
                std::cerr << "ERROR: Error happened for selecting" << std::endl;
                close(sockfd);
                exit(1);
            }
            else if (ret == 0) {
                std::cerr << "ERROR: Failed to send the bytes for 10s" << std::endl;
                close(sockfd);
                exit(1);
            }
        }
    }

    std::cout << "Total read bytes: " << totalReadBytes << std::endl;
    sendFile.close();
    close(sockfd);
    exit(0);
}

