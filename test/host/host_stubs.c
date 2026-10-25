/* Link stubs: aux.c references the UART/TCP transports, tests never use them. */
#include "mbus-protocol-aux.h"
#include "mbus-serial.h"

int  mbus_serial_connect(mbus_handle *h)                       { (void)h; return -1; }
int  mbus_serial_disconnect(mbus_handle *h)                    { (void)h; return -1; }
int  mbus_serial_send_frame(mbus_handle *h, mbus_frame *f)     { (void)h; (void)f; return -1; }
int  mbus_serial_recv_frame(mbus_handle *h, mbus_frame *f)     { (void)h; (void)f; return MBUS_RECV_RESULT_ERROR; }
void mbus_serial_data_free(mbus_handle *h)                     { (void)h; }

#ifdef FS_STUB_TCP
#include "mbus-tcp.h"
int  mbus_tcp_connect(mbus_handle *h)                          { (void)h; return -1; }
int  mbus_tcp_disconnect(mbus_handle *h)                       { (void)h; return -1; }
int  mbus_tcp_send_frame(mbus_handle *h, mbus_frame *f)        { (void)h; (void)f; return -1; }
int  mbus_tcp_recv_frame(mbus_handle *h, mbus_frame *f)        { (void)h; (void)f; return MBUS_RECV_RESULT_ERROR; }
void mbus_tcp_data_free(mbus_handle *h)                        { (void)h; }
#endif
