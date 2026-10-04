#include "console.hpp"

#include <cctype>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <strings.h>

#include "config.hpp"
#include "platform.hpp"

namespace {

constexpr const char* HELP = R"(Commands:
  show                                 settings, positions and home sensor readings
  set <name> <value>                   change a setting listed by "show"
  banner <set> <n|*> <field> <value>   field: width, steps (per unit), offset or reversed
  jog <set> <n> <half-steps>           move one banner; positive is away from home
  home [set]                           home every set, or just one
  shuffle                              shuffle now
  release                              turn every coil off
  save | load | defaults               store settings / reload stored ones / revert to config.hpp
  run                                  resume the shuffle cycle
)";

constexpr int64_t IDLE_RESUME_US = int64_t{config::CONSOLE_IDLE_RESUME_MINUTES} * 60 * 1'000'000;
constexpr size_t MAX_LINE = 120;

std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> words;
    size_t start = 0;
    while (start < line.size()) {
        const size_t begin = line.find_first_not_of(' ', start);
        if (begin == std::string::npos) {
            break;
        }
        const size_t end = line.find(' ', begin);
        words.push_back(line.substr(begin, end - begin));
        start = end;
    }
    return words;
}

bool parse_number(const std::string& text, double& value) {
    char* end = nullptr;
    value = std::strtod(text.c_str(), &end);
    return !text.empty() && *end == '\0' && std::isfinite(value);
}

bool parse_bool(const std::string& text, bool& value) {
    for (const char* yes : {"1", "yes", "true", "on"}) {
        if (strcasecmp(text.c_str(), yes) == 0) {
            value = true;
            return true;
        }
    }
    for (const char* no : {"0", "no", "false", "off"}) {
        if (strcasecmp(text.c_str(), no) == 0) {
            value = false;
            return true;
        }
    }
    return false;
}

int find_set(const std::string& name) {
    for (int i = 0; i < static_cast<int>(std::size(config::SETS)); ++i) {
        if (strcasecmp(name.c_str(), config::SETS[i].name) == 0) {
            return i;
        }
    }
    printf("No set called \"%s\".\n", name.c_str());
    return -1;
}

// Parses a banner number within `set`; prints why and returns -1 if it isn't one.
int find_banner(int set, const std::string& text) {
    const int count = static_cast<int>(config::SETS[set].banners.size());
    double number = 0;
    if (!parse_number(text, number) || number != std::floor(number) || number < 0 || number >= count) {
        printf("Banner must be a number from 0 to %d.\n", count - 1);
        return -1;
    }
    return static_cast<int>(number);
}

}  // namespace

Console::Console(Settings& settings, Motion& motion, BannerController& banners)
    : settings_(settings), motion_(motion), banners_(banners) {}

void Console::poll(uint32_t timeout_ms) {
    char buffer[64];
    const size_t count = platform::console_read(buffer, sizeof buffer, timeout_ms);
    const int64_t now = platform::now_us();

    if (count > 0) {
        last_input_us_ = now;
        if (!paused_) {
            paused_ = true;
            printf("\nShuffle cycle paused. \"help\" lists commands; \"run\" resumes.\n> ");
        }
    } else if (paused_ && now - last_input_us_ > IDLE_RESUME_US) {
        paused_ = false;
        printf("\nNo console input for %" PRIu32 " minutes; resuming the shuffle cycle.\n",
               config::CONSOLE_IDLE_RESUME_MINUTES);
    }

    for (size_t i = 0; i < count; ++i) {
        const char c = buffer[i];
        if (c == '\n' && last_was_cr_) {  // the second half of CR LF
            last_was_cr_ = false;
            continue;
        }
        last_was_cr_ = c == '\r';

        if (c == '\r' || c == '\n') {
            printf("\n");
            execute(line_);
            line_.clear();
            if (paused_) {
                printf("> ");
            }
        } else if (c == '\b' || c == 0x7F) {
            if (!line_.empty()) {
                line_.pop_back();
                printf("\b \b");
            }
        } else if (std::isprint(static_cast<unsigned char>(c)) && line_.size() < MAX_LINE) {
            line_ += c;
            putchar(c);
        }
    }
    fflush(stdout);
}

void Console::execute(const std::string& line) {
    const Args args = split(line);
    if (args.empty()) {
        return;
    }
    const std::string& command = args[0];

    if (command == "help") {
        printf("%s", HELP);
    } else if (command == "show") {
        show();
    } else if (command == "set" && args.size() == 3) {
        set_setting(args);
    } else if (command == "banner" && args.size() == 5) {
        set_banner(args);
    } else if (command == "jog" && args.size() == 4) {
        jog(args);
    } else if (command == "home" && args.size() <= 2) {
        home(args);
    } else if (command == "shuffle" && args.size() == 1) {
        banners_.home(false);
        banners_.shuffle();
        motion_.release_all();
    } else if (command == "release" && args.size() == 1) {
        motion_.release_all();
    } else if (command == "save" && args.size() == 1) {
        printf(settings_.save() ? "Settings saved.\n" : "Saving failed.\n");
    } else if (command == "load" && args.size() == 1) {
        Settings stored = settings_;
        if (stored.load()) {
            apply(stored);
            printf("Loaded saved settings.\n");
        } else {
            printf("No saved settings.\n");
        }
    } else if (command == "defaults" && args.size() == 1) {
        apply(Settings::defaults());
        printf("Using the defaults from config.hpp (not saved).\n");
    } else if (command == "run" && args.size() == 1) {
        paused_ = false;
        printf("Resuming the shuffle cycle.\n");
    } else {
        printf("Unrecognised command; \"help\" lists them.\n");
    }
}

void Console::show() const {
    printf("Settings (change with: set <name> <value>)\n");
    for (const SettingField& field : setting_fields()) {
        if (field.integer) {
            printf("  %-15s %9" PRIu32 "  %s\n", field.name, settings_.*field.integer, field.description);
        } else {
            printf("  %-15s %9.3f  %s\n", field.name, static_cast<double>(settings_.*field.real), field.description);
        }
    }

    for (int s = 0; s < static_cast<int>(std::size(config::SETS)); ++s) {
        const config::Set& set = config::SETS[s];
        printf("\nSet %s: %s", set.name, banners_.homed(s) ? "homed" : "not homed");
        if (set.home_mode == config::HomeMode::Sensor) {
            printf(", home sensor %s", banners_.sensor_active(s) ? "ACTIVE" : "clear");
        }
        if (!banners_.order(s).empty()) {
            printf(", order");
            for (int b : banners_.order(s)) {
                printf(" %d", b);
            }
        }
        printf("\n   n  motor     width     steps  offset  reversed  position\n");
        for (int i = 0; i < static_cast<int>(set.banners.size()); ++i) {
            const BannerSettings& banner = settings_.banner(s, i);
            printf("  %2d  %5d  %8.2f  %8.3f  %6" PRId32 "  %-8s  %8" PRId32 "\n", i, set.banners[i].motor,
                   static_cast<double>(banner.width), static_cast<double>(banner.steps_per_unit), banner.home_offset,
                   banner.reversed ? "yes" : "no", motion_.position(set.banners[i].motor));
        }
    }
}

void Console::set_setting(const Args& args) {
    for (const SettingField& field : setting_fields()) {
        if (args[1] != field.name) {
            continue;
        }
        double value = 0;
        if (!parse_number(args[2], value) || value < field.min || value > field.max ||
            (field.integer && value != std::floor(value))) {
            printf("%s must be %s from %g to %g.\n", field.name, field.integer ? "a whole number" : "a number",
                   field.min, field.max);
            return;
        }
        if (field.integer) {
            settings_.*field.integer = static_cast<uint32_t>(value);
        } else {
            settings_.*field.real = static_cast<float>(value);
        }
        printf("%s = %s\n", field.name, args[2].c_str());
        return;
    }
    printf("No setting called \"%s\"; \"show\" lists them.\n", args[1].c_str());
}

void Console::set_banner(const Args& args) {
    const int set = find_set(args[1]);
    if (set < 0) {
        return;
    }
    int first = 0;
    int last = static_cast<int>(config::SETS[set].banners.size()) - 1;
    if (args[2] != "*") {
        first = last = find_banner(set, args[2]);
        if (first < 0) {
            return;
        }
    }

    const std::string& field = args[3];
    double number = 0;
    bool flag = false;
    if (field == "reversed") {
        if (!parse_bool(args[4], flag)) {
            printf("reversed must be yes or no.\n");
            return;
        }
    } else if (field == "width" || field == "steps") {
        if (!parse_number(args[4], number) || number <= 0) {
            printf("%s must be a positive number.\n", field.c_str());
            return;
        }
    } else if (field == "offset") {
        if (!parse_number(args[4], number) || number != std::floor(number) || std::fabs(number) > 10'000'000) {
            printf("offset must be a whole number of half-steps.\n");
            return;
        }
    } else {
        printf("Field must be width, steps, offset or reversed.\n");
        return;
    }

    bool direction_changed = false;
    for (int i = first; i <= last; ++i) {
        BannerSettings& banner = settings_.banner(set, i);
        if (field == "width") {
            banner.width = static_cast<float>(number);
        } else if (field == "steps") {
            banner.steps_per_unit = static_cast<float>(number);
        } else if (field == "offset") {
            banner.home_offset = static_cast<int32_t>(number);
        } else {
            direction_changed |= banner.reversed != flag;
            banner.reversed = flag;
        }
    }
    printf("Set %s banner %s: %s = %s\n", config::SETS[set].name, args[2].c_str(), field.c_str(), args[4].c_str());
    if (direction_changed) {
        banners_.invalidate(set);
        printf("Set %s will be homed again before it next moves.\n", config::SETS[set].name);
    }
}

void Console::jog(const Args& args) {
    const int set = find_set(args[1]);
    const int banner = set < 0 ? -1 : find_banner(set, args[2]);
    double steps = 0;
    if (banner < 0) {
        return;
    }
    if (!parse_number(args[3], steps) || steps != std::floor(steps) || std::fabs(steps) > 10'000'000) {
        printf("Give a whole number of half-steps.\n");
        return;
    }
    banners_.jog(set, banner, static_cast<int32_t>(steps));
    motion_.release_all();
    printf("Set %s banner %d is at %" PRId32 ".\n", config::SETS[set].name, banner,
           motion_.position(config::SETS[set].banners[banner].motor));
}

void Console::home(const Args& args) {
    int set = -1;
    if (args.size() == 2 && (set = find_set(args[1])) < 0) {
        return;
    }
    banners_.home(true, set);
    motion_.release_all();
}

void Console::apply(const Settings& next) {
    for (int s = 0; s < static_cast<int>(std::size(config::SETS)); ++s) {
        for (int i = 0; i < static_cast<int>(config::SETS[s].banners.size()); ++i) {
            if (settings_.banner(s, i).reversed != next.banner(s, i).reversed) {
                banners_.invalidate(s);
                printf("Set %s will be homed again before it next moves.\n", config::SETS[s].name);
                break;
            }
        }
    }
    settings_ = next;
}
