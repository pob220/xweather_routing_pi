# Weather Routing 2.3.1 development

This update retains the capabilities and UI improvements of [2.3.0](RELEASE-NOTES-2.3.0.md).

- **Endpoint marks follow the visibility eye.** A visible weather routing shows its blue departure triangle and red destination cross before, during and after calculation, even when another routing is selected or no row is selected. Turning the eye off hides the track and both marks. This also works for Weather Routing positions that are not OpenCPN waypoint objects.
- **Selection remains for inspection.** Isochrone and other selected-route details keep their existing behaviour. Endpoint marks are drawn once, after geometry, so these details cannot cover them. No chart waypoint objects are created.
- **Routing methods replaces the ambiguous Engine title.** Settings pages, method selection, Auto/All membership, reset confirmation and related help now describe routing methods. Quick, Quick2, Atlas, Standard, Alternative and Professional retain their names and behaviour. Boat motoring controls remain separate.

The routing kernels, automatic adaptive policies, search allowances, saved method identifiers and user preferences are unchanged. The queued/running-settings guard from 2.3.0 remains active.

See [qualification](QUALIFICATION-2.3.1.md) and [Windows import instructions](TRY-WINDOWS.md). This is a developer test, not an alpha or catalogue publication.
