#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <unistd.h>
#include<fcntl.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "common.h"


#define BUFFER_SIZE 1024

struct SharedData{
    int buffer[1024];
    int front;
    int rear;
    int count;
    pthread_cond_t empty;
    pthread_mutex_t mutex;
};


pthread_mutex_t mutexFiles;
int tempTHREADNUM;
int PORT_NUMBER;
pthread_t *threadWorkers;

int clientCount = 0;
int bufferSize = 1024;
struct SharedData *sharedData;

int sigIntFlag = 0;

char serverDirName[256];


void enqueue(int data) {
    sharedData->rear = (sharedData->rear + 1) % bufferSize;
    int rear = sharedData->rear;
    sharedData->buffer[rear] = data;
    sharedData->count++;
}

int dequeue() {
    int data;
    int front = sharedData->front;
    data = sharedData->buffer[front];
    sharedData->front = (sharedData->front + 1) % bufferSize;
    sharedData->count--;
    return data;
}


void* workerThread(){
    int clientSocket;
    int message = 0;
    int numberofRecivedFiles = 0;
    int numFilesServer = 0;
    int bytes_received;
    int sent;
    FileInfo *recivedFiles = NULL;
    FileInfo *oldRecivedFiles = NULL;
    FileInfo *serverFiles = NULL;
    FileInfo *oldServerFiles = NULL;
    int operation;
    int i;
    int count;
    char tempLogFile[256];
    int logFileFd;
    while(1){
        pthread_mutex_lock(&sharedData->mutex);
        while(sharedData->count == 0) {
            pthread_cond_wait(&sharedData->empty, &sharedData->mutex);
            if(sigIntFlag == 1){
                pthread_mutex_unlock(&sharedData->mutex);
                pthread_exit(NULL); 
            }
        }
        clientSocket = dequeue();
        clientCount++;
        snprintf(tempLogFile, sizeof(tempLogFile), "clientLogFile%d", clientCount);
        logFileFd = open(tempLogFile, O_RDWR | O_CREAT | O_TRUNC, 0666);
        memset(tempLogFile, 0, sizeof(tempLogFile));
        pthread_mutex_unlock(&sharedData->mutex);
        
        numberofRecivedFiles = 0;
        numFilesServer = 0;

        while(1){
            if(sigIntFlag == 1){
                printf("\nSending close message to the client \n");
                operation = serverClosed;
                sent = send(clientSocket, &operation, sizeof(operation), 0);
                if (sent < 0) {
                    perror("Send failed");
                    exit(EXIT_FAILURE);
                }
                close(logFileFd);
                close(clientSocket);
                pthread_exit(NULL); 
            }
            else{
                operation = serverOpen;
                sent = send(clientSocket, &operation, sizeof(operation), 0);
                if (sent < 0) {
                    perror("Send failed");
                    exit(EXIT_FAILURE);
                }
            }
            // OLD FILE INFOS
            int oldServerNum = numFilesServer;
            int oldReceivedNum = numberofRecivedFiles;
            oldServerFiles = malloc(numFilesServer * (sizeof(FileInfo)));
            oldRecivedFiles = malloc(numberofRecivedFiles * (sizeof(FileInfo)));
            
            for (int j = 0; j < numFilesServer; j++) {
                oldServerFiles[j].modifiedTimeinTimeH = serverFiles[j].modifiedTimeinTimeH;
                strcpy(oldServerFiles[j].path, serverFiles[j].path);
                oldServerFiles[j].isDeleted = 1;
            }
            for (int j = 0; j < numberofRecivedFiles; j++) {
                oldRecivedFiles[j].modifiedTimeinTimeH = recivedFiles[j].modifiedTimeinTimeH;
                strcpy(oldRecivedFiles[j].path, recivedFiles[j].path);
                oldRecivedFiles[j].isDeleted = 1;
            }
            free(serverFiles);
            serverFiles = NULL;
            free(recivedFiles);
            recivedFiles = NULL;



            bytes_received = recv(clientSocket, &message, sizeof(message), 0);
            if (bytes_received < 0) {
                perror("Receive failed");
                exit(EXIT_FAILURE);
            }

            if(message == operationBye){
                printf("One of client is leaving..\n");
                close(logFileFd);
                close(clientSocket);
                break;
            }

            else if(message == sendFileInfos){
                bytes_received = recv(clientSocket, &numberofRecivedFiles, sizeof(numberofRecivedFiles), 0);
                if (bytes_received < 0) {
                    perror("Receive failed");
                    exit(EXIT_FAILURE);
                }
                recivedFiles = malloc(sizeof(FileInfo) * numberofRecivedFiles);
                if(recivedFiles == NULL){
                    perror("RecivedFIles null");
                    exit(EXIT_FAILURE);
                }
                count = numberofRecivedFiles;
                i = 0;
                while(count > 0){
                    bytes_received = recv(clientSocket, recivedFiles[i].path, sizeof(recivedFiles[i].path), 0);
                    if (bytes_received < 0) {
                        perror("Receive failed");
                        exit(EXIT_FAILURE);
                    }

                    bytes_received = recv(clientSocket, &recivedFiles[i].lenFile, sizeof(recivedFiles[i].lenFile), 0);
                    if (bytes_received < 0) {
                        perror("Receive failed");
                        exit(EXIT_FAILURE);
                    }

                    bytes_received = recv(clientSocket, &recivedFiles[i].modifiedTimeinTimeH, sizeof(recivedFiles[i].modifiedTimeinTimeH), 0);
                    if (bytes_received < 0) {
                        perror("Receive failed");
                        exit(EXIT_FAILURE);
                    }
                    count--;
                    i++;
                }

                for (int j = 0; j < numberofRecivedFiles; j++) {
                    recivedFiles[j].isDeleted = 0; // SAKIN SILME
                    //printf("File: %s\n", recivedFiles[j].path);
                    //printf("Last Modified: %ld\n", recivedFiles[j].modifiedTimeinTimeH);
                    //printf("-------------------------\n");
                }


                    

                pthread_mutex_lock(&mutexFiles);
                //her cagiridan once numFiles 0lanmali
                numFilesServer = 0;
                trackLastModified(serverDirName, &serverFiles, &numFilesServer, serverDirName);
                
                /*for (int j = 0; j < numFilesServer; j++) {
                    printf("File: %s\n", serverFiles[j].path);
                    printf("Last Modified: %ld\n", serverFiles[j].modifiedTimeinTimeH);
                    printf("-------------------------\n");
                }*/

                synchronizeDirectories(recivedFiles, numberofRecivedFiles,serverFiles, numFilesServer, clientSocket, serverDirName, oldServerFiles, oldServerNum, oldRecivedFiles, oldReceivedNum, logFileFd);
                
                //printf("\n\n\n");
                free(oldServerFiles);
                oldServerFiles = NULL;

                free(oldRecivedFiles);
                oldRecivedFiles = NULL;

                pthread_mutex_unlock(&mutexFiles);
            }
    
        }
    }

    printf("CIKTIM MI\n");
    close(clientSocket);
    pthread_exit(NULL); 
}


void sigint_handler(int sig) {
    printf("***********************\nProgram will be closed please wait!\n***********************\n");
    sigIntFlag = 1;
}

int main(int argc, char* argv[]) {
    if(argc != 4){
        printf("Wrong open command for server!\n");
        exit(EXIT_FAILURE);
    }



    copyString(serverDirName, argv[1], sizeof(serverDirName));
    printf("DirectoryName>%s\n", serverDirName);

    createDirectoryBase(serverDirName);

    printf("SERVRE DIR NAME:%s\n",serverDirName);

    tempTHREADNUM = atoi(argv[2]);
    PORT_NUMBER = atoi(argv[3]);

    int server_sock, client_sock;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    struct sigaction sa_int;
    memset(&sa_int, 0, sizeof(sa_int));
    sa_int.sa_handler = sigint_handler;
    sigaction(SIGINT, &sa_int, NULL);

    //trackLastModified("serverDir", &serverFiles, &numFilesServer, "serverDir");    //her cagiridan once numFiles 0lanmali

    sharedData = malloc(sizeof(struct SharedData));
    sharedData->front = 0;
    sharedData->rear = -1;
    sharedData->count = 0;
    pthread_mutex_init(&sharedData->mutex, NULL);
    pthread_mutex_init(&mutexFiles, NULL);
    pthread_cond_init(&sharedData->empty, NULL);
    threadWorkers = malloc(tempTHREADNUM * sizeof(pthread_t));;


    for (int i = 0; i < tempTHREADNUM; i++) {
        pthread_create(&threadWorkers[i], NULL, workerThread, NULL);
    }

    // Create server socket
    if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT_NUMBER);

    // Bind the socket to specified IP address and port
    if (bind(server_sock, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_sock, 100) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    

    printf("Server listening on port %d\n", PORT_NUMBER);

    while (1) {
        if(sigIntFlag == 1){
            break;
        }
        // Accept incoming connection
        if ((client_sock = accept(server_sock, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
            //perror("Accept failed");
            //exit(EXIT_FAILURE);
        }
        if(client_sock == -1){
            break;
        }
        //bytes_received = recv(client_sock, &received_loopCount, sizeof(received_loopCount), 0);
        //if (bytes_received < 0) {
        //    perror("Receive failed");
        //    exit(EXIT_FAILURE);
        //}

       /* bytes_received = recv(client_sock, &clientPid, sizeof(clientPid), 0);
        if (bytes_received < 0) {
            perror("Receive failed");
            exit(EXIT_FAILURE);
        }*/

        pthread_mutex_lock(&sharedData->mutex);
        enqueue(client_sock);
        pthread_mutex_unlock(&sharedData->mutex);
        pthread_cond_signal(&sharedData->empty);

        // Handle the client in a separate function
        //receiveStruct(client_sock, received_loopCount);
    }
    pthread_cond_broadcast(&sharedData->empty);
    for (int i = 0; i < tempTHREADNUM; i++) {
        pthread_join(threadWorkers[i], NULL);
    }
    // Close the server socket
    close(server_sock);

    return 0;
}
