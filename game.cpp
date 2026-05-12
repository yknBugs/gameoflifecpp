#include <random>
#include <string>
#include <set>
#include <array>
#include <map>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <csignal>
#include <cerrno>

#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <fcntl.h>
#endif

namespace ansi {
    constexpr const char* reset = "\033[0m";
    constexpr const char* black = "\033[30m";
    constexpr const char* dark_blue = "\033[34m";
    constexpr const char* dark_green = "\033[32m";
    constexpr const char* dark_cyan = "\033[36m";
    constexpr const char* dark_red = "\033[31m";
    constexpr const char* dark_purple = "\033[35m";
    constexpr const char* gold = "\033[33m";
    constexpr const char* gray = "\033[37m";
    constexpr const char* dark_gray = "\033[90m";
    constexpr const char* blue = "\033[94m";
    constexpr const char* green = "\033[92m";
    constexpr const char* cyan = "\033[96m";
    constexpr const char* red = "\033[91m";
    constexpr const char* purple = "\033[95m";
    constexpr const char* yellow = "\033[93m";
    constexpr const char* white = "\033[97m";
    constexpr const char* save_screen = "\033[?1049h";
    constexpr const char* restore_screen = "\033[?1049l";
    constexpr const char* clear_screen = "\033[2J";
    constexpr const char* clear_all = "\033[2J\033[H";
    constexpr const char* clear_remaining = "\033[J";
    constexpr const char* clear_line = "\033[2K";
    constexpr const char* clear_line_remaining = "\033[K";
    constexpr const char* hide_cursor = "\033[?25l";
    constexpr const char* show_cursor = "\033[?25h";
    constexpr const char* reset_cursor = "\033[H";
}

enum class StopReason {
    Unknown,
    Stable,
    Extinct,
    Oscillating,
    MaxSteps,
    Travelling,
};

typedef std::pair<int, int> Coord;

static void printHelp() {
    std::cout << "Game - Command Line Interface\n"
        << "==============================================\n\n"
        << "Usage: ./game [options] [input_file]\n\n"
        << "Options:\n"
        << "  -h, --help       Show this help message\n"
        << "  -a, --animate [MS]  Run in animation mode, optional update interval in ms (default: 1000)\n"
        << "  -s, --screen W H    Set viewport size (width height), e.g. 70 35\n"
        << "  -v, --view X Y      Set viewport top-left coordinate\n"
        << "  --stdin          Read initial state from stdin/keyboard (pipe or redirect)\n"
        << "  --random         Generate random initial state (default if no input)\n"
        << "  --edit           Start in interactive edit mode with an empty initial state\n"
        << "  --steps N        Set maximum steps (default: auto-detect)\n\n"
        << "Input Formats:\n"
        << "  - Space-separated: 0 1 0 1\n"
        << "  - Comma-separated: 0,1,0,1\n"
        << "  - Characters: O=alive, .=dead, *=alive, X=dead\n"
        << "  - Comments starting with # are ignored\n\n"
        << "Examples:\n"
        << "  ./game                           # Random initial state, quick mode\n"
        << "  ./game -a                        # Animation mode (1000ms/update)\n"
        << "  ./game -a 500                    # Animation mode (500ms/update)\n"
        << "  ./game glider.txt                # Read initial state from file\n"
        << "  ./game --screen 70 35 --view 0 0 # 70x35 viewport from (0,0)\n"
#ifdef _WIN32
        << "  ./game --stdin                   # Interactive stdin input, finish with Ctrl+Z then Enter\n"
#else
        << "  ./game --stdin                   # Interactive stdin input, finish with Ctrl+D\n"
#endif
        << "  ./game --edit                    # Start directly in edit mode\n"
        << std::endl;
}

struct CliOptions {
    bool showHelp = false;
    bool animate = false;
    std::size_t animateIntervalMs = 1000;
    bool useStdin = false;
    bool useRandom = false;
    bool startInEdit = false;
    bool hasSteps = false;
    std::size_t steps = 0;
    int screenWidth = 72;
    int screenHeight = 36;
    int viewX = -36;
    int viewY = -18;
    std::string inputFile;
};

static bool parseSignedInt(const std::string& raw, int& outValue) {
    try {
        std::size_t consumed = 0;
        int value = std::stoi(raw, &consumed);
        if (consumed != raw.size()) {
            return false;
        }
        outValue = value;
        return true;
    }
    catch (...) {
        return false;
    }
}

static bool parseUnsignedSize(const std::string& raw, std::size_t& outValue) {
    try {
        std::size_t consumed = 0;
        std::size_t value = static_cast<std::size_t>(std::stoull(raw, &consumed));
        if (consumed != raw.size()) {
            return false;
        }
        outValue = value;
        return true;
    }
    catch (...) {
        return false;
    }
}

static bool isAliveToken(const std::string& token) {
    if (token.empty()) {
        return false;
    }
    if (token == "1" || token == "O" || token == "o" || token == "*") {
        return true;
    }
    return false;
}

static std::string trim(const std::string& s) {
    std::size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])) != 0) {
        start++;
    }
    std::size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])) != 0) {
        end--;
    }
    return s.substr(start, end - start);
}

static bool parseArgs(int argc, char** argv, CliOptions& options, std::string& errorMessage) {
    for (int i = 1; i < argc; i++) {
        const std::string arg(argv[i]);
        if (arg == "-h" || arg == "--help") {
            options.showHelp = true;
        }
        else if (arg == "-a" || arg == "--animate") {
            options.animate = true;
            if (i + 1 < argc) {
                const std::string nextArg(argv[i + 1]);
                if (!nextArg.empty() && nextArg[0] != '-') {
                    std::size_t intervalMs = 0;
                    if (!parseUnsignedSize(nextArg, intervalMs) || intervalMs == 0) {
                        errorMessage = "Invalid value for --animate. Expected a positive integer milliseconds value.";
                        return false;
                    }
                    options.animateIntervalMs = intervalMs;
                    i++;
                }
            }
        }
        else if (arg == "-s" || arg == "--screen") {
            if (i + 2 >= argc) {
                errorMessage = "Missing values for --screen. Expected: --screen <width> <height>.";
                return false;
            }
            int width = 0;
            int height = 0;
            if (!parseSignedInt(argv[++i], width) || !parseSignedInt(argv[++i], height) || width <= 0 || height <= 0) {
                errorMessage = "Invalid values for --screen. Width and height must be positive integers.";
                return false;
            }
            options.screenWidth = width;
            options.screenHeight = height;
        }
        else if (arg == "-v" || arg == "--view") {
            if (i + 2 >= argc) {
                errorMessage = "Missing values for --view. Expected: --view <x> <y>.";
                return false;
            }
            int x = 0;
            int y = 0;
            if (!parseSignedInt(argv[++i], x) || !parseSignedInt(argv[++i], y)) {
                errorMessage = "Invalid values for --view. X and Y must be integers.";
                return false;
            }
            options.viewX = x;
            options.viewY = y;
        }
        else if (arg == "--stdin") {
            options.useStdin = true;
        }
        else if (arg == "--random") {
            options.useRandom = true;
        }
        else if (arg == "--edit") {
            options.startInEdit = true;
        }
        else if (arg == "--steps") {
            if (i + 1 >= argc) {
                errorMessage = "Missing value for --steps.";
                return false;
            }
            try {
                options.steps = static_cast<std::size_t>(std::stoull(argv[++i]));
                options.hasSteps = true;
            }
            catch (...) {
                errorMessage = "Invalid value for --steps. Expected a non-negative integer.";
                return false;
            }
        }
        else if (!arg.empty() && arg[0] == '-') {
            errorMessage = "Unknown option: " + arg;
            return false;
        }
        else {
            if (!options.inputFile.empty()) {
                errorMessage = "Only one input file is supported.";
                return false;
            }
            options.inputFile = arg;
        }
    }

    if (options.useStdin && !options.inputFile.empty()) {
        errorMessage = "Cannot use --stdin and input_file at the same time.";
        return false;
    }

    if (options.useRandom && (options.useStdin || !options.inputFile.empty())) {
        errorMessage = "Cannot combine --random with --stdin or input_file.";
        return false;
    }

    if (options.startInEdit && (options.useStdin || options.useRandom || !options.inputFile.empty())) {
        errorMessage = "Cannot combine --edit with --stdin, --random, or input_file.";
        return false;
    }

    return true;
}

class GameState {
public:
    std::set<Coord> livingCells;

    GameState() = default;

    explicit GameState(const std::set<Coord>& initialCells) : livingCells(initialCells) {}

    GameState(const GameState& other) : livingCells(other.livingCells) {}

    GameState(GameState&& other) noexcept : livingCells(std::move(other.livingCells)) {}

    int countNeighbors(const Coord& cell) const {
        int count = 0;
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                if (dx == 0 && dy == 0) {
                    continue;
                }
                Coord neighbor(cell.first + dx, cell.second + dy);
                if (livingCells.count(neighbor) > 0) {
                    count++;
                }
            }
        }
        return count;
    }

    std::map<Coord, int> countNeighbors() const {
        std::map<Coord, int> neighborCounts;
        for (const auto& cell : livingCells) {
            for (int dx = -1; dx <= 1; dx++) {
                for (int dy = -1; dy <= 1; dy++) {
                    if (dx == 0 && dy == 0) {
                        continue;
                    }
                    Coord neighbor(cell.first + dx, cell.second + dy);
                    neighborCounts[neighbor]++;
                }
            }
        }
        return neighborCounts;
    }

    GameState next() const {
        std::set<Coord> newLivingCells;
        std::map<Coord, int> neighborCounts = this->countNeighbors();
        for (const auto& [cell, count] : neighborCounts) {
            if (livingCells.count(cell) > 0) {
                if (count == 2 || count == 3) {
                    newLivingCells.insert(cell);
                }
            } else {
                if (count == 3) {
                    newLivingCells.insert(cell);
                }
            }
        }
        return GameState(newLivingCells);
    }

    Coord size() const {
        if (livingCells.empty()) {
            return { 0, 0 };
        }
        int min_x = std::numeric_limits<int>::max();
        int min_y = std::numeric_limits<int>::max();
        int max_x = std::numeric_limits<int>::min();
        int max_y = std::numeric_limits<int>::min();
        for (const auto& cell : livingCells) {
            if (cell.first < min_x) {
                min_x = cell.first;
            }
            if (cell.second < min_y) {
                min_y = cell.second;
            }
            if (cell.first > max_x) {
                max_x = cell.first;
            }
            if (cell.second > max_y) {
                max_y = cell.second;
            }
        }
        return { max_x - min_x + 1, max_y - min_y + 1 };
    }

    Coord min() const {
        if (livingCells.empty()) {
            return { 0, 0 };
        }
        int min_x = std::numeric_limits<int>::max();
        int min_y = std::numeric_limits<int>::max();
        for (const auto& cell : livingCells) {
            if (cell.first < min_x) {
                min_x = cell.first;
            }
            if (cell.second < min_y) {
                min_y = cell.second;
            }
        }
        return { min_x, min_y };
    }

    Coord max() const {
        if (livingCells.empty()) {
            return { 0, 0 };
        }
        int max_x = std::numeric_limits<int>::min();
        int max_y = std::numeric_limits<int>::min();
        for (const auto& cell : livingCells) {
            if (cell.first > max_x) {
                max_x = cell.first;
            }
            if (cell.second > max_y) {
                max_y = cell.second;
            }
        }
        return { max_x, max_y };
    }

    std::size_t population() const {
        return livingCells.size();
    }

    std::size_t area() const {
        Coord s = this->size();
        return static_cast<size_t>(s.first) * static_cast<size_t>(s.second);
    }

    std::size_t death() const {
        return this->area() - this->population();
    }

    double density() const {
        std::size_t a = this->area();
        return a == 0 ? 0.0 : static_cast<double>(this->population()) / a;
    }

    bool insert(const Coord& cell) {
        if (livingCells.count(cell) > 0) {
            return false;
        } else {
            livingCells.insert(cell);
            return true;
        }
    }

    bool reverse(const Coord& cell) {
        if (livingCells.count(cell) > 0) {
            livingCells.erase(cell);
            return false;
        } else {
            livingCells.insert(cell);
            return true;
        }
    }

    bool erase(const Coord& cell) {
        if (livingCells.count(cell) > 0) {
            livingCells.erase(cell);
            return true;
        } else {
            return false;
        }
    }

    GameState& operator = (const GameState& other) {
        if (this != &other) {
            livingCells = other.livingCells;
        }
        return *this;
    }

    GameState operator + (const std::size_t& n) const {
        GameState result(*this);
        for (std::size_t i = 0; i < n; i++) {
            result = result.next();
        }
        return result;
    }

    GameState operator + (const GameState& other) const {
        GameState result(*this);
        for (const auto& cell : other.livingCells) {
            if (result.livingCells.count(cell) == 0) {
                result.livingCells.insert(cell);
            }
        }
        return result;
    }

    GameState operator + (const Coord& cell) const {
        GameState result(*this);
        if (result.livingCells.count(cell) == 0) {
            result.livingCells.insert(cell);
        }
        return result;
    }

    GameState& operator += (const std::size_t& n) {
        for (std::size_t i = 0; i < n; i++) {
            *this = this->next();
        }
        return *this;
    }

    GameState& operator += (const GameState& other) {
        for (const auto& cell : other.livingCells) {
            if (livingCells.count(cell) == 0) {
                livingCells.insert(cell);
            }
        }
        return *this;
    }

    GameState& operator += (const Coord& cell) {
        if (livingCells.count(cell) == 0) {
            livingCells.insert(cell);
        }
        return *this;
    }

    GameState operator - (const GameState& other) const {
        GameState result(*this);
        for (const auto& cell : other.livingCells) {
            if (result.livingCells.count(cell) > 0) {
                result.livingCells.erase(cell);
            }
        }
        return result;
    }

    GameState operator - (const Coord& cell) const {
        GameState result(*this);
        if (result.livingCells.count(cell) > 0) {
            result.livingCells.erase(cell);
        }
        return result;
    }

    GameState& operator -= (const GameState& other) {
        for (const auto& cell : other.livingCells) {
            if (livingCells.count(cell) > 0) {
                livingCells.erase(cell);
            }
        }
        return *this;
    }

    GameState& operator -= (const Coord& cell) {
        if (livingCells.count(cell) > 0) {
            livingCells.erase(cell);
        }
        return *this;
    }

    GameState operator & (const GameState& other) const {
        GameState result;
        for (const auto& cell : livingCells) {
            if (other.livingCells.count(cell) > 0) {
                result.livingCells.insert(cell);
            }
        }
        return result;
    }

    GameState operator | (const GameState& other) const {
        return *this + other;
    }

    bool operator == (const GameState& other) const {
        return livingCells == other.livingCells;
    }

    bool operator != (const GameState& other) const {
        return livingCells != other.livingCells;
    }

    bool operator > (const GameState& other) const {
        return livingCells.size() > other.livingCells.size();
    }

    bool operator < (const GameState& other) const {
        return livingCells.size() < other.livingCells.size();
    }

    bool operator >= (const GameState& other) const {
        return livingCells.size() >= other.livingCells.size();
    }

    bool operator <= (const GameState& other) const {
        return livingCells.size() <= other.livingCells.size();
    }

    bool operator [] (const Coord& cell) const {
        return livingCells.count(cell) > 0;
    }

    explicit operator std::size_t() const {
        return livingCells.size();
    }

    operator std::set<Coord>() const {
        return livingCells;
    }
};

static bool parseInitialState(std::istream& in, GameState& outState) {
    std::set<Coord> cells;
    std::string line;
    int y = 0;
    bool parsedAnyRow = false;

    while (std::getline(in, line)) {
        std::size_t commentPos = line.find('#');
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        std::vector<std::string> tokens;
        std::string token;
        bool hasSeparator = false;
        for (char ch : line) {
            if (ch == ',' || std::isspace(static_cast<unsigned char>(ch)) != 0) {
                hasSeparator = true;
                if (!token.empty()) {
                    tokens.push_back(token);
                    token.clear();
                }
            } else {
                token.push_back(ch);
            }
        }
        if (!token.empty()) {
            tokens.push_back(token);
        }

        int x = 0;
        bool parsedThisRow = false;
        if (hasSeparator) {
            for (const std::string& t : tokens) {
                if (isAliveToken(t)) {
                    cells.insert({ x, y });
                }
                x++;
                parsedThisRow = true;
            }
        }
        else {
            for (char ch : line) {
                if (std::isspace(static_cast<unsigned char>(ch)) != 0 || ch == ',') {
                    continue;
                }
                if (ch == 'O' || ch == 'o' || ch == '*' || ch == '1') {
                    cells.insert({ x, y });
                    x++;
                    parsedThisRow = true;
                } else if (ch == '.' || ch == 'X' || ch == 'x' || ch == '0') {
                    x++;
                    parsedThisRow = true;
                }
            }
        }

        if (parsedThisRow) {
            parsedAnyRow = true;
            y++;
        }
    }

    if (!parsedAnyRow) {
        return false;
    }

    outState = GameState(cells);
    return true;
}

static std::vector<std::string> splitWhitespaceTokens(const std::string& text) {
    std::vector<std::string> tokens;
    std::istringstream in(text);
    std::string token;
    while (in >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

static bool parseInlinePatternText(const std::string& rawPattern, GameState& outState) {
    std::string pattern = trim(rawPattern);
    if (pattern.size() >= 2) {
        const char first = pattern.front();
        const char last = pattern.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            pattern = pattern.substr(1, pattern.size() - 2);
        }
    }

    std::string normalized;
    normalized.reserve(pattern.size());
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        const char ch = pattern[i];
        if (ch == '\\' && i + 1 < pattern.size()) {
            const char next = pattern[i + 1];
            if (next == 'n') {
                normalized.push_back('\n');
                i++;
                continue;
            }
            if (next == 't') {
                normalized.push_back('\t');
                i++;
                continue;
            }
            if (next == 'r') {
                normalized.push_back('\r');
                i++;
                continue;
            }
            if (next == '\\') {
                normalized.push_back('\\');
                i++;
                continue;
            }
        }
        normalized.push_back(ch);
    }

    std::istringstream in(normalized);
    return parseInitialState(in, outState);
}

static std::string unquoteMaybe(const std::string& raw) {
    std::string value = trim(raw);
    if (value.size() >= 2) {
        const char first = value.front();
        const char last = value.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            value = value.substr(1, value.size() - 2);
        }
    }
    return value;
}

static bool parsePatternFromFilePath(const std::string& rawPath, GameState& outState, std::string& errorMessage) {
    std::string path = unquoteMaybe(rawPath);
    if (path.empty()) {
        errorMessage = "File path is empty.";
        return false;
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        errorMessage = "Failed to open input file: " + path;
        return false;
    }

    if (!parseInitialState(file, outState)) {
        errorMessage = "No valid pattern found in input file: " + path;
        return false;
    }

    return true;
}

class InitialStateGenerator {
private:
    std::mt19937 rng;

    InitialStateGenerator() : rng(static_cast<std::mt19937::result_type>(std::chrono::system_clock::now().time_since_epoch().count())) {}

    int rand_int(int min, int max) {
        std::uniform_int_distribution<int> dist(min, max);
        return dist(rng);
    }

    double rand_double(double min, double max) {
        std::uniform_real_distribution<double> dist(min, max);
        return dist(rng);
    }

    double rand_normal(double mean, double stddev) {
        std::normal_distribution<double> dist(mean, stddev);
        return dist(rng);
    }

public:
    void gen_uniform_clusters(GameState& cells) {
        int clusters = rand_int(1, 4);

        for (int c = 0; c < clusters; c++) {
            int cx = rand_int(-25, 25);
            int cy = rand_int(-8, 8);
            int radius = rand_int(1, 8);
            double density = std::clamp(rand_normal(0.26, 0.07), 0.08, 0.48);

            for (int dx = -radius; dx <= radius; dx++) {
                for (int dy = -radius; dy <= radius; dy++) {
                    int radialJitter = rand_int(-radius, radius);
                    if (dx * dx + dy * dy > radius * radius + radialJitter) {
                        continue;
                    }
                    if (rand_double(0, 1) < density) {
                        cells.reverse({ cx + dx, cy + dy });
                    }
                }
            }
        }
    }

    void gen_irregula_clusters(GameState& cells) {
        int clusters = rand_int(1, 3);

        for (int c = 0; c < clusters; c++) {
            int cx = rand_int(-25, 25);
            int cy = rand_int(-10, 10);
            int radiusx = rand_int(4, 20);
            int radiusy = rand_int(2, 10);
            double density = rand_double(0.08, 0.55);
            double jitter = rand_double(0.35, 0.90);

            for (int x = -radiusx; x <= radiusx; x++) {
                for (int y = -radiusy; y <= radiusy; y++) {
                    double nx = static_cast<double>(x) / static_cast<double>(radiusx);
                    double ny = static_cast<double>(y) / static_cast<double>(radiusy);
                    double d = nx * nx + ny * ny;
                    if (d > 1.6) {
                        continue;
                    }
                    double local = density * (1.15 - 0.70 * d) * rand_double(1.0 - jitter, 1.0);
                    local = std::clamp(local, 0.0, 0.95);
                    if (rand_double(0, 1) < local) {
                        cells.reverse({ cx + x, cy + y });
                    }
                }
            }
        }
    }

    void gen_guassian_clusters(GameState& cells) {
        int clusters = rand_int(1, 2);

        for (int c = 0; c < clusters; c++) {
            int cx = rand_int(-25, 25);
            int cy = rand_int(-8, 8);
            double sigma = rand_double(1.0, 3.0);
            int points = rand_int(5, 50);

            for (int i = 0; i < points; i++) {
                int x = cx + static_cast<int>(rand_normal(0, sigma));
                int y = cy + static_cast<int>(rand_normal(0, sigma));
                cells.reverse({ x, y });
            }
        }
    }

    void gen_sparse_fill(GameState& cells) {
        int cx = rand_int(-15, 15);
        int cy = rand_int(-8, 8);
        int radiusx = rand_int(5, 15);
        int radiusy = rand_int(3, 8);
        double density = rand_double(0.01, 0.05);
        for (int x = -radiusx; x < radiusx; x++) {
            for (int y = -radiusy; y < radiusy; y++) {
                double nx = static_cast<double>(x) / static_cast<double>(radiusx);
                double ny = static_cast<double>(y) / static_cast<double>(radiusy);
                if (nx * nx + ny * ny > rand_double(0.75, 1.15)) {
                    continue;
                }
                if (rand_double(0, 1) < density) {
                    cells.reverse({ cx + x, cy + y });
                }
            }
        }
    }

    void gen_dense_fill(GameState& cells) {
        int cx = rand_int(-25, 25);
        int cy = rand_int(-10, 10);
        int radiusx = rand_int(1, 6);
        int radiusy = rand_int(1, 4);
        double density = rand_double(0.3, 0.8);
        for (int x = -radiusx; x < radiusx; x++) {
            for (int y = -radiusy; y < radiusy; y++) {
                double nx = static_cast<double>(x) / static_cast<double>(radiusx);
                double ny = static_cast<double>(y) / static_cast<double>(radiusy);
                if (nx * nx + ny * ny > rand_double(0.85, 1.20)) {
                    continue;
                }
                if (rand_double(0, 1) < density) {
                    cells.reverse({ cx + x, cy + y });
                }
            }
        }
    }

    void gen_random_walk(GameState& cells) {
        int walkers = rand_int(1, 4);
        for (int w = 0; w < walkers; w++) {
            int x = rand_int(-25, 25);
            int y = rand_int(-12, 12);
            int steps = rand_int(12, 90);
            int brush = rand_int(0, 2);

            for (int s = 0; s < steps; s++) {
                for (int dx = -brush; dx <= brush; dx++) {
                    for (int dy = -brush; dy <= brush; dy++) {
                        if (dx * dx + dy * dy > brush * brush + 1) {
                            continue;
                        }
                        if (rand_double(0, 1) < 0.45) {
                            cells.reverse({ x + dx, y + dy });
                        }
                    }
                }

                int dir = rand_int(0, 7);
                static constexpr std::array<Coord, 8> dirs = {
                    Coord{1, 0}, Coord{-1, 0}, Coord{0, 1}, Coord{0, -1},
                    Coord{1, 1}, Coord{1, -1}, Coord{-1, 1}, Coord{-1, -1}
                };
                x += dirs[static_cast<size_t>(dir)].first;
                y += dirs[static_cast<size_t>(dir)].second;
            }
        }
    }

    void gen_periodic_grid(GameState& cells) {
        int cx = rand_int(-15, 15);
        int cy = rand_int(-8, 8);
        int radiusx = rand_int(4, 15);
        int radiusy = rand_int(2, 8);

        int px = rand_int(1, 4);
        int py = rand_int(1, 4);
        int window = rand_int(1, 3);
        double density = rand_normal(0.25, 0.08);

        int offsetx = px + window;
        int offsety = py + window;

        for (int x = 0; x < px; x++) {
            for (int y = 0; y < py; y++) {
                if (rand_double(0, 1) < density) {
                    // broadcast to all the periodic cells
                    for (int xiter = -radiusx; xiter <= radiusx; xiter += offsetx) {
                        for (int yiter = -radiusy; yiter <= radiusy; yiter += offsety) {
                            if (rand_double(0, 1) < density) {
                                cells.reverse({ cx + x + xiter, cy + y + yiter });
                            }
                        }
                    }
                }
            }
        }
    }

    void gen_filament(GameState& cells) {
        bool horizontal = rand_int(0, 1) == 0 ? true : false;
        double density = rand_normal(0.25, 0.08);

        if (horizontal) {
            int cx = rand_int(-15, 15);
            int cy = rand_int(-12, 12);
            int length = rand_int(5, 15);
            int thickness = rand_int(1, 3);
            for (int x = -length; x < length; x++) {
                for (int y = -thickness; y < thickness; y++) {
                    if (rand_double(0, 1) < density) {
                        cells.reverse({ cx + x, cy + y });
                    }
                }
            }
        }
        else {
            int cx = rand_int(-30, 30);
            int cy = rand_int(-5, 5);
            int length = rand_int(3, 10);
            int thickness = rand_int(1, 3);
            for (int x = -thickness; x < thickness; x++) {
                for (int y = -length; y < length; y++) {
                    if (rand_double(0, 1) < density) {
                        cells.reverse({ cx + x, cy + y });
                    }
                }
            }
        }
    }

    void rand_offset(GameState& cells) {
        int dx = rand_int(-10, 10);
        int dy = rand_int(-10, 10);
        double p = rand_normal(0.2, 0.06);

        // Cannot modify the set while iterating
        GameState newState;
        for (const auto& c : cells.livingCells) {
            if (rand_double(0, 1) < p) {
                newState.insert({ c.first + dx, c.second + dy });
            } else {
                newState.insert(c);
            }
        }
        cells = std::move(newState);
    }

    void rand_invert(GameState& cells) {
        int cxmin = rand_int(-30, 30);
        int cxmax = rand_int(cxmin, 30);
        int cymin = rand_int(-15, 15);
        int cymax = rand_int(cymin, 15);

        for (int x = cxmin; x <= cxmax; x++) {
            for (int y = cymin; y <= cymax; y++) {
                cells.reverse({ x, y });
            }
        }
    }

    void rand_swap(GameState& cells) {
        int cxmin = rand_int(-25, 25);
        int cxmax = rand_int(cxmin, 25);
        int cymin = rand_int(-8, 8);
        int cymax = rand_int(cymin, 8);
        int dx = rand_int(-10, 10);
        int dy = rand_int(-8, 8);

        for (int x = cxmin; x <= cxmax; x++) {
            for (int y = cymin; y <= cymax; y++) {
                bool source = cells[{ x, y }];
                bool target = cells[{ x + dx, y + dy }];
                if (source != target) {
                    if (source) {
                        cells.erase({ x, y });
                        cells.insert({ x + dx, y + dy });
                    } else {
                        cells.erase({ x + dx, y + dy });
                        cells.insert({ x, y });
                    }
                }
            }
        }
    }

    static GameState generateRandomInitialState() {
        GameState state;
        InitialStateGenerator generator;
        std::discrete_distribution<int> buildWeights = {
            20, 18, 14, 3, 8, 8, 10, 12
        };
        std::discrete_distribution<int> allWeights = {
            20, 18, 14, 3, 8, 8, 10, 12, 3, 2, 2
        };
        std::lognormal_distribution<double> iterations_dist(0.8, 1.2);
        int iterations = std::min(6, 1 + std::max(0, static_cast<int>(iterations_dist(generator.rng))));

        for (int i = 0; i < iterations; i++) {
            int choice = i == 0 ? buildWeights(generator.rng) : allWeights(generator.rng);
            switch (choice) {
                case 0:
                    generator.gen_uniform_clusters(state);
                    break;
                case 1:
                    generator.gen_irregula_clusters(state);
                    break;
                case 2:
                    generator.gen_guassian_clusters(state);
                    break;
                case 3:
                    generator.gen_sparse_fill(state);
                    break;
                case 4:
                    generator.gen_dense_fill(state);
                    break;
                case 5:
                    generator.gen_periodic_grid(state);
                    break;
                case 6:
                    generator.gen_filament(state);
                    break;
                case 7:
                    generator.gen_random_walk(state);
                    break;
                case 8:
                    generator.rand_offset(state);
                    break;
                case 9:
                    generator.rand_invert(state);
                    break;
                case 10:
                    generator.rand_swap(state);
                    break;
            }
        }

        // Keep occasional sparse starts, but avoid too many trivially empty/one-tick deaths.
        if (state.population() < 6) {
            generator.gen_dense_fill(state);
            generator.gen_random_walk(state);
        }
        if (state.population() < 4) {
            generator.gen_guassian_clusters(state);
        }

        return state;
    }
};

class Game {
private:
    GameState initialState;

    GameState earlyState;

    GameState lastState;

    GameState currentState;

    std::size_t currentStep;

    std::size_t score;

    std::size_t totalSpawn;

    std::size_t totalDeath;

    std::size_t currentSpawn;

    std::size_t currentDeath;

    std::size_t stableSteps;

    GameState lastUnstable;

    StopReason stopReason;

    std::size_t totalSteps;

    std::size_t totalPopulation;

    Coord totalSize;

    constexpr static std::size_t DEFAULT_MAX_STEPS = 10000;

    constexpr static std::size_t MAX_INITIAL_POPULATION = 1000;

    constexpr static double MIN_INITIAL_AREA = 25.0;

    constexpr static std::size_t STABLE_STEP_THRESHOLD = 16;

    bool isEmpty() const {
        return currentState.population() == 0;
    }

    bool isStable() const {
        if (currentState == initialState) {
            return false;
        }
        return currentState == lastState;
    }

    bool isOscillating() const {
        if (currentState == initialState || currentState == lastState) {
            return false;
        }
        return currentState == earlyState;
    }

    bool isTravelling() const {
        if (stableSteps <= STABLE_STEP_THRESHOLD) {
            return false;
        }
        int sizeChangeLeft = lastUnstable.min().first - currentState.min().first;
        int sizeChangeTop = lastUnstable.min().second - currentState.min().second;
        int sizeChangeRight = currentState.max().first - lastUnstable.max().first;
        int sizeChangeBottom = currentState.max().second - lastUnstable.max().second;
        constexpr int needExpand = STABLE_STEP_THRESHOLD / 4;
        return sizeChangeLeft >= needExpand || sizeChangeTop >= needExpand || sizeChangeRight >= needExpand || sizeChangeBottom >= needExpand;
    }

    void initializeRandom() {
        initialState = InitialStateGenerator::generateRandomInitialState();
        earlyState = initialState;
        lastState = initialState;
        currentState = initialState;
    }

    void simulateTotalSteps(std::size_t maxSteps) {
        GameState state = currentState;
        GameState last = lastState;
        GameState early = earlyState;
        std::size_t keepStable = stableSteps;
        GameState unstable = state;
        for (std::size_t step = currentStep; step < maxSteps + currentStep; step++) {
            state = state.next();
            if (state == last) {
                stopReason = StopReason::Stable;
                totalSteps = step + 1;
                totalSize = state.size();
                totalPopulation = state.population();
                return;
            }
            if (state == early) {
                stopReason = StopReason::Oscillating;
                totalSteps = step + 1;
                totalSize = state.size();
                totalPopulation = state.population();
                return;
            }
            if (state.population() == 0) {
                stopReason = StopReason::Extinct;
                totalSteps = step + 1;
                totalSize = state.size();
                totalPopulation = state.population();
                return;
            }

            if (state.population() == last.population()) {
                keepStable++;
            } else {
                keepStable = 0;
                unstable = state;
            }
            // No population change for a long time, but the grid size keeps expanding, likely a travelling pattern
            if (keepStable > STABLE_STEP_THRESHOLD) {
                int sizeChangeLeft = unstable.min().first - state.min().first;
                int sizeChangeTop = unstable.min().second - state.min().second;
                int sizeChangeRight = state.max().first - unstable.max().first;
                int sizeChangeBottom = state.max().second - unstable.max().second;
                constexpr int needExpand = STABLE_STEP_THRESHOLD / 4;
                if (sizeChangeLeft >= needExpand || sizeChangeTop >= needExpand || sizeChangeRight >= needExpand || sizeChangeBottom >= needExpand) {
                    stopReason = StopReason::Travelling;
                    totalSteps = step - STABLE_STEP_THRESHOLD;
                    totalSize = unstable.size();
                    totalPopulation = unstable.population();
                    return;
                }
            }
            early = last;
            last = state;
        }
        stopReason = StopReason::MaxSteps;
        totalSteps = currentStep + maxSteps;
        totalSize = state.size();
        totalPopulation = state.population();
    }

public:
    Game() : currentStep(0), score(0), totalSpawn(0), totalDeath(0), currentSpawn(0), currentDeath(0), stableSteps(0) {
        initializeRandom();
        lastUnstable = currentState;
        stopReason = StopReason::Unknown;
        totalSteps = 0;
        totalPopulation = currentState.population();
        simulateTotalSteps(DEFAULT_MAX_STEPS);
    }

    explicit Game(std::size_t maxSteps) : currentStep(0), score(0), totalSpawn(0), totalDeath(0), currentSpawn(0), currentDeath(0), stableSteps(0) {
        initializeRandom();
        lastUnstable = currentState;
        stopReason = StopReason::Unknown;
        totalSteps = 0;
        totalPopulation = 0;
        simulateTotalSteps(maxSteps);
    }

    Game(const GameState& initial) :
        initialState(initial), earlyState(initial), lastState(initial), currentState(initial),
        currentStep(0), score(0), totalSpawn(0), totalDeath(0), currentSpawn(0), currentDeath(0),
        stableSteps(0), lastUnstable(initial)
    {
        stopReason = StopReason::Unknown;
        totalSteps = 0;
        totalPopulation = currentState.population();
        simulateTotalSteps(DEFAULT_MAX_STEPS);
    }

    Game(const GameState& initial, std::size_t maxSteps) :
        initialState(initial), earlyState(initial), lastState(initial), currentState(initial),
        currentStep(0), score(0), totalSpawn(0), totalDeath(0), currentSpawn(0), currentDeath(0),
        stableSteps(0), lastUnstable(initial)
    {
        stopReason = StopReason::Unknown;
        totalSteps = 0;
        totalPopulation = currentState.population();
        simulateTotalSteps(maxSteps);
    }

    void step() {
        this->earlyState = this->lastState;
        this->lastState = this->currentState;
        this->currentState = this->currentState.next();
        this->currentStep++;
        if (this->currentState.population() == this->lastState.population()) {
            this->stableSteps++;
        } else {
            this->stableSteps = 0;
            lastUnstable = currentState;
        }

        if (isEmpty()) {
            stopReason = StopReason::Extinct;
        } else if (isStable()) {
            stopReason = StopReason::Stable;
        } else if (isOscillating()) {
            stopReason = StopReason::Oscillating;
        } else if (isTravelling()) {
            stopReason = StopReason::Travelling;
        }
        currentSpawn = 0;
        currentDeath = 0;
        for (const auto& cell : currentState.livingCells) {
            if (lastState[cell] == 0) {
                totalSpawn++;
                currentSpawn++;
            }
        }
        for (const auto& cell : lastState.livingCells) {
            if (currentState[cell] == 0) {
                totalDeath++;
                currentDeath++;
            }
        }
        double activePopulation = lastState.population() == 0 ? 0.0 : (static_cast<double>(currentSpawn) + currentDeath) / (static_cast<double>(lastState.population()) + currentDeath);
        if (activePopulation > 0.6) {
            score += 5;
        } else if (activePopulation > 0.5) {
            score += 4;
        } else if (activePopulation > 0.4) {
            score += 3;
        } else if (activePopulation > 0.3) {
            score += 2;
        } else {
            score += 1;
        }
    }

    void put(const GameState& state) {
        put(state, DEFAULT_MAX_STEPS);
    }

    void put(const GameState& state, std::size_t maxSteps) {
        this->earlyState = this->lastState;
        this->lastState = this->currentState;
        // CurrentState XOR state
        this->currentState = (this->currentState | state) - (this->currentState & state);
        this->currentStep++;
        this->stableSteps = 0;
        this->lastUnstable = this->currentState;
        stopReason = StopReason::Unknown;
        simulateTotalSteps(maxSteps);
    }

    bool isFinished() const {
        // No check travelling
        return isOscillating() || isStable() || isEmpty();
    }

    const StopReason& getStopReason() const {
        return stopReason;
    }

    std::size_t getPopulation() const {
        return currentState.population();
    }

    std::size_t getStep() const {
        return currentStep;
    }

    std::size_t getScore() const {
        return score;
    }

    std::size_t getTotalSteps() const {
        return totalSteps;
    }

    Coord getTopCell() const {
        if (currentState.livingCells.empty()) {
            return { 0, 0 };
        }
        Coord best = *currentState.livingCells.begin();
        for (const Coord& cell : currentState.livingCells) {
            if (cell.second < best.second || (cell.second == best.second && cell.first < best.first)) {
                best = cell;
            }
        }
        return best;
    }

    Coord getBottomCell() const {
        if (currentState.livingCells.empty()) {
            return { 0, 0 };
        }
        Coord best = *currentState.livingCells.begin();
        for (const Coord& cell : currentState.livingCells) {
            if (cell.second > best.second || (cell.second == best.second && cell.first > best.first)) {
                best = cell;
            }
        }
        return best;
    }

    Coord getLeftCell() const {
        if (currentState.livingCells.empty()) {
            return { 0, 0 };
        }
        Coord best = *currentState.livingCells.begin();
        for (const Coord& cell : currentState.livingCells) {
            if (cell.first < best.first || (cell.first == best.first && cell.second < best.second)) {
                best = cell;
            }
        }
        return best;
    }

    Coord getRightCell() const {
        if (currentState.livingCells.empty()) {
            return { 0, 0 };
        }
        Coord best = *currentState.livingCells.begin();
        for (const Coord& cell : currentState.livingCells) {
            if (cell.first > best.first || (cell.first == best.first && cell.second > best.second)) {
                best = cell;
            }
        }
        return best;
    }

    std::array<std::array<std::size_t, 3>, 3> getCellDistribution(Coord min, Coord max) const {
        std::array<std::array<std::size_t, 3>, 3> distribution = {};
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                distribution[i][j] = 0;
            }
        }

        for (Coord cell : currentState.livingCells) {
            std::size_t xIndex = 1;
            if (cell.first < min.first) {
                xIndex = 0;
            } else if (cell.first > max.first) {
                xIndex = 2;
            }
            std::size_t yIndex = 1;
            if (cell.second < min.second) {
                yIndex = 0;
            } else if (cell.second > max.second) {
                yIndex = 2;
            }
            distribution[yIndex][xIndex]++;
        }
        return distribution;
    }

    std::size_t renderCell(Coord min, Coord max, std::ostringstream& out) const {
        std::size_t cell_onscreen = 0;
        const char* last_color = nullptr;
        for (int y = min.second; y <= max.second; y++) {
            for (int x = min.first; x <= max.first; x++) {
                Coord cell(x, y);
                const char* cell_color = nullptr;
                char cell_char = '.';

                if (currentState[cell]) {
                    if (lastState[cell]) {
                        cell_color = ansi::reset;
                        cell_char = 'O';
                    } else {
                        cell_color = ansi::green;
                        cell_char = 'O';
                    }
                    cell_onscreen++;
                } else {
                    if (lastState[cell]) {
                        cell_color = ansi::red;
                        cell_char = '.';
                    } else {
                        cell_color = ansi::dark_gray;
                        cell_char = '.';
                    }
                }

                // Only output color change if different from last to improve performance
                if (cell_color != last_color) {
                    out << cell_color;
                    last_color = cell_color;
                }
                out << cell_char << ' ';
            }
            out << ansi::clear_line_remaining << "\n";
        }

        if (last_color != ansi::reset) {
            out << ansi::reset;
        }
        return cell_onscreen;
    }

    std::size_t renderEdit(const GameState& pendingState, Coord min, Coord max, Coord cursor, std::ostringstream& out) const {
        std::size_t cell_onscreen = 0;
        const char* last_color = nullptr;
        for (int y = min.second; y <= max.second; y++) {
            for (int x = min.first; x <= max.first; x++) {
                Coord cell(x, y);
                const char* cell_color = nullptr;
                char cell_char = '.';

                if (pendingState[cell]) {
                    if (currentState[cell]) {
                        if (x == cursor.first && y == cursor.second) {
                            cell_color = ansi::cyan;
                            cell_char = '.';
                        } else {
                            cell_color = ansi::yellow;
                            cell_char = '.';
                        }
                    } else {
                        if (x == cursor.first && y == cursor.second) {
                            cell_color = ansi::cyan;
                            cell_char = 'O';
                        } else {
                            cell_color = ansi::yellow;
                            cell_char = 'O';
                        }
                    }
                } else if (currentState[cell]) {
                    if (x == cursor.first && y == cursor.second) {
                        cell_color = ansi::cyan;
                        cell_char = 'O';
                    } else if (lastState[cell]) {
                        cell_color = ansi::reset;
                        cell_char = 'O';
                    } else {
                        cell_color = ansi::green;
                        cell_char = 'O';
                    }
                    cell_onscreen++;
                } else {
                    if (x == cursor.first && y == cursor.second) {
                        cell_color = ansi::cyan;
                        cell_char = '.';
                    } else if (lastState[cell]) {
                        cell_color = ansi::red;
                        cell_char = '.';
                    } else {
                        cell_color = ansi::dark_gray;
                        cell_char = '.';
                    }
                }

                if (cell_color != last_color) {
                    out << cell_color;
                    last_color = cell_color;
                }
                out << cell_char << ' ';
            }
            out << ansi::clear_line_remaining << "\n";
        }

        if (last_color != ansi::reset) {
            out << ansi::reset;
        }
        return cell_onscreen;
    }

    void renderText(Coord min, Coord max, std::ostringstream& out) const {
        // Render Grid size info
        // T: 14 / 100 [14%] A: (24, 30) / (114, 514) S: (-10, 10), (14, 40) V: (0, 0) (70, 35)
        double average_score_per_step = currentStep == 0 ? 0.0 : static_cast<double>(score) / currentStep;
        const char* current_step_color = ansi::reset;
        if (average_score_per_step > 4.0) current_step_color = ansi::green;
        else if (average_score_per_step > 3.5) current_step_color = ansi::reset;
        else if (average_score_per_step > 3.0) current_step_color = ansi::yellow;
        else if (average_score_per_step > 2.5) current_step_color = ansi::gold;
        else current_step_color = ansi::red;

        const char* total_step_color = ansi::reset;
        if (this->stopReason == StopReason::MaxSteps) total_step_color = ansi::blue;
        else if (this->stopReason == StopReason::Unknown) total_step_color = ansi::purple;
        else if (this->totalSteps > 3000) total_step_color = ansi::green;
        else if (this->totalSteps > 1000) total_step_color = ansi::reset;
        else if (this->totalSteps > 100) total_step_color = ansi::yellow;
        else if (this->totalSteps > 20) total_step_color = ansi::gold;
        else total_step_color = ansi::red;

        double stepPercent = totalSteps == 0 ? 0.0 : static_cast<double>(currentStep) / totalSteps;
        const char* step_percentage_color = ansi::reset;
        if (stepPercent > 1.0) step_percentage_color = ansi::blue;
        else if (stepPercent > 0.8) step_percentage_color = ansi::green;
        else if (stepPercent > 0.6) step_percentage_color = ansi::reset;
        else if (stepPercent > 0.4) step_percentage_color = ansi::yellow;
        else if (stepPercent > 0.2) step_percentage_color = ansi::gold;
        else step_percentage_color = ansi::red;

        Coord currentGridSize = currentState.size();
        Coord lastGridSize = lastState.size();
        const char* grid_size_x_color = ansi::reset;
        if (currentGridSize.first > lastGridSize.first) grid_size_x_color = ansi::green;
        else if (currentGridSize.first == lastGridSize.first) grid_size_x_color = ansi::yellow;
        else grid_size_x_color = ansi::red;
        const char* grid_size_y_color = ansi::reset;
        if (currentGridSize.second > lastGridSize.second) grid_size_y_color = ansi::green;
        else if (currentGridSize.second == lastGridSize.second) grid_size_y_color = ansi::yellow;
        else grid_size_y_color = ansi::red;

        const char* final_size_x_color = ansi::reset;
        if (stopReason == StopReason::MaxSteps) final_size_x_color = ansi::blue;
        else if (stopReason == StopReason::Travelling) final_size_x_color = ansi::dark_blue;
        else if (stopReason == StopReason::Unknown) final_size_x_color = ansi::purple;
        else if (totalSize.first > 70) final_size_x_color = ansi::red;
        else if (totalSize.first > 60) final_size_x_color = ansi::gold;
        else if (totalSize.first > 50) final_size_x_color = ansi::yellow;
        else if (totalSize.first > 40) final_size_x_color = ansi::reset;
        else final_size_x_color = ansi::green;
        const char* final_size_y_color = ansi::reset;
        if (stopReason == StopReason::MaxSteps) final_size_y_color = ansi::blue;
        else if (stopReason == StopReason::Travelling) final_size_y_color = ansi::dark_blue;
        else if (stopReason == StopReason::Unknown) final_size_y_color = ansi::purple;
        else if (totalSize.second > 35) final_size_y_color = ansi::red;
        else if (totalSize.second > 30) final_size_y_color = ansi::gold;
        else if (totalSize.second > 25) final_size_y_color = ansi::yellow;
        else if (totalSize.second > 20) final_size_y_color = ansi::reset;
        else final_size_y_color = ansi::green;

        Coord currentGridMin = currentState.min();
        Coord lastGridMin = lastState.min();
        const char* grid_min_x_color = ansi::reset;
        if (currentGridMin.first < lastGridMin.first) grid_min_x_color = ansi::green;
        else if (currentGridMin.first == lastGridMin.first) grid_min_x_color = ansi::yellow;
        else grid_min_x_color = ansi::red;
        const char* grid_min_y_color = ansi::reset;
        if (currentGridMin.second < lastGridMin.second) grid_min_y_color = ansi::green;
        else if (currentGridMin.second == lastGridMin.second) grid_min_y_color = ansi::yellow;
        else grid_min_y_color = ansi::red;

        Coord currentGridMax = currentState.max();
        Coord lastGridMax = lastState.max();
        const char* grid_max_x_color = ansi::reset;
        if (currentGridMax.first > lastGridMax.first) grid_max_x_color = ansi::green;
        else if (currentGridMax.first == lastGridMax.first) grid_max_x_color = ansi::yellow;
        else grid_max_x_color = ansi::red;
        const char* grid_max_y_color = ansi::reset;
        if (currentGridMax.second > lastGridMax.second) grid_max_y_color = ansi::green;
        else if (currentGridMax.second == lastGridMax.second) grid_max_y_color = ansi::yellow;
        else grid_max_y_color = ansi::red;

        std::array<std::array<std::size_t, 3>, 3> numCells = getCellDistribution(min, max);
        double population = currentState.population() == 0 ? 1.0 : static_cast<double>(currentState.population());
        double percentOffScreenLeft = static_cast<double>(numCells[0][0] + numCells[1][0] + numCells[2][0]) / population;
        const char* render_x_min_color = ansi::reset;
        if (percentOffScreenLeft > 0.4) render_x_min_color = ansi::red;
        else if (percentOffScreenLeft > 0) render_x_min_color = ansi::yellow;
        else render_x_min_color = ansi::green;
        double percentOffScreenUp = static_cast<double>(numCells[0][0] + numCells[0][1] + numCells[0][2]) / population;
        const char* render_y_min_color = ansi::reset;
        if (percentOffScreenUp > 0.4) render_y_min_color = ansi::red;
        else if (percentOffScreenUp > 0) render_y_min_color = ansi::yellow;
        else render_y_min_color = ansi::green;

        double percentOffScreenRight = static_cast<double>(numCells[0][2] + numCells[1][2] + numCells[2][2]) / population;
        const char* render_x_max_color = ansi::reset;
        if (percentOffScreenRight > 0.4) render_x_max_color = ansi::red;
        else if (percentOffScreenRight > 0) render_x_max_color = ansi::yellow;
        else render_x_max_color = ansi::green;
        double percentOffScreenDown = static_cast<double>(numCells[2][0] + numCells[2][1] + numCells[2][2]) / population;
        const char* render_y_max_color = ansi::reset;
        if (percentOffScreenDown > 0.4) render_y_max_color = ansi::red;
        else if (percentOffScreenDown > 0) render_y_max_color = ansi::yellow;
        else render_y_max_color = ansi::green;

        out << ansi::clear_line << "T: " << current_step_color << this->currentStep << ansi::reset << " / " << total_step_color << this->totalSteps
            << step_percentage_color << " [" << static_cast<int>(stepPercent * 100) << "%] " << ansi::reset
            << "A: (" << grid_size_x_color << currentGridSize.first << ansi::reset << ", " << grid_size_y_color << currentGridSize.second << ansi::reset << ") / ("
            << final_size_x_color << totalSize.first << ansi::reset << ", " << final_size_y_color << totalSize.second << ansi::reset << ") "
            << "S: (" << grid_min_x_color << currentGridMin.first << ansi::reset << ", " << grid_min_y_color << currentGridMin.second << ansi::reset << "), "
            << "(" << grid_max_x_color << currentGridMax.first << ansi::reset << ", " << grid_max_y_color << currentGridMax.second << ansi::reset << ") "
            << "V: (" << render_x_min_color << min.first << ansi::reset << ", " << render_y_min_color << min.second << ansi::reset << "), "
            << "(" << render_x_max_color << max.first << ansi::reset << ", " << render_y_max_color << max.second << ansi::reset << ")"
            << ansi::clear_line_remaining << "\n";

        // Render Population info
        // P: 421 / 5684 / 16384 [17%] O: 16 X: 15 [3%] +4879 -4732 [(17, 12, 22), (8, 315, 3), (22, 14, 8)]
        double populationChangePercent = (initialState.population() == totalPopulation) ? 0.0 : (static_cast<double>(currentState.population()) - totalPopulation) / (static_cast<double>(totalPopulation) - initialState.population());
        const char* population_number_color = ansi::reset;
        if (populationChangePercent > 4.0 || populationChangePercent < -4.0) population_number_color = ansi::red;
        else if (populationChangePercent > 2.0 || populationChangePercent < -2.0) population_number_color = ansi::gold;
        else if (populationChangePercent > 1.0 || populationChangePercent < -1.0) population_number_color = ansi::yellow;
        else if (populationChangePercent > 0.5 || populationChangePercent < -0.5) population_number_color = ansi::reset;
        else population_number_color = ansi::green;

        const char* total_population_color = ansi::reset;
        if (totalPopulation > initialState.population() * 2) total_population_color = ansi::green;
        else if (totalPopulation > initialState.population()) total_population_color = ansi::reset;
        else if (totalPopulation == initialState.population()) total_population_color = ansi::yellow;
        else if (totalPopulation * 2 >= initialState.population()) total_population_color = ansi::gold;
        else total_population_color = ansi::red;

        double gridSizePercent = (totalSize.first == 0 || totalSize.second == 0) ? 0.0 : static_cast<double>(currentState.area()) / (totalSize.first * totalSize.second);
        const char* grid_size_percent_color = ansi::reset;
        if (gridSizePercent > 16.0 || gridSizePercent < 0.04) grid_size_percent_color = ansi::red;
        else if (gridSizePercent > 9.0 || gridSizePercent < 0.16) grid_size_percent_color = ansi::gold;
        else if (gridSizePercent > 4.0 || gridSizePercent < 0.36) grid_size_percent_color = ansi::yellow;
        else if (gridSizePercent > 1.0 || gridSizePercent < 0.64) grid_size_percent_color = ansi::reset;
        else grid_size_percent_color = ansi::green;

        const char* population_percent_color = ansi::reset;
        if (currentState.density() > 0.15 && currentState.density() <= 0.25) population_percent_color = ansi::green;
        else if (currentState.density() > 0.10 && currentState.density() <= 0.35) population_percent_color = ansi::reset;
        else if (currentState.density() > 0.05 && currentState.density() <= 0.45) population_percent_color = ansi::yellow;
        else if (currentState.density() > 0.01 && currentState.density() <= 0.55) population_percent_color = ansi::gold;
        else population_percent_color = ansi::red;

        const char* population_change_color = ansi::reset;
        if (currentSpawn > currentDeath) population_change_color = ansi::green;
        else if (currentSpawn < currentDeath) population_change_color = ansi::red;
        else population_change_color = ansi::yellow;

        double activePopulationPercent = population == 0 ? 0.0 : (static_cast<double>(currentSpawn) + currentDeath) / (static_cast<double>(population) + currentDeath);
        const char* population_activity_color = ansi::reset;
        if (activePopulationPercent > 0.6) population_activity_color = ansi::green;
        else if (activePopulationPercent > 0.5) population_activity_color = ansi::reset;
        else if (activePopulationPercent > 0.4) population_activity_color = ansi::yellow;
        else if (activePopulationPercent > 0.3) population_activity_color = ansi::gold;
        else population_activity_color = ansi::red;

        const char* population_summary_color = ansi::reset;
        if (totalSpawn - totalDeath > (currentSpawn - currentDeath) * currentStep) population_summary_color = ansi::green;
        else if (totalSpawn - totalDeath < (currentSpawn - currentDeath) * currentStep) population_summary_color = ansi::red;
        else population_summary_color = ansi::yellow;

        out << ansi::clear_line << "P: " << population_number_color << currentState.population() << ansi::reset << " / "
            << total_population_color << totalPopulation << ansi::reset << " / "
            << grid_size_percent_color << currentState.area() << ansi::reset
            << population_percent_color << " [" << static_cast<int>(currentState.density() * 100) << "%] "
            << population_change_color << "O: " << currentSpawn << " X: " << currentDeath
            << population_activity_color << " [" << static_cast<int>(activePopulationPercent * 100) << "%] "
            << population_summary_color << "+" << totalSpawn << " -" << totalDeath << " " << ansi::reset
            << "[";

        for (std::size_t y = 0; y < 3; y++) {
            out << "(";
            for (std::size_t x = 0; x < 3; x++) {
                const char* cell_dist_color = ansi::reset;
                double percent = currentState.population() == 0 ? 0.0 : static_cast<double>(numCells[y][x]) / currentState.population();
                if (x == 1 && y == 1) {
                    if (percent < 0.8) cell_dist_color = ansi::red;
                    else if (percent < 1.0) cell_dist_color = ansi::yellow;
                    else cell_dist_color = ansi::green;
                }
                else {
                    if (percent > 0.2) cell_dist_color = ansi::red;
                    else if (percent > 0) cell_dist_color = ansi::yellow;
                    else cell_dist_color = ansi::green;
                }
                out << cell_dist_color << numCells[y][x] << ansi::reset;
                if (x < 2) {
                    out << ", ";
                }
            }
            out << ")";
            if (y < 2) {
                out << ", ";
            }
        }
        out << "]" << ansi::clear_line_remaining << "\n";
    }

    void render(Coord min, Coord max, std::ostringstream& out) const {
        this->renderCell(min, max, out);
        this->renderText(min, max, out);
    }

    void exportCellGrid(Coord min, Coord max, std::ostream& out) const {
        for (int y = min.second; y <= max.second; y++) {
            for (int x = min.first; x <= max.first; x++) {
                const Coord cell(x, y);
                out << (currentState[cell] ? 'O' : '.') << ' ';
            }
            out << "\n";
        }
    }

    void render(const GameState& pendingState, Coord min, Coord max, Coord cursor, std::ostringstream& out) const {
        this->renderEdit(pendingState, min, max, cursor, out);
        this->renderText(min, max, out);
    }
};

class KeyboardInput {
private:
#ifndef _WIN32
    struct termios orig_termios;
    int orig_flags;
    bool initialized = false;
#endif

public:
    // For emergency cleanup from signal handlers
    void force_restore() {
#ifndef _WIN32
        if (initialized) {
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
            fcntl(STDIN_FILENO, F_SETFL, orig_flags);
            initialized = false;
        }
#endif
    }

private:
#ifndef _WIN32
    void enable_raw_mode() {
        if (initialized) return;

        // Save original terminal settings
        tcgetattr(STDIN_FILENO, &orig_termios);

        // Set up raw mode (disable echo/canonical mode; keep ISIG so Ctrl+C still sends SIGINT)
        struct termios raw = orig_termios;
        // Disable echo and canonical mode
        raw.c_lflag &= ~(ECHO | ICANON);  
        // Disable software flow control (Ctrl+S/Ctrl+Q) to avoid output freezes on Linux.
        raw.c_iflag &= ~(IXON | IXOFF | ICRNL);
        // Non-blocking read (for input thread polling)
        raw.c_cc[VMIN] = 0;  
        // No timeout 
        raw.c_cc[VTIME] = 0;  
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

        // Set non-blocking mode on stdin
        orig_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, orig_flags | O_NONBLOCK);

        initialized = true;
    }

    void disable_raw_mode() {
        if (!initialized) return;

        // Restore original terminal settings
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        fcntl(STDIN_FILENO, F_SETFL, orig_flags);

        initialized = false;
    }
#endif

public:
    KeyboardInput() {
#ifndef _WIN32
        enable_raw_mode();
#endif
    }

    ~KeyboardInput() {
#ifndef _WIN32
        disable_raw_mode();
#endif
    }

    // Check if a key has been pressed (non-blocking)
    bool kbhit() {
#ifdef _WIN32
        return _kbhit() != 0;
#else
        fd_set fds;
        struct timeval tv;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
#endif
    }

    // Read a single character (non-blocking, for input thread)
    int getch() {
#ifdef _WIN32
        return _getch();
#else
        int ch = getchar();
        return ch;
#endif
    }

    // Read a single character (blocking, for "press any key" scenarios)
    // Returns the first printable/control char received
    int getch_blocking() {
#ifdef _WIN32
        return _getch();
#else
        // Temporarily restore terminal to blocking, read, then revert
        struct termios current;
        tcgetattr(STDIN_FILENO, &current);
        
        struct termios blocking = current;
        blocking.c_cc[VMIN] = 1;
        blocking.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &blocking);
        
        // Clear O_NONBLOCK temporarily
        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);
        
        int ch = getchar();
        
        // Restore non-blocking mode
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &current);
        
        return ch;
#endif
    }
};

class TerminalManager {
private:
    bool alternate_screen_active = false;

public:
    void enter_alternate_screen() {
        if (!alternate_screen_active) {
            std::cout << ansi::save_screen;
            std::cout << ansi::clear_all;
            std::cout << ansi::hide_cursor;
            std::cout << std::flush;
            alternate_screen_active = true;
        }
    }

    void exit_alternate_screen() {
        if (alternate_screen_active) {
            std::cout << ansi::show_cursor;
            std::cout << ansi::restore_screen;
            std::cout << std::flush;
            alternate_screen_active = false;
        }
    }

    void reset_cursor() {
        std::cout << ansi::reset_cursor;
    }

    void clear_all() {
        std::cout << ansi::clear_all;
    }

    ~TerminalManager() {
        exit_alternate_screen();
    }
};

// Global pointer for signal handlers to access and restore terminal state
static TerminalManager* g_terminalManager = nullptr;
static KeyboardInput* g_keyboardInput = nullptr;

#ifndef _WIN32
static bool write_all_stdout(const std::string& data) {
    // We must explicitly handle partial writes to avoid large frames causing stdout overflow and freezing.
    const char* ptr = data.data();
    std::size_t remaining = data.size();
    while (remaining > 0) {
        const ssize_t written = ::write(STDOUT_FILENO, ptr, remaining);
        if (written > 0) {
            ptr += static_cast<std::size_t>(written);
            remaining -= static_cast<std::size_t>(written);
            continue;
        }

        if (written < 0 && (errno == EINTR)) {
            continue;
        }
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        return false;
    }

    // Ensure terminal drains this frame before next state update to reduce tearing.
    (void)tcdrain(STDOUT_FILENO);
    return true;
}
#endif

static void signal_handler(int sig) {
    // Restore terminal state before exiting (don't delete stack-allocated objects!)
    if (g_keyboardInput) {
        g_keyboardInput->force_restore();
    }
    if (g_terminalManager) {
        g_terminalManager->exit_alternate_screen();
    }
    
    // Re-raise the signal to allow normal termination
    signal(sig, SIG_DFL);
    raise(sig);
}

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);
    CliOptions options;
    std::string errorMessage;
    if (!parseArgs(argc, argv, options, errorMessage)) {
        std::cerr << errorMessage << "\n";
        std::cerr << "Use --help for help." << std::endl;
        return 1;
    }

    if (options.showHelp) {
        printHelp();
        return 0;
    }

    GameState inputState;
    bool hasInputState = false;

    if (!options.inputFile.empty()) {
        std::ifstream file(options.inputFile);
        if (!file.is_open()) {
            std::cerr << "Failed to open input file: " << options.inputFile << std::endl;
            return 1;
        }
        hasInputState = parseInitialState(file, inputState);
        if (!hasInputState) {
            std::cerr << "No valid pattern found in input file." << std::endl;
            return 1;
        }
    } else if (options.useStdin) {
        std::cout << "Reading pattern from stdin..." << std::endl;
        hasInputState = parseInitialState(std::cin, inputState);
        if (!hasInputState) {
            std::cerr << "No valid pattern found from stdin." << std::endl;
            return 1;
        }
    }

    // Install signal handlers to restore terminal state on Ctrl+C or termination
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    KeyboardInput keyboardInput;
    TerminalManager terminalManager;
    
    // Set global pointers for signal handlers
    g_keyboardInput = &keyboardInput;
    g_terminalManager = &terminalManager;
    terminalManager.enter_alternate_screen();
    std::cout << "Loading game..." << std::endl;

    Game game = options.startInEdit
        ? (options.hasSteps ? Game(GameState(), options.steps) : Game(GameState()))
        : (options.hasSteps
            ? (hasInputState ? Game(inputState, options.steps) : Game(options.steps))
            : (hasInputState ? Game(inputState) : Game()));

    std::atomic<int> screenWidth(options.screenWidth);
    std::atomic<int> screenHeight(options.screenHeight);
    std::atomic<int> viewMinX(options.viewX);
    std::atomic<int> viewMinY(options.viewY);
    std::atomic<int> viewMaxX(options.viewX + screenWidth.load() - 1);
    std::atomic<int> viewMaxY(options.viewY + screenHeight.load() - 1);
    std::atomic<bool> stopInput(false);
    std::atomic<bool> paused(options.startInEdit);
    std::atomic<bool> requestRender(true);
    std::atomic<int> stepIntervalMs(options.animate ? static_cast<int>(options.animateIntervalMs) : 50);
    std::mutex inputSignalMutex;
    std::condition_variable inputSignalCv;

    const auto gameStartTime = std::chrono::steady_clock::now();
    std::chrono::steady_clock::duration pausedDurationTotal = std::chrono::steady_clock::duration::zero();
    bool isPauseWindowOpen = paused.load();
    std::chrono::steady_clock::time_point pauseWindowStart = gameStartTime;
    std::chrono::steady_clock::time_point lastRenderedFrameTime = gameStartTime;
    std::chrono::steady_clock::duration lastFrameDuration = std::chrono::milliseconds(stepIntervalMs.load());

    std::mutex commandMutex;
    bool commandMode = options.startInEdit;
    bool editMode = options.startInEdit;
    GameState editPendingState;
    Coord editCursor = {
        options.viewX + (options.screenWidth / 2),
        options.viewY + (options.screenHeight / 2)
    };
    std::string pendingEditKeys;
    std::string commandBuffer;
    std::string pendingCommand;
    bool hasPendingCommand = false;
    std::string commandMessage = options.startInEdit
        ? (std::string(ansi::yellow) + "EDIT cursor: " + ansi::cyan + "(" + std::to_string(editCursor.first) + ", " + std::to_string(editCursor.second) + ")" +
            ansi::yellow + ", pending: " + ansi::red + "0 Cell(s)")
        : ("Initialilze the game with " + std::to_string(game.getPopulation()) + " cells.");

    std::thread inputThread([&]() {
        constexpr int moveStep = 2;
        while (!stopInput.load()) {
            if (keyboardInput.kbhit()) {
                int ch = keyboardInput.getch();
                bool currentCommandMode = false;
                {
                    std::lock_guard<std::mutex> lock(commandMutex);
                    currentCommandMode = commandMode;
                }

                if (currentCommandMode) {
                    bool currentEditMode = false;
                    {
                        std::lock_guard<std::mutex> lock(commandMutex);
                        currentEditMode = editMode;
                    }

                    if (currentEditMode) {
                        if (ch == 0 || ch == 224) {
                            (void)keyboardInput.getch();
                            continue;
                        }
                        char key = '\0';
                        if (ch == 10 || ch == 13 || ch == 27 || ch == ' ') {
                            key = static_cast<char>(ch);
                        } else if (ch == 'w' || ch == 'W') {
                            key = 'w';
                        } else if (ch == 'a' || ch == 'A') {
                            key = 'a';
                        } else if (ch == 's' || ch == 'S') {
                            key = 's';
                        } else if (ch == 'd' || ch == 'D') {
                            key = 'd';
                        }

                        if (key != '\0') {
                            std::lock_guard<std::mutex> lock(commandMutex);
                            pendingEditKeys.push_back(key);
                            requestRender.store(true);
                            inputSignalCv.notify_one();
                        }
                        continue;
                    }

                    if (ch == 10 || ch == 13) {
                        std::lock_guard<std::mutex> lock(commandMutex);
                        if (commandBuffer.size() > 1) {
                            pendingCommand = commandBuffer;
                            hasPendingCommand = true;
                            commandMessage.clear();
                        } else {
                            std::ostringstream out;
                            out << ansi::red << "Empty command.";
                            commandMessage = out.str();
                        }
                        commandMode = false;
                        commandBuffer.clear();
                        requestRender.store(true);
                        inputSignalCv.notify_one();
                    } else if (ch == 27) {
                        std::lock_guard<std::mutex> lock(commandMutex);
                        commandMode = false;
                        commandBuffer.clear();
                        std::ostringstream out;
                        out << ansi::yellow << "Command cancelled.";
                        commandMessage = out.str();
                        requestRender.store(true);
                        inputSignalCv.notify_one();
                    } else if (ch == 8 || ch == 127) {
                        std::lock_guard<std::mutex> lock(commandMutex);
                        if (commandBuffer.size() > 1) {
                            commandBuffer.pop_back();
                        }
                        requestRender.store(true);
                        inputSignalCv.notify_one();
                    } else if (ch == 0 || ch == 224) {
                        (void)keyboardInput.getch();
                    } else if (std::isprint(static_cast<unsigned char>(ch)) != 0) {
                        std::lock_guard<std::mutex> lock(commandMutex);
                        commandBuffer.push_back(static_cast<char>(ch));
                        requestRender.store(true);
                        inputSignalCv.notify_one();
                    }
                    continue;
                }

                if (ch == 'w' || ch == 'W') {
                    viewMinY.fetch_sub(moveStep);
                    viewMaxY.fetch_sub(moveStep);
                    requestRender.store(true);
                    inputSignalCv.notify_one();
                } else if (ch == 's' || ch == 'S') {
                    viewMinY.fetch_add(moveStep);
                    viewMaxY.fetch_add(moveStep);
                    requestRender.store(true);
                    inputSignalCv.notify_one();
                } else if (ch == 'a' || ch == 'A') {
                    viewMinX.fetch_sub(moveStep);
                    viewMaxX.fetch_sub(moveStep);
                    requestRender.store(true);
                    inputSignalCv.notify_one();
                } else if (ch == 'd' || ch == 'D') {
                    viewMinX.fetch_add(moveStep);
                    viewMaxX.fetch_add(moveStep);
                    requestRender.store(true);
                    inputSignalCv.notify_one();
                } else if (ch == ' ') {
                    paused.store(!paused.load());
                    requestRender.store(true);
                    inputSignalCv.notify_one();
                } else if ((ch == '/' || ch == '\\') && paused.load()) {
                    std::lock_guard<std::mutex> lock(commandMutex);
                    commandMode = true;
                    commandBuffer = "/";
                    commandMessage.clear();
                    requestRender.store(true);
                    inputSignalCv.notify_one();
                } else if (ch == 'q' || ch == 'Q') {
                    stopInput.store(true);
                    inputSignalCv.notify_one();
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    });

    auto nextStepDue = std::chrono::steady_clock::now() + std::chrono::milliseconds(stepIntervalMs.load());

    auto setCommandMessage = [&](const std::string& message) {
        std::lock_guard<std::mutex> lock(commandMutex);
        commandMessage = message;
    };

    auto maxStepsForRuntimeAction = [&]() -> std::size_t {
        return options.hasSteps ? options.steps : 10000;
    };

    auto updateEditStatusMessage = [&]() {
        Coord cursorCopy = { 0, 0 };
        std::size_t pendingCount = 0;
        {
            std::lock_guard<std::mutex> lock(commandMutex);
            cursorCopy = editCursor;
            pendingCount = editPendingState.population();
        }
        std::ostringstream out;
        if (pendingCount == 0) {
            out << ansi::yellow << "EDIT cursor: " << ansi::cyan << "(" << cursorCopy.first << ", " << cursorCopy.second << ")"
                << ansi::yellow << ", pending: " << ansi::red << pendingCount << " Cell(s)";
        }
        else {
            out << ansi::yellow << "EDIT cursor: " << ansi::cyan << "(" << cursorCopy.first << ", " << cursorCopy.second << ")"
                << ansi::yellow << ", pending: " << ansi::green << pendingCount << " Cell(s)";
        }
        setCommandMessage(out.str());
    };

    auto syncPauseTiming = [&]() {
        const bool nowPaused = paused.load();
        if (nowPaused && !isPauseWindowOpen) {
            pauseWindowStart = std::chrono::steady_clock::now();
            isPauseWindowOpen = true;
        }
        else if (!nowPaused && isPauseWindowOpen) {
            pausedDurationTotal += (std::chrono::steady_clock::now() - pauseWindowStart);
            isPauseWindowOpen = false;
        }
    };

    auto getUptimeSeconds = [&]() -> std::size_t {
        auto pausedDuration = pausedDurationTotal;
        if (isPauseWindowOpen) {
            pausedDuration += (std::chrono::steady_clock::now() - pauseWindowStart);
        }
        auto uptime = std::chrono::steady_clock::now() - gameStartTime - pausedDuration;
        if (uptime < std::chrono::steady_clock::duration::zero()) {
            uptime = std::chrono::steady_clock::duration::zero();
        }
        return static_cast<std::size_t>(std::chrono::duration_cast<std::chrono::seconds>(uptime).count());
    };

    auto formatDurationHMS = [&](int totalSeconds) -> std::string {
        int absSeconds = totalSeconds < 0 ? -totalSeconds : totalSeconds;
        const int hours = absSeconds / 3600;
        const int minutes = (absSeconds % 3600) / 60;
        const int seconds = absSeconds % 60;
        std::ostringstream out;
        if (totalSeconds < 0) {
            out << "-";
        }
        out << std::setfill('0') << std::setw(2) << hours
            << ":" << std::setw(2) << minutes
            << ":" << std::setw(2) << seconds;
        return out.str();
    };

    auto renderNow = [&]() {
        // Copy command state atomically
        std::string localCommandBuffer;
        std::string localCommandMessage;
        bool localCommandMode = false;
        bool localEditMode = false;
        GameState localEditPendingState;
        Coord localEditCursor = { 0, 0 };
        {
            std::lock_guard<std::mutex> lock(commandMutex);
            localCommandMode = commandMode;
            localCommandBuffer = commandBuffer;
            localCommandMessage = commandMessage;
            localEditMode = editMode;
            if (localEditMode) {
                localEditPendingState = editPendingState;
                localEditCursor = editCursor;
            }
        }

        std::ostringstream frameBuffer;
        frameBuffer << ansi::reset_cursor << ansi::clear_line; // No clear, only remove loading message
        if (localEditMode) {
            game.render(localEditPendingState, { viewMinX.load(), viewMinY.load() }, { viewMaxX.load(), viewMaxY.load() }, localEditCursor, frameBuffer);
        } else {
            game.render({ viewMinX.load(), viewMinY.load() }, { viewMaxX.load(), viewMaxY.load() }, frameBuffer);
        }
        if (paused.load()) {
            if (localEditMode) {
                frameBuffer << ansi::clear_line << ansi::cyan << "[EDIT] " << ansi::reset << "WASD move, SPACE toggle, ENTER apply, ESC cancel"
                            << ansi::clear_line_remaining << "\n";
            } else {
                frameBuffer << ansi::clear_line << ansi::cyan << "[PAUSED] " << ansi::reset << "SPACE resume, '/' command mode, Q quit"
                            << ansi::clear_line_remaining << "\n";
            }
            if (localCommandMode && !localEditMode) {
                frameBuffer << ansi::clear_line << ansi::yellow << localCommandBuffer << ansi::cyan << "_"
                            << ansi::clear_line_remaining << "\n" << ansi::reset;
            }
            if (!localCommandMessage.empty()) {
                frameBuffer << ansi::clear_line << ansi::reset << localCommandMessage << ansi::reset
                            << ansi::clear_line_remaining << "\n";
            }
        } else {
            frameBuffer << ansi::clear_line << ansi::cyan << "[RUNNING] " << ansi::reset << "SPACE pause, WASD pan, Q quit"
                        << ansi::clear_line_remaining << "\n";
            const std::size_t currentUptimeSeconds = getUptimeSeconds();
            const std::size_t frameMs = static_cast<std::size_t>(std::chrono::duration_cast<std::chrono::milliseconds>(lastFrameDuration).count());
            const double frameSeconds = std::chrono::duration<double>(lastFrameDuration).count();
            const double fps = frameSeconds > 0.0 ? 1.0 / frameSeconds : 0;
            const std::size_t currentSteps = game.getStep();
            const std::size_t totalSteps = game.getTotalSteps();
            const bool totalLessThanCurrent = totalSteps < currentSteps;
            const int remainingSteps = static_cast<int>(totalSteps) - static_cast<int>(currentSteps);
            const double remainingSeconds = fps > 0 ? (static_cast<double>(remainingSteps) / fps) : 0;
            const double totalEtaSeconds = static_cast<double>(currentUptimeSeconds) + remainingSeconds;
            frameBuffer << ansi::clear_line << ansi::cyan << static_cast<std::size_t>(fps) << ansi::yellow << " fps, "
                        << ansi::cyan << frameMs << "ms" << ansi::yellow << " per frame, ETA: "
                        << ansi::cyan << formatDurationHMS(static_cast<int>(currentUptimeSeconds)) << ansi::yellow << " / "
                        << ansi::cyan << formatDurationHMS(static_cast<int>(totalEtaSeconds)) << ansi::yellow << " ("
                        << (totalLessThanCurrent ? ansi::blue : ansi::cyan) 
                        << formatDurationHMS(remainingSeconds > 0 ? static_cast<int>(remainingSeconds) : 0)
                        << ansi::yellow << " remaining)" << ansi::reset
                        << ansi::clear_line_remaining << "\n";
        }
        frameBuffer << ansi::clear_remaining;
        const std::string frame = frameBuffer.str();
#ifdef _WIN32
        // Recover from transient stream errors so rendering does not stall permanently.
        if (!std::cout.good()) {
            std::cout.clear();
        }
        std::cout.write(frame.data(), static_cast<std::streamsize>(frame.size()));
        std::cout.flush();
#else
        // Use a write-all loop on Linux so a frame is not left partially emitted.
        if (!write_all_stdout(frame)) {
            // Fallback to iostream if syscall path fails for any reason.
            if (!std::cout.good()) {
                std::cout.clear();
            }
            std::cout.write(frame.data(), static_cast<std::streamsize>(frame.size()));
            std::cout.flush();
        }
#endif
    };

    auto processCommand = [&](const std::string& rawCommand) {
        const std::vector<std::string> tokens = splitWhitespaceTokens(rawCommand);
        if (tokens.empty()) {
            std::ostringstream out;
            out << ansi::red << "Empty command.";
            setCommandMessage(out.str());
            return;
        }
        const std::string& cmd = tokens[0];

        if (cmd == "/a" || cmd == "/animate") {
            int newInterval = 50;
            if (tokens.size() >= 2) {
                std::size_t parsed = 0;
                if (!parseUnsignedSize(tokens[1], parsed) || parsed == 0 || parsed > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
                    std::ostringstream out;
                    out << ansi::red << "Invalid animate interval. Use: /a [ms]";
                    setCommandMessage(out.str());
                    return;
                }
                newInterval = static_cast<int>(parsed);
            }
            if (tokens.size() > 2) {
                std::ostringstream out;
                out << ansi::red << "Too many arguments. Use: /a [ms]";
                setCommandMessage(out.str());
                return;
            }
            stepIntervalMs.store(newInterval);
            nextStepDue = std::chrono::steady_clock::now() + std::chrono::milliseconds(newInterval);
            std::ostringstream out;
            out << ansi::green << "Animation interval set to " << ansi::cyan << newInterval << "ms" << ansi::green << ".";
            setCommandMessage(out.str());
        } else if (cmd == "/s" || cmd == "/screen") {
            if (tokens.size() != 3) {
                std::ostringstream out;
                out << ansi::red << "Invalid arguments. Use: /s <W> <H>";
                setCommandMessage(out.str());
                return;
            }
            int w = 0;
            int h = 0;
            if (!parseSignedInt(tokens[1], w) || !parseSignedInt(tokens[2], h) || w <= 0 || h <= 0) {
                std::ostringstream out;
                out << ansi::red << "Invalid screen size. W and H must be positive integers.";
                setCommandMessage(out.str());
                return;
            }
            screenWidth.store(w);
            screenHeight.store(h);
            const int minX = viewMinX.load();
            const int minY = viewMinY.load();
            viewMaxX.store(minX + w - 1);
            viewMaxY.store(minY + h - 1);
            std::ostringstream clear_screen;
            clear_screen << ansi::clear_all << ansi::reset << "Applying new screen size...";
            setCommandMessage(clear_screen.str());
            renderNow();
            std::ostringstream out;
            out << ansi::green << "Screen size set to " << ansi::cyan << w << "x" << h << ansi::green << ".";
            setCommandMessage(out.str());
        } else if (cmd == "/v" || cmd == "/view") {
            if (tokens.size() == 3) {
                int x = 0;
                int y = 0;
                if (!parseSignedInt(tokens[1], x) || !parseSignedInt(tokens[2], y)) {
                    std::ostringstream out;
                    out << ansi::red << "Invalid view position. X and Y must be integers.";
                    setCommandMessage(out.str());
                    return;
                }
                viewMinX.store(x);
                viewMinY.store(y);
                viewMaxX.store(x + screenWidth.load() - 1);
                viewMaxY.store(y + screenHeight.load() - 1);
                std::ostringstream out;
                out << ansi::green << "View top-left set to " << ansi::cyan << "(" << x << ", " << y << ")" << ansi::green << ".";
                setCommandMessage(out.str());
            } else if (tokens.size() == 2) {
                std::string dir = tokens[1];
                std::transform(dir.begin(), dir.end(), dir.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });

                Coord target = { 0, 0 };

                if (dir == "top" || dir == "up" || dir == "t" || dir == "u") {
                    target = game.getTopCell();
                } else if (dir == "bottom" || dir == "down" || dir == "b" || dir == "d") {
                    target = game.getBottomCell();
                } else if (dir == "left" || dir == "l") {
                    target = game.getLeftCell();
                } else if (dir == "right" || dir == "r") {
                    target = game.getRightCell();
                } else {
                    std::ostringstream out;
                    out << ansi::red << "Invalid view sub command. Use: /view top|bottom|left|right (or up|down).";
                    setCommandMessage(out.str());
                    return;
                }

                const int w = screenWidth.load();
                const int h = screenHeight.load();
                const int x = target.first - (w / 2);
                const int y = target.second - (h / 2);
                viewMinX.store(x);
                viewMinY.store(y);
                viewMaxX.store(x + w - 1);
                viewMaxY.store(y + h - 1);

                std::ostringstream out;
                out << ansi::green << "View top-left set to " << ansi::cyan << "(" << x << ", " << y << ")" << ansi::green 
                    << " to focus on cell at " << ansi::cyan << "(" << target.first << ", " << target.second << ")" << ansi::green << ".";
                setCommandMessage(out.str());
            } else {
                std::ostringstream out;
                out << ansi::red << "Invalid arguments. Use: /v <X> <Y> or /view top|bottom|left|right (up|down).";
                setCommandMessage(out.str());
                return;
            }
        } else if (cmd == "/f" || cmd == "/step") {
            if (tokens.size() != 1 && tokens.size() != 2) {
                std::ostringstream out;
                out << ansi::red << "Invalid arguments. Use: /step [N]";
                setCommandMessage(out.str());
                return;
            }
            std::size_t n = 0;
            if (tokens.size() == 1) {
                n = 1;
            }
            else if (!parseUnsignedSize(tokens[1], n) || n == 0) {
                std::ostringstream out;
                out << ansi::red << "Invalid step count. N must be a positive integer.";
                setCommandMessage(out.str());
                return;
            }
            setCommandMessage("Fast-forwarding...");
            renderNow();
            std::size_t executed = 0;
            for (; executed < n; executed++) {
                if (game.isFinished()) {
                    break;
                }
                game.step();
            }
            nextStepDue = std::chrono::steady_clock::now() + std::chrono::milliseconds(stepIntervalMs.load());
            std::ostringstream out;
            out << ansi::green << "Fast-forward executed " << ansi::cyan << executed << " step(s)." << ansi::green << "";
            if (game.isFinished()) {
                out << " " << ansi::yellow << "Pattern finished.";
            }
            setCommandMessage(out.str());
        } else if (cmd == "/p" || cmd == "/put") {
            if (tokens.size() < 3) {
                std::ostringstream out;
                out << ansi::red << "Invalid arguments. Use: /put <X> <Y> [pattern|@file]";
                setCommandMessage(out.str());
                return;
            }

            int x = 0;
            int y = 0;
            if (!parseSignedInt(tokens[1], x) || !parseSignedInt(tokens[2], y)) {
                std::ostringstream out;
                out << ansi::red << "Invalid coordinates. X and Y must be integers.";
                setCommandMessage(out.str());
                return;
            }

            GameState putState;
            if (tokens.size() == 3) {
                putState.insert({ x, y });
            } else {
                std::istringstream cmdStream(rawCommand);
                std::string cmdWord;
                std::string xWord;
                std::string yWord;
                cmdStream >> cmdWord >> xWord >> yWord;
                std::string patternPayload;
                std::getline(cmdStream, patternPayload);
                patternPayload = trim(patternPayload);

                if (patternPayload.empty()) {
                    std::ostringstream out;
                    out << ansi::red << "Invalid pattern payload.";
                    setCommandMessage(out.str());
                    return;
                }

                GameState parsedPattern;
                bool parsed = false;
                std::string payloadError;

                if (!patternPayload.empty() && patternPayload[0] == '@') {
                    parsed = parsePatternFromFilePath(patternPayload.substr(1), parsedPattern, payloadError);
                } else {
                    parsed = parseInlinePatternText(patternPayload, parsedPattern);
                    if (!parsed) {
                        parsed = parsePatternFromFilePath(patternPayload, parsedPattern, payloadError);
                    }
                }

                if (!parsed) {
                    std::ostringstream out;
                    if (!payloadError.empty()) {
                        out << ansi::red << payloadError;
                    } else {
                        out << ansi::red << "Failed to parse pattern. Use stdin-style syntax or /put <X> <Y> @<file>.";
                    }
                    setCommandMessage(out.str());
                    return;
                }

                for (const Coord& cell : parsedPattern.livingCells) {
                    putState.insert({ x + cell.first, y + cell.second });
                }
            }

            setCommandMessage("Applying new pattern...");
            renderNow();

            game.put(putState, maxStepsForRuntimeAction());
            nextStepDue = std::chrono::steady_clock::now() + std::chrono::milliseconds(stepIntervalMs.load());

            std::ostringstream out;
            out << ansi::green << "Put toggled " << ansi::cyan << putState.population() << " cell(s)" << ansi::green << " at offset " << ansi::cyan << "(" << x << ", " << y << ")" << ansi::green << ".";
            setCommandMessage(out.str());
        } else if (cmd == "/export") {
            if (tokens.size() < 6) {
                std::ostringstream out;
                out << ansi::red << "Invalid arguments. Use: /export <xmin> <ymin> <xmax> <ymax> <path>";
                setCommandMessage(out.str());
                return;
            }

            int xmin = 0;
            int ymin = 0;
            int xmax = 0;
            int ymax = 0;
            if (!parseSignedInt(tokens[1], xmin) || !parseSignedInt(tokens[2], ymin) ||
                !parseSignedInt(tokens[3], xmax) || !parseSignedInt(tokens[4], ymax)) {
                std::ostringstream out;
                out << ansi::red << "Invalid coordinates. xmin ymin xmax ymax must be integers.";
                setCommandMessage(out.str());
                return;
            }

            if (xmin > xmax || ymin > ymax) {
                std::ostringstream out;
                out << ansi::red << "Invalid rectangle. Require xmin <= xmax and ymin <= ymax.";
                setCommandMessage(out.str());
                return;
            }

            std::istringstream cmdStream(rawCommand);
            std::string discard;
            for (int i = 0; i < 5; i++) {
                cmdStream >> discard;
            }
            std::string exportPath;
            std::getline(cmdStream, exportPath);
            exportPath = trim(exportPath);
            if (exportPath.empty()) {
                std::ostringstream out;
                out << ansi::red << "Invalid path. Use: /export <xmin> <ymin> <xmax> <ymax> <path>";
                setCommandMessage(out.str());
                return;
            }

            setCommandMessage("Exporting cell state...");
            renderNow();

            if (exportPath.size() >= 2) {
                const char first = exportPath.front();
                const char last = exportPath.back();
                if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
                    exportPath = exportPath.substr(1, exportPath.size() - 2);
                }
            }

            std::ofstream output(exportPath, std::ios::out | std::ios::trunc);
            if (!output.is_open()) {
                std::ostringstream out;
                out << ansi::red << "Failed to open export file: " << ansi::cyan << exportPath;
                setCommandMessage(out.str());
                return;
            }

            game.exportCellGrid({ xmin, ymin }, { xmax, ymax }, output);
            output.flush();
            if (!output.good()) {
                std::ostringstream out;
                out << ansi::red << "Failed to write export file: " << ansi::cyan << exportPath;
                setCommandMessage(out.str());
                return;
            }

            const int width = xmax - xmin + 1;
            const int height = ymax - ymin + 1;
            std::ostringstream out;
            out << ansi::green << "Exported " << ansi::cyan << width << "x" << height
                << ansi::green << " grid to " << ansi::cyan << exportPath << ansi::green << ".";
            setCommandMessage(out.str());
        } else if (cmd == "/e" || cmd == "/edit") {
            if (!(tokens.size() == 1 || tokens.size() == 3)) {
                std::ostringstream out;
                out << ansi::red << "Invalid arguments. Use: /edit [X] [Y]";
                setCommandMessage(out.str());
                return;
            }

            int x = 0;
            int y = 0;
            if (tokens.size() == 3) {
                if (!parseSignedInt(tokens[1], x) || !parseSignedInt(tokens[2], y)) {
                    std::ostringstream out;
                    out << ansi::red << "Invalid cursor coordinates. X and Y must be integers.";
                    setCommandMessage(out.str());
                    return;
                }
            } else {
                const int minX = viewMinX.load();
                const int minY = viewMinY.load();
                const int w = screenWidth.load();
                const int h = screenHeight.load();
                x = minX + (w / 2);
                y = minY + (h / 2);
            }

            {
                std::lock_guard<std::mutex> lock(commandMutex);
                editMode = true;
                commandMode = true;
                commandBuffer.clear();
                pendingEditKeys.clear();
                editPendingState = GameState();
                editCursor = { x, y };
            }

            updateEditStatusMessage();
            requestRender.store(true);
            inputSignalCv.notify_one();
            return;
        } else {
            std::ostringstream out;
            out << ansi::red << "Unknown command. Supported: /animate /screen /view /step /put /export /edit";
            setCommandMessage(out.str());
        }

        requestRender.store(true);
        inputSignalCv.notify_one();
    };

    while (!stopInput.load()) {
        syncPauseTiming();

        std::string editKeys;
        bool currentEditMode = false;
        {
            std::lock_guard<std::mutex> lock(commandMutex);
            currentEditMode = editMode;
            if (currentEditMode && !pendingEditKeys.empty()) {
                editKeys.swap(pendingEditKeys);
            }
        }
        if (currentEditMode && !editKeys.empty()) {
            bool shouldRender = false;
            for (char key : editKeys) {
                if (key == 'w' || key == 'a' || key == 's' || key == 'd') {
                    int dx = 0;
                    int dy = 0;
                    if (key == 'w') {
                        dy = -1;
                    } else if (key == 's') {
                        dy = 1;
                    } else if (key == 'a') {
                        dx = -1;
                    } else if (key == 'd') {
                        dx = 1;
                    }

                    Coord cursor;
                    {
                        std::lock_guard<std::mutex> lock(commandMutex);
                        if (!editMode) {
                            continue;
                        }
                        editCursor.first += dx;
                        editCursor.second += dy;
                        cursor = editCursor;
                    }

                    int minX = viewMinX.load();
                    int minY = viewMinY.load();
                    int maxX = viewMaxX.load();
                    int maxY = viewMaxY.load();
                    const int w = screenWidth.load();
                    const int h = screenHeight.load();

                    if (cursor.first < minX) {
                        minX = cursor.first;
                        maxX = minX + w - 1;
                    } else if (cursor.first > maxX) {
                        maxX = cursor.first;
                        minX = maxX - w + 1;
                    }

                    if (cursor.second < minY) {
                        minY = cursor.second;
                        maxY = minY + h - 1;
                    } else if (cursor.second > maxY) {
                        maxY = cursor.second;
                        minY = maxY - h + 1;
                    }

                    viewMinX.store(minX);
                    viewMaxX.store(maxX);
                    viewMinY.store(minY);
                    viewMaxY.store(maxY);

                    updateEditStatusMessage();
                    shouldRender = true;
                } else if (key == ' ') {
                    {
                        std::lock_guard<std::mutex> lock(commandMutex);
                        if (!editMode) {
                            continue;
                        }
                        editPendingState.reverse(editCursor);
                    }
                    updateEditStatusMessage();
                    shouldRender = true;
                } else if (key == 27) {
                    {
                        std::lock_guard<std::mutex> lock(commandMutex);
                        editMode = false;
                        commandMode = false;
                        commandBuffer.clear();
                        pendingEditKeys.clear();
                        editPendingState = GameState();
                    }
                    std::ostringstream out;
                    out << ansi::yellow << "Edit cancelled.";
                    setCommandMessage(out.str());
                    shouldRender = true;
                } else if (key == 10 || key == 13) {
                    GameState applyState;
                    setCommandMessage("Applying edit...");
                    renderNow();
                    {
                        std::lock_guard<std::mutex> lock(commandMutex);
                        if (!editMode) {
                            continue;
                        }
                        applyState = editPendingState;
                        editMode = false;
                        commandMode = false;
                        commandBuffer.clear();
                        pendingEditKeys.clear();
                        editPendingState = GameState();
                    }

                    if (applyState.population() == 0) {
                        std::ostringstream out;
                        out << ansi::yellow << "Edit applied: no changes.";
                        setCommandMessage(out.str());
                    } else {
                        game.put(applyState, maxStepsForRuntimeAction());
                        nextStepDue = std::chrono::steady_clock::now() + std::chrono::milliseconds(stepIntervalMs.load());
                        std::ostringstream out;
                        out << ansi::green << "Edit applied: toggled " << ansi::cyan << applyState.population() << ansi::green << " cell(s).";
                        setCommandMessage(out.str());
                    }
                    shouldRender = true;
                }
            }

            if (shouldRender) {
                requestRender.store(true);
                inputSignalCv.notify_one();
            }
        }

        std::string commandToRun;
        {
            std::lock_guard<std::mutex> lock(commandMutex);
            if (hasPendingCommand) {
                commandToRun = pendingCommand;
                pendingCommand.clear();
                hasPendingCommand = false;
            }
        }
        if (!commandToRun.empty()) {
            processCommand(commandToRun);
        }

        if (requestRender.exchange(false)) {
            renderNow();
        }

        if (paused.load()) {
            std::unique_lock<std::mutex> lock(inputSignalMutex);
            inputSignalCv.wait_for(lock, std::chrono::milliseconds(16), [&]() {
                return stopInput.load() || requestRender.load() || !paused.load();
            });
            continue;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now < nextStepDue) {
            std::unique_lock<std::mutex> lock(inputSignalMutex);
            inputSignalCv.wait_until(lock, nextStepDue, [&]() {
                return stopInput.load() || requestRender.load() || paused.load();
            });
            continue;
        }

        nextStepDue = std::chrono::steady_clock::now() + std::chrono::milliseconds(stepIntervalMs.load());
        const auto renderedAt = std::chrono::steady_clock::now();
        lastFrameDuration = renderedAt - lastRenderedFrameTime;
        lastRenderedFrameTime = renderedAt;

        game.step();
        renderNow();

        if (game.isFinished()) {
            switch (game.getStopReason()) {
                case StopReason::Stable:
                    std::cout << ansi::green << "Stable, press any key to exit..." << ansi::reset << std::endl;
                    break;
                case StopReason::Extinct:
                    std::cout << ansi::red << "Empty, press any key to exit..." << ansi::reset << std::endl;
                    break;
                case StopReason::Oscillating:
                    std::cout << ansi::yellow << "Oscillating, press any key to exit..." << ansi::reset << std::endl;
                    break;
                case StopReason::Travelling:
                    std::cout << ansi::blue << "Travelling, press any key to exit..." << ansi::reset << std::endl;
                    break;
                default:
                    std::cout << ansi::reset << "Finished, press any key to exit..." << ansi::reset << std::endl;
                    break;
            }
            keyboardInput.getch_blocking();
            break;
        }
    }

    stopInput.store(true);
    inputSignalCv.notify_one();
    if (inputThread.joinable()) {
        inputThread.join();
    }

    terminalManager.exit_alternate_screen();
    return 0;
}