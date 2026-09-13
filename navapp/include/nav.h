#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#define M_NONE 0
#define M_STARTUP 1
#define M_FIRST 2

struct Settings {
        std::array<std::string, 4> fields_priority = {"command", "path", "note",
                                                      "brief"};
        /*
         * 0 = go
         * 1 = brief-go
         * 2 = note-go
         */
        unsigned char on_click = 0;
        std::string on_startup = "";
        std::vector<std::pair<std::string, char>> status_to_symbol_map = {
            {"opened", ' '},
            {"closed", 'X'},
            {"finished", '-'},
            {"ongoing", '>'}};
};

struct Dest {
        std::string dest_name = "";
        unsigned char formatting = 0;
        std::string path = "";
        std::string brief = "";
        std::string note_path = "";
        std::string command = "";
        std::string task_status = "";
        std::string group_name = "";
        std::vector<std::string> displayed_lines;
        unsigned char priority = 0;
};

const uint32_t palette[] = {
    0,          // 0 unused
    0xFFB71C1C, // 1 red
    0xFF1B5E20, // 2 green
    0xFF0D47A1, // 3 blue
    0xFFF57F17, // 4 amber
    0xFF4A148C, // 5 purple
    0xFF006064, // 6 cyan
    0xFFE65100, // 7 orange
    0xFFFFD60A, // 8 yellow
    0xFFD50000, // 9 brown
    0xFF651FFF, // 10 pink
    0xFFFF00FF, // 11 aqua
    0xFF00FFFF, // 12 fuchsia
    0xFF90EE90, // 13 lime
    0xFF448AFF, // 14 sky blue
    0xFFEDD5E6, // 15 gray
};

bool find_dest(const std::vector<Dest> &dests, const std::string &dest_name,
               Dest &dest_out);

void append_dest_note(const Dest *dest, std::string &output, bool in_group,
                      const std::array<std::string, 4> &fields_priority);

size_t find_group(const std::vector<Dest> &dests,
                  const std::string &group_name);

int parse_navdict(const std::string &file_path, std::vector<Dest> &dests,
                  Settings &settings, std::string &errors, const char *filter,
                  char stop_mode);

unsigned char go(const Dest &dest, std::string &field_out,
                 const std::array<std::string, 4> &fields_priority);
