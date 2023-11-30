#include "usbpv_lib.h"
#include "usbpv_s.h"
#include "string.h"

#ifdef _WIN32
#else
#define strcat_s strcat
#endif

static char dev_list[4096];
static int last_error_code;

int upv_get_last_error()
{
    return last_error_code;
}
bool g_is_dark = false;
const char* upv_get_error_string(int errorCode)
{
    switch(errorCode){
    case upv_s::R_Success: return "Device operation success";
    case upv_s::R_DeviceNotFound: return "Device not found";
    case upv_s::R_DeviceNotOpen: return "Device not open";
    case upv_s::R_DeviceStatus: return "Device status error";
    case upv_s::R_Load: return "Device init fail";
    case upv_s::R_WriteConfig: return "Device write data fail";
    case upv_s::R_EEInit: return "Device EE init fail";
    case upv_s::R_Thread: return "Device init process thread fail";
    }
    return "Device unkown error";
}

const char* upv_list_devices()
{
    auto res = upv_s::list_devices();
    memset(dev_list, 0, sizeof(dev_list));
    for(auto it=res.begin();it!=res.end();it++){
        if(it!=res.begin()){
            strcat_s(dev_list,",");
        }
        strcat_s(dev_list, it->c_str());
    }
    return dev_list;
}

#define UPV_FREQ_HZ (60000000)
// convert tick to real timestamp
struct upv_wrap : public upv_s{
    void* context;
    pfn_packet_handler callback;
    uint32_t utc_ts;     /**< Seconds since epoch */
    uint32_t last_ov_ts; /**< Last seen OpenVizsla timestamp. Used to detect overflows */
    uint32_t ts_offset;  /**< Timestamp offset in 1/OV_TIMESTAMP_FREQ_HZ units */
    struct timespec last_ts;

    long on_packet(unsigned long tick_60MHz, const void* data, unsigned long len, long status)
    {
        uint32_t nsec;
        /* Increment timestamp based on the 60 MHz 24-bit counter value.
         * Convert remaining clocks to nanoseconds: 1 clk = 1 / 60 MHz = 16.(6) ns
         */
        uint64_t clks;
        if (tick_60MHz < last_ov_ts) {
          ts_offset += (1 << 24);
        }
        last_ov_ts = tick_60MHz;
        clks = ts_offset + tick_60MHz;
        if (clks >= UPV_FREQ_HZ) {
          utc_ts += 1;
          ts_offset -= UPV_FREQ_HZ;
          clks -= UPV_FREQ_HZ;
        }
        nsec = (clks * 17) - (clks / 3);

        ///*
        struct timespec ts;
    #ifdef _MSC_VER
        timespec_get(&ts, TIME_UTC);
    #else
        clock_gettime(CLOCK_REALTIME, &ts);
    #endif

        long dnano = ts.tv_nsec - last_ts.tv_nsec;
        long dts = ts.tv_sec - last_ts.tv_sec;
        if(ts.tv_nsec < last_ts.tv_nsec){
            dnano += 1000000000;
            dts--;
        }
        last_ts = ts;
        if(dnano > 280179507 || dts > 0){
            utc_ts = last_ts.tv_sec;
            last_ov_ts = 0;
            ts_offset = (last_ts.tv_nsec / 17) + (last_ts.tv_nsec / 850);
            nsec = ts.tv_nsec;
        }


        if(callback){
            return callback(context, utc_ts, nsec, data, len, status);
        }
        return 0;
    }
};

long UPV_CB on_packet(upv_wrap* wrap, unsigned long tick_60MHz, const void* data, unsigned long len, long status)
{
    return wrap->on_packet(tick_60MHz, data, len, status);
}

long UPV_CB on_packet_fast(upv_wrap* wrap, unsigned long tick_60MHz, const void* data, unsigned long len, long status)
{
    return wrap->callback(wrap->context, 0, tick_60MHz, data, len, status);
}

UPV_HANDLE upv_open_device(
        const char* option,
        int opt_len,
        void* context,
        pfn_packet_handler callback)
{

    upv_wrap* pv = new upv_wrap();
    pv->context = context;
    pv->callback = callback;
    int r = pv->open(option, opt_len);
    if(r != upv_s::R_Success){
        goto error;
    }
    r = pv->start_capture(pv, (pfnt_on_packet)on_packet);
    if(r != upv_s::R_Success){
        goto error;
    }
    return pv;
error:
    delete pv;
    last_error_code = r;
    return NULL;
}

UPV_HANDLE upv_open_device_fast(
        const char* option,
        int opt_len,
        void* context,
        pfn_packet_handler callback)
{

    upv_wrap* pv = new upv_wrap();
    pv->context = context;
    pv->callback = callback;
    int r = pv->open(option, opt_len);
    if(r != upv_s::R_Success){
        goto error;
    }
    r = pv->start_capture(pv, (pfnt_on_packet)on_packet_fast);
    if(r != upv_s::R_Success){
        goto error;
    }
    return pv;
error:
    delete pv;
    last_error_code = r;
    return NULL;
}

int upv_close_device(UPV_HANDLE upv)
{
    upv_wrap* pv = (upv_wrap*)upv;
    upv_s::upv_result r = upv_s::R_Success;
    if(pv != NULL){
        r = pv->close();
        delete pv;
    }
    return r;
}

int upv_get_monitor_speed(UPV_HANDLE upv)
{
    upv_wrap* pv = (upv_wrap*)upv;
    if(pv == NULL){
        return upv_s::R_DeviceNotOpen;
    }
    return pv->bcdUSB >= 0x300?1:0;
}
