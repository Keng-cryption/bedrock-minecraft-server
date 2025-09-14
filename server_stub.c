// server_stub.c
#include "server.h"
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

static int keep_running = 1;
void int_handler(int _) { keep_running = 0; }

int sample_packet_handler(bedrock_server_t* s, const uint8_t* data, size_t len, const char* addr, uint16_t port, void* ctx){
    (void)s; (void)ctx;
    // For now, just count packets; in real code parse Bedrock packets here and respond
    // Very lightweight: print periodic dots to show activity.
    static __thread int cnt = 0;
    cnt++;
    if((cnt & 0xFFFF) == 0) printf(".");
    (void)data; (void)len; (void)addr; (void)port;
    return 0;
}

int main(){
    signal(SIGINT, int_handler);
    server_config_t cfg = {19132, 19133, 4, 1024};
    bedrock_server_t* s = server_create(&cfg);
    server_set_packet_handler(s, sample_packet_handler, NULL);
    printf("Starting server on UDP %d\n", cfg.udp_port);
    server_start(s); // blocking
    printf("\nStopping server\n");
    server_destroy(s);
    return 0;
}
