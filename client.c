#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/wait.h>
#include "common.h"

#define PORT 12345
#define BUFFER_SIZE 1024
int PORT_NUMBER;
FileInfo *sendFiles = NULL;
char clientDirName[256];
int sigIntFlag = 0;

void sendStruct(int sock, struct MyStruct *sendFile, int index) {
    struct MyStruct data = sendFile[index];
    // Send the name
    int sent = send(sock, data.fileName, sizeof(data.fileName), 0);
    if (sent < 0) {
        perror("Send failed");
        exit(EXIT_FAILURE);
    }

    // Send the length of the data
    int data_length = strlen(data.dataReaded) + 1;
    int length_sent = send(sock, (void *)&data_length, sizeof(int), 0);
    if (length_sent < 0) {
        perror("Send failed");
        exit(EXIT_FAILURE);
    }

    // Send the data in chunks
    int bytes_sent = 0;
    while (bytes_sent < data_length) {
        int remaining_bytes = data_length - bytes_sent;
        int chunk_size = (remaining_bytes < BUFFER_SIZE) ? remaining_bytes : BUFFER_SIZE;

        int sent = send(sock, data.dataReaded + bytes_sent, chunk_size, 0);
        if (sent < 0) {
            perror("Send failed");
            exit(EXIT_FAILURE);
        }

        bytes_sent += sent;
    }
}

void sendClientDirectoryInfos(int sock, char*dirPath){
    int mesagge = 0;
    int counter = 0;
    int i = 0;
    int sent;
    int numofFileToSend;
    if(sigIntFlag == 1){
    mesagge = operationBye;
        sent = send(sock, &mesagge, sizeof(mesagge), 0); //SIMDILIK OPERATIONBYE
        if (sent < 0) {
            perror("Send failed");
            exit(EXIT_FAILURE);
        }
    }
    else{
        mesagge = sendFileInfos;
        sent = send(sock, &mesagge, sizeof(mesagge), 0); //
        if (sent < 0) {
            perror("Send failed");
            exit(EXIT_FAILURE);
        }
        numofFileToSend = 0;
        trackLastModified(dirPath, &sendFiles, &numofFileToSend, dirPath);
        counter = numofFileToSend;
        sent = send(sock, &numofFileToSend, sizeof(numofFileToSend), 0); //ILK ONCE KAC DOSYA GONDERCEGIM
        if (sent < 0) {
            perror("Send failed");
            exit(EXIT_FAILURE);
        }
        i = 0;
        while(counter > 0){
            sent = send(sock, sendFiles[i].path, sizeof(sendFiles[i].path), 0); //SIRASIYLA DOSYABILGILERI
            if (sent < 0) {
                perror("Send failed");
                exit(EXIT_FAILURE);
            }

            sent = send(sock, &sendFiles[i].lenFile, sizeof(sendFiles[i].lenFile), 0); //SIRASIYLA DOSYABILGILERI
            if (sent < 0) {
                perror("Send failed");
                exit(EXIT_FAILURE);
            }

            sent = send(sock, &sendFiles[i].modifiedTimeinTimeH, sizeof(sendFiles[i].modifiedTimeinTimeH), 0); //SIRASIYLA DOSYABILGILERI
            if (sent < 0) {
                perror("Send failed");
                exit(EXIT_FAILURE);
            }

            i++;
            counter--;
        }
    }
}

void deleteFileFromClient(int socket, char *dirName){
    FileInfo deletedFile;
    int bytes_received = recv(socket, deletedFile.path,  sizeof(deletedFile.path), 0);
    if (bytes_received < 0) {
        perror("Receive failed");
        exit(EXIT_FAILURE);
    }

    char tempPath[512];
    strcpy(tempPath,dirName);
    strcat(tempPath, "/");
    strcat(tempPath, deletedFile.path);

    printf("DELETE FILE NAME : %s\n",tempPath);
    if (remove(tempPath) == -1) {
        perror("Remove error");
    }
}

void sigint_handler(int sig) {
    printf("***********************\nProgram will be closed please wait!\n***********************\n");
    sigIntFlag = 1;
}

int main(int argc, char* argv[]) {
    if (argc != 4 && argc != 3) {
        printf("Wrong open command for client!\n");
        printf("ARGC:%d\n", argc);
        exit(EXIT_FAILURE);
    }

    char tempIp[256];

    if(argc == 4){
        copyString(tempIp, argv[3], sizeof(tempIp));
    }

    PORT_NUMBER = atoi(argv[2]);
    
    copyString(clientDirName, argv[1], sizeof(clientDirName));
    printf(">>>>%s\n", clientDirName);
    createDirectoryBase(clientDirName);

    struct sigaction sa_int;
    memset(&sa_int, 0, sizeof(sa_int));
    sa_int.sa_handler = sigint_handler;
    sigaction(SIGINT, &sa_int, NULL);

    int sock = 0;
    struct sockaddr_in serv_addr;
    
    int bytes_received;

    // Create client socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT_NUMBER);

    if(argc == 4){
        if (inet_pton(AF_INET, tempIp, &serv_addr.sin_addr) <= 0) {
            perror("Invalid address/ Address not supported");
            exit(EXIT_FAILURE);
        }
    }

    else{
         // Convert IP address from text to binary form
        if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
            perror("Invalid address/ Address not supported");
            exit(EXIT_FAILURE);
        }

    }

   

    // Connect to the server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection failed");
        exit(EXIT_FAILURE);
    }

    int operation;
    while(1){
       
        bytes_received = recv(sock, &operation, sizeof(operation), 0);
        if (bytes_received < 0) {
            perror("Receive failed");
            exit(EXIT_FAILURE);
        }
        if(operation == serverClosed){
            printf("Server is closed program is terminating..\n");
            return 0;
        }        
        sendClientDirectoryInfos(sock, clientDirName);
        if(sigIntFlag == 1){
            close(sock);
            printf("Program is exiting...\n");
            break;
        }

        while(1){
            
            bytes_received = recv(sock, &operation, sizeof(operation), 0);
            if (bytes_received < 0) {
                perror("Receive failed");
                exit(EXIT_FAILURE);
            }
            printf("Checking Sync..\n");
            if(operation == uploadFiles){
                sendFileContent(sock, clientDirName);
            }

            else if(operation == downloadFiles){
                receiveFileFromServer(sock, clientDirName);
            }

            else if(operation == updateFilesFromClient){
                sendFileContent(sock, clientDirName);
            }

            else if(operation == updateFilesFromServer){
                receiveFileFromServer(sock, clientDirName);
            }
            
            else if(operation == deleteFile){
                deleteFileFromClient(sock, clientDirName);
            }

            else if(operation == doneSync){
                break;
            }
            
        }
        sleep(5);
    }

    close(sock);
    return 0;
}




