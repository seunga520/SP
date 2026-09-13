/* 
 * echoserveri.c - An iterative echo server 
 */ 
/* $begin echoserverimain */
#include "csapp.h"
#include <signal.h>

typedef struct item {
    int id;
    int left_stock;
    int price;
    struct item *left;
    struct item *right;
} item;

static item *stock_root=NULL;

static item *bst_insert(item *root, int id, int cnt, int price){
    if(!root){
        item *node =(item *)Malloc(sizeof(item));
        node->id=id;
        node->left_stock=cnt;
        node->price=price;
        node->left=node->right=NULL;
        return node;
    }
    if(id < root->id) root->left=bst_insert(root->left,id,cnt,price);
    else if (id > root->id) root->right=bst_insert(root->right,id,cnt,price);
    else {
        root->left_stock=cnt;
        root->price=price;
    }
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
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%d %d %d\n", root->id, root->left_stock, root->price);
    strcat(buf, tmp);
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
            if (!node || node->left_stock < n)
                snprintf(resp, sizeof(resp), "Not enough left stocks\n");
            else {
                node->left_stock -= n;
                snprintf(resp, sizeof(resp), "[buy] success\n");
            }
            Rio_writen(connfd, resp, MAXLINE);
        }

    } else if (strcmp(cmd, "sell") == 0) {
        if (sscanf(buf, "%*s %d %d", &id, &n) == 2) {
            item *node = bst_find(stock_root, id);
            if (node) node->left_stock += n;
            snprintf(resp, sizeof(resp), "[sell] success\n");
            Rio_writen(connfd, resp, MAXLINE);
        }

    } else if (strcmp(cmd, "exit") == 0) {
        return -1;
    }
    return 0;
}

typedef struct {
    int maxfd;
    fd_set read_set;
    fd_set ready_set;
    int nready;
    int maxi;
    int clientfd[FD_SETSIZE];
    rio_t clientrio[FD_SETSIZE];
} pool; 

void init_pool(int listenfd,pool *p){
    int i;
    p->maxi=-1;
    for(i=0;i<FD_SETSIZE;i++){
        p->clientfd[i]=-1;
    }
    p-> maxfd=listenfd;
    FD_ZERO(&p->read_set);
    FD_SET(listenfd,&p->read_set);
}
void add_client(int connfd,pool *p){
    int i;
    p->nready--;
    for(i=0;i<FD_SETSIZE;i++){
        if(p->clientfd[i]<0){
            p->clientfd[i]=connfd;
            Rio_readinitb(&p->clientrio[i],connfd);

            FD_SET(connfd,&p->read_set);

            if(connfd>p->maxfd) p->maxfd=connfd;
            if(i>p->maxi) p->maxi=i;
            
            break;
        }
        if(i==FD_SETSIZE) app_error("add_client error:Too many clients");
    }
}
static void check_clients(pool *p)
{
    int i, connfd, n;
    char buf[MAXLINE];
    rio_t rio;

    for (i = 0; (i <= p->maxi) && (p->nready > 0); i++) {
        connfd = p->clientfd[i];
        rio    = p->clientrio[i];

        if ((connfd > 0) && (FD_ISSET(connfd, &p->ready_set))) {
            p->nready--;
            if ((n = Rio_readlineb(&p->clientrio[i], buf, MAXLINE)) != 0) {
                printf("server received %d bytes\n", n);
                int ret = process_request(connfd, buf);
                if (ret < 0) {
                    /* exit */
                    printf("[server] Client fd=%d exited\n", connfd);
                    Close(connfd);
                    FD_CLR(connfd, &p->read_set);
                    p->clientfd[i] = -1;
                }
            } else {
                /* EOF */
                printf("[server] Client fd=%d disconnected\n", connfd);
                Close(connfd);
                FD_CLR(connfd, &p->read_set);
                p->clientfd[i] = -1;
            }
        }
        (void)rio; 
    }
}

int main(int argc, char **argv) 
{
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;  /* Enough space for any address */  //line:netp:echoserveri:sockaddrstorage
    //char client_hostname[MAXLINE], client_port[MAXLINE];
    
    static pool pool;

    if (argc != 2) {
	fprintf(stderr, "usage: %s <port>\n", argv[0]);
	exit(0);
    }
    Signal(SIGINT, sigint_handler); 
    load_stocks();

    listenfd = Open_listenfd(argv[1]);
    init_pool(listenfd,&pool);

    while (1) {
        pool.ready_set=pool.read_set;
        pool.nready=Select(pool.maxfd+1,&pool.ready_set,NULL,NULL,NULL);

        if(FD_ISSET(listenfd,&pool.ready_set)){
            clientlen=sizeof(struct sockaddr_storage);
            connfd=Accept(listenfd, (SA *)&clientaddr,&clientlen);
            add_client(connfd,&pool);
        }    
        
        check_clients(&pool);
	    /*clientlen = sizeof(struct sockaddr_storage); 
	    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
            Getnameinfo((SA *) &clientaddr, clientlen, client_hostname, MAXLINE, 
                        client_port, MAXLINE, 0);
            printf("Connected to (%s, %s)\n", client_hostname, client_port);
	    echo(connfd);
    	Close(connfd);*/
    }
    exit(0);
}
/* $end echoserverimain */
