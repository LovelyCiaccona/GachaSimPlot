#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr double CHAR_BASE_6_RATE = 0.008;
constexpr double CHAR_INC_6_RATE = 0.05;
constexpr double CHAR_BASE_5_RATE = 0.08;
constexpr double CHAR_LIMITED_RATE = 0.5;
constexpr double CHAR_NON_LIMITED_RATE = 2.0 / 7.0;
constexpr int CHAR_6_PITY_START = 65;
constexpr int CHAR_6_PITY_END = 80;
constexpr int CHAR_5_PITY = 10;
constexpr int CHAR_LIMITED_GUARANTEE = 120;
constexpr int CHAR_LIMITED_BONUS = 240;

constexpr int WEAPON_TEN_PULL_COST = 1980;

struct Options {
    std::string sim = "endfield-joint";
    int samples = 10000;
    int target_char = 1;
    int target_weapon = 1;
    long long initial_arsenal_quota = 0;
    int initial_coral = 0;
    bool exchange_enabled = true;
    unsigned int seed = 0;
    std::string out_dir = "data/runs/manual";
};

struct CharacterState {
    int six_counter = 0;
    int five_counter = 0;
    int pulls = 0;
    long long weapon_points = 0;
    long long yellow_tickets = 0;
    int limited = 0;
    int limited_gifted = 0;
    int limited_free10 = 0;
    int non_limited = 0;
    int standard = 0;
    int five_star = 0;
    bool has_limited = false;
};

struct WeaponState {
    int total_draws = 0;
    int six_since_last = 0;
    int pulled_limited = 0;
    int pulled_standard = 0;
    int five_star = 0;
    int gifted_limited = 0;
    int gifted_standard_boxes = 0;
    int next_gift_threshold = 100;
    bool next_gift_is_limited = false;
};

struct SampleResult {
    int total_pulls = 0;
    int char_pulls = 0;
    int weapon_pulls = 0;
    int weapon_ten_pulls = 0;
    long long weapon_points_earned = 0;
    long long weapon_points_spent = 0;
    long long weapon_points_left = 0;
    long long yellow_tickets = 0;
    int limited_chars = 0;
    int limited_chars_drawn = 0;
    int limited_chars_gifted = 0;
    int limited_chars_free10 = 0;
    int overflow_limited_chars = 0;
    int weapon_limited = 0;
    int weapon_limited_pulled = 0;
    int weapon_limited_gifted = 0;
    int weapon_standard_pulled = 0;
    int weapon_standard_boxes = 0;
    int char_five_star = 0;
    int char_non_limited = 0;
    int char_standard = 0;
    int coral_left = 0;
    int wuwa_char_exchanged = 0;
    int four_weapon = 0;
    int four_character = 0;
};

struct Metrics {
    double mean_total_pulls = 0.0;
    double stddev_total_pulls = 0.0;
    double mean_char_pulls = 0.0;
    double stddev_char_pulls = 0.0;
    double p50 = 0.0;
    double p75 = 0.0;
    double p90 = 0.0;
    double p95 = 0.0;
    double mean_weapon_pulls = 0.0;
    double stddev_weapon_pulls = 0.0;
    double mean_weapon_ten_pulls = 0.0;
    double mean_weapon_points_left = 0.0;
    double mean_weapon_points_earned = 0.0;
    double mean_yellow_tickets = 0.0;
    double mean_overflow_limited_chars = 0.0;
    double mean_char_standard = 0.0;
    double mean_coral_left = 0.0;
    double mean_wuwa_char_exchanged = 0.0;
    double mean_four_weapon = 0.0;
    double mean_four_character = 0.0;
};

void print_help(const char* exe) {
    std::cout
        << "Usage: " << exe << " --sim endfield-joint|wuwa [options]\n"
        << "Options:\n"
        << "  --samples N                 sample count, default 10000\n"
        << "  --target-char N             target limited characters, default 1\n"
        << "  --target-weapon N           target limited weapons, default 1\n"
        << "  --initial-arsenal-quota N   initial arsenal quota, default 0\n"
        << "  --initial-weapon-points N   alias of --initial-arsenal-quota\n"
        << "  --initial-coral N           WuWa initial coral, default 0\n"
        << "  --exchange on|off           WuWa coral exchange switch, default on\n"
        << "  --no-exchange               WuWa alias of --exchange off\n"
        << "  --seed N                    random seed, default time/random_device\n"
        << "  --out DIR                   output directory, default data/runs/manual\n"
        << "  -h, --help                  show help\n";
}

bool parse_int(const char* text, int& out) {
    try {
        size_t pos = 0;
        int value = std::stoi(text, &pos);
        if (pos != std::string(text).size()) return false;
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_ll(const char* text, long long& out) {
    try {
        size_t pos = 0;
        long long value = std::stoll(text, &pos);
        if (pos != std::string(text).size()) return false;
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_uint(const char* text, unsigned int& out) {
    try {
        size_t pos = 0;
        unsigned long value = std::stoul(text, &pos);
        if (pos != std::string(text).size()) return false;
        out = static_cast<unsigned int>(value);
        return true;
    } catch (...) {
        return false;
    }
}

Options parse_args(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto need_value = [&](const std::string& name) -> const char* {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value for " + name);
            }
            return argv[++i];
        };

        if (arg == "-h" || arg == "--help") {
            print_help(argv[0]);
            std::exit(0);
        } else if (arg == "--sim") {
            opt.sim = need_value(arg);
        } else if (arg == "--samples") {
            if (!parse_int(need_value(arg), opt.samples)) throw std::runtime_error("Invalid --samples");
        } else if (arg == "--target-char") {
            if (!parse_int(need_value(arg), opt.target_char)) throw std::runtime_error("Invalid --target-char");
        } else if (arg == "--target-weapon") {
            if (!parse_int(need_value(arg), opt.target_weapon)) throw std::runtime_error("Invalid --target-weapon");
        } else if (arg == "--initial-arsenal-quota" || arg == "--initial-weapon-points") {
            if (!parse_ll(need_value(arg), opt.initial_arsenal_quota)) throw std::runtime_error("Invalid initial arsenal quota");
        } else if (arg == "--initial-coral") {
            if (!parse_int(need_value(arg), opt.initial_coral)) throw std::runtime_error("Invalid --initial-coral");
        } else if (arg == "--exchange") {
            std::string value = need_value(arg);
            opt.exchange_enabled = value == "on" || value == "1" || value == "true" || value == "yes";
        } else if (arg.rfind("--exchange=", 0) == 0) {
            std::string value = arg.substr(std::string("--exchange=").size());
            opt.exchange_enabled = value == "on" || value == "1" || value == "true" || value == "yes";
        } else if (arg == "--no-exchange") {
            opt.exchange_enabled = false;
        } else if (arg == "--seed") {
            if (!parse_uint(need_value(arg), opt.seed)) throw std::runtime_error("Invalid --seed");
        } else if (arg == "--out") {
            opt.out_dir = need_value(arg);
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }

    if (opt.sim != "endfield-joint" && opt.sim != "wuwa") {
        throw std::runtime_error("Only --sim endfield-joint and --sim wuwa are implemented");
    }
    if (opt.samples <= 0) throw std::runtime_error("--samples must be positive");
    if (opt.target_char < 0) throw std::runtime_error("--target-char must be non-negative");
    if (opt.target_weapon < 0) throw std::runtime_error("--target-weapon must be non-negative");
    if (opt.initial_arsenal_quota < 0) throw std::runtime_error("--initial-arsenal-quota must be non-negative");
    if (opt.initial_coral < 0) throw std::runtime_error("--initial-coral must be non-negative");
    return opt;
}

class EndfieldCharacterPool {
public:
    explicit EndfieldCharacterPool(std::mt19937& rng) : rng_(rng), dist_(0.0, 1.0) {}

    void pull(CharacterState& state) {
        state.pulls += 1;
        state.six_counter += 1;
        state.five_counter += 1;

        if (state.pulls == CHAR_LIMITED_GUARANTEE && !state.has_limited) {
            state.limited += 1;
            state.has_limited = true;
            state.weapon_points += 2000;
            state.yellow_tickets += 50;
            state.six_counter = 0;
            state.five_counter = 0;
            return;
        }

        if (state.pulls > 0 && state.pulls % CHAR_LIMITED_BONUS == 0) {
            state.limited_gifted += 1;
        }

        const double roll = dist_(rng_);
        const double six_rate = current_six_rate(state.six_counter);
        if (roll < six_rate) {
            handle_six_star(state);
        } else if (state.five_counter >= CHAR_5_PITY) {
            handle_five_star(state);
        } else if (roll < six_rate + CHAR_BASE_5_RATE) {
            handle_five_star(state);
        } else {
            handle_four_star(state);
        }

        if (state.pulls == 30) {
            state.limited_free10 += free_ten_pulls(state);
        }
    }

private:
    std::mt19937& rng_;
    std::uniform_real_distribution<double> dist_;

    double current_six_rate(int counter) const {
        if (counter <= CHAR_6_PITY_START) return CHAR_BASE_6_RATE;
        if (counter < CHAR_6_PITY_END) {
            return CHAR_BASE_6_RATE + (counter - CHAR_6_PITY_START) * CHAR_INC_6_RATE;
        }
        return 1.0;
    }

    void handle_six_star(CharacterState& state) {
        double roll = dist_(rng_);
        if (roll < CHAR_LIMITED_RATE) {
            state.limited += 1;
            state.has_limited = true;
        } else {
            roll = dist_(rng_);
            if (roll < CHAR_NON_LIMITED_RATE) {
                state.non_limited += 1;
            } else {
                state.standard += 1;
            }
        }

        state.weapon_points += 2000;
        state.yellow_tickets += 50;
        state.six_counter = 0;
        state.five_counter = 0;
    }

    void handle_five_star(CharacterState& state) {
        state.five_star += 1;
        state.weapon_points += 200;
        state.yellow_tickets += 10;
        state.five_counter = 0;
    }

    void handle_four_star(CharacterState& state) {
        state.weapon_points += 20;
    }

    int free_ten_pulls(CharacterState& state) {
        int hits = 0;
        for (int i = 0; i < 10; ++i) {
            const double roll = dist_(rng_);
            if (roll < CHAR_BASE_6_RATE) {
                const double limited_roll = dist_(rng_);
                if (limited_roll < CHAR_LIMITED_RATE) {
                    state.limited += 1;
                    hits += 1;
                } else {
                    const double type_roll = dist_(rng_);
                    if (type_roll < CHAR_NON_LIMITED_RATE) {
                        state.non_limited += 1;
                    } else {
                        state.standard += 1;
                    }
                }
                state.weapon_points += 2000;
                state.yellow_tickets += 50;
            } else if (roll < CHAR_BASE_6_RATE + CHAR_BASE_5_RATE) {
                state.five_star += 1;
                state.weapon_points += 200;
                state.yellow_tickets += 10;
            } else {
                state.weapon_points += 20;
            }
        }
        return hits;
    }
};

class EndfieldWeaponPool {
public:
    explicit EndfieldWeaponPool(std::mt19937& rng) : rng_(rng), dist_(0.0, 1.0) {}

    void ten_pull(WeaponState& state) {
        for (int draw = 0; draw < 10; ++draw) {
            state.total_draws += 1;

            bool forced_six = false;
            bool forced_limited = false;
            if (state.total_draws == 80 && state.pulled_limited == 0) {
                forced_six = true;
                forced_limited = true;
            }
            if (!forced_six && state.six_since_last >= 39) {
                forced_six = true;
            }

            bool is_six = false;
            bool is_limited = false;
            bool is_five = false;

            if (forced_six) {
                is_six = true;
                is_limited = forced_limited || dist_(rng_) < 0.25;
            } else {
                const double roll = dist_(rng_);
                if (roll < 0.04) {
                    is_six = true;
                    is_limited = dist_(rng_) < 0.25;
                } else if (roll < 0.19) {
                    is_five = true;
                }
            }

            if (is_six) {
                state.six_since_last = 0;
                if (is_limited) {
                    state.pulled_limited += 1;
                } else {
                    state.pulled_standard += 1;
                }
            } else {
                state.six_since_last += 1;
                if (is_five) state.five_star += 1;
            }

            while (state.total_draws >= state.next_gift_threshold) {
                if (state.next_gift_is_limited) {
                    state.gifted_limited += 1;
                } else {
                    state.gifted_standard_boxes += 1;
                }
                state.next_gift_threshold += 80;
                state.next_gift_is_limited = !state.next_gift_is_limited;
            }
        }
    }

private:
    std::mt19937& rng_;
    std::uniform_real_distribution<double> dist_;
};

int total_limited_chars(const CharacterState& state) {
    return state.limited + state.limited_gifted;
}

int total_limited_weapons(const WeaponState& state) {
    return state.pulled_limited + state.gifted_limited;
}

SampleResult simulate_one(const Options& opt, std::mt19937& rng) {
    CharacterState chars;
    WeaponState weapons;
    EndfieldCharacterPool char_pool(rng);
    EndfieldWeaponPool weapon_pool(rng);

    chars.weapon_points = opt.initial_arsenal_quota;
    long long spent_points = 0;

    while (total_limited_chars(chars) < opt.target_char ||
           total_limited_weapons(weapons) < opt.target_weapon) {
        if (total_limited_chars(chars) < opt.target_char) {
            char_pool.pull(chars);
            continue;
        }

        if (total_limited_weapons(weapons) < opt.target_weapon) {
            if (chars.weapon_points >= WEAPON_TEN_PULL_COST) {
                chars.weapon_points -= WEAPON_TEN_PULL_COST;
                spent_points += WEAPON_TEN_PULL_COST;
                weapon_pool.ten_pull(weapons);
            } else {
                char_pool.pull(chars);
            }
        }
    }

    SampleResult result;
    result.total_pulls = chars.pulls + weapons.total_draws;
    result.char_pulls = chars.pulls;
    result.weapon_pulls = weapons.total_draws;
    result.weapon_ten_pulls = weapons.total_draws / 10;
    result.weapon_points_spent = spent_points;
    result.weapon_points_left = chars.weapon_points;
    result.weapon_points_earned = chars.weapon_points + spent_points - opt.initial_arsenal_quota;
    result.yellow_tickets = chars.yellow_tickets;
    result.limited_chars = total_limited_chars(chars);
    result.limited_chars_drawn = chars.limited;
    result.limited_chars_gifted = chars.limited_gifted;
    result.limited_chars_free10 = chars.limited_free10;
    result.overflow_limited_chars = std::max(0, result.limited_chars - opt.target_char);
    result.weapon_limited = total_limited_weapons(weapons);
    result.weapon_limited_pulled = weapons.pulled_limited;
    result.weapon_limited_gifted = weapons.gifted_limited;
    result.weapon_standard_pulled = weapons.pulled_standard;
    result.weapon_standard_boxes = weapons.gifted_standard_boxes;
    result.char_five_star = chars.five_star;
    result.char_non_limited = chars.non_limited;
    result.char_standard = chars.standard;
    return result;
}

class WuWaSimulator {
public:
    explicit WuWaSimulator(std::mt19937& rng) : rng_(rng), dist_(0.0, 1.0) {}

    SampleResult simulate_one(const Options& opt) {
        reset();
        SampleResult result;
        int coral = opt.initial_coral;

        while (result.weapon_limited < opt.target_weapon) {
            result.weapon_pulls += 1;
            Event ev = pull_from_pool(true);
            if (ev.is_five) {
                result.weapon_limited += 1;
            } else if (ev.is_four && ev.is_character) {
                coral += 8;
                result.four_character += 1;
            } else if (ev.is_four) {
                coral += 3;
                result.four_weapon += 1;
            }
        }

        while (result.limited_chars_drawn + result.wuwa_char_exchanged < opt.target_char) {
            result.char_pulls += 1;
            Event ev = pull_from_pool(false);
            if (ev.is_five) {
                if (ev.is_limited) {
                    result.limited_chars_drawn += 1;
                    coral += 15;
                } else {
                    result.char_standard += 1;
                    coral += 45;
                }
            } else if (ev.is_four && ev.is_character) {
                coral += 8;
                result.four_character += 1;
            } else if (ev.is_four) {
                coral += 3;
                result.four_weapon += 1;
            }

            if (opt.exchange_enabled && result.limited_chars_drawn > 0) {
                while (coral >= 360 &&
                       result.limited_chars_drawn + result.wuwa_char_exchanged < opt.target_char &&
                       result.wuwa_char_exchanged < 2) {
                    coral -= 360;
                    result.wuwa_char_exchanged += 1;
                }
            }
        }

        result.total_pulls = result.weapon_pulls + result.char_pulls;
        result.limited_chars = result.limited_chars_drawn + result.wuwa_char_exchanged;
        result.coral_left = coral;
        return result;
    }

private:
    struct Event {
        bool is_five = false;
        bool is_four = false;
        bool is_limited = false;
        bool is_character = false;
    };

    std::mt19937& rng_;
    std::uniform_real_distribution<double> dist_;
    int five_counter_char = 0;
    int four_counter_char = 0;
    int five_counter_weap = 0;
    int four_counter_weap = 0;
    bool prev_char_five_was_standard = false;
    bool prev_char_four_was_weapon = false;
    bool prev_weap_four_was_character = false;

    void reset() {
        five_counter_char = 0;
        four_counter_char = 0;
        five_counter_weap = 0;
        four_counter_weap = 0;
        prev_char_five_was_standard = false;
        prev_char_four_was_weapon = false;
        prev_weap_four_was_character = false;
    }

    double five_rate(int counter) const {
        if (counter <= 65) return 0.008;
        if (counter <= 70) return 0.008 + (counter - 65) * 0.04;
        if (counter <= 75) return 0.008 + 5 * 0.04 + (counter - 70) * 0.08;
        if (counter <= 78) return 0.008 + 5 * 0.04 + 5 * 0.08 + (counter - 75) * 0.10;
        return 1.0;
    }

    Event pull_from_pool(bool weapon_pool) {
        Event ev;
        if (weapon_pool) {
            five_counter_weap += 1;
            four_counter_weap += 1;
            double p5 = five_rate(five_counter_weap);
            double roll = dist_(rng_);
            if (roll < p5) {
                ev.is_five = true;
                ev.is_limited = true;
                five_counter_weap = 0;
                four_counter_weap = 0;
                return ev;
            }

            if (four_counter_weap >= 10 || roll < 0.08 + p5) {
                ev.is_four = true;
                if (prev_weap_four_was_character) {
                    ev.is_character = false;
                } else {
                    ev.is_character = dist_(rng_) < 0.25;
                }
                four_counter_weap = 0;
                prev_weap_four_was_character = ev.is_character;
            }
            return ev;
        }

        five_counter_char += 1;
        four_counter_char += 1;
        double p5 = five_rate(five_counter_char);
        double roll = dist_(rng_);
        if (roll < p5) {
            ev.is_five = true;
            bool is_standard = false;
            if (prev_char_five_was_standard) {
                is_standard = false;
            } else {
                is_standard = dist_(rng_) < 0.5;
            }
            ev.is_limited = !is_standard;
            five_counter_char = 0;
            four_counter_char = 0;
            prev_char_five_was_standard = is_standard;
            return ev;
        }

        if (four_counter_char >= 10 || roll < 0.08 + p5) {
            ev.is_four = true;
            if (prev_char_four_was_weapon) {
                ev.is_character = true;
            } else {
                ev.is_character = dist_(rng_) < 0.75;
            }
            four_counter_char = 0;
            prev_char_four_was_weapon = !ev.is_character;
        }
        return ev;
    }
};

double percentile_sorted(const std::vector<int>& values, double p) {
    if (values.empty()) return 0.0;
    const double idx = p * (values.size() - 1);
    const int lo = static_cast<int>(std::floor(idx));
    const int hi = static_cast<int>(std::ceil(idx));
    if (lo == hi) return values[lo];
    const double frac = idx - lo;
    return values[lo] * (1.0 - frac) + values[hi] * frac;
}

double mean_from_ll(long double sum, size_t n) {
    return n == 0 ? 0.0 : static_cast<double>(sum / static_cast<long double>(n));
}

bool use_weapon_distribution(const Options& opt);
std::string primary_metric_key(const Options& opt);
int primary_metric_value(const Options& opt, const SampleResult& result);

Metrics compute_metrics(const Options& opt, const std::vector<SampleResult>& results) {
    Metrics m;
    if (results.empty()) return m;

    std::vector<int> char_pulls;
    std::vector<int> weapon_pulls;
    std::vector<int> primary_values;
    char_pulls.reserve(results.size());
    weapon_pulls.reserve(results.size());
    primary_values.reserve(results.size());

    long double sum_char = 0.0;
    long double sum_char_sq = 0.0;
    long double sum_total = 0.0;
    long double sum_total_sq = 0.0;
    long double sum_weapon = 0.0;
    long double sum_weapon_sq = 0.0;
    long double sum_weapon_ten = 0.0;
    long double sum_points_left = 0.0;
    long double sum_points_earned = 0.0;
    long double sum_tickets = 0.0;
    long double sum_overflow = 0.0;
    long double sum_char_standard = 0.0;
    long double sum_coral_left = 0.0;
    long double sum_wuwa_exchanged = 0.0;
    long double sum_four_weapon = 0.0;
    long double sum_four_character = 0.0;

    for (const auto& r : results) {
        char_pulls.push_back(r.char_pulls);
        weapon_pulls.push_back(r.weapon_pulls);
        primary_values.push_back(primary_metric_value(opt, r));
        sum_char += r.char_pulls;
        sum_char_sq += static_cast<long double>(r.char_pulls) * r.char_pulls;
        sum_total += r.total_pulls;
        sum_total_sq += static_cast<long double>(r.total_pulls) * r.total_pulls;
        sum_weapon += r.weapon_pulls;
        sum_weapon_sq += static_cast<long double>(r.weapon_pulls) * r.weapon_pulls;
        sum_weapon_ten += r.weapon_ten_pulls;
        sum_points_left += r.weapon_points_left;
        sum_points_earned += r.weapon_points_earned;
        sum_tickets += r.yellow_tickets;
        sum_overflow += r.overflow_limited_chars;
        sum_char_standard += r.char_standard;
        sum_coral_left += r.coral_left;
        sum_wuwa_exchanged += r.wuwa_char_exchanged;
        sum_four_weapon += r.four_weapon;
        sum_four_character += r.four_character;
    }

    std::sort(char_pulls.begin(), char_pulls.end());
    std::sort(weapon_pulls.begin(), weapon_pulls.end());
    std::sort(primary_values.begin(), primary_values.end());
    const long double n = static_cast<long double>(results.size());
    m.mean_total_pulls = mean_from_ll(sum_total, results.size());
    const long double total_variance = sum_total_sq / n - static_cast<long double>(m.mean_total_pulls) * m.mean_total_pulls;
    m.stddev_total_pulls = std::sqrt(static_cast<double>(std::max<long double>(0.0, total_variance)));
    m.mean_char_pulls = mean_from_ll(sum_char, results.size());
    const long double variance = sum_char_sq / n - static_cast<long double>(m.mean_char_pulls) * m.mean_char_pulls;
    m.stddev_char_pulls = std::sqrt(static_cast<double>(std::max<long double>(0.0, variance)));
    m.p50 = percentile_sorted(primary_values, 0.50);
    m.p75 = percentile_sorted(primary_values, 0.75);
    m.p90 = percentile_sorted(primary_values, 0.90);
    m.p95 = percentile_sorted(primary_values, 0.95);
    m.mean_weapon_pulls = mean_from_ll(sum_weapon, results.size());
    const long double weapon_variance = sum_weapon_sq / n - static_cast<long double>(m.mean_weapon_pulls) * m.mean_weapon_pulls;
    m.stddev_weapon_pulls = std::sqrt(static_cast<double>(std::max<long double>(0.0, weapon_variance)));
    m.mean_weapon_ten_pulls = mean_from_ll(sum_weapon_ten, results.size());
    m.mean_weapon_points_left = mean_from_ll(sum_points_left, results.size());
    m.mean_weapon_points_earned = mean_from_ll(sum_points_earned, results.size());
    m.mean_yellow_tickets = mean_from_ll(sum_tickets, results.size());
    m.mean_overflow_limited_chars = mean_from_ll(sum_overflow, results.size());
    m.mean_char_standard = mean_from_ll(sum_char_standard, results.size());
    m.mean_coral_left = mean_from_ll(sum_coral_left, results.size());
    m.mean_wuwa_char_exchanged = mean_from_ll(sum_wuwa_exchanged, results.size());
    m.mean_four_weapon = mean_from_ll(sum_four_weapon, results.size());
    m.mean_four_character = mean_from_ll(sum_four_character, results.size());
    return m;
}

std::string json_escape(const std::string& s) {
    std::ostringstream out;
    for (char c : s) {
        switch (c) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default: out << c; break;
        }
    }
    return out.str();
}

bool use_weapon_distribution(const Options& opt) {
    return opt.target_char == 0 && opt.target_weapon > 0;
}

std::string primary_metric_key(const Options& opt) {
    if (opt.sim == "wuwa") return "total_pulls";
    return use_weapon_distribution(opt) ? "weapon_pulls" : "char_pulls";
}

int primary_metric_value(const Options& opt, const SampleResult& result) {
    if (opt.sim == "wuwa") return result.total_pulls;
    return use_weapon_distribution(opt) ? result.weapon_pulls : result.char_pulls;
}

void write_distribution(const fs::path& path, const Options& opt, const std::vector<SampleResult>& results) {
    std::map<int, int> dist;
    for (const auto& r : results) dist[primary_metric_value(opt, r)] += 1;

    std::ofstream out(path, std::ios::binary);
    out << primary_metric_key(opt) << ",frequency\n";
    for (const auto& [pulls, freq] : dist) {
        out << pulls << "," << freq << "\n";
    }
}

void write_percentiles(const fs::path& path, const Options& opt, const std::vector<SampleResult>& results) {
    std::vector<int> values;
    values.reserve(results.size());
    for (const auto& r : results) values.push_back(primary_metric_value(opt, r));
    std::sort(values.begin(), values.end());

    std::ofstream out(path, std::ios::binary);
    out << "percentile," << primary_metric_key(opt) << "\n";
    for (int p = 1; p <= 100; ++p) {
        out << p << "," << percentile_sorted(values, p / 100.0) << "\n";
    }
}

void write_stats(const fs::path& path, const Options& opt, const Metrics& m) {
    std::ofstream out(path, std::ios::binary);
    out << std::fixed << std::setprecision(6);
    out << "metric,value\n";
    out << "simulator," << opt.sim << "\n";
    out << "samples," << opt.samples << "\n";
    out << "target_char," << opt.target_char << "\n";
    out << "target_weapon," << opt.target_weapon << "\n";
    out << "initial_arsenal_quota," << opt.initial_arsenal_quota << "\n";
    out << "initial_coral," << opt.initial_coral << "\n";
    out << "exchange_enabled," << (opt.exchange_enabled ? 1 : 0) << "\n";
    out << "primary_distribution_metric," << primary_metric_key(opt) << "\n";
    out << "mean_total_pulls," << m.mean_total_pulls << "\n";
    out << "stddev_total_pulls," << m.stddev_total_pulls << "\n";
    out << "mean_char_pulls," << m.mean_char_pulls << "\n";
    out << "stddev_char_pulls," << m.stddev_char_pulls << "\n";
    out << "p50," << m.p50 << "\n";
    out << "p75," << m.p75 << "\n";
    out << "p90," << m.p90 << "\n";
    out << "p95," << m.p95 << "\n";
    out << "mean_weapon_pulls," << m.mean_weapon_pulls << "\n";
    out << "stddev_weapon_pulls," << m.stddev_weapon_pulls << "\n";
    out << "mean_weapon_ten_pulls," << m.mean_weapon_ten_pulls << "\n";
    out << "mean_arsenal_quota_left," << m.mean_weapon_points_left << "\n";
    out << "mean_arsenal_quota_earned," << m.mean_weapon_points_earned << "\n";
    out << "mean_guarantee_quota," << m.mean_yellow_tickets << "\n";
    out << "mean_overflow_limited_chars," << m.mean_overflow_limited_chars << "\n";
    out << "mean_char_standard," << m.mean_char_standard << "\n";
    out << "mean_coral_left," << m.mean_coral_left << "\n";
    out << "mean_wuwa_char_exchanged," << m.mean_wuwa_char_exchanged << "\n";
    out << "mean_four_weapon," << m.mean_four_weapon << "\n";
    out << "mean_four_character," << m.mean_four_character << "\n";
}

void write_summary(const fs::path& path, const Options& opt, const Metrics& m, unsigned int seed) {
    std::ofstream out(path, std::ios::binary);
    out << std::fixed << std::setprecision(6);
    out << "{\n";
    out << "  \"simulator\": \"" << json_escape(opt.sim) << "\",\n";
    out << "  \"samples\": " << opt.samples << ",\n";
    out << "  \"seed\": " << seed << ",\n";
    out << "  \"params\": {\n";
    out << "    \"target_char\": " << opt.target_char << ",\n";
    out << "    \"target_weapon\": " << opt.target_weapon << ",\n";
    out << "    \"initial_arsenal_quota\": " << opt.initial_arsenal_quota << ",\n";
    out << "    \"initial_coral\": " << opt.initial_coral << ",\n";
    out << "    \"exchange_enabled\": " << (opt.exchange_enabled ? "true" : "false") << ",\n";
    out << "    \"arsenal_ten_pull_cost\": " << WEAPON_TEN_PULL_COST << "\n";
    out << "  },\n";
    out << "  \"primary_distribution_metric\": \"" << primary_metric_key(opt) << "\",\n";
    out << "  \"metrics\": {\n";
    out << "    \"mean_total_pulls\": " << m.mean_total_pulls << ",\n";
    out << "    \"stddev_total_pulls\": " << m.stddev_total_pulls << ",\n";
    out << "    \"mean_char_pulls\": " << m.mean_char_pulls << ",\n";
    out << "    \"stddev_char_pulls\": " << m.stddev_char_pulls << ",\n";
    out << "    \"p50\": " << m.p50 << ",\n";
    out << "    \"p75\": " << m.p75 << ",\n";
    out << "    \"p90\": " << m.p90 << ",\n";
    out << "    \"p95\": " << m.p95 << ",\n";
    out << "    \"mean_weapon_pulls\": " << m.mean_weapon_pulls << ",\n";
    out << "    \"stddev_weapon_pulls\": " << m.stddev_weapon_pulls << ",\n";
    out << "    \"mean_weapon_ten_pulls\": " << m.mean_weapon_ten_pulls << ",\n";
    out << "    \"mean_arsenal_quota_left\": " << m.mean_weapon_points_left << ",\n";
    out << "    \"mean_arsenal_quota_earned\": " << m.mean_weapon_points_earned << ",\n";
    out << "    \"mean_guarantee_quota\": " << m.mean_yellow_tickets << ",\n";
    out << "    \"mean_overflow_limited_chars\": " << m.mean_overflow_limited_chars << ",\n";
    out << "    \"mean_char_standard\": " << m.mean_char_standard << ",\n";
    out << "    \"mean_coral_left\": " << m.mean_coral_left << ",\n";
    out << "    \"mean_wuwa_char_exchanged\": " << m.mean_wuwa_char_exchanged << ",\n";
    out << "    \"mean_four_weapon\": " << m.mean_four_weapon << ",\n";
    out << "    \"mean_four_character\": " << m.mean_four_character << "\n";
    out << "  },\n";
    out << "  \"files\": {\n";
    out << "    \"distribution\": \"distribution.csv\",\n";
    out << "    \"percentiles\": \"percentiles.csv\",\n";
    out << "    \"stats\": \"stats.csv\"\n";
    out << "  }\n";
    out << "}\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options opt = parse_args(argc, argv);
        fs::create_directories(opt.out_dir);

        unsigned int seed = opt.seed;
        if (seed == 0) {
            std::random_device rd;
            seed = rd();
        }

        std::mt19937 rng(seed);
        std::vector<SampleResult> results;
        results.reserve(opt.samples);

        std::cout << "START simulator=" << opt.sim
                  << " samples=" << opt.samples
                  << " target_char=" << opt.target_char
                  << " target_weapon=" << opt.target_weapon
                  << " initial_arsenal_quota=" << opt.initial_arsenal_quota
                  << " initial_coral=" << opt.initial_coral
                  << " exchange_enabled=" << (opt.exchange_enabled ? 1 : 0)
                  << " seed=" << seed
                  << " out=" << opt.out_dir << "\n";

        const int progress_step = std::max(1, opt.samples / 20);
        WuWaSimulator wuwa_sim(rng);
        for (int i = 0; i < opt.samples; ++i) {
            if (opt.sim == "wuwa") {
                results.push_back(wuwa_sim.simulate_one(opt));
            } else {
                results.push_back(simulate_one(opt, rng));
            }
            if ((i + 1) % progress_step == 0 || i + 1 == opt.samples) {
                std::cout << "PROGRESS " << (i + 1) << "/" << opt.samples << "\n";
            }
        }

        const Metrics metrics = compute_metrics(opt, results);
        const fs::path out_dir(opt.out_dir);
        write_distribution(out_dir / "distribution.csv", opt, results);
        write_percentiles(out_dir / "percentiles.csv", opt, results);
        write_stats(out_dir / "stats.csv", opt, metrics);
        write_summary(out_dir / "summary.json", opt, metrics, seed);

        std::cout << "DONE summary=" << (out_dir / "summary.json").string() << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR " << e.what() << "\n";
        return 1;
    }
}
