#pragma once

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <system_error>
#include <string>
#include <iostream>

#pragma comment(lib, "Ws2_32.lib")

#define DEBUG_NETWORK_PRINT 0

class WSASession
{

public:

   WSASession()
   {
      int ret = WSAStartup(MAKEWORD(2, 2), &data);
      if (ret != 0)
         throw std::system_error(WSAGetLastError(), std::system_category(), "WSAStartup Failed");
   }

   ~WSASession()
   {
      WSACleanup();
   }

private:

   WSAData data;
};

class UDPSocket
{
public:

   UDPSocket()
   {
      sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
      if (sock == INVALID_SOCKET)
         throw std::system_error(WSAGetLastError(), std::system_category(), "Error opening socket");
   
      // mode: 0: blocking mode 1: non-blocking mode  
      enum { socket_mode_blocking = 0, socket_mode_non_blocking = 1 };

      u_long mode = socket_mode_blocking; 
      if(ioctlsocket(sock, FIONBIO, &mode) != NO_ERROR)
        throw std::system_error(WSAGetLastError(), std::system_category(), "Error setting ioctlsocket");
   }

   ~UDPSocket()
   {
      closesocket(sock);
   }

   void SendTo(const std::string& address, unsigned short port, const char* buffer, int len, int flags = 0)
   {
      sockaddr_in to;
      to.sin_family = AF_INET;
      to.sin_addr.s_addr = inet_addr(address.c_str());
      to.sin_port = htons(port);

      int ret = sendto(sock, buffer, len, flags, reinterpret_cast<SOCKADDR*>(&to), sizeof(to));
      
      if (ret < 0)
         throw std::system_error(WSAGetLastError(), std::system_category(), "sendto failed");
   }

   void SendTo(sockaddr_in& address, const char* buffer, int len, int flags = 0)
   {
      int ret = sendto(sock, buffer, len, flags, reinterpret_cast<SOCKADDR*>(&address), sizeof(address));

      if (ret < 0)
         throw std::system_error(WSAGetLastError(), std::system_category(), "sendto failed");
   }

   int RecvFrom(char* buffer, int len, int flags = 0)
   {
      sockaddr_in from;
      int size = sizeof(from);

      int ret = recvfrom(sock, buffer, len, flags, reinterpret_cast<SOCKADDR*>(&from), &size);
      
      if (ret < 0)
         throw std::system_error(WSAGetLastError(), std::system_category(), "recvfrom failed");

#if DEBUG_NETWORK_PRINT
      // Some info on the sender side
      getpeername(sock, (SOCKADDR*)&from, &size);
      printf("Message from: %s port: %d\n", inet_ntoa(from.sin_addr), htons(from.sin_port));
#endif

      return ret;
   }

   void Bind(unsigned short port)
   {
      sockaddr_in add;
      add.sin_family = AF_INET;
      add.sin_addr.s_addr = htonl(INADDR_ANY);
      add.sin_port = htons(port);

      int ret = bind(sock, reinterpret_cast<SOCKADDR*>(&add), sizeof(add));

      if (ret < 0)
         throw std::system_error(WSAGetLastError(), std::system_category(), "Bind failed");

#if DEBUG_NETWORK_PRINT
      // Some info on the Server side...
      int size = sizeof(add);
      getsockname(sock, (SOCKADDR*)&add, &size);
      printf("Listening for connection from IP: %s on port: %d\n", inet_ntoa(add.sin_addr), htons(add.sin_port));
#endif
   }

private:

   WSASession session;
   SOCKET sock;
};
