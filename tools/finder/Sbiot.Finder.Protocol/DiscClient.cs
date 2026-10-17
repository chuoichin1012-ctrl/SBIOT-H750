using System.Net;
using System.Net.Sockets;
using System.Text;

namespace Sbiot.Finder.Protocol;

/// <summary>Broadcasts from one adapter's address and reads the replies that come back to it (spec §3).</summary>
public sealed class DiscClient : IDisposable
{
    readonly UdpClient udp;

    public DiscClient(IPAddress local)
    {
        udp = new UdpClient(new IPEndPoint(local, 0)) { EnableBroadcast = true };
    }

    public void Dispose() => udp.Dispose();

    async Task Send(string text)
    {
        var b = Encoding.UTF8.GetBytes(text);
        await udp.SendAsync(b, b.Length, new IPEndPoint(IPAddress.Broadcast, Packets.Port));
    }

    /// <summary>Reads replies for `wait`, or until `done` says so.</summary>
    async Task Read(TimeSpan wait, Func<string, bool> done)
    {
        using var cts = new CancellationTokenSource(wait);
        try
        {
            while (true)
            {
                var r = await udp.ReceiveAsync(cts.Token);
                if (done(Encoding.UTF8.GetString(r.Buffer))) return;
            }
        }
        catch (OperationCanceledException) { }
    }

    public async Task<List<Found>> SearchAsync(TimeSpan wait, string? mac = null)
    {
        var id = Packets.NewId();
        var table = new FoundTable();
        await Send(Packets.Search(id, mac));
        await Read(wait, t => { if (Packets.ParseFound(t, id) is { } f) table.Merge(f); return false; });
        return table.Rows.ToList();
    }

    public async Task<Ack?> CommandAsync(string op, string mac, NetSettings? net, TimeSpan wait)
    {
        var id = Packets.NewId();
        Ack? ack = null;
        await Send(op == "set" ? Packets.Set(id, mac, net!) : Packets.Command(op, id, mac));
        await Read(wait, t => (ack = Packets.ParseAck(t, id, mac)) is not null);
        return ack;
    }

    /// <summary>Searches for `mac` every `every` until it answers with a boot above `boot`; null after `total`.</summary>
    public async Task<Found?> WaitBackAsync(string mac, long boot, TimeSpan total, TimeSpan every)
    {
        var end = DateTime.UtcNow + total;
        while (DateTime.UtcNow < end)
        {
            var f = (await SearchAsync(every, mac)).FirstOrDefault(x => x.Boot > boot);
            if (f is not null) return f;
        }
        return null;
    }
}
