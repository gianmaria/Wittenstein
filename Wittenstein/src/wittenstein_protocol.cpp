#include <stdio.h>
#include <string.h>

#include "wittenstein_protocol.h"


static enum
{
   STX_VAL = 0x2,
   EOT_VAL = 0x4,
   ENQ_VAL = 0x5
};


static unsigned short generateCRC(const char* cBuf, int iSize)
{
   unsigned short usCRC, usLSB, i, j;
   char* c;
   
   /* Generate CRC from data in cBuf. */
   usCRC = 0xffff;

   /* For each byte in the buffer. */
   for (i = 0; i < iSize; i++)
   {
      usCRC ^= *(cBuf + i);

      /* For each bit in the byte. */
      for (j = 0; j < 8; j++)
      {
         usLSB = (usCRC & 0x0001);
         usCRC >>= 1;

         if (usLSB)
         {
            usCRC ^= 0xa001;
         }
      }
   }
   /* Often code for proessing ASCII protocol includes C string handling functions.
      These may cause errors to occur if the checksum bytes contain a zero (C string terminator).
      If either byte is zero, set the byte to 'x'
   */

   c = (char*)&usCRC;

   if (c[0] == 0x00)
   {
      c[0] = 'x';
   }
   if (c[1] == 0x00)
   {
      c[1] = 'x';
   }

   return usCRC;
}

// return the ASCII representation of a hexadecimal number.
static char int_to_ascii_hex(int input)
{
   char res = 0;

   if (input >= 0x0 &&
       input <= 0x9)
   {
      res = '0' + input;
   }
   else if (input >= 0xA &&
            input <= 0xF)
   {
      res = 'a' + input - 0xA;
   }
   
   return res;
}

static int ascii_hex_to_int(char c)
{
   int res = 0;

   if (c >= '0' &&
       c <= '9')
   {
      res = c - '0';
   }
   else if (c >= 'a' &&
            c <= 'f')
   {
      res = 0xA + c - 'a';
   }

   return res;
}

static unsigned short swap_byte_u16(unsigned short input)
{
   unsigned short res = 0;

   byte* res_p = (byte*)&res;
   byte* input_p = (byte*)&input;

   res_p[0] = input_p[1];
   res_p[1] = input_p[0];

   return res;
}

static bool is_valid_response(const char* response, int size)
{
   if (!response)
      return false;

   unsigned short response_crc = swap_byte_u16(*(unsigned short*)(response + size - 2));

   unsigned short expected_crc = generateCRC(response, size - 2);

   bool equal = (response_crc == expected_crc);

   return equal;
}

static float data_value_to_float(WittProtocol::DataValue data_value)
{
   float res = 0.0f;

   res = std::stof(string(data_value.data, WittProtocol::DATA_VALUE_SIZE));

   return res;
}

vector<char> WittProtocol::generateQuery(WittProtocol::MSGType msg_type,
                       int start_axis,
                       int number_of_axis,
                       int node,
                       int tag)
{
   vector<char> query_msg;

   byte data_code = (byte)msg_type;
   
   query_msg.push_back(STX_VAL);
   query_msg.push_back(int_to_ascii_hex(node));
   query_msg.push_back(int_to_ascii_hex((data_code & 0xF0) >> 4));
   query_msg.push_back(int_to_ascii_hex(data_code & 0x0F));
   query_msg.push_back(int_to_ascii_hex(tag));
   query_msg.push_back(int_to_ascii_hex(start_axis));
   query_msg.push_back(int_to_ascii_hex(number_of_axis));
   query_msg.push_back(ENQ_VAL);

   unsigned short crc = generateCRC(query_msg.data(), query_msg.size());

   query_msg.push_back((byte)((crc & 0xFF00) >> 8));
   query_msg.push_back((byte)((crc & 0x00FF)));

   return query_msg;
}



vector<WittProtocol::DTMResponse> WittProtocol::decodeResponse(const vector<char>& response_)
{
   vector<WittProtocol::DTMResponse> responses;

   const char* response = response_.data();
   const char* end_response = response + response_.size();

   while (response != end_response)
   {
      WittProtocol::DTMResponse resp = {};

      WittProtocol::WittHeader* witt_header = (WittProtocol::WittHeader*)response;

      try
      {
         resp.data_code = (WittProtocol::MSGType)std::stoi(string(witt_header->data_code, 2), 0, 16);
      } catch (...)
      {
      }

      resp.node = ascii_hex_to_int(witt_header->node);
      resp.tag = ascii_hex_to_int(witt_header->tag);
      resp.start_axis = ascii_hex_to_int(witt_header->start_axis);
      resp.num_of_axes = ascii_hex_to_int(witt_header->num_of_axes);

      int resp_size = WittProtocol::WITT_HEADER_SIZE + (resp.num_of_axes * WittProtocol::DATA_VALUE_SIZE) + 1 + 2; // 1: EOT, 2: CRC

      if (!is_valid_response(response, resp_size))
      {
         responses.push_back(resp);
         continue;
      }

      resp.valid = true;

      response += WittProtocol::WITT_HEADER_SIZE;

      while (*response != EOT_VAL)
      {
         WittProtocol::DataValue data_value = {};

         for (int i = 0;
              i < WittProtocol::DATA_VALUE_SIZE;
              ++i)
         {
            char c = (char)*response;
            data_value.data[i] = c;
            ++response;
         }

         float data = data_value_to_float(data_value);
         resp.data_values.push_back(data);
      }

      ++response; // skip EOT
      response += 2; // skip CRC

      responses.push_back(resp);
   }

   return responses;
}


vector<char> WittProtocol::generateDTM(WittProtocol::MSGType msg_type,
                     int start_axis,
                     int number_of_axis,
                     int node,
                     const vector<float>& values,
                     int tag)
{
   vector<char> dtm_query;

   byte data_code = (byte)msg_type;

   dtm_query.push_back(STX_VAL);
   dtm_query.push_back(int_to_ascii_hex(node));
   dtm_query.push_back(int_to_ascii_hex((data_code & 0xF0) >> 4));
   dtm_query.push_back(int_to_ascii_hex(data_code & 0x0F));
   dtm_query.push_back(int_to_ascii_hex(tag));
   dtm_query.push_back(int_to_ascii_hex(start_axis));
   dtm_query.push_back(int_to_ascii_hex(number_of_axis));

   // data values here
   for (int i = 0;
        i < values.size();
        ++i)
   {
      char buffer[DATA_VALUE_SIZE] = {};
      _snprintf(buffer, DATA_VALUE_SIZE, "%+01.04f", values[i]);

      for (int j = 0;
           j < DATA_VALUE_SIZE;
           ++j)
      {
         dtm_query.push_back((byte)buffer[j]);
      }
   }

   dtm_query.push_back(EOT_VAL);

   unsigned short crc = generateCRC(dtm_query.data(), dtm_query.size());

   dtm_query.push_back((byte)((crc & 0xFF00) >> 8));
   dtm_query.push_back((byte)((crc & 0x00FF)));

   return dtm_query;
}

