Thank you for r2 (APPROVED through 6e2eb47). One more change since, at the user's report (the Search button turned black while searching: WinForms draws a disabled FlatStyle.Flat button with a custom BackColor dark): commit 9492db4 (`git diff 6e2eb47..9492db4 -- stm32/tools/finder`).

MainForm no longer sets search.Enabled or the form's Enabled. A `busy` flag and Busy(on, label): Search shows "Searching…" / "Working…" on a lighter brand blue (Brand.BlueBusy #5d6cc4) with the wait cursor; the adapter list and the Network/Restart/Factory buttons are disabled while busy (system-styled, so they gray normally); Search() and Act() return at once when busy; the list's selection handler keeps the action buttons off while busy; finally { Busy(false) } restores. Search has a minimum width so the label change does not move the other buttons. Builds with 0 warnings; dotnet test 32/32.

Reply with findings ranked (Critical / Important / Minor), then a verdict: APPROVED or CHANGES NEEDED.
