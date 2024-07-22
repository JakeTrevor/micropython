#include <stdio.h>

#include "extmod/modmachine.h"
#include "py/obj.h"
#include "py/runtime.h"

#define STRINGIFY(x) #x
#define EX_STRINGIFY(x) STRINGIFY(x)

#define SPI_MAX_BAUDRATE (25000000)
#define SPI_MIN_BAUDRATE (SPI_MAX_BAUDRATE >> 16)
#define DEFAULT_SPI_BAUDRATE (1000000)
#define DEFAULT_SPI_POLARITY (0)
#define DEFAULT_SPI_PHASE (0)
#define DEFAULT_SPI_BITS (8)
#define DEFAULT_SPI_FIRSTBIT (SPI_MSB_FIRST)

typedef struct _spi_block_t {
    uint32_t interruptState;
    uint32_t interruptEnable;
    uint32_t interruptTest;
    uint32_t configuration;
    uint32_t control;
    uint32_t status;
    uint32_t start;
    uint32_t receiveFifo;
    uint32_t transmitFifo;
} spi_block_t;

/// Configuration Register Fields
enum {
    ConfigurationHalfClockPeriodMask = 0xffffu << 0,
    ConfigurationMSBFirst = 1u << 29,
    ConfigurationClockPhase = 1u << 30,
    ConfigurationClockPolarity = 1u << 31,
};

/// Control Register Fields
enum {
    ControlTransmitClear = 1 << 0,
    ControlReceiveClear = 1 << 1,
    ControlTransmitEnable = 1 << 2,
    ControlReceiveEnable = 1 << 3,
    ControlTransmitWatermarkMask = 0xf << 4,
    ControlReceiveWatermarkMask = 0xf << 8,
};

/// Status Register Fields
enum {
    StatusTxFifoLevel = 0xffu << 0,
    StatusRxFifoLevel = 0xffu << 8,
    StatusTxFifoFull = 1u << 16,
    StatusRxFifoEmpty = 1u << 17,
    StatusIdle = 1u << 18,
};

/// Start Register Fields
enum {
    StartByteCountMask = 0x7ffu,
};

void configure_spi_block(volatile spi_block_t *spi, const bool ClockPolarity,
    const bool ClockPhase, const bool MsbFirst,
    const uint16_t HalfClockPeriod) {
    spi->configuration = (ClockPolarity ? ConfigurationClockPolarity : 0) |
        (ClockPhase ? ConfigurationClockPhase : 0) |
        (MsbFirst ? ConfigurationMSBFirst : 0) |
        (HalfClockPeriod & ConfigurationHalfClockPeriodMask);
}

/// Waits for the SPI device to become idle
static void wait_idle(volatile spi_block_t *spi) {
    // Wait whilst IDLE field in STATUS is low
    while ((spi->status & StatusIdle) == 0) {
    }
}

uint16_t convert_baudrate(uint32_t bdrt) {
    if (bdrt < SPI_MIN_BAUDRATE || bdrt > SPI_MAX_BAUDRATE) {
        mp_raise_ValueError(MP_ERROR_TEXT("Baudrate out of bounds (" EX_STRINGIFY(
            SPI_MIN_BAUDRATE) ", " EX_STRINGIFY(SPI_MAX_BAUDRATE) ")"));
    }

    return (uint16_t)((SPI_MAX_BAUDRATE / bdrt) - 1);
}

typedef struct _machine_spi_obj_t {
    mp_obj_base_t base;
    volatile spi_block_t *spi_block;
} machine_spi_obj_t;

static void machine_spi_print(const mp_print_t *print, mp_obj_t self_in,
    mp_print_kind_t kind) {
    machine_spi_obj_t *self = MP_OBJ_TO_PTR(self_in);
    const char *name;
    switch ((uint32_t)self->spi_block) {
        case 2150629376:
            name = "Flash";
            break;
        case 2150633472:
            name = "LCD";
            break;
        case 2150637568:
            name = "Ethernet";
            break;
        default:
            name = "Unknown SPI device";
            break;
    }
    mp_printf(print, "SPI(%s)", name);
}

volatile spi_block_t *get_spi_block(uint32_t spi_id) {
    switch (spi_id) {
        case 0:
            return MMIO_CAPABILITY(spi_block_t, spi0);
        case 1:
            return MMIO_CAPABILITY(spi_block_t, spi1);
        case 2:
            return MMIO_CAPABILITY(spi_block_t, spi2);
        #if 0
        case 3:
            return MMIO_CAPABILITY(spi_block_t, spi3);
        case 4:
            return MMIO_CAPABILITY(spi_block_t, spi4);
        #endif
        default:
            mp_raise_msg_varg(&mp_type_ValueError,
                MP_ERROR_TEXT("Unknown spi device \"%d\""), spi_id);
    }
}

mp_obj_t machine_spi_make_new(const mp_obj_type_t *type, size_t n_args,
    size_t n_kw, const mp_obj_t *all_args) {
    enum {
        ARG_id,
        ARG_baudrate,
        ARG_polarity,
        ARG_phase,
        ARG_firstbit,
        ARG_sck,
        ARG_mosi,
        ARG_miso
    };

    static const mp_arg_t allowed_args[] = {
        // TODO this one is probably wrong...
        {MP_QSTR_id, MP_ARG_REQUIRED | MP_ARG_OBJ},
        {MP_QSTR_baudrate, MP_ARG_INT, {.u_int = DEFAULT_SPI_BAUDRATE}},
        {MP_QSTR_polarity,
         MP_ARG_KW_ONLY | MP_ARG_INT,
         {.u_int = DEFAULT_SPI_POLARITY}},
        {MP_QSTR_phase,
         MP_ARG_KW_ONLY | MP_ARG_INT,
         {.u_int = DEFAULT_SPI_PHASE}},
        {MP_QSTR_firstbit,
         MP_ARG_KW_ONLY | MP_ARG_INT,
         {.u_int = DEFAULT_SPI_FIRSTBIT}},
        {MP_QSTR_sck, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_mosi, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_miso, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
    };

    // Parse the arguments.
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args),
        allowed_args, args);

    if ((args[ARG_sck].u_obj != MP_OBJ_NULL) ||
        (args[ARG_miso].u_obj != MP_OBJ_NULL) ||
        (args[ARG_mosi].u_obj != MP_OBJ_NULL)) {
        mp_raise_NotImplementedError(
            MP_ERROR_TEXT("explicit choice of sck/miso/mosi is not implemented"));
    }

    uint32_t spi_id = mp_obj_get_int(args[ARG_id].u_obj);
    volatile spi_block_t *block = get_spi_block(spi_id);

    uint32_t baudrate = args[ARG_baudrate].u_int;
    uint8_t polarity = args[ARG_polarity].u_int;
    uint8_t phase = args[ARG_phase].u_int;
    uint8_t firstbit = args[ARG_firstbit].u_int;

    configure_spi_block(block, polarity, phase, firstbit,
        convert_baudrate(baudrate));

    machine_spi_obj_t *self = m_new_obj(machine_spi_obj_t);
    self->spi_block = block;
    self->base.type = &machine_spi_type;

    return MP_OBJ_FROM_PTR(self);
}

static void machine_spi_init(mp_obj_base_t *self_in, size_t n_args,
    const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_baudrate, ARG_polarity, ARG_phase, ARG_firstbit };

    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_baudrate, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1}},
        {MP_QSTR_polarity, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1}},
        {MP_QSTR_phase, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1}},
        {MP_QSTR_firstbit, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1}},
    };

    machine_spi_obj_t *self = MP_OBJ_TO_PTR(self_in);

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args),
        allowed_args, args);

    uint32_t baudrate = args[ARG_baudrate].u_int;
    uint8_t polarity = args[ARG_polarity].u_int;
    uint8_t phase = args[ARG_phase].u_int;
    uint8_t firstbit = args[ARG_firstbit].u_int;

    uint32_t config = self->spi_block->configuration;

    if (baudrate > 0) {
        config &= ~ConfigurationHalfClockPeriodMask;
        config |= convert_baudrate(baudrate);
    }

    if (polarity >= 0) {
        config &= ~ConfigurationClockPolarity;
        config |= (polarity ? ConfigurationClockPolarity : 0);
    }

    if (phase >= 0) {
        config &= ~ConfigurationClockPhase;
        config |= (phase ? ConfigurationClockPhase : 0);
    }

    if (firstbit >= 0) {
        config &= ~ConfigurationMSBFirst;
        config |= (firstbit ? ConfigurationMSBFirst : 0);
    }

    self->spi_block->configuration = config;
}

static void machine_spi_transfer(mp_obj_base_t *obj, size_t len,
    const uint8_t *src, uint8_t *dest) {
    if (len >= 0x7ff) {
        mp_raise_ValueError(
            MP_ERROR_TEXT("cannot transfer more than 2047 bytes at a time"));
    }

    machine_spi_obj_t *self = MP_OBJ_TO_PTR(obj);

    volatile spi_block_t *spi_block = self->spi_block;

    wait_idle(spi_block);

    bool rcv = false;
    bool trx = false;
    uint32_t control = 0;

    uint32_t tx_count = len;
    uint32_t rx_count = len;

    if (src) {
        trx = true;
        control = ControlTransmitEnable;
        tx_count = 0;
    }
    if (dest) {
        rcv = true;
        control |= ControlReceiveEnable;
        rx_count = 0;
    }

    spi_block->control = control;
    spi_block->start = len & StartByteCountMask;

    while (tx_count < len || rx_count < len) {
        uint32_t status = spi_block->status;
        if (trx && (status & StatusTxFifoLevel) < 64 && tx_count < len) {
            spi_block->transmitFifo = src[tx_count];
            printf("tx >>> %02x -- %08x\n", src[tx_count],
                (uint32_t)&(spi_block->transmitFifo));
            tx_count++;
        } else if (rcv && (status & StatusRxFifoLevel) > 0 && rx_count < len) {
            dest[rx_count] = spi_block->receiveFifo;
            printf("rx >>> %02x -- %08x\n", dest[rx_count],
                (uint32_t)&(spi_block->receiveFifo));

            rx_count++;
        }
    }
}

static const mp_machine_spi_p_t machine_spi_protocol = {
    .init = machine_spi_init,
    .transfer = machine_spi_transfer,
};

MP_DEFINE_CONST_OBJ_TYPE(machine_spi_type, MP_QSTR_SPI, MP_TYPE_FLAG_NONE,
    make_new, machine_spi_make_new, call, print,
    machine_spi_print, protocol, &machine_spi_protocol,
    locals_dict, &mp_machine_spi_locals_dict);
