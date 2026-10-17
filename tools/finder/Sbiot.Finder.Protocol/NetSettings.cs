using System.Net;
using System.Text.RegularExpressions;

namespace Sbiot.Finder.Protocol;

/// <summary>The seven Ethernet fields of the gateway's `network` config (spec §2.2). The tool checks them; the gateway checks syntax only.</summary>
public sealed record NetSettings(int IpMode, string Ip, string Netmask, string Gateway, int DnsMode, string Dns1, string Dns2)
{
    // ASCII digits only ([0-9], not \d: .NET's \d takes any Unicode digit, which IPAddress.Parse then rejects; Codex plan r2).
    static readonly Regex Quad = new(@"\A(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])(\.(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])){3}\z");

    /// <summary>A strict dotted quad, as the gateway's netcfg_ip4: no sign, space or leading zero.</summary>
    public static bool IsQuad(string s) => s is not null && Quad.IsMatch(s);

    static uint U(string s) => (uint)IPAddress.NetworkToHostOrder(BitConverter.ToInt32(IPAddress.Parse(s).GetAddressBytes()));

    /// <summary>Null when the settings are good, else what to fix.</summary>
    public string? Validate()
    {
        foreach (var (v, what) in new[] { (Ip, "IP address"), (Netmask, "Subnet mask"), (Gateway, "Gateway"), (Dns1, "DNS 1"), (Dns2, "DNS 2") })
            if (!IsQuad(v)) return $"{what}: enter four numbers 0-255, such as 192.168.0.7.";
        if (IpMode == 1)
        {
            uint ip = U(Ip), mask = U(Netmask), gw = U(Gateway);
            uint inv = ~mask;
            if (mask == 0 || mask == 0xFFFFFFFF || (inv & (inv + 1)) != 0) return "Subnet mask: use a mask such as 255.255.255.0.";
            if ((ip & inv) == 0 || (ip & inv) == inv) return "IP address: this is the network's own or broadcast address.";
            // 0.0.0.0 is "no gateway" (an isolated LAN); the gateway accepts it.
            if (gw != 0 && ((gw & mask) != (ip & mask) || gw == ip || (gw & inv) == inv))
                return "Gateway: it must be another address in the same subnet as the IP address, or 0.0.0.0 for none.";
        }
        return null;  // DNS: syntax only; 0.0.0.0 is "none", as the gateway reads it
    }
}
