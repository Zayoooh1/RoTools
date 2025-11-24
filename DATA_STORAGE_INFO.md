Where Are Account Data Stored?

The application stores account data in the user’s Windows profile folder:

Production Version (installed):
%LOCALAPPDATA%\RoTools\
  └── AccountData.dat

Why This Folder?

The production build uses a dedicated folder to ensure:

✅ Your account data stays separate from program files

✅ The installer does not overwrite or copy user data

✅ Each Windows user has isolated and independent storage

Data Security
Data is stored locally per Windows user:

✅ Each user has their own %LOCALAPPDATA% path

✅ Data is encrypted using Windows DPAPI (Data Protection API)

✅ Only the currently logged-in user can decrypt the data

What does this mean in practice?

The installer does NOT include or transfer user data

It only installs program files and assets

It does NOT copy AccountData.dat

Each user has their own separate dataset

User A: C:\Users\UserA\AppData\Local\RoTools\

User B: C:\Users\UserB\AppData\Local\RoTools\

Different PCs = fully separate data as well

Each folder is independent and not shared across users.

How to Check Where Your Data Is?

Press Win + R

Type: %LOCALAPPDATA%\RoTools

Click OK

You will see the folder containing AccountData.dat (if you have added accounts).

Data Backup

If you want to back up your accounts:

Copy the entire %LOCALAPPDATA%\RoTools\ folder

Store it somewhere safe

To restore, copy it back to the same path

⚠️ IMPORTANT:
AccountData.dat is encrypted using your Windows account key.
Restoring it on a different PC or a different Windows user account will not work.

WebView2 Data

Browser-related data (cache, cookies) is stored here:

%LOCALAPPDATA%\RoTools\WebView2\

Update Settings

The UpdateSettings.txt file is located in the application directory (next to MultiRoblox.exe).
It contains only the last update check timestamp and the selected frequency (Everyday/Weekly/Monthly/Never).
It does not store any account data or tokens.
