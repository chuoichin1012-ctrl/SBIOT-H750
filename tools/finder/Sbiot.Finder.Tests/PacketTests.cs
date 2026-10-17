using System.Text.Json;
using Sbiot.Finder.Protocol;
using Xunit;

public class PacketTests
{
    static readonly JsonElement V = JsonDocument.Parse(File.ReadAllText("disc_vectors.json")).RootElement;
    static JsonElement Req(string name) => V.GetProperty("requests").EnumerateArray().First(r => r.GetProperty("name").GetString() == name);
    static string Text(string name) => Req(name).GetProperty("text").GetString()!;
    const string Mac = "02:95:b8:e9:a7:9f";
    static readonly NetSettings Away = new(1, "10.10.0.7", "255.255.255.0", "10.10.0.1", 1, "8.8.8.8", "1.1.1.1");

    [Fact] public void SearchMatchesTheGateway() => Assert.Equal(Text("search"), Packets.Search("7f3a91c2"));
    [Fact] public void SearchForOneMac() => Assert.Equal(Text("search for this MAC"), Packets.Search("7f3a91c2", Mac));
    [Fact] public void SetMatchesTheGateway() => Assert.Equal(Text("set"), Packets.Set("5b20e7d1", Mac, Away));
    [Fact] public void RestartAndFactory()
    {
        Assert.Equal(Text("restart"), Packets.Command("restart", "0a1b2c3d", Mac));
        Assert.Equal(Text("factory"), Packets.Command("factory", "0a1b2c3d", Mac));
    }

    [Fact] public void EveryBuiltVectorIsCovered() =>
        Assert.Equal(5, V.GetProperty("requests").EnumerateArray().Count(r => r.TryGetProperty("built", out var b) && b.GetBoolean()));

    [Fact] public void ParsesFound()
    {
        var f = V.GetProperty("found");
        var got = Packets.ParseFound(f.GetProperty("text").GetString()!, "7f3a91c2");
        Assert.NotNull(got);
        Assert.Equal(Mac, got!.Mac);
        Assert.Equal("SB2610000001", got.Sn);
        Assert.Equal(197, got.Boot);
        Assert.Equal(80, got.Web);
        Assert.Equal("192.168.0.7", got.EthIp);
        Assert.Equal(new NetSettings(1, "192.168.0.7", "255.255.255.0", "192.168.0.1", 1, "8.8.8.8", "1.1.1.1"), got.Net);
    }

    [Fact] public void FoundForAnotherIdIsNotOurs() =>
        Assert.Null(Packets.ParseFound(V.GetProperty("found").GetProperty("text").GetString()!, "00000000"));

    [Fact] public void OurOwnSearchIsNotAFound() => Assert.Null(Packets.ParseFound(Text("search"), "7f3a91c2"));

    [Fact] public void ParsesAcks()
    {
        foreach (var a in V.GetProperty("acks").EnumerateArray())
        {
            var got = Packets.ParseAck(a.GetProperty("text").GetString()!, "5b20e7d1", Mac);
            Assert.NotNull(got);
            var err = a.GetProperty("err").ValueKind == JsonValueKind.Null ? null : a.GetProperty("err").GetString();
            Assert.Equal(err is null, got!.Ok);
            Assert.Equal(err, got.Err);
        }
    }

    [Fact] public void AckOfAnotherGatewayOrIdIsNotOurs()
    {
        var t = V.GetProperty("acks")[0].GetProperty("text").GetString()!;
        Assert.Null(Packets.ParseAck(t, "5b20e7d1", "02:00:00:00:00:01"));
        Assert.Null(Packets.ParseAck(t, "00000000", Mac));
        Assert.Null(Packets.ParseAck("not json", "5b20e7d1", Mac));
    }

    [Fact] public void IdsAreEightLowercaseHex() =>
        Assert.Matches("^[0-9a-f]{8}$", Packets.NewId());
}
