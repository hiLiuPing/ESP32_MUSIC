#include "app/file_manager_network.h"

#include <DNSServer.h>
#include <FS.h>
#include <WebServer.h>
#include <WiFi.h>

#include <cctype>

#include "bsp/bsp_littlefs.h"
#include "bsp/bsp_storage.h"
#include "app/player_app.h"
#include "task/task_system.h"

namespace {
constexpr char AP_SSID[] = "DuduFiles";
constexpr uint16_t DNS_PORT = 53U;
const IPAddress AP_IP(192, 168, 1, 1);
const IPAddress AP_GATEWAY(192, 168, 1, 1);
const IPAddress AP_NETMASK(255, 255, 255, 0);

WebServer server(80);
DNSServer dns_server;
volatile bool ap_active = false;
bool routes_registered = false;
String ap_message;

File upload_file;
fs::FS *upload_filesystem = nullptr;
bool upload_sd_target = false;
bool upload_flash_lock = false;
bool upload_failed = false;
int upload_status = 500;
String upload_error;
String upload_final_path;
String upload_partial_path;
String upload_backup_path;
size_t upload_written = 0U;
bool upload_music_target = false;

const char PAGE_STYLE[] PROGMEM = R"CSS(
*{box-sizing:border-box}body{margin:0;background:#f4f6f8;color:#1f2937;font-family:Arial,sans-serif;padding:16px}
main{max-width:760px;margin:0 auto}h1{font-size:24px;margin:8px 0 14px}.tabs,.bar{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin:10px 0}
a.btn,button{border:0;border-radius:8px;padding:10px 12px;background:#e5e7eb;color:#111827;font-size:15px;text-decoration:none;display:inline-block}button:disabled{cursor:not-allowed;opacity:.55}
a.active{background:#2563eb;color:white}.path{font-family:monospace;background:white;border:1px solid #d1d5db;border-radius:8px;padding:10px;flex:1;min-width:180px}
.panel{background:white;border:1px solid #e5e7eb;border-radius:8px;padding:12px;margin:12px 0}.error{color:#b91c1c}.muted{color:#6b7280}
table{width:100%;border-collapse:collapse;background:white;border-radius:8px;overflow:hidden}th,td{text-align:left;border-bottom:1px solid #e5e7eb;padding:10px;font-size:14px}th{background:#f9fafb}
td.actions{white-space:nowrap;text-align:right}.danger{background:#dc2626;color:white}input[type=file]{max-width:100%}
form.inline{display:inline}@media(max-width:560px){th:nth-child(3),td:nth-child(3){display:none}td.actions{white-space:normal}}
.upload-status{display:block;margin-top:8px;min-height:18px}
)CSS";

struct FsTarget {
    fs::FS *filesystem = nullptr;
    bool is_sd = false;
    bool is_flash = false;
};

class LittleFsLockGuard {
public:
    explicit LittleFsLockGuard(bool enabled) : enabled_(enabled) {
        locked_ = !enabled_ || bsp_littlefs_lock(pdMS_TO_TICKS(2000U));
    }
    ~LittleFsLockGuard() {
        if (enabled_ && locked_) bsp_littlefs_unlock();
    }
    bool locked() const { return locked_; }

private:
    bool enabled_ = false;
    bool locked_ = false;
};

String json_escape(const String &value) {
    String out;
    out.reserve(value.length() + 8U);
    for (size_t i = 0; i < value.length(); ++i) {
        const char c = value[i];
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<uint8_t>(c) < 0x20U) {
            out += ' ';
        } else {
            out += c;
        }
    }
    return out;
}

String html_escape(const String &value) {
    String out = value;
    out.replace("&", "&amp;");
    out.replace("<", "&lt;");
    out.replace(">", "&gt;");
    out.replace("\"", "&quot;");
    out.replace("'", "&#39;");
    return out;
}

String url_encode(const String &value) {
    static constexpr char HEX_DIGITS[] = "0123456789ABCDEF";
    String out;
    out.reserve(value.length() + 8U);
    for (size_t i = 0; i < value.length(); ++i) {
        const uint8_t c = static_cast<uint8_t>(value[i]);
        const bool safe = std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
        if (safe) {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += HEX_DIGITS[c >> 4U];
            out += HEX_DIGITS[c & 0x0FU];
        }
    }
    return out;
}

String base_name(const String &path) {
    const int slash = path.lastIndexOf('/');
    return slash >= 0 ? path.substring(slash + 1) : path;
}

String parent_path(const String &path) {
    if (path == "/") return "/";
    const int slash = path.lastIndexOf('/');
    if (slash <= 0) return "/";
    return path.substring(0, slash);
}

String join_path(const String &directory, const String &name) {
    if (directory == "/") return "/" + name;
    return directory + "/" + name;
}

String page_url(const String &fs_arg, const String &path) {
    return "/?fs=" + url_encode(fs_arg) + "&path=" + url_encode(path);
}

String file_url(const String &fs_arg, const String &path) {
    return "/api/file?fs=" + url_encode(fs_arg) + "&path=" + url_encode(path);
}

String current_fs_arg() {
    const String fs_arg = server.arg("fs");
    return fs_arg == "flash" ? "flash" : "sd";
}

const char *method_name(HTTPMethod method) {
    switch (method) {
        case HTTP_GET: return "GET";
        case HTTP_POST: return "POST";
        case HTTP_DELETE: return "DELETE";
        case HTTP_PUT: return "PUT";
        default: return "OTHER";
    }
}

void log_request(const char *handler, int status, const String &fs_arg,
                 const String &path, const String &detail = "") {
    Serial.printf("[FILE_MANAGER] %s %s uri=%s fs=%s path=%s status=%d",
                  method_name(server.method()), handler, server.uri().c_str(),
                  fs_arg.c_str(), path.c_str(), status);
    if (!detail.isEmpty()) Serial.printf(" detail=%s", detail.c_str());
    Serial.println();
}

bool has_parent_segment(const String &path) {
    int start = 0;
    while (start < static_cast<int>(path.length())) {
        int end = path.indexOf('/', start);
        if (end < 0) end = path.length();
        if (end > start && path.substring(start, end) == "..") return true;
        start = end + 1;
    }
    return false;
}

bool normalize_path(const String &raw, String &normalized, bool allow_root) {
    String path = raw;
    path.trim();
    if (path.isEmpty()) return false;
    // Arduino String is NUL-terminated, so indexOf('\0') always finds the terminator.
    if (path.indexOf('\\') >= 0) return false;
    if (!path.startsWith("/")) path = "/" + path;

    String out = "/";
    int start = 1;
    while (start < static_cast<int>(path.length())) {
        int end = path.indexOf('/', start);
        if (end < 0) end = path.length();
        const String part = path.substring(start, end);
        if (part == "..") return false;
        if (!part.isEmpty() && part != ".") {
            if (out.length() > 1U) out += "/";
            out += part;
        }
        start = end + 1;
    }
    if (!allow_root && out == "/") return false;
    normalized = out;
    return !has_parent_segment(normalized);
}

bool normalize_upload_name(const String &raw, String &name) {
    String candidate = raw;
    candidate.trim();
    candidate.replace("\\", "/");
    candidate = base_name(candidate);
    if (candidate.isEmpty() || candidate == "." || candidate == "..") return false;
    if (candidate.indexOf('/') >= 0) return false;
    name = candidate;
    return true;
}

bool select_target_by_name(const String &fs_arg, FsTarget &target, String &error, int &status) {
    if (fs_arg == "sd") {
        if (!(bsp_storage_available() || bsp_storage_init())) {
            error = "SD unavailable";
            status = 503;
            return false;
        }
        target.filesystem = &bsp_storage_fs();
        target.is_sd = true;
        return true;
    }
    if (fs_arg == "flash") {
        if (!bsp_littlefs_available()) {
            error = "Flash unavailable";
            status = 503;
            return false;
        }
        target.filesystem = &bsp_littlefs_fs();
        target.is_flash = true;
        return true;
    }
    error = "Invalid fs";
    status = 400;
    return false;
}

bool select_target(FsTarget &target, String &error, int &status) {
    return select_target_by_name(server.arg("fs"), target, error, status);
}

void send_error(int status, const String &message) {
    server.send(status, "text/plain", message);
}

void redirect_to_page(const String &fs_arg, const String &path) {
    server.sendHeader("Location", page_url(fs_arg, path));
    server.send(303, "text/plain", "");
}

void notify_sd_changed(bool changed) {
    if (changed) (void)task_post_player_command(PlayerCommandType::Rescan);
}

bool has_audio_extension(const String &path) {
    String lower = path;
    lower.toLowerCase();
    return lower.endsWith(".mp3") || lower.endsWith(".aac") ||
           lower.endsWith(".m4a") || lower.endsWith(".wav") ||
           lower.endsWith(".flac");
}

bool is_music_audio_path(const String &path) {
    return path.startsWith("/music/") && has_audio_extension(path);
}

String megabytes(size_t bytes) {
    char text[32] = {};
    std::snprintf(text, sizeof(text), "%.2f MB",
                  static_cast<double>(bytes) / (1024.0 * 1024.0));
    return String(text);
}

void send_storage_usage(const String &fs_arg) {
    FsTarget target;
    String error;
    int status = 200;
    if (!select_target_by_name(fs_arg, target, error, status)) {
        server.sendContent("<div class='panel error'>" + html_escape(error) + "</div>");
        return;
    }

    LittleFsLockGuard lock(target.is_flash);
    if (!lock.locked()) {
        server.sendContent(F("<div class='panel error'>Flash busy</div>"));
        return;
    }

    const char *label = target.is_sd ? "SD" : "Flash";
    const size_t total = target.is_sd ? bsp_storage_total_bytes()
                                      : bsp_littlefs_total_bytes();
    const size_t used = target.is_sd ? bsp_storage_used_bytes()
                                     : bsp_littlefs_used_bytes();
    server.sendContent("<div class='panel'><strong>" + String(label) +
                       "</strong>: Used " + megabytes(used) +
                       " / Total " + megabytes(total) + "</div>");
}

bool wait_for_player_storage_release(uint32_t timeout_ms) {
    const uint32_t started = millis();
    while ((millis() - started) < timeout_ms) {
        PlayerStatus player_status = {};
        if (player_app_get_status(&player_status) &&
            player_status.state != PlayerState::Playing &&
            player_status.state != PlayerState::Paused) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
    return false;
}

const char *content_type_for(const String &path) {
    String lower = path;
    lower.toLowerCase();
    if (lower.endsWith(".html") || lower.endsWith(".htm")) return "text/html";
    if (lower.endsWith(".css")) return "text/css";
    if (lower.endsWith(".js")) return "application/javascript";
    if (lower.endsWith(".json")) return "application/json";
    if (lower.endsWith(".png")) return "image/png";
    if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) return "image/jpeg";
    if (lower.endsWith(".gif")) return "image/gif";
    if (lower.endsWith(".svg")) return "image/svg+xml";
    if (lower.endsWith(".txt") || lower.endsWith(".log")) return "text/plain";
    if (lower.endsWith(".mp3")) return "audio/mpeg";
    if (lower.endsWith(".aac")) return "audio/aac";
    if (lower.endsWith(".m4a")) return "audio/mp4";
    if (lower.endsWith(".wav")) return "audio/wav";
    if (lower.endsWith(".flac")) return "audio/flac";
    if (lower.endsWith(".epub")) return "application/epub+zip";
    return "application/octet-stream";
}

void send_page_begin(const String &fs_arg, const String &path) {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");
    server.sendContent(F("<!doctype html><html lang='en'><head><meta charset='utf-8'>"
                         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                         "<title>Dudu File Manager</title><style>"));
    server.sendContent_P(PAGE_STYLE);
    server.sendContent(F("</style></head><body><main><h1>Dudu File Manager</h1>"));

    server.sendContent(F("<div class='tabs'>"));
    server.sendContent("<a class='btn " + String(fs_arg == "sd" ? "active" : "") +
                       "' href='" + page_url("sd", "/") + "'>SD</a>");
    server.sendContent("<a class='btn " + String(fs_arg == "flash" ? "active" : "") +
                       "' href='" + page_url("flash", "/") + "'>Flash</a>");
    server.sendContent(F("<a class='btn' href='/diag'>Diag</a></div>"));
    send_storage_usage(fs_arg);

    server.sendContent(F("<div class='bar'>"));
    server.sendContent("<a class='btn' href='" + page_url(fs_arg, parent_path(path)) + "'>Up</a>");
    server.sendContent("<div class='path'>" + html_escape(path) + "</div>");
    server.sendContent("<a class='btn' href='" + page_url(fs_arg, path) + "'>Refresh</a>");
    server.sendContent(F("</div>"));

    server.sendContent("<div class='panel'><form id='upload-form' method='post' "
                       "enctype='multipart/form-data' action='/api/upload?redirect=1&fs=" +
                       url_encode(fs_arg) + "&path=" + url_encode(path) + "'>"
                       "<input id='upload-file' name='file' type='file'> "
                       "<button id='upload-button' type='submit'>Upload</button>"
                       "<span id='upload-status' class='upload-status' aria-live='polite'></span>"
                       "</form><script>(function(){const f=document.getElementById('upload-form'),"
                       "i=document.getElementById('upload-file'),b=document.getElementById('upload-button'),"
                       "s=document.getElementById('upload-status');let busy=false;"
                       "f.addEventListener('submit',function(e){if(busy){e.preventDefault();return}"
                       "if(!i.files.length){e.preventDefault();s.textContent='Choose a file first';return}"
                       "busy=true;b.disabled=true;s.textContent='Uploading...'})})()</script></div>");
}

void send_page_end() {
    server.sendContent(F("</main></body></html>"));
    server.sendContent("");
}

void send_error_panel(const String &message) {
    server.sendContent("<div class='panel error'>" + html_escape(message) + "</div>");
}

void send_directory_table(const String &fs_arg, const String &path, File &directory) {
    server.sendContent(F("<table><thead><tr><th>Name</th><th>Size</th><th>Type</th><th></th>"
                         "</tr></thead><tbody>"));
    uint32_t count = 0U;
    while (true) {
        File entry = directory.openNextFile(FILE_READ);
        if (!entry) break;
        const String name = base_name(String(entry.name()));
        const String child_path = join_path(path, name);
        const bool is_dir = entry.isDirectory();
        server.sendContent(F("<tr><td>"));
        if (is_dir) {
            server.sendContent("<a href='" + page_url(fs_arg, child_path) + "'>" +
                               html_escape(name) + "</a>");
        } else {
            server.sendContent(html_escape(name));
        }
        server.sendContent(F("</td><td>"));
        server.sendContent(is_dir ? "-" : megabytes(entry.size()));
        server.sendContent(F("</td><td>"));
        server.sendContent(is_dir ? "Dir" : "File");
        server.sendContent(F("</td><td class='actions'>"));
        if (!is_dir) {
            server.sendContent("<a class='btn' href='" + file_url(fs_arg, child_path) +
                               "'>Download</a> ");
            server.sendContent("<form class='inline' method='post' action='/delete'>"
                               "<input type='hidden' name='fs' value='" + html_escape(fs_arg) + "'>"
                               "<input type='hidden' name='path' value='" + html_escape(child_path) + "'>"
                               "<input type='hidden' name='return' value='" + html_escape(path) + "'>"
                               "<button class='danger' type='submit'>Delete</button></form>");
        }
        server.sendContent(F("</td></tr>"));
        entry.close();
        ++count;
    }
    if (count == 0U) {
        server.sendContent(F("<tr><td colspan='4' class='muted'>Empty directory</td></tr>"));
    }
    server.sendContent(F("</tbody></table>"));
}

void handle_root() {
    const String fs_arg = current_fs_arg();
    String path;
    int status = 200;
    String detail;
    if (!normalize_path(server.hasArg("path") ? server.arg("path") : "/", path, true)) {
        path = "/";
        status = 400;
        detail = "Invalid path";
    }

    send_page_begin(fs_arg, path);
    if (status == 200) {
        FsTarget target;
        String error;
        if (!select_target_by_name(fs_arg, target, error, status)) {
            detail = error;
            send_error_panel(error);
        } else {
            LittleFsLockGuard lock(target.is_flash);
            if (!lock.locked()) {
                status = 503;
                detail = "Flash busy";
                send_error_panel(detail);
            } else {
                File directory = target.filesystem->open(path, FILE_READ);
                if (!directory) {
                    status = 404;
                    detail = "Directory not found";
                    send_error_panel(detail);
                } else if (!directory.isDirectory()) {
                    status = 400;
                    detail = "Path is not a directory";
                    send_error_panel(detail);
                } else {
                    send_directory_table(fs_arg, path, directory);
                }
                directory.close();
            }
        }
    } else {
        send_error_panel(detail);
    }
    send_page_end();
    log_request("root", status, fs_arg, path, detail);
}

void handle_list() {
    FsTarget target;
    String error;
    int status = 200;
    if (!select_target(target, error, status)) {
        send_error(status, error);
        log_request("api_list", status, server.arg("fs"), server.arg("path"), error);
        return;
    }
    String path;
    if (!normalize_path(server.hasArg("path") ? server.arg("path") : "/", path, true)) {
        send_error(400, "Invalid path");
        log_request("api_list", 400, server.arg("fs"), server.arg("path"), "Invalid path");
        return;
    }

    LittleFsLockGuard lock(target.is_flash);
    if (!lock.locked()) {
        send_error(503, "Flash busy");
        log_request("api_list", 503, server.arg("fs"), path, "Flash busy");
        return;
    }

    File directory = target.filesystem->open(path, FILE_READ);
    if (!directory) {
        send_error(404, "Directory not found");
        log_request("api_list", 404, server.arg("fs"), path, "Directory not found");
        return;
    }
    if (!directory.isDirectory()) {
        directory.close();
        send_error(400, "Path is not a directory");
        log_request("api_list", 400, server.arg("fs"), path, "Path is not a directory");
        return;
    }

    String json = "[";
    bool first = true;
    while (true) {
        File entry = directory.openNextFile(FILE_READ);
        if (!entry) break;
        const String name = base_name(String(entry.name()));
        if (!first) json += ",";
        first = false;
        json += "{\"name\":\"";
        json += json_escape(name);
        json += "\",\"size\":";
        json += String(static_cast<uint32_t>(entry.size()));
        json += ",\"dir\":";
        json += entry.isDirectory() ? "true" : "false";
        json += "}";
        entry.close();
    }
    directory.close();
    json += "]";
    server.send(200, "application/json", json);
    log_request("api_list", 200, server.arg("fs"), path);
}

void handle_download() {
    FsTarget target;
    String error;
    int status = 200;
    if (!select_target(target, error, status)) {
        send_error(status, error);
        log_request("download", status, server.arg("fs"), server.arg("path"), error);
        return;
    }
    String path;
    if (!normalize_path(server.arg("path"), path, false)) {
        send_error(400, "Invalid path");
        log_request("download", 400, server.arg("fs"), server.arg("path"), "Invalid path");
        return;
    }

    LittleFsLockGuard lock(target.is_flash);
    if (!lock.locked()) {
        send_error(503, "Flash busy");
        log_request("download", 503, server.arg("fs"), path, "Flash busy");
        return;
    }

    File file = target.filesystem->open(path, FILE_READ);
    if (!file) {
        send_error(404, "File not found");
        log_request("download", 404, server.arg("fs"), path, "File not found");
        return;
    }
    if (file.isDirectory()) {
        file.close();
        send_error(400, "Path is a directory");
        log_request("download", 400, server.arg("fs"), path, "Path is a directory");
        return;
    }
    server.sendHeader("Content-Disposition",
                      "attachment; filename=\"" + html_escape(base_name(path)) + "\"");
    server.streamFile(file, content_type_for(path));
    file.close();
    log_request("download", 200, server.arg("fs"), path);
}

int delete_file_path(const String &fs_arg, const String &raw_path, String &message,
                     bool &sd_changed) {
    sd_changed = false;
    FsTarget target;
    String error;
    int status = 200;
    if (!select_target_by_name(fs_arg, target, error, status)) {
        message = error;
        return status;
    }
    String path;
    if (!normalize_path(raw_path, path, false)) {
        message = "Invalid path";
        return 400;
    }

    LittleFsLockGuard lock(target.is_flash);
    if (!lock.locked()) {
        message = "Flash busy";
        return 503;
    }

    File file = target.filesystem->open(path, FILE_READ);
    if (!file) {
        message = "File not found";
        return 404;
    }
    if (file.isDirectory()) {
        file.close();
        message = "Directory delete is disabled";
        return 400;
    }
    file.close();
    if (!target.filesystem->remove(path)) {
        message = "Delete failed";
        return 500;
    }
    sd_changed = target.is_sd;
    message = "Deleted";
    return 200;
}

void handle_delete() {
    String message;
    bool sd_changed = false;
    const int status = delete_file_path(server.arg("fs"), server.arg("path"), message,
                                        sd_changed);
    notify_sd_changed(sd_changed && status == 200);
    if (status == 200) server.send(200, "text/plain", message);
    else send_error(status, message);
    log_request("api_delete", status, server.arg("fs"), server.arg("path"), message);
}

void handle_delete_form() {
    const String fs_arg = current_fs_arg();
    String return_path;
    if (!normalize_path(server.hasArg("return") ? server.arg("return") : "/", return_path, true)) {
        return_path = "/";
    }
    String message;
    bool sd_changed = false;
    const int status = delete_file_path(fs_arg, server.arg("path"), message, sd_changed);
    notify_sd_changed(sd_changed && status == 200);
    log_request("form_delete", status, fs_arg, server.arg("path"), message);
    if (status == 200) {
        redirect_to_page(fs_arg, return_path);
    } else {
        send_error(status, message);
    }
}

void close_upload(bool remove_partial = false) {
    if (upload_file) {
        upload_file.close();
    }
    if (remove_partial && upload_filesystem != nullptr && !upload_partial_path.isEmpty()) {
        if (upload_filesystem->remove(upload_partial_path)) {
            Serial.printf("[FILE_MANAGER] upload partial removed %s\n",
                          upload_partial_path.c_str());
        }
    }
    if (upload_flash_lock) {
        bsp_littlefs_unlock();
        upload_flash_lock = false;
    }
    upload_filesystem = nullptr;
    upload_sd_target = false;
}

void fail_upload(const String &message, int status = 500) {
    upload_failed = true;
    upload_status = status;
    upload_error = message;
    close_upload(true);
}

bool commit_upload() {
    if (upload_filesystem == nullptr) return false;
    if (upload_file) upload_file.close();

    const bool replacing_existing = upload_filesystem->exists(upload_final_path);
    if (replacing_existing) {
        if (upload_filesystem->exists(upload_backup_path) &&
            !upload_filesystem->remove(upload_backup_path)) {
            upload_error = "Could not clear previous upload backup";
            return false;
        }
        if (!upload_filesystem->rename(upload_final_path, upload_backup_path)) {
            upload_error = "Could not preserve existing file";
            return false;
        }
    }

    if (!upload_filesystem->rename(upload_partial_path, upload_final_path)) {
        if (replacing_existing) {
            (void)upload_filesystem->rename(upload_backup_path, upload_final_path);
        }
        upload_error = "Could not finalize upload";
        return false;
    }

    if (replacing_existing && !upload_filesystem->remove(upload_backup_path)) {
        Serial.printf("[FILE_MANAGER] upload backup retained %s\n",
                      upload_backup_path.c_str());
    }
    return true;
}

void handle_upload_data() {
    HTTPUpload &upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        close_upload(true);
        upload_failed = false;
        upload_status = 500;
        upload_error = "";
        upload_sd_target = false;
        upload_flash_lock = false;
        upload_filesystem = nullptr;
        upload_final_path = "";
        upload_partial_path = "";
        upload_backup_path = "";
        upload_written = 0U;
        upload_music_target = false;

        FsTarget target;
        String error;
        int status = 200;
        if (!select_target(target, error, status)) {
            fail_upload(error, status);
            log_request("upload_start", status, server.arg("fs"), server.arg("path"), error);
            return;
        }

        if (target.is_sd) {
            if (!task_post_player_command(PlayerCommandType::StopForStorage)) {
                fail_upload("Player busy; retry upload", 503);
                log_request("upload_start", 503, server.arg("fs"), server.arg("path"),
                            "Player busy");
                return;
            }
            if (!wait_for_player_storage_release(500U)) {
                fail_upload("Player did not release storage; retry upload", 503);
                log_request("upload_start", 503, server.arg("fs"), server.arg("path"),
                            "Player did not release storage");
                return;
            }
        }

        String directory_path;
        String upload_name;
        if (!normalize_path(server.arg("path"), directory_path, true) ||
            !normalize_upload_name(upload.filename, upload_name)) {
            fail_upload("Invalid upload path", 400);
            log_request("upload_start", 400, server.arg("fs"), server.arg("path"),
                        "Invalid upload path");
            return;
        }

        if (target.is_flash) {
            if (!bsp_littlefs_lock(pdMS_TO_TICKS(2000U))) {
                fail_upload("Flash busy", 503);
                log_request("upload_start", 503, server.arg("fs"), directory_path,
                            "Flash busy");
                return;
            }
            upload_flash_lock = true;
        }

        File directory = target.filesystem->open(directory_path, FILE_READ);
        if (!directory || !directory.isDirectory()) {
            directory.close();
            fail_upload("Upload directory not found", 404);
            log_request("upload_start", 404, server.arg("fs"), directory_path,
                        "Upload directory not found");
            return;
        }
        directory.close();

        upload_final_path = join_path(directory_path, upload_name);
        upload_partial_path = upload_final_path + ".part";
        upload_backup_path = upload_final_path + ".upload-backup";
        File existing = target.filesystem->open(upload_final_path, FILE_READ);
        if (existing && existing.isDirectory()) {
            existing.close();
            fail_upload("Target is a directory", 400);
            log_request("upload_start", 400, server.arg("fs"), upload_final_path,
                        "Target is a directory");
            return;
        }
        existing.close();

        // A previous interrupted upload may have left a temporary file behind.
        if (target.filesystem->exists(upload_partial_path) &&
            !target.filesystem->remove(upload_partial_path)) {
            fail_upload("Could not clear incomplete upload", 500);
            log_request("upload_start", 500, server.arg("fs"), upload_partial_path,
                        "Could not clear incomplete upload");
            return;
        }

        upload_filesystem = target.filesystem;
        upload_file = target.filesystem->open(upload_partial_path, FILE_WRITE);
        if (!upload_file) {
            fail_upload("Open target failed", 500);
            log_request("upload_start", 500, server.arg("fs"), upload_partial_path,
                        "Open target failed");
            return;
        }
        upload_sd_target = target.is_sd;
        upload_music_target = target.is_sd && is_music_audio_path(upload_final_path);
        Serial.printf("[FILE_MANAGER] upload start final=%s partial=%s\n",
                      upload_final_path.c_str(), upload_partial_path.c_str());
        return;
    }

    if (upload.status == UPLOAD_FILE_WRITE) {
        if (upload_failed || !upload_file) return;
        const size_t written = upload_file.write(upload.buf, upload.currentSize);
        upload_written += written;
        if (written != upload.currentSize) {
            Serial.printf("[FILE_MANAGER] upload write failed path=%s wrote=%u chunk=%u total=%u\n",
                          upload_final_path.c_str(), static_cast<unsigned>(written),
                          static_cast<unsigned>(upload.currentSize),
                          static_cast<unsigned>(upload_written));
            fail_upload("Write failed");
        }
        return;
    }

    if (upload.status == UPLOAD_FILE_END) {
        bool committed = false;
        const bool should_refresh_music = upload_music_target && !upload_failed;
        if (!upload_failed && !commit_upload()) {
            fail_upload(upload_error.isEmpty() ? "Could not finalize upload" : upload_error);
        } else if (!upload_failed) {
            committed = true;
            close_upload();
            if (should_refresh_music) {
                (void)task_post_player_command(PlayerCommandType::RefreshLibraryStopped);
            }
        }
        Serial.printf("[FILE_MANAGER] upload end size=%lu written=%u ok=%d path=%s\n",
                      static_cast<unsigned long>(upload.totalSize),
                      static_cast<unsigned>(upload_written), committed ? 1 : 0,
                      upload_final_path.c_str());
        return;
    }

    if (upload.status == UPLOAD_FILE_ABORTED) {
        fail_upload("Upload aborted", 499);
    }
}

void handle_upload_done() {
    if (upload_failed) {
        const String message = upload_error.isEmpty() ? "Upload failed" : upload_error;
        send_error(upload_status, message);
        log_request("upload_done", upload_status, server.arg("fs"), server.arg("path"),
                    message);
        upload_failed = false;
        upload_status = 500;
        upload_error = "";
        return;
    }
    const String fs_arg = current_fs_arg();
    String path;
    if (!normalize_path(server.hasArg("path") ? server.arg("path") : "/", path, true)) {
        path = "/";
    }
    log_request("upload_done", 200, fs_arg, path, "Uploaded");
    if (server.hasArg("redirect")) {
        redirect_to_page(fs_arg, path);
    } else {
        server.send(200, "text/plain", "Uploaded");
    }
}

void handle_not_found() {
    log_request("not_found", 404, server.arg("fs"), server.arg("path"));
    if (server.uri().startsWith("/api/")) {
        send_error(404, "Not found");
        return;
    }
    handle_root();
}

void handle_probe() {
    server.sendHeader("Location", "http://192.168.1.1/");
    server.send(302, "text/plain", "");
    log_request("probe", 302, "", "");
}

void handle_favicon() {
    server.send(204, "text/plain", "");
    log_request("favicon", 204, "", "");
}

void handle_diag() {
    const bool sd_ready = bsp_storage_available() || bsp_storage_init();
    const bool flash_ready = bsp_littlefs_available();
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");
    server.sendContent(F("<!doctype html><html><head><meta charset='utf-8'>"
                         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                         "<title>Dudu File Manager Diag</title><style>"));
    server.sendContent_P(PAGE_STYLE);
    server.sendContent(F("</style></head><body><main><h1>Diag</h1><div class='panel'>"));
    server.sendContent("AP: " + WiFi.softAPIP().toString() + "<br>");
    server.sendContent(String("SD: ") + (sd_ready ? "ready" : "unavailable") + "<br>");
    server.sendContent(String("Flash: ") + (flash_ready ? "ready" : "unavailable") + "<br>");
    server.sendContent(String("Active: ") + (ap_active ? "yes" : "no") + "<br>");
    server.sendContent(F("</div><a class='btn' href='/'>Back</a></main></body></html>"));
    server.sendContent("");
    log_request("diag", 200, "", "");
}

void register_routes() {
    if (routes_registered) return;
    server.on("/", HTTP_GET, handle_root);
    server.on("/delete", HTTP_POST, handle_delete_form);
    server.on("/diag", HTTP_GET, handle_diag);
    server.on("/favicon.ico", HTTP_GET, handle_favicon);
    server.on("/generate_204", HTTP_GET, handle_probe);
    server.on("/hotspot-detect.html", HTTP_GET, handle_probe);
    server.on("/ncsi.txt", HTTP_GET, handle_probe);
    server.on("/api/list", HTTP_GET, handle_list);
    server.on("/api/file", HTTP_GET, handle_download);
    server.on("/api/file", HTTP_DELETE, handle_delete);
    server.on("/api/upload", HTTP_POST, handle_upload_done, handle_upload_data);
    server.onNotFound(handle_not_found);
    routes_registered = true;
}
}

void file_manager_network_init() {}

bool file_manager_network_start_ap() {
    if (ap_active) return true;
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_NETMASK);
    if (!WiFi.softAP(AP_SSID)) return false;
    register_routes();
    server.begin();
    dns_server.start(DNS_PORT, "*", AP_IP);
    ap_active = true;
    ap_message = String("AP ") + AP_SSID + " " + WiFi.softAPIP().toString();
    Serial.printf("[FILE_MANAGER] %s\n", ap_message.c_str());
    return true;
}

void file_manager_network_stop_ap() {
    if (!ap_active) return;
    close_upload(true);
    server.stop();
    dns_server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    ap_active = false;

    if (!task_post_player_command(PlayerCommandType::RefreshLibraryStopped)) {
        Serial.println("[FILE_MANAGER] unable to request player library refresh");
    }
}

void file_manager_network_process_ap() {
    if (!ap_active) return;
    dns_server.processNextRequest();
    server.handleClient();
}

bool file_manager_network_ap_active() { return ap_active; }

String file_manager_network_ap_message() { return ap_message; }
