/* An independent painted bitmap oracle, shared by CPU and real GL checks. */
static void window_case(unsigned which,uint32_t *r,unsigned char mask[256]) {
    memset(r,0,2048*sizeof *r);memset(mask,0,256);
    unsigned rect[8][4];
    for(unsigned i=0;i<8;i++){rect[i][0]=1;rect[i][1]=1;rect[i][2]=0;rect[i][3]=0;}
    if(which<8) { /* Every register slot, asymmetric to expose axis inversion. */
        rect[which][0]=2;rect[which][1]=5;rect[which][2]=7;rect[which][3]=9;
    } else if(which==8) {
        unsigned a[8][4]={{1,2,7,6},{5,4,12,10},{15,15,15,15},{0,0,0,0},
                          {9,0,8,15},{0,9,15,8},{4095,4095,4095,4095},{14,12,4095,4095}};
        memcpy(rect,a,sizeof rect);
    } else if(which==9) {
        memset(rect,0,sizeof rect); /* Zero is one pixel, not disabled. */
    } else if(which==11) {
        rect[0][0]=rect[0][1]=0;rect[0][2]=rect[0][3]=4095;
    } /* Case10: all rectangles inverted/empty. */
    for(unsigned i=0;i<8;i++) {
        r[0x2c0/4+i]=rect[i][0]|(rect[i][2]<<16)|0xf000f000;
        r[0x2e0/4+i]=rect[i][1]|(rect[i][3]<<16)|0xf000f000;
        for(unsigned y=rect[i][1];y<=rect[i][3] && y<16;y++)
            for(unsigned x=rect[i][0];x<=rect[i][2] && x<16;x++)mask[y*16+x]=1;
    }
}
