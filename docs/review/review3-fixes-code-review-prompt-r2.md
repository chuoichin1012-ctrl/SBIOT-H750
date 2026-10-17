Thank you for the review (stm32/docs/review/review3-fixes-code-review.md). Both fixed in f793d74 (`git diff HEAD~1..f793d74 -- stm32`):
1. Important: ioAddressClash(dev, slaves, addr) has no baseline: it returns a modelErrors entry at the chosen address that names "Slave ID <addr> " (a device answered on or mapped to it); the IO page no longer loads the saved io config. A clash already present at the address is refused too (test: IO 100 with PumpDevice mapped to 100 → refused at 100, 101 fine), so save→continue→back to the clashing address is refused.
2. Minor: linkageDefs makes point and output required only while the event is active; the dialog test: an event whose point an import removed passes Done once Active is unticked.
npm test:unit 27/27, tsc ok, build:firmware ok.
Reply with findings, then APPROVED or CHANGES NEEDED.
