// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "CloudUpload.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Client.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "ClockAgent.h"
#include "MicroSdCard.h"
#include "Aegis.h"
#include "dbg_log.h"

namespace
{

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

constexpr const char* TAG                = "CloudUpload";
constexpr const char* kHistoryPath       = "/cloud_history.txt";
constexpr size_t      kResponseMaxLength = 1024;
constexpr size_t      kUploadBufferSize  = 1024;
constexpr uint32_t    kDestroyPollMs     = 10;

// -----------------------------------------------------------------------------
// Types
// -----------------------------------------------------------------------------

using PendingFile = CloudUpload::PendingFile;
using UrlParts    = CloudUpload::UrlParts;

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

extern const uint8_t x509_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

const char* state_name(CloudUpload::State state);
const char* error_name(CloudUpload::Error error);
bool        copy_text(char* dst, size_t dst_size, const char* src);
bool        ends_with_rec(const char* path);
bool        make_child_path(const char* parent, const char* child, char* out, size_t out_size);
void        free_pending_list(PendingFile* head);
bool        append_pending(PendingFile*& head, PendingFile*& tail, const char* path, uint64_t size);
bool        make_single_pending_file(const char* path, PendingFile*& item, char* message, size_t message_size);
bool        read_fs_line(FsFile& file, char* line, size_t line_size);
bool        history_line_matches(const char* line, const char* path);
bool        history_contains(const char* path);
void        format_timestamp(char* buffer, size_t buffer_size);
bool        append_history_line(const char* line);
bool        append_history_success(const char* path);
bool        append_history_error(const char* path, const char* message);
bool        scan_directory(const char* path, PendingFile*& head, PendingFile*& tail, uint32_t& count, uint64_t& bytes);
bool        parse_url(const char* url, UrlParts& out);
uint32_t    timeout_seconds(uint32_t timeout_ms);
bool        configure_secure_client(WiFiClientSecure& client, uint32_t timeout_ms, char* message, size_t message_size);
String      filename_hash(const char* filename, const char* datetime, const char* destination_password);
bool        send_all(Client& client, const uint8_t* data, size_t size);
bool        send_all(Client& client, const char* text);
bool        read_client_line(Client& client, char* line, size_t line_size, uint32_t timeout_ms);
bool        read_body_bytes(Client& client, char* body, size_t body_size, size_t byte_count, uint32_t timeout_ms);
bool        read_response_body(
    Client& client, bool chunked, int content_length, char* body, size_t body_size, uint32_t timeout_ms);
bool parse_upload_ack(const char* response_body, char* message, size_t message_size);

} // namespace

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

CloudUpload::CloudUpload()
{
    m_status.state = State::Idle;
    m_status.error = Error::None;
}

CloudUpload::~CloudUpload()
{
    cancel();

    while (true)
    {
        portENTER_CRITICAL(&m_lock);
        const TaskHandle_t task_handle = m_task_handle;
        portEXIT_CRITICAL(&m_lock);

        if (!task_handle || task_handle == xTaskGetCurrentTaskHandle())
        {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(kDestroyPollMs));
    }
}

bool CloudUpload::start(const cloud_item_t* destination, uint32_t timeout_ms)
{
    portENTER_CRITICAL(&m_lock);
    const bool busy = m_status.state == State::Busy;
    portEXIT_CRITICAL(&m_lock);

    if (busy)
    {
        setStatusMessage(Error::AlreadyBusy, "cloud upload already busy");
        return false;
    }

    if (!copyDestination(destination, timeout_ms))
    {
        portENTER_CRITICAL(&m_lock);
        m_status          = {};
        m_status.state    = State::Error;
        m_status.error    = Error::InvalidArgument;
        m_status.finished = true;
        copy_text(m_status.message, sizeof(m_status.message), "invalid cloud upload destination");
        portEXIT_CRITICAL(&m_lock);
        return false;
    }

    portENTER_CRITICAL(&m_lock);
    m_cancel_requested = false;
    m_status           = {};
    m_status.state     = State::Busy;
    m_status.error     = Error::None;
    copy_text(m_status.destination, sizeof(m_status.destination), m_destination_name);
    portEXIT_CRITICAL(&m_lock);

    if (!startTaskOnCurrentCore())
    {
        finish(State::Error, Error::TaskCreateFailed, "could not create cloud upload task");
        return false;
    }

    return true;
}

bool CloudUpload::uploadSingleFile(const cloud_item_t* destination, const char* path, uint32_t timeout_ms)
{
    portENTER_CRITICAL(&m_lock);
    const bool busy = m_status.state == State::Busy;
    portEXIT_CRITICAL(&m_lock);

    if (busy)
    {
        setStatusMessage(Error::AlreadyBusy, "cloud upload already busy");
        return false;
    }

    if (!copyDestination(destination, timeout_ms))
    {
        portENTER_CRITICAL(&m_lock);
        m_status          = {};
        m_status.state    = State::Error;
        m_status.error    = Error::InvalidArgument;
        m_status.finished = true;
        copy_text(m_status.message, sizeof(m_status.message), "invalid cloud upload destination");
        portEXIT_CRITICAL(&m_lock);
        return false;
    }

    portENTER_CRITICAL(&m_lock);
    m_cancel_requested = false;
    m_task_handle      = xTaskGetCurrentTaskHandle();
    m_status           = {};
    m_status.state     = State::Busy;
    m_status.error     = Error::None;
    copy_text(m_status.destination, sizeof(m_status.destination), m_destination_name);
    portEXIT_CRITICAL(&m_lock);

    return uploadSingleFilePath(path);
}

bool CloudUpload::cancel()
{
    portENTER_CRITICAL(&m_lock);
    const bool busy = m_status.state == State::Busy && m_task_handle != nullptr;
    if (busy)
    {
        m_cancel_requested = true;
    }
    portEXIT_CRITICAL(&m_lock);
    return busy;
}

CloudUpload::Status CloudUpload::status() const
{
    portENTER_CRITICAL(&m_lock);
    const Status value = m_status;
    portEXIT_CRITICAL(&m_lock);
    return value;
}

CloudUpload::Error CloudUpload::error() const
{
    portENTER_CRITICAL(&m_lock);
    const Error value = m_status.error;
    portEXIT_CRITICAL(&m_lock);
    return value;
}

void CloudUpload::setOnCompleteCallback(CompleteCallback callback)
{
    portENTER_CRITICAL(&m_lock);
    m_on_complete = callback;
    portEXIT_CRITICAL(&m_lock);
}

const char* CloudUpload::stateName() const
{
    return state_name(status().state);
}

const char* CloudUpload::errorName() const
{
    return error_name(error());
}

void CloudUpload::taskEntry(void* argument)
{
    CloudUpload* uploader = static_cast<CloudUpload*>(argument);
    if (uploader)
    {
        uploader->taskMain();
    }
    vTaskDelete(nullptr);
}

bool CloudUpload::copyDestination(const cloud_item_t* destination, uint32_t timeout_ms)
{
    if (!destination || !destination->url || destination->url[0] == '\0')
    {
        return false;
    }

    if (!copy_text(m_destination_name, sizeof(m_destination_name), destination->url) ||
        !copy_text(m_destination_url, sizeof(m_destination_url), destination->url))
    {
        return false;
    }

#if BUILD_WITH_SECURITY_LEVEL <= 0
    if (!copy_text(m_destination_password, sizeof(m_destination_password), destination->password))
    {
        return false;
    }
#else
    m_destination_password[0] = '\0';
#endif
    m_timeout_ms = timeout_ms == 0 ? kDefaultTimeoutMs : timeout_ms;
    return true;
}

bool CloudUpload::startTaskOnCurrentCore()
{
    TaskHandle_t     task_handle = nullptr;
    const BaseType_t created =
        xTaskCreatePinnedToCore(taskEntry, "CloudUpload", 8192, this, 1, &task_handle, xPortGetCoreID());
    if (created != pdPASS)
    {
        return false;
    }

    portENTER_CRITICAL(&m_lock);
    m_task_handle = task_handle;
    portEXIT_CRITICAL(&m_lock);
    return true;
}

void CloudUpload::setStatusMessage(Error error, const char* message)
{
    portENTER_CRITICAL(&m_lock);
    m_status.error = error;
    copy_text(m_status.message, sizeof(m_status.message), message ? message : error_name(error));
    portEXIT_CRITICAL(&m_lock);
}

void CloudUpload::finish(State state, Error error, const char* message)
{
    CompleteCallback callback        = nullptr;
    Status           callback_status = {};

    portENTER_CRITICAL(&m_lock);
    m_status.state    = state;
    m_status.error    = error;
    m_status.finished = true;
    m_task_handle     = nullptr;
    copy_text(m_status.message, sizeof(m_status.message), message ? message : error_name(error));
    callback        = m_on_complete;
    callback_status = m_status;
    portEXIT_CRITICAL(&m_lock);

    if (state == State::Error)
    {
        DBG_LOGE(TAG, "%s", callback_status.message);
    }
    else if (state == State::Cancelled)
    {
        DBG_LOGW(TAG, "%s", callback_status.message);
    }
    else
    {
        DBG_LOGI(TAG, "%s", callback_status.message);
    }

    if (callback)
    {
        callback(callback_status);
    }
}

bool CloudUpload::uploadSingleFilePath(const char* path)
{
    if (!MicroSdCard::isReady())
    {
        finish(State::Error, Error::SdNotReady, "microSD is not ready");
        return false;
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        finish(State::Error, Error::WifiNotConnected, "Wi-Fi is not connected");
        return false;
    }

    UrlParts url = {};
    if (!parse_url(m_destination_url, url))
    {
        finish(State::Error, Error::InvalidArgument, "invalid cloud upload URL");
        return false;
    }

    if (!url.https)
    {
        finish(State::Error, Error::InvalidArgument, "cloud upload URL must use https");
        return false;
    }

    char         last_message[kMessageMaxLength] = {};
    PendingFile* item                            = nullptr;
    if (!make_single_pending_file(path, item, last_message, sizeof(last_message)))
    {
        const Error error = strstr(last_message, "allocate") ? Error::AllocationFailed : Error::FileOpenFailed;
        finish(State::Error, error, last_message);
        return false;
    }

    portENTER_CRITICAL(&m_lock);
    m_status.files_total = 1;
    m_status.bytes_total = item->size;
    portEXIT_CRITICAL(&m_lock);

    uint64_t                  committed_bytes = 0;
    const PendingUploadResult result =
        uploadPendingFile(*item, url, committed_bytes, last_message, sizeof(last_message));
    free_pending_list(item);

    switch (result)
    {
    case PendingUploadResult::Succeeded:
        finish(State::Done, Error::None, "cloud upload complete");
        return true;
    case PendingUploadResult::Cancelled:
        finish(State::Cancelled, Error::Cancelled, "cloud upload cancelled");
        return false;
    case PendingUploadResult::FatalError:
        finish(State::Error, Error::HistoryWriteFailed, last_message);
        return false;
    case PendingUploadResult::Failed:
    default:
        finish(State::Error, Error::ServerError, last_message[0] ? last_message : "cloud upload failed");
        return false;
    }
}

CloudUpload::PendingUploadResult CloudUpload::uploadPendingFile(
    PendingFile& item, const UrlParts& url, uint64_t& committed_bytes, char* last_message, size_t last_message_size)
{
    portENTER_CRITICAL(&m_lock);
    const bool cancel_requested = m_cancel_requested;
    portEXIT_CRITICAL(&m_lock);
    if (cancel_requested)
    {
        return PendingUploadResult::Cancelled;
    }

    portENTER_CRITICAL(&m_lock);
    ++m_status.files_started;
    copy_text(m_status.current_file, sizeof(m_status.current_file), item.path);
    m_status.bytes_uploaded = committed_bytes;
    portEXIT_CRITICAL(&m_lock);

    bool file_ok   = false;
    bool cancelled = false;
    for (uint8_t attempt = 0; attempt < kMaxRetries && !file_ok && !cancelled; ++attempt)
    {
        FsFile file;
        if (!file.open(item.path, O_RDONLY))
        {
            copy_text(last_message, last_message_size, "could not open recording file");
            break;
        }

        WiFiClientSecure secure_client;
        if (!configure_secure_client(secure_client, m_timeout_ms, last_message, last_message_size))
        {
            file.close();
            break;
        }

        if (!secure_client.connect(url.host, url.port))
        {
            char tls_error[96] = {};
            file.close();
            if (secure_client.lastError(tls_error, sizeof(tls_error)) != 0 && tls_error[0] != '\0')
            {
                snprintf(last_message, last_message_size, "secure connection failed: %s", tls_error);
            }
            else
            {
                copy_text(last_message, last_message_size, "secure connection failed");
            }
            delay(100);
            continue;
        }

        Client* client = &secure_client;

        m5::rtc_datetime_t upload_datetime = {};
        if (!Clock.getDateTime(&upload_datetime))
        {
            client->stop();
            file.close();
            copy_text(last_message, last_message_size, "clock is not ready for cloud upload");
            break;
        }

        char upload_timestamp[24];
        snprintf(upload_timestamp,
                 sizeof(upload_timestamp),
                 "%04d-%02d-%02d-%02d:%02d:%02d",
                 upload_datetime.date.year,
                 upload_datetime.date.month,
                 upload_datetime.date.date,
                 upload_datetime.time.hours,
                 upload_datetime.time.minutes,
                 upload_datetime.time.seconds);

        const String hash = filename_hash(item.path, upload_timestamp, m_destination_password);

        if (hash.length() == 0)
        {
            client->stop();
            file.close();
            copy_text(last_message, last_message_size, "cloud upload authentication hash failed");
            break;
        }

        const String source_mac = WiFi.macAddress();
        const char*  boundary   = "----TheFlyCloudUploadBoundary";

        char part_filename[CloudUpload::kPathMaxLength + 128];
        char part_timestamp[128];
        char part_hash[128];
        char part_source[128];
        char part_payload[CloudUpload::kPathMaxLength + 160];
        snprintf(part_filename,
                 sizeof(part_filename),
                 "--%s\r\nContent-Disposition: form-data; name=\"filename\"\r\n\r\n%s\r\n",
                 boundary,
                 item.path);
        snprintf(part_timestamp,
                 sizeof(part_timestamp),
                 "--%s\r\nContent-Disposition: form-data; name=\"timestamp\"\r\n\r\n%s\r\n",
                 boundary,
                 upload_timestamp);
        snprintf(part_hash,
                 sizeof(part_hash),
                 "--%s\r\nContent-Disposition: form-data; name=\"hash\"\r\n\r\n%s\r\n",
                 boundary,
                 hash.c_str());
        snprintf(part_source,
                 sizeof(part_source),
                 "--%s\r\nContent-Disposition: form-data; name=\"source_mac\"\r\n\r\n%s\r\n",
                 boundary,
                 source_mac.c_str());
        snprintf(part_payload,
                 sizeof(part_payload),
                 "--%s\r\nContent-Disposition: form-data; name=\"payload\"; filename=\"%s\"\r\nContent-Type: "
                 "application/octet-stream\r\n\r\n",
                 boundary,
                 item.path);

        char end_boundary[48];
        snprintf(end_boundary, sizeof(end_boundary), "\r\n--%s--\r\n", boundary);

        const uint64_t content_length = strlen(part_filename) + strlen(part_timestamp) + strlen(part_hash) +
                                        strlen(part_source) + strlen(part_payload) + item.size + strlen(end_boundary);

        char header[512];
        snprintf(header,
                 sizeof(header),
                 "POST %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nContent-Type: multipart/form-data; "
                 "boundary=%s\r\nContent-Length: %llu\r\n\r\n",
                 url.path,
                 url.host,
                 boundary,
                 static_cast<unsigned long long>(content_length));

        bool ok = send_all(*client, header) && send_all(*client, part_filename) && send_all(*client, part_timestamp) &&
                  send_all(*client, part_hash) && send_all(*client, part_source) && send_all(*client, part_payload);

        uint64_t attempt_bytes = 0;
        uint8_t  buffer[kUploadBufferSize];
        while (ok && attempt_bytes < item.size)
        {
            portENTER_CRITICAL(&m_lock);
            const bool cancel_requested_inner = m_cancel_requested;
            portEXIT_CRITICAL(&m_lock);
            if (cancel_requested_inner)
            {
                ok        = false;
                cancelled = true;
                copy_text(last_message, last_message_size, "cancelled during upload");
                break;
            }

            const size_t to_read    = (item.size - attempt_bytes) < sizeof(buffer)
                                          ? static_cast<size_t>(item.size - attempt_bytes)
                                          : sizeof(buffer);
            const int    bytes_read = file.read(buffer, to_read);
            if (bytes_read <= 0)
            {
                ok = false;
                copy_text(last_message, last_message_size, "recording file read failed");
                break;
            }

            if (!send_all(*client, buffer, static_cast<size_t>(bytes_read)))
            {
                ok = false;
                copy_text(last_message, last_message_size, "network write failed");
                break;
            }

            attempt_bytes += static_cast<uint64_t>(bytes_read);
            portENTER_CRITICAL(&m_lock);
            m_status.bytes_uploaded = committed_bytes + attempt_bytes;
            portEXIT_CRITICAL(&m_lock);
        }

        ok = ok && !cancelled && send_all(*client, end_boundary);
        file.close();

        int  http_status             = 0;
        bool chunked                 = false;
        int  content_length_response = -1;
        char response_body[kResponseMaxLength];
        char line[192];

        if (ok && read_client_line(*client, line, sizeof(line), m_timeout_ms))
        {
            if (strncmp(line, "HTTP/", 5) == 0)
            {
                const char* first_space = strchr(line, ' ');
                http_status             = first_space ? atoi(first_space + 1) : 0;
            }

            while (read_client_line(*client, line, sizeof(line), m_timeout_ms) && line[0] != '\0')
            {
                if (strncasecmp(line, "Content-Length:", 15) == 0)
                {
                    content_length_response = atoi(line + 15);
                }
                else if (strncasecmp(line, "Transfer-Encoding:", 18) == 0 && strstr(line, "chunked"))
                {
                    chunked = true;
                }
            }
        }
        else if (!cancelled)
        {
            ok = false;
            copy_text(last_message, last_message_size, "HTTP response read failed");
        }

        if (ok && (http_status < 200 || http_status >= 300))
        {
            ok = false;
            snprintf(last_message, last_message_size, "HTTP status %d", http_status);
        }

        if (ok && !read_response_body(*client,
                                      chunked,
                                      content_length_response,
                                      response_body,
                                      sizeof(response_body),
                                      m_timeout_ms))
        {
            ok = false;
            copy_text(last_message, last_message_size, "HTTP body read failed");
        }

        client->stop();

        if (cancelled)
        {
            break;
        }

        if (ok)
        {
            char server_message[kMessageMaxLength] = {};
            if (parse_upload_ack(response_body, server_message, sizeof(server_message)))
            {
                file_ok = true;
                break;
            }

            copy_text(last_message, last_message_size, server_message[0] ? server_message : "server returned error");
        }

        portENTER_CRITICAL(&m_lock);
        m_status.bytes_uploaded = committed_bytes;
        portEXIT_CRITICAL(&m_lock);
        delay(100);
    }

    if (cancelled)
    {
        return PendingUploadResult::Cancelled;
    }

    if (file_ok)
    {
        if (!append_history_success(item.path))
        {
            copy_text(last_message, last_message_size, "uploaded but failed to append success history");
            append_history_error(item.path, last_message);
            return PendingUploadResult::FatalError;
        }

        committed_bytes += item.size;
        portENTER_CRITICAL(&m_lock);
        ++m_status.files_succeeded;
        m_status.bytes_uploaded = committed_bytes;
        portEXIT_CRITICAL(&m_lock);
        return PendingUploadResult::Succeeded;
    }

    append_history_error(item.path, last_message);
    portENTER_CRITICAL(&m_lock);
    ++m_status.files_failed;
    m_status.bytes_uploaded = committed_bytes;
    copy_text(m_status.message, sizeof(m_status.message), last_message);
    portEXIT_CRITICAL(&m_lock);
    return PendingUploadResult::Failed;
}

void CloudUpload::taskMain()
{
    if (!MicroSdCard::isReady())
    {
        finish(State::Error, Error::SdNotReady, "microSD is not ready");
        return;
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        finish(State::Error, Error::WifiNotConnected, "Wi-Fi is not connected");
        return;
    }

    UrlParts url = {};
    if (!parse_url(m_destination_url, url))
    {
        finish(State::Error, Error::InvalidArgument, "invalid cloud upload URL");
        return;
    }

    if (!url.https)
    {
        finish(State::Error, Error::InvalidArgument, "cloud upload URL must use https");
        return;
    }

    PendingFile* head        = nullptr;
    PendingFile* tail        = nullptr;
    uint32_t     files_total = 0;
    uint64_t     bytes_total = 0;

    if (!scan_directory("/", head, tail, files_total, bytes_total))
    {
        free_pending_list(head);
        finish(State::Error, Error::ScanFailed, "failed while scanning recording files");
        return;
    }

    portENTER_CRITICAL(&m_lock);
    m_status.files_total = files_total;
    m_status.bytes_total = bytes_total;
    portEXIT_CRITICAL(&m_lock);

    char     last_message[kMessageMaxLength] = {};
    uint64_t committed_bytes                 = 0;

    for (PendingFile* item = head; item; item = item->next)
    {
        const PendingUploadResult result =
            uploadPendingFile(*item, url, committed_bytes, last_message, sizeof(last_message));
        if (result == PendingUploadResult::Cancelled)
        {
            free_pending_list(head);
            finish(State::Cancelled, Error::Cancelled, "cloud upload cancelled");
            return;
        }

        if (result == PendingUploadResult::FatalError)
        {
            free_pending_list(head);
            finish(State::Error, Error::HistoryWriteFailed, last_message);
            return;
        }
    }

    free_pending_list(head);

    if (status().files_failed > 0)
    {
        finish(State::Error, Error::ServerError, "one or more cloud uploads failed");
        return;
    }

    finish(State::Done, Error::None, "cloud upload complete");
}

namespace
{

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

const char* state_name(CloudUpload::State state)
{
    switch (state)
    {
    case CloudUpload::State::Idle:
        return "Idle";
    case CloudUpload::State::Busy:
        return "Busy";
    case CloudUpload::State::Done:
        return "Done";
    case CloudUpload::State::Error:
        return "Error";
    case CloudUpload::State::Cancelled:
        return "Cancelled";
    default:
        return "Unknown";
    }
}

const char* error_name(CloudUpload::Error error)
{
    switch (error)
    {
    case CloudUpload::Error::None:
        return "None";
    case CloudUpload::Error::AlreadyBusy:
        return "AlreadyBusy";
    case CloudUpload::Error::InvalidArgument:
        return "InvalidArgument";
    case CloudUpload::Error::SdNotReady:
        return "SdNotReady";
    case CloudUpload::Error::WifiNotConnected:
        return "WifiNotConnected";
    case CloudUpload::Error::TaskCreateFailed:
        return "TaskCreateFailed";
    case CloudUpload::Error::AllocationFailed:
        return "AllocationFailed";
    case CloudUpload::Error::HistoryOpenFailed:
        return "HistoryOpenFailed";
    case CloudUpload::Error::ScanFailed:
        return "ScanFailed";
    case CloudUpload::Error::FileOpenFailed:
        return "FileOpenFailed";
    case CloudUpload::Error::NetworkConnectFailed:
        return "NetworkConnectFailed";
    case CloudUpload::Error::NetworkWriteFailed:
        return "NetworkWriteFailed";
    case CloudUpload::Error::HttpError:
        return "HttpError";
    case CloudUpload::Error::JsonParseFailed:
        return "JsonParseFailed";
    case CloudUpload::Error::ServerError:
        return "ServerError";
    case CloudUpload::Error::HistoryWriteFailed:
        return "HistoryWriteFailed";
    case CloudUpload::Error::Cancelled:
        return "Cancelled";
    default:
        return "Unknown";
    }
}

bool copy_text(char* dst, size_t dst_size, const char* src)
{
    if (!dst || dst_size == 0)
    {
        return false;
    }

    const char*  value = src ? src : "";
    const size_t chars = strlen(value);
    if (chars >= dst_size)
    {
        return false;
    }

    memcpy(dst, value, chars + 1);
    return true;
}

bool ends_with_rec(const char* path)
{
    if (!path)
    {
        return false;
    }

    const size_t length = strlen(path);
    if (length < 4)
    {
        return false;
    }

    const char* suffix = path + length - 4;
    return suffix[0] == '.' && tolower(static_cast<unsigned char>(suffix[1])) == 'r' &&
           tolower(static_cast<unsigned char>(suffix[2])) == 'e' &&
           tolower(static_cast<unsigned char>(suffix[3])) == 'c';
}

bool make_child_path(const char* parent, const char* child, char* out, size_t out_size)
{
    if (!parent || !child || !out || out_size == 0)
    {
        return false;
    }

    const bool parent_is_root = strcmp(parent, "/") == 0;
    const int  written =
        parent_is_root ? snprintf(out, out_size, "/%s", child) : snprintf(out, out_size, "%s/%s", parent, child);
    return written > 0 && static_cast<size_t>(written) < out_size;
}

void free_pending_list(PendingFile* head)
{
    while (head)
    {
        PendingFile* next = head->next;
        free(head->path);
        free(head);
        head = next;
    }
}

bool append_pending(PendingFile*& head, PendingFile*& tail, const char* path, uint64_t size)
{
    PendingFile* item = static_cast<PendingFile*>(calloc(1, sizeof(PendingFile)));
    if (!item)
    {
        return false;
    }

    item->path = static_cast<char*>(malloc(strlen(path) + 1));
    if (!item->path)
    {
        free(item);
        return false;
    }

    strcpy(item->path, path);
    item->size = size;

    if (tail)
    {
        tail->next = item;
    }
    else
    {
        head = item;
    }
    tail = item;
    return true;
}

bool make_single_pending_file(const char* path, PendingFile*& item, char* message, size_t message_size)
{
    item = nullptr;
    if (!path || path[0] == '\0' || strlen(path) >= CloudUpload::kPathMaxLength)
    {
        copy_text(message, message_size, "invalid cloud upload file path");
        return false;
    }

    FsFile file;
    if (!file.open(path, O_RDONLY))
    {
        copy_text(message, message_size, "could not open recording file");
        return false;
    }

    if (!file.isFile())
    {
        file.close();
        copy_text(message, message_size, "cloud upload path is not a file");
        return false;
    }

    const uint64_t size = file.fileSize();
    file.close();

    PendingFile* head = nullptr;
    PendingFile* tail = nullptr;
    if (!append_pending(head, tail, path, size))
    {
        copy_text(message, message_size, "could not allocate pending upload file");
        return false;
    }

    item = head;
    return true;
}

bool read_fs_line(FsFile& file, char* line, size_t line_size)
{
    if (!line || line_size == 0)
    {
        return false;
    }

    bool   saw_any = false;
    size_t count   = 0;
    while (true)
    {
        const int value = file.read();
        if (value < 0)
        {
            break;
        }

        saw_any = true;
        if (value == '\n')
        {
            break;
        }

        if (count + 1 < line_size && value != '\r')
        {
            line[count++] = static_cast<char>(value);
        }
    }

    line[count] = '\0';
    return saw_any;
}

bool history_line_matches(const char* line, const char* path)
{
    if (!line || !path)
    {
        return false;
    }

    const char*  separator    = strchr(line, ';');
    const size_t token_length = separator ? static_cast<size_t>(separator - line) : strlen(line);
    return strlen(path) == token_length && strncmp(line, path, token_length) == 0;
}

bool history_contains(const char* path)
{
    FsFile history;
    if (!history.open(kHistoryPath, O_RDONLY))
    {
        return false;
    }

    char line[CloudUpload::kPathMaxLength + 64];
    while (read_fs_line(history, line, sizeof(line)))
    {
        if (history_line_matches(line, path))
        {
            history.close();
            return true;
        }
    }

    history.close();
    return false;
}

void format_timestamp(char* buffer, size_t buffer_size)
{
    m5::rtc_datetime_t now = {};
    if (!Clock.getDateTime(&now))
    {
        now.date.year    = 2026;
        now.date.month   = 1;
        now.date.date    = 1;
        now.time.hours   = 0;
        now.time.minutes = 0;
        now.time.seconds = 0;
    }

    snprintf(buffer,
             buffer_size,
             "%04d/%02d/%02d-%02d:%02d:%02d",
             now.date.year,
             now.date.month,
             now.date.date,
             now.time.hours,
             now.time.minutes,
             now.time.seconds);
}

bool append_history_line(const char* line)
{
    FsFile history;
    if (!history.open(kHistoryPath, O_WRONLY | O_CREAT | O_APPEND))
    {
        return false;
    }

    const bool ok = history.println(line) > 0 && history.sync();
    history.close();
    return ok;
}

bool append_history_success(const char* path)
{
    char timestamp[24];
    char line[CloudUpload::kPathMaxLength + 32];
    format_timestamp(timestamp, sizeof(timestamp));
    snprintf(line, sizeof(line), "%s;%s", path ? path : "", timestamp);
    return append_history_line(line);
}

bool append_history_error(const char* path, const char* message)
{
    char timestamp[24];
    char line[CloudUpload::kPathMaxLength + CloudUpload::kMessageMaxLength + 40];
    format_timestamp(timestamp, sizeof(timestamp));
    snprintf(line, sizeof(line), "%s ERROR %s %s", timestamp, path ? path : "", message ? message : "");
    return append_history_line(line);
}

bool scan_directory(const char* path, PendingFile*& head, PendingFile*& tail, uint32_t& count, uint64_t& bytes)
{
    FsFile dir;
    if (!dir.open(path, O_RDONLY))
    {
        return false;
    }

    FsFile child;
    char   child_name[96];
    char   child_path[CloudUpload::kPathMaxLength];

    while (child.openNext(&dir, O_RDONLY))
    {
        child.getName(child_name, sizeof(child_name));
        if (child_name[0] == '\0' || !make_child_path(path, child_name, child_path, sizeof(child_path)))
        {
            child.close();
            continue;
        }

        if (child.isDir())
        {
            const bool ok = scan_directory(child_path, head, tail, count, bytes);
            child.close();
            if (!ok)
            {
                dir.close();
                return false;
            }
            continue;
        }

        if (child.isFile() && ends_with_rec(child_path) && !history_contains(child_path))
        {
            const uint64_t size = child.fileSize();
            if (!append_pending(head, tail, child_path, size))
            {
                child.close();
                dir.close();
                return false;
            }

            ++count;
            bytes += size;
        }

        child.close();
    }

    dir.close();
    return true;
}

bool parse_url(const char* url, UrlParts& out)
{
    if (!url || url[0] == '\0')
    {
        return false;
    }

    const char* cursor = nullptr;
    if (strncmp(url, "https://", 8) == 0)
    {
        out.https = true;
        out.port  = 443;
        cursor    = url + 8;
    }
    else if (strncmp(url, "http://", 7) == 0)
    {
        out.https = false;
        out.port  = 80;
        cursor    = url + 7;
    }
    else
    {
        return false;
    }

    const char* path_start = strchr(cursor, '/');
    const char* host_end   = path_start ? path_start : cursor + strlen(cursor);
    const char* port_start = static_cast<const char*>(memchr(cursor, ':', host_end - cursor));

    const char*  host_limit = port_start ? port_start : host_end;
    const size_t host_len   = host_limit - cursor;
    if (host_len == 0 || host_len >= sizeof(out.host))
    {
        return false;
    }

    memcpy(out.host, cursor, host_len);
    out.host[host_len] = '\0';

    if (port_start)
    {
        const int port = atoi(port_start + 1);
        if (port <= 0 || port > 65535)
        {
            return false;
        }
        out.port = static_cast<uint16_t>(port);
    }

    if (!copy_text(out.path, sizeof(out.path), path_start ? path_start : "/"))
    {
        return false;
    }

    return true;
}

uint32_t timeout_seconds(uint32_t timeout_ms)
{
    return timeout_ms / 1000U + 1U;
}

bool configure_secure_client(WiFiClientSecure& client, uint32_t timeout_ms, char* message, size_t message_size)
{
    if (!Clock.ensureSystemTimeForTls())
    {
        copy_text(message, message_size, "clock is not ready for secure cloud upload");
        return false;
    }

    client.setCACertBundle(x509_crt_bundle_start, static_cast<size_t>(x509_crt_bundle_end - x509_crt_bundle_start));
    // while it looks like we can our own certs from a microSD card, don't do that, it adds another vector for attack
    client.setTimeout(timeout_seconds(timeout_ms));
    client.setHandshakeTimeout(timeout_seconds(timeout_ms));
    return true;
}

String filename_hash(const char* filename, const char* datetime, const char* destination_password)
{
    const uint8_t* auth_key     = nullptr;
    size_t         auth_key_len = 0;
#if BUILD_WITH_SECURITY_LEVEL <= 0
    // minimum security uses the password specified by the user for each cloud endpoint
    const char* safe_password = destination_password ? destination_password : "";
    auth_key                  = reinterpret_cast<const uint8_t*>(safe_password);
    auth_key_len              = strlen(safe_password);
#else
    // higher security uses network-key
    if (!Aegis::isInitialized())
    {
        return "";
    }

    const uint8_t* network_key = Aegis::getNetworkKey();
    if (!network_key)
    {
        return "";
    }
    auth_key     = network_key;
    auth_key_len = Aegis::kNetworkKeySize;
#endif

    if (!auth_key || auth_key_len == 0)
    {
        return "";
    }

    const char* safe_filename = filename ? filename : "";
    const char* safe_datetime = datetime ? datetime : "";

    String input;
    if (!input.reserve(strlen(safe_filename) + strlen(safe_datetime)))
    {
        return "";
    }
    input += safe_filename;
    input += safe_datetime;

    uint8_t digest[Aegis::kSha256Size] = {};
    if (!Aegis::hmacSha256(auth_key,
                           auth_key_len,
                           reinterpret_cast<const uint8_t*>(input.c_str()),
                           input.length(),
                           digest))
    {
        return "";
    }
    static constexpr char kHex[]                            = "0123456789abcdef";
    char                  hex[(Aegis::kSha256Size * 2) + 1] = {};
    for (size_t i = 0; i < Aegis::kSha256Size; ++i)
    {
        hex[i * 2]       = kHex[digest[i] >> 4];
        hex[(i * 2) + 1] = kHex[digest[i] & 0x0F];
    }

    return String(hex);
}

bool send_all(Client& client, const uint8_t* data, size_t size)
{
    size_t written_total = 0;
    while (written_total < size)
    {
        const size_t written = client.write(data + written_total, size - written_total);
        if (written == 0)
        {
            return false;
        }
        written_total += written;
    }
    return true;
}

bool send_all(Client& client, const char* text)
{
    return send_all(client, reinterpret_cast<const uint8_t*>(text), strlen(text));
}

bool read_client_line(Client& client, char* line, size_t line_size, uint32_t timeout_ms)
{
    if (!line || line_size == 0)
    {
        return false;
    }

    const uint32_t started = millis();
    size_t         count   = 0;
    while (millis() - started < timeout_ms)
    {
        while (client.available() > 0)
        {
            const int value = client.read();
            if (value < 0)
            {
                break;
            }
            if (value == '\n')
            {
                line[count] = '\0';
                return true;
            }
            if (count + 1 < line_size && value != '\r')
            {
                line[count++] = static_cast<char>(value);
            }
        }

        if (!client.connected() && client.available() <= 0)
        {
            break;
        }

        delay(1);
    }

    line[count] = '\0';
    return count > 0;
}

bool read_body_bytes(Client& client, char* body, size_t body_size, size_t byte_count, uint32_t timeout_ms)
{
    size_t         count   = strlen(body);
    const uint32_t started = millis();
    while (byte_count > 0 && millis() - started < timeout_ms)
    {
        while (byte_count > 0 && client.available() > 0)
        {
            const int value = client.read();
            if (value < 0)
            {
                break;
            }
            if (count + 1 < body_size)
            {
                body[count++] = static_cast<char>(value);
                body[count]   = '\0';
            }
            --byte_count;
        }

        if (byte_count == 0)
        {
            return true;
        }

        if (!client.connected() && client.available() <= 0)
        {
            break;
        }

        delay(1);
    }
    body[count] = '\0';
    return byte_count == 0;
}

bool read_response_body(
    Client& client, bool chunked, int content_length, char* body, size_t body_size, uint32_t timeout_ms)
{
    body[0] = '\0';

    if (chunked)
    {
        while (true)
        {
            char line[32];
            if (!read_client_line(client, line, sizeof(line), timeout_ms))
            {
                return false;
            }

            const size_t chunk_size = strtoul(line, nullptr, 16);
            if (chunk_size == 0)
            {
                read_client_line(client, line, sizeof(line), timeout_ms);
                return true;
            }

            if (!read_body_bytes(client, body, body_size, chunk_size, timeout_ms))
            {
                return false;
            }
            read_client_line(client, line, sizeof(line), timeout_ms);
        }
    }

    if (content_length >= 0)
    {
        return read_body_bytes(client, body, body_size, static_cast<size_t>(content_length), timeout_ms);
    }

    const uint32_t started = millis();
    size_t         count   = 0;
    while (millis() - started < timeout_ms)
    {
        while (client.available() > 0)
        {
            const int value = client.read();
            if (value < 0)
            {
                break;
            }
            if (count + 1 < body_size)
            {
                body[count++] = static_cast<char>(value);
                body[count]   = '\0';
            }
        }

        if (!client.connected() && client.available() <= 0)
        {
            return true;
        }
        delay(1);
    }

    return true;
}

bool parse_upload_ack(const char* response_body, char* message, size_t message_size)
{
    JsonDocument               doc;
    const DeserializationError error = deserializeJson(doc, response_body ? response_body : "");
    if (error)
    {
        copy_text(message, message_size, error.c_str());
        return false;
    }

    const char* status         = doc["status"].as<const char*>();
    const char* server_message = doc["message"].as<const char*>();
    if (server_message)
    {
        copy_text(message, message_size, server_message);
    }

    return status && strcmp(status, "ok") == 0;
}

} // namespace
