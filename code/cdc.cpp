#include "main.h"
#include <math.h>
#include <climits>

static const uint32_t pow_replace[17] = {3, 9, 27, 81, 243, 729, 2187, 6561, 19683, 59049, 177147, 531441, 1594323, 4782969, 14348907, 43046721, 129140163};
static uint32_t chunk_ctr=0;
static unsigned char compressed_data[COMPRESSED_DATA_SIZE];
static unsigned int data_fill = 0;
extern unsigned long total_lzw_len;

stopwatch lzw_hw_timer;
stopwatch sha_timer;
stopwatch dedup_timer;

uint64_t hash_func(unsigned char* input, unsigned int pos)
{
    // Initial hash function computation
    uint64_t hash = 0;
    for (int i = 0; i < WIN_SIZE; i++)
        hash += input[pos + WIN_SIZE - 1 - i] * pow_replace[i];
    return hash;
}

void cdc(unsigned char* buff, unsigned int buff_size, std::vector<hashtable_t> &sha_table, FILE *encode_fp, volatile int done, cl::Buffer chunk_arr_cl ,cl::Buffer output_hw_cl ,cl::Buffer out_hw_size_cl, unsigned char *output_hw, int *output_hw_size, unsigned char *chunk_arr, cl::CommandQueue q, cl::Kernel hw_encoding)
{
    unsigned char sha_output[SHA256_BLOCK_SIZE];    // Array to store SHA256 digest for each chunk.
    
    bool firstfound=1;                              // Flag to indicate the first chunk 
    bool seen;                                      // Flag to indicate if a chunk is already in sha_table

    unsigned int counter = 0;                       // Keep track of lower and upper bound for chunk size
    unsigned int chunk_size=0;                      // Size of current chunk
    unsigned int i;                                 // Iterator variable for characters in packet
    unsigned int j=0;                               // Keep track of chunk indices
    uint32_t header;                                // Header to be written into file for each chunk

    SHA256_CTX ctx;                                 // Context variable for SHA
    uint64_t hash = hash_func(buff, WIN_SIZE);      // Rolling hash value for CDC compare match

    std::vector<unsigned int> chunk_indices;        // store the chunk indices, this is cleared for every new packet

    std::vector<cl::Event> write_events;
    std::vector<cl::Event> execute_events;
    std::vector<cl::Event> read_events;
    cl::Event write, execute, read;

    for (i = WIN_SIZE; i < buff_size-WIN_SIZE; i++)
    {
        counter++;
        memset(sha_output,0,SHA256_BLOCK_SIZE);
        
        // Condition to make a chunk
        if ((hash % MODULUS == TARGET || counter == 4096) && counter > 100) {
            // Checking for first chunk
            if(firstfound == 1)
            {
                chunk_size = i+1;
                sha_timer.start();
                sha256_hash(&ctx, &buff[0], sha_output, 1, chunk_size);
                sha_timer.stop();
            }
            else
            {
                chunk_size = i - chunk_indices.back();
                sha_timer.start();
                sha256_hash(&ctx, &buff[chunk_indices.back()+1], sha_output, 1, chunk_size);
                sha_timer.stop();
            }

            dedup_timer.start();
            // Sending sha_table for deduplication
            seen = dedup(sha_table, chunk_ctr,sha_output);
            dedup_timer.stop();

            //Increment chunk_ctr
            chunk_ctr++;

            // Push the latest index back into the vector
            chunk_indices.push_back(i);

            if(seen)
            {
                // Found duplicate chunk
				header = (sha_table.back().id);
				header = header << 1;
				header |= (1<<0);
                fwrite(&header, sizeof(uint32_t), 1, encode_fp);
                //store_compressed_data(header, output_hw, 4, encode_fp, done, seen);
            }
            else
            {
                // New chunk
                memset(chunk_arr, 0, 8192);
                if(firstfound==1){
                    memcpy(chunk_arr, &buff[0], chunk_size);
                    firstfound = 0;
                }
                else{
                    memcpy(chunk_arr, &buff[chunk_indices[j-1]+1], chunk_size);
                }

                lzw_hw_timer.start();
 		hw_encoding.setArg(3, chunk_size);         

 		q.enqueueMigrateMemObjects({chunk_arr_cl}, 0 /* 0 means from host*/, NULL, &read);     
                read_events.push_back(read);
				
 		q.enqueueTask(hw_encoding, &read_events, &execute);
 		execute_events.push_back(execute);
				
 		q.enqueueMigrateMemObjects({out_hw_size_cl, output_hw_cl}, CL_MIGRATE_MEM_OBJECT_HOST, &execute_events, &write);
                write_events.push_back(write);

        clWaitForEvents(1, (cl_event*)&write);
                //q.finish();
        lzw_hw_timer.stop();

        total_lzw_len += chunk_size;

		//Write to file
		header = *output_hw_size;
		header = header << 1;
		header &= ~(1<<0);
                fwrite(&header, sizeof(uint32_t), 1, encode_fp);
                fwrite(output_hw, sizeof(unsigned char), *output_hw_size, encode_fp);
                //store_compressed_data(header, output_hw, *output_hw_size, encode_fp, done, seen);
            }
            //Increment counter to keep track of indices
            j++;

            // Reset counter
            counter = 0;
        }
        // Compute new hash
        hash = (hash * PRIME) - (buff[i] * pow_replace[16]) + (buff[i + WIN_SIZE] * PRIME);
    }

    //Hard chunking at the end of file
    chunk_size = buff_size - 1 - chunk_indices.back();

    sha_timer.start();
    sha256_hash(&ctx, &buff[chunk_indices.back()+1], sha_output, 1, chunk_size);
    sha_timer.stop();

    chunk_indices.push_back(buff_size-1);

    dedup_timer.start();
    seen = dedup(sha_table, chunk_ctr,sha_output);
    dedup_timer.stop();

    chunk_ctr++;

    if(seen)
    {
        header = (sha_table.back().id);
        header = header << 1;
        header |= (1<<0);
        fwrite(&header, sizeof(uint32_t), 1, encode_fp);
        //store_compressed_data(header, output_hw, 4, encode_fp, done, seen);
        if(done)
        {
            fwrite(compressed_data, sizeof(unsigned char), data_fill, encode_fp);
        }
    }
    else
    {
        memset(chunk_arr, 0, 8192);
        if(firstfound==1)
        {
            chunk_size = chunk_indices[0]+1;
            memcpy(chunk_arr, &buff[0], chunk_size);
            firstfound = 0;
        }
        else
        {
            chunk_size = chunk_indices[j] - chunk_indices[j-1];
            memcpy(chunk_arr, &buff[chunk_indices[j-1]+1], chunk_size);
        }

        hw_encoding.setArg(3, chunk_size);         

        lzw_hw_timer.start();
        q.enqueueMigrateMemObjects({chunk_arr_cl}, 0 /* 0 means from host*/, NULL, &read);     
        read_events.push_back(read);

        q.enqueueTask(hw_encoding, &read_events, &execute);
        execute_events.push_back(execute);

        q.enqueueMigrateMemObjects({out_hw_size_cl, output_hw_cl}, CL_MIGRATE_MEM_OBJECT_HOST, &execute_events, &write);
        write_events.push_back(write);

        clWaitForEvents(1, (cl_event*)&write);
        //q.finish();
        lzw_hw_timer.stop();
                                                    
        total_lzw_len += chunk_size;
        // LZW ON FPGA ENDS 
        header = *output_hw_size;
        header = header << 1;
        header &= ~(1<<0);
        fwrite(&header, sizeof(uint32_t), 1, encode_fp);
        fwrite(output_hw, sizeof(unsigned char), *output_hw_size, encode_fp);
//        store_compressed_data(header, output_hw, *output_hw_size, encode_fp, done, seen);
        if(done)
        {
            fwrite(compressed_data, sizeof(unsigned char), data_fill, encode_fp);
        }
    }

    // Clear chunk indices and chunk size for the next packet
    chunk_indices.clear();
    chunk_size = 0;
}


