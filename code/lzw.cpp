#include "main.h"
#include <iostream>
#include <vector>
#include <unordered_map>
#include <stdlib.h>

#include <ap_int.h>
#include <hls_stream.h>

//****************************************************************************************************************
//#define CAPACITY 4096
// try  uncommenting the line above and commenting line 6 to make the hash table smaller
// and see what happens to the number of entries in the assoc mem
// (make sure to also comment line 27 and uncomment line 28)
#define SEED 524057
#define ASSOC_MEM_STORE 256
// #define CLOSEST_PRIME 65497
// #define CLOSEST_PRIME 32749
// #define CLOSEST_PRIME 65497
#define FILE_SIZE 4096

static inline uint32_t murmur_32_scramble(uint32_t k) {
	k *= 0xcc9e2d51;
	k = (k << 15) | (k >> 17);
	k *= 0x1b873593;
	return k;
}

unsigned int my_hash(unsigned long key) {
	uint32_t h = SEED;
	uint32_t k = key;
	h ^= murmur_32_scramble(k);
	h = (h << 13) | (h >> 19);
	h = h * 5 + 0xe6546b64;

	h ^= murmur_32_scramble(k);
	/* Finalize. */
	h ^= h >> 16;
	h *= 0x85ebca6b;
	h ^= h >> 13;
	h *= 0xc2b2ae35;
	h ^= h >> 16;
	//return h & 0xFFFF;
	return h & (CAPACITY - 1);
	// return key % CLOSEST_PRIME;
}

void hash_lookup(unsigned long (*hash_table)[2], unsigned int key, bool *hit,
		unsigned int *result) {
	//std::cout << "hash_lookup():" << std::endl;
	key &= 0xFFFFF; // make sure key is only 20 bits

	unsigned int hash_val = my_hash(key);

	unsigned long lookup = hash_table[hash_val][0];

	// [valid][value][key]
	unsigned int stored_key = lookup & 0xFFFFF;       // stored key is 20 bits
	unsigned int value = (lookup >> 20) & 0xFFF;      // value is 12 bits
	unsigned int valid = (lookup >> (20 + 12)) & 0x1; // valid is 1 bit

	if (valid && (key == stored_key)) {
		*hit = 1;
		*result = value;
		//std::cout << "\thit the hash" << std::endl;
		//std::cout << "\t(k,v,h) = " << key << " " << value << " " << my_hash(key) << std::endl;
	} else {
		lookup = hash_table[hash_val][1];

		// [valid][value][key]
		stored_key = lookup & 0xFFFFF;       // stored key is 20 bits
		value = (lookup >> 20) & 0xFFF;      // value is 12 bits
		valid = (lookup >> (20 + 12)) & 0x1; // valid is 1 bit
		if (valid && (key == stored_key)) {
			*hit = 1;
			*result = value;
		} else {
			*hit = 0;
			*result = 0;
		}
		// std::cout << "\tmissed the hash" << std::endl;
	}
}


void hash_insert(unsigned long (*hash_table)[2], unsigned int key,
		unsigned int value, bool *collision) {
	//std::cout << "hash_insert():" << std::endl;
	key &= 0xFFFFF;   // make sure key is only 20 bits
	value &= 0xFFF;   // value is only 12 bits

	unsigned int hash_val = my_hash(key);

	unsigned long lookup = hash_table[hash_val][0];
	unsigned int valid = (lookup >> (20 + 12)) & 0x1;

	if (valid) {
		lookup = hash_table[hash_val][1];
		valid = (lookup >> (20 + 12)) & 0x1;
		if (valid) {
			*collision = 1;
		} else {
			hash_table[hash_val][1] = (1UL << (20 + 12)) | (value << 20) | key;
			*collision = 0;
		}
		// std::cout << "\tKey is:" << key << std::endl;
		// std::cout << "\tcollision in the hash" << std::endl;
	} else {
		hash_table[hash_val][0] = (1UL << (20 + 12)) | (value << 20) | key;
		*collision = 0;
		//std::cout << "\tinserted into the hash table" << std::endl;
		//std::cout << "\t(k,v,h) = " << key << " " << value << " " << my_hash(key) << std::endl;
	}
}
//****************************************************************************************************************
//typedef struct {
//	// Each key_mem has a 9 bit address (so capacity = 2^9 = 512)
//	// and the key is 20 bits, so we need to use 3 key_mems to cover all the key bits.
//	// The output width of each of these memories is 64 bits, so we can only store 64 key
//	// value pairs in our associative memory map.
//
//	unsigned long upper_key_mem[512]; // the output of these  will be 64 bits wide (size of unsigned long).
//	unsigned long middle_key_mem[512];
//	unsigned long lower_key_mem[512];
//	unsigned int value[ASSOC_MEM_STORE]; // value store is 64 deep, because the lookup mems are 64 bits wide
//	unsigned int fill;       // tells us how many entries we've currently stored
//} assoc_mem;

typedef struct {
	// Each key_mem has a 9 bit address (so capacity = 2^9 = 512)
	// and the key is 20 bits, so we need to use 3 key_mems to cover all the key bits.
	// The output width of each of these memories is 64 bits, so we can only store 64 key
	// value pairs in our associative memory map.

	unsigned long *upper_key_mem; // the output of these  will be 64 bits wide (size of unsigned long).
	unsigned long *middle_key_mem;
	unsigned long *lower_key_mem;
	unsigned int  *value; // value store is 64 deep, because the lookup mems are 64 bits wide
	unsigned int fill;       // tells us how many entries we've currently stored
} assoc_mem;

// cast to struct and use ap types to pull out various feilds.

void assoc_insert(assoc_mem *mem, unsigned int key, unsigned int value,
		bool *collision) {
	//std::cout << "assoc_insert():" << std::endl;
	key &= 0xFFFFF; // make sure key is only 20 bits
	value &= 0xFFF;   // value is only 12 bits

	if (mem->fill < ASSOC_MEM_STORE) {
		mem->upper_key_mem[(key >> 18) % 512] |= (1UL << mem->fill); // set the fill'th bit to 1, while preserving everything else
		mem->middle_key_mem[(key >> 9) % 512] |= (1UL << mem->fill); // set the fill'th bit to 1, while preserving everything else
		mem->lower_key_mem[(key >> 0) % 512] |= (1UL << mem->fill); // set the fill'th bit to 1, while preserving everything else
		mem->value[mem->fill] = value;
		mem->fill++;
		*collision = 0;
		//std::cout << "\tinserted into the assoc mem" << std::endl;
		//std::cout << "\t(k,v) = " << key << " " << value << std::endl;
	} else {
		*collision = 1;
	}
}

//static unsigned int findAddr(uint64_t match) {
//    #pragma HLS INLINE
//    if(match == 0)  return 64; //find address failed
//
//    uint8_t segment[8]; // split 64-bits match into 8 x 8 bits
//    #pragma HLS array_partition variable=segment complete
//    // initialize all the segments in parallel
//    for(int i = 0; i < 8; i++){
//        #pragma HLS UNROLL
//        segment[i] = (match >> (8 * i)) & 0xFF;
//    }
//
//    uint8_t mask[8];
//    #pragma HLS array_partition variable=mask complete
//    for(int j = 0; j < 8; j++){
//        #pragma HLS UNROLL
//        mask[j] = 0x1 << j;
//    }
//
//    // check each segment in parallel
//    for(int i = 0; i < 8; i++){
//        #pragma HLS UNROLL
//        if(segment[i] != 0){
//            for(int j = 0; j < 8; j++){
//                #pragma HLS UNROLL
//                if(segment[i] & mask[j]){
//                    return 8 * i + j;
//                }
//            }
//        }
//    }
//
//    return 64;
//}

void assoc_lookup(assoc_mem *mem, unsigned int key, bool *hit,
		unsigned int *result) {
	key &= 0xFFFFF; // make sure key is only 20 bits

//	unsigned int match_high = mem->upper_key_mem[(key >> 18) % 512];
//	unsigned int match_middle = mem->middle_key_mem[(key >> 9) % 512];
//	unsigned int match_low = mem->lower_key_mem[(key >> 0) % 512];
//
//	unsigned int match = match_high & match_middle & match_low;
//
    unsigned address = 256;
//	unsigned caught_add[64] = {0};
//	int i = 0;
//	for (; address < ASSOC_MEM_STORE; address++) {
//#pragma HLS unroll
//		if ((match >> address) & 0x1) {
//			caught_add[i++] = address;
//			//caught_add = address;
//		}
//	}  // might not work

    //address = log2(match & -match) + 1;
	if (address != ASSOC_MEM_STORE) {
		*result = mem->value[address];
		*hit = 1;
	} else {
		*hit = 0;
	}
}

//****************************************************************************************************************
void insert(unsigned long hash_table[][2], assoc_mem *mem, unsigned int key,
		unsigned int value, bool *collision) {
	hash_insert(hash_table, key, value, collision);
	if (*collision) {
		assoc_insert(mem, key, value, collision);
	}
}

void lookup(unsigned long hash_table[][2], assoc_mem *mem, unsigned int key,
		bool *hit, unsigned int *result) {
	hash_lookup(hash_table, key, hit, result);
	if (!*hit) {
		assoc_lookup(mem, key, hit, result);

	}
}


void inputdatamover(char *in_data, char *chunk_arr, unsigned int s1_len) {
In_Data:
    for (int i = 0; i < (s1_len*4096)/4096; i++) {
#pragma HLS LOOP_TRIPCOUNT min = 4096 max = 4096
        in_data[i] = chunk_arr[i];
    }
}

void outputdatamover(unsigned char *out_data, unsigned char *out_hw, int out_hw_size) {
Out_Data:
	for (int i = 0; i < (out_hw_size*4096)/4096; i++) {
#pragma HLS LOOP_TRIPCOUNT min = 4096 max = 4096
		out_hw[i] = out_data[i];
	}
}



void hardware_encoding_stream(hls::stream<ap_uint<32>>& out_hw_size_stream, hls::stream<ap_uint<8>>& output_hw_stream,
						hls::stream<ap_uint<8>>& chunk_arr_stream, int s1_len)
//void hardware_encoding(int *out_hw_size, unsigned char *output_hw, char *chunk_arr, unsigned int s1_len)
{
//#pragma HLS INTERFACE m_axi port=out_hw_size depth=1 bundle=axismm1
//#pragma HLS INTERFACE m_axi port=output_hw depth=8192 bundle=axismm2
//#pragma HLS INTERFACE m_axi port=chunk_arr depth=8192 bundle=axismm1

//#pragma HLS INTERFACE mode = m_axi bundle = aximm2  port = in
//#pragma HLS INTERFACE m_axi port = out bundle = aximm depth = 1024

//#pragma HLS interface ap_hs port = s1_len
//#pragma HLS interface ap_fifo depth = 1 port = out_hw_size
//#pragma HLS interface ap_fifo depth = 8192 port = chunk_arr, output_hw
	int tmp_out_size = 0;

	unsigned long upper_mem[512];
	unsigned long middle_mem[512];
	unsigned long lower_mem[512];
	unsigned int val[ASSOC_MEM_STORE];
	static unsigned long hash_table[CAPACITY][2];
//

#pragma HLS array_partition variable=upper_mem type=complete
#pragma HLS array_partition variable=middle_mem type=complete
#pragma HLS array_partition variable=lower_mem type=complete
#pragma HLS BIND_STORAGE variable = hash_table type = RAM_2P impl = BRAM
//#pragma HLS array_partition variable=hash_table type=block dim=2 factor=16

//#pragma HLS reset variable=hash_table on

	//inputdatamover(indata, chunk_arr, s1_len);

	int next_code = 256;
	//unsigned int prefix_code = indata[0];
	//unsigned int prefix_code = chunk_arr[0];
	unsigned int prefix_code = chunk_arr_stream.read();
	unsigned int code = 0;
	unsigned char next_char = 0;
	unsigned char output_byte = 0;
	uint32_t bp_push_index = 0;    

	// create hash table and assoc mem

	//ap_int<64> hash_table[CAPACITY][2];
	assoc_mem my_assoc_mem =
	{
		.upper_key_mem = upper_mem,
		.middle_key_mem = middle_mem,
		.lower_key_mem = lower_mem,
		.value = val,
		.fill = 0
	};

//#pragma HLS array_partition variable=hash_table type=complete dim=2
//#pragma HLS array_partition variable=hash_table ty

	// make sure the memories are clear
	for (int i = 0; i < CAPACITY; i++) {
			hash_table[i][0] = 0;
			hash_table[i][1] = 0;
	}

	my_assoc_mem.fill = 0;
	for (int i = 0; i < 512; i++) {
#pragma HLS UNROLL
		my_assoc_mem.upper_key_mem[i] = 0;
		my_assoc_mem.middle_key_mem[i] = 0;
		my_assoc_mem.lower_key_mem[i] = 0;
	}

    // main loop to iterate through the chunk
	unsigned int i = 0;
	//next_char = indata[i + 1];
	while (i < s1_len) {
        // Complete bitpacking in the last iteration 
		if (i+1 == s1_len) {
			if(bp_push_index % 3 == 0)
			{
				output_byte |= (prefix_code & 0xFF0) >> 4;
				//output_hw[bp_push_index] = output_byte;
				//outdata[bp_push_index] = output_byte;
				output_hw_stream.write(output_byte);
				bp_push_index++;
				output_byte = 0;
				output_byte |= (prefix_code & 0x00F) << 4;
				output_byte &= ~(0xF);
				//output_hw[bp_push_index] = output_byte;
				//outdata[bp_push_index] = output_byte;
				output_hw_stream.write(output_byte);
				bp_push_index++;
			}
			else
			{
				output_byte |= (prefix_code & 0xF00) >> 8;
				//output_hw[bp_push_index] = output_byte;
				//outdata[bp_push_index] = output_byte;
				output_hw_stream.write(output_byte);
				bp_push_index++;
				output_byte = 0;
				output_byte |= prefix_code & 0x0FF;
				//output_hw[bp_push_index] = output_byte;
				//outdata[bp_push_index] = output_byte;
				output_hw_stream.write(output_byte);
				bp_push_index++;
			}

			out_hw_size_stream.write(bp_push_index);
			break;
			//*out_hw_size = bp_push_index;
			//tmp_out_size = bp_push_index;
		}

		next_char = chunk_arr_stream.read();
		bool hit = 0;
		lookup(hash_table, &my_assoc_mem, (prefix_code << 8) + next_char, &hit, &code);

		if (!hit) 
        {
            // insert prefix code and next character into hash table (assoc mem if collision)
			bool collision = 0;
			insert(hash_table, &my_assoc_mem, (prefix_code << 8) + next_char, next_code, &collision);
//			if (collision)
//            {
//				std::cout << "ERROR: FAILED TO INSERT! NO MORE ROOM IN ASSOC MEM!" << std::endl;
//				break;
//			}

			next_code += 1;

            // Perform bitpacking
            if(bp_push_index % 3 == 0)
            {
                output_byte |= (prefix_code & 0xFF0) >> 4;
                //output_hw[bp_push_index] = output_byte;
                //outdata[bp_push_index] = output_byte;
                output_hw_stream.write(output_byte);
                bp_push_index++;
                output_byte = 0;
                output_byte |= (prefix_code & 0x00F) << 4;
                output_byte &= ~(0xF);
            }
            else
            {
                output_byte |= (prefix_code & 0xF00) >> 8;
                //output_hw[bp_push_index] = output_byte;
                output_hw_stream.write(output_byte);
                //outdata[bp_push_index] = output_byte;
                bp_push_index++;
                output_byte = 0;
                output_byte |= prefix_code & 0x0FF;
                //output_hw[bp_push_index] = output_byte;
                //outdata[bp_push_index] = output_byte;
                output_hw_stream.write(output_byte);
                bp_push_index++;
                output_byte = 0;
            }

			prefix_code = next_char;
		} else {
            // Take value from hash table if found there
			prefix_code = code;
		}
		i++;
	}

	//*out_hw_size = tmp_out_size;
	//outputdatamover(outdata, output_hw, tmp_out_size);
}

void load_input(unsigned char* in, hls::stream<ap_uint<8>>& inStream, int s1_len)
{
    for (int i = 0; i < s1_len; i++) {
#pragma HLS LOOP_TRIPCOUNT min = 4096 max = 4096
        inStream.write(in[i]);
    }
}

void store_result(int* out, unsigned char *output_hw, hls::stream<ap_uint<8>>& out_stream,
		hls::stream<ap_uint<32>>& out_size_stream)
{
	int temp_out_hw_size = out_size_stream.read();
    *out = temp_out_hw_size;

    int i = 0;
    for (; i < temp_out_hw_size; i++) {
#pragma HLS LOOP_TRIPCOUNT min = 4096 max = 4096
    	uint32_t temp = out_stream.read();
        output_hw[i] = temp;
    }
}

void hardware_encoding(int *out_hw_size, unsigned char *output_hw, unsigned char *chunk_arr, unsigned int s1_len)
{
#pragma HLS INTERFACE m_axi port=out_hw_size depth=1 bundle=axismm1
#pragma HLS INTERFACE m_axi port=output_hw depth=8192 bundle=axismm2
#pragma HLS INTERFACE m_axi port=chunk_arr depth=8192 bundle=axismm1

#pragma HLS DATAFLOW
    static hls::stream<ap_uint<32>> out_hw_size_stream("out_hw_size_stream");
    static hls::stream<ap_uint<8>> chunk_arr_stream("chunk_arr_stream");
    static hls::stream<ap_uint<8>> output_hw_stream("output_hw_stream");

#pragma HLS STREAM variable = out_hw_size_stream depth = 8192
#pragma HLS STREAM variable = chunk_arr_stream depth = 8192
#pragma HLS STREAM variable = output_hw_stream depth = 8192

    // dataflow pragma instruct compiler to run following three APIs in parallel
    load_input(chunk_arr, chunk_arr_stream, s1_len);
    hardware_encoding_stream(out_hw_size_stream, output_hw_stream, chunk_arr_stream, s1_len);
    store_result(out_hw_size, output_hw, output_hw_stream, out_hw_size_stream);
}
