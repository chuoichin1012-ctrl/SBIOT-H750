#include "qspi_flash.h"

#define PHASE_MS 100u  // HAL timeout of one command phase

static uint32_t s_dummy = 4;  // EBh dummy clocks after M7-0: 4 with DC = 0, 8 with DC = 1 (§6 table)

static QSPI_CommandTypeDef command(uint8_t ins)
{
    QSPI_CommandTypeDef c = {0};
    c.Instruction = ins;
    c.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    c.AddressMode = QSPI_ADDRESS_NONE;
    c.AddressSize = QSPI_ADDRESS_24_BITS;
    c.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    c.DataMode = QSPI_DATA_NONE;
    c.DdrMode = QSPI_DDR_MODE_DISABLE;
    c.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    c.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;
    return c;
}

static int send(QSPI_HandleTypeDef *h, uint8_t ins)
{
    QSPI_CommandTypeDef c = command(ins);
    return HAL_QSPI_Command(h, &c, PHASE_MS) == HAL_OK ? QF_OK : QF_EHAL;
}

static int read_reg(QSPI_HandleTypeDef *h, uint8_t ins, uint8_t *out, uint32_t n)
{
    QSPI_CommandTypeDef c = command(ins);
    c.DataMode = QSPI_DATA_1_LINE;
    c.NbData = n;
    if (HAL_QSPI_Command(h, &c, PHASE_MS) != HAL_OK)
        return QF_EHAL;
    return HAL_QSPI_Receive(h, out, PHASE_MS) == HAL_OK ? QF_OK : QF_EHAL;
}

static int write_reg(QSPI_HandleTypeDef *h, uint8_t ins, uint8_t v)
{
    QSPI_CommandTypeDef c = command(ins);
    c.DataMode = QSPI_DATA_1_LINE;
    c.NbData = 1;
    if (HAL_QSPI_Command(h, &c, PHASE_MS) != HAL_OK)
        return QF_EHAL;
    return HAL_QSPI_Transmit(h, &v, PHASE_MS) == HAL_OK ? QF_OK : QF_EHAL;
}

// Polls WIP (SR1 bit 0) until clear.
static int wait_ready(QSPI_HandleTypeDef *h, uint32_t ms, qf_kick_fn kick)
{
    uint32_t t0 = HAL_GetTick();
    for (;;) {
        uint8_t sr1;
        if (read_reg(h, 0x05, &sr1, 1))
            return QF_EHAL;
        if (!(sr1 & 0x01))
            return QF_OK;
        if (kick)
            kick();
        if (HAL_GetTick() - t0 > ms)
            return QF_ETIMEOUT;
    }
}

void qf_errata_timing(void)
{
    // The disable must follow the first CCR write within 127 kernel clocks (0.79 us at 160 MHz):
    // interrupts are off and the three stores are consecutive (tools/check_elf.py checks the window).
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    QUADSPI->CR = 0;  // prescaler not at maximum, peripheral disabled
    while (QUADSPI->SR & QUADSPI_SR_BUSY) {
    }
    QUADSPI->CR = 0xFF000001u;   // maximum prescaler, enabled
    QUADSPI->CCR = 0x20000000u;  // free-running clock
    QUADSPI->CCR = 0x20000000u;  // repeated, against a back-to-back disable
    QUADSPI->CR = 0;
    while (QUADSPI->SR & QUADSPI_SR_BUSY) {
    }
    __set_PRIMASK(primask);
}

uint32_t qf_read_jedec(QSPI_HandleTypeDef *h)
{
    uint8_t id[3];
    if (read_reg(h, 0x9F, id, 3))
        return 0;
    return (uint32_t)id[0] << 16 | (uint32_t)id[1] << 8 | id[2];
}

int qf_init(QSPI_HandleTypeDef *h, qf_kick_fn kick)
{
    // The MCU may have reset while a previous run left the flash in continuous-read mode, where it
    // takes the next clocks as an address. Ten clocks with IO0-IO3 high give it M7-0 = FFh, which
    // ends that mode (§7.11); a flash in normal mode sees an unknown command FFh and ignores it.
    // The ones go out as instruction (2 clocks) plus 32-bit alternate bytes (8 clocks): an address
    // phase cannot carry them, since QUADSPI rejects addresses beyond FSIZE (TEF).
    QSPI_CommandTypeDef c = command(0xFF);
    c.InstructionMode = QSPI_INSTRUCTION_4_LINES;
    c.AlternateByteMode = QSPI_ALTERNATE_BYTES_4_LINES;
    c.AlternateBytesSize = QSPI_ALTERNATE_BYTES_32_BITS;
    c.AlternateBytes = 0xFFFFFFFFu;
    if (HAL_QSPI_Command(h, &c, PHASE_MS) != HAL_OK)
        return QF_EHAL;
    // Software reset (§7.29). tRST is 30 us, tRST_E 12 ms when an erase was interrupted.
    if (send(h, 0x66) || send(h, 0x99))
        return QF_EHAL;
    HAL_Delay(13);
    if (kick)
        kick();
    if (qf_read_jedec(h) != QF_JEDEC_GD25Q32E)
        return QF_EID;
    // QE is SR2 bit 1 (S9), non-volatile: written only when clear, keeping the other SR2 bits.
    uint8_t sr2;
    if (read_reg(h, 0x35, &sr2, 1))
        return QF_EHAL;
    if (!(sr2 & 0x02)) {
        if (send(h, 0x06) || write_reg(h, 0x31, (uint8_t)(sr2 | 0x02)))
            return QF_EHAL;
        int r = wait_ready(h, 60, kick);  // tW is 30 ms at most
        if (r)
            return r;
        if (read_reg(h, 0x35, &sr2, 1))
            return QF_EHAL;
        if (!(sr2 & 0x02))
            return QF_EQE;
    }
    // DC is SR3 bit 0 (S16). It stays as found: the default 0 allows 104 MHz, and QSPI runs at 80 MHz.
    uint8_t sr3;
    if (read_reg(h, 0x15, &sr3, 1))
        return QF_EHAL;
    s_dummy = (sr3 & 0x01) ? 8 : 4;
    return QF_OK;
}

int qf_memory_mapped(QSPI_HandleTypeDef *h)
{
    QSPI_CommandTypeDef c = command(0xEB);
    c.AddressMode = QSPI_ADDRESS_4_LINES;
    c.AlternateByteMode = QSPI_ALTERNATE_BYTES_4_LINES;  // M7-0 on four lines (two clocks)
    c.AlternateBytesSize = QSPI_ALTERNATE_BYTES_8_BITS;
    c.AlternateBytes = 0xFF;  // M5-4 != 10b: the flash never enters continuous-read mode
    c.DummyCycles = s_dummy;
    c.DataMode = QSPI_DATA_4_LINES;
    QSPI_MemoryMappedTypeDef m = {.TimeOutActivation = QSPI_TIMEOUT_COUNTER_DISABLE, .TimeOutPeriod = 0};
    return HAL_QSPI_MemoryMapped(h, &c, &m) == HAL_OK ? QF_OK : QF_EHAL;
}

#define ERASE_MS 600u  // 2 x tSE 300 ms
#define PROGRAM_MS 6u  // 2 x tPP 2.4 ms, plus one tick of HAL_GetTick resolution

static int nor_code(int qf)
{
    return qf == QF_OK ? NOR_OK : qf == QF_ETIMEOUT ? NOR_ETIMEOUT : NOR_EIO;
}

static int nor_read(nor_t *n, uint32_t off, void *buf, uint32_t len)
{
    qf_nor_t *q = (qf_nor_t *)n;
    if (!len)
        return NOR_OK;
    QSPI_CommandTypeDef c = command(0xEB);  // the memory-mapped read, one transfer at a time
    c.AddressMode = QSPI_ADDRESS_4_LINES;
    c.Address = off;
    c.AlternateByteMode = QSPI_ALTERNATE_BYTES_4_LINES;
    c.AlternateBytesSize = QSPI_ALTERNATE_BYTES_8_BITS;
    c.AlternateBytes = 0xFF;
    c.DummyCycles = s_dummy;
    c.DataMode = QSPI_DATA_4_LINES;
    c.NbData = len;
    if (HAL_QSPI_Command(q->h, &c, PHASE_MS) != HAL_OK)
        return NOR_EIO;
    return HAL_QSPI_Receive(q->h, buf, PHASE_MS) == HAL_OK ? NOR_OK : NOR_EIO;
}

static int nor_erase(nor_t *n, uint32_t off)
{
    qf_nor_t *q = (qf_nor_t *)n;
    QSPI_CommandTypeDef c = command(0x20);
    c.AddressMode = QSPI_ADDRESS_1_LINE;
    c.Address = off;
    if (send(q->h, 0x06) || HAL_QSPI_Command(q->h, &c, PHASE_MS) != HAL_OK)
        return NOR_EIO;
    return nor_code(wait_ready(q->h, ERASE_MS, q->kick));
}

static int nor_program(nor_t *n, uint32_t off, const void *buf, uint32_t len)
{
    qf_nor_t *q = (qf_nor_t *)n;
    if (len == 0 || len > NOR_PAGE || off / NOR_PAGE != (off + len - 1) / NOR_PAGE)
        return NOR_EIO;
    QSPI_CommandTypeDef c = command(0x02);
    c.AddressMode = QSPI_ADDRESS_1_LINE;
    c.Address = off;
    c.DataMode = QSPI_DATA_1_LINE;
    c.NbData = len;
    if (send(q->h, 0x06) || HAL_QSPI_Command(q->h, &c, PHASE_MS) != HAL_OK ||
        HAL_QSPI_Transmit(q->h, (uint8_t *)buf, PHASE_MS) != HAL_OK)
        return NOR_EIO;
    return nor_code(wait_ready(q->h, PROGRAM_MS, q->kick));
}

void qf_nor_init(qf_nor_t *q, QSPI_HandleTypeDef *h, qf_kick_fn kick)
{
    q->nor.read = nor_read;
    q->nor.erase_sector = nor_erase;
    q->nor.program = nor_program;
    q->nor.ctx = q;
    q->h = h;
    q->kick = kick;
}
