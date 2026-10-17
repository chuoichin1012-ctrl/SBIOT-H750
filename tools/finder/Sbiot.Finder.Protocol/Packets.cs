using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace Sbiot.Finder.Protocol;

public sealed record Found(string Mac, string Sn, string Model, string Fw, string Hostname, int Web, long Boot,
                           string EthIp, string EthNetmask, string EthGateway, NetSettings Net);

public sealed record Ack(string Mac, bool Ok, string? Err);

/// <summary>FinderTool's packets (spec §2): built byte for byte as disc_vectors.json has them.</summary>
public static class Packets
{
    public const int Port = 48600;

    public static string NewId() => Convert.ToHexStringLower(RandomNumberGenerator.GetBytes(4));

    static string Write(Action<Utf8JsonWriter> body)
    {
        using var ms = new MemoryStream();
        using (var w = new Utf8JsonWriter(ms))
        {
            w.WriteStartObject();
            body(w);
            w.WriteEndObject();
        }
        return Encoding.UTF8.GetString(ms.ToArray());
    }

    static void Head(Utf8JsonWriter w, string op, string id, string? mac)
    {
        w.WriteString("sbiot", "disc");
        w.WriteNumber("v", 1);
        w.WriteString("op", op);
        w.WriteString("id", id);
        if (mac is not null) w.WriteString("mac", mac);
    }

    public static string Search(string id, string? mac = null) => Write(w => Head(w, "search", id, mac));

    public static string Command(string op, string id, string mac) => Write(w => Head(w, op, id, mac));

    public static string Set(string id, string mac, NetSettings n) => Write(w =>
    {
        Head(w, "set", id, mac);
        w.WriteStartObject("net");
        w.WriteNumber("ipMode", n.IpMode);
        w.WriteString("ip", n.Ip);
        w.WriteString("netmask", n.Netmask);
        w.WriteString("gateway", n.Gateway);
        w.WriteNumber("dnsMode", n.DnsMode);
        w.WriteString("dns1", n.Dns1);
        w.WriteString("dns2", n.Dns2);
        w.WriteEndObject();
    });

    static JsonElement? Ours(string text, string op, string id)
    {
        try
        {
            var r = JsonDocument.Parse(text).RootElement;
            if (r.ValueKind == JsonValueKind.Object && r.TryGetProperty("sbiot", out var s) && s.GetString() == "disc" &&
                r.TryGetProperty("v", out var v) && v.GetInt32() == 1 && r.GetProperty("op").GetString() == op &&
                r.GetProperty("id").GetString() == id)
                return r;
        }
        catch (Exception e) when (e is JsonException or InvalidOperationException or KeyNotFoundException or FormatException) { }
        return null;
    }

    static NetSettings Net(JsonElement n) => new(n.GetProperty("ipMode").GetInt32(), n.GetProperty("ip").GetString()!,
        n.GetProperty("netmask").GetString()!, n.GetProperty("gateway").GetString()!, n.GetProperty("dnsMode").GetInt32(),
        n.GetProperty("dns1").GetString()!, n.GetProperty("dns2").GetString()!);

    /// <summary>A gateway's reply to our search `id`; null for anything else (our own broadcast comes back too).</summary>
    public static Found? ParseFound(string text, string id)
    {
        if (Ours(text, "found", id) is not { } r) return null;
        try
        {
            var e = r.GetProperty("eth");
            return new Found(r.GetProperty("mac").GetString()!, r.GetProperty("sn").GetString()!, r.GetProperty("model").GetString()!,
                r.GetProperty("fw").GetString()!, r.GetProperty("hostname").GetString()!, r.GetProperty("web").GetInt32(),
                r.GetProperty("boot").GetInt64(), e.GetProperty("ip").GetString()!, e.GetProperty("netmask").GetString()!,
                e.GetProperty("gateway").GetString()!, Net(r.GetProperty("net")));
        }
        catch (Exception x) when (x is InvalidOperationException or KeyNotFoundException or FormatException) { return null; }
    }

    /// <summary>The ack of our command `id` to `mac`; null for anything else.</summary>
    public static Ack? ParseAck(string text, string id, string mac)
    {
        if (Ours(text, "ack", id) is not { } r) return null;
        try
        {
            if (r.GetProperty("mac").GetString() != mac) return null;
            bool ok = r.GetProperty("ok").GetBoolean();
            return new Ack(mac, ok, ok ? null : r.GetProperty("err").GetString());
        }
        catch (Exception x) when (x is InvalidOperationException or KeyNotFoundException) { return null; }
    }
}
