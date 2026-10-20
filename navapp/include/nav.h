#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#define M_NONE 0
#define M_STARTUP 1
#define M_FIRST 2
#define MCOMPOSE(x, cached, silent) (x | (cached ? 128 : 0) | (silent ? 64 : 0))
#define MCOMPOSERAW(x, cached, silent)                                         \
    (x | (cached ? 128 : 0) | (silent ? 64 : 0) | 32)
#define MCACHED(x) ((x & 128) == 128)
#define MSILENT(x) ((x & 64) == 64)
#define MRAW(x) ((x & 32) == 32)
#define MBASE(x) (x & 31)

struct Settings {
        std::array<std::string, 4> fields_priority = {"command", "path", "note",
                                                      "brief"};
        std::string on_startup = "";
        std::vector<std::pair<std::string, char>> mark_to_symbol_map = {
            {"opened", ' '},
            {"closed", 'X'},
            {"finished", '-'},
            {"ongoing", '>'}};
        // Group -> packed 8-bit format (fg in low nibble, bg in high nibble),
        // collected from `[Group, fmt]` headers. Missing entry means no format.
        std::vector<std::pair<std::string, unsigned char>> group_format_map;
};

struct Dest {
        std::string dest_name = "";
        unsigned char formatting = 0;
        std::string path = "";
        std::string brief = "";
        std::string note_path = "";
        std::string date = "";
        std::string command = "";
        std::string dest_mark = "";
        std::string group_name = "";
        unsigned char level = 0;
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

size_t find_group(const std::vector<Dest> &dests,
                  const std::string &group_name);

std::string expand_args(const std::string &str,
                        const std::vector<std::string> &args);

int parse_navdict(const std::string &file_path, std::vector<Dest> &dests,
                  Settings &settings, std::string &errors, const char *filter,
                  unsigned char mode);

int query(const std::string &line, std::vector<Dest> &dests,
          std::string &errors, bool show_fields = false);

/*
 * Executes a query (SELECT/WHERE/ORDER BY/LIMIT) leaving the filtered, sorted
 * and limited destinations in `dests` and the selected column indices in
 * `selected` (empty means "all columns"). Returns 0 on success.
 */
int run_query(const std::string &line, std::vector<Dest> &dests,
              std::vector<unsigned char> &selected, std::string &errors,
              Settings *settings_out = nullptr);

/* Date helpers. Dates are ISO `YYYY-MM-DD` strings backed by day counts. */
bool date_to_days(const std::string &in, int32_t &days_out);
std::string days_to_date(int32_t days);
std::string today_date();
int32_t today_days();

unsigned char go(const Dest &dest, std::string &field_out,
                 const std::array<std::string, 4> &fields_priority);

unsigned char go(const Dest &dest, std::string &field_out,
                 const std::array<std::string, 4> &fields_priority,
                 const std::vector<std::string> &args);

int edit_dest(const std::string &file_path, Dest &dest);
int edit_group(const std::string &file_path, const std::string &group_name);
int add_dest(const std::string &file_path, const std::string &group_name,
             const std::string &line_hint);
int remove_dest(const std::string &file_path, const Dest &dest, bool force);

void fields_used(const Dest &dest, std::string &fields_used);
