#include "apu_dsp_dma.h"
#include <string.h>
#include <stdlib.h>
typedef struct {uint8_t *address;size_t length;} Segment;
typedef struct {Segment *segments;size_t count,capacity;Segment local[16];} Plan;
static void release(Plan *p) {if(p->segments!=p->local)free(p->segments);}
static bool append(Plan *p,uint8_t *address,size_t length) {
    if(p->count==p->capacity) {
        if(p->capacity>SIZE_MAX/(2*sizeof(Segment)))return false;
        size_t capacity=p->capacity*2;
        Segment *grown=malloc(capacity*sizeof *grown);if(!grown)return false;
        memcpy(grown,p->segments,p->count*sizeof *grown);release(p);
        p->segments=grown;p->capacity=capacity;
    }
    p->segments[p->count++]=(Segment){address,length};return true;
}
static bool sg_plan(Plan *plan,SsxDspBusResolve resolve,void *opaque,uint32_t table,uint32_t maximum,
                    uint32_t address,size_t length) {
    uint64_t logical=address;
    while(length) {
        uint64_t page=logical/4096,offset=logical%4096;
        uint64_t entry=(uint64_t)table+page*8;
        if(page>maximum)return false;
        const uint8_t *p=resolve(opaque,entry,8);if(!p)return false;
        uint32_t physical=p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
        uint64_t start=(uint64_t)physical+offset;
        size_t chunk=4096-(size_t)offset;if(chunk>length)chunk=length;
        uint8_t *destination=resolve(opaque,start,chunk);if(!destination)return false;
        if(!append(plan,destination,chunk))return false;
        logical+=chunk;length-=chunk;
    }
    return true;
}
static void execute(const Plan *p,uint8_t *buffer,bool write) {
    for(size_t i=0;i<p->count;i++) {
        Segment s=p->segments[i];
        if(write)memcpy(s.address,buffer,s.length);
        else memcpy(buffer,s.address,s.length);
        buffer+=s.length;
    }
}
bool ssx_dsp_sg_bus(SsxDspBusResolve resolve,void *opaque,uint32_t table,uint32_t maximum,
    uint8_t *buffer,uint32_t address,size_t length,bool write) {
    if(!resolve || (length && !buffer))return false;
    Plan p={0};p.segments=p.local;p.capacity=16;
    bool valid=sg_plan(&p,resolve,opaque,table,maximum,address,length);
    if(valid)execute(&p,buffer,write);
    release(&p);return valid;
}
bool ssx_dsp_ring_bus(SsxDspBusResolve resolve,void *opaque,uint32_t table,uint32_t maximum,
    uint8_t *buffer,uint32_t base,uint32_t end,uint32_t *current,size_t length,bool write) {
    if(!resolve || !current || end<=base || *current>=end || (length && !buffer))return false;
    uint32_t cursor=*current<base?base:*current;size_t remaining=length;
    Plan p={0};p.segments=p.local;p.capacity=16;
    /* Resolve all segments before a write can overwrite its own table. */
    while(remaining) {
        size_t chunk=end-cursor;if(chunk>remaining)chunk=remaining;
        if(!sg_plan(&p,resolve,opaque,table,maximum,cursor,chunk)) {release(&p);return false;}
        remaining-=chunk;cursor+=(uint32_t)chunk;if(cursor==end)cursor=base;
    }
    execute(&p,buffer,write);release(&p);*current=cursor;return true;
}
typedef struct {uint8_t *ram;size_t size;} Ram;
static uint8_t *resolve_ram(void *opaque,uint64_t address,size_t length) {
    Ram *r=opaque;
    if(!r->ram || address>r->size || length>r->size-address)return NULL;
    return r->ram+(size_t)address;
}
bool ssx_dsp_sg_transfer(uint8_t *ram,size_t size,uint32_t table,uint32_t maximum,
    uint8_t *buffer,uint32_t address,size_t length,bool write) {
    Ram r={ram,size};return ssx_dsp_sg_bus(resolve_ram,&r,table,maximum,buffer,address,length,write);
}
bool ssx_dsp_ring_transfer(uint8_t *ram,size_t size,uint32_t table,uint32_t maximum,
    uint8_t *buffer,uint32_t base,uint32_t end,uint32_t *current,size_t length,bool write) {
    Ram r={ram,size};return ssx_dsp_ring_bus(resolve_ram,&r,table,maximum,buffer,base,end,current,length,write);
}
