using Sbiot.Finder.Protocol;
using Xunit;

public class FoundTableTests
{
    static readonly NetSettings N = new(1, "192.168.0.7", "255.255.255.0", "192.168.0.1", 1, "8.8.8.8", "1.1.1.1");
    static Found F(string mac, long boot) => new(mac, "SB2610000001", "SBIOT-H750", "0.7.0", "sbiot-gw", 80, boot, "192.168.0.7", "255.255.255.0", "192.168.0.1", N);

    [Fact] public void TwoGatewaysTwoRows()
    {
        var t = new FoundTable();
        t.Merge(F("02:00:00:00:00:01", 1));
        t.Merge(F("02:00:00:00:00:02", 1));
        Assert.Equal(2, t.Rows.Count);
    }

    [Fact] public void ALaterReplyReplacesItsRowInPlace()
    {
        var t = new FoundTable();
        t.Merge(F("02:00:00:00:00:01", 1));
        t.Merge(F("02:00:00:00:00:02", 1));
        t.Merge(F("02:00:00:00:00:01", 2));
        Assert.Equal(2, t.Rows.Count);
        Assert.Equal("02:00:00:00:00:01", t.Rows[0].Mac);
        Assert.Equal(2, t.Rows[0].Boot);
    }

    // A gateway set to DHCP with no server answers from 0.0.0.0 (the user, 2026-10-17): it has no address to open,
    // only Network… to give it one.
    [Fact] public void NoAddressIsShownAsSuch()
    {
        var f = F("02:00:00:00:00:01", 1) with { EthIp = "0.0.0.0" };
        Assert.False(f.HasAddress);
        Assert.True(F("02:00:00:00:00:02", 1).HasAddress);
    }
}
