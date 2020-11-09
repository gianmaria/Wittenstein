#pragma once

#include <vector>
#include <string>

using std::string;
using std::vector;

typedef unsigned char byte;
typedef unsigned char u8;
typedef unsigned short u16;


namespace WittProtocol
{

enum { start_axis = 0, all_axes = 2 };

enum MSGType
{
   invalid = 0,

   go_active = 1,
   start_traversal = 2,
   define_master_curve = 3,
   trim_command = 6,
   q_feel_value = 7,
   go_passive = 12,
   static_friction = 21,
   dynamic_friction = 22,
   number_of_axes = 24,
   input_force = 25,
   axis_position = 26,
   status = 51,
   can_io_message = 52,
   create_asynchronous_link = 53,
   actual_trim_position = 92,
   motor_error_status = 177
};

#pragma pack(push, 1)

union AxisStatus
{
   u8 raw;
   struct
   {
      unsigned int un_initialised : 1;
      unsigned int initialising : 1;
      unsigned int passive : 1;
      unsigned int active : 1;
      unsigned int motor_not_responding : 1;
      unsigned int not_used : 1;
      unsigned int terminated_due_to_an_unrecoverable_error : 1;
   };
};

struct WittHeader
{
   byte stx;
   char node;
   char data_code[2];
   char tag;
   char start_axis;
   char num_of_axes;
};

enum
{
   DATA_VALUE_SIZE = 7,
   WITT_HEADER_SIZE = sizeof(WittHeader)
};


struct DataValue
{
   char data[DATA_VALUE_SIZE];
};

#pragma pack(pop)


struct DTMResponse
{
   bool valid;

   WittProtocol::MSGType data_code;
   int node;
   int tag;
   int start_axis;
   int num_of_axes;
   vector<float> data_values;
};



vector<char> generateQuery(WittProtocol::MSGType msg_type,
                           int start_axis,
                           int number_of_axis,
                           int node = 0,
                           int tag = 0);

vector<DTMResponse> decodeResponse(const vector<char>& response);

vector<char> generateDTM(WittProtocol::MSGType msg_type,
                         int start_axis = 0,
                         int number_of_axis = 0,
                         int node = 0,
                         const vector<float>& values = vector<float>(),
                         int tag = 0);

}
