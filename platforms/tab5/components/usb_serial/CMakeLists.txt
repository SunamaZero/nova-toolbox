idf_component_register(
    SRCS "usb_serial.cpp"
    INCLUDE_DIRS "include"
    REQUIRES usb espressif__usb_host_vcp
             espressif__usb_host_cdc_acm
             espressif__usb_host_ch34x_vcp
             espressif__usb_host_cp210x_vcp
             espressif__usb_host_ftdi_vcp
             freertos
             esp_common
)
