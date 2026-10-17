namespace Sbiot.Finder;

/// <summary>SBIOT's look (the Web UI's colours, stm32/web/src/index.css; the logo from docs/SBIOT.ai, make_icon.py).</summary>
static class Brand
{
    public static readonly Color Blue = Color.FromArgb(0x1f, 0x35, 0xac);    // --color-brand
    public static readonly Color Orange = Color.FromArgb(0xeb, 0x60, 0x01);  // --color-brand-mark
    public static readonly Color Ink = Color.FromArgb(0x1a, 0x22, 0x33);     // --color-ink

    static Stream Res(string name) => typeof(Brand).Assembly.GetManifestResourceStream(name)!;

    public static Icon AppIcon() => new(Res("sbiot.ico"));

    public static Image Logo() => Image.FromStream(Res("sbiot-logo.png"));

    /// <summary>The window's header: the logo on white, the tool's name, an orange rule below (as the Web UI's tabs).</summary>
    public static Control Header()
    {
        var head = new Panel { Dock = DockStyle.Top, Height = 64, BackColor = Color.White, Padding = new Padding(12, 9, 12, 9) };
        var logo = new PictureBox { Image = Logo(), SizeMode = PictureBoxSizeMode.Zoom, Dock = DockStyle.Left, Width = 176 };
        var name = new Label
        {
            Text = "Finder", AutoSize = false, Dock = DockStyle.Left, Width = 120, ForeColor = Blue,
            Font = new Font("Segoe UI", 16f, FontStyle.Bold), TextAlign = ContentAlignment.MiddleLeft, Padding = new Padding(10, 0, 0, 2),
        };
        var rule = new Panel { Dock = DockStyle.Bottom, Height = 3, BackColor = Orange };
        head.Controls.Add(name);  // docked left after the logo: added first, laid out last
        head.Controls.Add(logo);
        var box = new Panel { Dock = DockStyle.Top, Height = head.Height + rule.Height };
        box.Controls.Add(head);
        box.Controls.Add(rule);
        return box;
    }
}
