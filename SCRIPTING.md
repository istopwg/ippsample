Scripting with ippserver
========================

`ippserver` supports a simple (unauthenticated and totally insecure) REST
interface for controlling the media and supply levels of printers.  This
interface is provided for scripted testing environments where you want to
control these values for specific tests.

The REST interface is implemented using HTTP GET requests with the (web form)
variables passed in the URL.  For example, if the printer URI is
"ipp://example.local:8501/ipp/print" the corresponding REST URLs are
"http://example.local:8501/ipp/print/media" for media requests and
"http://example.local:8501/ipp/print/supplies" for supply requests.  For "ipps"
printer URIs use "https" for the REST URLs.

Setting media and supply levels directly affects the corresponding printer
attributes ("media-ready", "printer-supply", etc.) and indirectly affects the
"printer-state-reasons" attribute.


Controlling Media
-----------------

`ippserver` printers typically have a single media source called `main`, but can
be configured with any number of sources using the "media-source-supported"
attribute.

The following variables are supported (where N starts at 0):

- `sizeN`: The PWG media size name for the source, e.g., `na_letter_8.5x11in`.
- `typeN`: The PWG media type name for the source, e.g., `stationery`.
- `levelN`: The number of sheets remaining in the tray from 0 to 250.

Omitting the size for a source will remove it from the "media-ready" and
"media-col-ready" attributes.  Setting the level to 0 will trigger the
`media-empty` and (if a job is printing) `media-needed` state reasons.  Setting
the level from 1 to 25 will trigger the `media-low` state reason.

For example, the following `curl` command will unload all media from the `main`
tray and trigger the `media-empty` and `media-needed` state reasons:

    curl "http://example.local:8501/ipp/print/media?level0=0" >/dev/null

While the following command loads 20 sheets of US Letter media and triggers the
`media-low` state reason:

    curl "http://example.local:8501/ipp/print/media?size0=na_letter_8.5x11in&level0=20" >/dev/null


Controlling Supply Levels
-------------------------

`ippserver` printers typically have two or five marker supplies: waste toner,
black toner, cyan toner, magenta toner, and yellow toner.  The color toners are
only available when the printer supports color printing.

The following variables are supported:

- `level0`: The level for the waste toner bin, from 0 (empty) to 100 (full).
- `level1`: The level for the black toner, from 0 (empty) to 100 (full).
- `level2`: The level for the cyan toner, from 0 (empty) to 100 (full).
- `level3`: The level for the magenta toner, from 0 (empty) to 100 (full).
- `level4`: The level for the yellow toner, from 0 (empty) to 100 (full).

Setting the waste toner level (`level0`) to 100 will trigger the
`marker-waste-full` state reason, while setting it to 91-99 will trigger the
`marker-waste-almost-full` state reason.

Setting any of the toner levels (`level1` to `level4`) to 0 will trigger the
`toner-empty` state reason, while setting it to 1-9 will trigger the
`toner-low` state reason.

For example, the following `curl` command will set the toner levels to 0 and
trigger the `toner-empty` state reason:

    curl "http://example.local:8501/ipp/print/supplies?level1=0&level2=0&level3=0&level4=0" >/dev/null
