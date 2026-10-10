# Font Importer (UE 5.5+)

Because importing fonts manually in Unreal Engine sucks.
This imports local `.ttf`/`.otf` families, creates persistent UFontFace assets containing the font bytes, and adds them to a composite UFont with style names.

## Installation

1. Copy the `FontImporter` directory into `<YourProject>/Plugins/`.
2. Make sure the project supports C++ and has a configured Windows compiler toolchain.
3. Regenerate project files if needed, and compile the project's **Editor** target.
4. Launch the editor. Enable **Font Importer** in Edit > Plugins if disabled.
5. Use **Tools > Import Font Family...**.
6. Browse to the disk folder containing TTF/OTF files and select a **/Game/** folder with the integrated Content Browser path picker.
7. Click **Import Font Families**. A result summary displays counts and skipped files.

## Expected example

Disk:
- `Roboto-Regular.ttf`
- `Roboto-Medium.ttf`
- `Roboto-SemiBold.ttf`
- `Roboto-Bold.ttf`
- `Roboto-BoldItalic.ttf`

Destination `/Game/UI/Fonts`:
- `F_Roboto` (UFont, runtime cached composite, names Regular, Medium, SemiBold, Bold, Bold Italic)
- `FF_Roboto_Regular`
- `FF_Roboto_Medium`
- `FF_Roboto_SemiBold`
- `FF_Roboto_Bold`
- `FF_Roboto_BoldItalic`

## Important behavior

- Uses the **filename** to discover family and style, not the font's internal `name` table, style names with non-standard suffixes may need renaming first.
- Scans subfolders too and creates one composite asset per detected family, all within the chosen destination folder.
- A file without a recognized style suffix is treated as Regular.
- Default is **non-destructive**: existing font faces are reused, existing composite fonts retain unrelated entries. New styles are appended.
- Check **Refresh existing...** to update bytes of already imported Font Face assets and refresh matching composite entries. The plugin does not delete old styles that are absent from the scanned folder.
- Font faces use **Inline** loading, embedding the bytes inside the `.uasset` so the source files aren't needed by your packaged game.
- The importer creates Font Face assets **directly**, not using Unreal's font file import factory. Unreal's native Reimport action may not know the source file, rerun this tool with Refresh enabled to update.
- Basic OpenType/TrueType signature validation only. It does not support WOFF/WOFF2, variable font axis extraction, .ttc collections, fonts with unusual internal naming, or overlapping duplicate styles.
- Asset names are sanitized and may conflict with existing assets, collisions are reported.
- Each font file is subject to its own licensing. Importing does not grant commercial-use rights.

