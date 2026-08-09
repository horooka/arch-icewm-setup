#include <array>
#include <cstdint>
#include <string>
#include <vector>

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
};

struct Dest {
        std::string dest_name = "";
        unsigned char formatting = 0;
        std::string path = "";
        std::string brief = "";
        std::string note_path = "";
        std::string command = "";
        std::vector<std::string> displayed_lines;
        unsigned char priority = 0;
};

struct DestGroup {
        unsigned char formatting = 0;
        std::string name;
        unsigned char priority = 0;
        std::vector<Dest> dests;

        void clear() {
            formatting = 0;
            name = "";
            priority = 0;
            dests.clear();
        }
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

bool find_dest(const std::vector<DestGroup> &dests_groups,
               const std::string &dest_name, Dest &dest);

void append_destgroup_note(const DestGroup *destgroup, std::string &output,
                           const std::array<std::string, 4> &fields_priority);

void append_dest_note(const Dest *dest, std::string &output, bool in_group,
                      const std::array<std::string, 4> &fields_priority);

size_t find_group(const std::vector<DestGroup> &dests_groups,
                  const std::string &group_name);

int parse_navdict(const std::string &path, std::vector<DestGroup> &dests_groups,
                  Settings &settings, std::string &errors,
                  const std::string &arg);

unsigned char go(const Dest &dest, std::string &field_out,
                 const std::array<std::string, 4> &fields_priority);
