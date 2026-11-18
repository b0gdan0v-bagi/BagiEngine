# Art generation: Accept copies the whole 1024x1024 image (no trim to the object, no target size)

- Version: 0.2.8
- Entry: artgen-accept-trim
- Date: 2026-11-17
- Bump: patch

## Added
- `Widgets/ArtGen/ArtGenImage.h`: the image Accept settings (`trim`, `padding`, `maxSize`), the drawn bounds, one frame and scale for a set, premultiplied box-filter downscale, PNG read and write.
- A preset's `accept` key holds those settings (the Python service ignores it); the Generate panel edits them in Settings and beside Accept, and shows the drawn and the written size; `artgen_accept` takes an optional `image` override and answers `processing`.
- An image set keeps one frame and one scale, measured over every variant at its first Accept all and stored in the manifest as `processing`; a switch, Accept all again and Keep write through it; `artgen_enqueue` takes `frameOf: <set>` to give a replacement set the frame of an older one.

## Changed
- An accepted image is trimmed to its drawn box with 2 % padding and downscaled to at most 256 px by default, instead of copied as the generator's full frame; its `.artgen.json` gets an `accepted` record (source, crop, scale, size). Adapt: nothing for sprites already accepted; a game that wants full frames sets `"accept": {"trim": false, "maxSize": 0}` in its preset (copied byte for byte, as before).
- A set accepted before this change, or with those identity settings, keeps writing full frames. Adapt: nothing.
