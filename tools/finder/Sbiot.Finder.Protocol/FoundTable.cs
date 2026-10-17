namespace Sbiot.Finder.Protocol;

/// <summary>The window's rows: one per MAC, in the order first seen; a later reply replaces its row.</summary>
public sealed class FoundTable
{
    readonly List<Found> rows = new();
    public IReadOnlyList<Found> Rows => rows;

    public void Merge(Found f)
    {
        int i = rows.FindIndex(r => r.Mac == f.Mac);
        if (i >= 0) rows[i] = f; else rows.Add(f);
    }

    public void Clear() => rows.Clear();
}
