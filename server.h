// server.h
#ifndef SERVER_H
#define SERVER_H

#include <stdint.h>
#include <stddef.h>

typedef struct bedrock_server bedrock_server_t;

typedef struct {
    uint16_t udp_port;
    uint16_t tcp_port;
    int    worker_threads; // number of worker threads for processing packets
    size_t max_clients;
} server_config_t;

// create/destroy
bedrock_server_t* server_create(const server_config_t* cfg);
void server_destroy(bedrock_server_t* s);

// lifecycle
int server_start(bedrock_server_t* s);
void server_stop(bedrock_server_t* s);

// stats and hooks
size_t server_get_connected_count(bedrock_server_t* s);

// Hook: packet handler. Return 0 on success, non-zero on error.
typedef int (*packet_handler_fn)(bedrock_server_t* s, const uint8_t* data, size_t len, const char* addr_str, uint16_t port, void* ctx);
void server_set_packet_handler(bedrock_server_t* s, packet_handler_fn cb, void* ctx);

#endif // SERVER_H
