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
#define BUFFER_SIZE 1024

struct MyStruct{
    char fileName[512];
    char *dataReaded;
};



enum Operations{
    downloadFiles, uploadFiles, sendFileInfos, operationBye, fileDone, fileEmpty, 
    fileContentOp, fileContentDone, updateFilesFromClient, updateFilesFromServer,
    doneSync, serverClosed, serverOpen, deleteFile
};

typedef struct {
    char path[512];
    off_t lenFile;
    time_t modifiedTimeinTimeH;
    int isDeleted;
} FileInfo;


int createDirectoryBase(char* directoryName) {
    struct stat st;
    
    // Check if the directory exists
    if (stat(directoryName, &st) == 0 && S_ISDIR(st.st_mode)) {
        printf("Directory already exists.\n");
        return 0;
    } else {
        // Create the directory
        int result = mkdir(directoryName, 0777);
        if (result == 0) {
            printf("Directory created successfully.\n");
            return 1;
        } else {
            printf("Failed to create directory.\n");
            return -1;
        }
    }
}

void removeParentDirectory(char *filePath, const char *parentDirectory) {
    size_t parentLen = strlen(parentDirectory);
    size_t filePathLen = strlen(filePath);

    if (parentLen >= filePathLen) {
        // Parent directory is longer or equal to the file path, cannot remove it
        return;
    }

    if (strncmp(filePath, parentDirectory, parentLen) == 0 && filePath[parentLen] == '/') {
        // Remove the parent directory from the file path
        memmove(filePath, filePath + parentLen + 1, filePathLen - parentLen);
        filePath[filePathLen - parentLen - 1] = '\0';
    }
}

void sendFileContentOne(int socket, int fromFd){
    int readedBytes;
    int writtedBytes;
    char bufferData[BUFFER_SIZE];
    enum Operations decide = fileContentOp;
    off_t lenFile = lseek(fromFd, 0, SEEK_END);

    if(lenFile <= 0){
        decide = fileEmpty;
    }
    else{
        lseek(fromFd, 0 , SEEK_SET);
   }

   send(socket, &decide, sizeof(decide), 0);
    

    //read write here
    while(decide == fileContentOp){
        memset(&bufferData, 0, sizeof(bufferData));
        readedBytes = read(fromFd, &bufferData, BUFFER_SIZE);
        if(readedBytes < BUFFER_SIZE){
            decide = fileContentDone;
        }
        writtedBytes = send(socket, &decide, sizeof(decide), 0);
        if(writtedBytes == -1){
            perror("here1: socket send");
        }
        writtedBytes =send(socket, &bufferData, readedBytes, 0);
        if(writtedBytes == -1){
            perror("here2: socket send");
        }

        recv(socket, &decide, sizeof(decide), 0);
    }

    close(fromFd);

}


void reciveFileContentOne(int socket, int toFd){
    int readedBytes, writtedBytes;
    char bufferData[BUFFER_SIZE+1];
    enum Operations decide = fileContentOp;
    recv(socket, &decide, sizeof(decide), 0);

    if(decide == fileEmpty){
        return;
    }

    //read write
    while(decide == fileContentOp){
        readedBytes = recv(socket, &decide, sizeof(decide), 0);
        if(readedBytes == -1){
            perror("here3: socket recv");
        }

        memset(&bufferData, 0, sizeof(bufferData));
        readedBytes = recv(socket, &bufferData, sizeof(bufferData), 0);
        if(readedBytes == -1){
            perror("here4: socket recv");
        }
        bufferData[readedBytes] = '\0';
        writtedBytes = write(toFd, &bufferData, readedBytes);
        if(writtedBytes == -1){
            perror("here5: write error");
        }
        send(socket, &decide, sizeof(decide), 0);
    }
    close(toFd);
}

void copyString(char dest[], const char src[], size_t destSize) {
    size_t i = 0;
    while (src[i] != '\0' && i < destSize - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

void createDirectories(const char* baseDir, const char* path) {
    // Count the number of slashes in the path
    int count = 0;
    for (int i = 0; path[i] != '\0'; i++) {
        if (path[i] == '/')
            count++;
    }
    
    if (count == 0) {
        //fprintf(stderr, "Invalid path: %s\n", path);
        return;
    }
    
    char* pathCopy = strdup(path);
    
    char* savePtr;
    char* directory = strtok_r(pathCopy, "/", &savePtr);
    
    char fullPath[256];
    snprintf(fullPath, sizeof(fullPath), "%s", baseDir);
    
    for (int i = 0; i < count; i++) {
        strncat(fullPath, "/", sizeof(fullPath) - strlen(fullPath) - 1);
        strncat(fullPath, directory, sizeof(fullPath) - strlen(fullPath) - 1);
        struct stat st;
        if (stat(fullPath, &st) == -1) {
            // Directory doesn't exist, create it
            if (mkdir(fullPath, 0777) == -1) {
                perror("Error creating directory");
                free(pathCopy);
                return;
            }
        } else if (!S_ISDIR(st.st_mode)) {
            fprintf(stderr, "%s is not a directory\n", fullPath);
            free(pathCopy);
            return;
        }
        
        directory = strtok_r(NULL, "/", &savePtr);
    }
    
    free(pathCopy);
}

void trackLastModified(const char *directory, FileInfo **files, int *numFiles, const char*dirTemp) {
    DIR *dir;
    struct dirent *entry;
    struct stat fileStat;
    char filePath[512];
    char tempPath[512];

    dir = opendir(directory);
    if (dir == NULL) {
        perror("opendir");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            snprintf(filePath, sizeof(filePath), "%s/%s", directory, entry->d_name);

            if (stat(filePath, &fileStat) < 0) {
                perror("stat");
                continue;
            }

            if (S_ISREG(fileStat.st_mode)) { // Check if the entry is a regular file
                // Allocate memory for the new file entry
                FileInfo *newFile = malloc(sizeof(FileInfo));
                if (newFile == NULL) {
                    perror("malloc");
                    break;
                }
                
                // Get the last modified time of the file
                strcpy(tempPath, filePath);
                removeParentDirectory(tempPath, dirTemp);
                strncpy(newFile->path, tempPath, sizeof(newFile->path));
                newFile->lenFile = fileStat.st_size;
                newFile->modifiedTimeinTimeH = fileStat.st_mtime;
                newFile->isDeleted = 0;
                
                // Resize the array if needed
                FileInfo *temp = realloc(*files, (*numFiles + 1) * sizeof(FileInfo));
                if (temp == NULL) {
                    perror("realloc");
                    free(newFile); // Free the memory allocated for the new file entry
                    break;
                }
                *files = temp;
                
                // Assign the new file entry to the array
                (*files)[*numFiles] = *newFile;
                (*numFiles)++;
                
                free(newFile);
            } else if (S_ISDIR(fileStat.st_mode)) { // Check if the entry is a directory
                trackLastModified(filePath, files, numFiles, dirTemp); // Recursive call for subdirectory
            }
        }
    }

    closedir(dir);
}

void receiveFile(int client_sock, FileInfo *sendFile, int existFlag, int index, char *dirName) {
    int sent, bytes_received;
    int checkSys = 0;


    sent = send(client_sock, sendFile[index].path, sizeof(sendFile[index].path), 0);
    if (sent < 0) {
        perror("Send failed receiveFile1");
        exit(EXIT_FAILURE);
    }

    bytes_received = recv(client_sock, &checkSys, sizeof(checkSys), 0);
    if(bytes_received == -1){
        perror("here3: socket recv");
    }

    char tempPath[512];
    createDirectories(dirName, sendFile[index].path);
    strcpy(tempPath,dirName);
    strcat(tempPath, "/");
    strcat(tempPath, sendFile[index].path);
    // Open the file
    int file = open(tempPath, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (file < 0) {
        perror("Failed to open file");
    }

    reciveFileContentOne(client_sock, file);
}


void receiveFileFromServer(int client_sock, char *dirName) {
    int bytes_received;
    int checkSys = 0;
    FileInfo data;

    bytes_received = recv(client_sock, data.path,  sizeof(data.path), 0);
    if (bytes_received < 0) {
        perror("Receive failed");
        exit(EXIT_FAILURE);
    }

    bytes_received = recv(client_sock, &checkSys, sizeof(checkSys), 0);
    if(bytes_received == -1){
        perror("here3: socket recv");
    }

    char tempPath[512];
    createDirectories(dirName, data.path);
    strcpy(tempPath,dirName);
    strcat(tempPath, "/");
    strcat(tempPath, data.path);
    // Open the file
    int file = open(tempPath, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (file < 0) {
        perror("Failed to open file");
    }

    reciveFileContentOne(client_sock, file);
}

void sendFileContent(int sock, char *dirName){
    FileInfo data;
    int checkSync = 1903;
    int sent;
    int bytes_received = recv(sock, data.path,  sizeof(data.path), 0);
    if (bytes_received < 0) {
        perror("Receive failed");
        exit(EXIT_FAILURE);
    }

    char path[512];
    strcpy(path,dirName);
    strcat(path, "/");
    strcat(path, data.path);

    // Open the file
    int file = open(path, O_RDONLY);
    if (file < 0) {
        perror("Failed to open file");
    }

    sent = send(sock, &checkSync, sizeof(checkSync), 0);
        if(sent == -1){
            perror("sendFileContent 1 send error");
        }

    sendFileContentOne(sock, file);

}

void sendFileContentFromServer(int sock, FileInfo data, char *dirName){
    int checkSync = 1903;
    int sent;
    sent = send(sock, data.path, sizeof(data.path), 0);
    if (sent < 0) {
        perror("Send failed receiveFile1");
        exit(EXIT_FAILURE);
    }
    char path[512];

    strcpy(path,dirName);
    strcat(path, "/");
    strcat(path, data.path);
    // Open the file
    int file = open(path, O_RDONLY);
    if (file < 0) {
        perror("Failed to open file");
    }

    sent = send(sock, &checkSync, sizeof(checkSync), 0);
        if(sent == -1){
            perror("sendFileContent 1 send error");
        }

    sendFileContentOne(sock, file);

}

int compareDates(char* date1, char* date2) {
    return strcmp(date1, date2);
}

//CLIENT -> SERVER
void synchronizeDirectories(FileInfo *receivedFiles, int numsourceFiles, FileInfo *serverFiles, int numdestFiles, int socketFd, char *dirName, FileInfo *oldServerFiles, int oldServerNum, FileInfo *oldRecivedFiles, int oldRecivedNum, int logFileFd) {
    // Check for files in receivedFiles that are missing or have different sizes in serverFiles
    int sent;
    int operation;
    int foundMatch = 0;
    char logMessage[1024];

    // CLIENTTAN SILINEN FILE SERVERDAN DA SILINMESI ICIN
    for(int i = 0; i < oldRecivedNum; i++){
        for(int j = 0; j < numsourceFiles; j++){
            if(strcmp(oldRecivedFiles[i].path, receivedFiles[j].path) == 0)
                oldRecivedFiles[i].isDeleted = 0;
        }
    }

    char tempPath[512];

    for(int i = 0; i < oldRecivedNum; i++){
        if(oldRecivedFiles[i].isDeleted == 1){
            memset(tempPath, 0, sizeof(tempPath));
            strcpy(tempPath,dirName);
            strcat(tempPath, "/");
            strcat(tempPath, oldRecivedFiles[i].path);
            if (remove(tempPath) == -1) {
                //perror("Remove error");
            }

            snprintf(logMessage, sizeof(logMessage), "%s is deleted client->server\n",oldRecivedFiles[i].path);
            write(logFileFd, logMessage, strlen(logMessage));
            memset(logMessage, 0, sizeof(logMessage));

            for(int j = 0; j < numdestFiles; j++){
                if(strcmp(oldRecivedFiles[i].path, serverFiles[j].path)==0)
                    serverFiles[j].isDeleted = 1;
            }
        }
    }


    // SERVERDAN SILINEN FILE SERVERDAN DA SILINMESI ICIN
    for(int i = 0; i < oldServerNum; i++){
        for(int j = 0; j < numdestFiles; j++){
            if(strcmp(oldServerFiles[i].path, serverFiles[j].path) == 0)
                oldServerFiles[i].isDeleted = 0;
        }
    }

    for(int i = 0; i < oldServerNum; i++){
        if(oldServerFiles[i].isDeleted == 1){       
            for(int j = 0; j < numsourceFiles; j++){
                if(strcmp(oldServerFiles[i].path, receivedFiles[j].path)==0){
                    receivedFiles[j].isDeleted = 1;
                    operation = deleteFile;
                    sent = send(socketFd, &operation, sizeof(operation), 0);
                    if (sent < 0) {
                        perror("Send failed");
                        exit(EXIT_FAILURE);
                    }

                    sent = send(socketFd, oldServerFiles[i].path, sizeof(oldServerFiles[i].path), 0);
                    if (sent < 0) {
                        perror("Send failed receiveFile1");
                        exit(EXIT_FAILURE);
                    }

                    snprintf(logMessage, sizeof(logMessage), "%s is deleted server->client\n",oldServerFiles[i].path);
                    write(logFileFd, logMessage, strlen(logMessage));
                    memset(logMessage, 0, sizeof(logMessage));
                }
            }
            
        }
    }



    for (int i = 0; i < numsourceFiles; i++) {
        foundMatch = 0;

        if(receivedFiles[i].isDeleted == 0){
            for (int j = 0; j < numdestFiles; j++) {
                if (strcmp(receivedFiles[i].path, serverFiles[j].path) == 0) {

                    foundMatch = 1;
                    if(receivedFiles[i].lenFile != serverFiles[j].lenFile){
                        if(receivedFiles[i].modifiedTimeinTimeH > serverFiles[j].modifiedTimeinTimeH){
                            operation = updateFilesFromClient;
                            sent = send(socketFd, &operation, sizeof(operation), 0);
                            if (sent < 0) {
                                perror("Send failed");
                                exit(EXIT_FAILURE);
                            }
                            receiveFile(socketFd, receivedFiles, 0, i, dirName);

                            snprintf(logMessage, sizeof(logMessage), "%s is updating client->server\n",receivedFiles[i].path);
                            write(logFileFd, logMessage, strlen(logMessage));
                            memset(logMessage, 0, sizeof(logMessage));
                        }
                        else{
                            operation = updateFilesFromServer;
                            sent = send(socketFd, &operation, sizeof(operation), 0);
                            if (sent < 0) {
                                perror("Send failed");
                                exit(EXIT_FAILURE);
                            }
                            sendFileContentFromServer(socketFd, serverFiles[i], dirName);
                            snprintf(logMessage, sizeof(logMessage), "%s is updating server->client\n",serverFiles[i].path);
                            write(logFileFd, logMessage, strlen(logMessage));
                            memset(logMessage, 0, sizeof(logMessage));
                        }
                    }
                }
            }

            if (!foundMatch) {
                operation = uploadFiles;
                sent = send(socketFd, &operation, sizeof(operation), 0);
                if (sent < 0) {
                    perror("Send failed");
                    exit(EXIT_FAILURE);
                }
                receiveFile(socketFd, receivedFiles, 0, i, dirName);
                snprintf(logMessage, sizeof(logMessage), "%s is downloading from client-> to server\n",receivedFiles[i].path);
                write(logFileFd, logMessage, strlen(logMessage));
                memset(logMessage, 0, sizeof(logMessage));
            }
        }
    }





    for(int i = 0; i < numdestFiles; i++){
        foundMatch = 0;
        if(serverFiles[i].isDeleted == 0){
            for(int j = 0; j < numsourceFiles; j++){
                if (strcmp(serverFiles[i].path, receivedFiles[j].path) == 0){
                    foundMatch = 1;
                    break;
                }
            }
            if (!foundMatch) {
                operation = downloadFiles;
                sent = send(socketFd, &operation, sizeof(operation), 0);
                if (sent < 0) {
                    perror("Send failed");
                    exit(EXIT_FAILURE);
                }
                sendFileContentFromServer(socketFd, serverFiles[i], dirName);
                snprintf(logMessage, sizeof(logMessage), "%s is downloading from server-> to client\n",serverFiles[i].path);
                write(logFileFd, logMessage, strlen(logMessage));
                memset(logMessage, 0, sizeof(logMessage));    
            }
        }
    }

    operation = doneSync;
    sent = send(socketFd, &operation, sizeof(operation), 0);
    if (sent < 0) {
        perror("Send failed");
        exit(EXIT_FAILURE);
    }

}
