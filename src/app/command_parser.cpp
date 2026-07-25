#include "app/command_parser.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
void set_error(char *error, size_t capacity, const char *message) {
    if ((error != nullptr) && (capacity > 0)) {
        std::snprintf(error, capacity, "%s", message);
    }
}

bool equals(const char *left, const char *right) {
    return strcasecmp(left, right) == 0;
}
}

bool command_parser_parse(const char *line, UiInputEvent *event,
                          char *error, size_t error_capacity) {
    if ((line == nullptr) || (event == nullptr)) {
        set_error(error, error_capacity, "invalid argument");
        return false;
    }

    char input[96];
    std::snprintf(input, sizeof(input), "%s", line);
    char *cursor = input;
    while (std::isspace(static_cast<unsigned char>(*cursor))) {
        ++cursor;
    }
    char *end = cursor + std::strlen(cursor);
    while ((end > cursor) && std::isspace(static_cast<unsigned char>(end[-1]))) {
        *--end = '\0';
    }

    *event = {};
    if (equals(cursor, "page-prev")) event->type = UiInputType::PagePrevious;
    else if (equals(cursor, "page-next")) event->type = UiInputType::PageNext;
    else if (equals(cursor, "up")) event->type = UiInputType::Up;
    else if (equals(cursor, "down")) event->type = UiInputType::Down;
    else if (equals(cursor, "ok")) event->type = UiInputType::Ok;
    else if (equals(cursor, "play")) event->type = UiInputType::Play;
    else if (equals(cursor, "pause")) event->type = UiInputType::Pause;
    else if (equals(cursor, "toggle")) event->type = UiInputType::Toggle;
    else if (equals(cursor, "prev")) event->type = UiInputType::TrackPrevious;
    else if (equals(cursor, "next")) event->type = UiInputType::TrackNext;
    else if (equals(cursor, "vol+")) { event->type = UiInputType::VolumeChange; event->value = 1; }
    else if (equals(cursor, "vol-")) { event->type = UiInputType::VolumeChange; event->value = -1; }
    else if (equals(cursor, "rescan")) event->type = UiInputType::Rescan;
    else if (equals(cursor, "status")) event->type = UiInputType::Status;
    else if (strncasecmp(cursor, "vol ", 4) == 0) {
        char *value_end = nullptr;
        const long value = std::strtol(cursor + 4, &value_end, 10);
        while ((value_end != nullptr) && std::isspace(static_cast<unsigned char>(*value_end))) ++value_end;
        if ((value_end == cursor + 4) || ((value_end != nullptr) && (*value_end != '\0')) ||
            (value < PLAYER_VOLUME_MIN) || (value > PLAYER_VOLUME_MAX)) {
            set_error(error, error_capacity, "volume must be 0..21");
            return false;
        }
        event->type = UiInputType::VolumeSet;
        event->value = static_cast<int16_t>(value);
    } else if (strncasecmp(cursor, "page ", 5) == 0) {
        const char *page = cursor + 5;
        event->type = UiInputType::PageGoto;
        if (equals(page, "home")) event->page = UiPage::Home;
        else if (equals(page, "music")) event->page = UiPage::Music;
        else if (equals(page, "read")) event->page = UiPage::Read;
        else if (equals(page, "setting")) event->page = UiPage::Setting;
        else {
            set_error(error, error_capacity, "page must be home|music|read|setting");
            return false;
        }
    } else {
        set_error(error, error_capacity, "unknown command");
        return false;
    }
    return true;
}
