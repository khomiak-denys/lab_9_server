#include <winsock2.h>
#include <iostream>
#include <filesystem>
#include <vector>
#include <string>
#include <ctime>
#include <sstream>
#include <chrono>
#include <thread>
#include <Windows.h>

#pragma comment(lib, "ws2_32.lib")

namespace fs = std::filesystem;
using namespace std;
const int PORT = 8080;

static vector<string> vecDir, vecEx;
static vector<string> cache;
static auto startTime = std::chrono::system_clock::now();
static auto endTime = std::chrono::system_clock::now();

std::string getCreationTime(const std::string& filePath);
string GetFileInfo(const std::string& directoryPath, const std::string& extension, std::string& Info);
bool UseCache(string dir, string ex, int& index);
void clearCache();

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Failed to initialize Winsock" << std::endl;
        return 1;
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "Failed to create socket" << std::endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(PORT);

    if (bind(serverSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR) {
        std::cerr << "Bind failed" << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed" << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "Server listening on port " << PORT << "..." << std::endl;

    while (true) {
        SOCKET clientSocket = accept(serverSocket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "Accept failed" << std::endl;
            closesocket(serverSocket);
            WSACleanup();
            return 1;
        }

        std::cout << "Client connected" << std::endl;

        char buffer[10000];           // Отримання одного повідомлення від клієнта
        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0);

        if (bytesReceived > 0) {
            buffer[bytesReceived] = '\0';
            std::cout << "Received from client: " << buffer << std::endl;

            std::istringstream iss(buffer);               // Розділення отриманого повідомлення
            std::string extension, directory;
            iss >> extension;
            std::getline(iss, directory);
            directory.erase(0, directory.find_first_not_of(' '));


            std::string Info;              // Обробка кешу
            int ii = 0;
            auto elapsedTimeInSeconds = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now() - startTime).count();

            if (elapsedTimeInSeconds > 10) {
                clearCache();
                startTime = std::chrono::system_clock::now();
            }

            bool cacheUsed = UseCache(directory, extension, ii);

            if (cacheUsed) {
                Info = cache[ii];
            }
            else {
                GetFileInfo(directory, extension, Info);
                vecDir.push_back(directory);
                vecEx.push_back(extension);
                cache.push_back(Info);
                startTime = std::chrono::system_clock::now();
            }

            std::cout << "File Info(Name, size, creation time):\n" << Info << std::endl;

            if (send(clientSocket, Info.c_str(), Info.size(), 0) == SOCKET_ERROR) {             // Відправлення інформації клієнту
                std::cerr << "Send failed" << std::endl;
            }
            else {
                std::cout << "Message sent to client" << std::endl;
            }
        }
        else {
            std::cerr << "Receive failed or client disconnected" << std::endl;
        }
        std::cout << "Client disconnected" << std::endl;
        closesocket(clientSocket);
    }
    closesocket(serverSocket);
    WSACleanup();

    return 0;
}


std::string getCreationTime(const std::string& filePath) {
    std::filesystem::path path(filePath);
    LPCSTR lpcstr = filePath.c_str();

    if (std::filesystem::exists(path)) {
        FILETIME ftCreate, ftAccess, ftWrite;
        HANDLE hFile = CreateFile(lpcstr, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);

        if (hFile != INVALID_HANDLE_VALUE) {
            if (GetFileTime(hFile, &ftCreate, &ftAccess, &ftWrite)) {
                CloseHandle(hFile);

                FILETIME localFtCreate;
                FileTimeToLocalFileTime(&ftCreate, &localFtCreate);
                time_t* l;
                SYSTEMTIME st;
                FileTimeToSystemTime(&localFtCreate, &st);

                tm s = {};
                s.tm_year = st.wYear - 1900;  // Рік від 1900
                s.tm_mon = st.wMonth - 1;     // Місяць від 0 до 11
                s.tm_mday = st.wDay;
                s.tm_hour = st.wHour;
                s.tm_min = st.wMinute;
                s.tm_sec = st.wSecond;


                std::ostringstream on;
                on << s.tm_year + 1900 << "-" << s.tm_mon + 1 << "-" << s.tm_mday << "_" << s.tm_hour << ":" << s.tm_min << ":" << s.tm_sec << std::endl;
                std::string str = on.str();
                return str;
            }
            CloseHandle(hFile);
        }
    }
    return "File not found or creation time not available.";
}

string GetFileInfo(const std::string& directoryPath, const std::string& extension, std::string& Info) {
    try {
        std::ostringstream on;
        bool isExists = true, isEmpty = false;
        if (!fs::exists(directoryPath) || !fs::is_directory(directoryPath)) {
            std::cerr << "Error: Directory does not exist or is not a directory." << std::endl;
            string str = "Error: Directory does not exist or is not a directory.";
            Info = str.c_str();
            isExists = false;
        }

        if (isExists == true) {
            std::uintmax_t totalSize = 0;      // Initialize total size to 0
            vector<string> name, size_b, time_all;

            for (const auto& entry : fs::directory_iterator(directoryPath)) {   // Iterate over files in the directory
                if (fs::is_regular_file(entry)) {
                    if (entry.path().extension() == "." + extension) {
                        std::string fileName = entry.path().filename().string();
                        std::filesystem::file_time_type creationTime = fs::last_write_time(entry);

                        std::uintmax_t fileSize = fs::file_size(entry);
                        totalSize += fileSize;
                        name.push_back(fileName);
                        size_b.push_back(to_string(fileSize));
                    }
                }
            }

            if (totalSize == 0) {
                std::cerr << "Error: No files with the specified extension found in the directory." << std::endl;
                on << "Error: No files with the specified extension found in the directory." << std::endl;
                isEmpty = true;
            }
            if (isExists == false || isEmpty == true) {  // Print the total size after processing all files
                Info = on.str();
            }
            else {
                for (int i = 0; i < name.size(); i++) {
                    string filePath = directoryPath + "\\" + name[i];
                    time_all.push_back(getCreationTime(filePath));
                    on << name[i] << " " << size_b[i] << " " << time_all[i];
                }
                Info = on.str();
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return "asd";
}

bool UseCache(string dir, string ex, int& index) {
    for (int i = 0; i < vecDir.size(); i++) {
        if (vecDir[i] == dir && vecEx[i] == ex) {
            index = i;
            return true;
        }
    }
    return false;
}

void clearCache() {
    vecDir.clear();
    vecEx.clear();
    cache.clear();
}



