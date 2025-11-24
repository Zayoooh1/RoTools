Why Are There Separate Folders?

The developer version and the production version use separate folders to:

✅ Prevent mixing test data with production data

✅ Avoid accidentally including developer data inside the installer

✅ Allow testing without affecting production data

Data Security
Data is stored locally per user:

✅ Each Windows user has their own %LOCALAPPDATA% folder

✅ Data is encrypted using Windows DPAPI (Data Protection API)

✅ Only the logged-in user can decrypt their data

What does this mean in practice?

No data is copied by the installer

The installer copies only the program files and assets

It does NOT copy any account data files

Each user has completely separate data

User A on PC 1: C:\Users\UserA\AppData\Local\RoTools\

User B on PC 1: C:\Users\UserB\AppData\Local\RoTools\

User A on PC 2: C:\Users\UserA\AppData\Local\RoTools\

Each of these folders is completely independent.

Developer vs Production

Developer: %LOCALAPPDATA%\RoTools_Dev\

Production: %LOCALAPPDATA%\RoTools\

These directories are fully separated.

How to Check Where Your Data Is?

Press Win + R

Type: %LOCALAPPDATA%\RoTools

Click OK

You will see the folder containing AccountData.dat (if you have added any accounts).

Data Backup

If you want to back up your accounts:

Copy the entire %LOCALAPPDATA%\RoTools\ folder

Store it somewhere safe

To restore it, copy it back to the same location

⚠️ IMPORTANT:
The file AccountData.dat is encrypted using your Windows user account key.
If you restore it on a different computer or a different user account, it will not work.

WebView2 Data

Browser-related data (cache, cookies) is also stored separately:

Production: %LOCALAPPDATA%\RoTools\WebView2\

Developer: %LOCALAPPDATA%\RoTools_Dev\WebView2\

Update Settings

The UpdateSettings.txt file is located in the application folder (next to MultiRoblox.exe).
It contains only the last-check timestamp and the selected frequency (Everyday/Weekly/Monthly/Never); it does not store any account data or tokens.
