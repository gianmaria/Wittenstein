#include <stdio.h>
#include <string.h>

#include <iostream>
#include <string>

using std::string;
using std::cout;
using std::endl;

#include "tinythread.h"

#include "Network.h"
#include "wittenstein_protocol.h"

#define SCM_IP "127.0.0.1"
#define SCM_PORT 3001

#define LOCAL_PORT 6969

#if 0

void define_master_curve(UDPSocket& socket)
{
   vector<float> values;
   values.push_back(-2.50f);
   values.push_back(-1.17f);
   values.push_back(+12.89f);
   values.push_back(+7.992f);
   values.push_back(+123.45f);

   auto query = generateDTM(WittProtocol::define_master_curve,
                            0, 2, 0, values, 2);

   socket.SendTo(SCM_IP, SCM_PORT, (const char*)query.data(), query.size());

   int stop = 0;
}

#endif // 0


struct StickInfo
{
   float position;
   float force;
   AxisStatus status;
};

static StickInfo g_sticks[2];

#define DEBUG_PRINT 0



void init_system(UDPSocket& socket)
{
   auto msg = generateDTM(WittProtocol::start_traversal, 0, 2);
   socket.SendTo(SCM_IP, SCM_PORT, (const char*)msg.data(), msg.size());
}

void go_active(UDPSocket& socket)
{
   auto msg = generateDTM(WittProtocol::go_active, 0, 2);
   socket.SendTo(SCM_IP, SCM_PORT, (const char*)msg.data(), msg.size());
}

void create_async_link(UDPSocket& socket)
{
   auto msg = generateDTM(WittProtocol::create_asynchronous_link, 0, 1);
   socket.SendTo(SCM_IP, SCM_PORT, (const char*)msg.data(), msg.size());
}

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

void request_axes_status(UDPSocket& socket)
{
   auto msg = generateQuery(WittProtocol::status, 0, 2);
   socket.SendTo(SCM_IP, SCM_PORT, (const char*)msg.data(), msg.size());
}

void request_axes_position(UDPSocket& socket)
{
   auto msg = generateQuery(WittProtocol::axis_position, 0, 2);
   socket.SendTo(SCM_IP, SCM_PORT, (const char*)msg.data(), msg.size());
}

void request_axes_force(UDPSocket& socket)
{
   auto msg = generateQuery(WittProtocol::input_force, 0, 2);
   socket.SendTo(SCM_IP, SCM_PORT, (const char*)msg.data(), msg.size());
}


void decode_can_message(const DTMResponse& resp)
{
   u16 val0 = (u16)resp.data_values[0];
   byte val1 = (byte)resp.data_values[1];
   byte val2 = (byte)resp.data_values[2];

   unsigned short buttons = 0;
   buttons = val1 << 8 | val2;

   byte val3 = (byte)resp.data_values[3];
   byte val4 = (byte)resp.data_values[4];
   byte val5 = (byte)resp.data_values[5];

   u16 y = val3 << 2 | ((val5 & 0x0c) >> 2);
   u16 x = val4 << 2 | ((val5 & 0x03) >> 0);



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

      g_sticks[i].status = *axis_status;

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
      g_sticks[i].position = resp.data_values[i];
   }
}

void decode_axes_force(const DTMResponse& resp)
{
   for (int i = 0;
        i < resp.num_of_axes;
        ++i)
   {
      g_sticks[i].force = resp.data_values[i];
   }
}


static bool g_done = false;
static bool g_thread_died = false;

void listen(void *data)
{
   try
   {
      UDPSocket& socket = *(UDPSocket*)data;

      while (!g_done)
      {
         char buffer[512] = {};
         int byte_recv = socket.RecvFrom(buffer, 512);

         DTMResponse resp = decodeResponse(buffer);

         if (!resp.valid)
            continue;

         switch (resp.data_code)
         {
            case WittProtocol::can_io_message:
            {
               decode_can_message(resp);
            } break;

            case WittProtocol::status:
            {
               decode_axes_status(resp);
            } break;

            case WittProtocol::axis_position:
            {
               decode_axes_position(resp);
            } break;

            case WittProtocol::input_force:
            {
               decode_axes_force(resp);
            } break;


            default:
            {
               printf("Response:\n");

               for (int i = 0;
                    i < byte_recv;
                    ++i)
               {
                  if (i != 0 && i % 16 == 0)
                  {
                     printf("\n");
                  }
                  printf("%02hx ", (byte)buffer[i]);
               }
               printf("\n");

               printf("Response (ASCII): ");

               for (int i = 0;
                    i < byte_recv;
                    ++i)
               {
                  char c = buffer[i];
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

   } catch (std::system_error& e)
   {
      printf("**** Thread died: %s (%d) ***\n", e.what(), e.code().value());
      g_thread_died = true;
   }
}

int main()
{
   setbuf(stdout, NULL);

   try
   {
      UDPSocket socket;
      socket.Bind(LOCAL_PORT);

      tthread::thread thread(listen, (void*)&socket);

      request_axes_status(socket);
      Sleep(200);

      if (!g_sticks[0].status.active &&
          !g_sticks[1].status.active)
      {
         printf("Initializing...\n");
         init_system(socket);

         while (!g_sticks[0].status.passive &&
                !g_sticks[1].status.passive)
         {
            request_axes_status(socket);
            Sleep(1000);
         }

         printf("Going active...\n");
         go_active(socket);

         while (!g_sticks[0].status.active &&
                !g_sticks[1].status.active)
         {
            request_axes_status(socket);
            Sleep(500);
         }

         printf("Creating CAN link...\n");
         create_async_link(socket);
         Sleep(2000);
      }
      
      printf("Opeartive!\n");

      while (!g_done && !g_thread_died)
      {
         request_axes_status(socket);
         Sleep(50);

         request_axes_position(socket);
         Sleep(50);

         request_axes_force(socket);
         Sleep(50);

         for (int i = 0;
              i < 2;
              ++i)
         {
            printf("Axis %d - status: %s pos: %+06.2f force: %+06.2f\n",
                   i+1,
                   axis_status_to_string(g_sticks[i].status),
                   g_sticks[i].position,
                   g_sticks[i].force);
         }
         puts("");

         Sleep(1000);

#if 0
         string input;
         cout << "> ";
         std::getline(std::cin, input);

         int command = 0;
         try
         {
            command = std::stoi(input);
         } catch (...)
         {
         }

         switch (command)
         {
            case WittProtocol::axis_position:
            {
               auto msg = generateQuery(WittProtocol::axis_position, 0, 2);
               socket.SendTo(SCM_IP, SCM_PORT, (const char*)msg.data(), msg.size());
            } break;

            default:
            cout << "Unknown command: '" << input << "'" << endl;

         }

         Sleep(50);
#endif // 0

      }


   } 
   catch (std::system_error& e)
   {
      printf("**** Exception in main: %s (%d) ***\n", e.what(), e.code().value());
   }

   printf("Press enter to close ");
   getchar();

   return 0;
}