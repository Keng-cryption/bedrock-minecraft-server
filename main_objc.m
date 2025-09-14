// main_objc.m
#import <Foundation/Foundation.h>
#include "server.h"

int sample_packet_handler(bedrock_server_t* s, const uint8_t* data, size_t len, const char* addr, uint16_t port, void* ctx){
    (void)ctx;
    // For manager demo, we just print short stats occasionally
    static int calls = 0;
    calls++;
    if((calls & 0x1FFFF) == 0){
        NSLog(@"[manager] packet from %s:%u total_conn=%zu", addr, port, server_get_connected_count(s));
    }
    return 0;
}

int main(int argc, const char * argv[]) {
    @autoreleasepool {
        server_config_t cfg;
        cfg.udp_port = 19132;
        cfg.tcp_port = 19133;
        cfg.worker_threads = 4;
        cfg.max_clients = 1024;
        bedrock_server_t* s = server_create(&cfg);
        server_set_packet_handler(s, sample_packet_handler, NULL);
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            server_start(s); // blocking inside; but run on background thread
        });
        NSLog(@"Manager started. Press Enter to stop.");
        getchar();
        server_stop(s);
        // allow graceful shutdown
        sleep(1);
        server_destroy(s);
    }
    return 0;
}
