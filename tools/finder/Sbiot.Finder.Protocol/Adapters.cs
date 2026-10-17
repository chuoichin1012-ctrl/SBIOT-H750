using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;

namespace Sbiot.Finder.Protocol;

public static class Adapters
{
    /// <summary>Each IPv4 address of each adapter that is up, loopback left out.</summary>
    public static IReadOnlyList<(string Name, IPAddress Address)> List() =>
        NetworkInterface.GetAllNetworkInterfaces()
            .Where(n => n.OperationalStatus == OperationalStatus.Up && n.NetworkInterfaceType != NetworkInterfaceType.Loopback)
            .SelectMany(n => n.GetIPProperties().UnicastAddresses
                .Where(a => a.Address.AddressFamily == AddressFamily.InterNetwork)
                .Select(a => (n.Name, a.Address)))
            .ToList();
}
