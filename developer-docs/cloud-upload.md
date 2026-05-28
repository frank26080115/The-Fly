## File Upload

parameter ["action"] should be "upload"

other fields expected:

 * `date-time`
 * `file-name`
 * `mac-addr`
 * `auth-hash`

then followed by file content

## Enrollment

parameter ["action"] should be "enroll"

 * `date-time`
 * `filecrypt-key` (encrypted by network-key)
 * `network-key` (just a hash)
 * `mac-addr`

details are in `lib\Security\enrollment-mechanism.md`

## Environment

 * DB username
 * DB password
 * FS file storage directory

## Backend Scripts

 * Set Network Password (inserts a new network-key)
 * Accept Enrollment
 * Upload/Enrollment Flask Server
 * Decoder
 * AI Processing

## Databases

### enrollment

 * UUID
 * date-time
 * filecrypt-key (encrypted by network-key)
 * network-key (just a hash)
 * bdaddr
 * accepted boolean
 * accepted-date-time
 * accepted-by-user UUID of user
