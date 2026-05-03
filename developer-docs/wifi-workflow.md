There shall be a `wifi.json` file on the microSD card.

 * a timezone is specified as a TZ string
 * three NTP servers listed
 * a list of station SSIDs and passwords
 * a SSID and password to use if enabling access point mode
 * a list of cloud upload URLs and associated username and password

If the user chooses to launch the access point mode, then only FTP is enabled, as there is no internet to do much else anyways.

Otherwise, the user can choose any one of the Wi-Fi routers to connect to. When successful, the FTP server starts to run, and also, the user is presented with the option to sync time with NTP, and to pick a cloud upload destination (up to 2).

A file named `upload-history.txt` tracks which files have been uploaded, one file name per line. Do not upload any files that have been uploaded already. Only add a file to the list after completion of upload.

The upload is a simple HTTPS form submission, it includes the file, and another field for the file name itself (even if it is redundant), the username, and a hash.

The password is never sent as plaintext. The hash is a SHA-1 hash over the combination of the username and file name.

(the username is used more like an identifier of this particular The Fly device)

while uploading, show the uploading file name, and then two progress bars. The first progress bar is the upload progress of the current file. The second progress bar is the overall progress for all files.
