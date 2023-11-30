#include "usbpv_s.h"
#include "string.h"
#include "pthread.h"
#include "signal.h"

#define UPV_RESET  0x73
#define UPV_START  0x74
#define UPV_STATUS 0x75

#define USB_WRITE_TIMEOUT 5000


#define upv_error_return(code, str) do {  \
        if(error_string)*error_string = str; \
        return code;                       \
   } while(0);

#define upv_error_return_free_device_list(code, str, devs) do {    \
        libusb_free_device_list(devs,1);   \
        if(error_string) *error_string = str;    \
        return code;                       \
   } while(0);

int upv_usb_open_serial(libusb_context *usb_ctx, libusb_device_handle **usb_dev, int vendor, int product,
                             const char* description, const char* serial, uint16_t* bcdUSB, const char** error_string)
{
    libusb_device *dev;
    libusb_device **devs;
    char string[256];
    int i = 0;

    if (usb_dev == NULL)
        upv_error_return(-11, "device context invalid");

    if (libusb_get_device_list(usb_ctx, &devs) < 0)
        upv_error_return(-12, "libusb_get_device_list() failed");

    while ((dev = devs[i++]) != NULL)
    {
        struct libusb_device_descriptor desc;
        int res;

        if (libusb_get_device_descriptor(dev, &desc) < 0)
            upv_error_return_free_device_list(-13, "libusb_get_device_descriptor() failed", devs);

        if (desc.idVendor == vendor && desc.idProduct == product)
        {
            if (libusb_open(dev, usb_dev) < 0) {
                //ftdi_error_return_free_device_list(-4, "usb_open() failed", devs);
                // maybe multiple devices, this one may open
                continue;
            }

            if (description != NULL && *description != 0)
            {
                if (libusb_get_string_descriptor_ascii(*usb_dev, desc.iManufacturer, (unsigned char *)string, sizeof(string)) < 0)
                {
                    libusb_close(*usb_dev);
                    *usb_dev = NULL;
                    upv_error_return_free_device_list(-8, "unable to fetch product description", devs);
                }
                if (strncmp(string, description, sizeof(string)) != 0)
                {
                    libusb_close(*usb_dev);
                    *usb_dev = NULL;
                    continue;
                }
            }
            if (serial != NULL && *serial != 0)
            {
                if (libusb_get_string_descriptor_ascii(*usb_dev, desc.iSerialNumber, (unsigned char *)string, sizeof(string)) < 0)
                {
                    libusb_close(*usb_dev);
                    *usb_dev = NULL;
                    upv_error_return_free_device_list(-9, "unable to fetch serial number", devs);
                }
                if (strncmp(string, serial, sizeof(string)) != 0)
                {
                    libusb_close(*usb_dev);
                    *usb_dev = NULL;
                    continue;
                }
            }

            {
                // here we got the target device
                libusb_free_device_list(devs,1);

                struct libusb_config_descriptor *config0;
                int cfg, cfg0, detach_errno = 0;

                if (libusb_get_config_descriptor(dev, 0, &config0) < 0)
                    upv_error_return(-10, "libusb_get_config_descriptor() failed");
                cfg0 = config0->bConfigurationValue;
                libusb_free_config_descriptor (config0);

                if (libusb_detach_kernel_driver(*usb_dev, 0) !=0)
                    detach_errno = errno;

                if (libusb_get_configuration(*usb_dev, &cfg) < 0)
                    upv_error_return(-12, "libusb_get_configuration () failed");
                // set configuration (needed especially for windows)
                // tolerate EBUSY: one device with one configuration, but two interfaces
                //    and libftdi sessions to both interfaces (e.g. FT2232)
                if (desc.bNumConfigurations > 0 && cfg != cfg0) {
                    if (libusb_set_configuration(*usb_dev, cfg0) < 0) {
                        libusb_close(*usb_dev);
                        *usb_dev = NULL;
                        if (detach_errno == EPERM) {
                            upv_error_return(-8, "inappropriate permissions on device!");
                        } else {
                            upv_error_return(-3,
                                          "unable to set usb configuration. Make sure the default driver is not in use");
                        }
                    }
                }

                if (libusb_claim_interface(*usb_dev, 0) < 0)
                {
                    libusb_close(*usb_dev);
                    *usb_dev = NULL;
                    if (detach_errno == EPERM)
                    {
                        upv_error_return(-8, "inappropriate permissions on device!");
                    }
                    else
                    {
                        upv_error_return(-5, "unable to claim usb device. Make sure the default driver is not in use");
                    }
                }

                if (libusb_control_transfer(*usb_dev, UPV_OUT_REQ,
                    UPV_RESET, 0,
                    0, NULL, 0, USB_WRITE_TIMEOUT) < 0){
                    libusb_close(*usb_dev);
                    *usb_dev = NULL;
                    upv_error_return(-1, "Device reset failed");
                }
                if(bcdUSB){
                    *bcdUSB = desc.bcdUSB;
                }
                return 0;
            }
            return res;
        }
    }

    // device not found
    upv_error_return_free_device_list(-3, "device not found", devs);
}


int upv_get_status(libusb_device_handle *usb_dev, const char** error_string)
{
    if (usb_dev == NULL)
        upv_error_return(-2, "USB device unavailable");
    uint16_t status;
    if (libusb_control_transfer(usb_dev, UPV_IN_REQ, UPV_STATUS, 0, 0, (unsigned char*)&status, 2, USB_WRITE_TIMEOUT) < 0)
        upv_error_return(-1, "unable to get device status. may not a upv?");
    return (int)status;
}

int upv_reset_device(libusb_device_handle *usb_dev, const char** error_string)
{
    if (usb_dev == NULL)
        upv_error_return(-2, "USB device unavailable");

    if (libusb_control_transfer(usb_dev, UPV_OUT_REQ, UPV_RESET, 0, 0, NULL, 0, USB_WRITE_TIMEOUT) < 0)
        upv_error_return(-1, "unable to reset device. may not a upv?");
    return 0;
}

int upv_start_device(libusb_device_handle *usb_dev, const char** error_string)
{
    if (usb_dev == NULL)
        upv_error_return(-2, "USB device unavailable");

    if (libusb_control_transfer(usb_dev, UPV_OUT_REQ, UPV_START, 0, 0, NULL, 0, USB_WRITE_TIMEOUT) < 0)
        upv_error_return(-1, "unable to start device. may not a upv?");
    return 0;
}


#define WIRTE_CHUNK_SIZE 4096
#define OUT_EP           0x01
#define IN_EP            0x81

int upv_write_data(libusb_device_handle *usb_dev, unsigned char *buf, int size, const char** error_string)
{
    int offset = 0;
    int actual_length;

    if (usb_dev == NULL)
        upv_error_return(-666, "USB device unavailable");

    while (offset < size)
    {
        int write_size = WIRTE_CHUNK_SIZE;

        if (offset+write_size > size)
            write_size = size-offset;

        if (libusb_bulk_transfer(usb_dev, OUT_EP, buf+offset, write_size, &actual_length, USB_WRITE_TIMEOUT) < 0)
            upv_error_return(-1, "usb bulk write failed");

        offset += actual_length;
    }

    return offset;
}


int upv_read_data(libusb_device_handle *usb_dev, unsigned char* buf, int size, const char** error_string)
{
    int actual_length;
    if (libusb_bulk_transfer(usb_dev, IN_EP, buf, size, &actual_length, USB_WRITE_TIMEOUT) < 0)
        upv_error_return(-1, "usb bulk read failed");
    return actual_length;
}

int upv_write_config_data(libusb_device_handle *usb_dev, uint8_t id, uint8_t val)
{
    uint8_t buf_in[4] = {0x55, id, val};
    uint8_t buf_out[4] = {0};
    buf_in[3] = (uint8_t)0x55+id+val;
    int r = upv_write_data(usb_dev, buf_in, 4, NULL);
    if(r < 0)return r;
    r = upv_read_data(usb_dev, buf_out, 4, NULL);
    if(r <= 0)return -1;
    r = memcmp(buf_in, buf_out, sizeof(buf_in));
    if(r != 0) return -2;
    return 0;
}
