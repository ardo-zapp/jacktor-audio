/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Universal USB-to-Serial KernelPatch Module (KPM) Driver
 * For Jacktor Audio Framework (Realme Bengal - Linux 4.19.152-perf+)
 * Exposes /dev/ttyUSB0..ttyUSB7 natively with multi-device & hotplug safety
 */

#include <compiler.h>
#include <kpmodule.h>
#include <linux/printk.h>
#include <common.h>
#include <kputils.h>
#include <symbol.h>
#include <linux/string.h>

KPM_NAME("usb-serial");
KPM_VERSION("1.0.0");
KPM_LICENSE("GPL v2");
KPM_AUTHOR("Ardo Zapp");
KPM_DESCRIPTION("USB Serial Driver (/dev/ttyUSBx)");

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long u64;

/*
 * Exact Linux 4.19 GFP allocation flags (include/linux/gfp.h):
 * ___GFP_HIGH           = 0x000020u
 * ___GFP_IO             = 0x000040u
 * ___GFP_FS             = 0x000080u
 * ___GFP_ATOMIC         = 0x080000u
 * ___GFP_KSWAPD_RECLAIM = 0x200000u
 * ___GFP_DIRECT_RECLAIM = 0x400000u
 * __GFP_RECLAIM         = 0x600000u
 * GFP_KERNEL            = __GFP_RECLAIM | __GFP_IO | __GFP_FS = 0x6000c0u
 * GFP_ATOMIC            = __GFP_HIGH | __GFP_ATOMIC | __GFP_KSWAPD_RECLAIM = 0x280020u
 */
#define GFP_KERNEL 0x6000c0u
#define GFP_ATOMIC 0x280020u

/* Ukuran antrean & buffer transfer data universal (usb-serial) */
#define MAX_USB_SERIAL_PORTS 8
#define RX_BUF_SIZE          512
#define TX_BUF_SIZE          4096
#define TX_RING_SIZE         16384
#define TX_RING_MASK         (TX_RING_SIZE - 1)

/*
 * ============================================================================
 * BAGIAN 1: KONSTANTA & DEFINISI SPESIFIK DRIVER SILICON LABS CP210X (cp210x)
 * ============================================================================
 * Seluruh nilai request vendor USB, nilai register UART, bit mask sinyal modem
 * (DTR/RTS), ukuran buffer kontrol vendor, nilai baud default, serta Vendor/Product
 * ID di bawah ini adalah spesifik untuk chip jembatan USB-ke-UART Silicon Labs
 * CP210x (CP2102/CP2104/CP2105/CP2108/CP2109).
 */
#define CP210X_VENDOR_ID                 0x10c4
#define CP210X_PID_EA60                  0xea60 /* CP2101 / CP2102 / CP2104 / CP2109 Single UART */
#define CP210X_PID_EA61                  0xea61 /* CP210x Custom Single UART */
#define CP210X_PID_EA63                  0xea63 /* CP210x Windows Update PID */
#define CP210X_PID_EA70                  0xea70 /* CP2105 Dual UART Bridge */
#define CP210X_PID_EA71                  0xea71 /* CP2108 Quad UART Bridge */

/* CP210x USB Vendor Request Type (Host-to-Interface) */
#define CP210X_REQTYPE_HOST_TO_INTERFACE 0x41

/* CP210x Configuration Request Codes (bRequest) */
#define CP210X_IFC_ENABLE                0x00
#define CP210X_SET_LINE_CTL              0x03
#define CP210X_GET_LINE_CTL              0x04
#define CP210X_SET_BREAK                 0x05
#define CP210X_SET_MHS                   0x07
#define CP210X_PURGE                     0x12
#define CP210X_SET_FLOW                  0x13
#define CP210X_GET_FLOW                  0x14
#define CP210X_GET_BAUDRATE              0x1d
#define CP210X_SET_BAUDRATE              0x1e

/* CP210x_IFC_ENABLE Values (wValue) */
#define CP210X_UART_ENABLE               0x0001
#define CP210X_UART_DISABLE              0x0000

/* CP210X_SET_LINE_CTL Values (wValue bitfields) */
#define CP210X_LINE_CTL_DATABITS_5       0x0500
#define CP210X_LINE_CTL_DATABITS_6       0x0600
#define CP210X_LINE_CTL_DATABITS_7       0x0700
#define CP210X_LINE_CTL_DATABITS_8       0x0800
#define CP210X_LINE_CTL_PARITY_NONE      0x0000
#define CP210X_LINE_CTL_PARITY_ODD       0x0010
#define CP210X_LINE_CTL_PARITY_EVEN      0x0020
#define CP210X_LINE_CTL_PARITY_MARK      0x0030
#define CP210X_LINE_CTL_PARITY_SPACE     0x0040
#define CP210X_LINE_CTL_STOP_1           0x0000
#define CP210X_LINE_CTL_STOP_1_5         0x0001
#define CP210X_LINE_CTL_STOP_2           0x0002
#define CP210X_LINE_CTL_8N1              (CP210X_LINE_CTL_DATABITS_8 | CP210X_LINE_CTL_PARITY_NONE | CP210X_LINE_CTL_STOP_1)

/* CP210X_SET_BREAK Values (wValue) */
#define CP210X_BREAK_ON                  0x0001
#define CP210X_BREAK_OFF                 0x0000

/* CP210X_PURGE Values (wValue) */
#define CP210X_PURGE_TX                  0x0001
#define CP210X_PURGE_RX                  0x0002
#define CP210X_PURGE_ALL                 0x000f

/* CP210X_SET_MHS Modem Handshake Control Bits (wValue) */
#define CP210X_CONTROL_DTR               0x0001
#define CP210X_CONTROL_RTS               0x0002
#define CP210X_CONTROL_WRITE_DTR         0x0100
#define CP210X_CONTROL_WRITE_RTS         0x0200

/* CP210x Control Transfer Buffer Size, Timeout & Baudrate Constants */
#define CP210X_CTRL_BUF_SIZE             64
#define CP210X_CTRL_TIMEOUT_MS           300
#define CP210X_DEFAULT_BAUD              115200
#define CP210X_FALLBACK_BAUD             115200

/*
 * ============================================================================
 * BAGIAN 2: KONSTANTA STANDAR TTY LINUX (Universal usb-serial)
 * ============================================================================
 */
#define TIOCM_DTR                 0x002
#define TIOCM_RTS                 0x004
#define TIOCM_CTS                 0x020
#define TIOCM_CAR                 0x040
#define TIOCM_DSR                 0x100

#define TIOCMGET                  0x5415
#define TIOCMBIS                  0x5416
#define TIOCMBIC                  0x5417
#define TIOCMSET                  0x5418

/* Linux 4.19 ktermios layout */
struct ktermios {
    u32 c_iflag;      /* input mode flags */
    u32 c_oflag;      /* output mode flags */
    u32 c_cflag;      /* control mode flags */
    u32 c_lflag;      /* local mode flags */
    u8  c_line;       /* line discipline */
    u8  c_cc[19];     /* control characters */
    u32 c_ispeed;     /* input speed (baud) */
    u32 c_ospeed;     /* output speed (baud) */
};

/* Kernel function pointers resolved via kallsyms */
static void *(*fn_tty_alloc_driver)(unsigned int lines, void *owner, unsigned long flags);
static int (*fn_tty_register_driver)(void *driver);
static int (*fn_tty_unregister_driver)(void *driver);
static void (*fn_put_tty_driver)(void *driver);
static void *sym_tty_std_termios;

static void (*fn_tty_port_init)(void *port);
static void (*fn_tty_port_destroy)(void *port);
static void (*fn_tty_port_link_device)(void *port, void *driver, unsigned int index);
static void *(*fn_tty_port_register_device)(void *port, void *driver, unsigned int index, void *device);
static void (*fn_tty_port_unregister_device)(void *port, void *driver, unsigned int index);
static int (*fn_tty_port_open)(void *port, void *tty, void *filp);
static void (*fn_tty_port_close)(void *port, void *tty, void *filp);
static void (*fn_tty_port_hangup)(void *port);
static int (*fn_tty_insert_flip_string_fixed_flag)(void *port, const unsigned char *chars, char flag, unsigned long size);
static void (*fn_tty_flip_buffer_push)(void *port);
static void (*fn_tty_wakeup)(void *tty);

static int (*fn_usb_register_driver)(void *new_driver, void *owner, const char *mod_name);
static void (*fn_usb_deregister)(void *driver);
static int (*fn_usb_find_common_endpoints)(void *alt, void **bulk_in, void **bulk_out, void **int_in, void **int_out);
static int (*fn_usb_control_msg)(void *dev, unsigned int pipe, u8 request, u8 requesttype, u16 value, u16 index, void *data, u16 size, int timeout);
static int (*fn_usb_bulk_msg)(void *usb_dev, unsigned int pipe, void *data, int len, int *actual_length, int timeout);
static void *(*fn_usb_alloc_urb)(int iso_packets, unsigned int mem_flags);
static void (*fn_usb_free_urb)(void *urb);
static void (*fn_usb_kill_urb)(void *urb);

static void *(*fn_kmalloc)(unsigned long size, unsigned int flags);
static void (*fn_kfree)(const void *ptr);
static unsigned long (*fn_copy_from_user)(void *to, const void *from, unsigned long n);
static unsigned long (*fn_copy_to_user)(void *to, const void *from, unsigned long n);

static void *(*fn_kthread_create_on_node)(int (*threadfn)(void *data), void *data, int node, const char namefmt[], ...);
static int (*fn_wake_up_process)(void *tsk);
static void (*fn_msleep)(unsigned int msecs);
static void (*fn_usleep_range)(unsigned long min, unsigned long max);

/* Per-device port state for universal /dev/ttyUSB0 .. /dev/ttyUSB7 (usb-serial) */
struct usb_serial_port_slot {
    u8 tty_port[512] __attribute__((aligned(8)));
    int index;
    volatile int in_use;
    volatile int dev_registered;
    volatile int connected;
    volatile int port_active;
    volatile int rx_running;
    volatile int tx_running;
    volatile int io_in_flight;
    void *udev;
    void *intf;
    void *rx_urb;
    void *tx_urb;
    u8 ifnum;
    u8 ep_in;
    u8 ep_out;
    u8 *rx_buf;          /* Buffer DMA Bulk-IN universal (usb-serial) */
    u8 *tx_buf;          /* Buffer DMA Bulk-OUT universal (usb-serial) */
    u8 *cp210x_ctrl_buf; /* Buffer DMA Control Transfer khusus vendor CP210x */
    u8 *tx_ring;         /* Ring buffer TX 16KB universal (usb-serial) */
    volatile u32 tx_head;
    volatile u32 tx_tail;
    int cp210x_dtr;      /* Status bit DTR aktif pada chip CP210x */
    int cp210x_rts;      /* Status bit RTS aktif pada chip CP210x */
    u32 cp210x_baud;     /* Nilai baudrate aktif pada chip CP210x */
};

/* Driver global state */
static void *g_tty_drv = 0;
static struct usb_serial_port_slot g_ports[MAX_USB_SERIAL_PORTS] __attribute__((aligned(8)));
static void *g_tty_ops[32] __attribute__((aligned(8)));
static void *g_port_ops[8] __attribute__((aligned(8)));
static int (*orig_client_receive_buf)(void *port, const unsigned char *p, const unsigned char *f, unsigned long count) = 0;
static u8 g_usb_drv[320] __attribute__((aligned(8)));

struct usb_dev_id_entry {
    u16 match_flags;
    u16 idVendor;
    u16 idProduct;
    u16 bcdDevice_lo;
    u16 bcdDevice_hi;
    u8 bDeviceClass;
    u8 bDeviceSubClass;
    u8 bDeviceProtocol;
    u8 bInterfaceClass;
    u8 bInterfaceSubClass;
    u8 bInterfaceProtocol;
    u8 bInterfaceNumber;
    u8 _pad[6];
    u64 driver_info;
};

/*
 * Tabel ID Perangkat USB Spesifik Silicon Labs CP210x (cp210x)
 * Digunakan oleh subsistem usb-serial untuk mencocokkan chip CP210x saat dicolokkan.
 */
static struct usb_dev_id_entry g_cp210x_id_table[6] __attribute__((aligned(8))) = {
    { .match_flags = 0x0003, .idVendor = CP210X_VENDOR_ID, .idProduct = CP210X_PID_EA60 }, /* CP2102 / CP2101 / CP2104 / CP2109 */
    { .match_flags = 0x0003, .idVendor = CP210X_VENDOR_ID, .idProduct = CP210X_PID_EA61 }, /* CP210x Single UART */
    { .match_flags = 0x0003, .idVendor = CP210X_VENDOR_ID, .idProduct = CP210X_PID_EA63 }, /* CP210x Single UART */
    { .match_flags = 0x0003, .idVendor = CP210X_VENDOR_ID, .idProduct = CP210X_PID_EA70 }, /* CP2105 Dual UART */
    { .match_flags = 0x0003, .idVendor = CP210X_VENDOR_ID, .idProduct = CP210X_PID_EA71 }, /* CP2108 Quad UART */
    { 0 }
};

/* Safe lookup helpers for multi-port mapping */
static struct usb_serial_port_slot *get_slot_from_port(void *port)
{
    int i;
    if (!port)
        return 0;
    for (i = 0; i < MAX_USB_SERIAL_PORTS; i++) {
        if ((void *)g_ports[i].tty_port == port)
            return &g_ports[i];
    }
    return 0;
}

static struct usb_serial_port_slot *get_slot_from_tty(void *tty)
{
    int i, idx;
    if (!tty)
        return 0;
    idx = *(int *)((char *)tty + 32); /* tty->index */
    if (idx >= 0 && idx < MAX_USB_SERIAL_PORTS)
        return &g_ports[idx];
    for (i = 0; i < MAX_USB_SERIAL_PORTS; i++) {
        if (*(void **)((char *)g_ports[i].tty_port + 168) == tty ||
            *(void **)((char *)g_ports[i].tty_port + 176) == tty)
            return &g_ports[i];
    }
    return 0;
}

static struct usb_serial_port_slot *get_slot_from_intf(void *intf)
{
    int i;
    if (!intf)
        return 0;
    for (i = 0; i < MAX_USB_SERIAL_PORTS; i++) {
        if (g_ports[i].in_use && g_ports[i].intf == intf)
            return &g_ports[i];
    }
    return 0;
}

/* Lifespan / In-Flight reference guard preventing buffer free during active IO */
static inline int slot_io_enter(struct usb_serial_port_slot *slot)
{
    if (!slot)
        return 0;
    __sync_fetch_and_add(&slot->io_in_flight, 1);
    __sync_synchronize();
    if (!slot->connected || !slot->udev || !slot->rx_buf || !slot->tx_buf ||
        !slot->cp210x_ctrl_buf || !slot->tx_ring) {
        __sync_fetch_and_sub(&slot->io_in_flight, 1);
        return 0;
    }
    return 1;
}

static inline void slot_io_exit(struct usb_serial_port_slot *slot)
{
    if (slot) {
        __sync_synchronize();
        __sync_fetch_and_sub(&slot->io_in_flight, 1);
    }
}

/* USB pipe helpers (Linux include/linux/usb.h) */
static inline unsigned int mk_sndctrlpipe(void *udev, unsigned int ep)
{
    int devnum = *(int *)udev;
    return (2U << 30) | (ep << 15) | (devnum << 8);
}

static inline unsigned int mk_sndbulkpipe(void *udev, unsigned int ep)
{
    int devnum = *(int *)udev;
    return (3U << 30) | ((ep & 0x0f) << 15) | (devnum << 8);
}

static inline unsigned int mk_rcvbulkpipe(void *udev, unsigned int ep)
{
    int devnum = *(int *)udev;
    return (3U << 30) | ((ep & 0x0f) << 15) | (devnum << 8) | 0x80;
}

/*
 * ============================================================================
 * BAGIAN 3: FUNGSI KONTROL HARDWARE SPESIFIK CHIP CP210X (cp210x_*)
 * ============================================================================
 * Mengirim paket USB Control Transfer (Vendor Request 0x41) ke endpoint 0 chip
 * Silicon Labs CP210x untuk mengaktifkan UART, mengatur format 8N1, baud rate,
 * serta status pin modem DTR/RTS.
 */
static int cp210x_ctrl_set(struct usb_serial_port_slot *slot, u8 request, u16 value, void *buf, u16 size)
{
    int ret;
    void *udev;
    if (!slot_io_enter(slot))
        return -19; /* -ENODEV */
    udev = slot->udev;
    ret = fn_usb_control_msg(udev, mk_sndctrlpipe(udev, 0),
                             request, CP210X_REQTYPE_HOST_TO_INTERFACE,
                             value, slot->ifnum, buf, size, CP210X_CTRL_TIMEOUT_MS);
    slot_io_exit(slot);
    return ret;
}

static int cp210x_uart_enable(struct usb_serial_port_slot *slot)
{
    return cp210x_ctrl_set(slot, CP210X_IFC_ENABLE, CP210X_UART_ENABLE, 0, 0);
}

static int cp210x_uart_disable(struct usb_serial_port_slot *slot)
{
    return cp210x_ctrl_set(slot, CP210X_IFC_ENABLE, CP210X_UART_DISABLE, 0, 0);
}

static int cp210x_set_line_ctl(struct usb_serial_port_slot *slot, u16 line_ctl)
{
    return cp210x_ctrl_set(slot, CP210X_SET_LINE_CTL, line_ctl, 0, 0);
}

/* Mengatur baud rate hardware pada chip CP210x menggunakan perintah CP210X_SET_BAUDRATE (0x1E) */
static int cp210x_set_baudrate(struct usb_serial_port_slot *slot, u32 cp210x_baud)
{
    int ret;
    void *udev;
    if (!slot_io_enter(slot))
        return -19;
    if (cp210x_baud == 0)
        cp210x_baud = CP210X_FALLBACK_BAUD;
    slot->cp210x_baud = cp210x_baud;
    /* Gunakan buffer DMA cp210x_ctrl_buf yang dialokasikan dengan GFP_KERNEL pada slot */
    *(u32 *)slot->cp210x_ctrl_buf = cp210x_baud;
    udev = slot->udev;
    ret = fn_usb_control_msg(udev, mk_sndctrlpipe(udev, 0),
                             CP210X_SET_BAUDRATE, CP210X_REQTYPE_HOST_TO_INTERFACE,
                             0, slot->ifnum, slot->cp210x_ctrl_buf, 4, CP210X_CTRL_TIMEOUT_MS);
    slot_io_exit(slot);
    return ret;
}

/* Mengatur garis Modem Handshake (DTR & RTS) pada chip CP210x menggunakan CP210X_SET_MHS (0x07) */
static int cp210x_set_mhs(struct usb_serial_port_slot *slot, int dtr, int rts)
{
    u16 cp210x_mhs_ctrl = 0;
    if (!slot)
        return -19;
    slot->cp210x_dtr = dtr ? 1 : 0;
    slot->cp210x_rts = rts ? 1 : 0;
    if (slot->cp210x_dtr)
        cp210x_mhs_ctrl |= CP210X_CONTROL_DTR;
    if (slot->cp210x_rts)
        cp210x_mhs_ctrl |= CP210X_CONTROL_RTS;
    cp210x_mhs_ctrl |= CP210X_CONTROL_WRITE_DTR | CP210X_CONTROL_WRITE_RTS;
    return cp210x_ctrl_set(slot, CP210X_SET_MHS, cp210x_mhs_ctrl, 0, 0);
}

/* Mengatur Hardware Flow Control (CRTSCTS) pada chip CP210x menggunakan CP210X_SET_FLOW (0x13) */
static int cp210x_set_flow_control(struct usb_serial_port_slot *slot, int crtscts)
{
    u32 flow_buf[4];
    int ret;
    void *udev;

    if (!slot_io_enter(slot))
        return -19;

    memset(flow_buf, 0, sizeof(flow_buf));
    if (crtscts) {
        flow_buf[0] = 0x00000008; /* CTS handshake enabled (CP210X_SERIAL_CTS_HANDSHAKE) */
        flow_buf[1] = 0x00000080; /* Hardware RTS auto-flow control (CP210X_SERIAL_RTS_FLOW_CTL) */
    } else {
        flow_buf[0] = 0x00000000; /* CTS handshake disabled */
        flow_buf[1] = 0x00000040; /* Manual/Static RTS control */
    }
    flow_buf[2] = 0; /* ulXonLimit */
    flow_buf[3] = 0; /* ulXoffLimit */

    memcpy(slot->cp210x_ctrl_buf, flow_buf, 16);
    udev = slot->udev;
    ret = fn_usb_control_msg(udev, mk_sndctrlpipe(udev, 0),
                             CP210X_SET_FLOW, CP210X_REQTYPE_HOST_TO_INTERFACE,
                             0, slot->ifnum, slot->cp210x_ctrl_buf, 16, CP210X_CTRL_TIMEOUT_MS);
    slot_io_exit(slot);
    return ret;
}

/* Mengatur Line Break State pada chip CP210x menggunakan CP210X_SET_BREAK (0x05) */
static int cp210x_break_ctl(struct usb_serial_port_slot *slot, int break_state)
{
    u16 val = break_state ? CP210X_BREAK_ON : CP210X_BREAK_OFF;
    return cp210x_ctrl_set(slot, CP210X_SET_BREAK, val, 0, 0);
}

/* Mengosongkan FIFO buffer hardware chip CP210x menggunakan CP210X_PURGE (0x12) */
static int cp210x_purge(struct usb_serial_port_slot *slot, u16 purge_flags)
{
    return cp210x_ctrl_set(slot, CP210X_PURGE, purge_flags, 0, 0);
}

/* Mengonversi nilai cflag termios POSIX Linux ke register format CP210X_SET_LINE_CTL */
static u16 cp210x_calc_line_ctl(u32 cflag)
{
    u16 line_ctl = 0;

    /* 1. Data bits (CSIZE: CS5=0x00, CS6=0x10, CS7=0x20, CS8=0x30) */
    switch (cflag & 0x00000030U) {
    case 0x00000000U: /* CS5 */
        line_ctl |= CP210X_LINE_CTL_DATABITS_5;
        break;
    case 0x00000010U: /* CS6 */
        line_ctl |= CP210X_LINE_CTL_DATABITS_6;
        break;
    case 0x00000020U: /* CS7 */
        line_ctl |= CP210X_LINE_CTL_DATABITS_7;
        break;
    case 0x00000030U: /* CS8 */
    default:
        line_ctl |= CP210X_LINE_CTL_DATABITS_8;
        break;
    }

    /* 2. Parity (PARENB=0x100, PARODD=0x200, CMSPAR=0x40000000) */
    if (cflag & 0x00000100U) { /* PARENB */
        if (cflag & 0x40000000U) { /* CMSPAR (mark/space parity) */
            if (cflag & 0x00000200U) /* PARODD */
                line_ctl |= CP210X_LINE_CTL_PARITY_MARK;
            else
                line_ctl |= CP210X_LINE_CTL_PARITY_SPACE;
        } else {
            if (cflag & 0x00000200U) /* PARODD */
                line_ctl |= CP210X_LINE_CTL_PARITY_ODD;
            else
                line_ctl |= CP210X_LINE_CTL_PARITY_EVEN;
        }
    } else {
        line_ctl |= CP210X_LINE_CTL_PARITY_NONE;
    }

    /* 3. Stop bits (CSTOPB=0x40) */
    if (cflag & 0x00000040U) { /* CSTOPB */
        if ((cflag & 0x00000030U) == 0x00000000U) /* CS5 */
            line_ctl |= CP210X_LINE_CTL_STOP_1_5;
        else
            line_ctl |= CP210X_LINE_CTL_STOP_2;
    } else {
        line_ctl |= CP210X_LINE_CTL_STOP_1;
    }

    return line_ctl;
}

/* Offset cache termios di dalam tty_struct untuk resolusi O(1) aman */
static int g_tty_termios_offset = -1;

/*
 * Menemukan lokasi struct ktermios di dalam tty_struct secara dinamis menggunakan
 * konstanta fingerprint unik tty_std_termios (c_line=0, VINTR=3, VQUIT=28, VERASE=127,
 * VKILL=21, VEOF=4) di offset +16 dalam batas rentang aman 0x100 s/d 0x2c0.
 */
static struct ktermios *find_tty_termios(void *tty)
{
    int off;
    if (!tty)
        return 0;

    /* Fast path: gunakan offset yang sudah terverifikasi */
    if (g_tty_termios_offset >= 0x100 && g_tty_termios_offset <= 0x2c0) {
        struct ktermios *k = (struct ktermios *)((char *)tty + g_tty_termios_offset);
        if (k->c_line == 0 && k->c_cc[0] == 3 && k->c_cc[1] == 28 && k->c_cc[2] == 127 && k->c_cc[3] == 21)
            return k;
    }

    /* Scan fingerprint unik pada tty_struct dari offset 0x100 s/d 0x2c0 */
    for (off = 0x100; off <= 0x2c0; off += 4) {
        struct ktermios *k = (struct ktermios *)((char *)tty + off);
        if (k->c_line == 0 && k->c_cc[0] == 3 && k->c_cc[1] == 28 && k->c_cc[2] == 127 && k->c_cc[3] == 21) {
            g_tty_termios_offset = off;
            pr_info("usb-serial: termios resolved at offset 0x%x (cflag=0x%08x)\n", off, k->c_cflag);
            return k;
        }
    }

    return 0;
}

/* Mendekode kecepatan baud secara fleksibel dari struct ktermios */
static u32 decode_baud(struct ktermios *kterm, u32 fallback)
{
    u32 baud = 0;

    if (!kterm)
        return fallback ? fallback : CP210X_DEFAULT_BAUD;

    /* 1. Jika c_ospeed valid (> 0), gunakan langsung (mencakup arbitrary baud rate) */
    if (kterm->c_ospeed > 0 && kterm->c_ospeed <= 3000000)
        baud = kterm->c_ospeed;

    /* 2. Decode manual dari bitmask CBAUD */
    if (baud == 0) {
        u32 cbaud = kterm->c_cflag & 0x100f;
        switch (cbaud) {
        case 0x0001: baud = 50; break;
        case 0x0002: baud = 75; break;
        case 0x0003: baud = 110; break;
        case 0x0004: baud = 134; break;
        case 0x0005: baud = 150; break;
        case 0x0006: baud = 200; break;
        case 0x0007: baud = 300; break;
        case 0x0008: baud = 600; break;
        case 0x0009: baud = 1200; break;
        case 0x000a: baud = 1800; break;
        case 0x000b: baud = 2400; break;
        case 0x000c: baud = 4800; break;
        case 0x000d: baud = 9600; break;
        case 0x000e: baud = 19200; break;
        case 0x000f: baud = 38400; break;
        case 0x1001: baud = 57600; break;
        case 0x1002: baud = 115200; break;
        case 0x1003: baud = 230400; break;
        case 0x1004: baud = 460800; break;
        case 0x1005: baud = 500000; break;
        case 0x1006: baud = 576000; break;
        case 0x1007: baud = 921600; break;
        case 0x1008: baud = 1000000; break;
        case 0x1009: baud = 1152000; break;
        case 0x100a: baud = 1500000; break;
        case 0x100b: baud = 2000000; break;
        case 0x100c: baud = 2500000; break;
        case 0x100d: baud = 3000000; break;
        }
    }

    if (baud == 0)
        baud = fallback ? fallback : CP210X_DEFAULT_BAUD;

    return baud;
}

/*
 * Menerapkan konfigurasi termios secara lengkap dan dinamis ke register hardware CP210x:
 * Baud rate, Data bits (5/6/7/8), Parity (None/Odd/Even/Mark/Space), Stop bits (1/1.5/2),
 * serta Hardware Flow Control (RTS/CTS).
 */
static void cp210x_apply_termios(struct usb_serial_port_slot *slot, void *tty)
{
    struct ktermios *kterm;
    u32 baud;
    u16 line_ctl;
    int crtscts;

    if (!slot || !slot->connected || !tty)
        return;

    kterm = find_tty_termios(tty);
    if (!kterm) {
        baud = slot->cp210x_baud ? slot->cp210x_baud : CP210X_DEFAULT_BAUD;
        cp210x_set_baudrate(slot, baud);
        cp210x_set_line_ctl(slot, CP210X_LINE_CTL_8N1);
        return;
    }

    /* 1. Atur Baud Rate secara dinamis */
    baud = decode_baud(kterm, slot->cp210x_baud);
    if (baud != slot->cp210x_baud || slot->cp210x_baud == 0) {
        cp210x_set_baudrate(slot, baud);
        pr_info("usb-serial: ttyUSB%d baudrate set to %u bps\n", slot->index, baud);
    }

    /* 2. Atur Line Control (Data Bits, Parity, Stop Bits) */
    line_ctl = cp210x_calc_line_ctl(kterm->c_cflag);
    cp210x_set_line_ctl(slot, line_ctl);

    /* 3. Atur Hardware Flow Control (CRTSCTS) */
    crtscts = (kterm->c_cflag & 0x80000000U) ? 1 : 0;
    cp210x_set_flow_control(slot, crtscts);
}

/*
 * ============================================================================
 * BAGIAN 4: ENGINE TRANSFER DATA & OPERASI TTY UNIVERSAL (usb-serial)
 * ============================================================================
 * Menangani thread Bulk-IN/Bulk-OUT, ring buffer TX 16KB, serta operasi TTY
 * standar Linux (/dev/ttyUSB0..ttyUSB7) yang memanggil fungsi hardware cp210x_*
 * ketika konfigurasi UART atau sinyal modem berubah.
 */

/* Dedicated per-port TX kthread batching ring-buffer writes into USB Bulk-OUT bursts */
static int usb_serial_tx_thread(void *data)
{
    struct usb_serial_port_slot *slot = (struct usb_serial_port_slot *)data;
    if (!slot)
        return 0;

    slot->tx_running = 1;
    __sync_synchronize();

    while (slot->connected && slot->port_active) {
        u32 head = slot->tx_head;
        u32 tail = slot->tx_tail;
        u32 avail = head - tail;
        if (avail > 0) {
            u32 chunk = avail > TX_BUF_SIZE ? TX_BUF_SIZE : avail;
            u32 i;
            int actual_len = 0;
            void *tty_ptr;

            if (!slot_io_enter(slot))
                break;

            for (i = 0; i < chunk; i++) {
                slot->tx_buf[i] = slot->tx_ring[(tail + i) & TX_RING_MASK];
            }
            __sync_synchronize();
            slot->tx_tail = tail + chunk;

            tty_ptr = *(void **)((char *)slot->tty_port + 168);
            if (fn_tty_wakeup && tty_ptr)
                fn_tty_wakeup(tty_ptr);

            fn_usb_bulk_msg(slot->udev, mk_sndbulkpipe(slot->udev, slot->ep_out),
                            slot->tx_buf, (int)chunk, &actual_len, 200);
            slot_io_exit(slot);
        } else {
            if (fn_usleep_range)
                fn_usleep_range(300, 700);
            else if (fn_msleep)
                fn_msleep(1);
        }
    }

    slot->tx_running = 0;
    __sync_synchronize();
    return 0;
}

/* Dedicated per-port RX kthread using DMA-safe rx_buf and official usb_bulk_msg */
static int usb_serial_rx_thread(void *data)
{
    struct usb_serial_port_slot *slot = (struct usb_serial_port_slot *)data;
    if (!slot)
        return 0;

    slot->rx_running = 1;
    __sync_synchronize();

    while (slot->connected && slot->port_active) {
        int actual_len = 0;
        int ret;

        if (!slot_io_enter(slot))
            break;

        ret = fn_usb_bulk_msg(slot->udev, mk_rcvbulkpipe(slot->udev, slot->ep_in),
                              slot->rx_buf, RX_BUF_SIZE, &actual_len, 50);
        if (ret == 0 && actual_len > 0 && slot->connected && slot->port_active) {
            if (orig_client_receive_buf) {
                orig_client_receive_buf(slot->tty_port, slot->rx_buf, 0, (unsigned long)actual_len);
            } else {
                fn_tty_insert_flip_string_fixed_flag(slot->tty_port, slot->rx_buf, 0, (unsigned long)actual_len);
                fn_tty_flip_buffer_push(slot->tty_port);
            }
        }
        slot_io_exit(slot);

        if (ret != 0 && ret != -110) { /* -110 is -ETIMEDOUT */
            if (!slot->connected || !slot->port_active)
                break;
            if (fn_msleep)
                fn_msleep(5);
        }
    }

    slot->rx_running = 0;
    __sync_synchronize();
    return 0;
}

/* tty_port_operations callbacks */
static int usb_serial_port_carrier_raised(void *port)
{
    struct usb_serial_port_slot *slot = get_slot_from_port(port);
    return (slot && slot->connected) ? 1 : 0;
}

static void usb_serial_port_dtr_rts(void *port, int on)
{
    struct usb_serial_port_slot *slot = get_slot_from_port(port);
    if (!slot || !slot->connected)
        return;
    /* Keep DTR and RTS in sync so ESP32 auto-reset circuit doesn't hold EN low on close */
    cp210x_set_mhs(slot, 0, 0);
}

static int usb_serial_port_activate(void *port, void *tty)
{
    struct usb_serial_port_slot *slot = get_slot_from_port(port);

    if (!slot || !slot->connected || !slot->udev)
        return -19;

    slot->tx_head = 0;
    slot->tx_tail = 0;
    __sync_synchronize();

    /* Inisialisasi hardware chip CP210x (Enable UART, konfig termios dinamis, DTR/RTS=0) */
    cp210x_uart_enable(slot);
    cp210x_apply_termios(slot, tty);
    cp210x_set_mhs(slot, 0, 0); /* CP210x DTR=0, RTS=0 -> ESP32 EN=HIGH (normal run state) */

    if (tty && !*(void **)((char *)slot->tty_port + 176))
        *(void **)((char *)slot->tty_port + 176) = tty;

    slot->port_active = 1;
    __sync_synchronize();

    if (!slot->rx_running && fn_kthread_create_on_node && fn_wake_up_process) {
        void *tsk = fn_kthread_create_on_node(usb_serial_rx_thread, slot, -1,
                                              "usb_serial_rx%d", slot->index);
        if ((unsigned long)tsk < (unsigned long)-4095UL)
            fn_wake_up_process(tsk);
    }
    if (!slot->tx_running && fn_kthread_create_on_node && fn_wake_up_process) {
        void *tsk = fn_kthread_create_on_node(usb_serial_tx_thread, slot, -1,
                                              "usb_serial_tx%d", slot->index);
        if ((unsigned long)tsk < (unsigned long)-4095UL)
            fn_wake_up_process(tsk);
    }

    return 0;
}

static void usb_serial_port_shutdown(void *port)
{
    int i;
    struct usb_serial_port_slot *slot = get_slot_from_port(port);
    if (!slot)
        return;

    /* Wait briefly for pending TX bytes to drain if device is still connected */
    for (i = 0; i < 20 && slot->connected && (slot->tx_head != slot->tx_tail); i++) {
        if (fn_msleep)
            fn_msleep(5);
    }

    slot->port_active = 0;
    __sync_synchronize();

    if (fn_usb_kill_urb) {
        if (slot->rx_urb)
            fn_usb_kill_urb(slot->rx_urb);
        if (slot->tx_urb)
            fn_usb_kill_urb(slot->tx_urb);
    }

    for (i = 0; i < 40 && (slot->rx_running || slot->tx_running); i++) {
        if (fn_msleep)
            fn_msleep(10);
    }

    /* Nonaktifkan antarmuka UART pada chip CP210x jika perangkat masih terhubung */
    if (slot->connected && slot->udev)
        cp210x_uart_disable(slot);
}

/* tty_operations callbacks */
static int usb_serial_tty_open(void *tty, void *filp)
{
    struct usb_serial_port_slot *slot = get_slot_from_tty(tty);
    if (!slot || !slot->connected)
        return -19;
    return fn_tty_port_open(slot->tty_port, tty, filp);
}

static void usb_serial_tty_close(void *tty, void *filp)
{
    struct usb_serial_port_slot *slot = get_slot_from_tty(tty);
    if (!slot)
        return;
    fn_tty_port_close(slot->tty_port, tty, filp);
}

static int usb_serial_tty_write(void *tty, const unsigned char *buf, int count)
{
    u32 head, tail, used, room;
    int i;
    struct usb_serial_port_slot *slot = get_slot_from_tty(tty);

    if (!slot_io_enter(slot))
        return -19;
    if (count <= 0) {
        slot_io_exit(slot);
        return 0;
    }

    head = slot->tx_head;
    tail = slot->tx_tail;
    used = head - tail;
    if (used >= TX_RING_SIZE) {
        slot_io_exit(slot);
        return 0;
    }
    room = TX_RING_SIZE - used;
    if (count > (int)room)
        count = (int)room;

    for (i = 0; i < count; i++) {
        slot->tx_ring[(head + (u32)i) & TX_RING_MASK] = buf[i];
    }
    __sync_synchronize();
    slot->tx_head = head + (u32)count;
    slot_io_exit(slot);
    return count;
}

static int usb_serial_tty_write_room(void *tty)
{
    u32 used;
    struct usb_serial_port_slot *slot = get_slot_from_tty(tty);
    if (!slot || !slot->connected || !slot->tx_ring)
        return 0;
    used = slot->tx_head - slot->tx_tail;
    if (used >= TX_RING_SIZE)
        return 0;
    return (int)(TX_RING_SIZE - used);
}

static int usb_serial_tty_chars_in_buffer(void *tty)
{
    struct usb_serial_port_slot *slot = get_slot_from_tty(tty);
    if (!slot || !slot->connected || !slot->tx_ring)
        return 0;
    return (int)(slot->tx_head - slot->tx_tail);
}

static void usb_serial_tty_set_termios(void *tty, void *old_termios)
{
    struct usb_serial_port_slot *slot = get_slot_from_tty(tty);
    if (!slot || !slot->connected || !tty)
        return;
    cp210x_apply_termios(slot, tty);
}

static int usb_serial_tty_break_ctl(void *tty, int state)
{
    struct usb_serial_port_slot *slot = get_slot_from_tty(tty);
    if (!slot || !slot->connected)
        return -19;
    return cp210x_break_ctl(slot, state);
}

static void usb_serial_tty_flush_buffer(void *tty)
{
    struct usb_serial_port_slot *slot = get_slot_from_tty(tty);
    if (!slot)
        return;
    slot->tx_tail = slot->tx_head;
    __sync_synchronize();
    if (slot->connected)
        cp210x_purge(slot, CP210X_PURGE_ALL);
}

static void usb_serial_tty_hangup(void *tty)
{
    struct usb_serial_port_slot *slot = get_slot_from_tty(tty);
    if (!slot)
        return;
    fn_tty_port_hangup(slot->tty_port);
}

static int usb_serial_tty_tiocmget(void *tty)
{
    int res = TIOCM_DSR | TIOCM_CTS | TIOCM_CAR;
    struct usb_serial_port_slot *slot = get_slot_from_tty(tty);
    if (!slot || !slot->connected)
        return -19;
    if (slot->cp210x_dtr)
        res |= TIOCM_DTR;
    if (slot->cp210x_rts)
        res |= TIOCM_RTS;
    return res;
}

static int usb_serial_tty_tiocmset(void *tty, unsigned int set, unsigned int clear)
{
    int dtr, rts;
    struct usb_serial_port_slot *slot = get_slot_from_tty(tty);
    if (!slot || !slot->connected)
        return -19;

    dtr = slot->cp210x_dtr;
    rts = slot->cp210x_rts;

    if (set & TIOCM_DTR)
        dtr = 1;
    if (set & TIOCM_RTS)
        rts = 1;
    if (clear & TIOCM_DTR)
        dtr = 0;
    if (clear & TIOCM_RTS)
        rts = 0;

    return cp210x_set_mhs(slot, dtr, rts);
}

static int usb_serial_tty_ioctl(void *tty, unsigned int cmd, unsigned long arg)
{
    int ret;
    unsigned int val = 0;
    if (!fn_copy_from_user || !fn_copy_to_user)
        return -515;
    switch (cmd) {
    case TIOCMGET:
        ret = usb_serial_tty_tiocmget(tty);
        if (ret < 0)
            return ret;
        val = (unsigned int)ret;
        if (fn_copy_to_user((void *)arg, &val, sizeof(val)))
            return -14;
        return 0;
    case TIOCMBIS:
        if (fn_copy_from_user(&val, (const void *)arg, sizeof(val)))
            return -14;
        return usb_serial_tty_tiocmset(tty, val, 0);
    case TIOCMBIC:
        if (fn_copy_from_user(&val, (const void *)arg, sizeof(val)))
            return -14;
        return usb_serial_tty_tiocmset(tty, 0, val);
    case TIOCMSET:
        if (fn_copy_from_user(&val, (const void *)arg, sizeof(val)))
            return -14;
        return usb_serial_tty_tiocmset(tty, val, ~val);
    default:
        return -515; /* -ENOIOCTLCMD */
    }
}

static void slot_free_resources(struct usb_serial_port_slot *slot)
{
    if (!slot)
        return;
    if (slot->rx_urb && fn_usb_free_urb) {
        void *u = slot->rx_urb;
        slot->rx_urb = 0;
        __sync_synchronize();
        fn_usb_free_urb(u);
    }
    if (slot->tx_urb && fn_usb_free_urb) {
        void *u = slot->tx_urb;
        slot->tx_urb = 0;
        __sync_synchronize();
        fn_usb_free_urb(u);
    }
    if (slot->rx_buf) {
        u8 *b = slot->rx_buf;
        slot->rx_buf = 0;
        __sync_synchronize();
        fn_kfree(b);
    }
    if (slot->tx_buf) {
        u8 *b = slot->tx_buf;
        slot->tx_buf = 0;
        __sync_synchronize();
        fn_kfree(b);
    }
    if (slot->cp210x_ctrl_buf) {
        u8 *b = slot->cp210x_ctrl_buf;
        slot->cp210x_ctrl_buf = 0;
        __sync_synchronize();
        fn_kfree(b);
    }
    if (slot->tx_ring) {
        u8 *b = slot->tx_ring;
        slot->tx_ring = 0;
        __sync_synchronize();
        fn_kfree(b);
    }
}

/* usb_driver probe & hotplug-safe disconnect */
static int usb_serial_usb_probe(void *intf, const void *id)
{
    void *alt;
    void *ep_in_desc = 0;
    void *ep_out_desc = 0;
    void *parent_dev;
    void *tty_dev;
    struct usb_serial_port_slot *slot = 0;
    int i, ret;

    alt = *(void **)((char *)intf + 8); /* intf->cur_altsetting */
    if (!alt)
        return -19;

    ret = fn_usb_find_common_endpoints(alt, &ep_in_desc, &ep_out_desc, 0, 0);
    if (ret < 0 || !ep_in_desc || !ep_out_desc) {
        pr_err("usb-serial: bulk endpoints not found (%d)\n", ret);
        return -19;
    }

    /* Claim the lowest available /dev/ttyUSB<index> slot atomically */
    for (i = 0; i < MAX_USB_SERIAL_PORTS; i++) {
        if (__sync_bool_compare_and_swap(&g_ports[i].in_use, 0, 1)) {
            slot = &g_ports[i];
            break;
        }
    }
    if (!slot) {
        pr_err("usb-serial: maximum %d USB-Serial devices reached\n", MAX_USB_SERIAL_PORTS);
        return -28; /* -ENOSPC */
    }

    slot->ep_in = *(u8 *)((char *)ep_in_desc + 2);
    slot->ep_out = *(u8 *)((char *)ep_out_desc + 2);
    slot->ifnum = *(u8 *)((char *)alt + 2);

    parent_dev = *(void **)((char *)intf + 48);
    slot->udev = (void *)((char *)parent_dev - 0xa0);
    slot->intf = intf;
    slot->cp210x_baud = CP210X_DEFAULT_BAUD;
    slot->cp210x_dtr = 0;
    slot->cp210x_rts = 0;
    slot->tx_head = 0;
    slot->tx_tail = 0;
    slot->io_in_flight = 0;

    pr_info("usb-serial: probe cp210x slot=%d devnum=%d ifnum=%d speed=%d\n",
            slot->index, *(int *)slot->udev, slot->ifnum, *(int *)((char *)slot->udev + 28));

    /* Allocate persistent DMA-safe kernel slab buffers with exact GFP_KERNEL (0x6000c0u) */
    if (!slot->rx_buf)
        slot->rx_buf = (u8 *)fn_kmalloc(RX_BUF_SIZE, GFP_KERNEL);
    if (!slot->tx_buf)
        slot->tx_buf = (u8 *)fn_kmalloc(TX_BUF_SIZE, GFP_KERNEL);
    if (!slot->cp210x_ctrl_buf)
        slot->cp210x_ctrl_buf = (u8 *)fn_kmalloc(CP210X_CTRL_BUF_SIZE, GFP_KERNEL);
    if (!slot->tx_ring)
        slot->tx_ring = (u8 *)fn_kmalloc(TX_RING_SIZE, GFP_KERNEL);
    if (fn_usb_alloc_urb) {
        if (!slot->rx_urb)
            slot->rx_urb = fn_usb_alloc_urb(0, GFP_KERNEL);
        if (!slot->tx_urb)
            slot->tx_urb = fn_usb_alloc_urb(0, GFP_KERNEL);
    }

    if (!slot->rx_buf || !slot->tx_buf || !slot->cp210x_ctrl_buf || !slot->tx_ring) {
        pr_err("usb-serial: GFP_KERNEL buffer allocation failed for ttyUSB%d\n", slot->index);
        slot_free_resources(slot);
        slot->udev = 0;
        slot->intf = 0;
        __sync_synchronize();
        slot->in_use = 0;
        return -12; /* -ENOMEM */
    }

    slot->connected = 1;
    __sync_synchronize();

    tty_dev = fn_tty_port_register_device(slot->tty_port, g_tty_drv,
                                          (unsigned int)slot->index, (char *)intf + 48);
    if ((unsigned long)tty_dev >= (unsigned long)-4095UL) {
        pr_err("usb-serial: tty_port_register_device failed for ttyUSB%d (%ld)\n",
               slot->index, (long)tty_dev);
        slot->connected = 0;
        __sync_synchronize();
        slot->udev = 0;
        slot->intf = 0;
        __sync_synchronize();
        slot->in_use = 0;
        return (int)(long)tty_dev;
    }

    slot->dev_registered = 1;
    pr_info("usb-serial: attached cp210x converter to /dev/ttyUSB%d (ep_in=0x%02x, ep_out=0x%02x)\n",
            slot->index, slot->ep_in, slot->ep_out);
    return 0;
}

static void usb_serial_usb_disconnect(void *intf)
{
    int i;
    struct usb_serial_port_slot *slot = get_slot_from_intf(intf);
    if (!slot)
        return;

    /* 1. Mark disconnected so no new IO or URB submissions can start */
    slot->connected = 0;
    slot->port_active = 0;
    __sync_synchronize();

    /* 2. Cancel any queued URBs via usb_kill_urb */
    if (fn_usb_kill_urb) {
        if (slot->rx_urb)
            fn_usb_kill_urb(slot->rx_urb);
        if (slot->tx_urb)
            fn_usb_kill_urb(slot->tx_urb);
    }

    /* 3. Wait for RX/TX kthreads and all in-flight IO references to finish */
    for (i = 0; i < 100 && (slot->rx_running || slot->tx_running || slot->io_in_flight > 0); i++) {
        if (fn_msleep)
            fn_msleep(10);
    }

    /* 4. Hang up active TTY session safely via tty_port_hangup */
    fn_tty_port_hangup(slot->tty_port);

    /* 5. Unregister /dev/ttyUSB<index> with exact 3-arg signature (port, driver, index) */
    if (slot->dev_registered) {
        fn_tty_port_unregister_device(slot->tty_port, g_tty_drv, (unsigned int)slot->index);
        slot->dev_registered = 0;
    }

    /*
     * 6. Reset ring indices while keeping DMA slab buffers pinned for slot lifetime
     * so delayed userspace close/hangup callbacks can never hit a freed buffer.
     */
    slot->tx_head = 0;
    slot->tx_tail = 0;
    slot->udev = 0;
    slot->intf = 0;
    __sync_synchronize();
    slot->in_use = 0;
    pr_info("usb-serial: /dev/ttyUSB%d disconnected safely\n", slot->index);
}

#define RESOLVE_SYM(var, name) do { \
    var = (typeof(var))kallsyms_lookup_name(name); \
    if (!var) { \
        pr_err("usb-serial: symbol not found: " name "\n"); \
        return -1; \
    } \
} while (0)

static long usb_serial_init(const char *args, const char *event, void *__reserved)
{
    int i, ret;

    pr_info("usb-serial: initializing driver v1.0.0 (max %d ports)...\n", MAX_USB_SERIAL_PORTS);

    RESOLVE_SYM(fn_tty_alloc_driver, "__tty_alloc_driver");
    RESOLVE_SYM(fn_tty_register_driver, "tty_register_driver");
    RESOLVE_SYM(fn_tty_unregister_driver, "tty_unregister_driver");
    RESOLVE_SYM(fn_put_tty_driver, "put_tty_driver");
    RESOLVE_SYM(sym_tty_std_termios, "tty_std_termios");

    RESOLVE_SYM(fn_tty_port_init, "tty_port_init");
    RESOLVE_SYM(fn_tty_port_destroy, "tty_port_destroy");
    RESOLVE_SYM(fn_tty_port_link_device, "tty_port_link_device");
    RESOLVE_SYM(fn_tty_port_register_device, "tty_port_register_device");
    RESOLVE_SYM(fn_tty_port_unregister_device, "tty_port_unregister_device");
    RESOLVE_SYM(fn_tty_port_open, "tty_port_open");
    RESOLVE_SYM(fn_tty_port_close, "tty_port_close");
    RESOLVE_SYM(fn_tty_port_hangup, "tty_port_hangup");
    RESOLVE_SYM(fn_tty_insert_flip_string_fixed_flag, "tty_insert_flip_string_fixed_flag");
    RESOLVE_SYM(fn_tty_flip_buffer_push, "tty_flip_buffer_push");
    fn_tty_wakeup = (typeof(fn_tty_wakeup))kallsyms_lookup_name("tty_wakeup");

    RESOLVE_SYM(fn_usb_register_driver, "usb_register_driver");
    RESOLVE_SYM(fn_usb_deregister, "usb_deregister");
    RESOLVE_SYM(fn_usb_find_common_endpoints, "usb_find_common_endpoints");
    RESOLVE_SYM(fn_usb_control_msg, "usb_control_msg");
    RESOLVE_SYM(fn_usb_bulk_msg, "usb_bulk_msg");
    fn_usb_alloc_urb = (typeof(fn_usb_alloc_urb))kallsyms_lookup_name("usb_alloc_urb");
    fn_usb_free_urb = (typeof(fn_usb_free_urb))kallsyms_lookup_name("usb_free_urb");
    fn_usb_kill_urb = (typeof(fn_usb_kill_urb))kallsyms_lookup_name("usb_kill_urb");

    RESOLVE_SYM(fn_kmalloc, "__kmalloc");
    RESOLVE_SYM(fn_kfree, "kfree");

    fn_copy_from_user = (typeof(fn_copy_from_user))kallsyms_lookup_name("__arch_copy_from_user");
    fn_copy_to_user = (typeof(fn_copy_to_user))kallsyms_lookup_name("__arch_copy_to_user");
    fn_kthread_create_on_node = (typeof(fn_kthread_create_on_node))kallsyms_lookup_name("kthread_create_on_node");
    fn_wake_up_process = (typeof(fn_wake_up_process))kallsyms_lookup_name("wake_up_process");
    fn_msleep = (typeof(fn_msleep))kallsyms_lookup_name("msleep");
    fn_usleep_range = (typeof(fn_usleep_range))kallsyms_lookup_name("usleep_range");

    /* 1. Initialize tty_port_operations table */
    memset(g_port_ops, 0, sizeof(g_port_ops));
    g_port_ops[0] = usb_serial_port_carrier_raised;
    g_port_ops[1] = usb_serial_port_dtr_rts;
    g_port_ops[2] = usb_serial_port_shutdown;
    g_port_ops[3] = usb_serial_port_activate;

    /* 2. Allocate tty_driver (MAX_USB_SERIAL_PORTS lines, TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV = 0x09) */
    g_tty_drv = fn_tty_alloc_driver(MAX_USB_SERIAL_PORTS, 0, 0x09);
    if ((unsigned long)g_tty_drv >= (unsigned long)-4095UL) {
        pr_err("usb-serial: __tty_alloc_driver failed\n");
        return -12;
    }

    *(const char **)((char *)g_tty_drv + 24) = "usb-serial";
    *(const char **)((char *)g_tty_drv + 32) = "ttyUSB";
    *(int *)((char *)g_tty_drv + 44) = 188; /* TTY_USB_MAJOR */
    *(int *)((char *)g_tty_drv + 48) = 0;   /* minor_start */
    *(short *)((char *)g_tty_drv + 56) = 3; /* TTY_DRIVER_TYPE_SERIAL */
    *(short *)((char *)g_tty_drv + 58) = 1; /* SERIAL_TYPE_NORMAL */

    memcpy((char *)g_tty_drv + 60, sym_tty_std_termios, 44);
    *(u32 *)((char *)g_tty_drv + 60) = 0;      /* c_iflag = 0 (raw) */
    *(u32 *)((char *)g_tty_drv + 64) = 0;      /* c_oflag = 0 (raw) */
    *(u32 *)((char *)g_tty_drv + 68) = 0x1cb2; /* B115200 | CS8 | CREAD | HUPCL | CLOCAL (Default 115200 bps) */
    *(u32 *)((char *)g_tty_drv + 72) = 0;      /* c_lflag = 0 (raw) */
    *(u32 *)((char *)g_tty_drv + 96) = 115200; /* c_ispeed */
    *(u32 *)((char *)g_tty_drv + 100) = 115200; /* c_ospeed */

    memset(g_tty_ops, 0, sizeof(g_tty_ops));
    g_tty_ops[3]  = usb_serial_tty_open;
    g_tty_ops[4]  = usb_serial_tty_close;
    g_tty_ops[7]  = usb_serial_tty_write;
    g_tty_ops[10] = usb_serial_tty_write_room;
    g_tty_ops[11] = usb_serial_tty_chars_in_buffer;
    g_tty_ops[12] = usb_serial_tty_ioctl;
    g_tty_ops[14] = usb_serial_tty_set_termios;
    g_tty_ops[19] = usb_serial_tty_hangup;
    g_tty_ops[20] = usb_serial_tty_break_ctl;
    g_tty_ops[21] = usb_serial_tty_flush_buffer;
    g_tty_ops[25] = usb_serial_tty_tiocmget;
    g_tty_ops[26] = usb_serial_tty_tiocmset;
    *(void **)((char *)g_tty_drv + 160) = g_tty_ops;

    /* 3. Initialize all tty_port slots and link to tty_driver */
    memset(g_ports, 0, sizeof(g_ports));
    for (i = 0; i < MAX_USB_SERIAL_PORTS; i++) {
        g_ports[i].index = i;
        g_ports[i].cp210x_baud = CP210X_DEFAULT_BAUD;
        fn_tty_port_init(g_ports[i].tty_port);
        if (i == 0) {
            void **cops = *(void ***)((char *)g_ports[i].tty_port + 192);
            if (cops && cops[0])
                orig_client_receive_buf = (typeof(orig_client_receive_buf))cops[0];
        }
        *(void **)((char *)g_ports[i].tty_port + 184) = g_port_ops;
        fn_tty_port_link_device(g_ports[i].tty_port, g_tty_drv, (unsigned int)i);
    }

    ret = fn_tty_register_driver(g_tty_drv);
    if (ret < 0) {
        pr_err("usb-serial: tty_register_driver failed (%d)\n", ret);
        for (i = 0; i < MAX_USB_SERIAL_PORTS; i++)
            fn_tty_port_destroy(g_ports[i].tty_port);
        fn_put_tty_driver(g_tty_drv);
        g_tty_drv = 0;
        return ret;
    }

    /* 4. Register usb_driver (usb-serial framework dengan tabel ID perangkat cp210x) */
    memset(g_usb_drv, 0, sizeof(g_usb_drv));
    *(const char **)((char *)g_usb_drv + 0) = "usb-serial";
    *(void **)((char *)g_usb_drv + 8) = usb_serial_usb_probe;
    *(void **)((char *)g_usb_drv + 16) = usb_serial_usb_disconnect;
    *(void **)((char *)g_usb_drv + 72) = g_cp210x_id_table;
    *((u8 *)g_usb_drv + 280) = 1; /* no_dynamic_id = 1 */

    ret = fn_usb_register_driver(g_usb_drv, 0, "usb-serial");
    if (ret < 0) {
        pr_err("usb-serial: usb_register_driver failed (%d)\n", ret);
        fn_tty_unregister_driver(g_tty_drv);
        for (i = 0; i < MAX_USB_SERIAL_PORTS; i++)
            fn_tty_port_destroy(g_ports[i].tty_port);
        fn_put_tty_driver(g_tty_drv);
        g_tty_drv = 0;
        return ret;
    }

    pr_info("usb-serial: USB & TTY driver v1.0.0 registered (/dev/ttyUSB0..ttyUSB%d)\n",
            MAX_USB_SERIAL_PORTS - 1);
    return 0;
}

static long usb_serial_exit(void *__reserved)
{
    int i;
    pr_info("usb-serial: unloading driver v1.0.0...\n");
    for (i = 0; i < MAX_USB_SERIAL_PORTS; i++) {
        g_ports[i].port_active = 0;
        g_ports[i].connected = 0;
    }
    __sync_synchronize();
    fn_usb_deregister(g_usb_drv);
    if (g_tty_drv) {
        fn_tty_unregister_driver(g_tty_drv);
        fn_put_tty_driver(g_tty_drv);
        g_tty_drv = 0;
    }
    for (i = 0; i < MAX_USB_SERIAL_PORTS; i++) {
        slot_free_resources(&g_ports[i]);
        fn_tty_port_destroy(g_ports[i].tty_port);
    }
    return 0;
}

KPM_INIT(usb_serial_init);
KPM_EXIT(usb_serial_exit);
