Implementation of the FTP server

Publicly, it should have a start function, a stop function, and a polling function if required.

Internally, if possible, both disks are served as root directories, one being `on-board` and the other being `microSD`. If the FTP server implementation can only use one, then only `microSD` will be served.
