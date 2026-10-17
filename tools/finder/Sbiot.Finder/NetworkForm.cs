using Sbiot.Finder.Protocol;

namespace Sbiot.Finder;

/// <summary>The Ethernet settings of one gateway, filled from its saved values (`net` of found).</summary>
sealed class NetworkForm : Form
{
    readonly RadioButton dhcp = new() { Text = "DHCP", AutoSize = true };
    readonly RadioButton stat = new() { Text = "Static", AutoSize = true };
    readonly TextBox ip = new(), mask = new(), gw = new(), dns1 = new(), dns2 = new();
    readonly CheckBox manualDns = new() { Text = "Manual DNS", AutoSize = true };
    public NetSettings Result { get; private set; }

    public NetworkForm(NetSettings n)
    {
        Text = "Network settings";
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = MinimizeBox = false;
        StartPosition = FormStartPosition.CenterParent;
        AutoSize = true;
        AutoSizeMode = AutoSizeMode.GrowAndShrink;
        Result = n;
        (n.IpMode == 1 ? stat : dhcp).Checked = true;
        ip.Text = n.Ip; mask.Text = n.Netmask; gw.Text = n.Gateway; dns1.Text = n.Dns1; dns2.Text = n.Dns2;
        manualDns.Checked = n.DnsMode == 1;
        var grid = new TableLayoutPanel { ColumnCount = 2, AutoSize = true, Padding = new Padding(10) };
        var mode = new FlowLayoutPanel { AutoSize = true };
        mode.Controls.AddRange([dhcp, stat]);
        void Row(string label, Control c) { grid.Controls.Add(new Label { Text = label, AutoSize = true, Padding = new Padding(0, 6, 0, 0) }); grid.Controls.Add(c); c.Width = 160; }
        Row("Address", mode);
        Row("IP address", ip);
        Row("Subnet mask", mask);
        Row("Gateway", gw);
        Row("DNS", manualDns);
        Row("DNS 1", dns1);
        Row("DNS 2", dns2);
        var ok = new Button { Text = "Save", DialogResult = DialogResult.None };
        var cancel = new Button { Text = "Cancel", DialogResult = DialogResult.Cancel };
        var buttons = new FlowLayoutPanel { AutoSize = true, FlowDirection = FlowDirection.RightToLeft, Dock = DockStyle.Bottom };
        buttons.Controls.AddRange([cancel, ok]);
        grid.Controls.Add(new Label());
        grid.Controls.Add(buttons);
        Controls.Add(grid);
        AcceptButton = ok;
        CancelButton = cancel;
        void Sync()
        {
            ip.Enabled = mask.Enabled = gw.Enabled = stat.Checked;
            if (stat.Checked) manualDns.Checked = true;  // a static address always uses the manual DNS (spec §7.1)
            manualDns.Enabled = dhcp.Checked;
            dns1.Enabled = dns2.Enabled = manualDns.Checked;
        }
        dhcp.CheckedChanged += (_, _) => Sync();
        manualDns.CheckedChanged += (_, _) => Sync();
        Sync();
        ok.Click += (_, _) =>
        {
            var r = new NetSettings(stat.Checked ? 1 : 0, ip.Text.Trim(), mask.Text.Trim(), gw.Text.Trim(), manualDns.Checked ? 1 : 0, dns1.Text.Trim(), dns2.Text.Trim());
            if (r.Validate() is { } err) { MessageBox.Show(this, err, Text, MessageBoxButtons.OK, MessageBoxIcon.Warning); return; }
            Result = r;
            DialogResult = DialogResult.OK;
        };
    }
}
