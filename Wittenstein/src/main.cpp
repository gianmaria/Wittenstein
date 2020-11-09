#include <stdio.h>
#include <string.h>

#include <iostream>
#include <string>

using std::string;
using std::cout;
using std::endl;


#include "tinythread.h"

//#include "Network.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

#include "wittenstein_protocol.h"

#define SCM_IP "127.0.0.1"
#define SCM_PORT "3001"
#define SCM_PORT_NUM 3001

#define LOCAL_PORT 6969


struct AxisInfo
{
   float position;
   float force;
   float current_trim;
   AxisStatus status;
};

struct JoystickInfo
{
   AxisInfo axes[2];
   unsigned short x, y;
   unsigned short switches;   
};

static JoystickInfo g_joystick;
static bool g_SCM_online = false;
bool g_invalid_response = true;

SOCKET sock = INVALID_SOCKET;
sockaddr_in SCM_addr = {};


#define DEBUG_PRINT 0

const char* axis_status_to_string(AxisStatus axis_status)
{
   if (axis_status.un_initialised)
   {
      return "un initialised";
   }
   else if (axis_status.initialising)
   {
      return "initialising";
   }
   else if (axis_status.passive)
   {
      return "passive";
   }
   else if (axis_status.active)
   {
      return "active";
   }
   else if (axis_status.motor_not_responding)
   {
      return "motor not responding";
   }
   else if (axis_status.not_used)
   {
      return "not_used";
   }
   else if (axis_status.terminated_due_to_an_unrecoverable_error)
   {
      return "terminated due to an unrecoverable error";
   }
   else
   {
      return "unknown axis status";
   }

}


void decode_can_message(const DTMResponse& resp)
{
   u16 val0 = (u16)resp.data_values[0]; // GRIP ID
   byte val1 = (byte)resp.data_values[1];
   byte val2 = (byte)resp.data_values[2];

   unsigned short buttons = 0;
   buttons = val1 << 8 | val2;

   byte val3 = (byte)resp.data_values[3];
   byte val4 = (byte)resp.data_values[4];
   byte val5 = (byte)resp.data_values[5];

   u16 y = val3 << 2 | ((val5 & 0x0c) >> 2);
   u16 x = val4 << 2 | ((val5 & 0x03) >> 0);

   g_joystick.x = x;
   g_joystick.y = y;

   g_joystick.switches = buttons;

#if DEBUG_PRINT
   printf("Grip ID: 0x%hX\n", val0);

   for (int i = 0;
        i < 16;
        ++i)
   {
      u16 mask = 0x1 << i;

      if (buttons & mask)
      {
         printf("  Grip Switch %d enabled\n", i + 1);
      }
   }

   printf("  Analog in 1 (y): %hd\n", y);
   printf("  Analog in 2 (x): %hd\n", x);
#endif

}

void decode_axes_status(const DTMResponse& resp)
{
   for (int i = 0;
        i < resp.num_of_axes;
        ++i)
   {
      u8 axis_status_raw = resp.data_values[i];

      AxisStatus* axis_status = (AxisStatus*)&axis_status_raw;

      g_joystick.axes[i].status = *axis_status;

#if DEBUG_PRINT
      const char* axis_status_str = axis_status_to_string(*axis_status);
      printf("Axis %d status: %s\n", i+1, axis_status_str);
#endif

   }

}

void decode_axes_position(const DTMResponse& resp)
{
   for (int i = 0;
        i < resp.num_of_axes;
        ++i)
   {
      g_joystick.axes[i].position = resp.data_values[i];
   }
}

void decode_axes_force(const DTMResponse& resp)
{
   for (int i = 0;
        i < resp.num_of_axes;
        ++i)
   {
      g_joystick.axes[i].force = resp.data_values[i];
   }
}

void decode_trim(const DTMResponse& resp)
{
   for (int i = 0;
        i < resp.num_of_axes;
        ++i)
   {
      g_joystick.axes[i].current_trim = resp.data_values[i];
   }
}




void send_data(const vector<char>& data)
{
   int ret = sendto(sock, data.data(), data.size(), 0, 
                           (SOCKADDR*)&SCM_addr, sizeof(SCM_addr));

   //printf("sendto() sent: %d bytes\n", ret);

   if (ret < 0)
   {
      printf("sendto() failed!");
   }
}

void get_data()
{
   enum { buff_size = 512 };

   vector<char> raw_response;
   raw_response.reserve(buff_size);

   int recv = 0;
   
   char buffer_[buff_size] = {};

   //sockaddr_in from = {};
   //int from_len = sizeof(from);
   
   while ((recv = recvfrom(sock, buffer_, buff_size, 0, 0/*(sockaddr*)&from*/, 0/*&from_len*/)) > 0)
   {
      raw_response.insert(raw_response.end(), buffer_, buffer_ + recv);
   }

   if (recv == 0)
   {
      // connection closed, never happen for UDP (?)
      int closed = 0;
   }
   else
   {
      int err = WSAGetLastError();

      switch (err)
      {
         case WSAECONNRESET: // Connection reset by peer.
         {
            g_SCM_online = false;
            return;
         } break;

         case WSAEWOULDBLOCK: // Resource temporarily unavailable.
         {
            // It is a nonfatal error, and the operation should be retried later.
            // Means no resource was availalre after a calls to recvfrom()
         } break;

         default:
         {
            enum { error_buffer_size = 1024 };
            char error_buffer[error_buffer_size] = {};

            FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                           NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                           error_buffer, error_buffer_size, NULL);

            printf("recvfrom() failed (%d) - '%s'\n", err, error_buffer);
         }
      }
   }

   g_SCM_online = true;

   raw_response.shrink_to_fit();
   auto dtm_resps = decodeResponse(raw_response);

   for (int resp_idx = 0;
        resp_idx < dtm_resps.size();
        ++resp_idx)
   {
      const DTMResponse& dtm_resp = dtm_resps[resp_idx];

      if (!dtm_resp.valid)
      {
         printf("Invalid respose\n");
         g_invalid_response = true;
         return;
      }

      g_invalid_response = false;


      switch (dtm_resp.data_code)
      {
         case WittProtocol::can_io_message:
         {
            decode_can_message(dtm_resp);
         } break;

         case WittProtocol::status:
         {
            decode_axes_status(dtm_resp);
         } break;

         case WittProtocol::axis_position:
         {
            decode_axes_position(dtm_resp);
         } break;

         case WittProtocol::input_force:
         {
            decode_axes_force(dtm_resp);
         } break;

         case WittProtocol::trim_command:
         {
            decode_trim(dtm_resp);
         } break;


         default:
         {
            printf("Response:\n");

            for (int i = 0;
                 i < raw_response.size();
                 ++i)
            {
               if (i != 0 && i % 16 == 0)
               {
                  printf("\n");
               }
               printf("%02hx ", (byte)raw_response[i]);
            }
            printf("\n");

            printf("Response (ASCII): ");

            for (int i = 0;
                 i < raw_response.size();
                 ++i)
            {
               char c = raw_response[i];
               if (c >= ' ' &&
                   c <= '~')
               {
                  printf("%c", c);
               }
               else
               {
                  printf(".");
               }
            }
            printf("\n");

         } break;
      }
   }

}

void model_exec()
{
   vector<char> msg;

#if 1
   msg = generateQuery(WittProtocol::status, WittProtocol::start_axis, WittProtocol::all_axes);
   send_data(msg);
   Sleep(50);
   get_data();

   msg = generateQuery(WittProtocol::axis_position, WittProtocol::start_axis, WittProtocol::all_axes);
   send_data(msg);
   Sleep(50);
   get_data();

   msg = generateQuery(WittProtocol::input_force, WittProtocol::start_axis, WittProtocol::all_axes);
   send_data(msg);
   Sleep(50);
   get_data();

#endif

   msg = generateQuery(WittProtocol::trim_command, WittProtocol::start_axis, WittProtocol::all_axes);
   send_data(msg);
   Sleep(50);
   get_data();
}

void perform_joystick_init()
{
   printf("Start traversal...\n");
   auto msg = generateDTM(WittProtocol::start_traversal, 0, 2);
   send_data(msg);
   Sleep(12000);

   printf("Going active...\n");
   msg = generateDTM(WittProtocol::go_active, 0, 2);
   send_data(msg);
   Sleep(2000);

   printf("Creating async CAN link...\n");
   msg = generateDTM(WittProtocol::create_asynchronous_link, 0, 1);
   send_data(msg);
   Sleep(2000);

   printf("Ready!\n");
}

int main()
{
   setbuf(stdout, NULL);
   
   HANDLE consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
   CONSOLE_CURSOR_INFO info;
   info.dwSize = 100;
   info.bVisible = FALSE;
   SetConsoleCursorInfo(consoleHandle, &info);

   WSAData data;
   if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
   {
      printf("WSAStartup() failed!\n");
      return 1;
   }

   struct addrinfo hints = {};
   hints.ai_flags = AI_PASSIVE; // socket used in a bind call
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_DGRAM;
   hints.ai_protocol = IPPROTO_UDP;
   
   struct addrinfo* result = 0;
   if (getaddrinfo(SCM_IP, SCM_PORT, &hints, &result) != 0)
   {
      printf("getaddrinfo() failed!\n");
      return 1;
   }

   if (!result)
   {
      printf("getaddrinfo() returned invalid result!\n");
      return 1;
   }

   sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
   if (sock == INVALID_SOCKET)
   {
      printf("socket() failed!\n");
      return 1;
   }

   enum
   {
      socket_mode_blocking = 0, socket_mode_non_blocking = 1
   };

   u_long mode = socket_mode_non_blocking;
   if (ioctlsocket(sock, FIONBIO, &mode) != NO_ERROR)
   {
      printf("ioctlsocket() failed!\n");
      return 1;
   }

   sockaddr_in listening_addr;
   listening_addr.sin_family = AF_INET;
   listening_addr.sin_addr.s_addr = htonl(INADDR_ANY);
   listening_addr.sin_port = htons(LOCAL_PORT);

   if (bind(sock, reinterpret_cast<SOCKADDR*>(&listening_addr), sizeof(listening_addr)) < 0)
   {
      printf("bind() failed!\n");
      return 1;
   }

   // setup SCM socket address
   SCM_addr.sin_family = AF_INET;
   SCM_addr.sin_addr.s_addr = inet_addr(SCM_IP);
   SCM_addr.sin_port = htons(SCM_PORT_NUM);


   while (1)
   {
      model_exec();

      if (g_joystick.axes[0].status.un_initialised ||
          g_joystick.axes[1].status.un_initialised)
      {
         perform_joystick_init();
      }

      printf("SCM: (%s)\n", g_SCM_online ? "ONLINE " : "OFFLINE");
      
      if (!g_invalid_response)
      {
         for (int i = 0;
              i < 2;
              ++i)
         {
            printf("Axis %d (%s)  pos: %+06.2f  force: %+06.2f  trim: %+06.2f\n",
                   i + 1,
                   axis_status_to_string(g_joystick.axes[i].status),
                   g_joystick.axes[i].position,
                   g_joystick.axes[i].force,
                   g_joystick.axes[i].current_trim
            );
         }
         printf("Analog 1: %04hd  Analog 2: %05hd\n", g_joystick.x, g_joystick.y);
         printf("Switches: ");
         for (int i = 0;
              i < 16;
              ++i)
         {
            u16 mask = 0x1 << i;
            printf("%c", g_joystick.switches & mask ? '1' : '0');
         }
         puts("\n");

      }

      Sleep(10);

      COORD xy = {};
      SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), xy);
   }

   closesocket(sock);
   WSACleanup();


   printf("Press enter to close ");
   getchar();

   return 0;
}