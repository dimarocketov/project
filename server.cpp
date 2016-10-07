#define _CRT_SECURE_NO_WARNINGS

// Socket creation
#define TCP 0
// Timer
#define START true
#define STOP  false

#include <time.h>
#if defined(_WIN32) || defined(_WIN64)
#include <string>
#include <winsock.h>
#include <iostream>
#endif
#if defined(linux) || defined(__linux)
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include <signal.h>
typedef int SOCKET;
#endif

#pragma comment(lib, "Ws2_32.lib")
using namespace std;

bool sclose;

class Log
{
	unsigned int startTime;
	unsigned int endTime;
public:
	void Timer(bool action) {
		action == true ? startTime = clock() : endTime = clock();
		if (!action) cout << "        " << endTime - startTime << " ms" << endl;
	}
	void Delay(int seconds)
	{
#if defined(linux) || defined(__linux)
		sleep(seconds);
#else
		Sleep(seconds * 1000);
#endif
	}
	void Error(string message) {
		cout << "[Errr] " << message << " failed" << endl;
		Delay(3);
	}
	void Info(string message) {
		cout << "[Info] " << message << endl;
	}
	void Warn(string message) {
		cout << "[Warn] " << message << endl;
	}
};

class Connection
{
private:
	Log Log;
	int addressLen;
public:
	SOCKET sock;
	sockaddr_in address;
	void StartUp() {
#if defined(_WIN32) || defined(_WIN64)
		WSADATA WSAData;
		if (WSAStartup(0x202, &WSAData)) Log.Error("Starting");
#endif
		Log.Info("Started");
	}
	void CreateSocket(int protocol) {
		protocol == 0 ? sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)
			: sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		sock == -1 ? Log.Error("Socket creation") : Log.Info("Socket created");
	}
	void CloseSocket() {
		shutdown(sock, 2);
#if defined(linux) || defined(__linux) 
		close(sock);
#else 
		closesocket(sock);
#endif
		Log.Info("Accept complete");
	}
	void SetAddress(int port) {
		address.sin_family = AF_INET;
		address.sin_port = htons(port);
		address.sin_addr.s_addr = htonl(INADDR_ANY);
	}
	void Binding() {
		bind(sock, (sockaddr*)&address, sizeof(address)) != 0 ? Log.Error("Binding")
			: Log.Info("Binding complete");
	}
	void Listen() {
		listen(sock, SOMAXCONN) != 0 ? Log.Error("Listen")
			: Log.Info("Listen complete");
	}
	void Accept(SOCKET socket) {
		sock = accept(socket, (sockaddr*)&address, &addressLen);
		Log.Info("Client connected");
	}
	Connection() {
		addressLen = sizeof(address);
	}
	~Connection() {	}
} Server, Client;

class Processing
{
	SOCKET sock;
	sockaddr_in address;
	sockaddr_in sAddress;
	Log Log;
	int addressLen;
	int bytes;
	int bytesReceived;
	int bytesRead;
	int position;
	int inDownload;
	int inUpload;
	string data;
	string fileRequest;
	string oldIP;
	string newIP;
	string curPosInFile;
	char* filePack;
	char* OOBdata;
	char* answer;

	void SetRecvTimeout(int seconds) {
		struct timeval optval;
		optval.tv_sec = seconds;
		optval.tv_usec = 0;
		setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&optval, sizeof(optval));
		//	setsockopt(sockUDP, SOL_SOCKET, SO_RCVTIMEO, (char*)&optval, sizeof(optval));
	}
	void Send(string buf, int length) {
		if ((bytes = send(sock, buf.c_str(), length, 0)) < 0) Log.Error("Sending");
		if (send(sock, "O", 1, MSG_OOB) < 0)				  Log.Error("Sending OOB data");
		else cout << "       Bytes sent: " << bytes << endl;
	}
	void Recv(string &buf, int length) {
		char* buffer = new char[length];
		if ((bytes = recv(sock, buffer, length, 0)) < 0) Log.Error("Receiving");
		if (recv(sock, OOBdata, 1, MSG_OOB) < 0)         Log.Error("Receiving OOB data");
		else cout << "       Bytes recv: " << bytes << endl;
		buf = buffer;
		buf = buf.substr(0, bytes);
		delete buffer;
	}
	void GetTime() {
		time_t seconds = time(NULL);
		struct tm* timeinfo = localtime(&seconds);
		data = asctime(timeinfo);
	}
	int GetFileSize(FILE* file) {
		int fileSize = 0;
		fseek(file, 0, SEEK_END);
		fileSize = ftell(file);
		fseek(file, 0, SEEK_SET);
		return fileSize;
	}
	void IncompleteDownload() {
		Recv(curPosInFile, 8);
		curPosInFile[bytes] = '\0';
		if (curPosInFile.find('N') != string::npos) inDownload = 0;
		else { data = fileRequest;	position = stoi(curPosInFile); }
		Send("Y", 1);
	}
	void IncompleteUpload() {
		int size;
		FILE* lastFile;
		string answerStr;
		Recv(answerStr, 16);
		if (answer[0] == 'N') { inUpload = 0;	Send("Y", 1); }
		else {
			data = fileRequest;
			lastFile = fopen(fileRequest.substr(7, fileRequest.length()).c_str(), "rb");
			size = GetFileSize(lastFile);
			curPosInFile = to_string(size);
			fclose(lastFile);
			Send(curPosInFile, curPosInFile.length());
			inUpload = 1;
		}
	}
	void WithoutIncompletes() {
		string answerStr;
		oldIP = newIP;
		Send("N", 1);
		Recv(answerStr, 16);
		Send("Y", 1);
	}
	int DownloadTCP() {
		Log.Timer(START);
		int bytesSentTotal = 0;
		FILE* file;
		string answerStr;
		Recv(answerStr, 16);
		if ((file = fopen(data.c_str(), "rb")) == NULL) {
			if (send(sock, "File not found", 14, 0) < 0) Log.Error("Sending");
			position = 0;
			if (recv(sock, answer, 16, 0) < 0) Log.Error("Receiving");
			return 1;
		}
		int size = GetFileSize(file);
		fseek(file, position, SEEK_SET);
		position = 0;
		SetRecvTimeout(1000);
		while (ftell(file) < size)
		{
			bytesRead = fread(filePack, sizeof(char), 2048, file);
			if (send(sock, filePack, bytesRead, 0) < 0) Log.Error("Sending");
			else bytesSentTotal += bytesRead;
			if ((bytes = recv(sock, answer, 16, 0)) < 0) Log.Error("Receiving");
			else bytesReceived += bytes;
			if (bytes == -1) {
				inDownload = 1;
				break;
			}
		}
		SetRecvTimeout(100000);
		fclose(file);
		if (inDownload == 1) {
			Log.Warn("Connection problem");
			return 0;
		}
		else {
			data = "File downloaded";
			Send(data, data.length());
			cout << "       Bytes sent: " << bytesSentTotal << endl;
			Recv(answerStr, 16);
			cout << "       Bytes recv: " << bytesReceived << endl;
			return 1;
		}
		Log.Timer(STOP);
	}
	int UploadTCP() {
		Log.Timer(START);
		int bytesSent = 0;
		FILE* file = fopen(data.c_str(), "ab");
		SetRecvTimeout(1000);
		while (1) {
			if ((bytes = recv(sock, filePack, 2048, 0)) < 0) Log.Error("Receiving");
			else bytesReceived += bytes;
			if (bytes <= 0) {
				inUpload = 1;
				break;
			}
			if (send(sock, "C", 1, 0) < 0) Log.Error("Sending");
			else bytesSent++;
			filePack[bytes] = '\0';
			if (strcmp(filePack, "File uploaded") == 0) break;
			if (strcmp(filePack, "File not found") == 0) break;
			fwrite(filePack, sizeof(char), bytes, file);
		}
		SetRecvTimeout(100000);
		if (file != NULL) fclose(file);
		if (inUpload == 1) {
			Log.Warn("Connection problem");
			return 0;
		}
		else {
			if (recv(sock, OOBdata, 1, MSG_OOB) < 0) Log.Error("Receiving OOB data");
			else cout << "       Bytes recv: " << bytesReceived << endl;
			if (send(sock, "O", 1, MSG_OOB) < 0)		Log.Error("Sending OOB data");
			else cout << "       Bytes sent: " << bytesSent << endl;
		}
		Log.Timer(STOP);
		Log.Delay(1);
		return 1;
	}

public:
	void GetTarget(SOCKET socket, sockaddr_in addr) {
		sock = socket;
		address = addr;
		sAddress = Server.address;
		newIP = inet_ntoa(address.sin_addr);
	}
	void GetRequest() {
		bytesReceived = 0;
		if (inDownload == 0 && inUpload == 0) {
			char* reserve = new char[256];
			if ((bytes = recv(sock, reserve, 256, 0)) < 0) Log.Error("Receiving");
			if (recv(sock, OOBdata, 1, MSG_OOB) < 0)	   Log.Error("Receiving OOB data");
			else cout << "       Bytes recv: " << bytes << endl;
			data = reserve;
			data = data.substr(0, bytes - 1);
			Send("A", 1);
			delete reserve;
		}
		else { inDownload = 0; inUpload = 0; }
	}
	int ParseRequest() {
		if (data.substr(0, 5) == "close")	return 0;
		else if (data.substr(0, 6) == "sclose")	return 2;
		else if (data.substr(0, 4) == "time")	GetTime();
		else if (data.substr(0, 5) == "echo ")	data = data.substr(5, data.length());
		else if (data.substr(0, 9) == "download ") {
			fileRequest = data;
			data = data.substr(9, data.length());
			if (DownloadTCP() == 1) return 1;
			else return 0;
		}
		else if (data.substr(0, 7) == "upload ") {
			fileRequest = data;
			data = data.substr(7, data.length());
			if (UploadTCP() == 1) return 1;
			else return 0;
		}
		Send(data, 256);
		return 3;
	}
	Processing() {
		position = inDownload = inUpload = bytesReceived = 0;
		addressLen = sizeof(address);
		OOBdata = new char;
		answer = new char[16];
		filePack = new char[2049];
	}
	~Processing() {
		delete OOBdata;
		delete answer;
		delete filePack;
	}
};


DWORD WINAPI ClientThread(LPVOID lpCOM)
{
	SOCKET socket = (SOCKET)lpCOM;
	sockaddr_in address;
	Processing Handle = Processing();
	int aSize = sizeof(address);
	int result;

	getsockname(socket, (sockaddr*)&address, &aSize);
	Handle.GetTarget(socket, address);
	while (1) {
		Handle.GetRequest();
		result = Handle.ParseRequest();
		if (result == 0) break;
		if (result == 1) continue;
		if (result == 2) { sclose = true; break; }
	}
	closesocket(socket);
	if (sclose) Server.CloseSocket();
	return 0;
}

int main()
{
	Server = Connection();
	Client = Connection();

	Server.StartUp();
	Server.SetAddress(3000);
	Server.CreateSocket(TCP);
	Server.Binding();
	Server.Listen();

	DWORD dwThreadId;
	HANDLE hThread;

	while (1) {
		Client.Accept(Server.sock);
		if (sclose) break;
		hThread = CreateThread(NULL, 0, ClientThread, (LPVOID)Client.sock, 0, &dwThreadId);
	}
	Client.CloseSocket();
	return 0;
}