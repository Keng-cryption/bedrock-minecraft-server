// server.c
#define _GNU_SOURCE
#include "server.h"
#include "mempool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <fcntl.h>
#include <stdatomic.h>

#define MAX_EVENTS 1024
#define UDP_BUFSIZE 2048
#define CLIENT_POOL 65536

struct packet_job {
    uint8_t data[UDP_BUFSIZE];
    size_t len;
    struct sockaddr_in addr;
};

typedef struct job_node {
    struct packet_job job;
    struct job_node* next;
} job_node_t;

// simple lock-free queue (single-producer multiple-consumer-ish with spin)
typedef struct job_queue {
    job_node_t* head;
    job_node_t* tail;
    pthread_mutex_t lock;
    pthread_cond_t  cond;
} job_queue_t;

struct bedrock_server {
    server_config_t cfg;
    int epoll_fd;
    int udp_fd;
    int tcp_fd;
    pthread_t accept_thread;
    pthread_t* workers;
    size_t worker_count;
    job_queue_t q;
    mempool_t* packet_pool;
    atomic_size_t connected;
    packet_handler_fn handler;
    void* handler_ctx;
    atomic_int running;
};

static void job_queue_init(job_queue_t* q){
    q->head = q->tail = NULL;
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->cond, NULL);
}
static void job_queue_push(job_queue_t* q, job_node_t* n){
    n->next = NULL;
    pthread_mutex_lock(&q->lock);
    if(q->tail) q->tail->next = n;
    else q->head = n;
    q->tail = n;
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->lock);
}
static job_node_t* job_queue_pop(job_queue_t* q){
    pthread_mutex_lock(&q->lock);
    while(q->head == NULL) pthread_cond_wait(&q->cond, &q->lock);
    job_node_t* n = q->head;
    q->head = n->next;
    if(q->head == NULL) q->tail = NULL;
    pthread_mutex_unlock(&q->lock);
    return n;
}

static int set_nonblocking(int fd){
    int flags = fcntl(fd, F_GETFL, 0);
    if(flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void* worker_func(void* arg){
    bedrock_server_t* s = (bedrock_server_t*)arg;
    while(atomic_load(&s->running)){
        job_node_t* n = job_queue_pop(&s->q);
        if(!n) continue;
        // call user's packet handler
        if(s->handler) {
            char addrbuf[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &n->job.addr.sin_addr, addrbuf, sizeof(addrbuf));
            s->handler(s, n->job.data, n->job.len, addrbuf, ntohs(n->job.addr.sin_port), s->handler_ctx);
        }
        // return node to pool
        mempool_free(s->packet_pool, n);
    }
    return NULL;
}

bedrock_server_t* server_create(const server_config_t* cfg){
    bedrock_server_t* s = calloc(1, sizeof(*s));
    if(!s) return NULL;
    s->cfg = *cfg;
    s->epoll_fd = -1;
    s->udp_fd = -1;
    s->tcp_fd = -1;
    s->worker_count = cfg->worker_threads > 0 ? cfg->worker_threads : 4;
    s->workers = calloc(s->worker_count, sizeof(pthread_t));
    job_queue_init(&s->q);
    s->packet_pool = mempool_create(sizeof(job_node_t), CLIENT_POOL);
    atomic_store(&s->connected, 0);
    atomic_store(&s->running, 0);
    return s;
}

void server_destroy(bedrock_server_t* s){
    if(!s) return;
    if(s->workers) free(s->workers);
    if(s->packet_pool) mempool_destroy(s->packet_pool);
    free(s);
}

void server_set_packet_handler(bedrock_server_t* s, packet_handler_fn cb, void* ctx){
    s->handler = cb;
    s->handler_ctx = ctx;
}

size_t server_get_connected_count(bedrock_server_t* s){
    return (size_t)atomic_load(&s->connected);
}

static int create_udp(bind) {
    (void)bind;
    return 0;
}

int server_start(bedrock_server_t* s){
    if(atomic_load(&s->running)) return -1;
    atomic_store(&s->running, 1);

    // create UDP socket (Bedrock uses UDP / RakNet-ish)
    s->udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(s->udp_fd < 0){ perror("socket"); return -1; }
    int opt = 1;
    setsockopt(s->udp_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(s->udp_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif
    struct sockaddr_in addr;
    memset(&addr,0,sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(s->cfg.udp_port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if(bind(s->udp_fd, (struct sockaddr*)&addr, sizeof(addr))<0){ perror("bind udp"); return -1; }
    set_nonblocking(s->udp_fd);

    // epoll
    s->epoll_fd = epoll_create1(0);
    if(s->epoll_fd < 0){ perror("epoll_create1"); return -1; }
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = s->udp_fd;
    if(epoll_ctl(s->epoll_fd, EPOLL_CTL_ADD, s->udp_fd, &ev) < 0){ perror("epoll_ctl udp"); return -1; }

    // start workers
    for(size_t i=0;i<s->worker_count;i++){
        pthread_create(&s->workers[i], NULL, worker_func, s);
    }

    // main loop thread (we'll just reuse current thread to poll)
    struct epoll_event events[MAX_EVENTS];
    while(atomic_load(&s->running)){
        int n = epoll_wait(s->epoll_fd, events, MAX_EVENTS, 1000);
        for(int i=0;i<n;i++){
            if(events[i].data.fd == s->udp_fd){
                // UDP: read as many datagrams as possible
                while(1){
                    job_node_t* node = mempool_alloc(s->packet_pool);
                    if(!node){
                        // drop if pool exhausted
                        uint8_t tmpbuf[UDP_BUFSIZE];
                        struct sockaddr_in tmpaddr;
                        socklen_t sl = sizeof(tmpaddr);
                        ssize_t r = recvfrom(s->udp_fd, tmpbuf, UDP_BUFSIZE, 0, (struct sockaddr*)&tmpaddr, &sl);
                        if(r<=0) break;
                        // dropped
                        continue;
                    }
                    socklen_t sl = sizeof(node->job.addr);
                    ssize_t r = recvfrom(s->udp_fd, node->job.data, UDP_BUFSIZE, 0, (struct sockaddr*)&node->job.addr, &sl);
                    if(r <= 0){
                        mempool_free(s->packet_pool, node);
                        break;
                    }
                    node->job.len = (size_t)r;
                    job_queue_push(&s->q, node);
                }
            }
        }
    }

    // shutdown workers
    for(size_t i=0;i<s->worker_count;i++){
        // push dummy nodes to wake them; simpler: set running false and signal
        pthread_cond_broadcast(&s->q.cond);
    }
    for(size_t i=0;i<s->worker_count;i++) pthread_join(s->workers[i], NULL);

    close(s->udp_fd);
    close(s->epoll_fd);
    return 0;
}

void server_stop(bedrock_server_t* s){
    atomic_store(&s->running, 0);
    // wake any waiting threads
    pthread_cond_broadcast(&s->q.cond);
}
