#include "usbpv_s.h"
#include "string.h"
#include "pthread.h"
#include "signal.h"

const uint8_t init_data[] = {
#include "init_data.txt"
};

#define UPV_VID 0x16C0
#define UPV_PID 0x05DC
#define UPV_MAN "tusb.org"

#define UPV_START_CMD 0x57010155
#define UPV_STOP_CMD  0x56000155

#define DBG_PRINTF   printf


int upv_usb_open_serial(libusb_context *usb_ctx, libusb_device_handle **usb_dev, int vendor, int product,
                             const char* description, const char* serial, uint16_t* bcdUSB, const char** error_string);

int upv_get_status(libusb_device_handle *usb_dev, const char** error_string);
int upv_reset_device(libusb_device_handle *usb_dev, const char** error_string);
int upv_start_device(libusb_device_handle *usb_dev, const char** error_string);
int upv_write_data(libusb_device_handle *usb_dev, unsigned char *buf, int size, const char** error_string);
int upv_write_config_data(libusb_device_handle *usb_dev, uint8_t id, uint8_t val);

static void msleep(unsigned int ms) {
#ifdef WIN32
  Sleep(ms);
#elif _POSIX_C_SOURCE >= 199309L
  struct timespec ts;
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000;
  nanosleep(&ts, NULL);
#else
  usleep(ms * 1000);
#endif
}

upv_s::upv_s()
    :usb_ctx(NULL)
    ,usb_dev(NULL)
    ,buf_data_q(NULL)
    ,data_reader_q(NULL)
    ,data_parser_q(NULL)
    ,packet_handler(NULL)
    ,capture_finish(1)
    ,data_state(0)
{

}
upv_s::~upv_s(){
    close();
}

upv_s::upv_result upv_s::open(const char* option, int opt_len)
{
    close();
    if(libusb_init(&usb_ctx)<0){
        return upv_s::R_EEInit;
    }
    char sn[128] = "";
    if(opt_len > 0){
        strncpy(sn, option, opt_len);
    }else{
        strncpy(sn, option, sizeof(sn));
    }
    int sn_len = strlen(sn);
    int r = upv_usb_open_serial(usb_ctx, &usb_dev, UPV_VID, UPV_PID, UPV_MAN, sn, &bcdUSB, &last_error_string);
    if(r < 0){
        if(r == -3){
            return upv_s::R_DeviceNotFound;
        }
        return upv_s::R_DeviceNotOpen;
    }

    int retry = 3;
    do{
        r = upv_get_status(usb_dev, NULL);
        if(r < 0){
            return upv_s::R_DeviceStatus;
        }
        if((r & 0xf0) == 0){
            break;
        }
        retry--;
        if(retry <= 0){
            return upv_s::R_Load;
            break;
        }
        msleep(1);
        r = upv_reset_device(usb_dev, NULL);
        if(r < 0){
            return upv_s::R_Load;
        }
    }while(retry>0);

    r = upv_write_data(usb_dev, (uint8_t*)init_data, sizeof(init_data), NULL);
    if(r < 0){
        return upv_s::R_Load;
    }

    retry = 3;
    do{
        r = upv_get_status(usb_dev, NULL);
        if(r < 0){
            return upv_s::R_DeviceStatus;
        }
        if((r & 0x0f) == 3){
            break;
        }
        retry--;
        if(retry <= 0){
            return upv_s::R_Load;
            break;
        }
    }while(retry>0);

    r = upv_start_device(usb_dev, NULL);
    if(r < 0){
        return upv_s::R_Load;
    }

    int speed = CS_AutoSpeed;
    int param_index = sn_len+1;
    if(opt_len > param_index){
        speed = option[param_index++];
    }
    r = upv_write_config_data(usb_dev, 8, 0x0c | (speed & 0x03));
    if( r < 0) return upv_s::R_WriteConfig;

    uint8_t flag = UPV_FLAG_ALL;
    if(opt_len > param_index){
        flag = option[param_index++];
    }
    r = upv_write_config_data(usb_dev, 31, flag^0xff);
    if( r < 0) return upv_s::R_WriteConfig;

    {
        struct AddrEpFilter_t{
            uint8_t addr        :7;
            uint8_t addr_valid  :1;
            uint8_t ep          :4;
            uint8_t reserved    :1;
            uint8_t accept      :1;
            uint8_t valid       :1;
            uint8_t ep_valid    :1;
        };

        uint8_t filter[8];
        int accFilterIdx = param_index;
        int accFilter = 1;
        if(opt_len > accFilterIdx){
            accFilter = option[accFilterIdx];
        }
        memset(filter, 0, sizeof(filter));
        AddrEpFilter_t* pFilter = (AddrEpFilter_t*)filter;
        if(accFilter){
            // this filter will accept all items
        }else{
            // this filter will drop all items
            pFilter[0].accept = 1;
        }
        int hasValidItem = 0;
        for(int i=0;i<4;i++){
            int valid = 0;
            if(opt_len > accFilterIdx+1+i*2){
                int addr = option[accFilterIdx+1+i*2];
                if(addr >=0 && addr <= 127){
                    pFilter[i].addr_valid = 1;
                    pFilter[i].addr = addr;
                    hasValidItem = 1;
                    valid = 1;
                }
            }
            if(opt_len > accFilterIdx+2+i*2){
                int ep = option[accFilterIdx+2+i*2];
                if(ep >=0 && ep <= 15){
                    pFilter[i].ep_valid = 1;
                    pFilter[i].ep = ep;
                    hasValidItem = 1;
                    valid = 1;
                }
            }
            if(valid)pFilter[i].valid = 1;
        }
        if(hasValidItem){
            for(int i=0;i<4;i++){
                pFilter[i].accept = accFilter != 0;
            }
        }else{
            // no valid item found, fall back to default value
            memset(filter, 0, sizeof(filter));
            if(!accFilter){
                pFilter[0].accept = 1;
            }
        }

        uint8_t addr = 32;
        for(int i=0;i<8;i++){
            uint8_t v = filter[i];
            r = upv_write_config_data(usb_dev, addr, v);
            if(r!=R_Success){
                return upv_s::R_WriteConfig;
            }
            addr++;
        }
    }

    return upv_s::R_Success;
}

static void* reader_thread_callback(void* upv)
{
    return ((upv_s*)upv)->reader_thread_func();
}
static void* parser_thread_callback(void* upv)
{
    return ((upv_s*)upv)->parser_thread_func();
}

upv_s::upv_result upv_s::start_capture(void* context, pfnt_on_packet callback)
{
    capture_context = context;
    packet_handler = callback;
    if(usb_dev == NULL){
        return upv_s::R_DeviceNotOpen;
    }

    buf_data_q = new upv_queue<buf_data_t>;
    data_reader_q = new upv_queue<int>;
    data_parser_q = new upv_queue<int>;

    capture_finish = 0;

    int r = pthread_create(&reader_thread, NULL, reader_thread_callback, this);
    if(r != 0){
        return upv_s::R_Thread;
    }
    r = pthread_create(&parser_thread, NULL, parser_thread_callback, this);
    if(r != 0){
        return upv_s::R_Thread;
    }

    data_state = 0;
    uint32_t data = UPV_START_CMD;
    r = upv_write_data(usb_dev, (uint8_t*)&data, 4, NULL);
    if (r < 0) { return upv_s::R_WriteConfig; }
    return upv_s::R_Success;
}

static void LIBUSB_CALL usb_data_callback(struct libusb_transfer* transfer) {
    int ret = 0;
    upv_s* upv = (upv_s*)transfer->user_data;
    switch (transfer->status) {
    case LIBUSB_TRANSFER_COMPLETED: {
        if (transfer->actual_length > 0) {
            upv->buf_data_q->en_q({transfer->buffer, transfer->actual_length});
            transfer->buffer = upv->mem_pool.get();
        }
        ret = libusb_submit_transfer(transfer);
        if (ret < 0) {
            upv->capture_finish = 1;
        }
    } break;
    case LIBUSB_TRANSFER_CANCELLED: {
        upv->capture_finish = 1;
    } break;
    case LIBUSB_TRANSFER_TIMED_OUT: {
        if (!upv->capture_finish) {
            ret = libusb_submit_transfer(transfer);
            if (ret < 0) {
                upv->capture_finish = 1;
            }
        }
    }break;
    case LIBUSB_TRANSFER_ERROR:
    case LIBUSB_TRANSFER_STALL:
    case LIBUSB_TRANSFER_NO_DEVICE:
    case LIBUSB_TRANSFER_OVERFLOW:
    default: {
        upv->capture_finish = 1;
    } break;
    }
}

void* upv_s::reader_thread_func()
{
    struct libusb_transfer* usb_transfer = libusb_alloc_transfer(0);
    capture_finish = 0;
    libusb_fill_bulk_transfer(usb_transfer, usb_dev, 0x81, mem_pool.get(), mem_pool.size,
              &usb_data_callback, this, 1000);

    int ret = 0;
    if ((ret = libusb_submit_transfer(usb_transfer)) < 0) {
        libusb_free_transfer(usb_transfer);
        capture_finish = 1;
        buf_data_q->en_q({0,0});
        data_reader_q->en_q(0);
        return NULL;
    }

    do {
        if ((ret = libusb_handle_events_completed(usb_ctx, NULL)) < 0) {
            capture_finish = 1;
        }
    } while (!capture_finish);

    buf_data_q->en_q({0,0});
    libusb_cancel_transfer(usb_transfer);
    data_reader_q->en_q(0);

    return NULL;
}

void* upv_s::parser_thread_func()
{
    buf_data_t msg;
    while (buf_data_q->de_q(msg)) {
        if (msg.buffer && msg.len) {
            int len = (int)msg.len;
            uint8_t* data = (uint8_t*)msg.buffer;
            int ret = process_data(data, len);
            mem_pool.put(data);
            if (ret < 0) {
                capture_finish = 1;
                break;
            }
        }else{
            capture_finish = 1;
            break;
        }
    }
    data_parser_q->en_q(0);
    return NULL;
}

const uint8_t speed_cvt[16] = {
  0x03,
  0x02,
  0x01,
};

__attribute__((weak)) int usbpv_record_data(const uint8_t* data, int len){ (void)data; (void)len; return 0; }
int upv_s::process_data(const uint8_t* data, int len)
{
    const uint32_t* buf = (const uint32_t*)data;
    int count = len/4;
    int ret = 0;
    for(;count>0;count--,buf++){
        uint32_t header = *buf;
        last_header = header;
        switch(data_state){
        case 0:
            if(header == UPV_START_CMD){
                data_state = 1;
            }
            break;
        case 1:
            if(header == UPV_STOP_CMD){
                ret = -1;
                break;
            }
            pkt_tick = header>>8;
            pkt_status = speed_cvt[header&0x0f] | (header & 0xf0);
            data_buf_idx = 0;
            if((header & 0xf0) == 0x60){
                pkt_status&=0xffffff0f;
                data_state = 2;
            }else{
                if(packet_handler){
                    packet_handler(capture_context, pkt_tick, data, 0, pkt_status);
                }
            }
            break;
        case 2:
            pkt_len = header & 0xffff;
            if(pkt_len > (1024+3)){
                // wrong packet data
                data_state = 10; // goto recover mode
                break;
            }
            data_buf[data_buf_idx++] = header;
            if(pkt_len <= 2){
                if(packet_handler){
                    packet_handler(capture_context, pkt_tick, ((char*)data_buf)+2, pkt_len, pkt_status);
                }
                data_state = 1;
            }else{
                data_state = 3;
            }
            break;
        case 3:
            data_buf[data_buf_idx++] = header;
            if(data_buf_idx * 4 - 2 >= pkt_len){
                if(packet_handler){
                    packet_handler(capture_context, pkt_tick, ((char*)data_buf)+2, pkt_len, pkt_status);
                }
                data_state = 1;
            }
            break;
        case 4:
            if(header == UPV_STOP_CMD){
                ret = -1;
                break;
            }
            break;
        case 10:
             pkt_len = header & 0xffff;
             if((last_header & 0xf0) == 0x60 && pkt_len<=(1024+3)){
                 data_buf[data_buf_idx++] = header;
                 if(pkt_len <= 2){
                     if(packet_handler){
                         packet_handler(capture_context, pkt_tick, ((char*)data_buf)+2, pkt_len, pkt_status);
                     }
                     data_state = 1;
                 }else{
                     data_state = 3;
                 }
                 break;
             }
            break;
        default:
            data_state = 1;
            break;
        }
        if(ret<0)break;
    }
    usbpv_record_data(data, len);
    return ret;
}

upv_s::upv_result upv_s::stop_capture(int timeout)
{
    static uint32_t stop_cmd = UPV_STOP_CMD;
    if(capture_finish){
        return upv_s::R_Success;
    }
    data_state = 4;
    int r = upv_write_data(usb_dev, (uint8_t*)&stop_cmd, 4, NULL);
    if (r < 0) { return upv_s::R_WriteConfig; }
    int tmp;
    int retry = 3;
    bool de_q_res = false;
    void* thread_res;
    for(int i=0;i<retry;i++){
        de_q_res = data_reader_q->de_q_timeout(tmp, timeout);
        if(de_q_res){
            break;
        }
        DBG_PRINTF("reader thread reamain %d\n", retry-i);
        capture_finish = 1;
        r = upv_write_data(usb_dev, (uint8_t*)&stop_cmd, 4, NULL);
        if (r < 0) { return upv_s::R_WriteConfig; }
    }
    if(de_q_res){
        pthread_join(reader_thread, &thread_res);
    }else{
        DBG_PRINTF("reader thread will terminate\n");
        pthread_kill(reader_thread, 0);
    }

    de_q_res = data_parser_q->de_q_timeout(tmp, timeout);
    if(de_q_res){
        pthread_join(parser_thread, &thread_res);
    }else{
        DBG_PRINTF("parser thread will terminate\n");
        pthread_kill(parser_thread, 0);
    }

    return upv_s::R_Success;
}

upv_s::upv_result upv_s::close()
{

    upv_s::upv_result r = stop_capture(1000);

    if(data_parser_q){
        delete data_parser_q;
        data_parser_q = NULL;
    }

    if(data_reader_q){
        delete data_reader_q;
        data_reader_q = NULL;
    }

    if(buf_data_q){
        delete buf_data_q;
        buf_data_q = NULL;
    }

    if(usb_dev != NULL){
        libusb_close(usb_dev);
        usb_dev = NULL;
    }
    if(usb_ctx != NULL){
        libusb_exit(usb_ctx);
        usb_ctx = NULL;
    }
    return r;
}

list<string> upv_s::list_devices()
{
    list<string> res;


    libusb_device** devs;
    libusb_context* ctx;
    int r;
    ssize_t cnt;

    libusb_device* dev;
    int i = 0;
    char tmpstr[256];

    r = libusb_init(&ctx);
    if (r < 0) {
      return res;
    }

    cnt = libusb_get_device_list(ctx, &devs);
    if (cnt < 0) {
      libusb_exit(ctx);
      return res;
    }

    while ((dev = devs[i++]) != NULL) {
      struct libusb_device_descriptor desc;
      int r = libusb_get_device_descriptor(dev, &desc);
      if (r < 0) {
        break;
      }
      if (desc.idVendor == UPV_VID && desc.idProduct == UPV_PID) {
        libusb_device_handle* hdev;
        if (libusb_open(dev, &hdev) >= 0) {
          if (libusb_get_string_descriptor_ascii(hdev, desc.iManufacturer, (unsigned char*)tmpstr, sizeof(tmpstr)) >= 0) {
            if (strncmp(tmpstr, UPV_MAN, sizeof(tmpstr)) == 0) {
              if (libusb_get_string_descriptor_ascii(hdev, desc.iSerialNumber, (unsigned char*)tmpstr, sizeof(tmpstr)) >= 0) {
                res.push_back(tmpstr);
              }
              else {
                res.push_back("XXX");
              }
            }
          }
          libusb_close(hdev);
        }
      }
    }

    libusb_free_device_list(devs, 1);
    libusb_exit(ctx);
    return res;
}
