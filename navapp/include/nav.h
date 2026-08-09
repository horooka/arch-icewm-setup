#include <cstdint>
#include <string>
#include <vector>

struct Dest {
        unsigned char formatting = 0;
        std::string path = "";
        std::vector<std::string> note_lines;
        std::string note_ret;
        unsigned char priority = 0;
};

struct DestGroup {
        unsigned char formatting = 0;
        std::string name;
        unsigned char priority = 0;
        std::vector<std::pair<std::string, Dest>> dests;

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

void append_destgroup_note(const DestGroup *destgroup, std::string &output);

void append_dest_note(const Dest *dest, std::string &output, bool in_group);

size_t find_group(const std::vector<DestGroup> &dests_groups,
                  const std::string &group_name);

int parse_navdict(const std::string &path, std::vector<DestGroup> &dests_groups,
                  std::string &errors, const std::string &arg);
