# Portable mode

üxzView can keep its settings and application data beside the executable instead of in your user profile. The normal user-profile mode remains the default.

## Enable it

Open **Options → Settings → Storage locations**, enable **Use portable mode (after restart)** and confirm with OK. On first activation, üxzView copies the current settings and application data into `IxView-data` beside the application. Copying runs in the background and can be cancelled. The old user profile is retained. Restart üxzView to use the portable copy.

The copy includes settings and screenshot shortcuts, resize/color presets, batch profiles saved in the default profile folder, ratings and metadata catalog, installed extensions and their preferences, and the file journal including its internal trash/backups. Image and watermark files outside the application data folder are referenced, not copied. Profiles explicitly saved elsewhere also remain external.

An existing portable profile is resumed rather than overwritten. Disabling portable mode switches back to the original user profile after restart; it does not merge changes back or delete either profile.

For a new, separate portable profile, start üxzView once with `--portable`, or create an empty file named `portable.mode` beside the executable. This activation route starts a fresh profile and does not import normal-mode settings. The marker keeps portable mode enabled on later launches.

## Files and moving the app

```text
uxzView.exe                 # Windows; uxzView on Linux
portable.mode
tools/                      # Included QR/OCR tools and language models
IxView-data/
  settings/IxView/IxView.ini
  profiles/
  catalog.json
  extensions/
  file-journal/             # Includes the internal trash/backups
  cache/                    # Temporary batch-preview files
  location.json
```

On macOS, `tools`, `portable.mode` and `IxView-data` live beside `uxzView.app`, outside its signed bundle. On Linux, they live beside the actual executable. Native Linux/macOS runs still need validation. The former IxView storage identifiers are deliberately retained, so existing profiles keep working after the rename to üxzView.

Close üxzView, then move or copy the application **together with** `portable.mode` and the complete `IxView-data` and `tools` folders. Keep `location.json`: it lets üxzView update references to files inside the old application folder, including journal backups and recent images carried with the folder. External image paths stay external. File undo still checks content hashes before changing a file.

The destination must be writable. If a requested portable profile cannot be used, startup reports an error and does not silently switch to the user profile. A protected installation such as Program Files may therefore need to be copied to a writable folder before enabling portable mode.

For storage diagnostics, `uxzView.exe --storage-info` prints the active mode, settings filename, data folder, cache folder and tool folder as JSON. On Linux/macOS, use the corresponding executable path.

Windows default-app registration belongs to Windows and must be repeated after moving the executable. Native file dialogs and the operating system may retain their own recent-file information; portable mode controls üxzView-managed data, not operating-system records.

## Updating and packaging

Replace program files while retaining `portable.mode`, `IxView-data` and your installed `tools` folder. The project's Windows deployment script preserves these entries when rebuilding `dist/uxzView`. Generated binary and source archives exclude portable profiles, their marker and preparation files. Personal profiles are never included as release defaults.

The default tools folder lives beside the app in both profile modes. Enabling portable mode does not duplicate it into `IxView-data`. External custom tool folders are copied during first-time profile import. Release archives contain verified factory tool defaults from a separate build snapshot, without personal activation state or additional installed packs.
