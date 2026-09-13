
#include "csapp.h"
#include <signal.h>


#define NTHREADS  4     
#define SBUFSIZE  16    


typedef struct item {
    int id;
    int left_stock;
    int price;

    int   readcnt;      
    sem_t mutex;        
    sem_t w;            
    struct item *left;
    struct item *right;
} item;

static item *stock_root = NULL;

static item *bst_insert(item *root, int id, int cnt, int price)
{
    if (!root) {
        item *node = (item *)Malloc(sizeof(item));
        node->id         = id;
        node->left_stock = cnt;
        node->price      = price;
        node->readcnt    = 0;
        Sem_init(&node->mutex, 0, 1);
        Sem_init(&node->w,     0, 1);
        node->left = node->right = NULL;
        return node;
    }
    if      (id < root->id) root->left  = bst_insert(root->left,  id, cnt, price);
    else if (id > root->id) root->right = bst_insert(root->right, id, cnt, price);
    else { root->left_stock = cnt; root->price = price; }
    return root;
}

static item *bst_find(item *root, int id)
{
    if (!root)          return NULL;
    if (id == root->id) return root;
    if (id  < root->id) return bst_find(root->left,  id);
    return                     bst_find(root->right, id);
}

static void bst_show(item *root, char *buf)
{
    if (!root) return;
    bst_show(root->left, buf);

    P(&root->mutex);
    root->readcnt++;
    if (root->readcnt == 1) P(&root->w);  
    V(&root->mutex);

    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%d %d %d\n",
             root->id, root->left_stock, root->price);
    strcat(buf, tmp);

    P(&root->mutex);
    root->readcnt--;
    if (root->readcnt == 0) V(&root->w);  
    V(&root->mutex);

    bst_show(root->right, buf);
}

static void bst_save(item *root, FILE *fp)
{
    if (!root) return;
    bst_save(root->left, fp);
    fprintf(fp, "%d %d %d\n", root->id, root->left_stock, root->price);
    bst_save(root->right, fp);
}

static void bst_free(item *root)
{
    if (!root) return;
    bst_free(root->left);
    bst_free(root->right);
    free(root);
}

static void load_stocks(void)
{
    FILE *fp = fopen("stock.txt", "r");
    if (!fp) { fprintf(stderr, "[server] stock.txt not found\n"); return; }
    int id, cnt, price;
    while (fscanf(fp, "%d %d %d", &id, &cnt, &price) == 3)
        stock_root = bst_insert(stock_root, id, cnt, price);
    fclose(fp);
    printf("[server] stock.txt loaded\n");
}

static void save_stocks(void)
{
    FILE *fp = fopen("stock.txt", "w");
    if (!fp) { fprintf(stderr, "[server] failed to save stock.txt\n"); return; }
    bst_save(stock_root, fp);
    fclose(fp);
    printf("[server] stock.txt saved\n");
}

static void sigint_handler(int sig)
{
    (void)sig;
    save_stocks();
    bst_free(stock_root);
    printf("[server] Bye.\n");
    exit(0);
}

static int process_request(int connfd, char *buf)
{
    char cmd[16];
    int  id, n;
    char resp[MAXLINE * 16];

    buf[strcspn(buf, "\r\n")] = '\0';
    if (sscanf(buf, "%15s", cmd) < 1) return 0;

    if (strcmp(cmd, "show") == 0) {
        resp[0] = '\0';
        bst_show(stock_root, resp);     
        Rio_writen(connfd, resp, MAXLINE);

    } else if (strcmp(cmd, "buy") == 0) {
        if (sscanf(buf, "%*s %d %d", &id, &n) == 2) {
            item *node = bst_find(stock_root, id);
            if (!node) {
                snprintf(resp, sizeof(resp), "Not enough left stocks\n");
                Rio_writen(connfd, resp, MAXLINE);
                return 0;
            }
            P(&node->w);
            if (node->left_stock < n)
                snprintf(resp, sizeof(resp), "Not enough left stocks\n");
            else {
                node->left_stock -= n;
                snprintf(resp, sizeof(resp), "[buy] success\n");
            }
            V(&node->w);
            Rio_writen(connfd, resp, MAXLINE);
        }

    } else if (strcmp(cmd, "sell") == 0) {
        if (sscanf(buf, "%*s %d %d", &id, &n) == 2) {
            item *node = bst_find(stock_root, id);
            if (node) {
                P(&node->w);
                node->left_stock += n;
                V(&node->w);
            }
            snprintf(resp, sizeof(resp), "[sell] success\n");
            Rio_writen(connfd, resp, MAXLINE);
        }

    } else if (strcmp(cmd, "exit") == 0) {
        return -1;
    }
    return 0;
}


typedef struct {
    int  *buf;      
    int   n;        
    int   front;    
    int   rear;    
    sem_t mutex;    
    sem_t slots;    
    sem_t items;   
} sbuf_t;

static sbuf_t sbuf;

static void sbuf_init(sbuf_t *sp, int n)
{
    sp->buf   = Malloc(n * sizeof(int));
    sp->n     = n;
    sp->front = sp->rear = 0;
    Sem_init(&sp->mutex, 0, 1);
    Sem_init(&sp->slots, 0, n);
    Sem_init(&sp->items, 0, 0);
}

static void sbuf_insert(sbuf_t *sp, int item)
{
    P(&sp->slots);                         
    P(&sp->mutex);                         
    sp->buf[(++sp->rear) % sp->n] = item;
    V(&sp->mutex);
    V(&sp->items);                       
}

static int sbuf_remove(sbuf_t *sp)
{
    int item;
    P(&sp->items);                        
    P(&sp->mutex);
    item = sp->buf[(++sp->front) % sp->n];
    V(&sp->mutex);
    V(&sp->slots);               
    return item;
}


static void *thread(void *vargp)
{
    (void)vargp;
    Pthread_detach(Pthread_self());    

    while (1) {
        int connfd = sbuf_remove(&sbuf);   

        rio_t rio;
        Rio_readinitb(&rio, connfd);

        char buf[MAXLINE];
        int  n;
        while ((n = Rio_readlineb(&rio, buf, MAXLINE)) > 0) {
            printf("server received %d bytes\n", n);
            if (process_request(connfd, buf) < 0)
                break;        
        }

        printf("[server] Client fd=%d disconnected\n", connfd);
        Close(connfd);         
    }
    return NULL;
}


int main(int argc, char **argv)
{
    int listenfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }

    Signal(SIGINT, sigint_handler);
    load_stocks();

    sbuf_init(&sbuf, SBUFSIZE);


    for (int i = 0; i < NTHREADS; i++)
        Pthread_create(&tid, NULL, thread, NULL);

    listenfd = Open_listenfd(argv[1]);
    printf("[server] Listening on port %s (thread-based, %d workers)\n",
           argv[1], NTHREADS);

    while (1) {
        clientlen = sizeof(struct sockaddr_storage);
        int connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        sbuf_insert(&sbuf, connfd);     
    }

    return 0;
}
