#include "cachelab.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include <assert.h>
#include <bits/getopt_core.h>

// #define DEBUG 1
#define MAX_ARRAY_NAME 200

typedef struct {
    int valid;
    int tag;
    int block_byte;
    int time_stamp; // LRU Stamp
} cache_line, *E_cache, **S_cache;

S_cache _simulation_cache;

char tracefile_path[MAX_ARRAY_NAME];
int S, E, b;
int h, v, s;
int hit_count     = 0;
int miss_count    = 0;
int replace_count = 0;

void PrintHelpInfo() {
    puts("Usage: ./csim-ref [-hv] -s <num> -E <num> -b <num> -t <file>");
    puts("Options:");
    puts("  -h         Print this help message.");
    puts("  -v         Optional verbose flag.");
    puts("  -s <num>   Number of set index bits.");
    puts("  -E <num>   Number of lines per set.");
    puts("  -b <num>   Number of block offset bits.");
    puts("  -t <file>  Trace file.");
    puts("Examples:");
    puts("  linux>  ./csim-ref -s 4 -E 1 -b 4 -t traces/yi.trace");
    puts("  linux>  ./csim-ref -v -s 8 -E 2 -b 4 -t traces/yi.trace");
}

void InitCache() {
    _simulation_cache = (S_cache)malloc(sizeof(E_cache) * S);
    for(int i = 0; i < S; i++) {
        _simulation_cache[i] = (E_cache)malloc(sizeof(cache_line) * E);
        for(int j = 0; j < E; j++) {
            _simulation_cache[i][j].valid = 0;
            _simulation_cache[i][j].tag = -1;
            _simulation_cache[i][j].block_byte = -1;
            _simulation_cache[i][j].time_stamp = -1;
        }
    }
}

void FreeCache() {
    for(int i = 0; i < S; i++) {
        free(_simulation_cache[i]);
    }
    free(_simulation_cache);
}

void UpdateTimeStamp() {
    for(int i = 0; i < S; i++) {
        for(int j = 0; j < E; j++) {
            if(_simulation_cache[i][j].valid == 1) {
                _simulation_cache[i][j].time_stamp++;
            }
        }
    }
}

void Update(const unsigned int address) {
    // Address: t bits | s bits | b bits
    //          Tag      SetIndex BlockOffset

    // tag_offset = t bits
    //              Tag
    unsigned int tag_offset = address >> (s + b);

    // set_index = s bits => (t bits | s bits) & (000...0011...111) => s bits
    //                                            t bits  s bits
    unsigned int set_index = (address >> b) & ((-1U) >> (64 - s));

    // hit cache
    for(int i = 0; i < E; i++) {
        if(_simulation_cache[set_index][i].valid == 1 && _simulation_cache[set_index][i].tag == tag_offset) {
            hit_count++;
            _simulation_cache[set_index][i].time_stamp = 0;
            return ;
        }
    }

    // miss cache
    for(int i = 0; i < E; i++) {
        if(_simulation_cache[set_index][i].valid == 0) {
            miss_count++;
            _simulation_cache[set_index][i].valid = 1;
            _simulation_cache[set_index][i].tag = tag_offset;
            _simulation_cache[set_index][i].time_stamp = 0;
            return ;
        }
    }

    // replace cache
    miss_count++;
    replace_count++;

    int max_time_stamp = -9999;
    int LRU_index = 0;

    for(int i = 0; i < E; i++) {
        if(_simulation_cache[set_index][i].time_stamp > max_time_stamp) {
            max_time_stamp = _simulation_cache[set_index][i].time_stamp;
            LRU_index = i;
        }
    }

    _simulation_cache[set_index][LRU_index].valid = 1;
    _simulation_cache[set_index][LRU_index].tag = tag_offset;
    _simulation_cache[set_index][LRU_index].time_stamp = 0;

}

void ModifyCache() {
    FILE *fp = fopen(tracefile_path, "r");

    char identifier;
    unsigned int address;
    int size;

    S = 1 << s;
    
    InitCache();

    while(fscanf(fp, " %c %x,%d", &identifier, &address, &size) > 0) {
        switch (identifier) {
            case 'I':
                continue;
                break;
            case 'M':
                Update(address);
                hit_count++;
                break;
            case 'L':
                Update(address);
                break;
            case 'S':
                Update(address);
                break;
            default:
                break;
        }
        UpdateTimeStamp();
    }

    fclose(fp);
    FreeCache();
}

int main(int argc, char *argv[]) {
    int opt;

    while(-1 != (opt = getopt(argc, argv, "hvs:E:b:t:"))) {
        switch (opt) {
            case 'h':
                PrintHelpInfo();
                break;
            case 'v':
                PrintHelpInfo();
                break;
            case 's':
                s = atoi(optarg);
                break;
            case 'E':
                E = atoi(optarg);
                break;
            case 'b':
                b = atoi(optarg);
                break;
            case 't':
                strcpy(tracefile_path, optarg);
                break;
            default:
                PrintHelpInfo();
                break;
        }
    }

    assert(s > 0 && E > 0 && b > 0);
    assert(tracefile_path != NULL);

    ModifyCache();

    printSummary(hit_count, miss_count, replace_count);

    return 0;
}
