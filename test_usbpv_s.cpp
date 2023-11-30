#include "usbpv_s.h"
#include "stdio.h"

FILE* fp_data = NULL;

long UPV_CB on_packet(void* context, unsigned long tick_60MHz, const void* data, unsigned long len, long status);
int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    auto devs = upv_s::list_devices();
    printf("There are %d devices\n", devs.size());
    for(auto it = devs.begin(); it!=devs.end(); it++){
        printf("%s\n", it->c_str());
    }
    if(devs.size() < 1){
        return 0;
    }

    upv_s upv;
    unsigned char option[128] = {0};
    strncpy((char*)option,devs.begin()->c_str(), 128);
    int opt_len = strlen((char*)option);
    opt_len++; // string null end
    option[opt_len++] = CS_AutoSpeed;
    option[opt_len++] = UPV_FLAG_ALL;
    option[opt_len++] = 1;    // accept
    option[opt_len++] = 0xff; // addr1
    option[opt_len++] = 0xff; // ep1
    option[opt_len++] = 0xff; // addr2
    option[opt_len++] = 0xff; // ep2
    option[opt_len++] = 0xff; // addr3
    option[opt_len++] = 0xff; // ep3
    option[opt_len++] = 0xff; // addr4
    option[opt_len++] = 0xff; // ep4

    auto res = upv.open((char*)option, opt_len);
    if(res != 0){
        printf("fail to open %s, %d\n", devs.begin()->c_str(), res);
        return res;
    }

    fp_data = fopen("test.bin", "wb+");

    upv.start_capture(NULL, on_packet);

    printf("press any key to quit\n");
    getchar();

    res = upv.close();
    if(res != 0){
        printf("close device fail %d\n", res);
    }else{
        printf("close device success\n");
    }

    if(fp_data){
        fclose(fp_data);
        fp_data = NULL;
    }

    return 0;
}

int usbpv_record_data_unused(const uint8_t* data, int len)
{
    if(fp_data){
        fwrite(data,1,len,fp_data);
        fflush(fp_data);
    }
    return 0;
}


#define UPV_SPD_Unknown  0
#define UPV_SPD_LOW      1
#define UPV_SPD_FULL     2
#define UPV_SPD_HIGH     3
#define GetPacketSpeed(status)  ((status) & 0x03)


#define UPV_DATA_PACKET     0
#define UPV_RESET_BEGIN     1
#define UPV_RESET_END       2
#define UPV_SUSPEND_BEGIN   3
#define UPV_SUSPEND_END     4
#define UPV_OVERFLOW        0xf
#define GetPacketType(status)   (((status)>>4) & 0x0f)

const char* SPD_STR[] = { "Xxxx","Low ","Full","High" };
long UPV_CB on_packet(void* context, unsigned long tick_60MHz, const void* data, unsigned long len, long status)
{
    (void)context;
    int spd = GetPacketSpeed(status);
    int pktType = GetPacketType(status);
    const uint8_t* buf = (const uint8_t*)data;
    int remain_mem = buf[len];
    if(pktType == UPV_DATA_PACKET){
        printf("[%d] PID: %x LEN: %d %s, remain %d\n", tick_60MHz, buf[0], len, SPD_STR[spd], remain_mem);
    }else{
        printf("[%d] Bus event %d\n", tick_60MHz, pktType);
    }
    return 0;
}
