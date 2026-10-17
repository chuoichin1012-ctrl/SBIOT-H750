using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using Sbiot.Finder.Protocol;

namespace Sbiot.Finder;

/// <summary>FinderTool (Plan 12, spec §3): pick the adapter, Search, act on the selected gateway.</summary>
sealed class MainForm : Form
{
    readonly ComboBox adapter = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 360 };
    readonly Button search = new() { Text = "Search", AutoSize = true };
    readonly Button network = new() { Text = "Network…", AutoSize = true, Enabled = false };
    readonly Button restart = new() { Text = "Restart", AutoSize = true, Enabled = false };
    readonly Button factory = new() { Text = "Factory reset", AutoSize = true, Enabled = false };
    readonly ListView list = new() { View = View.Details, FullRowSelect = true, MultiSelect = false, Dock = DockStyle.Fill };
    readonly Label status = new() { Dock = DockStyle.Bottom, Height = 24, TextAlign = ContentAlignment.MiddleLeft };
    readonly FoundTable table = new();
    IReadOnlyList<(string Name, IPAddress Address)> adapters = [];

    public MainForm()
    {
        Text = "FinderTool";
        Icon = Brand.AppIcon();
        BackColor = Color.White;
        search.BackColor = Brand.Blue;  // the main action, in the brand's blue as the Web UI's buttons
        search.ForeColor = Color.White;
        search.FlatStyle = FlatStyle.Flat;
        search.FlatAppearance.BorderColor = Brand.Blue;
        search.MinimumSize = new Size(110, 0);  // "Searching…" does not move the buttons beside it
        Width = 900;
        Height = 480;
        foreach (var (h, w) in new[] { ("MAC", 130), ("SN", 110), ("IP", 110), ("Hostname", 130), ("Model", 100), ("Firmware", 80) })
            list.Columns.Add(h, w);
        var bar = new FlowLayoutPanel { Dock = DockStyle.Top, Height = 40, Padding = new Padding(6) };
        bar.Controls.AddRange([new Label { Text = "Adapter:", AutoSize = true, Padding = new Padding(0, 6, 0, 0) }, adapter, search, network, restart, factory]);
        Controls.Add(list);
        Controls.Add(bar);
        Controls.Add(status);
        Controls.Add(Brand.Header());  // added last, so docked first: above the toolbar
        adapters = Adapters.List();
        foreach (var (name, addr) in adapters) adapter.Items.Add($"{name} — {addr}");
        if (adapter.Items.Count > 0) adapter.SelectedIndex = 0;
        search.Click += async (_, _) => await Search();
        list.SelectedIndexChanged += (_, _) => network.Enabled = restart.Enabled = factory.Enabled = !busy && Selected is not null;
        list.DoubleClick += (_, _) => OpenWeb();
        network.Click += async (_, _) => await SetNetwork();
        restart.Click += async (_, _) => await Act("restart", null, "Restart");
        factory.Click += async (_, _) =>
        {
            if (MessageBox.Show(this, "Factory reset erases every setting of this gateway (network, MQTT, Edge, certificates, web password) and restarts it. Continue?",
                    "Factory reset", MessageBoxButtons.YesNo, MessageBoxIcon.Warning) == DialogResult.Yes)
                await Act("factory", null, "Factory reset");
        };
    }

    bool busy;

    /// <summary>A search or a command at work: the other controls are locked. Search itself is never disabled: WinForms
    /// draws a disabled flat button with our blue dark (the user); it shows the work in a lighter blue instead.</summary>
    void Busy(bool on, string label)
    {
        busy = on;
        search.Text = on ? label : "Search";
        search.BackColor = search.FlatAppearance.BorderColor = on ? Brand.BlueBusy : Brand.Blue;
        adapter.Enabled = !on;
        network.Enabled = restart.Enabled = factory.Enabled = !on && Selected is not null;
        UseWaitCursor = on;
    }

    Found? Selected => list.SelectedItems.Count == 1 ? (Found)list.SelectedItems[0].Tag! : null;

    DiscClient? Client()
    {
        if (adapter.SelectedIndex < 0) { status.Text = "No network adapter with an IPv4 address."; return null; }
        return new DiscClient(adapters[adapter.SelectedIndex].Address);
    }

    void ShowRow(Found f)
    {
        table.Merge(f);
        list.BeginUpdate();
        list.Items.Clear();
        foreach (var r in table.Rows)
            list.Items.Add(new ListViewItem([r.Mac, r.Sn, r.EthIp, r.Hostname, r.Model, r.Fw]) { Tag = r });
        list.EndUpdate();
    }

    // Every handler catches the network errors itself (an async void handler must not throw, Codex plan r1).
    async Task Search()
    {
        if (busy) return;
        Busy(true, "Searching…");
        try
        {
            using var c = Client();
            if (c is null) return;
            status.Text = "Searching…";
            table.Clear();
            list.Items.Clear();
            var found = await c.SearchAsync(TimeSpan.FromSeconds(2));
            foreach (var f in found) ShowRow(f);
            status.Text = $"{found.Count} gateway(s) found.";
        }
        catch (Exception e) when (e is SocketException or ObjectDisposedException or InvalidOperationException)
        {
            status.Text = $"Search failed on this adapter: {e.Message}. Choose another adapter or try again.";
        }
        finally { Busy(false, ""); }
    }

    void OpenWeb()
    {
        if (Selected is not { } f) return;
        try { Process.Start(new ProcessStartInfo($"http://{f.EthIp}:{f.Web}/") { UseShellExecute = true }); }
        catch (System.ComponentModel.Win32Exception e) { status.Text = $"Could not open the browser: {e.Message}"; }
    }

    async Task SetNetwork()
    {
        if (Selected is not { } f) return;
        using var dlg = new NetworkForm(f.Net);
        if (dlg.ShowDialog(this) == DialogResult.OK)
            await Act("set", dlg.Result, "Network settings");
    }

    async Task Act(string op, NetSettings? net, string what)
    {
        if (busy || Selected is not { } f) return;
        Busy(true, "Working…");
        bool sent = false;
        try
        {
            using var c = Client();
            if (c is null) return;
            status.Text = $"{what}: sending…";
            sent = true;  // from here a failure may come after the gateway got the command
            var ack = await c.CommandAsync(op, f.Mac, net, TimeSpan.FromSeconds(10));
            if (ack is null) { status.Text = $"{what}: outcome unknown (no reply). Press Search to check the gateway."; return; }
            if (!ack.Ok)
            {
                status.Text = ack.Err == "bad" ? $"{what}: the gateway refused the settings." : $"{what}: the gateway could not save (flash error).";
                return;
            }
            status.Text = $"{what}: done. Waiting for the gateway to restart (up to 2 minutes)…";
            var back = await c.WaitBackAsync(f.Mac, f.Boot, TimeSpan.FromSeconds(120), TimeSpan.FromSeconds(3));
            if (back is null) { status.Text = $"{what}: the gateway has not come back yet. Press Search later."; return; }
            ShowRow(back);
            status.Text = $"{what}: the gateway is back at {back.EthIp}.";
        }
        catch (Exception e) when (e is SocketException or ObjectDisposedException or InvalidOperationException)
        {
            status.Text = sent ? $"{what}: outcome unknown ({e.Message}). Press Search to check the gateway; nothing was sent again."
                               : $"{what}: the adapter failed ({e.Message}). Nothing was sent.";
        }
        finally { Busy(false, ""); }
    }
}
