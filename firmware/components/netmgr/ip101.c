#include "ip101.h"

#define MAX_ADDR 31u

static int rd(ip101_t *phy, uint32_t reg, uint32_t *val)
{
    return phy->io.read(phy->io.ctx, phy->addr, reg, val);
}

ip101_status_t ip101_init(ip101_t *phy, const ip101_io_t *io)
{
    phy->io = *io;
    ip101_status_t none = IP101_ENOPHY;
    for (uint32_t a = 0; a <= MAX_ADDR; a++) {
        uint32_t id1, id2;
        if (io->read(io->ctx, a, PHY_ID1, &id1) || io->read(io->ctx, a, PHY_ID2, &id2)) {
            none = IP101_EREAD;  // can't read from this address: go on with the next one
            continue;
        }
        id1 &= 0xFFFFu;
        id2 &= 0xFFFFu;
        if (id1 == 0x0000u || id1 == 0xFFFFu)
            continue;
        phy->addr = a;
        phy->id = id1 << 16 | id2;
        return IP101_OK;
    }
    return none;
}

ip101_status_t ip101_link(ip101_t *phy)
{
    uint32_t bmsr, bmcr, anar, anlpar;
    // The link bit latches low: the first read clears an old drop, the second tells the state now.
    if (rd(phy, PHY_BMSR, &bmsr) || rd(phy, PHY_BMSR, &bmsr))
        return IP101_EREAD;
    if (!(bmsr & BMSR_LINK))
        return IP101_LINK_DOWN;
    if (rd(phy, PHY_BMCR, &bmcr))
        return IP101_EREAD;
    if (!(bmcr & BMCR_ANEN)) {
        if (bmcr & BMCR_SPEED100)
            return bmcr & BMCR_FULL ? IP101_100_FULL : IP101_100_HALF;
        return bmcr & BMCR_FULL ? IP101_10_FULL : IP101_10_HALF;
    }
    if (!(bmsr & BMSR_ANDONE))
        return IP101_AUTONEG_NOT_DONE;
    if (rd(phy, PHY_ANAR, &anar) || rd(phy, PHY_ANLPAR, &anlpar))
        return IP101_EREAD;
    uint32_t common = anar & anlpar;  // the highest ability both ends advertise (802.3 Annex 28B.3)
    if (common & AN_100FULL)
        return IP101_100_FULL;
    if (common & AN_100HALF)
        return IP101_100_HALF;
    if (common & AN_10FULL)
        return IP101_10_FULL;
    return IP101_10_HALF;
}

ip101_status_t ip101_power(ip101_t *phy, int down)
{
    uint32_t bmcr = down ? BMCR_PDOWN : BMCR_ANEN | BMCR_ANRESTART;
    return phy->io.write(phy->io.ctx, phy->addr, PHY_BMCR, bmcr) ? IP101_EWRITE : IP101_OK;
}
