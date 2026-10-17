using Sbiot.Finder.Protocol;
using Xunit;

public class NetSettingsTests
{
    static NetSettings S(string ip = "10.10.0.7", string mask = "255.255.255.0", string gw = "10.10.0.1", int ipMode = 1, int dnsMode = 1,
                         string d1 = "8.8.8.8", string d2 = "1.1.1.1") => new(ipMode, ip, mask, gw, dnsMode, d1, d2);

    [Fact] public void AGoodStaticAddress() => Assert.Null(S().Validate());
    [Theory]
    [InlineData("10.10.0.256")] [InlineData("10.10.0.07")] [InlineData("10.10.0")] [InlineData(" 10.10.0.7")] [InlineData("")]
    [InlineData("10.10.0.\u0667")] [InlineData("10.10.0.7\n")]  // an Arabic-Indic seven; a trailing newline
    public void ABadQuad(string ip) => Assert.NotNull(S(ip: ip).Validate());
    [Theory] [InlineData("255.0.255.0")] [InlineData("0.0.0.0")] [InlineData("255.255.255.255")]
    public void AMaskThatIsNotUsable(string mask) => Assert.NotNull(S(mask: mask).Validate());
    [Fact] public void TheNetworkAddress() => Assert.NotNull(S(ip: "10.10.0.0").Validate());
    [Fact] public void TheBroadcastAddress() => Assert.NotNull(S(ip: "10.10.0.255").Validate());
    [Fact] public void AGatewayOutsideTheSubnet() => Assert.NotNull(S(gw: "10.10.1.1").Validate());
    [Fact] public void TheGatewayIsTheAddress() => Assert.NotNull(S(gw: "10.10.0.7").Validate());
    [Fact] public void AnIsolatedLanHasNoGateway() => Assert.Null(S(gw: "0.0.0.0").Validate());
    [Fact] public void NoDnsServerIsAllowed() => Assert.Null(S(d1: "0.0.0.0", d2: "0.0.0.0").Validate());
    [Fact] public void DhcpChecksSyntaxOnly() => Assert.Null(S(ipMode: 0, dnsMode: 0, gw: "10.10.1.1", d1: "0.0.0.0").Validate());
    [Fact] public void DhcpStillNeedsQuads() => Assert.NotNull(S(ipMode: 0, ip: "x").Validate());
}
