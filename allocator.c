#define _GNU_SOURCE
#include <sys/mman.h>
#include <sys/resource.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define ARENA_SIZE (64UL*1024*1024)
#define ALIGN 16UL
#define MAGIC 0xA110CA7EUL
typedef struct Block { size_t size, requested; struct Block *next; unsigned long magic; int free; } Block;
static void *arena;
static Block *head;
static int best_fit;
static size_t live, peak, allocations, frees;
static size_t align_up(size_t n) { return (n+ALIGN-1)&~(ALIGN-1); }
int my_init(int best) {
    arena=mmap(NULL,ARENA_SIZE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(arena==MAP_FAILED){perror("mmap");return -1;}
    head=arena;*head=(Block){.size=ARENA_SIZE-sizeof(Block),.next=NULL,.magic=MAGIC,.free=1};
    best_fit=best;live=peak=allocations=frees=0;return 0;
}
void my_destroy(void){if(arena)munmap(arena,ARENA_SIZE);arena=NULL;head=NULL;}
void *my_malloc(size_t n){
    if(!n || n>ARENA_SIZE)return NULL;
    size_t need=align_up(n);Block *choice=NULL;
    for(Block *b=head;b;b=b->next)if(b->free && b->size>=need){
        if(!best_fit){choice=b;break;}
        if(!choice || b->size<choice->size)choice=b;
    }
    if(!choice)return NULL;
    if(choice->size>=need+sizeof(Block)+ALIGN){
        Block *next=(Block*)((char*)(choice+1)+need);
        *next=(Block){.size=choice->size-need-sizeof(Block),.next=choice->next,.magic=MAGIC,.free=1};
        choice->next=next;choice->size=need;
    }
    choice->free=0;choice->requested=n;live+=n;if(live>peak)peak=live;allocations++;
    return choice+1;
}
static Block *find_block(void *ptr){
    for(Block *b=head;b;b=b->next)if((void*)(b+1)==ptr)return b;
    return NULL;
}
void my_free(void *ptr){
    if(!ptr)return;
    Block *b=find_block(ptr);
    if(!b || b->magic!=MAGIC){fprintf(stderr,"ERROR: invalid free\n");return;}
    if(b->free){fprintf(stderr,"ERROR: double free detected\n");return;}
    live-=b->requested;b->requested=0;b->free=1;frees++;
    for(Block *p=head;p && p->next;){
        if(p->free && p->next->free){
            p->size+=sizeof(Block)+p->next->size;p->next=p->next->next;
        }else p=p->next;
    }
}
void *my_realloc(void *ptr,size_t n){
    if(!ptr)return my_malloc(n);
    if(!n){my_free(ptr);return NULL;}
    Block *b=find_block(ptr);
    if(!b || b->free){fprintf(stderr,"ERROR: invalid realloc\n");return NULL;}
    if(n<=b->size){live=live-b->requested+n;b->requested=n;if(live>peak)peak=live;return ptr;}
    void *other=my_malloc(n);if(!other)return NULL;
    memcpy(other,ptr,b->requested);my_free(ptr);return other;
}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static size_t rss_kb(void){
    FILE *f=fopen("/proc/self/status","r");if(!f)return 0;
    char line[256];size_t rss=0;
    while(fgets(line,sizeof line,f))if(sscanf(line,"VmRSS: %zu kB",&rss)==1)break;
    fclose(f);return rss;
}
static double fragmentation(void){
    size_t total=0,largest=0;
    for(Block *b=head;b;b=b->next)if(b->free){total+=b->size;if(b->size>largest)largest=b->size;}
    return total ? 100.0*(total-largest)/total : 0;
}
static void benchmark(int custom,int best,int scenario){
    if(custom && my_init(best))exit(1);
    enum {N=2500};void *ptr[N]={0};
    size_t before=rss_kb(),maxrss=before;
    double start=now();
    for(int i=0;i<N;i++){
        size_t n=scenario==0?32+(i%8)*16:scenario==1?64+(i%10)*32:8192+(i%5)*4096;
        ptr[i]=custom?my_malloc(n):malloc(n);
        if(!ptr[i]){fprintf(stderr,"allocation failed at %d\n",i);break;}
        memset(ptr[i],0x5a,n); // touch pages so RSS becomes observable
        if(scenario==1 && i%3==2){int j=i-1;if(ptr[j]){if(custom)my_free(ptr[j]);else free(ptr[j]);ptr[j]=NULL;}}
        if(i%100==0){size_t r=rss_kb();if(r>maxrss)maxrss=r;}
    }
    for(int i=0;i<N;i++)if(ptr[i]){if(custom)my_free(ptr[i]);else free(ptr[i]);}
    double elapsed=(now()-start)*1000;
    size_t r=rss_kb();if(r>maxrss)maxrss=r;
    printf("%s,%s,%d,%.3f,%zu,%.2f\n",custom?"custom":"glibc",custom?(best?"best":"first"):"system",scenario+1,elapsed,maxrss,custom?fragmentation():-1.0);
    if(custom){if(live)fprintf(stderr,"LEAK: %zu bytes remain\n",live);my_destroy();}
}
int main(int argc,char **argv){
    if(argc>1 && !strcmp(argv[1],"test")){
        if(my_init(0))return 1;
        char *a=my_malloc(20);strcpy(a,"hello");a=my_realloc(a,100);
        printf("realloc preserves data: %s\n",a);
        my_free(a);my_free(a); // intentional double free detection
        char *leak=my_malloc(40);(void)leak;
        printf("leak detector: %zu bytes outstanding\n",live);
        my_destroy();return 0;
    }
    puts("allocator,strategy,scenario,time_ms,peak_rss_kb,external_fragmentation_pct");
    for(int scenario=0;scenario<3;scenario++){
        benchmark(1,0,scenario);benchmark(1,1,scenario);benchmark(0,0,scenario);
    }
    return 0;
}
