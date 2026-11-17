#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../templates/ps1/starter/runtime/packet_fragment.h"

static void check(int condition, const char* message) {
    if (!condition) { fprintf(stderr, "%s\n", message); exit(1); }
}

int main(void) {
    uint32_t packets[6] = {0x01001000u,0x004004u,0x01004000u,0x002008u,0x01002000u,0xffffffu};
    uint32_t ot[4] = {0xffffffu,0x4008u,0x2004u,0x4010u};
    ps1_packet_fragment_bucket entries[4];
    uint32_t scratch[2] = {0,0xfeedfaceu};
    size_t count = 0;
#define CAPTURE(source, words, address, table, capacity) \
    ps1_packet_fragment_capture(source,words,address,table,4,entries,capacity,scratch,2,&count)
    check(CAPTURE(packets,6,0x4000u,ot,4), "A fragment owns two independent bucket chains");
    check(count==2 && entries[0].bucket==1 && entries[0].head==2 && entries[0].tail==0,
          "The first chain retains its head and last owned header");
    check(entries[1].bucket==3 && entries[1].head==4 && entries[1].tail==4,
          "The second chain terminates at an earlier OT entry");
    check(scratch[1]==0xfeedfaceu, "Capture touches only the required scratch words");
    {
        uint32_t target[8] = {0}, current[4] = {0xffffffu,0x00b000u,0x009004u,0x00b100u};
        target[6]=0xfeedfaceu;
        ps1_packet_fragment_copy(target,packets,6,0x4000u,0x8000u,current,entries,count);
        check(target[0]==0x0100b000u && target[2]==0x01008000u && target[4]==0x0100b100u,
              "Internal links relocate; tails attach to the current frame's earlier entities");
        check(current[1]==0x8008u && current[3]==0x8010u, "Only owned buckets receive new heads");
        check(current[0]==0xffffffu && current[2]==0x009004u, "Unowned OT buckets stay unchanged");
        check(target[1]==packets[1] && target[3]==packets[3] && target[5]==packets[5],
              "Address-shaped payload words are never relocated");
        check(target[6]==0xfeedfaceu, "The next entity's packet memory stays untouched");
    }
    {
        uint32_t malformed[6], badot[4];
        memcpy(malformed,packets,sizeof packets); malformed[2]=0x01004008u;
        check(!CAPTURE(malformed,6,0x4000u,ot,4) && count==0, "A self-cycle is rejected");
        memcpy(malformed,packets,sizeof packets); malformed[2]=0x01004004u;
        check(!CAPTURE(malformed,6,0x4000u,ot,4), "Links into command payload are rejected");
        memcpy(malformed,packets,sizeof packets); malformed[0]=0xff001000u;
        check(!CAPTURE(malformed,6,0x4000u,ot,4), "Truncated packet lengths are rejected");
        memcpy(malformed,packets,sizeof packets); malformed[0]=0x01001001u;
        check(!CAPTURE(malformed,6,0x4000u,ot,4), "Misaligned foreign links are rejected");
        memcpy(badot,ot,sizeof ot); badot[2]=badot[1];
        check(!CAPTURE(packets,6,0x4000u,badot,4), "Multiple buckets cannot own one chain");
        memcpy(badot,ot,sizeof ot); badot[3]=0x2008u;
        check(!CAPTURE(packets,6,0x4000u,badot,4), "Unreachable packets are rejected");
        memcpy(badot,ot,sizeof ot); badot[1]=0x4004u;
        check(!CAPTURE(packets,6,0x4000u,badot,4), "A bucket cannot start in payload");
    }
    check(!CAPTURE(packets,6,0x4000u,ot,1), "Insufficient bucket storage is rejected");
    check(!CAPTURE(packets,6,0xfffffcu,ot,4), "24-bit source overflow is rejected");
    check(!CAPTURE(packets,6,0x4001u,ot,4), "A source must be word-aligned");
    check(!CAPTURE(packets,65537u,0x4000u,ot,4), "Offsets must fit the compact representation");
    check(!ps1_packet_fragment_capture(packets,6,0x4000u,ot,4,entries,4,scratch,0,&count),
          "Insufficient caller-owned scratch is rejected");
    check(CAPTURE(packets,6,0x4000u,ot,4), "The valid source remains reusable after failures");
    {
        uint32_t a[6], b[6];
        memcpy(a,packets,sizeof packets);
        for (unsigned frame=0;frame<100;++frame) {
            const uint32_t previous=0xc000u+frame*4;
            uint32_t heads[4]={0xffffffu,previous,0x9004u,previous+0x1000u};
            ps1_packet_fragment_copy(b,a,6,0x4000u,0x8000u,heads,entries,count);
            check((b[0]&0xffffffu)==previous && (b[4]&0xffffffu)==previous+0x1000u,
                  "Every frame merges its own dynamic predecessor");
            heads[1]=previous; heads[3]=previous+0x1000u;
            ps1_packet_fragment_copy(a,b,6,0x8000u,0x4000u,heads,entries,count);
            check((a[2]&0xffffffu)==0x4000u && a[1]==packets[1] && a[3]==packets[3] && a[5]==packets[5],
                  "Repeated buffer swaps preserve internal links and payload");
        }
    }
    {
        uint32_t overlap[8]={0}, overlap_ot[4], current[4]={0xffffffu,0xb000u,0x9004u,0xb100u};
        uint32_t untouched;
        memcpy(overlap+2,packets,sizeof packets); overlap[4]=0x01004008u;
        memcpy(overlap_ot,ot,sizeof ot); overlap_ot[1]+=8; overlap_ot[3]+=8;
        check(CAPTURE(overlap+2,6,0x4008u,overlap_ot,4), "An overlapping source is captured normally");
        untouched=overlap[6];
        ps1_packet_fragment_copy(overlap,overlap+2,6,0x4008u,0x4000u,current,entries,count);
        check(overlap[0]==0x0100b000u && overlap[2]==0x01004000u && overlap[4]==0x0100b100u,
              "Downward overlapping copies relocate each source header before overwrite");
        check(overlap[1]==packets[1] && overlap[3]==packets[3] && overlap[5]==packets[5] && overlap[6]==untouched,
              "Overlapping copies preserve payload and following memory");
    }
    {
        uint32_t retained[7], target[6];
        memcpy(retained,packets,sizeof packets);
        retained[6]=0xfeedfaceu;
        check(CAPTURE(retained,6,0x4000u,ot,4), "In-place commands are captured before reuse");
        for(unsigned frame=0;frame<100;++frame) {
            const uint32_t previous=0xb000u+frame*4u;
            uint32_t heads[4]={0xffffffu,previous,0x9004u,previous+0x1000u};
            ps1_packet_fragment_copy(retained,retained,6,0x4000u,0x4000u,heads,entries,count);
            check(retained[0]==(0x01000000u|previous) &&
                  retained[4]==(0x01000000u|(previous+0x1000u)),
                  "In-place reuse replaces both previous-frame predecessors");
            check(retained[2]==0x01004000u && heads[1]==0x4008u && heads[3]==0x4010u,
                  "In-place reuse preserves internal links and restores bucket heads");
            check(retained[1]==packets[1] && retained[3]==packets[3] &&
                  retained[5]==packets[5] && retained[6]==0xfeedfaceu && heads[2]==0x9004u,
                  "In-place reuse preserves payload, foreign buckets and following storage");
        }
        memset(target,0,sizeof target);
        {
            uint32_t heads[4]={0xffffffu,0xb000u,0x9004u,0xb100u};
            ps1_packet_fragment_copy(target,packets,6,0x4000u,0x4000u,heads,entries,count);
            check(target[1]==packets[1] && target[2]==0x01004000u,
                  "Equal DMA bases with distinct CPU buffers still copy their payload");
        }
        {
            uint32_t heads[4]={0xffffffu,0xb000u,0x9004u,0xb100u};
            memcpy(retained,packets,sizeof packets);
            ps1_packet_fragment_copy(retained,retained,6,0x4000u,0x8000u,heads,entries,count);
            check(retained[2]==0x01008000u && heads[1]==0x8008u,
                  "In-place relocation with a changed DMA base still rebases links");
        }
    }
    {
        uint32_t packet=0x00ffffffu, table[1]={0xfffffcu};
        check(ps1_packet_fragment_capture(&packet,1,0xfffffcu,table,1,entries,4,scratch,2,&count),
              "The DMA terminator stays a terminator at the 24-bit boundary");
    }
    {
        const size_t words = PS1_PACKET_FRAGMENT_MAX_WORDS;
        uint32_t* source = malloc(words * sizeof *source);
        uint32_t* target = malloc(words * sizeof *target);
        uint32_t* heads = malloc(65536u * sizeof *heads);
        uint32_t* bits = malloc(((words + 31u) / 32u) * sizeof *bits);
        ps1_packet_fragment_bucket boundary;
        check(source && target && heads && bits, "Boundary test storage is available");
        for (size_t i = 0; i < words; ++i)
            source[i] = i ? 0x4000u + (uint32_t)(i - 1u) * 4u : 0xffffffu;
        for (size_t i = 0; i < 65536u; ++i) heads[i] = 0xffffffu;
        heads[65535] = 0x4000u + (uint32_t)(words - 1u) * 4u;
        check(ps1_packet_fragment_capture(source, words, 0x4000u, heads, 65536u,
                  &boundary, 1, bits, (words + 31u) / 32u, &count),
              "The largest representable fragment and bucket are supported");
        check(count == 1 && boundary.bucket == 65535u &&
                  boundary.head == 65535u && boundary.tail == 0,
              "Maximum header and bucket offsets do not wrap");
        heads[65535] = 0xb000u;
        ps1_packet_fragment_copy(target, source, words, 0x4000u, 0x80000u,
                                 heads, &boundary, count);
        check(heads[65535] == 0xbfffcu && target[0] == 0xb000u &&
                  target[65535] == 0xbfff8u,
              "The largest fragment relocates and rejoins its predecessor");
        check(!ps1_packet_fragment_capture(source, words, 0x4000u, heads, 65537u,
                  &boundary, 1, bits, (words + 31u) / 32u, &count),
              "Bucket indices beyond the compact representation are refused");
        free(bits);
        free(heads);
        free(target);
        free(source);
    }
    puts("PS1 packet-fragment tests passed");
    return 0;
}
