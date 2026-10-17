**Critical:** None found.

**Important**

1. **The IO conflict baseline stays stale after saving.**  
   `stm32/web/src/pages/Io.tsx:237` — successful Save reloads only `f`; the separate `saved.data` used by `ioAddressClash` remains unchanged.  
   **Scenario:** start with an existing collision at IO address 100, fix it to 101, save and choose **Continue editing**. Changing back to 100 is then accepted because the stale baseline makes `addr === current`, reintroducing the routing conflict.  
   **Fix:** update the saved baseline after every successful save, or reload it alongside the form and block Save until that reload completes. Add a save→continue→restore-conflicting-address test.

**Minor**

2. **“Turn off the event” is not available for an event with a missing point.**  
   `stm32/web/src/lib/edgeModel.ts:219` — the dialog still requires a valid point/output when `enable` is false. After CSV removes `Temperature`, unticking **Active** cannot pass Done, despite the page suggesting this remedy.  
   **Fix:** skip operational-field validation for disabled events, or provide a direct deactivate action. Test the dialog path, not only `linkageErrors()`.

No additional blocking finding in the store-progress change. Its FreeRTOS integration has not been verified on hardware in this review.

**Verdict: CHANGES NEEDED.**