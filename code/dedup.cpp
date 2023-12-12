#include "main.h"

bool dedup(std::vector<hashtable_t> &hashTable, unsigned long chunk_ctr, unsigned char *sha_output) {

    hashtable_t temp;
    temp.id = chunk_ctr;
    memcpy(temp.hashval, sha_output, SHA256_BLOCK_SIZE);
    temp.seen = 0;

    for(int i=0; i<hashTable.size(); i++) {
        if(memcmp(&hashTable[i].hashval, sha_output, SHA256_BLOCK_SIZE) == 0) {
            temp.id = hashTable[i].id;
            temp.seen = 1;
            hashTable.push_back(temp);
            return 1;
        }
    }

    hashTable.push_back(temp);
    return 0;
}
