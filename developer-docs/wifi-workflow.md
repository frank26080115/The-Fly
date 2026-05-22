Wi-Fi is used for administration, for uploading recordings to the cloud, and for using NTP to sync the clock

The user first chooses how to connect: automatic (scan for known SSIDs and automatically connect), connect to specific known SSID, and use a built-in soft-AP.

After the Wi-Fi connection is established, then the user can select an action, such as upload to cloud, and sync with NTP.

The web server for administration and file download is always running in the background.

Cloud upload procedure is to be described in another document.

A file named `cloud_history.txt` tracks which files have been uploaded, one file name per line. Do not upload any files that have been uploaded already. Only add a file to the list after completion of upload.

When Wi-Fi is shutdown by the user, the whole device simply reboots.
