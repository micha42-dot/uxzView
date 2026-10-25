# Default image viewer

## Windows 10 and 11

The portable package includes `register-windows.bat`, `register-windows.ps1` and `unregister-windows.bat` beside `uxzView.exe`. Registration is per user and does not need elevation. The batch file automatically uses the executable beside it; its `IXVIEW_EXE` line can instead point to a different installation.

Registration adds the `IxView.Image` ProgID, `Applications\uxzView.exe`, supported image extensions, application capabilities and `RegisteredApplications` under `HKEY_CURRENT_USER\Software`. Executable paths and the image argument are quoted separately, including paths with spaces or non-ASCII characters. It notifies Explorer after importing the generated registry file.

The script registers üxzView as a candidate; it does not change Windows' protected `UserChoice` keys or take over existing defaults. After registration, choose üxzView in **Settings → Apps → Default apps** or **Open with → Choose another app**. The batch file opens Default apps after successful registration. Supported formats still depend on the codecs included in the build.

To generate a `.reg` file without changing the registry:

```powershell
./register-windows.ps1 -ExecutablePath 'C:\Apps\üxzView\uxzView.exe' -GenerateOnly -RegFilePath './uxzView.reg'
```

Unregistration removes only üxzView's own registration and its OpenWithProgids entries. It leaves application preferences, image files, other applications and protected defaults alone. Windows may ask you to select a replacement default afterwards. Re-register after moving the portable folder.

Microsoft documents [application capabilities and registration](https://learn.microsoft.com/en-us/windows/win32/shell/default-programs) and the [Windows 10 default association model](https://techcommunity.microsoft.com/blog/askperf/how-to-configure-file-associations-for-it-pros/1313151).

## Linux

`cmake --install` installs `org.ixview.IxView.desktop` and the application icon. Make sure the installed binary directory is in `PATH`, then update the desktop database if your desktop environment has not refreshed it:

```sh
update-desktop-database ~/.local/share/applications
xdg-mime default org.ixview.IxView.desktop image/jpeg image/png image/webp image/gif image/tiff image/bmp
xdg-mime query default image/png
```

Use the actual desktop-entry directory if you installed system-wide. The file manager can also set the default through Open with. Desktop environments use the [freedesktop MIME application association specification](https://specifications.freedesktop.org/mime-apps-spec/latest/) and [xdg-utils](https://www.freedesktop.org/wiki/Software/xdg-utils/).

## macOS

The application bundle declares the `public.image` document type and receives Finder file-open events. Place `uxzView.app` in Applications and open it once. In Finder, select a representative image, open **Get Info**, select üxzView under **Open with**, then choose **Change All**. Repeat for other file types as needed. See [Apple's instructions](https://support.apple.com/guide/mac-help/mh35597/mac).

The Linux and macOS registrations are prepared in source; native desktop validation is still outstanding.
