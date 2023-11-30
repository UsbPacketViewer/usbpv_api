#ifndef __USBPV_S_H__
#define __USBPV_S_H__

#include "libusb.h"
#include "semaphore.h"
#include <list>
#include <string>
#include "string.h"

#ifdef _WIN32
#define UPV_CALL __cdecl
#define UPV_CB   __cdecl

#ifdef USBPV_LIB
#define UPV_API  __declspec(dllexport)
#else
#define UPV_API  __declspec(dllimport)
#endif

#else
#define UPV_CALL
#define UPV_CB

#define UPV_API  __attribute__((visibility("default")))
#endif


#define UPV_OUT_REQ  (LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE | LIBUSB_ENDPOINT_OUT)
#define UPV_IN_REQ   (LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE | LIBUSB_ENDPOINT_IN)

using std::list;
using std::string;

enum CaptureSpeed {
  CS_HighSpeed = 0,
  CS_FullSpeed = 1,
  CS_LowSpeed = 2,
  CS_AutoSpeed = 3
};

#define UPV_FLAG_ACK      (0x01)
#define UPV_FLAG_ISO      (0x02)
#define UPV_FLAG_NAK      (0x04)
#define UPV_FLAG_STALL    (0x08)

#define UPV_FLAG_SOF      (0x10)
#define UPV_FLAG_PING     (0x20)
#define UPV_FLAG_INCOMP   (0x40)
#define UPV_FLAG_ERROR    (0x80)

#define UPV_FLAG_ALL      (0xff)



template<int SIZE, int COUNT>
class mem_pool_t {
public:
    mem_pool_t() {
        memset(mem, 0, sizeof(mem));
        init();
    }
    ~mem_pool_t() {
        deinit();
    }

    inline void lock() {
        pthread_mutex_lock(&mutex);
    }
    inline void unlock() {
        pthread_mutex_unlock(&mutex);
    }

    int init() {
        sem_init(&sem, 0, COUNT);
        pthread_mutex_init(&mutex, NULL);
        for (int i = 0; i < COUNT; i++) {
            mem[i] = new uint8_t[SIZE];
        }
        rd_idx = 0;
        wr_idx = 0;
        remain = COUNT;
        return 0;
    }
    int deinit() {
        for (int i = 0; i < COUNT; i++) {
            delete mem[i];
        }
        pthread_mutex_destroy(&mutex);
        sem_destroy(&sem);
        return 0;
    }

    uint8_t* get() {
        int result = sem_wait(&sem);
        if(result == 0){
            lock();
            uint8_t* res = mem[rd_idx];
            rd_idx = (rd_idx + 1) % COUNT;
            remain--;
            unlock();
            return res;
        }
        return NULL;
    }
    void put(uint8_t* p) {
        lock();
        if(mem[wr_idx] != p){
            printf("wrong pointer position\n");
        }
        wr_idx = (wr_idx + 1) % COUNT;
        remain++;
        unlock();
        sem_post(&sem);
    }
    sem_t  sem;
    pthread_mutex_t mutex;

    int remain;
    uint8_t* mem[COUNT];
    int rd_idx;
    int wr_idx;
    enum {
        size = SIZE,
    };
};


struct buf_data_t{
    unsigned char* buffer;
    int len;
};
template<typename T>
struct upv_queue{
    upv_queue(){
        sem_init(&sem, 0, 0);
    }
    ~upv_queue(){
        sem_destroy(&sem);
    }
    void en_q(const T& v){
        data.push_back(v);
        sem_post(&sem);
    }
    bool de_q(T& v){
        int r = sem_wait(&sem);
        if(r == 0){
            v = data.front();
            data.pop_front();
            return true;
        }
        return false;
    }
    bool de_q_timeout(T& v, int ms){
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        int ns = ts.tv_nsec + ms*1000000;
        ts.tv_sec += ns/1000000000;
        ts.tv_nsec = ns%1000000000;
        do{
          int r = sem_timedwait(&sem, &ts);
          if(r<0){
              if(errno != EINTR){
                  return false;
              }
          }else if(r == 0){
              v = data.front();
              data.pop_front();
              return true;
          }else{
              return false;
          }
        }while(true);
        return false;
    }
    list<T> data;
    sem_t  sem;
};

typedef long(UPV_CB* pfnt_on_packet)(void* context, unsigned long tick_60MHz, const void* data, unsigned long len, long status);

class upv_s
{
public:

    enum upv_result {
      R_Success = 0,
      R_DeviceNotFound = -1,
      R_DeviceNotOpen = -2,
      R_DeviceStatus = -3,
      R_Load = -4,
      R_WriteConfig = -5,
      R_EEInit = -6,
      R_Thread = -12,
    };

    upv_s();
    ~upv_s();
    upv_result open(const char* option, int opt_len);
    upv_result close();
    upv_result start_capture(void* context, pfnt_on_packet callback);
    upv_result stop_capture(int timeout);
    static list<string> list_devices();

    int process_data(const uint8_t* data, int len);
    void* reader_thread_func();
    void* parser_thread_func();

public:
    struct libusb_context *usb_ctx;
    struct libusb_device_handle *usb_dev;
    const char* last_error_string;
    mem_pool_t<1024*1024*8, 32> mem_pool;  // 8M*32
    upv_queue<buf_data_t>* buf_data_q;
    upv_queue<int>* data_reader_q;
    upv_queue<int>* data_parser_q;
    pthread_t reader_thread;
    pthread_t parser_thread;
    void* capture_context;
    pfnt_on_packet packet_handler;
    int capture_finish;
    int data_state;
    uint32_t last_header;
    uint32_t data_buf[1024+16]; // USB max packet size <= 4096bytes
    int32_t data_buf_idx;
    int32_t pkt_len;
    int32_t pkt_status;
    int32_t pkt_tick;
    uint16_t bcdUSB;
};

#endif
